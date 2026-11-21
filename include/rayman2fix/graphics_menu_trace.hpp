#pragma once

// Diagnostic-only bounded monitor for Rayman 2's approved 1999 CD executable.
// This is NOT the Options > Resolution menu detector. It never hooks a video
// function, modifies a menu, sends input or changes a display mode.
namespace r2fix {
using GraphicsTraceLogger = void (*)(const char*);
void start_graphics_menu_trace(GraphicsTraceLogger logger) noexcept;
} // namespace r2fix
