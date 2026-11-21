#pragma once
// Read-only screen-geometry candidate for the original Rayman 2 menu
// selection sprite. The exact cursor identity is NOT established.
// No vertices are moved or hidden based on this predicate.
#include <cmath>
namespace r2fix {
struct GraphicsMenuCursorBounds {
    float left, right, top, bottom;
};
inline bool may_be_graphics_menu_cursor(
    GraphicsMenuCursorBounds b) noexcept {
    if(!std::isfinite(b.left)||!std::isfinite(b.right)||
       !std::isfinite(b.top)||!std::isfinite(b.bottom))
        return false;
    const float width=b.right-b.left, height=b.bottom-b.top;
    const float center_x=(b.left+b.right)*0.5f;
    const float center_y=(b.top+b.bottom)*0.5f;
    // Internal GameFrame is always 1024x768; both supplied 1920x1080
    // screenshots show the butterfly in the middle of this canvas.
    // Intentionally permissive so missing candidates are informative.
    return width>=12.0f && width<=260.0f &&
           height>=12.0f && height<=220.0f &&
           center_x>=250.0f && center_x<=800.0f &&
           center_y>=85.0f && center_y<=630.0f;
}
} // namespace r2fix
