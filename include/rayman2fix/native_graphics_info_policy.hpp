#pragma once
#include <cstdint>
namespace r2fix {
enum class NativeVideoCall { selection, save, brightness };
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
constexpr bool graphics_info_text_handle(unsigned id) noexcept {
    return id>=379u && id<=384u;
}
constexpr bool graphics_info_brightness_preserved(unsigned id) noexcept {
    return id==385u && !graphics_info_text_handle(id);
}
}
