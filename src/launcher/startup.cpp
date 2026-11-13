#include "rayman2fix/startup.hpp"
#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/game_config.hpp"

#include <chrono>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <ctime>
#include <array>
#include <vector>
#include <utility>
#include <cstring>
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
    game_dir_ = root_ / "Rayman2Fix";
    ini_path_ = game_dir_ / "Rayman2Fix.ini";
    log_path_ = root_ / "Rayman2Fix_installer.log";
    runtime_log_path_ = root_ / "Rayman2Fix.log";
}

#ifdef _WIN32
namespace {

// The Windows launcher stores its own x86 runtime DLL as RCDATA #1101.
// Retain at most one backup: Rayman2Fix.dll.bak always contains the
// immediately preceding DLL, replacing the previous backup after success.
// Previous bootstrap versions generated Rayman2Fix.dll.backup.1, .2...
// Keep only the newest legacy file as .bak if no backup exists yet; otherwise
// delete only those precisely named obsolete launcher-generated backups.
void prune_legacy_runtime_backups(const fs::path& directory) {
    const std::string prefix = "Rayman2Fix.dll.backup.";
    std::vector<std::pair<unsigned long, fs::path>> found;
    for (const auto& entry : fs::directory_iterator(directory)) {
        const auto filename = entry.path().filename().u8string();
        if (filename.rfind(prefix, 0) != 0) continue;
        const auto suffix = filename.substr(prefix.size());
        if (suffix.empty() ||
            !std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) {
                return c >= '0' && c <= '9';
            })) continue;
        if (entry.is_symlink() || !entry.is_regular_file())
            throw std::runtime_error("unsafe old Rayman2Fix.dll backup path");
        unsigned long id = 0;
        try { id = std::stoul(suffix); }
        catch (...) { throw std::runtime_error("malformed old runtime backup number"); }
        found.emplace_back(id, entry.path());
    }
    if (found.empty()) return;
    const fs::path backup = directory / "Rayman2Fix.dll.bak";
    if (fs::is_symlink(backup) ||
        (fs::exists(backup) && !fs::is_regular_file(backup)))
        throw std::runtime_error("unsafe Rayman2Fix.dll.bak path");
    if (!fs::exists(backup)) {
        const auto latest = std::max_element(found.begin(), found.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        fs::rename(latest->second, backup);
    }
    for (const auto& entry : found) {
        if (fs::exists(entry.second))
            fs::remove(entry.second);
    }
}

void ensure_embedded_runtime(const fs::path& directory) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(1101),
                                        MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!resource) throw std::runtime_error("Rayman2Fix.exe is missing bundled runtime DLL");
    const DWORD length = SizeofResource(nullptr, resource);
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto* bytes = loaded ?
        static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
    if (!bytes || !length)
        throw std::runtime_error("bundled Rayman2Fix.dll is empty or unreadable");

    const auto dest = directory / "Rayman2Fix.dll";
    if (fs::is_symlink(dest))
        throw std::runtime_error("refusing a symlink at runtime DLL destination");
    bool identical = fs::is_regular_file(dest) && fs::file_size(dest) == length;
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
    if (identical) {
        prune_legacy_runtime_backups(directory);
        return;
    }

    const auto temporary = directory / "Rayman2Fix.dll.installing";
    if (fs::exists(temporary))
        throw std::runtime_error("old runtime staging file exists; refusing overwrite");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot create bundled runtime DLL");
        out.write(reinterpret_cast<const char*>(bytes),
                  static_cast<std::streamsize>(length));
        if (!out) throw std::runtime_error("error writing bundled runtime DLL");
    }
    fs::permissions(temporary, fs::perms::owner_write, fs::perm_options::add);

    const fs::path backup = directory / "Rayman2Fix.dll.bak";
    const fs::path prior = directory / "Rayman2Fix.dll.bak.previous";
    if (fs::exists(prior) || fs::is_symlink(prior)) {
        fs::remove(temporary);
        throw std::runtime_error("unfinished Rayman2Fix.dll backup rotation; preserving files");
    }
    if (fs::is_symlink(backup) ||
        (fs::exists(backup) && !fs::is_regular_file(backup))) {
        fs::remove(temporary);
        throw std::runtime_error("Rayman2Fix.dll.bak is not a safe backup file");
    }
    if (fs::exists(dest) && !fs::is_regular_file(dest)) {
        fs::remove(temporary);
        throw std::runtime_error("Rayman2Fix.dll is not a regular file");
    }

    bool moved_prior = false;
    bool backed_up_current = false;
    bool published = false;
    try {
        if (fs::exists(dest)) {
            if (fs::exists(backup)) {
                fs::rename(backup, prior);
                moved_prior = true;
            }
            fs::rename(dest, backup);
            backed_up_current = true;
        }
        fs::rename(temporary, dest);
        published = true;
    } catch (...) {
        std::error_code ignored;
        // Restore both the current runtime and the previous backup if the
        // replacement could not be completed. Never overwrite user data.
        if (backed_up_current) {
            if (published) fs::remove(dest, ignored);
            if (!fs::exists(dest))
                fs::rename(backup, dest, ignored);
        }
        if (moved_prior && !fs::exists(backup))
            fs::rename(prior, backup, ignored);
        fs::remove(temporary, ignored);
        throw;
    }

    // Only after the new DLL is safely published do we remove the older .bak.
    // On success there is exactly one backup of the immediately prior DLL.
    if (moved_prior) {
        fs::permissions(prior, fs::perms::owner_write, fs::perm_options::add);
        fs::remove(prior);
    }
    prune_legacy_runtime_backups(directory);
}
} // namespace
#endif

void Startup::initialize() {
    if (fs::exists(game_dir_) && !fs::is_directory(game_dir_))
        throw std::runtime_error("Rayman2Fix installation path is not a directory");
    fs::create_directories(game_dir_);
#ifdef _WIN32
    ensure_embedded_runtime(game_dir_);
    ensure_gameframe_glide(game_dir_);
#endif

    // Existing settings belong to the user. No silent regeneration, and no
    // claim that currently unimplemented options actually change the game.
    if (!fs::exists(ini_path_)) {
        std::ofstream ini(ini_path_, std::ios::binary | std::ios::out);
        if (!ini) throw std::runtime_error("cannot create Rayman2Fix.ini");
        ini << "[Debug]\n"
               "Log=1\n"
               "\n"
               "[Graphics]\n"
               "Resolution=Desktop\n"
               "; FPSLimit=0 disables the cap; accepted range: 15..240\n"
               "FPSLimit=60\n"
               "; 1 = centered 4:3 picture (black sidebars)\n"
               "Force4x3=0\n"
               "\n"
               "[Features]\n"
               "; 1 = allow Space/Enter/Esc to skip jail_20 only\n"
               "; Learn_10 skip remains disabled (unsafe)\n"
               "SkipCinematics=0\n";
        if (!ini) throw std::runtime_error("failed writing Rayman2Fix.ini");
    }

    // Preserve user values; add new opt-in options to older config files.
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
    log("LAYOUT: launcher=Rayman2Fix.exe install=Rayman2Fix/ config=Rayman2Fix/Rayman2Fix.ini installer_log=Rayman2Fix_installer.log runtime_log=Rayman2Fix.log");
    log("CONFIG: Debug.Log=" +
        std::string(debug == "1" ? "1" : "invalid or missing (default 1)"));
    const auto display = option("Graphics", "Resolution");
    log("CONFIG: Graphics.Resolution=" +
        (display.empty() ? "Desktop (default)" : display));
    const auto fps = option("Graphics", "FPSLimit");
    log("CONFIG: Graphics.FPSLimit=" +
        (fps.empty() ? "60 (default)" : fps));
    log(std::string("CONFIG: Graphics.Force4x3=") +
        (configured_force_4_3(ini_path_) ? "1" : "0"));
    log(std::string("CONFIG: Features.SkipCinematics=") +
        (configured_skip_cinematics(ini_path_) ? "1" : "0"));

    log("RUNTIME: Rayman2Fix.dll=" +
        std::string(has_runtime_dll() ? "present" : "not packaged in this directory"));
    if (gameframe_glide_ready(game_dir_))
        log("RENDERER: GameFrame glide2x.dll=verified sha256=" +
            gameframe_glide_expected_sha256() +
            " upstream=f195a074a63cc150673c38becdec696163b0b3d1");
    else
        log("RENDERER: GameFrame glide2x.dll=not bundled in this developer build");
    log("INPUT: SDL3 deferred to second phase; controller support not yet active");
    log("INSTALL: no verified CD edition profile, no playable installation");
}

void Startup::prepare_runtime_log() {
    if (!logging_enabled_) return;
    std::ofstream out(runtime_log_path_, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create Rayman2Fix.log");
    out << "RUNTIME-SESSION: Rayman2Fix.dll and GameFrame diagnostics; "
           "installer=Rayman2Fix_installer.log\n";
    out.close();
    if (!out) throw std::runtime_error("cannot initialize Rayman2Fix.log");
}

void Startup::log(const std::string& line) {
    if (log_stream_) {
        log_stream_ << line << '\n';
        log_stream_.flush();
    }
}

bool Startup::has_runtime_dll() const {
    return fs::is_regular_file(game_dir_ / "Rayman2Fix.dll");
}

} // namespace r2fix
