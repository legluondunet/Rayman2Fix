#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace r2fix {

// The actual x86 JFFTXT_tdstString layout (ACP_Ray2, JFFTXT/JFFTXT.h).
// Use a fixed 32-bit pointer word so synthetic tests work on Linux/x64.
// Never dereference text_ptr or change the original engine-owned record.
struct JffTextString32 {
    std::uint32_t text_ptr = 0;
    float x = 0.0f;            // original game uses a 0..1000 canvas
    float y = 0.0f;
    float size = 0.0f;
    std::uint8_t alpha = 0;
    std::uint8_t frozen = 0;
    std::uint8_t wave = 0;
    std::uint8_t flags = 0;
    std::int32_t effect = 0;
    std::uint8_t forced_color = 0;
    std::uint8_t padding[3]{};
};
static_assert(sizeof(JffTextString32)==28, "x86 JFFTXT layout");
static_assert(offsetof(JffTextString32,x)==4, "JFFTXT X");
static_assert(offsetof(JffTextString32,y)==8, "JFFTXT Y");
static_assert(offsetof(JffTextString32,size)==12, "JFFTXT size");
static_assert(offsetof(JffTextString32,flags)==19, "JFFTXT flags");
static_assert(offsetof(JffTextString32,effect)==20, "JFFTXT effect");

// An additional scale correction is required for JFFTXT text: unlike
// 4:3 GLI sprite quads, native font placement uses a 1000-unit canvas.
// The text in Learn_10 currently begins *left* of the dialogue panel,
// while the black panel itself is already correctly centered by GLI.
// Apply the aspect correction to native font X and requested size,
// leaving the panel, face portrait, arrow, glyph content, colors and
// on-disk assets entirely untouched. Preserve native Y/line placement;
// the JFFTXT font engine derives its glyph height/line spacing from size.
// The user's Wine trace 20261110-165832 identifies a native centered
// caption family with X=500, size=20 and three repeatedly used origins
// Y=500/600/700. The previous generic Y>=660 test modified ONLY Y=700,
// leaving Y=500 and 600 at their original oversized glyph scale. Restrict
// this compatibility fix to this observed family. Do not apply an
// unrestricted "lower screen" rule that might change menus or HUD.
// Values are in the native 1000-unit JFFTXT coordinate system; NOT pixels.
// Keep the original per-call Y and let the native text engine decide
// centering/line breaks. The screen-space panel, face icon and arrow are
// handled by separate original sprite paths.
inline bool is_learn10_caption_family(const JffTextString32& text) {
    if (text.text_ptr==0 || !std::isfinite(text.x) ||
        !std::isfinite(text.y) || !std::isfinite(text.size) ||
        text.flags!=0 || std::fabs(text.x-500.0f)>1.0f ||
        std::fabs(text.size-20.0f)>0.5f)
        return false;
    return std::fabs(text.y-500.0f)<=1.0f ||
           std::fabs(text.y-600.0f)<=1.0f ||
           std::fabs(text.y-700.0f)<=1.0f;
}

inline bool fit_learn10_dialogue_text(const JffTextString32& original,
                                    float hud_scale,
                                    JffTextString32& adjusted) {
    if (!std::isfinite(hud_scale) || hud_scale>=0.9999f ||
        hud_scale<0.3f || !is_learn10_caption_family(original))
        return false;
    adjusted=original;
    adjusted.x=500.0f+(original.x-500.0f)*hud_scale;
    adjusted.size=original.size*hud_scale;
    return true;
}
} // namespace r2fix
