#include <cmath>
#include <cstdlib>
#include "rayman2fix/graphics_menu_glyph_layout.hpp"
using namespace r2fix;
void expect(bool ok) { if(!ok)std::abort(); }
int main() {
    GraphicsInfoQuad out{};
    // Native menu strings: 5 unevenly positioned rows -> ordered grid.
    expect(layout_graphics_info_quad({35,20,38,25},out));
    expect(out.left>=20 && out.top>=17 && out.bottom<22);
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
    return 0;
}
