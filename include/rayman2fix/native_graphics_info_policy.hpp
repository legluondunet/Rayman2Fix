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
constexpr bool graphics_info_text_handle(unsigned id) noexcept {
    return id>=379u && id<=384u;
}
constexpr bool graphics_info_brightness_preserved(unsigned id) noexcept {
    return id==385u && !graphics_info_text_handle(id);
}
}
