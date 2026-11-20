#pragma once

namespace r2fix {

// Side-effect-free pre-launch policy: only the normal user-facing command
// may re-execute under Wine with a larger host-side PulseAudio target.
// Importantly, the internal marker prevents an infinite relaunch loop even
// if an unknown Wine build does not pass host variables to Win32 processes.
enum class WineHostAudioAction {
    unchanged_command,
    unchanged_native_windows,
    unchanged_explicit_latency,
    unchanged_already_relaunched,
    reexec_with_60ms
};

constexpr WineHostAudioAction select_wine_host_audio(
    bool normal_launch, bool wine_detected,
    bool latency_already_present, bool already_relaunched) {
    if (!normal_launch) return WineHostAudioAction::unchanged_command;
    if (!wine_detected) return WineHostAudioAction::unchanged_native_windows;
    if (latency_already_present)
        return WineHostAudioAction::unchanged_explicit_latency;
    if (already_relaunched)
        return WineHostAudioAction::unchanged_already_relaunched;
    return WineHostAudioAction::reexec_with_60ms;
}

} // namespace r2fix
