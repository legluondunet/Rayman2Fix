#pragma once

#include "rayman2fix/display.hpp"

#include <cstdint>
#include <string>

namespace r2fix {
// Run at a low rate while the game is alive. Never use exclusive fullscreen
// or HWND_TOPMOST, so Alt+Tab and other windows remain functional.
struct PresentationResult {
    bool window_found = false;
    bool borderless_applied = false;
    std::uint32_t observed_dpi = 0;
    unsigned changes = 0;
    std::string last_error;
};

#ifdef _WIN32
// Single process child HWND supervisor. The caller must be PMv2 DPI aware.
class WindowPresentation {
public:
    WindowPresentation(unsigned long child_pid, DisplayMetrics display);
    void update();
    PresentationResult result() const;
private:
    unsigned long pid_;
    DisplayMetrics display_;
    PresentationResult result_;
    void* last_window_ = nullptr;
};
#endif
} // namespace r2fix
