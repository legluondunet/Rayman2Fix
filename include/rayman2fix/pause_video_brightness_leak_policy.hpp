#pragma once
// Fix.sna sometimes continues drawing its exact two-image *VIDEO*
// brightness meter after leaving normal9/reflex2. The renderer confirmed
// this artifact in the original 2026-11-23 Wine session: original 60..90%
// X, 85..90% Y, in VIDEO, normal6/reflex2 Options, and later gameplay.
// Other native image producers and the real SOUND controls are untouched.
// This latch begins only after a verified replacement VIDEO frame, and
// ends when the game returns to the top-level MENU scene.
namespace r2fix {
constexpr bool pause_video_brightness_leak_scope(
    bool verified_video_frame_ever_drawn,bool in_main_menu,
    bool generic_pause_open,int normal,int reflex) noexcept {
    if(!verified_video_frame_ever_drawn || in_main_menu)return false;
    if(!generic_pause_open)return true; // stale image inside gameplay
    return normal==6 && reflex==2; // exact verified Options return
}
} // namespace r2fix
