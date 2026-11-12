#include "rayman2fix/hud_math.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    constexpr float source_width = 1024.0f;
    constexpr float epsilon = 0.0001f;
    auto near = [epsilon](float a, float b) { return std::fabs(a - b) < epsilon; };

    const float wide = r2fix::hud_4_3_scale(1920, 1080);
    assert(near(wide, 0.75f));
    assert(near(r2fix::centered_hud_x(0, source_width, wide), 128.0f));
    assert(near(r2fix::centered_hud_x(512, source_width, wide), 512.0f));
    assert(near(r2fix::centered_hud_x(1024, source_width, wide), 896.0f));
    // Both axes grow by the same number of physical pixels after correction.
    assert(near((896.0f - 128.0f) * 1920.0f / 1024.0f, 1440.0f));
    assert(near(1440.0f, 1080.0f * 4.0f / 3.0f));

    assert(near(r2fix::hud_4_3_scale(1280, 1024), 1.0f));
    assert(near(r2fix::hud_4_3_scale(1024, 768), 1.0f));
    assert(near(r2fix::hud_4_3_scale(1920, 1200), 5.0f / 6.0f));
    assert(near(r2fix::hud_4_3_scale(2560, 1080), 0.5625f));
    assert(near(r2fix::hud_4_3_scale(0, 1080), 1.0f));

    // The native GLI entry point consumes percent (0..100), with a
    // different coordinate system from the 1024-pixel driver vertices.
    // This protects against accidentally scaling by 1000 or twice.
    float xmin=0.0f,xmax=0.0f;
    assert(r2fix::center_native_2d_percent(0.0f,100.0f,wide,xmin,xmax));
    assert(near(xmin,12.5f) && near(xmax,87.5f));
    assert(r2fix::center_native_2d_percent(10.0f,90.0f,wide,xmin,xmax));
    assert(near(xmin,20.0f) && near(xmax,80.0f));
    const float ratio1610=r2fix::hud_4_3_scale(1920,1200);
    assert(r2fix::center_native_2d_percent(0.0f,100.0f,
                                           ratio1610,xmin,xmax));
    assert(near(xmin,100.0f/12.0f) &&
           near(xmax,100.0f-100.0f/12.0f));
    assert(r2fix::center_native_2d_percent(-20.0f,120.0f,
                                           ratio1610,xmin,xmax));
    assert(near(xmin,50.0f-70.0f*ratio1610));
    assert(near(xmax,50.0f+70.0f*ratio1610));
    assert(!r2fix::center_native_2d_percent(0.0f,100.0f,
                                             1.0f,xmin,xmax));
    assert(!r2fix::center_native_2d_percent(
        std::numeric_limits<float>::quiet_NaN(),100.0f,
        wide,xmin,xmax));
    assert(!r2fix::center_native_2d_percent(0.0f,
        std::numeric_limits<float>::infinity(),wide,xmin,xmax));

    r2fix::HudVertex quad[4] = {
        {0.0f,   0.0f,   1.0f, 0x11223344},
        {1024.0f,0.0f,   1.0f, 0x55667788},
        {1024.0f,768.0f, 1.0f, 0x99aabbcc},
        {0.0f, 768.0f,  1.0f, 0xddeeff00}
    };
    assert(r2fix::is_screen_sprite_quad(quad));
    r2fix::center_screen_sprite_quad(quad, source_width, wide);
    assert(near(quad[0].x, 128.0f) && near(quad[1].x, 896.0f));
    assert(near(quad[0].y, 0.0f) && near(quad[2].y, 768.0f));
    assert(quad[0].packed_color == 0x11223344 &&
           quad[1].packed_color == 0x55667788);

    r2fix::HudVertex world[4] = {
        {10,10,0.5f,0}, {20,10,0.5f,0}, {20,20,0.5f,0}, {10,20,0.5f,0}
    };
    assert(!r2fix::is_screen_sprite_quad(world));
    world[0].reciprocal_z = 1.0f;
    assert(!r2fix::is_screen_sprite_quad(world));
    assert(!r2fix::is_screen_sprite_quad(nullptr));

    // A 0-reciprocal-Z UV quad was observed on the real renderer,
    // but is not sufficient evidence of HUD ownership. The probe
    // must measure it without treating it as a 2D correction target.
    r2fix::HudVertex unknown_uv[4] = {
        {900,610,0.0f,0}, {940,610,0.0f,0},
        {940,660,0.0f,0}, {900,660,0.0f,0}
    };
    r2fix::ScreenQuadGeometry g{};
    assert(r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    assert(near(g.left,900.0f) && near(g.right,940.0f));
    assert(near(g.top,610.0f) && near(g.bottom,660.0f));
    assert(near(g.min_reciprocal_z,0.0f) &&
           near(g.max_reciprocal_z,0.0f));
    assert(r2fix::is_lower_screen_probe(g));
    assert(r2fix::screen_quad_probe_sector(g,1024.0f)==2);
    assert(!r2fix::is_screen_sprite_quad(unknown_uv));
    unknown_uv[0].x=50.0f;
    assert(r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    assert(r2fix::screen_quad_probe_sector(g,1024.0f)==1);
    unknown_uv[0].x=900.0f;
    unknown_uv[0].y=50.0f;
    assert(r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    assert(!r2fix::is_lower_screen_probe(g));
    unknown_uv[0].y=610.0f;
    unknown_uv[0].x=std::numeric_limits<float>::quiet_NaN();
    assert(!r2fix::inspect_screen_quad_geometry(unknown_uv,g));
    assert(!r2fix::inspect_screen_quad_geometry(nullptr,g));
    assert(r2fix::screen_quad_probe_sector(g,0.0f)==1);
    return 0;
}
