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
// In the new VIDEO information-only variant, script #2 no longer
// enters native brightness at V17=5. V17=5 is therefore still part
// of the same read-only page. This extension is valid ONLY after
// the exact transition-suppression script copy has been installed.
// Without that proof, 5 remains the obsolete native brightness entry.
constexpr bool graphics_info_readonly_video_page(
    bool approved_exe,bool menu_level,bool controller_valid,
    int normal,int reflex,bool selection_valid,int selection,
    bool latch_valid,unsigned latch,
    bool brightness_transition_disabled) noexcept {
    if(graphics_info_video_page(approved_exe,menu_level,controller_valid,
        normal,reflex,selection_valid,selection,latch_valid,latch))
        return true;
    return brightness_transition_disabled && approved_exe && menu_level &&
        controller_valid && normal==5 && reflex==0 &&
        selection_valid && selection==5 &&
        latch_valid && latch<=1u;
}
// All four-copy MENU guards are installed before the video entry.
// Static source verification of the approved 638463-byte Menu.sna finds
// the VIDEO-localization handles 379..385 exclusively in the controller
// normal behaviors 3, 5, 13 and 19. The game's native renderer can
// cache older strings BEFORE normal-3 is polled: preinstall once in the
// approved MENU session rather than at the first normal-3 observation.
// The characters do not affect input, mode selection, or other menus.
// Return the original strings only when MENU unloads (or on rollback).
constexpr bool graphics_info_pin_text_for_menu(
    bool approved_exe,bool menu_loaded,bool guarded,
    bool controller_valid,bool readonly_script_copies_active,
    bool pointers_already_swapped) noexcept {
    return approved_exe && menu_loaded && guarded && controller_valid &&
        readonly_script_copies_active && !pointers_already_swapped;
}
constexpr bool graphics_info_restore_pinned_text(
    bool menu_loaded,bool readonly_script_copies_active) noexcept {
    return !menu_loaded || !readonly_script_copies_active;
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
// Native VIDEO preparation normal-3 can briefly move V17 from 0 to
// 1 and back (observed on the user's second visit, ~71-73 seconds).
// The original script may still paint old cached selection glyphs in
// that phase. Only the successfully protected read-only MENU may
// suppress those preparation glyphs; do not suppress Options text.
constexpr bool graphics_info_readonly_preparation(
    bool approved_exe,bool menu_level,bool controller_valid,
    int normal,int reflex,bool selection_valid,int selection,
    bool latch_valid,unsigned latch,
    bool readonly_script_copies_active) noexcept {
    return readonly_script_copies_active && approved_exe &&
        menu_level && controller_valid && normal==3 && reflex==0 &&
        selection_valid && (selection==0 || selection==1) &&
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
