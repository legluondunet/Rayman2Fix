#include "rayman2fix/game_config.hpp"
#include "rayman2fix/startup.hpp"
#include "rayman2fix/display.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace r2fix {
namespace {
std::string trim(std::string value) {
    auto start = value.find_first_not_of(" \r\n\t");
    if (start == std::string::npos) return {};
    return value.substr(start, value.find_last_not_of(" \r\n\t") - start + 1);
}
std::string upper(std::string v) {
    for (auto& c : v)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return v;
}
bool section(const std::string& line, std::string& value) {
    auto t = trim(line);
    if (t.size() >= 3 &&
        static_cast<unsigned char>(t[0]) == 0xef &&
        static_cast<unsigned char>(t[1]) == 0xbb &&
        static_cast<unsigned char>(t[2]) == 0xbf)
        t.erase(0, 3);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    value = upper(trim(t.substr(1, t.size() - 2)));
    return true;
}
std::vector<std::string> lines_of(const fs::path& file) {
    if (!fs::is_regular_file(file) || fs::is_symlink(file) ||
        fs::file_size(file) > 1024u * 1024u)
        throw std::runtime_error("unsafe/oversize INI configuration file");
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open INI configuration file");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    if (in.bad()) throw std::runtime_error("failed reading INI");
    return lines;
}
std::string read_ini_setting(const fs::path& ini,
                             const char* section_name,
                             const char* key, const char* fallback) {
    const auto lines = lines_of(ini);
    std::string current, choice = fallback;
    for (const auto& raw : lines) {
        std::string next;
        if (section(raw, next)) {current=next;continue;}
        const auto line=trim(raw);
        if(line.empty() || line.front()==';' || line.front()=='#')continue;
        const auto equal=line.find('=');
        if(equal==std::string::npos || current!=upper(section_name) ||
           upper(trim(line.substr(0,equal)))!=upper(key))continue;
        choice=trim(line.substr(equal+1));
    }
    if(choice.empty())
        throw std::runtime_error(std::string("empty [")+section_name+
                                 "] "+key+" in Rayman2Fix.ini");
    return choice;
}
std::string read_graphics_setting(const fs::path& ini,
                                  const char* key, const char* fallback) {
    return read_ini_setting(ini,"Graphics",key,fallback);
}
bool read_ini_flag(const fs::path& ini,const char* section_name,
                   const char* key) {
    const std::string value=read_ini_setting(ini,section_name,key,"0");
    if(value!="0" && value!="1")
        throw std::runtime_error(std::string("invalid [")+
            section_name+"] "+key+"; use 0 or 1");
    return value=="1";
}
constexpr std::array<const char*, 5> kKeys = {{
    "GLI_DllFile", "GLI_Dll", "GLI_Driver", "GLI_Device", "GLI_Mode"
}};
bool controlled_key(const std::string& name) {
    const auto key = upper(trim(name));
    for (auto* item : kKeys) if (key == upper(item)) return true;
    return false;
}
std::string generated_ubi(const std::vector<std::string>& original,
                          std::uint32_t w, std::uint32_t h) {
    const std::array<std::string, 5> forced = {{
        "GLI_DllFile=GliVd1",
        "GLI_Dll=Glide2",
        "GLI_Driver=",
        "GLI_Device=",
        // Rayman 2/Glide only supports the original 4:3 internal modes;
        // desktop dimensions are applied by GameFrame via GLIDE_SIZE.
        "GLI_Mode=1 - 1024 x 768 x 16"
    }};
    std::vector<std::string> out;
    std::string current;
    bool seen_rayman2 = false;
    const auto emit = [&]() {
        for (const auto& line : forced) out.push_back(line);
    };
    bool present = false;
    for (const auto& original_line : original) {
        std::string next;
        if (section(original_line, next)) {
            if (current == "RAYMAN2" && present) emit();
            current = next;
            present = current == "RAYMAN2";
            seen_rayman2 |= present;
            out.push_back(original_line);
            continue;
        }
        const auto t = trim(original_line);
        const auto equals = t.find('=');
        if (present && !t.empty() && t[0] != '#' && t[0] != ';' &&
            equals != std::string::npos &&
            controlled_key(t.substr(0, equals)))
            continue; // remove old conflicting settings, preserve all others
        out.push_back(original_line);
    }
    if (present) emit();
    if (!seen_rayman2) {
        if (!out.empty() && !out.back().empty()) out.emplace_back();
        out.emplace_back("[Rayman2]");
        emit();
    }
    std::string text;
    for (const auto& item : out) { text += item; text += "\r\n"; }
    return text;
}
void atomically_install(const fs::path& output, const std::string& bytes) {
    fs::path pending = output;
    pending += ".rayman2fix-new";
    fs::path previous = output;
    previous += ".rayman2fix-previous";
    if (fs::exists(pending) || fs::is_symlink(pending) ||
        fs::exists(previous) || fs::is_symlink(previous))
        throw std::runtime_error("unresolved ubi.ini update staging files");
    if (fs::exists(output) &&
        (fs::is_symlink(output) || !fs::is_regular_file(output)))
        throw std::runtime_error("invalid game-facing ubi.ini path");
    // Avoid rewriting an already matching game-facing config on each launch.
    if (fs::exists(output)) {
        std::ifstream old(output, std::ios::binary);
        if (old && std::string(std::istreambuf_iterator<char>(old), {}) == bytes)
            return;
    }
    try {
        std::ofstream out(pending, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot stage ubi.ini");
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) throw std::runtime_error("cannot write ubi.ini");
        bool saved = false;
        if (fs::exists(output)) {
            fs::rename(output, previous);
            saved = true;
        }
        try {
            fs::rename(pending, output);
        } catch (...) {
            std::error_code ignored;
            if (saved) fs::rename(previous, output, ignored);
            throw;
        }
        if (saved) fs::remove(previous);
    } catch (...) {
        std::error_code ignored;
        fs::remove(pending, ignored);
        throw;
    }
}
} // namespace

// Desktop resolution is always detected, never read from user INI.
// The Linux target is a synthetic test harness; the shipping build is Win32.
std::pair<std::uint32_t,std::uint32_t> desktop_resolution() {
#ifdef _WIN32
    // The install/verify path may query dimensions before the game launch.
    // Ensure physical, not DPI-virtualized, primary monitor coordinates.
    configure_launcher_dpi_awareness();
    const auto display=primary_display_metrics();
    if(display.width<640u || display.height<480u)
        throw std::runtime_error("invalid primary display dimensions");
    return {display.width, display.height};
#else
    return {640u,480u};
#endif
}

std::string configured_aspect_ratio(const fs::path& ini) {
    const auto value=upper(trim(read_graphics_setting(ini,"AspectRatio","Auto")));
    if(value=="AUTO")return "Auto";
    if(value=="4:3")return "4:3";
    throw std::runtime_error("invalid [Graphics] AspectRatio; use Auto or 4:3");
}
bool configured_aspect_ratio_is_4_3(const fs::path& ini) {
    return configured_aspect_ratio(ini)=="4:3";
}
bool configured_skip_cinematics(const fs::path& ini) {
    return read_ini_flag(ini,"Features","SkipCinematics");
}
bool configured_remove_dialogue_backgrounds(const fs::path& ini) {
    return read_ini_flag(ini,"Features","RemoveDialogueBackgrounds");
}
bool configured_remove_cinematic_bars(const fs::path& ini) {
    return read_ini_flag(ini,"Features","RemoveCinematicBars");
}
bool configured_music_in_pause_menu(const fs::path& ini) {
    return read_ini_flag(ini,"Features","MusicInPauseMenu");
}
std::pair<std::uint32_t,std::uint32_t> render_resolution_for_display(
    std::uint32_t width,std::uint32_t height,bool classic_4_3) {
    if(width<640u || height<480u || width>16384u || height>8640u)
        throw std::runtime_error("invalid primary display geometry");
    if(!classic_4_3)return {width,height};
    // Largest exact 4:3 area inside the native physical monitor.
    const auto units=std::min(width/4u,height/3u);
    if(units<160u)
        throw std::runtime_error("unsupported 4:3 canvas on primary display");
    return {units*4u,units*3u};
}
std::pair<std::uint32_t,std::uint32_t> configured_render_resolution(
        const fs::path& ini) {
    const auto physical=desktop_resolution();
    return render_resolution_for_display(
        physical.first,physical.second,configured_aspect_ratio_is_4_3(ini));
}

// FPSLimit=60 by default; 0 = unlimited for troubleshooting, 15-240 =
// supported limits. Keep this in the single Rayman2Fix.ini user config.
std::uint32_t configured_fps_limit(const fs::path& ini) {
    const auto value = read_graphics_setting(ini, "FPSLimit", "60");
    if (value.size() > 3 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch >= '0' && ch <= '9';
        }))
        throw std::runtime_error("invalid [Graphics] FPSLimit; use 0 or 15..240");
    const auto fps = static_cast<std::uint32_t>(std::stoul(value));
    if (fps != 0 && (fps < 15 || fps > 240))
        throw std::runtime_error("unsupported [Graphics] FPSLimit; use 0 or 15..240");
    return fps;
}

void ensure_rayman2fix_ini_options(const fs::path& ini) {
    const auto raw=lines_of(ini);
    std::string current;
    bool graphics=false,features=false,aspect=false,force=false,skip=false;
    bool dialogue=false,cinematic=false,legacy=false,music=false,old_early_theme=false;
    bool obsolete_resolution=false;
    for(const auto& text:raw) {
        std::string next;
        if(section(text,next)) {
            current=next;
            graphics|=current=="GRAPHICS";
            features|=current=="FEATURES";
            continue;
        }
        const auto t=trim(text);
        if(t.empty() || t.front()==';' || t.front()=='#')continue;
        const auto p=t.find('=');
        if(p==std::string::npos)continue;
        const auto key=upper(trim(t.substr(0,p)));
        if(current=="GRAPHICS" && key=="FORCE4X3")force=true;
        if(current=="GRAPHICS" && key=="RESOLUTION")obsolete_resolution=true;
        if(current=="GRAPHICS" && key=="ASPECTRATIO")aspect=true;
        if(current=="FEATURES" && key=="SKIPCINEMATICS")skip=true;
        if(current=="FEATURES" && key=="REMOVEDIALOGUEBACKGROUNDS")dialogue=true;
        if(current=="FEATURES" && key=="REMOVECINEMATICBARS")cinematic=true;
        if(current=="FEATURES" && key=="MUSICINPAUSEMENU")music=true;
        if(current=="FEATURES" && key=="EARLYTHEMEMUSICEXPERIMENTAL")old_early_theme=true;
        if(current=="FEATURES" && key=="REMOVEBLACKFRAMES")legacy=true;
    }
    // Existing opt-in users retain BOTH former behaviors. If only one new
    // setting is present, its explicit value always takes precedence.
    // Invalid legacy values are rejected before editing the user's INI.
    const bool inherited=legacy &&
        read_ini_flag(ini,"Features","RemoveBlackFrames");
    // Respect an explicitly configured AspectRatio if both keys are present.
    // Reject invalid settings before changing the user's file.
    if(aspect)(void)configured_aspect_ratio(ini);
    const std::string migrated_aspect=(!aspect && force &&
        read_ini_flag(ini,"Graphics","Force4x3")) ? "4:3" : "Auto";
    if(aspect && !force && skip && dialogue && cinematic && music &&
       !old_early_theme && !legacy && !obsolete_resolution)return;
    std::vector<std::string> output;
    std::string active;
    bool added_aspect=false,added_skip=false,added_dialogue=false,added_cinematic=false,added_music=false;
    const auto inject=[&]() {
        if(active=="GRAPHICS" && !aspect && !force && !added_aspect) {
            output.emplace_back("; Auto = monitor aspect ratio; 4:3 = centered classic picture");
            output.emplace_back("AspectRatio=Auto");
            added_aspect=true;
        }
        if(active=="FEATURES" && !skip && !added_skip) {
            output.emplace_back("; 1 = Space/Enter/Esc skip for jail_20 only");
            output.emplace_back("; Learn_10 skipping remains disabled (unsafe)");
            output.emplace_back("SkipCinematics=0");
            added_skip=true;
        }
        if(active=="FEATURES" && !dialogue && !added_dialogue) {
            output.emplace_back("; 1 = hide native backdrop rectangles behind in-game dialogue");
            output.emplace_back(std::string("RemoveDialogueBackgrounds=")+(inherited?"1":"0"));
            added_dialogue=true;
        }
        if(active=="FEATURES" && !cinematic && !added_cinematic) {
            output.emplace_back("; 1 = experimental removal of game-drawn cinematic letterbox bars");
            output.emplace_back("; Does not affect GameFrame AspectRatio=4:3 sidebars");
            output.emplace_back(std::string("RemoveCinematicBars=")+(inherited?"1":"0"));
            added_cinematic=true;
        }
        if(active=="FEATURES" && !music && !added_music) {
            output.emplace_back("; 1 = continue level music throughout ESC, including verified early Theme handoffs");
            output.emplace_back("; Default 0 (opt in); original SFX and level changes remain native");
            output.emplace_back("MusicInPauseMenu=0");
            added_music=true;
        }
    };
    for(const auto& text:raw) {
        std::string next;
        if(section(text,next)) {
            inject();
            active=next;
        }
        // Retire the obsolete combined flag after migrating its value.
        // All unrelated lines, comments and custom sections are preserved.
        const auto t=trim(text);
        const auto eq=t.find('=');
        const auto key=eq==std::string::npos ? std::string{} :
                       upper(trim(t.substr(0,eq)));
        if(active=="GRAPHICS") {
            if(key=="RESOLUTION")continue; // retired: always use physical monitor size
            if(t=="; 1 = centered 4:3 picture with black sidebars" ||
               t=="; 1 = centered 4:3 picture (black sidebars)")
                continue; // remove obsolete generated comments
            if(key=="FORCE4X3") {
                if(!aspect && !added_aspect) {
                    output.emplace_back("; Auto = monitor aspect ratio; 4:3 = centered classic picture");
                    output.emplace_back("AspectRatio="+migrated_aspect);
                    added_aspect=true;
                }
                continue; // retire old key; explicit AspectRatio always wins
            }
        }
        if(active=="FEATURES" && (key=="REMOVEBLACKFRAMES" ||
                                   key=="EARLYTHEMEMUSICEXPERIMENTAL"))
            continue; // retired toggles; MusicInPauseMenu owns both audio paths
        // Remove only our generated comments; retain custom user annotations.
        if(active=="FEATURES" &&
           (t=="; Experimental: attempt ESC music while Theme handoff is pending" ||
            t=="; Default 0; requires MusicInPauseMenu=1 and Debug Log=1"))
            continue;
        if(t=="; GameFrame Force4x3 sidebars remain unchanged") {
            output.emplace_back("; GameFrame AspectRatio=4:3 sidebars remain unchanged");
            continue;
        }
        output.push_back(text);
    }
    inject();
    if(!graphics) {
        if(!output.empty() && !output.back().empty())
            output.emplace_back();
        output.emplace_back("[Graphics]");
        output.emplace_back("; Auto = monitor aspect ratio; 4:3 = centered classic picture");
        output.emplace_back("AspectRatio=Auto");
    }
    if(!features) {
        if(!output.empty() && !output.back().empty())
            output.emplace_back();
        output.emplace_back("[Features]");
        output.emplace_back("; 1 = Space/Enter/Esc skip for jail_20 only");
        output.emplace_back("; Learn_10 skipping remains disabled (unsafe)");
        output.emplace_back("SkipCinematics=0");
        output.emplace_back("; 1 = hide native backdrop rectangles behind in-game dialogue");
        output.emplace_back("RemoveDialogueBackgrounds=0");
        output.emplace_back("; 1 = experimental removal of game-drawn cinematic letterbox bars");
        output.emplace_back("; Does not affect GameFrame AspectRatio=4:3 sidebars");
        output.emplace_back("RemoveCinematicBars=0");
        output.emplace_back("; 1 = continue level music throughout ESC, including verified early Theme handoffs");
        output.emplace_back("; Default 0 (opt in); preserves native SFX and level changes");
        output.emplace_back("MusicInPauseMenu=0");
    }
    // Retain original BOM, comments, unrelated keys and EOL type.
    std::ifstream old(ini,std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(old)),
                            std::istreambuf_iterator<char>());
    if(old.bad())throw std::runtime_error("cannot read Rayman2Fix.ini");
    old.close(); // Release file handle before atomic rename on Windows/Wine.
    const std::string eol=bytes.find("\r\n")!=std::string::npos ? "\r\n" : "\n";
    std::string updated;
    for(const auto& text:output){updated+=text;updated+=eol;}
    fs::path pending=ini;pending+=".rayman2fix-new";
    fs::path previous=ini;previous+=".rayman2fix-previous";
    if(fs::exists(pending)||fs::is_symlink(pending)||
       fs::exists(previous)||fs::is_symlink(previous))
        throw std::runtime_error("unresolved Rayman2Fix.ini staging");
    try {
        std::ofstream out(pending,std::ios::binary|std::ios::trunc);
        if(!out)throw std::runtime_error("cannot stage Rayman2Fix.ini");
        out.write(updated.data(),static_cast<std::streamsize>(updated.size()));
        out.close();
        if(!out)throw std::runtime_error("cannot write Rayman2Fix.ini");
        fs::rename(ini,previous);
        try{fs::rename(pending,ini);}
        catch(...){fs::rename(previous,ini);throw;}
        fs::remove(previous);
    }catch(...){
        std::error_code ignored;
        fs::remove(pending,ignored);
        throw;
    }
}

void synchronize_rayman2_ubi_ini(const fs::path& game_dir, Startup& startup) {
    const auto [w,h] = desktop_resolution();
    const fs::path target = game_dir / "ubi.ini";
    const auto original = fs::exists(target) ? lines_of(target) :
                                               std::vector<std::string>{};
    const auto updated = generated_ubi(original, w, h);
    atomically_install(target, updated);
    startup.log("CONFIG-UBI: renderer=Glide2"
                " adapter=GliVd1 internal=1024x768x16 desktop_output=" +
                std::to_string(w) + "x" + std::to_string(h) +
                " (GameFrame GLIDE_SIZE; no user resolution override)");
}
} // namespace r2fix
