#include "rayman2fix/disc.hpp"

#include <array>
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

static void check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
static void put32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
    for (int i = 0; i < 4; ++i) p[4 + i] = static_cast<std::uint8_t>(v >> (8 * (3 - i)));
}
static std::size_t record(std::uint8_t* p, std::uint32_t extent,
                          std::uint32_t size, std::uint8_t flags,
                          const std::string& filename) {
    const auto n = filename.size();
    const auto length = 33 + n + (n % 2 == 0 ? 1 : 0);
    p[0] = static_cast<std::uint8_t>(length);
    put32(p + 2, extent);
    put32(p + 10, size);
    p[25] = flags;
    p[28] = 1;
    p[31] = 1;
    p[32] = static_cast<std::uint8_t>(n);
    for (std::size_t i = 0; i < n; ++i) p[33 + i] = static_cast<std::uint8_t>(filename[i]);
    return length;
}
static std::vector<std::uint8_t> make_iso(const std::string& name = "DATA.TXT;1") {
    std::vector<std::uint8_t> iso(22 * 2048, 0);
    auto* pvd = iso.data() + 16 * 2048;
    pvd[0] = 1; pvd[1] = 'C'; pvd[2] = 'D'; pvd[3] = '0';
    pvd[4] = '0'; pvd[5] = '1'; pvd[6] = 1;
    const char* volume = "RAYMAN2_TEST";
    for (unsigned i = 0; volume[i] != 0; ++i) pvd[40 + i] = volume[i];
    record(pvd + 156, 20, 2048, 2, std::string(1, '\0'));
    auto* dir = iso.data() + 20 * 2048;
    std::size_t pos = record(dir, 20, 2048, 2, std::string(1, '\0'));
    pos += record(dir + pos, 20, 2048, 2, std::string(1, '\1'));
    pos += record(dir + pos, 21, 6, 0, name);
    (void)pos;
    auto* file = iso.data() + 21 * 2048;
    for (unsigned i = 0; i < 6; ++i) file[i] = "HELLO\n"[i];
    return iso;
}
static void write_file(const fs::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    check(static_cast<bool>(out), "create test file");
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(out), "write test file");
}
static bool fails(const fs::path& path) {
    try { (void)r2fix::DiscImage::open(path); }
    catch (const std::exception&) { return true; }
    return false;
}

int main() {
    const auto marker = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() /
                      ("rayman2fix-disc-tests-" + std::to_string(marker));
    fs::create_directories(root);
    try {
        const auto iso = make_iso();
        const auto iso_path = root / "test.iso";
        write_file(iso_path, iso);
        const auto parsed = r2fix::DiscImage::open(iso_path);
        check(parsed.info().files.size() == 1, "ISO file count");
        check(parsed.info().volume_id == "RAYMAN2_TEST", "ISO volume");
        check(parsed.info().total_bytes == 6, "file size");
        const auto target = root / "Rayman2Fix" / "DiscContent";
        parsed.extract(target);
        std::ifstream content(target / "DATA.TXT", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(content)), {});
        check(text == "HELLO\n", "extraction data");
        content.close();  // Windows cannot delete an open output file.
        bool refused = false;
        try { parsed.extract(target); }
        catch (const std::exception&) { refused = true; }
        check(refused, "existing directory must be refused");

        std::vector<std::uint8_t> bin(22 * 2352, 0);
        for (std::size_t i = 0; i < 22; ++i) {
            for (std::size_t j = 0; j < 2048; ++j)
                bin[i * 2352 + 16 + j] = iso[i * 2048 + j];
        }
        write_file(root / "test.bin", bin);
        {
            std::ofstream cue(root / "test.cue");
            cue << "FILE \"test.bin\" BINARY\n"
                << "  TRACK 01 MODE1/2352\n"
                << "    INDEX 01 00:00:00\n"
                << "  TRACK 02 AUDIO\n"
                << "    INDEX 01 00:10:00\n";
        }
        const auto cue = r2fix::DiscImage::open(root / "test.cue");
        check(cue.info().files.size() == 1, "CUE file count");
        check(cue.info().format.find("2352") != std::string::npos, "raw sectors");

        auto bad = make_iso("..;1");
        write_file(root / "unsafe.iso", bad);
        check(fails(root / "unsafe.iso"), "traversal must be refused");
        bad = make_iso();
        bad[16 * 2048 + 1] = 'X';
        write_file(root / "invalid.iso", bad);
        check(fails(root / "invalid.iso"), "invalid ISO descriptor must fail");

        {
            std::ofstream cuefile(root / "unsafe.cue");
            cuefile << "FILE \"../test.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n";
        }
        check(fails(root / "unsafe.cue"), "unsafe CUE path must fail");
        fs::remove_all(root);
        std::cout << "All Rayman2Fix disc parser tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
