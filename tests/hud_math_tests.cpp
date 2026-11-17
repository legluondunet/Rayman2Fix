#include "rayman2fix/hud_math.hpp"

#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>

// Exercise every geometry check in Debug AND Release (NDEBUG disables assert).
#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")\\n"; \
        std::abort(); \
    } \
} while (false)

int main() {
    constexpr float source_width = 1024.0f;
    constexpr float epsilon = 0.0001f;
    auto near = [epsilon](float a, float b) { return std::fabs(a - b) < epsilon; };

    const float wide = r2fix::hud_4_3_scale(1920, 1080);
    CHECK(near(wide, 0.75f));
    CHECK(near(r2fix::centered_hud_x(0, source_width, wide), 128.0f));
    CHECK(near(r2fix::centered_hud_x(512, source_width, wide), 512.0f));
    CHECK(near(r2fix::centered_hud_x(1024, source_width, wide), 896.0f));
    // Both axes grow by the same number of physical pixels after correction.
    CHECK(near((896.0f - 128.0f) * 1920.0f / 1024.0f, 1440.0f));
    CHECK(near(1440.0f, 1080.0f * 4.0f / 3.0f));

    CHECK(near(r2fix::hud_4_3_scale(1280, 1024), 1.0f));
    CHECK(near(r2fix::hud_4_3_scale(1024, 768), 1.0f));
    CHECK(near(r2fix::hud_4_3_scale(1920, 1200), 5.0f / 6.0f));
    CHECK(near(r2fix::hud_4_3_scale(2560, 1080), 0.5625f));
    CHECK(near(r2fix::hud_4_3_scale(0, 1080), 1.0f));

    // The native GLI entry point consumes percent (0..100), with a
    // different coordinate system from the 1024-pixel driver vertices.
    // This protects against accidentally scaling by 1000 or twice.
    float xmin=0.0f,xmax=0.0f;
    CHECK(r2fix::center_native_2d_percent(0.0f,100.0f,wide,xmin,xmax));
    CHECK(near(xmin,12.5f) && near(xmax,87.5f));
    CHECK(r2fix::center_native_2d_percent(10.0f,90.0f,wide,xmin,xmax));
    CHECK(near(xmin,20.0f) && near(xmax,80.0f));
    const float ratio1610=r2fix::hud_4_3_scale(1920,1200);
    CHECK(r2fix::center_native_2d_percent(0.0f,100.0f,
                                           ratio1610,xmin,xmax));
    CHECK(near(xmin,100.0f/12.0f) &&
           near(xmax,100.0f-100.0f/12.0f));
    CHECK(r2fix::center_native_2d_percent(-20.0f,120.0f,
                                           ratio1610,xmin,xmax));
    CHECK(near(xmin,50.0f-70.0f*ratio1610));
    CHECK(near(xmax,50.0f+70.0f*ratio1610));
    CHECK(!r2fix::center_native_2d_percent(0.0f,100.0f,
                                             1.0f,xmin,xmax));
    CHECK(!r2fix::center_native_2d_percent(
        std::numeric_limits<float>::quiet_NaN(),100.0f,
        wide,xmin,xmax));
    CHECK(!r2fix::center_native_2d_percent(0.0f,
        std::numeric_limits<float>::infinity(),wide,xmin,xmax));

    r2fix::HudVertex quad[4] = {
        {0.0f,   0.0f,   1.0f, 0x11223344},
        {1024.0f,0.0f,   1.0f, 0x55667788},
        {1024.0f,768.0f, 1.0f, 0x99aabbcc},
        {0.0f, 768.0f,  1.0f, 0xddeeff00}
    };
    CHECK(r2fix::is_screen_sprite_quad(quad));
    r2fix::center_screen_sprite_quad(quad, source_width, wide);
    CHECK(near(quad[0].x, 128.0f) && near(quad[1].x, 896.0f));
    CHECK(near(quad[0].y, 0.0f) && near(quad[2].y, 768.0f));
    CHECK(quad[0].packed_color == 0x11223344 &&
           quad[1].packed_color == 0x55667788);

    r2fix::HudVertex world[4] = {
        {10,10,0.5f,0}, {20,10,0.5f,0}, {20,20,0.5f,0}, {10,20,0.5f,0}
    };
    CHECK(!r2fix::is_screen_sprite_quad(world));
    world[0].reciprocal_z = 1.0f;
    CHECK(!r2fix::is_screen_sprite_quad(world));
    CHECK(!r2fix::is_screen_sprite_quad(nullptr));

    // A 0-reciprocal-Z UV quad was observed on the real renderer,
    // but is not sufficient evidence of HUD ownership. The probe
    // must measure it without treating it as a 2D correction target.
    r2fix::HudVertex unknown_uv[4] = {
        {900,610,0.0f,0}, {940,610,0.0f,0},
        {940,660,0.0f,0}, {900,660,0.0f,0}
    };
    r2fix::ScreenQuadGeometry g{};
    CHECK(r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    CHECK(near(g.left,900.0f) && near(g.right,940.0f));
    CHECK(near(g.top,610.0f) && near(g.bottom,660.0f));
    CHECK(near(g.min_reciprocal_z,0.0f) &&
           near(g.max_reciprocal_z,0.0f));
    // The owner's observed 0..1024 × 34..618 UV quad is inset but
    // its texture, color and cinematic role remain unknown.
    r2fix::ScreenQuadGeometry cinematic_uv{
        0.0f,1024.0f,34.0f,618.0f,0.0f,0.0f
    };
    CHECK(r2fix::is_inset_full_width_uv_probe(cinematic_uv,1024,768));
    cinematic_uv.top=0.0f;
    cinematic_uv.bottom=768.0f;
    CHECK(!r2fix::is_inset_full_width_uv_probe(cinematic_uv,1024,768));
    cinematic_uv.top=34.0f;
    cinematic_uv.bottom=618.0f;
    cinematic_uv.right=450.0f;
    CHECK(!r2fix::is_inset_full_width_uv_probe(cinematic_uv,1024,768));
    cinematic_uv.right=1024.0f;
    CHECK(!r2fix::is_inset_full_width_uv_probe(cinematic_uv,0,768));
    CHECK(r2fix::is_lower_screen_probe(g));
    CHECK(r2fix::screen_quad_probe_sector(g,1024.0f)==2);
    CHECK(!r2fix::is_screen_sprite_quad(unknown_uv));
    unknown_uv[0].x=50.0f;
    CHECK(r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    CHECK(r2fix::screen_quad_probe_sector(g,1024.0f)==1);
    unknown_uv[0].x=900.0f;
    unknown_uv[0].y=50.0f;
    CHECK(r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    CHECK(!r2fix::is_lower_screen_probe(g));
    unknown_uv[0].y=610.0f;
    unknown_uv[0].x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    CHECK(!r2fix::inspect_screen_quad_geometry(nullptr,g));
    CHECK(r2fix::screen_quad_probe_sector(g,0.0f)==1);
    std::uint32_t rva=0;
    CHECK(r2fix::executable_callsite_rva(
        0x501234u,0x400000u,0x200000u,rva) && rva==0x101234u);
    CHECK(!r2fix::executable_callsite_rva(
        0x3fffffu,0x400000u,0x200000u,rva));
    CHECK(!r2fix::executable_callsite_rva(
        0x600000u,0x400000u,0x200000u,rva));
    CHECK(!r2fix::executable_callsite_rva(
        0x501234u,0,0x200000u,rva));
    CHECK(!r2fix::executable_callsite_rva(
        0x501234u,0x400000u,0,rva));
    CHECK(!r2fix::native_glyph_overlaps_frame_y(
        70.0f,75.0f,80.6f,92.4f));
    CHECK(r2fix::native_glyph_overlaps_frame_y(
        80.0f,85.0f,80.6f,92.4f));
    CHECK(!r2fix::native_glyph_overlaps_frame_y(
        90.0f,95.0f,60.0f,80.0f));
    CHECK(r2fix::native_glyph_outside_frame_x(
        90.0f,98.0f,14.0f,94.0f));
    CHECK(!r2fix::native_glyph_outside_frame_x(
        14.7f,18.9f,14.0f,94.0f));
    CHECK(!r2fix::native_glyph_overlaps_frame_y(
        std::numeric_limits<float>::quiet_NaN(),85.0f,80.6f,92.4f));
    CHECK(r2fix::native_glyph_y_band(10.0f,15.0f)==0u);
    CHECK(r2fix::native_glyph_y_band(45.0f,55.0f)==1u);
    CHECK(r2fix::native_glyph_y_band(65.0f,70.0f)==2u);
    CHECK(r2fix::native_glyph_y_band(80.0f,85.0f)==3u);
    CHECK(r2fix::native_glyph_y_band(90.0f,80.0f)==4u);
    CHECK(r2fix::native_glyph_y_band(
        std::numeric_limits<float>::quiet_NaN(),50.0f)==4u);
    return 0;
}
