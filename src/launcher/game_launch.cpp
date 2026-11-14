#include "rayman2fix/game_launch.hpp"

#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/game_config.hpp"
#include "rayman2fix/display.hpp"
#include "rayman2fix/presentation.hpp"
#include "rayman2fix/runtime_inject.hpp"
#include "rayman2fix/patch_install.hpp"
#include "rayman2fix/sha256.hpp"
#include "rayman2fix/startup.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <iterator>
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
// Temporarily override the selected wrapper variables only while creating
// the child. Restore the parent's original environment even on failures.
class ScopedChildEnvironment {
public:
    explicit ScopedChildEnvironment(const wchar_t* name) : name_(name) {}
    ScopedChildEnvironment(const ScopedChildEnvironment&) = delete;
    ScopedChildEnvironment& operator=(const ScopedChildEnvironment&) = delete;
    ~ScopedChildEnvironment() { restore(); }

    bool set(const std::wstring& desired) {
        const DWORD n = GetEnvironmentVariableW(name_, nullptr, 0);
        if (n > 32768) { error_ = ERROR_INVALID_PARAMETER; return false; }
        if (n) {
            original_.resize(n, L'\0');
            const DWORD actual = GetEnvironmentVariableW(name_, &original_[0], n);
            if (!actual || actual >= n) { error_ = GetLastError(); return false; }
            original_.resize(actual);
            existed_ = true;
        }
        if (!SetEnvironmentVariableW(name_, desired.c_str())) {
            error_ = GetLastError();
            return false;
        }
        active_ = true;
        return true;
    }
    DWORD error() const { return error_; }
    void restore() {
        if (!active_) return;
        if (existed_) SetEnvironmentVariableW(name_, original_.c_str());
        else SetEnvironmentVariableW(name_, nullptr);
        active_ = false;
    }
private:
    const wchar_t* name_;
    std::wstring original_;
    bool existed_ = false;
    bool active_ = false;
    DWORD error_ = 0;
};

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

void ensure_rayman2_dpi_manifest(const fs::path& game_dir) {
    // Separate declarative, game-facing compatibility file. This does not
    // touch the authenticated rayman2.exe bytes or the single user INI.
    constexpr const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
        "  <assemblyIdentity type=\"win32\" name=\"Rayman2Fix.Rayman2\" version=\"1.0.0.0\" processorArchitecture=\"x86\"/>\r\n"
        "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n"
        "    <windowsSettings>\r\n"
        "      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true/pm</dpiAware>\r\n"
        "      <dpiAwareness xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">PerMonitorV2,PerMonitor</dpiAwareness>\r\n"
        "    </windowsSettings>\r\n"
        "  </application>\r\n"
        "</assembly>\r\n";
    const fs::path dest = game_dir / "rayman2.exe.manifest";
    if (fs::exists(dest)) {
        if (!fs::is_regular_file(dest) || fs::is_symlink(dest) ||
            fs::file_size(dest) > 16u*1024u)
            throw std::runtime_error("unsafe existing Rayman2 DPI manifest");
        std::ifstream in(dest, std::ios::binary);
        const std::string existing((std::istreambuf_iterator<char>(in)), {});
        if (!in.bad() && existing == xml) return;
        // Preserve user files; fail rather than overwrite an unrelated or
        // tampered native manifest with a new SxS configuration.
        throw std::runtime_error("unknown existing rayman2.exe.manifest; refusing overwrite");
    }
    fs::path pending = dest;
    pending += ".installing";
    if (fs::exists(pending) || fs::is_symlink(pending))
        throw std::runtime_error("stale Rayman2 DPI manifest staging file");
    try {
        std::ofstream out(pending, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot stage Rayman2 DPI manifest");
        out << xml;
        out.close();
        if (!out) throw std::runtime_error("cannot write Rayman2 DPI manifest");
        fs::rename(pending, dest);
    } catch (...) {
        std::error_code ignored;
        fs::remove(pending, ignored);
        throw;
    }
}

ChildProcessResult run_child_process(
    const fs::path& executable, const fs::path& working_directory,
    const ChildDisplayOptions& display) {
#ifdef _WIN32
    if (!display.skip_executable_precheck &&
        (!fs::is_regular_file(executable) || fs::is_symlink(executable) ||
         !fs::is_directory(working_directory) || fs::is_symlink(working_directory)))
        return {false, 0, ERROR_FILE_NOT_FOUND};
    // In explicit --launch-no-verify mode, CreateProcessW itself reports
    // missing EXE/working directory. No hash, receipt or asset checks run.

    const std::wstring application = fs::absolute(executable).wstring();
    const std::wstring cwd = fs::absolute(working_directory).wstring();
    std::wstring commandline = L"\"" + application + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION raw{};

    // GameFrame uses GLIDE_SIZE for physical output and our pinned-source
    // extension RAYMAN2FIX_FPS_LIMIT for producer-side grBufferSwap pacing.
    ScopedChildEnvironment glide_size(L"GLIDE_SIZE");
    ScopedChildEnvironment fps_limit(L"RAYMAN2FIX_FPS_LIMIT");
    ScopedChildEnvironment glide_probe(L"RAYMAN2FIX_GLIDE_PROBE");
    if (!glide_probe.set(display.log_enabled ? L"1" : L"0"))
        return {false, 0, glide_probe.error()};
    if (display.borderless) {
        if (!display.output_width || !display.output_height ||
            display.output_width > 16384 || display.output_height > 8640 ||
            (display.fps_limit != 0 &&
             (display.fps_limit < 15 || display.fps_limit > 240)))
            return {false, 0, ERROR_INVALID_PARAMETER};
        const std::wstring size = std::to_wstring(display.output_width) +
                                  L"x" + std::to_wstring(display.output_height);
        if (!glide_size.set(size))
            return {false, 0, glide_size.error()};
        if (!fps_limit.set(std::to_wstring(display.fps_limit)))
            return {false, 0, fps_limit.error()};
    }
    // Win32 CreateProcessW may modify the command-line buffer. A trusted
    // absolute application path is used; there is no shell involved.
    const BOOL created = CreateProcessW(application.c_str(), &commandline[0],
                                        nullptr, nullptr, FALSE, 0, nullptr,
                                        cwd.c_str(), &startup, &raw);
    const DWORD spawn_error = created ? 0 : GetLastError();
    glide_probe.restore();
    fps_limit.restore();
    glide_size.restore();
    if (!created) return {false, 0, spawn_error};
    Handle process(raw.hProcess);
    Handle thread(raw.hThread);
    ChildProcessResult result{};
    result.started = true;
    if (display.borderless && display.enable_fov) {
        result.fov_hook = initialize_game_fov(
            reinterpret_cast<std::uintptr_t>(process.handle),
            raw.dwProcessId, working_directory / "Rayman2Fix.dll",
            display.output_width, display.output_height, display.log_enabled,
            display.skip_cinematics, display.music_in_pause_menu);
    }

    if (display.borderless) {
        WindowPresentation presentation(raw.dwProcessId, display.monitor);
        DWORD wait = WAIT_TIMEOUT;
        while ((wait = WaitForSingleObject(process.handle, 120)) == WAIT_TIMEOUT) {
            presentation.update();
        }
        result.presentation = presentation.result();
        if (wait != WAIT_OBJECT_0) {
            result.error_code = GetLastError();
            if (!result.error_code) result.error_code = ERROR_GEN_FAILURE;
            return result;
        }
    } else if (WaitForSingleObject(process.handle, INFINITE) != WAIT_OBJECT_0) {
        result.error_code = GetLastError();
        if (!result.error_code) result.error_code = ERROR_GEN_FAILURE;
        return result;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process.handle, &code)) {
        result.error_code = GetLastError();
        return result;
    }
    result.exit_code = code;
    return result;
#else
    (void)executable;
    (void)working_directory;
    (void)display;
    return {false, 0, 1};
#endif
}

int launch_verified_game(Startup& startup, bool verify) {
    const auto& root = startup.game_dir();
    if (verify) {
        const auto decision = check_game_launch(root);
        if (!decision.allowed) {
            startup.log("LAUNCH-BLOCKED: " + decision.reason);
            std::cerr << "Rayman 2 launch blocked: " << decision.reason << '\n';
            return 2;
        }
    } else {
        startup.log("LAUNCH-NO-VERIFY: direct CreateProcessW; no receipt, "
                    "hash, file-presence, patch or runtime provisioning checks");
    }

    configure_launcher_dpi_awareness();
    const auto monitor = primary_display_metrics();
    // In unchecked mode, missing configuration uses display defaults;
    // no INI file is created or regenerated by the launcher.
    const bool unchecked_without_ini=!verify && !fs::exists(startup.ini_path());
    const auto configured = unchecked_without_ini
        ? std::make_pair(monitor.width, monitor.height)
        : configured_output_resolution(startup.ini_path());
    const auto content = unchecked_without_ini ? configured
        : configured_render_resolution(startup.ini_path());
    if (configured.first < 640 || configured.second < 480)
        throw std::runtime_error("invalid native output size");
    ChildDisplayOptions display{};
    display.borderless = true;
    display.monitor = monitor;
    display.output_width = content.first;
    display.output_height = content.second;
    display.skip_cinematics = !unchecked_without_ini &&
        configured_skip_cinematics(startup.ini_path());
    display.music_in_pause_menu = !unchecked_without_ini &&
        configured_music_in_pause_menu(startup.ini_path());
    display.enable_fov = true;
    display.fps_limit = unchecked_without_ini
        ? 60u : configured_fps_limit(startup.ini_path());
    display.skip_executable_precheck = !verify;
    display.log_enabled = startup.logging_enabled();
    if (verify) ensure_rayman2_dpi_manifest(root);
    const auto aspect = static_cast<double>(content.first) /
                        static_cast<double>(content.second);
    std::ostringstream ratio;
    ratio << std::fixed << std::setprecision(4) << aspect;
    if (verify) {
        startup.log(std::string("LAUNCH-READY: original_sha256=") + kOriginalHash +
                    " patched_sha256=" + kUnprotectedHash +
                    " gameframe=verified input=keyboard SDL3=deferred");
    } else {
        startup.log("LAUNCH-READY: unchecked game edition and files; "
                    "do not interpret this as a verified installation");
    }
    startup.log("DISPLAY: primary_physical=" +
                std::to_string(monitor.width) + "x" +
                std::to_string(monitor.height) + " dpi=" +
                std::to_string(monitor.dpi) + " origin=" +
                std::to_string(monitor.left) + "," +
                std::to_string(monitor.top) + " output=" +
                std::to_string(display.output_width) + "x" +
                std::to_string(display.output_height) +
                " output_aspect=" + ratio.str() +
                " physical_canvas=" + std::to_string(configured.first) + "x" +
                std::to_string(configured.second) +
                " GameFrame.GLIDE_SIZE=explicit window=borderless-not-topmost" +
                (configured_force_4_3(startup.ini_path()) ?
                    " aspect_mode=4:3-centered-black-bars" :
                    " aspect_mode=Auto"));
    startup.log(configured_force_4_3(startup.ini_path()) ?
                "WIDESCREEN: 4:3 mode selected; world Hor+ inactive, internal and output aspect 4:3" :
                "WIDESCREEN: native output configured; game camera/Hor+ FOV patch not yet validated (do not confuse output scaling with Hor+)");
    startup.log("FPS: limiter=" + std::to_string(display.fps_limit) +
                (display.fps_limit ? " cap at GameFrame.grBufferSwap"
                                   : " unlimited (GameFrame pacing disabled)"));
    std::cout << "Launching Rayman 2 (Glide2)...\n" << std::flush;
    startup.log("LAUNCH-START: child=rayman2.exe working_directory=Rayman2Fix");
    const auto started = std::chrono::steady_clock::now();
    // Launcher and DLL no longer share a file handle: only the approved
    // game launch resets Rayman2Fix.log, keeping installer messages separate.
    startup.prepare_runtime_log();
    const ChildProcessResult result =
        run_child_process(root / "rayman2.exe", root, display);
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
    startup.log("PRESENTATION: window_found=" +
                std::string(result.presentation.window_found ? "1" : "0") +
                " borderless_applied=" +
                (result.presentation.borderless_applied ? "1" : "0") +
                " changes=" + std::to_string(result.presentation.changes) +
                " child_window_dpi=" +
                std::to_string(result.presentation.observed_dpi) +
                (result.presentation.last_error.empty() ? "" :
                  " last_error=" + result.presentation.last_error));
    startup.log("LAUNCH-EXIT: child_exit_code=" + exit_hex(result.exit_code) +
                " elapsed_ms=" + std::to_string(
                    static_cast<unsigned long long>(seconds * 1000.0)));
    startup.log("RUNTIME-FOV: injection_attempted=" +
                std::string(result.fov_hook.attempted ? "1" : "0") +
                " dll_loaded=" + (result.fov_hook.dll_loaded ? "1" : "0") +
                " active=" + (result.fov_hook.active ? "1" : "0") +
                " status=" + std::to_string(result.fov_hook.init_status) +
                " win32_error=" + std::to_string(result.fov_hook.win32_error));
    if (result.exit_code != 0) {
        std::cerr << "Rayman 2 exited with code " << exit_hex(result.exit_code)
                  << "; see Rayman2Fix_installer.log and Rayman2Fix.log.\n";
        return 1;
    }
    std::cout << "Rayman 2 exited normally.\n";
    return 0;
}
} // namespace r2fix
