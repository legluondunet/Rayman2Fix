#pragma once

#include <cstdint>

namespace r2fix {
// Physical desktop pixels, not Windows DPI-virtualized logical coordinates.
struct DisplayMetrics {
    int left = 0;
    int top = 0;
    std::uint32_t width = 640;
    std::uint32_t height = 480;
    std::uint32_t dpi = 96;
};

// Call before querying monitor geometry or creating the Win32 child.
void configure_launcher_dpi_awareness();

// Physical primary monitor geometry; respects nonzero desktop origins.
// Windows fallback always refuses invalid monitor sizes.
DisplayMetrics primary_display_metrics();
}
