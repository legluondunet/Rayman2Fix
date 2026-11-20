#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/game_config.hpp"
#include "rayman2fix/gameframe.hpp"
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
    std::vector<std::uint8_t> iso(38 * 2048, 0);
    auto* pvd = iso.data() + 16 * 2048;
    pvd[0] = 1; pvd[1] = 'C'; pvd[2] = 'D'; pvd[3] = '0';
    pvd[4] = '0'; pvd[5] = '1'; pvd[6] = 1;
    put32(pvd + 80, 38);
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
    p += record(root + p, 37, 4, false, "LOADER.INI;1");
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
    std::copy_n("RAYM", 4, iso.data() + 37 * 2048);
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
        // No migration: previously installed trees are refused and untouched.
        {
            const auto oldroot=root/"legacy-refusal";
            fs::create_directories(oldroot/"rayman2");
            write(oldroot/"rayman2"/"rayman2.exe","OLD-GAME");
            bool refused=false;
            try {
                r2fix::Startup old(oldroot/"Rayman2Fix.exe");
                old.initialize();
            } catch(const std::exception&) {refused=true;}
            check(refused &&
                  contents(oldroot/"rayman2"/"rayman2.exe")=="OLD-GAME" &&
                  !fs::exists(oldroot/"game") &&
                  !fs::exists(oldroot/"Rayman2Fix.ini"),
                  "old installations are rejected without migration or mutation");
            fs::remove_all(oldroot);
        }
        // First install enables diagnostics and retains both session logs;
        // only successful verified game completion consumes this marker.
        {
            const auto test_root=root/"first-session-logging";
            fs::create_directories(test_root);
            const auto ini=test_root/"Rayman2Fix.ini";
            const auto install_log=test_root/"Rayman2Fix_installer.log";
            const auto runtime_log=test_root/"Rayman2Fix.log";
            {
                r2fix::Startup first(test_root/"Rayman2Fix.exe");
                first.initialize();
                check(contents(ini).find("Log=1")!=std::string::npos &&
                      contents(ini).find("auto-disable logs after first successful verified game run")!=
                          std::string::npos,
                      "fresh installation starts with one-shot Log=1");
                check(fs::exists(install_log) && !fs::exists(runtime_log),
                      "installer-only stage writes installer log, not runtime log");
                // An installation attempt/--install-only must leave the marker
                // until the *first actual successful verified game session*.
                const auto before=contents(ini);
                first.log("INSTALL-ONLY: no game session yet");
                check(contents(ini)==before,
                      "installer-only work must not auto-disable first session logs");
                first.prepare_runtime_log();
                const auto installer_before=contents(install_log);
                const auto runtime_before=contents(runtime_log);
                check(r2fix::finish_first_session_logging(ini),
                      "successful verified session consumes one-shot logging marker");
                check(contents(ini).find("Log=0")!=std::string::npos &&
                      contents(ini).find("auto-disable logs after first successful verified game run")==
                          std::string::npos,
                      "first successful game session automatically stores Log=0");
                check(contents(install_log)==installer_before &&
                      contents(runtime_log)==runtime_before,
                      "the two first-session logs remain available after opt-out");
                const auto completed=contents(ini);
                check(!r2fix::finish_first_session_logging(ini) &&
                      contents(ini)==completed,
                      "automatic opt-out runs only once");
                // User-set Log=1 without the one-shot marker remains untouched.
                write(ini, "[Debug]\nLog=1\n[Personal]\nKeep=manual\n");
                const auto manual=contents(ini);
                check(!r2fix::finish_first_session_logging(ini) &&
                      contents(ini)==manual,
                      "manually reenabled logging is never auto-disabled");
                write(ini, "[Debug]\nLog=0\n");
            }
            {
                r2fix::Startup next(test_root/"Rayman2Fix.exe");
                next.initialize();
                check(!next.logging_enabled() &&
                      !fs::exists(install_log) && !fs::exists(runtime_log),
                      "later Log=0 runs do not generate logs and clean stale logs");
            }
            // Preserve BOM, CRLF and unrelated options on first-session edit.
            const std::string bom="\xef\xbb\xbf";
            write(ini,bom+"[Debug]\r\nLog=1\r\n"
                      "; Rayman2Fix: auto-disable logs after first successful verified game run\r\n"
                      "[Personal]\r\nKeep=present\r\n");
            check(r2fix::finish_first_session_logging(ini) &&
                  contents(ini)==bom+"[Debug]\r\nLog=0\r\n"
                                 "[Personal]\r\nKeep=present\r\n",
                  "first-session opt-out preserves Windows BOM, CRLF and user options");
            // Never rewrite ambiguous user INIs.
            const auto ambiguous=bom+"[Debug]\r\nLog=1\r\nLog=1\r\n"
                "; Rayman2Fix: auto-disable logs after first successful verified game run\r\n";
            write(ini,ambiguous);
            check(!r2fix::finish_first_session_logging(ini) &&
                  contents(ini)==ambiguous,
                  "duplicated Log keys are left untouched");
            fs::remove_all(test_root);
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
        check(contents(root / "Rayman2Fix.log") ==
                  "RUNTIME-SESSION: fix.dll and GameFrame diagnostics; "
                  "installer=Rayman2Fix_installer.log\n" &&
              contents(root / "Rayman2Fix_installer.log").find(
                  "LAUNCH-PRETEST:") != std::string::npos,
              "next game launch starts with exactly one runtime session header "
              "and no lines from an earlier game");
        check(fs::exists(root / "Rayman2Fix.ini"),
              "bootstrap initialization must create INI when absent");
        check(contents(root/"Rayman2Fix.ini").find(
                  "jail_20 and guarded F248 for learn_10") != std::string::npos &&
              contents(root/"Rayman2Fix.ini").find(
                  "Learn_10 skip remains disabled") == std::string::npos &&
              contents(root/"Rayman2Fix.ini").find(
                  "DisableLearn10Skip") == std::string::npos,
              "fresh default INI documents both skips without exposing debug override");
        check(fs::is_directory(root / "mods" / "textures"),
              "bootstrap creates a textures folder for replacement images");
        check(fs::is_directory(root / "docs"),
              "bootstrap creates a separate docs/ folder");
        check(!fs::exists(root / "third_party"),
              "new renderer layout must not create a third_party/ folder");
        check(fs::is_regular_file(root / "docs" / "build-info.txt") &&
              !fs::exists(root / "build-info.txt"),
              "installer writes build identity exclusively to docs/");
        check(!fs::exists(root / "state"),
              "state/ is reserved for verified installation receipts");
#ifdef _WIN32
        check(fs::is_regular_file(root / "game" / "glide2x.dll") &&
              !fs::exists(root / "third_party"),
              "single EXE installs Glide into game/ beside rayman2.exe");
        check(fs::is_regular_file(root / "docs" / "README.md") &&
              !fs::exists(root / "README.md"),
              "single EXE deploys documentation only inside docs/");
        check(fs::is_regular_file(root / "docs" / "GameFrame-LICENSE-APACHE.txt") &&
              !fs::exists(root / "third_party" / "gameframe" /
                          "GameFrame-LICENSE-APACHE.txt"),
              "GameFrame Apache license remains deployed, under docs/ only");
        const auto installed_guide=contents(root / "docs" / "README.md");
        check(installed_guide.find("Installation neuve uniquement") !=
                    std::string::npos &&
              installed_guide.find("state/") != std::string::npos,
              "bundled guide describes clean installs and receipt separation");
#endif
        check(contents(root / "Rayman2Fix.ini").find("AspectRatio=Auto") != std::string::npos &&
              contents(root / "Rayman2Fix.ini").find("SkipCinematics=1") != std::string::npos &&
              contents(root / "Rayman2Fix.ini").find("RemoveDialogueBackgrounds=1") != std::string::npos &&
              contents(root / "Rayman2Fix.ini").find("RemoveCinematicBars=1") != std::string::npos &&
              contents(root / "Rayman2Fix.ini").find("MusicInPauseMenu=1") != std::string::npos &&
              contents(root / "Rayman2Fix.ini").find("Resolution=") == std::string::npos,
              "bootstrap has no Resolution setting and four enabled-by-default feature flags");
        write(root / "game" / "fix.dll", "BOOTSTRAP");
        write(root / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\n");
        const auto user_ini = root / "Rayman2Fix.ini";
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
              "[Debug]\nLog=1\n\n[Graphics]\nFPSLimit=60\n");
        write(user_ini, "[Graphics]\nAspectRatio=4:3\n"
                        "[Features]\nSkipCinematics=1\n");
        check(r2fix::configured_aspect_ratio_is_4_3(user_ini) &&
              r2fix::configured_skip_cinematics(user_ini),
              "4:3 and cinematic skip opt-in recognized");
        check(r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              r2fix::configured_remove_cinematic_bars(user_ini),
              "both visual filters default to enabled when absent");
        check(r2fix::configured_music_in_pause_menu(user_ini),
              "ESC menu music defaults to enabled when absent");
        write(user_ini,"[Features]\nMusicInPauseMenu=1\nRemoveDialogueBackgrounds=1\nRemoveCinematicBars=1\n");
        check(r2fix::configured_music_in_pause_menu(user_ini) &&
              r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              r2fix::configured_remove_cinematic_bars(user_ini),
              "opt-in ESC music coexists with visual filters");
        // The feature flag and graphics aspect ratio must remain
        // independent. This validates configuration wiring, NOT in-game
        // first-ESC Theme availability or a Wine rendering callback.
        for (const char* aspect : {"Auto", "4:3"}) {
            write(user_ini,std::string("[Graphics]\nAspectRatio=")
                + aspect + "\n[Features]\nMusicInPauseMenu=1\n"
                "SkipCinematics=1\n");
            check(r2fix::configured_music_in_pause_menu(user_ini) &&
                  r2fix::configured_skip_cinematics(user_ini),
                  "ESC music and intro skip enabled at any selected aspect");
            const auto canvas=r2fix::render_resolution_for_display(
                1920u,1080u,r2fix::configured_aspect_ratio_is_4_3(user_ini));
            check(canvas==(std::string(aspect)=="4:3" ?
                  std::make_pair(1440u,1080u) :
                  std::make_pair(1920u,1080u)),
                  "native output aspect independently controls canvas");
        }
        write(user_ini,"[Features]\nMusicInPauseMenu=0\n");
        check(!r2fix::configured_music_in_pause_menu(user_ini),
              "explicit ESC music opt-out works");
        write(user_ini,"[Graphics]\nAspectRatio=4:3\n"
                       "[Features]\nSkipCinematics=1\nRemoveDialogueBackgrounds=1\n"
                       "RemoveCinematicBars=0\n");
        check(r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              !r2fix::configured_remove_cinematic_bars(user_ini),
              "dialogue-only opt-in does not enable cinematic filter");
        write(user_ini,"[Features]\nRemoveDialogueBackgrounds=0\nRemoveCinematicBars=1\n");
        check(!r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              r2fix::configured_remove_cinematic_bars(user_ini),
              "cinematic-only opt-in does not enable dialogue filter");
        write(user_ini,"[Graphics]\nAspectRatio=4:3\n"
                       "[Features]\nSkipCinematics=1\nRemoveDialogueBackgrounds=1\n"
                       "RemoveCinematicBars=1\n");
        check(r2fix::render_resolution_for_display(1920u,1080u,true) ==
              std::make_pair(1440u,1080u),"1920x1080: 4:3 content area 1440x1080");
        write(user_ini, "[Graphics]\nAspectRatio=4:3\n");
        check(r2fix::render_resolution_for_display(1280u,800u,true) ==
              std::make_pair(1064u,798u),"1280x800 exact 4:3");
        write(user_ini, "[Graphics]\nAspectRatio=Auto\n");
        check(r2fix::render_resolution_for_display(1920u,1080u,false) ==
              std::make_pair(1920u,1080u),"default wide unchanged");
        const auto desktop=r2fix::desktop_resolution();
        check(desktop.first>=640u && desktop.second>=480u &&
              r2fix::configured_render_resolution(user_ini)==desktop,
              "Auto mode uses detected desktop resolution without an INI override");
        for(const auto* bad:{"2","yes",""}) {
            write(user_ini,std::string("[Graphics]\nAspectRatio=")+bad+"\n");
            bool refused=false;
            try{(void)r2fix::configured_aspect_ratio_is_4_3(user_ini);}
            catch(const std::exception&){refused=true;}
            check(refused,"invalid AspectRatio rejected");
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
              "[Graphics]\r\nFPSLimit=60\r\n"
              "; comment\r\n[Personal]\r\nSetting=keep\r\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        const auto migrated=contents(user_ini);
        check(migrated.find(bom)==0 &&
              migrated.find("Log=0\r\n")!=std::string::npos &&
              migrated.find("Setting=keep\r\n")!=std::string::npos &&
              migrated.find("; comment\r\n")!=std::string::npos &&
              migrated.find("AspectRatio=Auto\r\n")!=std::string::npos &&
              migrated.find("[Features]\r\n")!=std::string::npos &&
              migrated.find("SkipCinematics=1\r\n")!=std::string::npos &&
              migrated.find("RemoveDialogueBackgrounds=1\r\n")!=std::string::npos &&
              migrated.find("RemoveCinematicBars=1\r\n")!=std::string::npos &&
              migrated.find("MusicInPauseMenu=1\r\n")!=std::string::npos,
              "existing INI migration retains BOM, CRLF, custom values");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(contents(user_ini)==migrated,"migration is idempotent");

        // User opt-outs must remain respected: new defaults are not forced on
        // previously explicit 0 values or rewritten on repeat launches.
        write(user_ini,"[Features]\nSkipCinematics=0\n"
                       "RemoveDialogueBackgrounds=0\n"
                       "RemoveCinematicBars=0\n"
                       "MusicInPauseMenu=0\n[Personal]\nName=kept\n");
        check(!r2fix::configured_skip_cinematics(user_ini) &&
              !r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              !r2fix::configured_remove_cinematic_bars(user_ini) &&
              !r2fix::configured_music_in_pause_menu(user_ini),
              "explicit 0 opt-outs override every new default");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        const auto opted_out=contents(user_ini);
        check(opted_out.find("SkipCinematics=0")!=std::string::npos &&
              opted_out.find("RemoveDialogueBackgrounds=0")!=std::string::npos &&
              opted_out.find("RemoveCinematicBars=0")!=std::string::npos &&
              opted_out.find("MusicInPauseMenu=0")!=std::string::npos &&
              opted_out.find("Name=kept")!=std::string::npos,
              "INI normalization preserves existing disabled options and custom keys");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(contents(user_ini)==opted_out,"explicit opt-out normalization is idempotent");

        write(user_ini,"[Features]\nRemoveCinematicBars=0\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(r2fix::configured_skip_cinematics(user_ini) &&
              r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              !r2fix::configured_remove_cinematic_bars(user_ini) &&
              r2fix::configured_music_in_pause_menu(user_ini),
              "missing features default to 1 while explicitly disabled bars stay at 0");
        // Retire the former experimental switch regardless of its value;
        // the explicit single MusicInPauseMenu option always wins.
        for (const char* old_early : {"0","1"}) {
            write(user_ini,bom+std::string("[Debug]\r\nLog=0\r\n")+
                  "[Features]\r\nMusicInPauseMenu=1\r\n"
                  "; Experimental: attempt ESC music while Theme handoff is pending\r\n"
                  "; Default 0; requires MusicInPauseMenu=1 and Debug Log=1\r\n"
                  "EarlyThemeMusicExperimental="+old_early+"\r\n"
                  "[Personal]\r\nSetting=keep\r\n");
            r2fix::ensure_rayman2fix_ini_options(user_ini);
            const auto cleaned=contents(user_ini);
            check(cleaned.find(bom)==0 &&
                  cleaned.find("Log=0\r\n")!=std::string::npos &&
                  cleaned.find("MusicInPauseMenu=1\r\n")!=std::string::npos &&
                  cleaned.find("Setting=keep\r\n")!=std::string::npos &&
                  cleaned.find("EarlyThemeMusicExperimental")==std::string::npos &&
                  cleaned.find("; Experimental: attempt ESC music")==std::string::npos &&
                  r2fix::configured_music_in_pause_menu(user_ini),
                  "legacy Theme setting removed while one music toggle survives");
            r2fix::ensure_rayman2fix_ini_options(user_ini);
            check(contents(user_ini)==cleaned,
                  "retired experimental-key migration is idempotent");
        }
        write(user_ini,"[Features]\nMusicInPauseMenu=0\n"
                       "EarlyThemeMusicExperimental=1\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(!r2fix::configured_music_in_pause_menu(user_ini) &&
              contents(user_ini).find("EarlyThemeMusicExperimental")==std::string::npos,
              "legacy experiment cannot override an explicit disabled music option");
        // Existing boolean settings migrate without losing the chosen geometry.
        write(user_ini,"[Graphics]\nForce4x3=1\n"
                       "[Personal]\nSetting=keep\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        const auto old_enabled=contents(user_ini);
        check(old_enabled.find("AspectRatio=4:3")!=std::string::npos &&
              old_enabled.find("Force4x3=")==std::string::npos &&
              old_enabled.find("Setting=keep")!=std::string::npos &&
              r2fix::render_resolution_for_display(
                  1920u,1080u,r2fix::configured_aspect_ratio_is_4_3(user_ini))==
                  std::make_pair(1440u,1080u),
              "Force4x3=1 migrates to 4:3 with identical geometry");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(contents(user_ini)==old_enabled,"aspect migration is idempotent");
        write(user_ini,"[Graphics]\nForce4x3=0\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(r2fix::configured_aspect_ratio(user_ini)=="Auto" &&
              contents(user_ini).find("Force4x3=")==std::string::npos,
              "Force4x3=0 migrates to Auto");
        write(user_ini,"[Graphics]\nForce4x3=1\nAspectRatio=auto\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(r2fix::configured_aspect_ratio(user_ini)=="Auto" &&
              contents(user_ini).find("Force4x3=")==std::string::npos,
              "explicit AspectRatio wins over the obsolete flag");
        write(user_ini,"[Graphics]\nForce4x3=2\n");
        bool invalid_legacy_aspect=false;
        try{r2fix::ensure_rayman2fix_ini_options(user_ini);}
        catch(const std::exception&){invalid_legacy_aspect=true;}
        check(invalid_legacy_aspect &&
              contents(user_ini).find("Force4x3=2")!=std::string::npos,
              "invalid old flag fails without altering INI");
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
        write(user_ini,"[Features]\nRemoveBlackFrames=0\n");
        r2fix::ensure_rayman2fix_ini_options(user_ini);
        check(!r2fix::configured_remove_dialogue_backgrounds(user_ini) &&
              !r2fix::configured_remove_cinematic_bars(user_ini),
              "explicit legacy disabled setting retains disabled visual filters");
        write(user_ini,"[Features]\nRemoveBlackFrames=bogus\n");
        bool legacy_refused=false;
        try{r2fix::ensure_rayman2fix_ini_options(user_ini);}
        catch(const std::exception&){legacy_refused=true;}
        check(legacy_refused && contents(user_ini).find("RemoveBlackFrames=bogus")!=std::string::npos,
              "invalid legacy setting rejected without altering INI");
        write(user_ini,
              "[Debug]\nLog=1\n\n[Graphics]\nFPSLimit=60\n");
        write(root / "mods" / "textures" / "logo.rgba", "NEW-HD-LOGO");
        write(root / "mods" / "textures" / "custom.rgba", "USER-IMAGE");
        check(r2fix::run_automatic(app) == 2,
              "no game or image must report missing media, not success");
        check(!fs::exists(root / "game" / "rayman2.exe"),
              "missing media must not mark game installed");

#ifdef _WIN32
        // An unknown renderer backed up during bootstrap must survive the
        // atomic CD/game publication instead of being lost with game.bootstrap-backup.
        write(root/"game"/"glide2x.dll.bak","PRESERVE-OLD-GLIDE");
#endif
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
              !fs::exists(root / "game" / "rayman2.exe") &&
              !fs::exists(root / "game.installing"),
              "invalid local executable rejected before publishing original CD files");
        fs::remove(root / "Rayman2.exe");
        check(r2fix::run_automatic(app) == 2,
              "CD assets install but nonfunctional game runtime must not launch");
        const auto game = root / "game";
        check(fs::exists(game / "rayman2.exe"), "auto mode installs game in rayman2");
#ifdef _WIN32
        check(r2fix::gameframe_glide_ready(game),
              "published ISO game contains the verified local GameFrame DLL");
        check(contents(game/"glide2x.dll.bak")=="PRESERVE-OLD-GLIDE",
              "atomic CD installation preserves the user's Glide backup");
#endif
        check(!fs::exists(game / "DiscContent"), "no intermediate DiscContent layout");
        check(contents(game / "rayman2.exe") == "GAME", "retail exe copied unchanged");
        check(!fs::exists(game / "winmm.dll"),
              "fresh install never deploys a project WinMM proxy");
        check(contents(root / "mods" / "textures" / "logo.rgba") == "NEW-HD-LOGO" &&
              contents(root / "mods" / "textures" / "custom.rgba") == "USER-IMAGE",
              "external mod textures are unchanged by disc installation");
        check(contents(game / "fix.dll") == "BOOTSTRAP",
              "injected runtime is deployed beside the game executable");
        check(contents(game / "dll" / "snd_cpa_mmx.ini") == "AUDIO\n",
              "MMX sound settings copied from disc");
        check(contents(game / "dll" / "snd_cpa.ini") == "AUDIO\n",
              "snd_cpa.ini initialized from MMX variant");
        check(contents(game / "data" / "game.dat") == "DATA\n",
              "original game data copied in lowercase");
        check(contents(game / "fix.dll") == "BOOTSTRAP",
              "launcher runtime payload remains available");
        check(contents(root / "Rayman2Fix.ini") ==
              "[Debug]\nLog=1\n\n[Graphics]\nFPSLimit=60\n",
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
        check(contents(root / "state" / ".rayman2fix-cd-files.tsv").find("\tubi.ini") ==
                  std::string::npos,
              "managed ubi.ini must not be recorded as an immutable CD asset");
        check(fs::exists(root / "state" / ".rayman2fix-cd-files.tsv"),
              "local installation receipt created");
        check(!fs::exists(root / "docs" / ".rayman2fix-cd-files.tsv") &&
              !fs::exists(root / "docs" / ".rayman2fix-exe-patch.tsv"),
              "installation receipts must never be published under docs/");
        check(fs::is_directory(root / "docs"),
              "docs/ survives CD publication without being replaced");
        // All ten root-only exclusions are absent from a fresh installation
        // and its SHA256 receipt. Unrelated similarly named files survive.
        for (const auto* filename : {"00000001.tmp", "0000040c.016",
                                     "0000040c.256", "secdrv.sys",
                                     "rayman2.icd", "clcd32.dll",
                                     "clokspl.exe", "drvmgt.dll",
                                     "loader.ini", "dplayerx.dll"}) {
            check(!fs::exists(game / filename), "obsolete protection file not extracted");
            check(contents(root / "state" / ".rayman2fix-cd-files.tsv").find(filename) ==
                      std::string::npos,
                  "obsolete protection file omitted from receipt");
        }
        check(fs::exists(game / "dxplayerx.dll"),
              "unrelated DXPLAYERX root file must survive installation");
        check(contents(root / "state" / ".rayman2fix-cd-files.tsv").find(
                  "\tdxplayerx.dll") != std::string::npos,
              "unrelated DXPLAYERX remains in the SHA256 receipt");
        check(!fs::exists(root / "game.installing"),
              "staging directory removed after publishing");
        check(r2fix::verify_installation(app) == 0,
              "installed game resources verify from local receipt");

        // A fresh install must not synthesize obsolete files or repair them.
        check(fs::is_regular_file(root / "state" / ".rayman2fix-cd-files.tsv"),
              "new receipt is independent of the game directory");
        // Treat the original CD-sourced ubi.ini as a mutable game config.
        // The immutable local receipt must never track mutable ubi.ini.
        write(root / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nFPSLimit=60\n");
        check(r2fix::run_automatic(app) == 2,
              "reconfigure graphics from one INI with no CD present");
        ubi = contents(game / "ubi.ini");
        check(ubi.find("GLI_Mode=1 - 1024 x 768 x 16") != std::string::npos &&
              ubi.find("GLI_Mode=1 - 1280 x 720 x 16") == std::string::npos,
              "game-facing config retains supported internal Glide 1024x768 regardless of monitor output");
        const auto selected = r2fix::desktop_resolution();
        check(selected.first >= 640u && selected.second >= 480u &&
              r2fix::configured_render_resolution(app.ini_path()) == selected,
              "output always follows desktop size, independent of the INI");
        check(ubi.find("Language=French") != std::string::npos &&
              ubi.find("Keep=present") != std::string::npos,
              "regenerated config retains other original CD options");
        check(contents(root / "state" / ".rayman2fix-cd-files.tsv").find("\tubi.ini") ==
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

        // Retired Resolution keys never control output or block startup.
        // Normalization strips them without changing the game-facing mode.
        const auto good_ubi=contents(game / "ubi.ini");
        write(root / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nResolution=invalid\nFPSLimit=60\n");
        check(r2fix::run_automatic(app)==2 &&
              contents(game / "ubi.ini")==good_ubi,
              "obsolete invalid Resolution is ignored without modifying Glide mode");
        r2fix::ensure_rayman2fix_ini_options(root / "Rayman2Fix.ini");
        check(contents(root / "Rayman2Fix.ini").find("Resolution=")==std::string::npos,
              "obsolete Resolution line removed from existing user INI");
        write(root / "Rayman2Fix.ini",
              "[Debug]\nLog=1\n\n[Graphics]\nFPSLimit=60\n");

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
