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
#include <utility>

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
        {
            const auto legacy_root=root/"legacy-migration";
            const auto old=legacy_root/"Rayman2Fix";
            fs::create_directories(old);
            write(old/"rayman2.exe","RETAIL-GAME");
            write(old/"Rayman2Fix.ini","[Debug]\nLog=1\n[User]\nKeep=unchanged\n");
            write(old/"Rayman2Fix.dll","OLD-FIX-DLL");
            write(old/"glide2x.dll","OLD-GLIDE");
            write(old/"winmm.dll","WINMM-GAME");
            write(legacy_root/"Rayman2Fix_logo_hd.rgba","OLD-ART-LEFT-IN-PLACE");
            {
                r2fix::Startup migrating(legacy_root/"Rayman2Fix.exe");
                migrating.initialize();
                const auto game=legacy_root/"rayman2";
                check(!fs::exists(old) && contents(game/"rayman2.exe")=="RETAIL-GAME",
                      "legacy game moved without disc re-extraction");
                check(contents(game/"winmm.dll")=="WINMM-GAME",
                      "legacy winmm retained beside game executable");
                check(!fs::exists(game/"rayman2fix"/"winmm.dll"),
                      "no winmm under nested fix folder");
                check(contents(game/"rayman2fix"/"Rayman2Fix.ini").find("Keep=unchanged")!=std::string::npos,
                      "existing user INI migrated without losing values");
#ifndef _WIN32
                check(contents(game/"rayman2fix"/"Rayman2Fix.dll")=="OLD-FIX-DLL",
                      "legacy runtime DLL relocated unchanged");
                check(contents(game/"rayman2fix"/"glide2x.dll")=="OLD-GLIDE",
                      "legacy Glide DLL relocated unchanged");
#endif
                check(contents(legacy_root/"Rayman2Fix_logo_hd.rgba")=="OLD-ART-LEFT-IN-PLACE",
                      "obsolete atlas is not automatically migrated");
                check(fs::is_directory(game/"rayman2fix"/"textures"),
                      "migration creates replacement textures folder");
                check(!fs::exists(game/"rayman2fix"/"textures"/"logo.rgba"),
                      "old atlas is never implicitly renamed or converted");
            }
            fs::remove_all(legacy_root);
        }
        {
            const auto collision=root/"layout-collision";
            fs::create_directories(collision/"Rayman2Fix");
            fs::create_directories(collision/"rayman2");
            write(collision/"Rayman2Fix"/"rayman2.exe","OLD");
            write(collision/"rayman2"/"rayman2.exe","NEW");
            bool refused=false;
            try {
                r2fix::Startup ambiguous(collision/"Rayman2Fix.exe");
                ambiguous.initialize();
            } catch(const std::exception&) {refused=true;}
            check(refused && fs::exists(collision/"Rayman2Fix"/"rayman2.exe") &&
                  fs::exists(collision/"rayman2"/"rayman2.exe"),
                  "two layouts cause safe refusal without overwriting data");
            fs::remove_all(collision);
        }
        r2fix::Startup app(root / "Rayman2Fix.exe");
        app.initialize();
        app.log("LAUNCH-PRETEST: launcher active");
        check(!fs::exists(root / "Rayman2Fix.log"),
              "runtime log is not created during installer initialization");
        app.prepare_runtime_log();
        {
            std::ofstream child(root / "Rayman2Fix.log",
                                std::ios::binary | std::ios::app);
            check(static_cast<bool>(child), "DLL can append to runtime log");
            child << "RUNTIME-HUD: child log visible\n";
            check(static_cast<bool>(child), "runtime log write succeeds");
        }
        app.log("LAUNCH-POSTTEST: launcher resumed");
        const auto installer_log=contents(root / "Rayman2Fix_installer.log");
        const auto runtime_log=contents(root / "Rayman2Fix.log");
        check(installer_log.find("LAUNCH-PRETEST: launcher active") != std::string::npos &&
              installer_log.find("LAUNCH-POSTTEST: launcher resumed") != std::string::npos &&
              installer_log.find("RUNTIME-HUD:") == std::string::npos &&
              runtime_log.find("RUNTIME-HUD: child log visible") != std::string::npos &&
              runtime_log.find("LAUNCH-PRETEST:") == std::string::npos,
              "installer and runtime traces must not mix");
        app.prepare_runtime_log();
        check(contents(root / "Rayman2Fix.log").find("RUNTIME-HUD:") ==
                  std::string::npos &&
              contents(root / "Rayman2Fix_installer.log").find(
                  "LAUNCH-PRETEST:") != std::string::npos,
              "next game launch clears only the dedicated runtime log");
        check(fs::exists(root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini"),
              "bootstrap initialization must create INI when absent");
        check(fs::is_directory(root / "rayman2" / "rayman2fix" / "textures"),
              "bootstrap creates a textures folder for replacement images");
        check(contents(root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini").find("Force4x3=0") != std::string::npos &&
              contents(root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini").find("SkipCinematics=0") != std::string::npos &&
              contents(root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini").find("RemoveDialogueBackgrounds=0") != std::string::npos &&
              contents(root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini").find("RemoveCinematicBars=0") != std::string::npos &&
               contents(root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini").find("MusicInPauseMenu=0") != std::string::npos,
              "bootstrap provides independent disabled visual options");
        write(root / "rayman2" / "rayman2fix" / "Rayman2Fix.dll", "BOOTSTRAP");
        write(root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1024x768\n");
        const auto user_ini = root / "rayman2" / "rayman2fix" / "Rayman2Fix.ini";
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
        write(user_ini, "[Graphics]\nResolution=1920x1080\nForce4x3=1\n"
                        "[Features]\nSkipCinematics=1\n");
        check(r2fix::configured_force_4_3(user_ini) &&
              r2fix::configured_skip_cinematics(user_ini),
              "4:3 and cinematic skip opt-in recognized");
        check(!r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              !r2fix::configured_remove_cinematic_bars(user_ini),
              "both visual filters disabled when absent");
        check(!r2fix::configured_music_in_pause_menu(user_ini),
              "music is disabled when no MusicInPauseMenu setting is present");
        write(user_ini,"[Features]\nMusicInPauseMenu=1\nRemoveDialogueBackgrounds=1\nRemoveCinematicBars=1\n");
        check(r2fix::configured_music_in_pause_menu(user_ini) &&
              r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              r2fix::configured_remove_cinematic_bars(user_ini),
              "opt-in ESC music coexists with visual filters");
        write(user_ini,"[Features]\nMusicInPauseMenu=0\n");
        check(!r2fix::configured_music_in_pause_menu(user_ini),
              "explicit ESC music opt-out works");
        write(user_ini,"[Graphics]\nResolution=1920x1080\nForce4x3=1\n"
                       "[Features]\nSkipCinematics=1\nRemoveDialogueBackgrounds=1\n"
                       "RemoveCinematicBars=0\n");
        check(r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              !r2fix::configured_remove_cinematic_bars(user_ini),
              "dialogue-only opt-in does not enable cinematic filter");
        write(user_ini,"[Features]\nRemoveDialogueBackgrounds=0\nRemoveCinematicBars=1\n");
        check(!r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              r2fix::configured_remove_cinematic_bars(user_ini),
              "cinematic-only opt-in does not enable dialogue filter");
        write(user_ini,"[Graphics]\nResolution=1920x1080\nForce4x3=1\n"
                       "[Features]\nSkipCinematics=1\nRemoveDialogueBackgrounds=1\n"
                       "RemoveCinematicBars=1\n");
        check(r2fix::configured_render_resolution(user_ini) ==
              std::make_pair(1440u,1080u),"1920x1080: 4:3 content area 1440x1080");
        write(user_ini, "[Graphics]\nResolution=1280x800\nForce4x3=1\n");
        check(r2fix::configured_render_resolution(user_ini) ==
              std::make_pair(1064u,798u),"1280x800 exact 4:3");
        write(user_ini, "[Graphics]\nResolution=1920x1080\nForce4x3=0\n");
        check(r2fix::configured_render_resolution(user_ini) ==
              std::make_pair(1920u,1080u),"default wide unchanged");
        for(const auto* bad:{"2","yes",""}) {
            write(user_ini,std::string("[Graphics]\nForce4x3=")+bad+"\n");
            bool refused=false;
            try{(void)r2fix::configured_force_4_3(user_ini);}
            catch(const std::exception&){refused=true;}
            check(refused,"invalid 4:3 flag rejected");
            write(user_ini,std::string("[Features]\nSkipCinematics=")+bad+"\n");
            refused=false;
            try{(void)r2fix::configured_skip_cinematics(user_ini);}
            catch(const std::exception&){refused=true;}
            check(refused,"invalid cinematic flag rejected");
            write(user_ini,std::string("[Features]\nRemoveDialogueBackgrounds=")+bad+"\n");
            refused=false;
            try{(void)r2fix::configured_remove_dialogue_backgrounds(user_ini);}
            catch(const std::exception&){refused=true;}
            check(refused,"invalid dialogue flag rejected");
            write(user_ini,std::string("[Features]\nRemoveCinematicBars=")+bad+"\n");
            refused=false;
            try{(void)r2fix::configured_remove_cinematic_bars(user_ini);}
            catch(const std::exception&){refused=true;}
            check(refused,"invalid cinematic bars flag rejected");
            write(user_ini,std::string("[Features]\nMusicInPauseMenu=")+bad+"\n");
            refused=false;
            try{(void)r2fix::configured_music_in_pause_menu(user_ini);}
            catch(const std::exception&){refused=true;}
            check(refused,"invalid ESC music flag rejected");
        }
        const std::string bom="\xef\xbb\xbf";
        write(user_ini,bom+"[Debug]\r\nLog=0\r\n"
              "[Graphics]\r\nResolution=1024x768\r\nFPSLimit=60\r\n"
              "; comment\r\n[Personal]\r\nSetting=keep\r\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        const auto migrated=contents(user_ini);
        check(migrated.find(bom)==0 &&
              migrated.find("Log=0\r\n")!=std::string::npos &&
              migrated.find("Setting=keep\r\n")!=std::string::npos &&
              migrated.find("; comment\r\n")!=std::string::npos &&
              migrated.find("Force4x3=0\r\n")!=std::string::npos &&
              migrated.find("[Features]\r\n")!=std::string::npos &&
              migrated.find("SkipCinematics=0\r\n")!=std::string::npos &&
              migrated.find("RemoveDialogueBackgrounds=0\r\n")!=std::string::npos &&
              migrated.find("RemoveCinematicBars=0\r\n")!=std::string::npos &&
               migrated.find("MusicInPauseMenu=0\r\n")!=std::string::npos,
              "existing INI migration retains BOM, CRLF, custom values");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(contents(user_ini)==migrated,"migration is idempotent");
        write(user_ini,"[Features]\nRemoveBlackFrames=1\n"
                       "RemoveCinematicBars=0\n; user setting retained\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        const auto legacy_migrated=contents(user_ini);
        check(legacy_migrated.find("RemoveBlackFrames=")==std::string::npos &&
              legacy_migrated.find("RemoveDialogueBackgrounds=1")!=std::string::npos &&
              legacy_migrated.find("RemoveCinematicBars=0")!=std::string::npos &&
              legacy_migrated.find("; user setting retained")!=std::string::npos &&
              r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              !r2fix::configured_remove_cinematic_bars(user_ini),
              "legacy combined opt-in migrated, explicit cinematic override kept");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(contents(user_ini)==legacy_migrated,"legacy migration is idempotent");
        write(user_ini,"[Features]\nRemoveBlackFrames=1\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              r2fix::configured_remove_cinematic_bars(user_ini),
              "legacy enabled setting migrates to both enabled");
        write(user_ini,"[Features]\nRemoveBlackFrames=bogus\n");
        bool legacy_refused=false;
        try{r2fix::ensure_rayman2fix_ini_options(user_ini);}
        catch(const std::exception&){legacy_refused=true;}
        check(legacy_refused && contents(user_ini).find("RemoveBlackFrames=bogus")!=std::string::npos,
              "invalid legacy setting rejected without altering INI");
        write(user_ini,
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1024x768\nFPSLimit=60\n");
        // winmm is game-facing: preserve it beside rayman2.exe.
        write(root / "rayman2" / "winmm.dll", "WINMM");
        write(root / "rayman2" / "rayman2fix" / "textures" / "logo.rgba",
              "NEW-HD-LOGO");
        write(root / "rayman2" / "rayman2fix" / "textures" / "custom.rgba",
              "USER-IMAGE");
        check(r2fix::run_automatic(app) == 2,
              "no game or image must report missing media, not success");
        check(!fs::exists(root / "rayman2" / "rayman2.exe"),
              "missing media must not mark game installed");

        const auto bytes = test_disc();
        {
            std::ofstream out(root / "Rayman2.iso", std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            check(static_cast<bool>(out), "write sample ISO");
        }

        // A file named Rayman2.exe beside the launcher must be SHA256
        // allowlisted; arbitrary local EXEs may never replace CD content.
        write(root / "Rayman2.exe", "NOT_THE_APPROVED_DECRYPTED_EXECUTABLE");
        bool rejected_local_exe = false;
        try { (void)r2fix::run_automatic(app); }
        catch(const std::exception&) { rejected_local_exe = true; }
        check(rejected_local_exe &&
              !fs::exists(root / "rayman2" / "rayman2.exe") &&
              !fs::exists(root / "rayman2.installing"),
              "invalid local executable rejected before publishing original CD files");
        fs::remove(root / "Rayman2.exe");
        check(r2fix::run_automatic(app) == 2,
              "CD assets install but nonfunctional game runtime must not launch");
        const auto game = root / "rayman2";
        check(fs::exists(game / "rayman2.exe"), "auto mode installs game in rayman2");
        check(!fs::exists(game / "DiscContent"), "no intermediate DiscContent layout");
        check(contents(game / "rayman2.exe") == "GAME", "retail exe copied unchanged");
        check(contents(game / "winmm.dll") == "WINMM",
              "game-facing winmm preserved across staging commit");
        check(!fs::exists(game / "rayman2fix" / "winmm.dll"),
              "winmm must not be installed under rayman2fix");
        check(fs::is_directory(game / "rayman2fix"),
              "mod directory created under rayman2");
        check(contents(game / "rayman2fix" / "textures" / "logo.rgba") ==
              "NEW-HD-LOGO", "HD logo retained during CD extraction");
        check(contents(game / "rayman2fix" / "textures" / "custom.rgba") ==
              "USER-IMAGE", "other replacement images retained");
        check(contents(game / "dll" / "snd_cpa_mmx.ini") == "AUDIO\n",
              "MMX sound settings copied from disc");
        check(contents(game / "dll" / "snd_cpa.ini") == "AUDIO\n",
              "snd_cpa.ini initialized from MMX variant");
        check(contents(game / "data" / "game.dat") == "DATA\n",
              "original game data copied in lowercase");
        check(contents(game / "rayman2fix" / "Rayman2Fix.dll") == "BOOTSTRAP",
              "launcher runtime payload remains available");
        check(contents(game / "rayman2fix" / "Rayman2Fix.ini") ==
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
        check(contents(game / "rayman2fix" / ".rayman2fix-cd-files.tsv").find("\tubi.ini") ==
                  std::string::npos,
              "managed ubi.ini must not be recorded as an immutable CD asset");
        check(fs::exists(game / "rayman2fix" / ".rayman2fix-cd-files.tsv"),
              "local installation receipt created");
        // All eight root-only exclusions are absent from a fresh installation
        // and its SHA256 receipt. Similar but unrelated filenames survive.
        for (const auto* filename : {"00000001.tmp", "0000040c.016",
                                     "0000040c.256", "secdrv.sys",
                                     "rayman2.icd", "clcd32.dll",
                                     "clokspl.exe", "drvmgt.dll"}) {
            check(!fs::exists(game / filename), "obsolete protection file not extracted");
            check(contents(game / "rayman2fix" / ".rayman2fix-cd-files.tsv").find(filename) ==
                      std::string::npos,
                  "obsolete protection file omitted from receipt");
        }
        for (const auto* name : {"dxplayerx.dll", "dplayerx.dll"}) {
            check(fs::exists(game / name),
                  "unrelated lookalike root names must survive installation");
            check(contents(game / "rayman2fix" / ".rayman2fix-cd-files.tsv").find(name) !=
                      std::string::npos,
                  "unrelated lookalike root names stay in SHA256 receipt");
        }
        check(!fs::exists(root / "rayman2.installing"),
              "staging directory removed after publishing");
        check(r2fix::verify_installation(app) == 0,
              "installed game resources verify from local receipt");

        // Simulate an already-installed copy made by an older Rayman2Fix
        // build: the receipt used to list now-excluded root files.
        const std::array<std::pair<const char*, const char*>, 8> legacy = {{
            {"00000001.tmp", "TEMP"},
            {"0000040c.016", "ICON"},
            {"0000040c.256", "LOGO"},
            {"secdrv.sys", "DRV!"},
            {"rayman2.icd", "ICD!"},
            {"clcd32.dll", "CL32"},
            {"clokspl.exe", "CLOK"},
            {"drvmgt.dll", "DRVM"}
        }};
        {
            std::ofstream receipt(game / "rayman2fix" / ".rayman2fix-cd-files.tsv",
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
            check(contents(game / "rayman2fix" / ".rayman2fix-cd-files.tsv").find(file.first) ==
                      std::string::npos,
                  "obsolete entry removed from old SHA256 receipt");
        }
        check(r2fix::verify_installation(app) == 0,
              "post-migration hash verification remains valid");
        check(fs::exists(game / "dxplayerx.dll") &&
              fs::exists(game / "dplayerx.dll"),
              "lookalike root files remain untouched during migration");
        check(contents(game / "data" / "game.dat") == "DATA\n",
              "migration preserves unrelated game assets");
        check(contents(game / "rayman2fix" / "Rayman2Fix.ini") ==
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=1024x768\nFPSLimit=60\n",
              "migration preserves user options");

        // Treat the original CD-sourced ubi.ini as a mutable game config.
        // A legacy manifest entry must be removed without deleting ubi.ini.
        {
            std::ofstream receipt(game / "rayman2fix" / ".rayman2fix-cd-files.tsv",
                                  std::ios::binary | std::ios::app);
            receipt << "FILE\t" << fs::file_size(game / "ubi.ini") << "\t"
                    << r2fix::sha256_file(game / "ubi.ini") << "\tubi.ini\n";
            check(static_cast<bool>(receipt), "legacy ubi.ini receipt appended");
        }
        write(game / "rayman2fix" / "Rayman2Fix.ini",
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
        check(contents(game / "rayman2fix" / ".rayman2fix-cd-files.tsv").find("\tubi.ini") ==
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
        write(game / "rayman2fix" / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=invalid\nFPSLimit=60\n");
        bool rejected_resolution = false;
        try { r2fix::run_automatic(app); }
        catch (const std::exception&) { rejected_resolution = true; }
        check(rejected_resolution &&
              contents(game / "ubi.ini") == good_ubi,
              "invalid Rayman2Fix.ini resolution refused with legacy config unchanged");
        write(game / "rayman2fix" / "Rayman2Fix.ini",
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
