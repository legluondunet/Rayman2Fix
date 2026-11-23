#pragma once
// Presentation-only geometry for the self-owned Glide VIDEO info page.
// The native animated butterfly/luciole is never transformed or moved:
// on the in-game ESC page the information panel sits lower, leaving
// room for its head and antennae above the blue outline.
namespace r2fix {
constexpr unsigned kVideoCanvasWidth=1024u;
constexpr unsigned kVideoCanvasHeight=768u;
constexpr unsigned kVideoInfoWidth=600u;
constexpr unsigned kVideoInfoHeight=320u;
constexpr unsigned kVideoInfoX=(kVideoCanvasWidth-kVideoInfoWidth)/2u;
constexpr unsigned kVideoMainInfoY=224u;
constexpr unsigned kVideoPauseInfoY=280u; // +56 inside 1024x768 canvas
constexpr unsigned video_info_panel_y(bool in_game_pause) noexcept {
    return in_game_pause?kVideoPauseInfoY:kVideoMainInfoY;
}
constexpr int kVideoInfoFontPreferredPx=19;
constexpr int kVideoInfoFontMinPx=12;
constexpr int kVideoInfoLabelLeft=40;
constexpr int kVideoInfoValueLeft=172;
constexpr int kVideoInfoValueRight=578;
constexpr int kVideoInfoTextPadding=2;
static_assert(kVideoInfoX+kVideoInfoWidth<=kVideoCanvasWidth,
              "VIDEO panel horizontal bounds");
static_assert(kVideoMainInfoY+kVideoInfoHeight<=kVideoCanvasHeight,
              "MENU VIDEO panel vertical bounds");
static_assert(kVideoPauseInfoY+kVideoInfoHeight<=kVideoCanvasHeight,
              "Pause VIDEO panel vertical bounds");
static_assert(kVideoPauseInfoY>kVideoMainInfoY,
              "Reserve clear space above pause panel for native luciole");
static_assert(kVideoInfoValueLeft<kVideoInfoValueRight,
              "Value column must have positive width");
} // namespace r2fix
