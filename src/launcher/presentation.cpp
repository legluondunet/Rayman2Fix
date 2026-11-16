#include "rayman2fix/presentation.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
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
                                       DisplayMetrics display,
                                       const std::wstring& game_executable)
    : pid_(child_pid), display_(display), game_executable_(game_executable) {}

WindowPresentation::~WindowPresentation() {
    // The launcher waits until the child process exits before this runs.
    // Never destroy these user objects while a live game HWND uses them.
    if (big_icon_) DestroyIcon(static_cast<HICON>(big_icon_));
    if (small_icon_ && small_icon_ != big_icon_)
        DestroyIcon(static_cast<HICON>(small_icon_));
}

void WindowPresentation::load_game_icons() {
    if (game_icons_loaded_) return;
    game_icons_loaded_ = true;
    if (game_executable_.empty()) {
        result_.game_icon_error = "game executable path unavailable";
        return;
    }

    // Read ONLY the current user's rayman2.exe resource, never a .bak:
    // the original protected file is not guaranteed to remain installed.
    const UINT groups=ExtractIconExW(game_executable_.c_str(),-1,
                                      nullptr,nullptr,0);
    result_.game_icon_groups=groups==UINT(-1) ? 0u : groups;
    if(groups==0 || groups==UINT(-1)) {
        result_.game_icon_error = "rayman2.exe has no extractable icon groups";
        return;
    }

    HICON big=nullptr,small=nullptr;
    const UINT extracted=ExtractIconExW(game_executable_.c_str(),0,
                                         &big,&small,1);
    if(extracted!=1 || (!big && !small)) {
        if(big)DestroyIcon(big);
        if(small && small!=big)DestroyIcon(small);
        result_.game_icon_error = "ExtractIconExW failed on rayman2.exe";
        return;
    }
    // WM_SETICON has independent large/small slots. CopyIcon returns
    // another caller-owned icon when only one embedded size exists.
    if(!big)big=CopyIcon(small);
    if(!small)small=CopyIcon(big);
    if(!big || !small) {
        if(big)DestroyIcon(big);
        if(small && small!=big)DestroyIcon(small);
        result_.game_icon_error = "unable to create both icon sizes from rayman2.exe";
        return;
    }
    big_icon_=big;
    small_icon_=small;
    result_.game_icon_available=true;
    result_.game_icon_source="rayman2.exe";
}

void WindowPresentation::apply_game_icons(void* raw_window) {
    if (!big_icon_ || !small_icon_) return;
    const HWND hwnd = static_cast<HWND>(raw_window);
    if (last_icon_window_ == hwnd) return;
    const DWORD now = GetTickCount();
    // A hung or just-created renderer must never block the 120ms
    // presentation supervisor indefinitely. Retry at most once/second.
    if (last_icon_try_ms_ && DWORD(now-last_icon_try_ms_) < 1000)
        return;
    last_icon_try_ms_ = now;

    DWORD_PTR previous_big = 0, previous_small = 0;
    const BOOL big_ok = SendMessageTimeoutW(
        hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon_),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &previous_big) != 0;
    const BOOL small_ok = SendMessageTimeoutW(
        hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon_),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &previous_small) != 0;
    // The old icon handles belong to the game's window/class; never
    // destroy them. Only destroy our ExtractIconEx handles on exit.
    (void)previous_big;
    (void)previous_small;
    if (!big_ok || !small_ok) {
        result_.game_icon_error = "WM_SETICON failed or game window hung";
        result_.game_icon_applied = false;
        return;
    }
    // A successful SendMessageTimeout means the window procedure returned;
    // it does NOT prove the game retained either icon. Cross-check with
    // WM_GETICON before calling this applied. This still cannot prove XFCE
    // sees Wine's X11 _NET_WM_ICON property.
    DWORD_PTR actual_big=0,actual_small=0;
    const BOOL get_big=SendMessageTimeoutW(
        hwnd,WM_GETICON,ICON_BIG,0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,100,&actual_big)!=0;
    const BOOL get_small=SendMessageTimeoutW(
        hwnd,WM_GETICON,ICON_SMALL,0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,100,&actual_small)!=0;
    result_.game_icon_readback=get_big && get_small &&
        actual_big==reinterpret_cast<DWORD_PTR>(big_icon_) &&
        actual_small==reinterpret_cast<DWORD_PTR>(small_icon_);
    result_.game_icon_applied=true;
    last_icon_window_=hwnd;
    ++result_.game_icon_updates;
    if(result_.game_icon_readback)result_.game_icon_error.clear();
    else result_.game_icon_error=
        "WM_SETICON returned but WM_GETICON did not retain both icons";
}

void WindowPresentation::update() {
    Search search{static_cast<DWORD>(pid_)};
    EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&search));
    if (!search.best) return;
    result_.window_found = true;

    // In multi-monitor configurations choose the primary screen used by
    // GLIDE_SIZE and the single user-facing Graphics.Resolution=Desktop.
    const HWND hwnd = search.best;
    // Updating the icon is independent of borderless geometry; avoid
    // skipping it when style/position have already converged.
    load_game_icons();
    char klass[100]{};
    if(GetClassNameA(hwnd,klass,100))
        result_.game_window_class=klass;
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
    // Setting WM_SETICON before SWP_FRAMECHANGED could be lost if Wine
    // recreates the X11 host surface. Assign only after the final style
    // and window-position update, or on subsequent settled polls.
    if (!new_window && style == desired_style &&
        exstyle == desired_exstyle && !needs_position) {
        apply_game_icons(hwnd);
        return;
    }

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
    apply_game_icons(hwnd);
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    using GetWindowDpi = UINT (WINAPI*)(HWND);
    const auto get_dpi = user ? reinterpret_cast<GetWindowDpi>(
        GetProcAddress(user, "GetDpiForWindow")) : nullptr;
    result_.observed_dpi = get_dpi ? get_dpi(hwnd) : display_.dpi;
}
PresentationResult WindowPresentation::result() const { return result_; }
} // namespace r2fix
#endif
