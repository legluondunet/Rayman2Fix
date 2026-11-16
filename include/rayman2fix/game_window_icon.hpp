#pragma once

#ifdef _WIN32
namespace r2fix {
// Called after the verified game runtime has initialized (NEVER under
// DllMain's loader lock). All HICONs are created inside rayman2.exe.
// The logger is the existing synchronous runtime logger and remains valid
// for the lifetime of the process.
void start_game_window_icon_worker(void (*logger)(const char*));
}
#endif
