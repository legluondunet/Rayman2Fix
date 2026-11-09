#include "rayman2fix/game_launch.hpp"
#include "rayman2fix/display.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void write(const fs::path& target, const std::string& content) {
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) throw std::runtime_error("cannot write test file");
}

int main(int argc, char** argv) {
    const auto root = fs::temp_directory_path() /
        ("r2fix process with spaces " +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto preflight = r2fix::check_game_launch(root);
        check(!preflight.allowed, "empty installation must be refused");
        write(root / "rayman2.exe", "untrusted executable");
        write(root / "rayman2.exe.bak", "untrusted backup");
        write(root / ".rayman2fix-exe-patch.tsv", "invalid patch marker");
        write(root / "ubi.ini", "[Rayman2]\nGLI_Dll=Glide2\n");
        check(!r2fix::check_game_launch(root).allowed,
              "tampered executable/marker must not be allowed to launch");
        r2fix::ensure_rayman2_dpi_manifest(root);
        check(fs::is_regular_file(root / "rayman2.exe.manifest"),
              "external PMv2 manifest created without modifying game executable");
        const auto manifest_copy = [&]() {
            std::ifstream in(root / "rayman2.exe.manifest", std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        check(manifest_copy().find("PerMonitorV2,PerMonitor") != std::string::npos,
              "manifest requests per-monitor DPI awareness");
        const auto first_manifest = manifest_copy();
        r2fix::ensure_rayman2_dpi_manifest(root);
        check(manifest_copy() == first_manifest, "manifest generation idempotent");

#ifdef _WIN32
        const auto child = fs::absolute(fs::path(argv[0])).parent_path() /
                           "test_child_process.exe";
        check(fs::exists(child), "test child executable not built");
        const auto result = r2fix::run_child_process(child, root);
        check(result.started && result.error_code == 0 &&
              result.exit_code == 17,
              "child process launched and nonzero exit status captured");
        check(fs::exists(root / "child-working-directory.txt"),
              "child inherited explicit game working directory");

        r2fix::configure_launcher_dpi_awareness();
        r2fix::ChildDisplayOptions options;
        options.borderless = true;
        options.monitor = r2fix::primary_display_metrics();
        options.output_width = options.monitor.width;
        options.output_height = options.monitor.height;
        // The fixture is deliberately not an approved Rayman 2 build.
        // Injection must load the local DLL but safely refuse to hook it.
        const auto local_runtime = fs::absolute(fs::path(argv[0])).parent_path() /
                                   "Rayman2Fix.dll";
        check(fs::exists(local_runtime), "native Hor+ runtime built beside tests");
        fs::copy_file(local_runtime, root / "Rayman2Fix.dll");
        options.enable_fov = true;
        options.fps_limit = 75;
        options.log_enabled = false;
        check(SetEnvironmentVariableW(L"RAYMAN2FIX_FPS_LIMIT", L"99") != 0,
              "set existing parent FPS setting for restoration check");
        const auto fullscreen = r2fix::run_child_process(child, root, options);
        wchar_t parent_fps[16]{};
        GetEnvironmentVariableW(L"RAYMAN2FIX_FPS_LIMIT", parent_fps, 16);
        check(std::wstring(parent_fps) == L"99",
              "launcher restores parent FPS variable after game creation");
        SetEnvironmentVariableW(L"RAYMAN2FIX_FPS_LIMIT", nullptr);
        check(fullscreen.started && fullscreen.error_code == 0 &&
              fullscreen.exit_code == 17,
              "borderless-window child launched and returned expected exit code");
        check(fullscreen.presentation.window_found &&
              fullscreen.presentation.borderless_applied &&
              fullscreen.presentation.changes > 0,
              "window supervisor found and applied borderless fullscreen");
        if (!(fullscreen.fov_hook.attempted &&
              fullscreen.fov_hook.dll_loaded &&
              !fullscreen.fov_hook.active &&
              fullscreen.fov_hook.init_status == 3)) {
            std::cerr << "FOV test diagnostics: attempted=" << fullscreen.fov_hook.attempted
                      << " loaded=" << fullscreen.fov_hook.dll_loaded
                      << " active=" << fullscreen.fov_hook.active
                      << " init_status=" << fullscreen.fov_hook.init_status
                      << " win32_error=" << fullscreen.fov_hook.win32_error
                      << "\\n";
            throw std::runtime_error(
                "remote Hor+ runtime loads but refuses unknown executable signature");
        }

        std::wifstream window(root / "child-window.txt");
        std::wstring inherited_size;
        unsigned long long style = 0;
        int left = 0, top = 0, width = 0, height = 0;
        std::wstring inherited_fps;
        window >> inherited_size >> style >> left >> top >> width >> height
               >> inherited_fps;
        check(static_cast<bool>(window), "child reported window and env values");
        check(inherited_fps == L"75",
              "child inherits configured FPSLimit, not parent's prior value");
        const auto requested_size =
            std::to_wstring(options.output_width) + L"x" +
            std::to_wstring(options.output_height);
        check(inherited_size == requested_size,
              "game child inherited exact physical GameFrame GLIDE_SIZE");
        check((style & 0x80000000ull) != 0 &&
              (style & 0x00c00000ull) == 0,
              "test child window is WS_POPUP without title bar");
        check(left == options.monitor.left && top == options.monitor.top &&
              width == static_cast<int>(options.monitor.width) &&
              height == static_cast<int>(options.monitor.height),
              "test child window covers physical primary monitor exactly");
        window.close(); // Release Windows sharing lock before temp tree cleanup.

        const auto nonexistent = root / "does-not-exist.exe";
        const auto failure = r2fix::run_child_process(nonexistent, root);
        check(!failure.started && failure.error_code != 0,
              "missing executable must fail with an OS error");
#else
        (void)argc;
        (void)argv;
        const auto failure = r2fix::run_child_process(root / "missing.exe", root);
        check(!failure.started, "non-Windows tests do not spawn Windows EXEs");
#endif
        fs::remove_all(root);
        std::cout << "Launch preflight / process tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::cerr << "Launch tests failed: " << e.what() << '\n';
        return 1;
    }
}
