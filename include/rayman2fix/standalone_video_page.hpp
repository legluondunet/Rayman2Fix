#pragma once
#include <cmath>
// Experimental self-owned, read-only video information page.
// No commercial assets or native VIDEO widgets are used by this renderer.
namespace r2fix {
using StandaloneVideoLogger=void (*)(const char*);
constexpr bool standalone_video_page_state(
    bool approved, bool menu_loaded, bool controller_valid,
    bool scripts_swapped, int normal, int reflex) noexcept {
    return approved && menu_loaded && controller_valid &&
           scripts_swapped && normal==5 && reflex==0;
}
void standalone_video_page_configure(StandaloneVideoLogger logger,
    unsigned output_width,unsigned output_height,bool classic_4_3) noexcept;
void standalone_video_page_set_visible(bool visible) noexcept;
// Separate ESC in-game model, never inferred from the MENU controller.
void standalone_video_page_set_pause_visible(bool visible) noexcept;
bool standalone_video_page_pause_is_drawn() noexcept;
// Render-thread-only: live authenticated Pause state and prepared panel.
bool standalone_video_page_pause_render_ready() noexcept;
// A verified VIDEO visit can leave its two obsolete native brightness
// images visible after returning to Options or gameplay. This scope is
// calculated by the authenticated Pause model observer; never suppress
// unrelated sound/options controls or native gameplay HUD in fresh runs.
void standalone_video_page_set_leaked_gauge_visible(bool suppress) noexcept;
bool standalone_video_page_suppress_leaked_gauge() noexcept;
// True only when this own-panel has rendered in an authenticated
// MENU or Pause VIDEO scene. Inputs in all other scenes are untouched.
bool standalone_video_page_owns_navigation(bool& main,bool& pause) noexcept;
// The native 2D percent sprite dispatcher identifies JFFTXT glyphs
// by the verified exact renderer return address, not screen geometry.
// Preserve only the original CENTERED VIDEO title in the known native
// 2D JFFTXT heading band, after independent verification of the
// normal-5 VIDEO state. This must NEVER be used in normal-3 or Options.
inline bool standalone_video_native_title_glyph(
    float xmin,float ymin,float xmax,float ymax) noexcept {
    return std::isfinite(xmin)&&std::isfinite(xmax)&&
           std::isfinite(ymin)&&std::isfinite(ymax)&&
           xmin>=28.0f && xmax<=72.0f &&
           ymin>=16.0f && ymax<=30.0f &&
           xmin<xmax && ymin<ymax;
}
constexpr bool standalone_video_hide_native_glyphs(
    bool page_visible,bool already_drawn,bool verified_glyph,
    bool verified_title) noexcept {
    return page_visible && already_drawn && verified_glyph &&
           !verified_title;
}
// Freeze title glyph positions only inside the authenticated VIDEO
// page, keeping the game's own font texture, colors and JFFTXT effects.
// This is not a general-purpose Options or HUD coordinate correction.
inline bool standalone_video_stabilize_native_title(
    float& xmin,float& ymin,float& xmax,float& ymax) noexcept {
    if(!standalone_video_native_title_glyph(xmin,ymin,xmax,ymax))
        return false;
    // Reference fixed Rayman 2 title row from source percent glyphs.
    // Selected text pulsation changes these extents (~19.66..25.34);
    // restore a fixed 5% high native header without touching materials.
    ymin=20.0f;
    ymax=25.0f;
    return true;
}
// Fix.sna ESC Video places its original heading slightly above the
// MENU.sna heading. The same 2D native font must remain visible in
// both controllers; allow the verified Pause title band separately.
inline bool standalone_video_stabilize_native_title_for_context(
    bool in_game_pause,float& xmin,float& ymin,
    float& xmax,float& ymax) noexcept {
    if(standalone_video_stabilize_native_title(xmin,ymin,xmax,ymax))
        return true;
    if(!in_game_pause ||
       !std::isfinite(xmin)||!std::isfinite(xmax)||
       !std::isfinite(ymin)||!std::isfinite(ymax)||
       xmin<28.0f || xmax>72.0f ||
       ymin<12.0f || ymax>30.0f ||
       xmin>=xmax || ymin>=ymax)
        return false;
    // Reuse MENU's verified title baseline and native glyph source;
    // do not re-enable the original ESC resolution/brightness widgets.
    ymin=20.0f;
    ymax=25.0f;
    return true;
}
bool standalone_video_page_filter_native_glyphs(
    float& xmin,float& ymin,float& xmax,float& ymax) noexcept;
// Invoked once per game buffer flip, on the owning Glide render thread.
// Returns false without altering a pixel if any Glide probe fails.
bool standalone_video_page_draw_before_flip() noexcept;
} // namespace r2fix
