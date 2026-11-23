#pragma once
#include <cstdint>
namespace r2fix {
enum class NativeVideoCall { selection, save, brightness };
// The two hashed-executable VIDEO dispatcher slots may be guarded before
// the Menu AI controller has been found. Once verified, guard only exact
// model script children. Never suppress brightness (separate procedure 357).
constexpr bool protected_menu_video_dispatch(
    bool verified_hook_active,bool menu_level,bool exact_script_child,
    bool approved_dispatch_fallback,NativeVideoCall which) noexcept {
    return verified_hook_active && menu_level &&
        (exact_script_child || approved_dispatch_fallback) &&
        which!=NativeVideoCall::brightness;
}
constexpr bool suppress_menu_video_call(
    bool approved_exe, bool validated_controller, bool menu_level,
    bool exact_script_child, NativeVideoCall which) noexcept {
    return approved_exe && validated_controller && menu_level &&
           exact_script_child && which!=NativeVideoCall::brightness;
}
// V51 is a live native menu latch, not a trustworthy submenu identity.
// The maintainer's Wine run shows V51 0->1 after Enter, and it remains 1
// across later re-entries to normal behavior 5. Preserve the information
// page for BOTH validated latch values; unknown values fail closed.
// V17=4 is another observed row in the verified normal-5 video behavior.
constexpr bool graphics_info_video_page(
    bool approved_exe, bool menu_level, bool controller_valid,
    int normal, int reflex, bool selection_valid, int selection,
    bool latch_valid, unsigned latch) noexcept {
    return approved_exe && menu_level && controller_valid &&
           normal==5 && reflex==0 &&
           selection_valid && selection>=1 && selection<=4 &&
           latch_valid && latch<=1u;
}
// The approved Menu.sna's normal-3 setup script references the same
// VIDEO text handles (379/381/382/383/380/385) as normal-5. Wine logs
// show normal=3/reflex=0/V17=0 immediately before the VIDEO page.
// Install the replacement localization pointers in this verified
// preparation state, BEFORE native text widgets cache the old labels.
// Never treat another normal behavior as a VIDEO entry from V17 alone.
constexpr bool graphics_info_video_preparation(
    bool approved_exe,bool menu_level,bool controller_valid,
    int normal,int reflex,bool selection_valid,int selection,
    bool latch_valid,unsigned latch) noexcept {
    return approved_exe && menu_level && controller_valid &&
        normal==3 && reflex==0 && selection_valid && selection==0 &&
        latch_valid && latch<=1u;
}
// Keep the same read-only information glyphs visible while the native
// BRIGHTNESS slider is selected after a verified VIDEO visit. The previous
// implementation restored the text immediately, leaving no information
// above the slider. The independent brightness text handle (385) must
// remain intact. Only the exact normal=18/reflex=5/V17=5 state qualifies.
// Do not infer this from an arbitrary V17=5 or an unknown menu screen.
constexpr bool graphics_info_brightness_with_panel(
    bool approved_exe,bool menu_level,bool controller_valid,
    int normal,int reflex,bool selection_valid,int selection,
    bool video_text_installed,bool previously_showed_information) noexcept {
    return approved_exe && menu_level && controller_valid &&
        normal==18 && reflex==5 && selection_valid && selection==5 &&
        video_text_installed && previously_showed_information;
}
constexpr bool graphics_info_text_handle(unsigned id) noexcept {
    return id>=379u && id<=384u;
}
constexpr bool graphics_info_brightness_preserved(unsigned id) noexcept {
    return id==385u && !graphics_info_text_handle(id);
}
}
