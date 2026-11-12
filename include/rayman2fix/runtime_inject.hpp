#pragma once

#include <cstdint>
#include <filesystem>

namespace r2fix {

struct FovHookResult {
    bool attempted = false;
    bool dll_loaded = false;
    bool active = false;
    unsigned long init_status = 0;
    unsigned long win32_error = 0;
};

// Inject only our SHA256-pinned Rayman2Fix.dll into the independently verified
// Rayman 2 game process. All memory hooks are installed by its exported
// init function, never inside DllMain. Failure is diagnostic, not fatal to
// the already-working game process.
FovHookResult initialize_game_fov(
    std::uintptr_t process_handle, unsigned long process_id,
    const std::filesystem::path& dll,
    std::uint32_t width, std::uint32_t height, bool log_enabled,
    bool skip_cinematics);
}
