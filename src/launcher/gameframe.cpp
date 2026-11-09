#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/sha256.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace r2fix {
namespace {
#ifdef _WIN32
// Fails closed: only the pinned GameFrame build may be installed.
// Missing resources are allowed in local developer/test builds, not CI.
#ifndef RAYMAN2FIX_GLIDE_SHA256
#define RAYMAN2FIX_GLIDE_SHA256 ""
#endif
static_assert(sizeof(RAYMAN2FIX_GLIDE_SHA256) == 1 ||
              sizeof(RAYMAN2FIX_GLIDE_SHA256) == 65,
              "pinned GameFrame SHA256 must be a 64-character hex digest");

struct Resource {
    const unsigned char* data;
    DWORD size;
};
Resource get_resource(int id) {
    const auto res = FindResourceW(nullptr, MAKEINTRESOURCEW(id),
                                   MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!res) throw std::runtime_error("missing embedded GameFrame resource " + std::to_string(id));
    const DWORD length = SizeofResource(nullptr, res);
    const HGLOBAL loaded = LoadResource(nullptr, res);
    const auto* bytes = loaded ?
        static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
    if (!bytes || !length) throw std::runtime_error("empty embedded GameFrame resource");
    return {bytes, length};
}
void write_and_replace(const fs::path& dest, const Resource& payload,
                       const std::string& expected_hash) {
    if (fs::is_symlink(dest))
        throw std::runtime_error("GameFrame destination is a symlink");
    if (fs::exists(dest) && !fs::is_regular_file(dest))
        throw std::runtime_error("GameFrame destination is not a regular file");
    if (fs::is_regular_file(dest) && fs::file_size(dest) == payload.size &&
        sha256_file(dest) == expected_hash)
        return;

    fs::path pending = dest; pending += ".installing";
    if (fs::exists(pending) || fs::is_symlink(pending))
        throw std::runtime_error("existing GameFrame installation staging file");
    {
        std::ofstream output(pending, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot stage GameFrame library");
        output.write(reinterpret_cast<const char*>(payload.data),
                     static_cast<std::streamsize>(payload.size));
        output.close();
        if (!output) throw std::runtime_error("error writing GameFrame library");
    }
    if (sha256_file(pending) != expected_hash) {
        fs::remove(pending);
        throw std::runtime_error("embedded GameFrame library SHA256 mismatch");
    }
    fs::permissions(pending, fs::perms::owner_write, fs::perm_options::add);

    fs::path backup;
    if (fs::exists(dest)) {
        // Preserve the first original as .bak; choose a unique numbered
        // backup when it already exists. Never silently overwrite backups.
        auto candidate = dest; candidate += ".bak";
        if (!fs::exists(candidate)) backup = candidate;
        else {
            for (unsigned i = 1; i <= 1000; ++i) {
                candidate = dest;
                candidate += ".bak." + std::to_string(i);
                if (!fs::exists(candidate)) { backup = candidate; break; }
            }
        }
        if (backup.empty()) {
            fs::remove(pending);
            throw std::runtime_error("too many GameFrame backup files");
        }
        try { fs::rename(dest, backup); }
        catch (...) { fs::remove(pending); throw; }
    }
    try {
        fs::rename(pending, dest);
    } catch (...) {
        std::error_code ignored;
        if (!backup.empty()) fs::rename(backup, dest, ignored);
        fs::remove(pending, ignored);
        throw;
    }
}
#endif
}

std::string gameframe_glide_expected_sha256() {
#ifdef _WIN32
    return RAYMAN2FIX_GLIDE_SHA256;
#else
    return "";
#endif
}
bool gameframe_glide_ready(const fs::path& game_dir) {
    const auto expected = gameframe_glide_expected_sha256();
    if (expected.empty()) return false;
    const auto dest = game_dir / "glide2x.dll";
    return fs::is_regular_file(dest) && !fs::is_symlink(dest) &&
           sha256_file(dest) == expected;
}
void ensure_gameframe_glide(const fs::path& game_dir) {
#ifdef _WIN32
    const auto expected = gameframe_glide_expected_sha256();
    if (expected.empty()) return;
    const auto glide = get_resource(1103);
    const auto license = get_resource(1104);
    // A bundled license accompanies the Apache 2.0 GameFrame binary, while
    // the release ZIP still contains only the Rayman2Fix.exe launcher.
    write_and_replace(game_dir / "glide2x.dll", glide, expected);
    const fs::path license_file = game_dir / "GameFrame-LICENSE-APACHE.txt";
    const std::string license_text(reinterpret_cast<const char*>(license.data), license.size);
    Sha256 checksum;
    checksum.update(license.data, license.size);
    write_and_replace(license_file, license, checksum.finish());
    if (!gameframe_glide_ready(game_dir))
        throw std::runtime_error("GameFrame post-install SHA256 verification failed");
#else
    (void)game_dir;
#endif
}
} // namespace r2fix
