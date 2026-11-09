#include "rayman2fix/patch_install.hpp"
#include "rayman2fix/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace r2fix {
namespace {
constexpr const char* kMarker = ".rayman2fix-exe-patch.tsv";
constexpr const char* kMarkerHeader = "RAYMAN2FIX-EXE-PATCH\t1";
constexpr std::uint64_t kMaxSize = 64ull * 1024 * 1024;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("EXE replacement: " + message);
}
bool clean_file(const fs::path& path) {
    return fs::is_regular_file(path) && !fs::is_symlink(path);
}
bool is_digest(const std::string& value) {
    return value.size() == 64 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
}
std::uint64_t parse_decimal(const std::string& str) {
    if (str.empty() || !std::all_of(str.begin(), str.end(), [](unsigned char c) {
        return c >= '0' && c <= '9';
    })) fail("bad size in patch marker");
    const auto result = std::stoull(str);
    if (!result || result > kMaxSize) fail("unsafe size in patch marker");
    return result;
}
std::pair<std::uint64_t, std::string> parse_marker_row(
    const std::string& line, const char* label) {
    const std::string prefix = std::string(label) + '\t';
    if (line.rfind(prefix, 0) != 0) fail("unexpected patch marker entry");
    const auto tab = line.find('\t', prefix.size());
    if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos)
        fail("invalid patch marker fields");
    const auto length = parse_decimal(line.substr(prefix.size(), tab - prefix.size()));
    const auto hash = line.substr(tab + 1);
    if (!is_digest(hash)) fail("invalid SHA256 in patch marker");
    return {length, hash};
}
ExePatchHeader load_marker(const fs::path& game_root) {
    const auto marker = game_root / kMarker;
    if (!clean_file(marker) || fs::file_size(marker) > 512)
        fail("invalid executable patch marker");
    std::ifstream in(marker, std::ios::binary);
    std::string header, original, patched, trailing;
    if (!std::getline(in, header) || header != kMarkerHeader ||
        !std::getline(in, original) || !std::getline(in, patched) ||
        std::getline(in, trailing) || in.bad())
        fail("incomplete executable patch marker");
    const auto src = parse_marker_row(original, "ORIGINAL");
    const auto dst = parse_marker_row(patched, "PATCHED");
    return {src.first, dst.first, src.second, dst.second};
}
void save_marker(const fs::path& path, const ExePatchHeader& info) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) fail("cannot stage executable patch marker");
    out << kMarkerHeader << '\n'
        << "ORIGINAL\t" << info.original_size << '\t' << info.original_sha256 << '\n'
        << "PATCHED\t" << info.patched_size << '\t' << info.patched_sha256 << '\n';
    out.flush();
    if (!out) fail("cannot write executable patch marker");
}
bool match(const fs::path& file, std::uint64_t expected_size,
           const std::string& expected_sha) {
    return clean_file(file) && fs::file_size(file) == expected_size &&
           sha256_file(file) == expected_sha;
}
} // namespace

bool is_exe_replaced(const fs::path& root) {
    return fs::exists(root / kMarker);
}

bool verify_exe_replacement(const fs::path& root,
                            const std::string& expected_cd_sha256,
                            std::uint64_t expected_cd_size,
                            std::string& reason) {
    try {
        if (!is_digest(expected_cd_sha256) || !expected_cd_size ||
            expected_cd_size > kMaxSize)
            fail("invalid source-executable receipt");
        const fs::path exe = root / "rayman2.exe";
        const fs::path bak = root / "rayman2.exe.bak";
        const fs::path marker = root / kMarker;
        if (!fs::exists(marker)) {
            if (fs::exists(bak) || fs::is_symlink(bak))
                fail("unexpected rayman2.exe.bak without patch marker");
            if (!match(exe, expected_cd_size, expected_cd_sha256))
                fail("original executable SHA256 mismatch");
            return true;
        }
        const auto info = load_marker(root);
        if (info.original_size != expected_cd_size ||
            info.original_sha256 != expected_cd_sha256)
            fail("patch original SHA256 does not match the CD receipt");
        if (!match(bak, info.original_size, info.original_sha256))
            fail("rayman2.exe.bak does not match the original CD executable");
        if (!match(exe, info.patched_size, info.patched_sha256))
            fail("patched rayman2.exe does not match its patch SHA256");
        return true;
    } catch (const std::exception& error) {
        reason = error.what();
        return false;
    }
}

ExePatchHeader install_exe_replacement(
    const fs::path& root, const std::vector<unsigned char>& patch) {
    const auto info = inspect_exe_patch(patch);
    const fs::path original = root / "rayman2.exe";
    const fs::path backup = root / "rayman2.exe.bak";
    const fs::path patched = root / "rayman2.exe.patching";
    const fs::path marker = root / kMarker;
    const fs::path marker_new = root / ".rayman2fix-exe-patch.tsv.new";
    if (!clean_file(original) || fs::exists(backup) || fs::is_symlink(backup) ||
        fs::exists(patched) || fs::is_symlink(patched) ||
        fs::exists(marker) || fs::is_symlink(marker) ||
        fs::exists(marker_new) || fs::is_symlink(marker_new))
        fail("existing/missing files prevent safe EXE replacement; preserving original");
    if (!match(original, info.original_size, info.original_sha256))
        fail("original CD executable has unsupported SHA256");
    // Reconstruct and verify SHA256 *before* changing the original game file.
    try {
        const auto applied = apply_exe_patch(original, patched, patch);
        save_marker(marker_new, applied);
    } catch (...) {
        std::error_code ignored;
        fs::remove(patched, ignored);
        fs::remove(marker_new, ignored);
        throw;
    }
    fs::rename(original, backup);
    bool renamed_patched = false;
    try {
        fs::rename(patched, original);
        renamed_patched = true;
        fs::rename(marker_new, marker);
    } catch (...) {
        std::error_code ignored;
        if (renamed_patched) fs::remove(original, ignored);
        // Never remove a good original backup unless the restore succeeded.
        fs::rename(backup, original, ignored);
        fs::remove(patched, ignored);
        fs::remove(marker_new, ignored);
        throw;
    }
    return info;
}
} // namespace r2fix
