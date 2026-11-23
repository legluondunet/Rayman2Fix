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
bool standalone_video_page_suppress_native_glyphs(
    float xmin,float ymin,float xmax,float ymax) noexcept;
// Invoked once per game buffer flip, on the owning Glide render thread.
// Returns false without altering a pixel if any Glide probe fails.
bool standalone_video_page_draw_before_flip() noexcept;
} // namespace r2fix
