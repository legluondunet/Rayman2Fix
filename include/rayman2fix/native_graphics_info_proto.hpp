#pragma once
namespace r2fix {
using GraphicsInfoLogger=void (*)(const char*);
void start_native_graphics_info_proto(GraphicsInfoLogger logger,
                                      unsigned output_width,
                                      unsigned output_height) noexcept;
// Only called for the verified native game glyph producer, before 4:3
// horizontal centering. Returns false without altering coordinates
// outside the validated Resolution page.
bool remap_native_graphics_info_glyph(float& xmin,float& ymin,
                                      float& xmax,float& ymax) noexcept;
// Read-only, bounded geometry evidence for the native screen-image/cursor
// producer while the video information page is active. This does not
// suppress or move the image.
void observe_native_graphics_info_screen_image(float xmin,float ymin,
                                               float xmax,float ymax) noexcept;
}
