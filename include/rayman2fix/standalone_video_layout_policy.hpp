#pragma once
// Presentation-only geometry for the self-owned Glide VIDEO info page.
// The native animated butterfly/luciole is never transformed or moved:
// both MENU and in-game ESC use the same lower panel position, leaving
// room for native butterfly/luciole above the blue outline.
namespace r2fix {
constexpr unsigned kVideoCanvasWidth=1024u;
constexpr unsigned kVideoCanvasHeight=768u;
constexpr unsigned kVideoInfoWidth=600u;
constexpr unsigned kVideoInfoHeight=320u;
constexpr unsigned kVideoInfoX=(kVideoCanvasWidth-kVideoInfoWidth)/2u;
constexpr unsigned kVideoInfoY=304u; // +24 on both pages; leaves extra clearance above frame
constexpr unsigned video_info_panel_y(bool /*in_game_pause*/) noexcept {
    return kVideoInfoY; // identical geometry for MENU and in-game ESC
}
constexpr int kVideoInfoFontPreferredPx=19;
constexpr int kVideoInfoFontMinPx=12;
constexpr int kVideoInfoLabelLeft=40;
constexpr int kVideoInfoValueLeft=172;
constexpr int kVideoInfoValueRight=578;
constexpr int kVideoInfoTextPadding=2;
static_assert(kVideoInfoX+kVideoInfoWidth<=kVideoCanvasWidth,
              "VIDEO panel horizontal bounds");
static_assert(kVideoInfoY+kVideoInfoHeight<=kVideoCanvasHeight,
              "Both VIDEO panels must fit the Glide canvas");
static_assert(video_info_panel_y(false)==video_info_panel_y(true),
              "MENU and Pause VIDEO panels must share the same vertical position");
static_assert(kVideoInfoValueLeft<kVideoInfoValueRight,
              "Value column must have positive width");
} // namespace r2fix
