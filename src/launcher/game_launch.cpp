#include "rayman2fix/game_launch.hpp"

#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/patch_install.hpp"
#include "rayman2fix/sha256.hpp"
#include "rayman2fix/startup.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace r2fix {
namespace {
constexpr std::uint64_t kOriginalBytes = 676352;
constexpr std::uint64_t kUnprotectedBytes = 1468928;
constexpr const char* kOriginalHash =
    "7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3";
constexpr const char* kUnprotectedHash =
    "9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713";

bool matches(const fs::path& path, std::uint64_t bytes, const char* hash) {
    return fs::is_regular_file(path) && !fs::is_symlink(path) &&
           fs::file_size(path) == bytes && sha256_file(path) == hash;
}
std::string exit_hex(unsigned long exit_code) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << exit_code;
    return stream.str();
}
#ifdef _WIN32
struct Handle {
    HANDLE handle = nullptr;
    ~Handle() {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    Handle() = default;
    explicit Handle(HANDLE h) : handle(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
#endif
} // namespace

GameLaunchPreflight check_game_launch(const fs::path& root) {
    try {
        // Existing installer verification checks the entire CD receipt first;
        // this second gate authenticates the specific executable edition.
        if (!is_exe_replaced(root))
            return {false, "original protected game EXE has not been patched"};
        const auto exe = root / "rayman2.exe";
        const auto backup = root / "rayman2.exe.bak";
        if (!matches(exe, kUnprotectedBytes, kUnprotectedHash))
            return {false, "patched rayman2.exe SHA256/size is not approved"};
        if (!matches(backup, kOriginalBytes, kOriginalHash))
            return {false, "original rayman2.exe.bak SHA256/size is not approved"};
        std::string problem;
        if (!verify_exe_replacement(root, kOriginalHash, kOriginalBytes, problem))
            return {false, "EXE patch marker verification failed: " + problem};
        if (!gameframe_glide_ready(root))
            return {false, "required GameFrame glide2x.dll is missing or unverified"};
        if (!fs::is_regular_file(root / "ubi.ini") ||
            fs::is_symlink(root / "ubi.ini"))
            return {false, "generated ubi.ini is missing or invalid"};
        if (!fs::is_regular_file(root / "dll" / "snd_cpa.ini") ||
            fs::is_symlink(root / "dll" / "snd_cpa.ini"))
            return {false, "initialized audio config is missing"};
        return {true, {}};
    } catch (const std::exception& error) {
        return {false, std::string("launch preflight error: ") + error.what()};
    }
}

ChildProcessResult run_child_process(const fs::path& executable,
                                    const fs::path& working_directory) {
#ifdef _WIN32
    if (!fs::is_regular_file(executable) || fs::is_symlink(executable) ||
        !fs::is_directory(working_directory) || fs::is_symlink(working_directory))
        return {false, 0, ERROR_FILE_NOT_FOUND};

    const std::wstring application = fs::absolute(executable).wstring();
    const std::wstring cwd = fs::absolute(working_directory).wstring();
    std::wstring commandline = L"\"" + application + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION raw{};
    // Win32 CreateProcessW may modify the command line buffer, so pass
    // a non-const std::wstring. The absolute lpApplicationName rules out
    // shell interpretation and executable path hijacking.
    if (!CreateProcessW(application.c_str(), &commandline[0],
                        nullptr, nullptr, FALSE, 0, nullptr,
                        cwd.c_str(), &startup, &raw))
        return {false, 0, GetLastError()};
    Handle process(raw.hProcess);
    Handle thread(raw.hThread);
    if (WaitForSingleObject(process.handle, INFINITE) != WAIT_OBJECT_0)
        return {true, 0, GetLastError() ? GetLastError() : ERROR_GEN_FAILURE};
    DWORD code = 0;
    if (!GetExitCodeProcess(process.handle, &code))
        return {true, 0, GetLastError()};
    return {true, code, 0};
#else
    (void)executable;
    (void)working_directory;
    return {false, 0, 1};
#endif
}

int launch_verified_game(Startup& startup) {
    const auto& root = startup.game_dir();
    const auto decision = check_game_launch(root);
    if (!decision.allowed) {
        startup.log("LAUNCH-BLOCKED: " + decision.reason);
        std::cerr << "Rayman 2 launch blocked: " << decision.reason << '\n';
        return 2;
    }

    startup.log(std::string("LAUNCH-READY: original_sha256=") + kOriginalHash +
                " patched_sha256=" + kUnprotectedHash +
                " gameframe=verified input=keyboard SDL3=deferred");
    std::cout << "Launching Rayman 2 (Glide2)...\n" << std::flush;
    startup.log("LAUNCH-START: child=rayman2.exe working_directory=Rayman2Fix");
    const auto started = std::chrono::steady_clock::now();
    const auto result = run_child_process(root / "rayman2.exe", root);
    const auto seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    if (!result.started) {
        startup.log("LAUNCH-ERROR: CreateProcessW error=" +
                    std::to_string(result.error_code));
        std::cerr << "Cannot start rayman2.exe; Win32 error="
                  << result.error_code << '\n';
        return 1;
    }
    if (result.error_code) {
        startup.log("LAUNCH-ERROR: waiting for game error=" +
                    std::to_string(result.error_code));
        std::cerr << "Could not retrieve Rayman 2 exit status; Win32 error="
                  << result.error_code << '\n';
        return 1;
    }
    startup.log("LAUNCH-EXIT: child_exit_code=" + exit_hex(result.exit_code) +
                " elapsed_ms=" + std::to_string(
                    static_cast<unsigned long long>(seconds * 1000.0)));
    if (result.exit_code != 0) {
        std::cerr << "Rayman 2 exited with code " << exit_hex(result.exit_code)
                  << "; see Rayman2Fix.log.\n";
        return 1;
    }
    std::cout << "Rayman 2 exited normally.\n";
    return 0;
}
} // namespace r2fix
