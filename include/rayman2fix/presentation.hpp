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
    // Best effort: native Windows/Wine taskbar and Alt+Tab icon. A missing
    // icon never prevents launch or modifies the authenticated game EXE.
    bool game_icon_available = false;
    bool game_icon_applied = false;
    unsigned game_icon_updates = 0;
    unsigned game_icon_groups = 0;
    bool game_icon_readback = false;
    std::string game_window_class;
    std::string game_icon_source;
    std::string game_icon_error;
    std::string last_error;
};

#ifdef _WIN32
// Single process child HWND supervisor. The caller must be PMv2 DPI aware.
class WindowPresentation {
public:
    // The two HICON handles are extracted from the owner's installed
    // rayman2.exe only. They remain alive until the game
    // process exits; the installer never embeds or redistributes an icon.
    WindowPresentation(unsigned long child_pid, DisplayMetrics display,
                       const std::wstring& game_executable);
    ~WindowPresentation();
    WindowPresentation(const WindowPresentation&) = delete;
    WindowPresentation& operator=(const WindowPresentation&) = delete;
    void update();
    PresentationResult result() const;
private:
    void load_game_icons();
    void apply_game_icons(void* hwnd);
    unsigned long pid_;
    DisplayMetrics display_;
    std::wstring game_executable_;
    PresentationResult result_;
    bool game_icons_loaded_ = false;
    void* big_icon_ = nullptr;
    void* small_icon_ = nullptr;
    void* last_icon_window_ = nullptr;
    unsigned long last_icon_try_ms_ = 0;
    void* last_window_ = nullptr;
};
#endif
} // namespace r2fix
