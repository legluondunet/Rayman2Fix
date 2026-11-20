#pragma once

namespace r2fix {

// Keep this policy free of Win32/Wine calls, so every branch is testable
// on Linux and Windows CI. The 60 ms request is a Wine/PulseAudio
// compatibility experiment, not a guaranteed end-to-end output latency.
enum class WineAudioLatencyAction {
    unchanged_unverified,
    unchanged_native,
    unchanged_user_configured,
    request_60ms
};

constexpr WineAudioLatencyAction select_wine_audio_latency(
    bool verified_game, bool wine_detected, bool pulse_latency_present) {
    return !verified_game
        ? WineAudioLatencyAction::unchanged_unverified
        : !wine_detected
            ? WineAudioLatencyAction::unchanged_native
            : pulse_latency_present
                ? WineAudioLatencyAction::unchanged_user_configured
                : WineAudioLatencyAction::request_60ms;
}

} // namespace r2fix
