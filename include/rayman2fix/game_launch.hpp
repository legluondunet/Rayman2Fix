#pragma once

#include <filesystem>
#include <string>

namespace r2fix {

class Startup;

struct GameLaunchPreflight {
    bool allowed = false;
    std::string reason;
};

// Only the exact user-supplied, SHA256-approved unprotected Rayman 2
// executable can be launched; the original protected CD EXE must remain
// verifiably saved under rayman2.exe.bak.
GameLaunchPreflight check_game_launch(const std::filesystem::path& game_dir);

struct ChildProcessResult {
    bool started = false;
    unsigned long exit_code = 0;
    unsigned long error_code = 0;
};

// Spawn a Windows process with an explicit working directory, using the
// executable path as lpApplicationName rather than going through a shell.
// Wait until it exits so Rayman2Fix.log can record the actual game result.
ChildProcessResult run_child_process(
    const std::filesystem::path& executable,
    const std::filesystem::path& working_directory);

// After a successful automatic installation/verification, try launching the
// supported game edition. Return 2 if blocked, 1 for OS/game error.
int launch_verified_game(Startup& startup);

} // namespace r2fix
