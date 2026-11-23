#pragma once
namespace r2fix {
using GraphicsInfoLogger=void (*)(const char*);
void start_native_graphics_info_proto(GraphicsInfoLogger logger,
                                      unsigned output_width,
                                      unsigned output_height,
                                      bool classic_4_3) noexcept;
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
// Only called synchronously at approved native GLI flip on its game
// thread, never from the asynchronous 100-ms controller worker.
void native_graphics_info_stabilize_video_cursor_at_flip() noexcept;
#endif
// Only called for the verified native game glyph producer, before 4:3
// horizontal centering. Returns false without altering coordinates
// outside the validated Resolution page.
bool remap_native_graphics_info_glyph(float& xmin,float& ymin,
                                      float& xmax,float& ymax) noexcept;
// Only for the verified native font producer in the separately verified
// BRIGHTNESS behavior, after the video information was shown. Returns
// true to discard a stale cached VIDEO information glyph. Never touches
// the original brightness slider or butterfly.
bool suppress_cached_native_graphics_info_glyph(
    float xmin,float ymin,float xmax,float ymax) noexcept;
// Snapshot the verified native VIDEO focus (V17) for the scoped glyph
// material diagnostic. Returns -1 for all other screens; no writes.
int native_graphics_info_focus_row() noexcept;
// This compatibility entry point intentionally never hides glyphs:
 // normal-3 also renders the parent Options menu. Position-only
 // filtering previously removed legitimate Options labels.
bool suppress_legacy_video_preparation_glyph(
    float xmin,float ymin,float xmax,float ymax) noexcept;
// Remove exactly the two verified native brightness-meter screen-image
// quads while our authenticated information-only VIDEO page is active.
// No caller may use this on other menus or for 3D butterfly rendering.
bool suppress_readonly_video_brightness_meter(unsigned producer,
    float xmin,float ymin,float xmax,float ymax) noexcept;
// Read-only, bounded geometry evidence for each non-glyph producer in
// the verified video information page: screen image, frame and other.
// route values match Native2DProducer (1,2,3). Never draws or hides.
void observe_native_graphics_info_non_glyph(unsigned route,
    float xmin,float ymin,float xmax,float ymax) noexcept;
}
