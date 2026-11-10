#include "rayman2fix/hud_math.hpp"

#include <cassert>
#include <cmath>

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

    return 0;
}
