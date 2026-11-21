#include <cmath>
#include <cstdlib>
#include "rayman2fix/graphics_menu_glyph_layout.hpp"
using namespace r2fix;
void expect(bool ok) { if(!ok)std::abort(); }
int main() {
    GraphicsInfoQuad out{};
    // Native menu strings: 5 unevenly positioned rows -> ordered grid.
    expect(layout_graphics_info_quad({35,20,38,25},out));
    expect(out.left>=7.0f && out.left<12.0f &&
           out.top>=17.0f && out.bottom<22.0f);
    expect(layout_graphics_info_quad({47,64,50,69},out));
    expect(out.top>=28 && out.bottom<34);
    expect(layout_graphics_info_quad({55,34,58,39},out));
    expect(out.top>=39 && out.bottom<45);
    expect(layout_graphics_info_quad({8,45,11,50},out));
    expect(out.top>=50 && out.bottom<57);
    expect(layout_graphics_info_quad({55,55,58,60},out));
    expect(out.top>=61 && out.bottom<68);
    // Brightness, other pages, strange geometry and overlong quads unchanged.
    expect(!layout_graphics_info_quad({8,84,11,89},out));
    expect(!layout_graphics_info_quad({0,80,5,85},out));
    expect(!layout_graphics_info_quad({0,20,400,25},out));
    expect(!layout_graphics_info_quad({22,22,20,27},out));
    expect(!layout_graphics_info_quad({NAN,20,25,25},out));
    expect(!layout_graphics_info_quad({10,4,13,8},out));
    // A long Win32/Wine adapter name is kept in a left column, away
    // from the native selection butterfly (middle of the VIDEO page).
    GraphicsInfoQuad wide{};
    expect(layout_graphics_info_quad({55,34,200,39},wide));
    expect(wide.left>=5.0f && wide.right<55.0f);
    // Both the in-game 4:3 path and the centered 16:9 4:3 canvas
    // must remain valid. centering uses x'=50+(x-50)*0.75.
    const float screen_left=50.0f+(wide.left-50.0f)*0.75f;
    const float screen_right=50.0f+(wide.right-50.0f)*0.75f;
    expect(screen_left>=12.5f && screen_right<55.0f);
    // Reject bizarre off-column glyphs instead of remapping them:
    // the viewport hook must fail back to the untouched native quad.
    expect(!layout_graphics_info_quad({-9,45,210,50},wide));
    // For each known native information row, the first glyph should
    // start consistently in the left column, retaining y ordering.
    const GraphicsInfoQuad samples[]={
        {35,20,38,25}, {55,34,58,39}, {8,45,11,50},
        {55,55,58,60}, {47,64,50,69}};
    for(const auto& src:samples) {
        GraphicsInfoQuad transformed{};
        expect(layout_graphics_info_quad(src,transformed));
        expect(transformed.left>=5 && transformed.left<=12);
        expect(transformed.right<15);
    }
    return 0;
}
