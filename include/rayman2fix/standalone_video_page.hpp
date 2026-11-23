#pragma once
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
// The native 2D percent sprite dispatcher identifies JFFTXT glyphs
// by the verified exact renderer return address, not screen geometry.
// Only the independent VIDEO page may suppress those glyphs.
constexpr bool standalone_video_hide_native_glyphs(
    bool page_visible, bool already_drawn, bool verified_glyph) noexcept {
    return page_visible && already_drawn && verified_glyph;
}
bool standalone_video_page_suppress_native_glyphs() noexcept;
// Invoked once per game buffer flip, on the owning Glide render thread.
// Returns false without altering a pixel if any Glide probe fails.
bool standalone_video_page_draw_before_flip() noexcept;
} // namespace r2fix
