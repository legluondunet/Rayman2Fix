#include "rayman2fix/display.hpp"

#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace r2fix {
#ifdef _WIN32
namespace {
// Runtime dynamically discovers Vista/Win10 DPI APIs: Windows, Wine and
// older SDKs may differ. PMv2 is preferred; system-aware is fallback.
using DpiContext = HANDLE;
using SetDpiContext = BOOL (WINAPI*)(DpiContext);
using SetThreadDpiContext = DpiContext (WINAPI*)(DpiContext);
using GetDpiWindow = UINT (WINAPI*)(HWND);
using GetDpiMonitor = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);
constexpr intptr_t kPerMonitorV2 = -4;
constexpr intptr_t kPerMonitor = -3;
} // namespace
#endif

void configure_launcher_dpi_awareness() {
#ifdef _WIN32
    const auto user = GetModuleHandleW(L"user32.dll");
    const auto set = user ? reinterpret_cast<SetDpiContext>(
        GetProcAddress(user, "SetProcessDpiAwarenessContext")) : nullptr;
    if (set) {
        if (set(reinterpret_cast<DpiContext>(kPerMonitorV2)) ||
            set(reinterpret_cast<DpiContext>(kPerMonitor)))
            return;
    }
    // Already configured by a process manifest is fine. This fallback
    // avoids GetSystemMetrics returning logical pixels on older systems.
    SetProcessDPIAware();
#endif
}

DisplayMetrics primary_display_metrics() {
#ifdef _WIN32
    // A DPI-aware calling process sees native desktop coordinates.
    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0},
                                             MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
        throw std::runtime_error("cannot determine primary monitor bounds");
    DisplayMetrics result;
    result.left = info.rcMonitor.left;
    result.top = info.rcMonitor.top;
    const auto width = info.rcMonitor.right - info.rcMonitor.left;
    const auto height = info.rcMonitor.bottom - info.rcMonitor.top;
    if (width < 640 || width > 16384 || height < 480 || height > 8640)
        throw std::runtime_error("invalid primary monitor physical bounds");
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);

    const HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        const auto get = reinterpret_cast<GetDpiMonitor>(
            GetProcAddress(shcore, "GetDpiForMonitor"));
        UINT horizontal = 96, vertical = 96;
        if (get && SUCCEEDED(get(monitor, 0, &horizontal, &vertical)) &&
            horizontal >= 48 && horizontal <= 960)
            result.dpi = horizontal;
        FreeLibrary(shcore);
    }
    if (result.dpi == 96) {
        const auto user = GetModuleHandleW(L"user32.dll");
        const auto get = user ? reinterpret_cast<GetDpiWindow>(
            GetProcAddress(user, "GetDpiForWindow")) : nullptr;
        const auto desktop = GetDesktopWindow();
        if (get && desktop) {
            const auto dpi = get(desktop);
            if (dpi >= 48 && dpi <= 960) result.dpi = dpi;
        }
    }
    return result;
#else
    return {};
#endif
}

} // namespace r2fix
