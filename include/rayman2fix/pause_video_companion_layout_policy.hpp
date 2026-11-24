#pragma once
// In the original Fix.sna ESC VIDEO UI, the native 3D menu companion
// is projected lower than in Menu.sna. Lift *only* a centered approved
// native UI actor while the authenticated ESC VIDEO information panel
// is already drawn. Do not alter world cameras, sprite geometry, MENU
// butterfly, other Options pages, or underlying source assets.
#include <cmath>
namespace r2fix {
constexpr float kPauseVideoCompanionLift=0.11f; // normalized source Y (~85px in 768 canvas)
inline bool lift_pause_video_companion(
    bool pause_video_drawn,bool approved_ui_viewport,
    unsigned native_source_route,float x,float& y,float depth) noexcept {
    if(!pause_video_drawn || !approved_ui_viewport ||
       (native_source_route!=0u && native_source_route!=1u) ||
       !std::isfinite(x) || !std::isfinite(y) ||
       !std::isfinite(depth) ||
       x<0.40f || x>0.64f ||
       y<0.16f || y>0.64f ||
       depth<0.0f || depth>0.25f)
        return false;
    const float lifted=y-kPauseVideoCompanionLift;
    if(lifted<0.0f)return false;
    y=lifted;
    return true;
}
} // namespace r2fix
