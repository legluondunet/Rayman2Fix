#include <filesystem>
#include <fstream>
#include <string>

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
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"Rayman2 test window",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                55, 60, 640, 480, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return 23;
    DWORD ticks = GetTickCount();
    MSG msg{};
    while (GetTickCount() - ticks < 3000) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(15);
    }
    wchar_t buffer[128]{};
    GetEnvironmentVariableW(L"GLIDE_SIZE", buffer, 128);
    wchar_t frame_limit[64]{};
    GetEnvironmentVariableW(L"RAYMAN2FIX_FPS_LIMIT", frame_limit, 64);
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
    DestroyWindow(hwnd);
#endif
    return 17;
}
