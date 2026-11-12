#pragma once

#include <cmath>
#include <cstdint>

namespace r2fix {

// The approved game is always rendered internally at 1024 x 768 (4:3).
// Only its screen-space sprites are squeezed horizontally; the 3D camera
// remains Hor+ and keeps the entire native monitor width.
struct HudVertex {
    float x;
    float y;
    float reciprocal_z;
    std::uint32_t packed_color;
};
static_assert(sizeof(HudVertex) == 16, "Rayman 2 screen vertex layout changed");

inline float hud_4_3_scale(std::uint32_t output_width,
                          std::uint32_t output_height) {
    if (!output_width || !output_height) return 1.0f;
    const double aspect = static_cast<double>(output_width) / output_height;
    if (!std::isfinite(aspect) || aspect <= 4.0 / 3.0 || aspect > 5.0)
        return 1.0f;
    return static_cast<float>((4.0 / 3.0) / aspect);
}

inline float centered_hud_x(float x, float source_width, float scale) {
    return (source_width * 0.5f) + (x - source_width * 0.5f) * scale;
}

// GLI_vDraw2DSpriteWithPercent (approved EXE VA 0x429670) takes
// horizontal coordinates in PERCENT, not pixels or 1000-unit text
// descriptor space. Its binary multiplies those coordinates by
// 0x49C618 = 0.01 and the native viewport width. The same centered
// affine correction, applied ONCE at the common 2D input boundary,
// therefore works for native fonts, image objects, and frame quads.
inline bool center_native_2d_percent(float x_min,float x_max,
                                     float scale,float& out_min,
                                     float& out_max) {
    if (!std::isfinite(x_min) || !std::isfinite(x_max) ||
        !std::isfinite(scale) || scale<=0.25f || scale>=1.0f ||
        std::fabs(x_min)>100000.0f || std::fabs(x_max)>100000.0f)
        return false;
    const float a=centered_hud_x(x_min,100.0f,scale);
    const float b=centered_hud_x(x_max,100.0f,scale);
    if (!std::isfinite(a) || !std::isfinite(b)) return false;
    out_min=a;
    out_max=b;
    return true;
}

// Original 2D sprite submission initializes all four xOoZ coordinates to 1.
// Do not modify world sprites and ordinary geometry with real perspective
// depths; do not make assumptions from menu names or level identifiers.
inline bool is_screen_sprite_quad(const HudVertex* quad) {
    if (!quad) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (!std::isfinite(quad[i].x) || !std::isfinite(quad[i].y) ||
            !std::isfinite(quad[i].reciprocal_z) ||
            std::fabs(quad[i].reciprocal_z - 1.0f) > 0.0001f ||
            std::fabs(quad[i].x) > 32768.0f ||
            std::fabs(quad[i].y) > 32768.0f)
            return false;
    }
    return true;
}

inline void center_screen_sprite_quad(HudVertex* quad,
                                      float source_width, float scale) {
    for (unsigned i = 0; i < 4; ++i)
        quad[i].x = centered_hud_x(quad[i].x, source_width, scale);
}
// Read-only classification of lower-screen quads not submitted by the
// verified native 2D percent path. Never use these predicates to move
// game vertices: arbitrary 3D world sprites may share screen coordinates.
struct ScreenQuadGeometry {
    float left=0.0f, right=0.0f, top=0.0f, bottom=0.0f;
    float min_reciprocal_z=0.0f, max_reciprocal_z=0.0f;
};
inline bool inspect_screen_quad_geometry(const HudVertex* quad,
                                        ScreenQuadGeometry& out) {
    if (!quad) return false;
    ScreenQuadGeometry bounds{};
    for (unsigned i=0; i<4; ++i) {
        const auto& p=quad[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
            !std::isfinite(p.reciprocal_z) ||
            std::fabs(p.x)>32768.0f || std::fabs(p.y)>32768.0f ||
            std::fabs(p.reciprocal_z)>32768.0f)
            return false;
        if(i==0) {
            bounds.left=bounds.right=p.x;
            bounds.top=bounds.bottom=p.y;
            bounds.min_reciprocal_z=bounds.max_reciprocal_z=p.reciprocal_z;
        } else {
            if (p.x<bounds.left) bounds.left=p.x;
            if (p.x>bounds.right) bounds.right=p.x;
            if (p.y<bounds.top) bounds.top=p.y;
            if (p.y>bounds.bottom) bounds.bottom=p.y;
            if (p.reciprocal_z<bounds.min_reciprocal_z)
                bounds.min_reciprocal_z=p.reciprocal_z;
            if (p.reciprocal_z>bounds.max_reciprocal_z)
                bounds.max_reciprocal_z=p.reciprocal_z;
        }
    }
    out=bounds;
    return true;
}
inline bool is_lower_screen_probe(const ScreenQuadGeometry& b) {
    return b.top>=540.0f && b.bottom<=850.0f;
}
// Spatial bins for OBSERVATION only, not a special-case UI correction.
inline unsigned screen_quad_probe_sector(const ScreenQuadGeometry& b,
                                        float source_width) {
    if (!std::isfinite(source_width) || source_width<=0.0f)
        return 1;
    const float center=(b.left+b.right)*0.5f;
    return center<source_width/3.0f ? 0u :
           center>source_width*2.0f/3.0f ? 2u : 1u;
}

} // namespace r2fix
