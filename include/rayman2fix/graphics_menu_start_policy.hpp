#pragma once
#include <cstdint>
// Compile-time feature presence and the user's INI logging flag are
// different concerns. The experimental native VIDEO information screen
// and its script-level video-write guards must initialize even at Log=0.
// Passive observations which exist solely to write diagnostics remain
// disabled unless logging is explicitly enabled.
namespace r2fix {
// A bounded scan may stop part-way through a large MEM_PRIVATE allocation.
// Preserve the address of the first unread byte, not just the region base.
// Only wrap after reaching the end of the eligible 32-bit space.
constexpr std::uintptr_t menu_scan_resume_cursor(
    std::uintptr_t next,std::uintptr_t address_space_end) noexcept {
    return next>=0x10000u && next<address_space_end ?
        next : static_cast<std::uintptr_t>(0x10000u);
}
constexpr bool menu_controller_worker_required(
    bool logging_enabled,bool graphics_info_compiled) noexcept {
    return graphics_info_compiled || logging_enabled;
}
constexpr bool menu_passive_diagnostics_required(
    bool logging_enabled) noexcept {
    return logging_enabled;
}
// A normal game session can return to Options after many minutes.
// Native graphics information requires continuous controller recovery
// across game->MENU transitions; trace-only diagnostics stay bounded.
constexpr bool menu_controller_worker_continue(
    bool native_info_compiled,unsigned iteration,unsigned reports,
    unsigned max_iterations,unsigned max_reports) noexcept {
    return native_info_compiled ||
           (iteration<max_iterations && reports<max_reports);
}
// Never launch a potentially expensive full memory scan during normal
// gameplay in the persistent native info configuration.
constexpr bool menu_controller_scan_allowed(
    bool native_info_compiled,bool menu_level,unsigned attempts,
    unsigned max_attempts) noexcept {
    return attempts<max_attempts &&
           (!native_info_compiled || menu_level);
}
} // namespace r2fix
