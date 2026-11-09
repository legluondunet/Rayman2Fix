#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/game_config.hpp"
#include "rayman2fix/game_config.hpp"
#include "rayman2fix/sha256.hpp"
#include "rayman2fix/startup.hpp"

#include <algorithm>
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
    std::vector<std::uint8_t> iso(37 * 2048, 0);
    auto* pvd = iso.data() + 16 * 2048;
    pvd[0] = 1; pvd[1] = 'C'; pvd[2] = 'D'; pvd[3] = '0';
    pvd[4] = '0'; pvd[5] = '1'; pvd[6] = 1;
    put32(pvd + 80, 37);
    const char* name = "RAYMAN2";
    std::copy(name, name + 7, pvd + 40);
    record(pvd + 156, 20, 2048, true, std::string(1, '\0'));

    auto* root = iso.data() + 20 * 2048;
    std::size_t p = record(root, 20, 2048, true, std::string(1, '\0'));
    p += record(root + p, 20, 2048, true, std::string(1, '\1'));
    p += record(root + p, 23, 4, false, "RAYMAN2.EXE;1");
    p += record(root + p, 21, 2048, true, "DLL");
    p += record(root + p, 22, 2048, true, "DATA");
    p += record(root + p, 26, 4, false, "00000001.TMP;1");
    p += record(root + p, 27, 4, false, "0000040C.016;1");
    p += record(root + p, 28, 4, false, "0000040C.256;1");
    p += record(root + p, 29, 4, false, "SECDRV.SYS;1");
    p += record(root + p, 30, 4, false, "RAYMAN2.ICD;1");
    p += record(root + p, 31, 4, false, "DXPLAYERX.DLL;1");
    p += record(root + p, 33, 4, false, "CLCD32.DLL;1");
    p += record(root + p, 34, 4, false, "CLOKSPL.EXE;1");
    p += record(root + p, 35, 4, false, "DPLAYERX.DLL;1");
    p += record(root + p, 36, 4, false, "DRVMGT.DLL;1");
    const std::string original_ubi =
        "[Rayman2]\r\nLanguage=French\r\nGLI_Dll=DirectX6\r\n"
        "GLI_Mode=1 - 640 x 480 x 16\r\n[Other]\r\nKeep=present\r\n";
    p += record(root + p, 32, static_cast<std::uint32_t>(original_ubi.size()),
                false, "UBI.INI;1");

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
    std::copy_n("TEMP", 4, iso.data() + 26 * 2048);
    std::copy_n("ICON", 4, iso.data() + 27 * 2048);
    std::copy_n("LOGO", 4, iso.data() + 28 * 2048);
    std::copy_n("DRV!", 4, iso.data() + 29 * 2048);
    std::copy_n("ICD!", 4, iso.data() + 30 * 2048);
    std::copy_n("DXP!", 4, iso.data() + 31 * 2048);
    std::copy_n("CL32", 4, iso.data() + 33 * 2048);
    std::copy_n("CLOK", 4, iso.data() + 34 * 2048);
    std::copy_n("DPLY", 4, iso.data() + 35 * 2048);
    std::copy_n("DRVM", 4, iso.data() + 36 * 2048);
    std::copy(original_ubi.begin(), original_ubi.end(), iso.begin() + 32 * 2048);
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
        // The child runtime writes to the same launcher-side log while the
        // launcher waits. Verify ordering and append-only reopen behavior.
        app.log("LAUNCH-PRETEST: launcher active");
        app.suspend_logging_for_child();
        {
            std::ofstream child(root / "Rayman2Fix.log", std::ios::binary | std::ios::app);
            check(static_cast<bool>(child), "child should be able to append launcher log");
            child << "RUNTIME-HUD: child log visible\n";
            check(static_cast<bool>(child), "child runtime log write succeeds");
        }
        app.resume_logging_after_child();
        app.log("LAUNCH-POSTTEST: launcher resumed");
        const auto shared_log = contents(root / "Rayman2Fix.log");
        check(shared_log.find("LAUNCH-PRETEST: launcher active") != std::string::npos &&
              shared_log.find("RUNTIME-HUD: child log visible") != std::string::npos &&
              shared_log.find("LAUNCH-POSTTEST: launcher resumed") != std::string::npos,
              "single launcher/runtime log preserves all messages");
        check(fs::exists(root / "Rayman2Fix" / "Rayman2Fix.ini"),
              "bootstrap initialization must create INI when absent");
        write(root / "Rayman2Fix" / "Rayman2Fix.dll", "BOOTSTRAP");
        write(root / "Rayman2Fix" / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1024x768\n");
        const auto user_ini = root / "Rayman2Fix" / "Rayman2Fix.ini";
        check(r2fix::configured_fps_limit(user_ini) == 60,
              "older INI without FPSLimit uses default 60");
        write(user_ini, "[Graphics]\nFPSLimit=120\n");
        check(r2fix::configured_fps_limit(user_ini) == 120,
              "custom FPSLimit=120 read from user INI");
        write(user_ini, "[Graphics]\nFPSLimit=0\n");
        check(r2fix::configured_fps_limit(user_ini) == 0,
              "FPSLimit=0 disables frame pacing");
        write(user_ini, "[Graphics]\nFPSLimit=240\n");
        check(r2fix::configured_fps_limit(user_ini) == 240,
              "maximum FPSLimit=240 accepted");
        for (const auto* invalid : {"-1", "5", "241", "abc", "99999999999", ""}) {
            write(user_ini, std::string("[Graphics]\nFPSLimit=") + invalid + "\n");
            bool refused = false;
            try { (void)r2fix::configured_fps_limit(user_ini); }
            catch (const std::exception&) { refused = true; }
            check(refused, "invalid or unsupported FPSLimit rejected");
        }
        write(user_ini,
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1024x768\nFPSLimit=60\n");
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
        check(contents(game / "Rayman2Fix.ini") ==
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1024x768\nFPSLimit=60\n",
              "user ini retained across staging commit");
        auto ubi = contents(game / "ubi.ini");
        check(ubi.find("Language=French") != std::string::npos &&
              ubi.find("[Other]") != std::string::npos &&
              ubi.find("Keep=present") != std::string::npos,
              "CD ubi.ini custom/unknown values preserved");
        check(ubi.find("GLI_DllFile=GliVd1") != std::string::npos &&
              ubi.find("GLI_Dll=Glide2") != std::string::npos &&
              ubi.find("GLI_Mode=1 - 1024 x 768 x 16") != std::string::npos &&
              ubi.find("GLI_Dll=DirectX6") == std::string::npos,
              "Glide 2 selected by single Rayman2Fix.ini at 1024x768");
        check(contents(game / ".rayman2fix-cd-files.tsv").find("\tubi.ini") ==
                  std::string::npos,
              "managed ubi.ini must not be recorded as an immutable CD asset");
        check(fs::exists(game / ".rayman2fix-cd-files.tsv"),
              "local installation receipt created");
        // All ten exact root-only exclusions must be absent from fresh
        // installation and from the local SHA256 receipt.
        for (const auto* filename : {"00000001.tmp", "0000040c.016",
                                     "0000040c.256", "secdrv.sys",
                                     "rayman2.icd", "dxplayerx.dll",
                                     "clcd32.dll", "clokspl.exe",
                                     "dplayerx.dll", "drvmgt.dll"}) {
            check(!fs::exists(game / filename), "obsolete protection file not extracted");
            check(contents(game / ".rayman2fix-cd-files.tsv").find(filename) ==
                      std::string::npos,
                  "obsolete protection file omitted from receipt");
        }
        check(!fs::exists(root / "Rayman2Fix.installing"),
              "staging directory removed after publishing");
        check(r2fix::verify_installation(app) == 0,
              "installed game resources verify from local receipt");

        // Simulate an already-installed copy made by an older Rayman2Fix
        // build: the receipt used to list now-excluded root files.
        const std::array<std::pair<const char*, const char*>, 10> legacy = {{
            {"00000001.tmp", "TEMP"},
            {"0000040c.016", "ICON"},
            {"0000040c.256", "LOGO"},
            {"secdrv.sys", "DRV!"},
            {"rayman2.icd", "ICD!"},
            {"dxplayerx.dll", "DXP!"},
            {"clcd32.dll", "CL32"},
            {"clokspl.exe", "CLOK"},
            {"dplayerx.dll", "DPLY"},
            {"drvmgt.dll", "DRVM"}
        }};
        {
            std::ofstream receipt(game / ".rayman2fix-cd-files.tsv",
                                  std::ios::binary | std::ios::app);
            for (const auto& file : legacy) {
                receipt << "FILE\t4\t" << r2fix::sha256_text(file.second)
                        << '\t' << file.first << '\n';
                write(game / file.first, file.second);
            }
            check(static_cast<bool>(receipt), "extend old receipt for migration test");
        }
        // Also accept an obsolete file which the user had removed manually.
        fs::remove(game / "00000001.tmp");
        fs::remove(root / "Rayman2.iso");
        check(r2fix::run_automatic(app) == 2,
              "old install is migrated without disc and runtime stays gated");
        for (const auto& file : legacy) {
            check(!fs::exists(game / file.first),
                  "existing obsolete protection file removed by migration");
            check(contents(game / ".rayman2fix-cd-files.tsv").find(file.first) ==
                      std::string::npos,
                  "obsolete entry removed from old SHA256 receipt");
        }
        check(r2fix::verify_installation(app) == 0,
              "post-migration hash verification remains valid");
        check(contents(game / "data" / "game.dat") == "DATA\n",
              "migration preserves unrelated game assets");
        check(contents(game / "Rayman2Fix.ini") ==
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1024x768\nFPSLimit=60\n",
              "migration preserves user options");

        // Treat the original CD-sourced ubi.ini as a mutable game config.
        // A legacy manifest entry must be removed without deleting ubi.ini.
        {
            std::ofstream receipt(game / ".rayman2fix-cd-files.tsv",
                                  std::ios::binary | std::ios::app);
            receipt << "FILE\t" << fs::file_size(game / "ubi.ini") << "\t"
                    << r2fix::sha256_file(game / "ubi.ini") << "\tubi.ini\n";
            check(static_cast<bool>(receipt), "legacy ubi.ini receipt appended");
        }
        write(game / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1280x720\nFPSLimit=60\n");
        check(r2fix::run_automatic(app) == 2,
              "reconfigure graphics from one INI with no CD present");
        ubi = contents(game / "ubi.ini");
        check(ubi.find("GLI_Mode=1 - 1024 x 768 x 16") != std::string::npos &&
              ubi.find("GLI_Mode=1 - 1280 x 720 x 16") == std::string::npos,
              "game-facing config retains supported internal Glide 1024x768 regardless of monitor output");
        const auto selected = r2fix::configured_output_resolution(app.ini_path());
        check(selected.first == 1280 && selected.second == 720,
              "single user INI controls native output dimensions separately from internal Glide mode");
        check(ubi.find("Language=French") != std::string::npos &&
              ubi.find("Keep=present") != std::string::npos,
              "regenerated config retains other original CD options");
        check(contents(game / ".rayman2fix-cd-files.tsv").find("\tubi.ini") ==
                  std::string::npos,
              "legacy receipt no longer tracks the mutable ubi.ini");
        check(r2fix::verify_installation(app) == 0,
              "modified generated ubi.ini must not break immutable asset verification");

        fs::remove(game / "ubi.ini");
        check(r2fix::run_automatic(app) == 2,
              "missing managed ubi.ini is automatically recreated");
        check(contents(game / "ubi.ini").find("GLI_Mode=1 - 1024 x 768 x 16") !=
                  std::string::npos,
              "missing ubi.ini rebuilt in safe internal Glide 4:3 mode");
        check(r2fix::verify_installation(app) == 0,
              "recreated game config passes CD asset verification");

        // Invalid resolutions fail before rewriting any game-facing
        // configuration, and a corrected user INI works on the next start.
        const auto good_ubi = contents(game / "ubi.ini");
        write(game / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=invalid\nFPSLimit=60\n");
        bool rejected_resolution = false;
        try { r2fix::run_automatic(app); }
        catch (const std::exception&) { rejected_resolution = true; }
        check(rejected_resolution &&
              contents(game / "ubi.ini") == good_ubi,
              "invalid Rayman2Fix.ini resolution refused with legacy config unchanged");
        write(game / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1280x720\nFPSLimit=60\n");

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
