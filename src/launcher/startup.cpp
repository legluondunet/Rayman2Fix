#include "rayman2fix/startup.hpp"

#include <chrono>
#include <algorithm>
#include <cctype>
#include <sstream>
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
        ini << "[Debug]\n"
               "Log=1\n";
        if (!ini) throw std::runtime_error("failed writing Rayman2Fix.ini");
    }

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
        return;
    }
    // Exactly one diagnostic file beside the launcher. Clear it each run.
    log_stream_.open(log_path_, std::ios::binary | std::ios::trunc);
    if (!log_stream_) throw std::runtime_error("cannot create Rayman2Fix.log");
    log(std::string("Rayman2Fix ") + RAYMAN2FIX_VERSION + "-bootstrap commit=" + RAYMAN2FIX_BUILD_COMMIT);
    log(std::string("START: local_time=") + when);
    log("LAYOUT: launcher=Rayman2Fix.exe install=Rayman2Fix/ config=Rayman2Fix/Rayman2Fix.ini log=Rayman2Fix.log");
    log("CONFIG: Debug.Log=" +
        std::string(debug == "1" ? "1" : "invalid or missing (default 1)"));

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
