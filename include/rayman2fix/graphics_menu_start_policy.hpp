#pragma once
// Compile-time feature presence and the user's INI logging flag are
// different concerns. The experimental native VIDEO information screen
// and its script-level video-write guards must initialize even at Log=0.
// Passive observations which exist solely to write diagnostics remain
// disabled unless logging is explicitly enabled.
namespace r2fix {
constexpr bool menu_controller_worker_required(
    bool logging_enabled,bool graphics_info_compiled) noexcept {
    return graphics_info_compiled || logging_enabled;
}
constexpr bool menu_passive_diagnostics_required(
    bool logging_enabled) noexcept {
    return logging_enabled;
}
} // namespace r2fix
