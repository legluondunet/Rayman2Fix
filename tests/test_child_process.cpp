#include <filesystem>
#include <fstream>
#include <string>
#include "rayman2fix/game_window_icon.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main() {
    // Harmless Win32 window fixture for the launcher window supervisor.
    // The child writes its own final window style and inherited GLIDE_SIZE.
    const auto root = std::filesystem::current_path();
    std::ofstream out(root / "child-working-directory.txt");
    out << "cwd-verified\n";
    out.close();
#ifdef _WIN32
    WNDCLASSW klass{};
    klass.lpfnWndProc=DefWindowProcW;
    klass.hInstance=GetModuleHandleW(nullptr);
    klass.lpszClassName=L"Rayman2";
    if(!RegisterClassW(&klass))return 26;
    HWND hwnd = CreateWindowExW(0, klass.lpszClassName, L"Rayman2 test window",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                55, 60, 640, 480, nullptr,
                                nullptr, klass.hInstance, nullptr);
    if (!hwnd) return 23;
    wchar_t self_icon_flag[4]{};
    const bool icon_test=GetEnvironmentVariableW(
        L"RAYMAN2FIX_TEST_SELF_ICON",self_icon_flag,4)==1 &&
        self_icon_flag[0]==L'1';
    if(icon_test)
        r2fix::start_game_window_icon_worker(nullptr);
    DWORD ticks = GetTickCount();
    MSG msg{};
    while (GetTickCount() - ticks < 3000) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(15);
    }
    // Native Windows control: the launcher must not invent or modify the
    // Unix/PulseAudio setting, even when experimental Wine policy is enabled.
    const DWORD pulse_needed=GetEnvironmentVariableW(
        L"PULSE_LATENCY_MSEC",nullptr,0);
    std::wstring pulse_value;
    if(pulse_needed && pulse_needed<=32768) {
        pulse_value.resize(pulse_needed,L'\0');
        const DWORD copied=GetEnvironmentVariableW(
            L"PULSE_LATENCY_MSEC",pulse_value.data(),pulse_needed);
        if(copied>=pulse_needed)return 28;
        pulse_value.resize(copied);
    }
    std::wofstream pulse_report(root/"child-pulse-latency.txt");
    pulse_report << pulse_value;
    pulse_report.close();
    if(!pulse_report)return 29;
    wchar_t buffer[128]{};
    GetEnvironmentVariableW(L"GLIDE_SIZE", buffer, 128);
    wchar_t frame_limit[64]{};
    GetEnvironmentVariableW(L"RAYMAN2FIX_FPS_LIMIT", frame_limit, 64);
    std::wstring child_path(32768, L'\0');
    const DWORD path_len=GetEnvironmentVariableW(
        L"PATH",child_path.data(),static_cast<DWORD>(child_path.size()));
    if(!path_len || path_len>=child_path.size())return 24;
    child_path.resize(path_len);
    std::wofstream path_report(root/"child-dll-search-path.txt");
    path_report << child_path;
    path_report.close();
    if(!path_report)return 25;
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    std::wofstream info(root / "child-window.txt");
    info << buffer << L'\n'
         << static_cast<unsigned long long>(GetWindowLongPtrW(hwnd, GWL_STYLE)) << L'\n'
         << rect.left << L' ' << rect.top << L' '
         << (rect.right - rect.left) << L' '
         << (rect.bottom - rect.top) << L'\n'
         << frame_limit << L'\n';
    info.close();
    if(icon_test) {
        const HICON big=reinterpret_cast<HICON>(
            SendMessageW(hwnd,WM_GETICON,ICON_BIG,0));
        const HICON small=reinterpret_cast<HICON>(
            SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0));
        const HICON class_big=reinterpret_cast<HICON>(
            GetClassLongPtrW(hwnd,GCLP_HICON));
        const HICON class_small=reinterpret_cast<HICON>(
            GetClassLongPtrW(hwnd,GCLP_HICONSM));
        std::ofstream self_report(root/"child-owned-window-icon.txt");
        self_report << (big!=nullptr) << ' ' << (small!=nullptr)
                    << ' ' << (class_big!=nullptr)
                    << ' ' << (class_small!=nullptr) << '\n';
        self_report.close();
        if(!self_report)return 27;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(klass.lpszClassName,klass.hInstance);
#endif
    return 17;
}
