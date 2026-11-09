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
std::string read_graphics_setting(const fs::path& ini,
                                  const char* key, const char* fallback) {
    const auto lines = lines_of(ini);
    std::string current, choice = fallback;
    for (const auto& raw : lines) {
        std::string next;
        if (section(raw, next)) { current = next; continue; }
        const auto line = trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos || current != "GRAPHICS" ||
            upper(trim(line.substr(0, equal))) != upper(key))
            continue;
        choice = trim(line.substr(equal + 1));
    }
    if (choice.empty())
        throw std::runtime_error(std::string("empty [Graphics] ") + key +
                                 " in Rayman2Fix.ini");
    return choice;
}
std::string read_resolution_setting(const fs::path& ini) {
    return read_graphics_setting(ini, "Resolution", "Desktop");
}
std::pair<std::uint32_t,std::uint32_t> resolution(const std::string& setting) {
    const auto value = upper(trim(setting));
    if (value == "DESKTOP" || value == "AUTO") {
#ifdef _WIN32
        const auto display = primary_display_metrics();
        const int w = static_cast<int>(display.width);
        const int h = static_cast<int>(display.height);
        if (w <= 0 || h <= 0)
            throw std::runtime_error("cannot detect primary desktop resolution");
        return {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
#else
        // Linux build is an ISO unit-test harness, not the shipping Win32
        // application. Always use an explicit resolution in host tests.
        return {640, 480};
#endif
    }
    const auto split = value.find('X');
    if (split == std::string::npos ||
        value.find('X', split + 1) != std::string::npos)
        throw std::runtime_error("invalid [Graphics] Resolution; use Desktop or 1920x1080");
    const auto ws = trim(value.substr(0, split));
    const auto hs = trim(value.substr(split + 1));
    const auto decimal = [](const std::string& text) {
        return !text.empty() && std::all_of(text.begin(), text.end(),
            [](unsigned char c) { return c >= '0' && c <= '9'; });
    };
    if (!decimal(ws) || !decimal(hs) || ws.size() > 5 || hs.size() > 5)
        throw std::runtime_error("invalid [Graphics] Resolution dimensions");
    const auto w = std::stoul(ws), h = std::stoul(hs);
    if (w < 640 || w > 8192 || h < 480 || h > 4320)
        throw std::runtime_error("unsupported [Graphics] Resolution; expected 640..8192 x 480..4320");
    return {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
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

std::pair<std::uint32_t, std::uint32_t> configured_output_resolution(
    const fs::path& ini) {
    return resolution(read_resolution_setting(ini));
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

void synchronize_rayman2_ubi_ini(const fs::path& game_dir, Startup& startup) {
    const auto [w,h] = configured_output_resolution(startup.ini_path());
    const fs::path target = game_dir / "ubi.ini";
    const auto original = fs::exists(target) ? lines_of(target) :
                                               std::vector<std::string>{};
    const auto updated = generated_ubi(original, w, h);
    atomically_install(target, updated);
    startup.log("CONFIG-UBI: managed from Rayman2Fix.ini, renderer=Glide2"
                " adapter=GliVd1 internal=1024x768x16 output=" +
                std::to_string(w) + "x" + std::to_string(h) +
                " (GameFrame GLIDE_SIZE, game-facing ubi.ini)");
}
} // namespace r2fix
