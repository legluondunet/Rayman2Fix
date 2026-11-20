#include "rayman2fix/startup.hpp"
#include "rayman2fix/install_layout.hpp"
#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/game_config.hpp"

#include <chrono>
#include <cstdint>
#include <system_error>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <ctime>
#include <array>
#include <vector>
#include <utility>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace r2fix {

// Passive inspection only: a valid header does not prove that the source logo
// tiles were recognized or that GameFrame uploaded the replacement.
namespace {
std::string intro_logo_asset_status(const fs::path& root) {
    const auto asset = layout::mods(root) / "textures" / "logo.rgba";
    std::error_code ec;
    const auto st = fs::symlink_status(asset, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        st.type() == fs::file_type::not_found)
        return "missing";
    if (ec)
        return "unreadable-metadata";
    if (!fs::is_regular_file(st) || fs::is_symlink(st))
        return "invalid-file-type";

    constexpr std::uintmax_t kAtlasSize = 16u + 768u * 384u * 4u;
    const auto size = fs::file_size(asset, ec);
    if (ec)
        return "unreadable-metadata";
    if (size != kAtlasSize)
        return "invalid-size";

    std::array<unsigned char, 16> header{};
    std::ifstream in(asset, std::ios::binary);
    if (!in || !in.read(reinterpret_cast<char*>(header.data()),
                        static_cast<std::streamsize>(header.size())))
        return "unreadable-header";
    constexpr std::array<unsigned char, 8> kMagic{{
        'R', '2', 'L', 'O', 'G', 'O', 'H', 'D'
    }};
    const auto u32le = [&](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(header[offset]) |
               (static_cast<std::uint32_t>(header[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(header[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(header[offset + 3]) << 24);
    };
    if (!std::equal(kMagic.begin(), kMagic.end(), header.begin()) ||
        u32le(8) != 768u || u32le(12) != 384u)
        return "invalid-header";

    return "header-valid"; // Actual substitution is logged by GameFrame.
}
} // namespace

fs::path Startup::executable_path(const fs::path& argv0) {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(),
                                       static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size())
        throw std::runtime_error("cannot determine launcher path");
    buffer.resize(n);
    return fs::path(buffer);
#elif defined(__linux__)
    std::string buffer(4096, '\0');
    const auto n = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (n > 0 && static_cast<std::size_t>(n) < buffer.size()) {
        buffer.resize(static_cast<std::size_t>(n));
        return fs::path(buffer);
    }
    return fs::absolute(argv0);
#else
    return fs::absolute(argv0);
#endif
}

Startup::Startup(const fs::path& executable) {
    root_ = fs::absolute(executable).parent_path();
    game_dir_ = layout::game(root_);
    ini_path_ = root_ / "Rayman2Fix.ini";
    log_path_ = root_ / "Rayman2Fix_installer.log";
    runtime_log_path_ = root_ / "Rayman2Fix.log";
}

#ifdef _WIN32
namespace {

// The Windows launcher stores its own x86 runtime DLL as RCDATA #1101.
// The embedded DLL is authoritative; unknown runtime versions are replaced.
// No old runtime backup is created or preserved.
void ensure_embedded_readme(const fs::path& root) {
    // User documentation lives under docs/; state/ contains only receipts.
    const auto dest=layout::docs(root)/"README.md";
    if(fs::is_symlink(dest) ||
       (fs::exists(dest) && !fs::is_regular_file(dest)))
        throw std::runtime_error("unsafe existing README.md path");
    if(fs::exists(dest))return; // Never overwrite user documentation.
    const HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(1105),
                                      MAKEINTRESOURCEW(10));
    if(!resource)throw std::runtime_error("missing bundled README.md");
    const auto size=SizeofResource(nullptr,resource);
    const HGLOBAL loaded=LoadResource(nullptr,resource);
    const auto* bytes=loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if(!bytes || !size)throw std::runtime_error("invalid bundled README.md");
    std::ofstream out(dest,std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("cannot deploy README.md");
    out.write(bytes,static_cast<std::streamsize>(size));
    out.close();
    if(!out)throw std::runtime_error("could not write bundled README.md");
}

// The launcher owns game/fix.dll. Obsolete copies from older launcher builds
// are not retained: the verified embedded runtime is the only supported one.
struct RuntimeDeployment {
    const char* status;
    std::size_t backups_removed;
};

std::size_t discard_legacy_runtime_backups(const fs::path& directory) {
    std::size_t removed = 0;
    for (const auto* name : {"fix.dll.bak", "fix.dll.bak.previous"}) {
        const auto old = directory / name;
        if (!fs::exists(old) && !fs::is_symlink(old))
            continue;
        if (fs::is_directory(old) && !fs::is_symlink(old))
            throw std::runtime_error("refusing to remove a directory at obsolete runtime backup path");
        // Old backups may have inherited the read-only flag from the CD or
        // an older runtime. Unlink the backup itself, never a link target.
        if (!fs::is_symlink(old)) {
            const DWORD attrs = GetFileAttributesW(old.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
                throw std::runtime_error("cannot inspect obsolete fix.dll backup");
            if ((attrs & FILE_ATTRIBUTE_READONLY) &&
                !SetFileAttributesW(old.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY))
                throw std::runtime_error("cannot clear read-only obsolete fix.dll backup");
        }
        if (fs::remove(old)) ++removed; // Never follow a link target.
    }
    return removed;
}

RuntimeDeployment ensure_embedded_runtime(const fs::path& directory) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(1101),
                                        MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!resource) throw std::runtime_error("Rayman2Fix.exe is missing bundled runtime DLL");
    const DWORD length = SizeofResource(nullptr, resource);
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto* bytes = loaded ?
        static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
    if (!bytes || !length)
        throw std::runtime_error("bundled fix.dll is empty or unreadable");

    const auto dest = directory / "fix.dll";
    // Unknown DLLs are replaced, but never follow links or overwrite folders.
    if (fs::is_symlink(dest) ||
        (fs::exists(dest) && !fs::is_regular_file(dest)))
        throw std::runtime_error("unsafe game/fix.dll destination");
    const bool has_existing = fs::exists(dest);
    bool identical = has_existing && fs::file_size(dest) == length;
    if (identical) {
        std::ifstream old(dest, std::ios::binary);
        std::array<char, 4096> block{};
        std::size_t offset = 0;
        while (offset < length && old) {
            const std::size_t n = std::min<std::size_t>(block.size(), length - offset);
            old.read(block.data(), static_cast<std::streamsize>(n));
            if (!old || std::memcmp(block.data(), bytes + offset, n) != 0) {
                identical = false;
                break;
            }
            offset += n;
        }
    }
    if (identical)
        return {"unchanged", discard_legacy_runtime_backups(directory)};

    // Write a complete new DLL first. MoveFileExW with REPLACE_EXISTING
    // replaces it atomically on the same volume: failure keeps the old DLL.
    // No fix.dll.bak (or numbered backup) is ever created.
    const auto temporary = directory / "fix.dll.installing";
    if (fs::exists(temporary) || fs::is_symlink(temporary))
        throw std::runtime_error("old runtime staging file exists; refusing overwrite");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot stage bundled fix.dll");
        out.write(reinterpret_cast<const char*>(bytes),
                  static_cast<std::streamsize>(length));
        out.close();
        if (!out) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw std::runtime_error("error writing bundled fix.dll");
        }
    }

    // Windows may refuse replacement of a read-only DLL. Clear only that
    // attribute; if publication fails, restore the old file's attributes.
    DWORD previous_attributes = INVALID_FILE_ATTRIBUTES;
    bool old_was_readonly = false;
    if (has_existing) {
        previous_attributes = GetFileAttributesW(dest.c_str());
        if (previous_attributes == INVALID_FILE_ATTRIBUTES) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw std::runtime_error("cannot inspect existing game/fix.dll attributes");
        }
        old_was_readonly = (previous_attributes & FILE_ATTRIBUTE_READONLY) != 0;
        if (old_was_readonly &&
            !SetFileAttributesW(dest.c_str(), previous_attributes & ~FILE_ATTRIBUTE_READONLY)) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw std::runtime_error("cannot make old game/fix.dll writable");
        }
    }

    if (!MoveFileExW(temporary.c_str(), dest.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        if (old_was_readonly)
            SetFileAttributesW(dest.c_str(), previous_attributes);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw std::runtime_error("cannot replace game/fix.dll with bundled runtime (Win32=" +
                                 std::to_string(error) + ")");
    }
    const auto removed = discard_legacy_runtime_backups(directory);
    return {has_existing ? "replaced" : "installed", removed};
}
} // namespace
#endif

void Startup::initialize(bool provision_files) {
    std::string runtime_deployment_status = "not-packaged";
    std::size_t runtime_backups_removed = 0;
    if (provision_files) {
        // Clean layout only: NEVER rename/copy an older installation.
        // Reject before creating a new workspace so the user can keep it.
        for (const auto& old : {root_/"rayman2",root_/"Rayman2Fix"}) {
            if (fs::exists(old) || fs::is_symlink(old))
                throw std::runtime_error(
                    "old Rayman2Fix installation detected; use a separate clean folder (no migration)");
        }
        const auto mods = layout::mods(root_);
        const auto textures = mods/"textures";
        const auto docs=layout::docs(root_);
        for (const auto& entry : {game_dir_,mods,textures,docs}) {
            if (fs::is_symlink(entry) ||
                (fs::exists(entry) && !fs::is_directory(entry)))
                throw std::runtime_error("unsafe clean Rayman2Fix layout directory");
            fs::create_directories(entry);
        }
#ifdef _WIN32
        const auto runtime = ensure_embedded_runtime(game_dir_);
        runtime_deployment_status = runtime.status;
        runtime_backups_removed = runtime.backups_removed;
        ensure_gameframe_glide(game_dir_);
        prune_obsolete_nested_gameframe(root_);
        ensure_embedded_readme(root_);
#endif
    }

    // Existing settings belong to the user. No silent regeneration, and no
    // claim that currently unimplemented options actually change the game.
    if (provision_files && !fs::exists(ini_path_)) {
        std::ofstream ini(ini_path_, std::ios::binary | std::ios::out);
        if (!ini) throw std::runtime_error("cannot create Rayman2Fix.ini");
        ini << "[Debug]\n"
               "Log=1\n"
               "; Rayman2Fix: auto-disable logs after first successful verified game run\n"
               "\n"
               "[Graphics]\n"

               "; FPSLimit=0 disables the cap; accepted range: 15..240\n"
               "FPSLimit=60\n"
               "; Auto = monitor aspect ratio; 4:3 = centered classic picture\n"
               "AspectRatio=Auto\n"
               "\n"
               "[Features]\n"
               "; 1 = Space/Enter/Esc skip for jail_20 and guarded F248 for learn_10\n"
               "SkipCinematics=1\n"
               "; 1 = hide native rectangles behind in-game dialogue\n"
               "RemoveDialogueBackgrounds=1\n"
               "; 1 = experimental game-drawn cinematic letterbox removal\n"
               "; GameFrame AspectRatio=4:3 sidebars remain unchanged\n"
               "RemoveCinematicBars=1\n"
               "; 1 = continue level music while the ESC menu is open\n"
               "MusicInPauseMenu=1\n";
        if (!ini) throw std::runtime_error("failed writing Rayman2Fix.ini");
    }

    // --launch-no-verify is intentionally side-effect-free for installed
    // game files/config: never create or migrate an INI in unchecked mode.
    // In particular, an absent rayman2/ directory must still reach
    // CreateProcessW and return the genuine operating-system error.
    if (provision_files)
        ensure_rayman2fix_ini_options(ini_path_);

    // Parse the only supported option before opening a log file.
    // Log=0 is an opt-out; never create a diagnostic log in that mode.
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char when[32]{};
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
#ifndef RAYMAN2FIX_VERSION
#define RAYMAN2FIX_VERSION "0.1.0"
#endif
#ifndef RAYMAN2FIX_BUILD_COMMIT
#define RAYMAN2FIX_BUILD_COMMIT "unknown"
#endif
    // The build identity is documentation in docs/, not a game/CD asset.
    // Update only our own recognized file and stage the replacement safely.
    if(provision_files) {
        const auto dest=layout::docs(root_)/"build-info.txt";
        if(fs::is_symlink(dest) ||
           (fs::exists(dest) && !fs::is_regular_file(dest)))
            throw std::runtime_error("unsafe build-info.txt");
        const std::string metadata=
            std::string("Project=Rayman2Fix\nVersion=")+RAYMAN2FIX_VERSION+
            "\nCommit="+RAYMAN2FIX_BUILD_COMMIT+
            "\nLayout=clean-v1\nRuntime=game/fix.dll\n"+
            "GameFrame=game/glide2x.dll\n";
        std::string prior;
        if(fs::exists(dest)) {
            std::ifstream old(dest,std::ios::binary);
            prior.assign(std::istreambuf_iterator<char>(old),
                         std::istreambuf_iterator<char>());
            if(old.bad() || prior.rfind("Project=Rayman2Fix\n",0)!=0)
                throw std::runtime_error("unknown build-info.txt; preserving user file");
        }
        if(prior!=metadata) {
            const auto pending=layout::docs(root_)/"build-info.txt.installing";
            const auto backup=layout::docs(root_)/"build-info.txt.previous";
            if(fs::exists(pending) || fs::is_symlink(pending) ||
               fs::exists(backup) || fs::is_symlink(backup))
                throw std::runtime_error("unfinished build-info.txt update");
            {
                std::ofstream out(pending,std::ios::binary|std::ios::trunc);
                if(!out)throw std::runtime_error("cannot stage build-info.txt");
                out << metadata;
                out.close();
                if(!out)throw std::runtime_error("cannot write build-info.txt");
            }
            const bool existed=fs::exists(dest);
            if(existed)fs::rename(dest,backup);
            try { fs::rename(pending,dest); }
            catch(...) {
                if(existed)fs::rename(backup,dest);
                throw;
            }
            if(existed)fs::remove(backup);
        }
    }

    // Log only specific, public configuration enums. Do not expose arbitrary
    // INI strings (which may contain private filesystem paths or tokens).
    auto option = [&](const std::string& section, const std::string& key) {
        std::ifstream in(ini_path_, std::ios::binary);
        std::string current, line;
        bool first_line = true;
        while (std::getline(in, line)) {
            if (first_line && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
            first_line = false;
            const auto trim = [](std::string v) {
                const auto first = v.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return std::string();
                return v.substr(first, v.find_last_not_of(" \t\r\n") - first + 1);
            };
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                current = line.substr(1, line.size() - 2);
                continue;
            }
            const auto equals = line.find('=');
            if (current == section && equals != std::string::npos &&
                trim(line.substr(0, equals)) == key)
                return trim(line.substr(equals + 1));
        }
        return std::string();
    };
    const auto debug = option("Debug", "Log");
    logging_enabled_ = debug != "0";
    if (!logging_enabled_) {
        // Prevent a stale log from looking like the latest run.
        std::error_code ignored;
        fs::remove(log_path_, ignored);
        fs::remove(runtime_log_path_, ignored);
        return;
    }
    // The installer log is reset each launcher run; an earlier game log is
    // retained unless another verified game session actually starts.
    log_stream_.open(log_path_, std::ios::binary | std::ios::trunc);
    if (!log_stream_) throw std::runtime_error("cannot create Rayman2Fix_installer.log");
    log(std::string("Rayman2Fix ") + RAYMAN2FIX_VERSION + "-bootstrap commit=" + RAYMAN2FIX_BUILD_COMMIT);
    log(std::string("START: local_time=") + when);
    log("LAYOUT: launcher=Rayman2Fix.exe game=game/ runtime=game/fix.dll config=Rayman2Fix.ini renderer=game/glide2x.dll mods=mods/ docs=docs/ state=state/ logs=launcher_directory");
    log("CONFIG: Debug.Log=" +
        std::string(debug == "1" ? "1" : "invalid or missing (default 1)"));
    log("CONFIG: Graphics.Output=primary desktop (automatic)");
    const auto fps = option("Graphics", "FPSLimit");
    log("CONFIG: Graphics.FPSLimit=" +
        (fps.empty() ? "60 (default)" : fps));
    const bool have_ini=fs::exists(ini_path_);
    log(std::string("CONFIG: Graphics.AspectRatio=") +
        (have_ini ? configured_aspect_ratio(ini_path_) : "Auto"));
    log(std::string("CONFIG: Features.SkipCinematics=") +
        (have_ini && configured_skip_cinematics(ini_path_) ? "1" : "0"));
    log(std::string("CONFIG: Features.RemoveDialogueBackgrounds=") +
        (have_ini && configured_remove_dialogue_backgrounds(ini_path_) ? "1" : "0"));
    log(std::string("CONFIG: Features.RemoveCinematicBars=") +
        (have_ini && configured_remove_cinematic_bars(ini_path_) ? "1" : "0"));
    log(std::string("CONFIG: Features.MusicInPauseMenu=") +
        (have_ini && configured_music_in_pause_menu(ini_path_) ? "1" : "0"));
    log("MODS: intro_logo=" + intro_logo_asset_status(root_) +
        " path=mods/textures/logo.rgba (header-only; replacement reported by GameFrame)");

    if (provision_files) {
        log("RUNTIME: fix.dll=" +
            std::string(has_runtime_dll() ? "present" : "not packaged in this directory"));
        log("RUNTIME-DEPLOY: status=" + runtime_deployment_status +
            " backups_removed=" + std::to_string(runtime_backups_removed) +
            " backup_created=0");
        if (gameframe_glide_ready(game_dir_))
            log("RENDERER: GameFrame glide2x.dll=verified sha256=" +
                gameframe_glide_expected_sha256() +
                " upstream=f195a074a63cc150673c38becdec696163b0b3d1");
        else
            log("RENDERER: GameFrame glide2x.dll=not bundled in this developer build");
        log("INPUT: SDL3 deferred to second phase; controller support not yet active");
        log("INSTALL: startup resources ready; CD/game verification follows (local SHA256 receipt only)");
    } else {
        log("STARTUP: --launch-no-verify skips DLL deployment, runtime "
            "hash checks, installation repair and INI creation");
    }
}

void Startup::prepare_runtime_log() {
    if (!logging_enabled_) return;
    std::ofstream out(runtime_log_path_, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create Rayman2Fix.log");
    out << "RUNTIME-SESSION: fix.dll and GameFrame diagnostics; "
           "installer=Rayman2Fix_installer.log\n";
    out.close();
    if (!out) throw std::runtime_error("cannot initialize Rayman2Fix.log");
}

void Startup::log(const std::string& line) {
    if (verbose_) std::cout << "[Rayman2Fix] " << line << '\n';
    if (log_stream_) {
        log_stream_ << line << '\n';
        log_stream_.flush();
    }
}

bool Startup::has_runtime_dll() const {
    return fs::is_regular_file(game_dir_ / "fix.dll");
}

} // namespace r2fix
