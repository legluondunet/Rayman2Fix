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
// Invoked once per game buffer flip, on the owning Glide render thread.
// Returns false without altering a pixel if any Glide probe fails.
bool standalone_video_page_draw_before_flip() noexcept;
} // namespace r2fix
