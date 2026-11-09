#include "rayman2fix/startup.hpp"

#include <chrono>
#include <ctime>
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
    log_path_ = root_ / "Rayman2Fix.log";
}

void Startup::initialize() {
    if (fs::exists(game_dir_) && !fs::is_directory(game_dir_))
        throw std::runtime_error("Rayman2Fix installation path is not a directory");
    fs::create_directories(game_dir_);

    // Existing settings belong to the user. No silent regeneration, and no
    // claim that currently unimplemented options actually change the game.
    if (!fs::exists(ini_path_)) {
        std::ofstream ini(ini_path_, std::ios::binary | std::ios::out);
        if (!ini) throw std::runtime_error("cannot create Rayman2Fix.ini");
        ini << "; Rayman2Fix bootstrap configuration\n"
               "; Graphical rendering and SDL3 runtime are not enabled yet.\n"
               "; Display and input sections become active after validation.\n"
               "[Graphics]\n"
               "DisplayMode=BorderlessFullscreen\n"
               "\n"
               "[Input]\n"
               "GamepadBackend=SDL3\n"
               "\n"
               "[Debug]\n"
               "Log=1\n";
        if (!ini) throw std::runtime_error("failed writing Rayman2Fix.ini");
    }

    // Exactly one diagnostic file beside the launcher. Clear it each run.
    log_stream_.open(log_path_, std::ios::binary | std::ios::trunc);
    if (!log_stream_) throw std::runtime_error("cannot create Rayman2Fix.log");
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
    log("Rayman2Fix 0.1.0-bootstrap");
    log(std::string("START: local_time=") + when);
    log("LAYOUT: launcher=Rayman2Fix.exe install=Rayman2Fix/ config=Rayman2Fix/Rayman2Fix.ini log=Rayman2Fix.log");
    log("RUNTIME: Rayman2Fix.dll=" +
        std::string(has_runtime_dll() ? "present" : "not packaged in this directory"));
    log("RENDERER: GameFrame (glide2x.dll)=not integrated");
    log("INPUT: SDL3.dll=not integrated; controller input is not active");
    log("INSTALL: no verified CD edition profile, no playable installation");
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
