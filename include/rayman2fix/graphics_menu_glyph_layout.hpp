#pragma once
// Experimental menu-only glyph placement. The game's native font and
// texture pipeline are preserved: only the original percent quad geometry
// of verified Resolution-page text is transformed. Never call this for
// other menu pages or unknown executable/script states.
#include <cmath>
namespace r2fix {
struct GraphicsInfoQuad { float left=0, top=0, right=0, bottom=0; };
struct GraphicsInfoRow {
    float first_y, last_y, source_x, source_y, target_y;
};
// Source bands are the visual MENU level's native JFFTXT glyph percent
// positions observed in the owner-provided 1920x1080 Wine screenshot.
// The slots are spatially out of order (379,381,382,383,380).
// These are provisional and must be validated under Wine and Windows.
// Brightness lives below y=80 and is never touched.
constexpr GraphicsInfoRow kGraphicsInfoRows[]{
    {18.0f,28.0f,35.0f,20.0f,18.0f}, // title, handle 379
    {31.0f,43.0f,55.0f,34.0f,40.0f}, // GPU, handle 381
    {43.0f,53.0f, 8.0f,45.0f,51.0f}, // driver, handle 382
    {53.0f,62.0f,55.0f,55.0f,62.0f}, // depth, handle 383
    {62.0f,75.0f,47.0f,64.0f,29.0f}  // output, handle 380
};
inline bool layout_graphics_info_quad(
    const GraphicsInfoQuad& in,GraphicsInfoQuad& out) noexcept {
    if(!std::isfinite(in.left) || !std::isfinite(in.top) ||
       !std::isfinite(in.right) || !std::isfinite(in.bottom) ||
       in.right<=in.left || in.bottom<=in.top ||
       in.left < -10.0f || in.right > 210.0f ||
       in.top < 0.0f || in.bottom > 80.0f)
        return false;
    const float middle=(in.top+in.bottom)*0.5f;
    for(const auto& row:kGraphicsInfoRows) {
        if(middle<row.first_y || middle>=row.last_y)continue;
        // Keep the entire line within the original 4:3-safe canvas.
        const float x0=21.0f+(in.left-row.source_x)*0.34f;
        const float x1=21.0f+(in.right-row.source_x)*0.34f;
        const float y0=row.target_y+(in.top-row.source_y)*0.48f;
        const float y1=row.target_y+(in.bottom-row.source_y)*0.48f;
        if(x0<10.0f || x1>90.0f || y0<12.0f || y1>78.0f)
            return false;
        out={x0,y0,x1,y1};
        return true;
    }
    return false;
}
} // namespace r2fix
