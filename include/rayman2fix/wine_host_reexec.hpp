#pragma once

#include <filesystem>
#include <optional>

namespace r2fix {

// Return a process exit code only when a real host-side relaunch has run.
// std::nullopt means "do not re-execute", or a safe fallback to normal launch.
std::optional<int> maybe_reexec_with_wine_host_audio(
    const std::filesystem::path& launcher, bool normal_launch,
    bool already_relaunched, bool verbose);

} // namespace r2fix
