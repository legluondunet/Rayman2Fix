#pragma once
#include "rayman2fix/gog_source_policy.hpp"
#include <array>
#include <set>

namespace r2fix::gog {

// Build argument vectors, not shell commands. The caller supplies an
// absolute, independently verified extractor and launches it directly.
// A single extraction performs integrity checks, and aborts at a checksum
// failure. No --gog (external RAR tool) or --dump (unsafe raw paths).
using Args = std::vector<std::wstring>;
inline Args product_args(const fs::path& setup) {
    return {L"--no-extract-unknown", L"--gog-game-id", L"--silent",
            L"--color=0", L"--progress=0", L"--", fs::absolute(setup).wstring()};
}
inline Args extract_args(const fs::path& setup, const fs::path& staging,
                         const std::wstring& language = {}) {
    Args args{L"--extract", L"--test", L"--no-extract-unknown",
              L"--exclude-temp", L"--lowercase", L"--collisions=error",
              L"--color=0", L"--progress=0",
              L"--output-dir", fs::absolute(staging).wstring()};
    if (!language.empty()) {
        // This value must come from verified setup metadata, not filename.
        args.push_back(L"--language");
        args.push_back(language);
    }
    args.push_back(L"--");
    args.push_back(fs::absolute(setup).wstring());
    return args;
}

// Windows CRT-compatible quoting, including terminal backslashes,
// whitespace and embedded quotes. Do not call a command interpreter.
inline std::wstring quote_arg(const std::wstring& arg) {
    std::wstring out(1, L'"');
    std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') out.append(2 * slashes + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(2 * slashes, L'\\');
    out.push_back(L'"');
    return out;
}
inline std::wstring command_line(const fs::path& extractor, const Args& args) {
    auto out = quote_arg(fs::absolute(extractor).wstring());
    for (const auto& a : args) { out += L' '; out += quote_arg(a); }
    return out;
}

enum class FileRole { game, config, metadata, documentation, auxiliary, unknown };

// Interpret normalized relative names only. Reject Windows aliases,
// alternate streams, reserved device names, trailing dots and traversal
// even when Linux host tests would permit such filenames.
inline bool safe_component(std::string_view part) {
    if (part.empty() || part == "." || part == ".." ||
        part.back() == '.' || part.back() == ' ') return false;
    for (unsigned char c : part) {
        if (c < 32 || c == 127 || c == ':' || c == '\\' || c == '/' ||
            c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*') return false;
    }
    const auto base = ascii_lower(std::string(part.substr(0, part.find('.'))));
    if (base == "con" || base == "prn" || base == "aux" ||
        base == "nul" || base == "conin$" || base == "conout$") return false;
    if (base.size() == 4 && (base.substr(0,3) == "com" || base.substr(0,3) == "lpt") &&
        base[3] >= '0' && base[3] <= '9') return false;
    return true;
}
inline std::string normalized_path(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path())
        throw std::runtime_error("unsafe GOG relative path");
    std::string normalized;
    for (const auto& element : path) {
        const auto part = element.u8string();
        if (!safe_component(part)) throw std::runtime_error("unsafe GOG path component");
        if (!normalized.empty()) normalized += '/';
        normalized += ascii_lower(part);
    }
    if (normalized.empty()) throw std::runtime_error("empty GOG path");
    return normalized;
}

// Roles are inventory classifications, NOT a permission to publish files.
// The wrapper dependencies and mutable asset policy need separate review.
inline FileRole role(std::string_view path) {
    if (path == "ubi.ini" || path == "dll/snd_cpa.ini") return FileRole::config;
    if (path == "rayman2.exe" || path.rfind("data/",0) == 0 ||
        path.rfind("dll/",0) == 0) return FileRole::game;
    if (path == "goggame-1207658940.info" ||
        path == "goggame-1207658940.hashdb") return FileRole::metadata;
    if (path == "manual.pdf" || path == "readme.txt" || path == "lisezmoi.txt" ||
        path == "liesmich.txt" || path == "leggimi.txt" || path == "leeme.txt")
        return FileRole::documentation;
    if (path == "glide2x.dll" || path == "ddraw.dll" || path == "dinput.dll" ||
        path == "3dfxspl2.dll" || path == "goginput.ini" || path == "gxsetup.exe" ||
        path == "dxcfg.exe" || path == "dxcfg.ini" ||
        path == "nglide_config.exe" || path == "nglide_config.ini" ||
        path == "nglide_readme.txt" || path.rfind("tmp/",0) == 0 ||
        path.rfind("__redist/",0) == 0 || path.rfind("commonappdata/",0) == 0 ||
        path.rfind("app/",0) == 0) return FileRole::auxiliary;
    return FileRole::unknown;
}
inline constexpr std::array<std::string_view,8> required_files{
    "rayman2.exe", "ubi.ini", "dll/snd_cpa.ini", "dll/glivd1vf.dll",
    "data/game.dsb", "data/textures.cnt", "data/vignette.cnt",
    "data/world/levels/levels0.dat"
};
inline bool has_required_files(const std::set<std::string>& files) {
    for (auto path : required_files)
        if (!files.count(std::string(path))) return false;
    return true;
}
} // namespace r2fix::gog
