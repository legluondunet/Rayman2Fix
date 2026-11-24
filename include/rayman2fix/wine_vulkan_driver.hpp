#pragma once
namespace r2fix {
// Optional Wine-only probe using Vulkan's own host-facing properties.
// Failure is non-fatal: keep the existing informative fallback.
bool wine_vulkan_driver_for_display(
    const char* wine_display_name,const char* wine_device_id,
    char* result,unsigned capacity,
    char* diagnostic,unsigned diagnostic_capacity) noexcept;
} // namespace r2fix
