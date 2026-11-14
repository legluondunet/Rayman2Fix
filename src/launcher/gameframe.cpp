#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/sha256.hpp"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <vector>
#include <utility>
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
// Pre-single-backup builds created glide2x.dll.bak.1, .bak.2 ...
// and likewise for the bundled license. Only touch the exact .bak.NUMBER
// pattern for the requested, launcher-managed destination, never generic
// DLLs or unrelated user files. Keep the newest numbered backup if there
// is no .bak yet; otherwise retain the existing .bak.
void prune_numbered_backups(const fs::path& dest) {
    fs::path backup=dest; backup+=".bak";
    if(fs::is_symlink(backup) ||
       (fs::exists(backup) && !fs::is_regular_file(backup)))
        throw std::runtime_error("unsafe GameFrame .bak destination");
    const std::string prefix=dest.filename().u8string()+".bak.";
    std::vector<std::pair<unsigned long,fs::path>> legacy;
    for(const auto& entry:fs::directory_iterator(dest.parent_path())) {
        const auto name=entry.path().filename().u8string();
        if(name.rfind(prefix,0)!=0)continue;
        const auto suffix=name.substr(prefix.size());
        if(suffix.empty() || suffix.size()>10u ||
           !std::all_of(suffix.begin(),suffix.end(),[](unsigned char ch){
               return ch>='0' && ch<='9';
           }))continue;
        if(entry.is_symlink() || !entry.is_regular_file())
            throw std::runtime_error("unsafe legacy GameFrame .bak.NUMBER file");
        legacy.emplace_back(std::stoul(suffix),entry.path());
    }
    if(legacy.empty())return;
    if(!fs::exists(backup)) {
        const auto newest=std::max_element(legacy.begin(),legacy.end(),
            [](const auto& a,const auto& b){return a.first<b.first;});
        fs::rename(newest->second,backup);
    }
    for(const auto& [id,path]:legacy) {
        (void)id;
        if(fs::exists(path)) {
            fs::permissions(path,fs::perms::owner_write,fs::perm_options::add);
            fs::remove(path);
        }
    }
}

void write_and_replace(const fs::path& dest, const Resource& payload,
                       const std::string& expected_hash) {
    if(fs::is_symlink(dest))
        throw std::runtime_error("GameFrame destination is a symlink");
    if(fs::exists(dest) && !fs::is_regular_file(dest))
        throw std::runtime_error("GameFrame destination is not a regular file");
    // Even if the current DLL matches, migrate old .bak.1/.bak.2 files
    // left by earlier releases. Never create a backup on a no-op update.
    if(fs::is_regular_file(dest) && fs::file_size(dest)==payload.size &&
       sha256_file(dest)==expected_hash) {
        prune_numbered_backups(dest);
        return;
    }

    fs::path pending=dest; pending+=".installing";
    fs::path backup=dest; backup+=".bak";
    fs::path prior=dest; prior+=".bak.previous";
    if(fs::exists(pending) || fs::is_symlink(pending) ||
       fs::exists(prior) || fs::is_symlink(prior))
        throw std::runtime_error("unfinished GameFrame backup/staging rotation");
    if(fs::is_symlink(backup) ||
       (fs::exists(backup) && !fs::is_regular_file(backup)))
        throw std::runtime_error("unsafe GameFrame .bak file");
    // Never expose an unverified DLL as the active game renderer.
    {
        std::ofstream output(pending,std::ios::binary|std::ios::trunc);
        if(!output)throw std::runtime_error("cannot stage GameFrame library");
        output.write(reinterpret_cast<const char*>(payload.data),
                     static_cast<std::streamsize>(payload.size));
        output.close();
        if(!output)throw std::runtime_error("error writing GameFrame library");
    }
    if(sha256_file(pending)!=expected_hash) {
        fs::remove(pending);
        throw std::runtime_error("embedded GameFrame SHA256 mismatch");
    }
    fs::permissions(pending,fs::perms::owner_write,fs::perm_options::add);

    bool moved_prior=false,backed_up_current=false,published=false;
    try {
        if(fs::exists(dest)) {
            if(fs::exists(backup)) {
                fs::rename(backup,prior);
                moved_prior=true;
            }
            fs::rename(dest,backup);
            backed_up_current=true;
        }
        fs::rename(pending,dest);
        published=true;
    }catch(...) {
        std::error_code ignored;
        // If publishing fails, restore the previously working DLL and its
        // preceding backup, rather than losing either of them.
        if(backed_up_current) {
            if(published)fs::remove(dest,ignored);
            if(!fs::exists(dest))fs::rename(backup,dest,ignored);
        }
        if(moved_prior && !fs::exists(backup))
            fs::rename(prior,backup,ignored);
        fs::remove(pending,ignored);
        throw;
    }
    // The new renderer is verified and published. Keep exactly the DLL it
    // replaced in .bak; only then discard the older .bak.previous.
    if(moved_prior) {
        fs::permissions(prior,fs::perms::owner_write,fs::perm_options::add);
        fs::remove(prior);
    }
    prune_numbered_backups(dest);
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
