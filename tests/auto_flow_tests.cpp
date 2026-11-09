#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/startup.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void put32(std::uint8_t* p, std::uint32_t n) {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>(n >> (8 * i));
        p[i + 4] = static_cast<std::uint8_t>(n >> (8 * (3 - i)));
    }
}
static std::size_t record(std::uint8_t* p, std::uint32_t extent,
                          std::uint32_t bytes, bool dir,
                          const std::string& name) {
    const std::size_t n = name.size();
    const std::size_t length = 33 + n + (n % 2 == 0 ? 1 : 0);
    p[0] = static_cast<std::uint8_t>(length);
    put32(p + 2, extent);
    put32(p + 10, bytes);
    p[25] = dir ? 2 : 0;
    p[28] = p[31] = 1;
    p[32] = static_cast<std::uint8_t>(n);
    for (std::size_t i = 0; i < n; ++i) p[33 + i] = static_cast<std::uint8_t>(name[i]);
    return length;
}
static std::vector<std::uint8_t> test_disc() {
    std::vector<std::uint8_t> iso(30 * 2048, 0);
    auto* pvd = iso.data() + 16 * 2048;
    pvd[0] = 1; pvd[1] = 'C'; pvd[2] = 'D'; pvd[3] = '0';
    pvd[4] = '0'; pvd[5] = '1'; pvd[6] = 1;
    put32(pvd + 80, 30);
    const char* name = "RAYMAN2";
    std::copy(name, name + 7, pvd + 40);
    record(pvd + 156, 20, 2048, true, std::string(1, '\0'));

    auto* root = iso.data() + 20 * 2048;
    std::size_t p = record(root, 20, 2048, true, std::string(1, '\0'));
    p += record(root + p, 20, 2048, true, std::string(1, '\1'));
    p += record(root + p, 23, 4, false, "RAYMAN2.EXE;1");
    p += record(root + p, 21, 2048, true, "DLL");
    p += record(root + p, 22, 2048, true, "DATA");

    auto* dll = iso.data() + 21 * 2048;
    p = record(dll, 21, 2048, true, std::string(1, '\0'));
    p += record(dll + p, 20, 2048, true, std::string(1, '\1'));
    p += record(dll + p, 24, 6, false, "SND_CPA_MMX.INI;1");

    auto* data = iso.data() + 22 * 2048;
    p = record(data, 22, 2048, true, std::string(1, '\0'));
    p += record(data + p, 20, 2048, true, std::string(1, '\1'));
    p += record(data + p, 25, 5, false, "GAME.DAT;1");

    std::copy_n("GAME", 4, iso.data() + 23 * 2048);
    std::copy_n("AUDIO\n", 6, iso.data() + 24 * 2048);
    std::copy_n("DATA\n", 5, iso.data() + 25 * 2048);
    return iso;
}
static void write(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary);
    check(static_cast<bool>(out), "test output create");
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(out), "test output write");
}
static std::string contents(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    check(static_cast<bool>(in), "cannot read test output");
    return std::string(std::istreambuf_iterator<char>(in), {});
}

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("r2fix-auto-tests-" + std::to_string(stamp));
    fs::create_directory(root);
    try {
        r2fix::Startup app(root / "Rayman2Fix.exe");
        app.initialize();
        check(fs::exists(root / "Rayman2Fix" / "Rayman2Fix.ini"),
              "bootstrap initialization must create INI when absent");
        write(root / "Rayman2Fix" / "Rayman2Fix.dll", "BOOTSTRAP");
        check(r2fix::run_automatic(app) == 2,
              "no game or image must report missing media, not success");
        check(!fs::exists(root / "Rayman2Fix" / "rayman2.exe"),
              "missing media must not mark game installed");

        const auto bytes = test_disc();
        {
            std::ofstream out(root / "Rayman2.iso", std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            check(static_cast<bool>(out), "write sample ISO");
        }

        check(r2fix::run_automatic(app) == 2,
              "CD assets install but nonfunctional game runtime must not launch");
        const auto game = root / "Rayman2Fix";
        check(fs::exists(game / "rayman2.exe"), "auto mode installs game in root");
        check(!fs::exists(game / "DiscContent"), "no intermediate DiscContent layout");
        check(contents(game / "rayman2.exe") == "GAME", "retail exe copied unchanged");
        check(contents(game / "dll" / "snd_cpa_mmx.ini") == "AUDIO\n",
              "MMX sound settings copied from disc");
        check(contents(game / "dll" / "snd_cpa.ini") == "AUDIO\n",
              "snd_cpa.ini initialized from MMX variant");
        check(contents(game / "data" / "game.dat") == "DATA\n",
              "original game data copied in lowercase");
        check(contents(game / "Rayman2Fix.dll") == "BOOTSTRAP",
              "launcher runtime payload remains available");
        check(contents(game / "Rayman2Fix.ini") == "[Debug]\nLog=1\n",
              "user ini retained across staging commit");
        check(fs::exists(game / ".rayman2fix-cd-files.tsv"),
              "local installation receipt created");
        check(!fs::exists(root / "Rayman2Fix.installing"),
              "staging directory removed after publishing");
        check(r2fix::verify_installation(app) == 0,
              "installed game resources verify from local receipt");

        fs::remove(root / "Rayman2.iso");
        check(r2fix::run_automatic(app) == 2,
              "existing verified assets need no original CD, but launch stays blocked");

        // Same-size corruption: size-only verification would have missed it.
        write(game / "data" / "game.dat", "OTHER");
        check(r2fix::verify_installation(app) == 2,
              "same-size changed game file must fail SHA256 verification");
        check(r2fix::run_automatic(app) == 2,
              "tampered installation is not repaired or launched silently");
        check(contents(game / "data" / "game.dat") == "OTHER",
              "existing modified file preserved");

        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::cout << "Automatic CD installer workflow tests passed\n";
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "Automatic installer test failed: " << err.what() << '\n';
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
