#pragma once
// Passive observer for the distinct in-game ESC graphics controller.
// This never writes, renders, changes modes or intercepts user input.
namespace r2fix {
using PauseVideoLogger=void(*)(const char*);
void start_pause_video_observer(PauseVideoLogger logger) noexcept;
// Live model, generic pause flag and current normal/reflex are checked on use.
bool pause_menu_live_state(int& normal, int& reflex) noexcept;
} // namespace r2fix
