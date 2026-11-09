#include "rayman2fix/presentation.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <string>

namespace r2fix {
namespace {
struct Search {
    DWORD pid;
    HWND best = nullptr;
    std::uint64_t score = 0;
};
BOOL CALLBACK find_game_window(HWND hwnd, LPARAM param) {
    auto& search = *reinterpret_cast<Search*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search.pid || !IsWindowVisible(hwnd) ||
        GetWindow(hwnd, GW_OWNER)) return TRUE;

    const LONG_PTR exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exstyle & WS_EX_TOOLWINDOW) != 0) return TRUE;
    RECT client{};
    if (!GetClientRect(hwnd, &client)) return TRUE;
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    // Do not resize transient 1990s error dialogs, tool windows, or
    // button boxes. Always target the main renderer window.
    if (w < 320 || h < 200) return TRUE;
    wchar_t klass[100]{};
    GetClassNameW(hwnd, klass, 100);
    const bool wrapper = std::wstring(klass) == L"GlideWrapper";
    const std::uint64_t score = static_cast<std::uint64_t>(w) * h +
                                (wrapper ? (1ull << 48) : 0);
    if (score > search.score) {
        search.score = score;
        search.best = hwnd;
    }
    return TRUE;
}
} // namespace

WindowPresentation::WindowPresentation(unsigned long child_pid,
                                       DisplayMetrics display)
    : pid_(child_pid), display_(display) {}

void WindowPresentation::update() {
    Search search{static_cast<DWORD>(pid_)};
    EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&search));
    if (!search.best) return;
    result_.window_found = true;

    // In multi-monitor configurations choose the primary screen used by
    // GLIDE_SIZE and the single user-facing Graphics.Resolution=Desktop.
    const HWND hwnd = search.best;
    RECT current{};
    if (!GetWindowRect(hwnd, &current)) return;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR desired_style =
        ((style | WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS) &
         ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
           WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
    const LONG_PTR desired_exstyle =
        exstyle & ~(WS_EX_TOPMOST | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE |
                    WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME);
    const bool new_window = hwnd != last_window_;
    const bool needs_position =
        current.left != display_.left || current.top != display_.top ||
        current.right != display_.left + static_cast<int>(display_.width) ||
        current.bottom != display_.top + static_cast<int>(display_.height);
    if (!new_window && style == desired_style &&
        exstyle == desired_exstyle && !needs_position) return;

    if (style != desired_style) SetWindowLongPtrW(hwnd, GWL_STYLE, desired_style);
    if (exstyle != desired_exstyle)
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, desired_exstyle);
    if (!SetWindowPos(hwnd, nullptr, display_.left, display_.top,
                      static_cast<int>(display_.width),
                      static_cast<int>(display_.height),
                      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
        result_.last_error = "SetWindowPos Win32 error=" +
                             std::to_string(GetLastError());
        result_.borderless_applied = false;
        return;
    }
    last_window_ = hwnd;
    ++result_.changes;
    result_.borderless_applied = true;
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    using GetWindowDpi = UINT (WINAPI*)(HWND);
    const auto get_dpi = user ? reinterpret_cast<GetWindowDpi>(
        GetProcAddress(user, "GetDpiForWindow")) : nullptr;
    result_.observed_dpi = get_dpi ? get_dpi(hwnd) : display_.dpi;
}
PresentationResult WindowPresentation::result() const { return result_; }
} // namespace r2fix
#endif
