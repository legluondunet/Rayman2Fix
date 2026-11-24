#pragma once
// Passive observer for the distinct in-game ESC graphics controller.
// This never writes, renders, changes modes or intercepts user input.
namespace r2fix {
using PauseVideoLogger=void(*)(const char*);
void start_pause_video_observer(PauseVideoLogger logger) noexcept;
} // namespace r2fix
