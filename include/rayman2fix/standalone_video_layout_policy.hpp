#pragma once
// Presentation-only geometry for the self-owned Glide VIDEO info page.
// The native animated butterfly/luciole is never transformed or moved:
// both MENU and in-game ESC use the same lower panel position, leaving
// room for native butterfly/luciole above the blue outline.
namespace r2fix {
constexpr unsigned kVideoCanvasWidth=1024u;
constexpr unsigned kVideoCanvasHeight=768u;
constexpr unsigned kVideoInfoWidth=600u;
constexpr unsigned kVideoInfoHeight=368u;
constexpr unsigned kVideoInfoX=(kVideoCanvasWidth-kVideoInfoWidth)/2u;
constexpr unsigned kVideoInfoY=284u; // raised 20 native pixels on both pages
constexpr unsigned video_info_panel_y(bool /*in_game_pause*/) noexcept {
    return kVideoInfoY; // identical geometry for MENU and in-game ESC
}
constexpr unsigned kVideoInfoRowCount=6u;
constexpr unsigned kVideoInfoPanelAlpha=102u; // exactly 40% of 255
constexpr int kVideoInfoRowTop=42;
constexpr int kVideoInfoRowStep=53;
constexpr int kVideoInfoRowHeight=38;
constexpr int kVideoInfoWrappedRowHeight=48;
constexpr int video_info_row_y(unsigned row) noexcept {
    return kVideoInfoRowTop+int(row)*kVideoInfoRowStep;
}
static_assert(video_info_row_y(kVideoInfoRowCount-1u)+
              kVideoInfoWrappedRowHeight<int(kVideoInfoHeight)-2,
              "Last VIDEO row stays inside the rounded frame");
static_assert(kVideoInfoPanelAlpha*5u==255u*2u,
              "VIDEO panel opacity must be exactly 40 percent");
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
