#pragma once
// Diagnostic only: original Pause VIDEO may leave a native sound or
// brightness slider visible after returning to the game. Track only
// plausible *small* lower-right native ScreenImage/frame rectangles,
// before deciding which actor or native routine owns them.
// This function never suppresses any native draw.
#include <cmath>
namespace r2fix {
inline bool post_pause_ui_bar_probe(
    bool verified_video_seen,bool pause_video_current,
    unsigned native_producer,unsigned native_route,
    float xmin,float ymin,float xmax,float ymax) noexcept {
    if(!verified_video_seen || pause_video_current ||
       native_producer==0u || native_route>3u ||
       !std::isfinite(xmin)||!std::isfinite(xmax)||
       !std::isfinite(ymin)||!std::isfinite(ymax)||
       xmin>=xmax||ymin>=ymax)return false;
    const float w=xmax-xmin,h=ymax-ymin;
    // Allow the original 60..90/85..90 gauge as well as native
    // slider variants. Never match world-sized fullscreen quads.
    return xmin>=42.0f && xmax<=100.0f &&
           ymin>=65.0f && ymax<=98.0f &&
           w>=3.0f && w<=46.0f &&
           h>=0.7f && h<=12.0f;
}
} // namespace r2fix
