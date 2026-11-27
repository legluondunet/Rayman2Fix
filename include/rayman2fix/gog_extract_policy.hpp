#pragma once
#include "rayman2fix/gog_source_policy.hpp"
#include <array>
#include <set>
#include <map>
#include <sstream>
#include <iterator>

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
    Args args{L"--extract", L"--test", L"--list-sizes", L"--silent",
              L"--no-extract-unknown",
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

// List exactly the same selected files without writing anything. The
// listing preflight rejects incomplete or oversized archives before extraction.
inline Args list_args(const fs::path& setup, const fs::path& staging,
                      const std::wstring& language = {}) {
    auto args = extract_args(setup, staging, language);
    args.erase(std::remove(args.begin(), args.end(), L"--extract"), args.end());
    args.erase(std::remove(args.begin(), args.end(), L"--test"), args.end());
    const auto output = std::find(args.begin(), args.end(), L"--output-dir");
    if (output == args.end() || std::next(output) == args.end())
        throw std::runtime_error("missing extractor output argument");
    args.erase(output, std::next(output, 2));
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
// Innoextract 1.9 --silent --list-sizes prints one "bytes path" line
// per selected file. Combined with --extract --test this independently
// describes the expected payload, including files skipped on warnings.
using FileSizes = std::map<std::string, std::uintmax_t>;
inline FileSizes parse_extraction_manifest(std::string_view output) {
    constexpr std::uintmax_t limit = 4ull * 1024 * 1024 * 1024;
    FileSizes files;
    std::uintmax_t total = 0;
    std::istringstream in{std::string(output)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto space = line.find(' ');
        if (line.size() > 8192 || space == std::string::npos ||
            space == 0 || space > 20 || space + 1 == line.size())
            throw std::runtime_error("invalid extractor file manifest");
        std::uintmax_t size = 0;
        for (std::size_t i = 0; i < space; ++i) {
            const char c = line[i];
            if (c < '0' || c > '9' || size > (limit - (c - '0')) / 10)
                throw std::runtime_error("invalid extractor manifest size");
            size = size * 10 + static_cast<unsigned>(c - '0');
        }
        auto name = line.substr(space + 1);
        std::replace(name.begin(), name.end(), '\\', '/');
        // Reject double/leading/trailing separators rather than letting
        // filesystem::path silently collapse them.
        if (name.front() == '/' || name.back() == '/' ||
            name.find("//") != std::string::npos)
            throw std::runtime_error("invalid extractor manifest path");
        name = normalized_path(fs::u8path(name));
        if (!files.emplace(name, size).second || files.size() > 50000 ||
            size > limit - total)
            throw std::runtime_error("duplicate or oversized extractor manifest");
        total += size;
    }
    if (files.empty()) throw std::runtime_error("empty extractor file manifest");
    return files;
}
inline void verify_extraction_manifest(const FileSizes& expected,
                                       const FileSizes& actual) {
    for (const auto& item : expected) {
        const auto found = actual.find(item.first);
        if (found == actual.end() || found->second != item.second)
            throw std::runtime_error("GOG extracted file missing or wrong size: " + item.first);
    }
    if (actual.size() != expected.size())
        throw std::runtime_error("GOG payload contains unlisted files");
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
// Preflight the selection without relying on archive name, build or hash.
inline void validate_manifest_layout(const FileSizes& files) {
    for (auto path : required_files) {
        const auto found = files.find(std::string(path));
        if (found == files.end() || !found->second)
            throw std::runtime_error("GOG setup lacks required file: " + std::string(path));
    }
    if (files.at("rayman2.exe") != executable_size)
        throw std::runtime_error("GOG setup declares an unsupported game executable size");
    const auto metadata = files.find("goggame-1207658940.info");
    if (metadata == files.end() || !metadata->second)
        throw std::runtime_error("GOG setup lacks product metadata");
}
} // namespace r2fix::gog
