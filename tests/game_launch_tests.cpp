#include "rayman2fix/game_launch.hpp"
#include "rayman2fix/display.hpp"
#include "rayman2fix/presentation.hpp"

#include <array>
#include <cstdint>
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

        {
            std::wifstream probe(root/"child-dll-search-path.txt");
            check(static_cast<bool>(probe),"child PATH probe missing");
            std::wstring actual;
            std::getline(probe,actual);
            const auto expected=fs::absolute(root/"rayman2fix").wstring();
            check(actual==expected || actual.rfind(expected+L";",0)==0,
                  "game child must search nested rayman2fix before inherited PATH");
        }
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
        fs::create_directories(root / "rayman2fix");
        fs::copy_file(local_runtime, root / "rayman2fix" / "Rayman2Fix.dll");
        options.enable_fov = true;
        options.fps_limit = 75;
        options.log_enabled = false;
        check(SetEnvironmentVariableW(L"RAYMAN2FIX_FPS_LIMIT", L"99") != 0,
              "set existing parent FPS setting for restoration check");
        const DWORD orig_path_bytes=GetEnvironmentVariableW(L"PATH",nullptr,0);
        std::wstring orig_path(orig_path_bytes,L'\0');
        if(orig_path_bytes){
            const DWORD n=GetEnvironmentVariableW(L"PATH",orig_path.data(),orig_path_bytes);
            check(n<orig_path_bytes,"parent PATH capture valid");
            orig_path.resize(n);
        }
        unsigned live_presentation_reports=0;
        const auto fullscreen = r2fix::run_child_process(
            child, root, options,
            [&](const r2fix::PresentationResult& observed) {
                check(observed.window_found,
                      "live presentation callback only sees selected HWND");
                ++live_presentation_reports;
            });
        check(live_presentation_reports>0,
              "presentation diagnostic arrives while child process is running");
        const DWORD after_path_bytes=GetEnvironmentVariableW(L"PATH",nullptr,0);
        std::wstring after_path(after_path_bytes,L'\0');
        if(after_path_bytes){
            const DWORD n=GetEnvironmentVariableW(L"PATH",after_path.data(),after_path_bytes);
            check(n<after_path_bytes,"parent PATH restored value valid");
            after_path.resize(n);
        }
        check(orig_path==after_path,
              "launcher must restore PATH after game starts");
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
        // Camera dispatch now supports the targeted intro skip even in 4:3.
        // An untrusted executable MUST be refused at ALL resolutions:
        // neither the camera nor the intro/2D hooks may be installed.
        if (!(fullscreen.fov_hook.attempted &&
              fullscreen.fov_hook.dll_loaded &&
              !fullscreen.fov_hook.active &&
              fullscreen.fov_hook.init_status == 3ul)) {
            std::cerr << "FOV test diagnostics: attempted=" << fullscreen.fov_hook.attempted
                      << " loaded=" << fullscreen.fov_hook.dll_loaded
                      << " active=" << fullscreen.fov_hook.active
                      << " init_status=" << fullscreen.fov_hook.init_status
                      << " win32_error=" << fullscreen.fov_hook.win32_error
                      << "\\n";
            throw std::runtime_error(
                "remote runtime must refuse unknown executable at every aspect ratio");
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

        // Exercise EXE-icon assignment with a temporary 16x16 ICO file
        // generated by this test, NOT a shipped resource or game asset.
        // The installed product still supplies only rayman2.exe's path.
        // Do not rely on shell32.dll: current Windows CI images may
        // ship no extractible grouped icons in their system DLLs.
        {
            const fs::path source=root/"test-game-window-icon.ico";
            {
                // ICONDIR + ICONDIRENTRY: one 16x16 32-bit BMP-based icon.
                constexpr unsigned char header[22]={
                    0,0, 1,0, 1,0, 16,16,0,0, 1,0,32,0,
                    0x68,0x04,0,0, 22,0,0,0
                };
                // BITMAPINFOHEADER: biHeight is doubled for XOR+AND.
                constexpr unsigned char bitmap[40]={
                    40,0,0,0, 16,0,0,0, 32,0,0,0, 1,0,32,0,
                    0,0,0,0, 0,0,0,0, 0,0,0,0,
                    0,0,0,0, 0,0,0,0, 0,0,0,0
                };
                std::ofstream icon(source,std::ios::binary|std::ios::trunc);
                icon.write(reinterpret_cast<const char*>(header),sizeof(header));
                icon.write(reinterpret_cast<const char*>(bitmap),sizeof(bitmap));
                constexpr char bgra[4]={0,96,static_cast<char>(204),
                                         static_cast<char>(255)};
                for(unsigned i=0;i<16u*16u;++i)icon.write(bgra,4);
                const std::array<char,64> and_mask{};
                icon.write(and_mask.data(),and_mask.size());
                icon.close();
                check(static_cast<bool>(icon),"synthetic icon fixture created");
            }
            check(fs::file_size(source)==1150u,
                  "synthetic ICO file has exact 1150-byte layout");
            WNDCLASSW klass{};
            klass.lpfnWndProc = DefWindowProcW;
            klass.hInstance = GetModuleHandleW(nullptr);
            klass.lpszClassName = L"Rayman2FixIconWindowTest";
            check(RegisterClassW(&klass) != 0,
                  "synthetic icon test window class registered");
            HWND hwnd = CreateWindowExW(0, klass.lpszClassName,
                L"Rayman2Fix icon fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                30, 30, 640, 480, nullptr, nullptr, klass.hInstance, nullptr);
            check(hwnd != nullptr, "synthetic icon test window created");
            {
                r2fix::WindowPresentation presentation(
                    GetCurrentProcessId(), r2fix::primary_display_metrics(),
                    source.wstring());
                presentation.update();
                const auto observed = presentation.result();
                if (!(observed.window_found && observed.game_icon_available &&
                      observed.game_icon_applied && observed.game_icon_updates == 1)) {
                    std::cerr << "Icon fixture diagnostics: found=" << observed.window_found
                              << " available=" << observed.game_icon_available
                              << " applied=" << observed.game_icon_applied
                              << " updates=" << observed.game_icon_updates
                              << " source=" << observed.game_icon_source
                              << " icon_error=" << observed.game_icon_error
                              << " last_error=" << observed.last_error << "\\n";
                    throw std::runtime_error(
                        "executable resource icons applied to actual HWND");
                }
                check(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) != 0 &&
                      SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0) != 0,
                      "window's large and small icons visibly assigned by WM_SETICON");
                presentation.update();
                check(presentation.result().game_icon_updates == 1 &&
                      presentation.result().game_icon_readback,
                      "icon handles assigned once and Win32 WM_GETICON retained both");
                // Destroy before presentation's HICON destructor: the
                // launcher normally waits for game process termination.
                DestroyWindow(hwnd);
            }
            UnregisterClassW(klass.lpszClassName, klass.hInstance);

            // Production targets a DIFFERENT process. Confirm its WM_SETICON
            // and WM_GETICON round-trip, not just an in-process DefWindowProc.
            STARTUPINFOW icon_start{};
            icon_start.cb=sizeof(icon_start);
            PROCESS_INFORMATION icon_child{};
            const auto executable=child.wstring();
            const auto workdir=root.wstring();
            std::wstring command=L"\""+executable+L"\"";
            check(CreateProcessW(executable.c_str(),command.data(),
                nullptr,nullptr,FALSE,0,nullptr,workdir.c_str(),
                &icon_start,&icon_child)!=0,
                "cross-process icon fixture child launched");
            r2fix::WindowPresentation remote(
                icon_child.dwProcessId,r2fix::primary_display_metrics(),
                source.wstring());
            const DWORD started=GetTickCount();
            r2fix::PresentationResult remote_result{};
            do {
                remote.update();
                remote_result=remote.result();
                if(remote_result.game_icon_applied)break;
                Sleep(40);
            } while(DWORD(GetTickCount()-started)<2000u);
            WaitForSingleObject(icon_child.hProcess,4500);
            CloseHandle(icon_child.hThread);
            CloseHandle(icon_child.hProcess);
            if(!(remote_result.game_icon_available &&
                 remote_result.game_icon_applied &&
                 remote_result.game_icon_readback)) {
                std::cerr << "Cross-process icon diagnostics: found="
                          << remote_result.window_found
                          << " available=" << remote_result.game_icon_available
                          << " applied=" << remote_result.game_icon_applied
                          << " readback=" << remote_result.game_icon_readback
                          << " class=" << remote_result.game_window_class
                          << " error=" << remote_result.game_icon_error << "\\n";
                throw std::runtime_error(
                    "cross-process WM_SETICON did not round-trip");
            }
        }

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
