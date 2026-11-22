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
// positions observed in the owner-provided screenshot.
// Preserve y=18..28: this is the native VIDEO heading (handle 371),
// not a replaceable VIDEO information field. The four data rows are
// handles 381,382,383,380; handle 379 is blanked by localization.
// These bands remain provisional until validated in Wine/Windows.
// Brightness lives below y=80 and is never touched.
constexpr GraphicsInfoRow kGraphicsInfoRows[]{
    {31.0f,43.0f,55.0f,34.0f,42.0f}, // GPU, handle 381
    {43.0f,53.0f,55.0f,45.0f,52.0f}, // driver, handle 382
    {53.0f,62.0f,55.0f,55.0f,62.0f}, // Glide + desktop, handle 383
    {62.0f,75.0f,47.0f,64.0f,32.0f}  // output, handle 380
};
// A native VIDEO choice widget can retain text prepared with the
// information pointers after the original localization table is restored.
// On the separately verified BRIGHTNESS behavior only, discard its stale
// glyph quads at the original four VIDEO choice bands. The VIDEO title
// (y≈20), LUMINOSITY (y≈85), color bar (non-glyph), and 3D butterfly are
// outside this policy. Never apply it to unknown/other menu behaviors.
inline bool cached_video_glyph_in_brightness(
    bool verified_brightness,bool previously_showed_information,
    const GraphicsInfoQuad& in) noexcept {
    if(!verified_brightness || !previously_showed_information ||
       !std::isfinite(in.left) || !std::isfinite(in.top) ||
       !std::isfinite(in.right) || !std::isfinite(in.bottom) ||
       in.right<=in.left || in.bottom<=in.top ||
       in.left<35.0f || in.right>210.0f)return false;
    const float mid=(in.top+in.bottom)*0.5f;
    for(const auto& row:kGraphicsInfoRows)
        if(mid>=row.first_y && mid<row.last_y)return true;
    return false;
}
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
        // The named menu selection butterfly remains rendered around
        // the middle of the native VIDEO choice region. Until its draw
        // owner is verified, preserve the actor and lay out read-only
        // information in a deliberately LEFT-SIDED column instead.
        // This only remaps the font quads in the verified video state:
        // it does not affect native selection, other menus or brightness.
        // Smaller glyph scale also makes long adapter descriptions fit.
        constexpr float kInfoColumnX=8.0f;
        constexpr float kInfoGlyphScale=0.28f;
        const float x0=kInfoColumnX+(in.left-row.source_x)*kInfoGlyphScale;
        const float x1=kInfoColumnX+(in.right-row.source_x)*kInfoGlyphScale;
        const float y0=row.target_y+(in.top-row.source_y)*0.40f;
        const float y1=row.target_y+(in.bottom-row.source_y)*0.40f;
        // Bounded 4:3 percent canvas; allow long GPU text only as far
        // as this left-column geometry can position it safely.
        if(x0<5.0f || x1>65.0f || y0<12.0f || y1>78.0f)
            return false;
        out={x0,y0,x1,y1};
        return true;
    }
    return false;
}
} // namespace r2fix
