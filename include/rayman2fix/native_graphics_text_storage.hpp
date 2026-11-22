#pragma once
// Native VIDEO localization transition policy, independent of Win32.
// The old strategy copied a new string into a previously installed
// buffer while keeping the pointer unchanged. Wine's actual menu did
// not refresh the rendered text and continued to show original labels.
// Install the INFORMATION pointers only while the verified VIDEO page
// is active; restore the exact original pointers on exit.
namespace r2fix {
enum class NativeGraphicsTextTransition {
    unchanged,
    install_information_pointers,
    restore_original_pointers
};
constexpr NativeGraphicsTextTransition native_graphics_text_transition(
    bool verified_video_page,bool info_pointers_installed) noexcept {
    return verified_video_page ?
        (info_pointers_installed ?
            NativeGraphicsTextTransition::unchanged :
            NativeGraphicsTextTransition::install_information_pointers) :
        (info_pointers_installed ?
            NativeGraphicsTextTransition::restore_original_pointers :
            NativeGraphicsTextTransition::unchanged);
}
} // namespace r2fix
