#pragma once

#include <cstddef>

namespace r2fix {

// Rayman 2's two new-game introduction levels are "jail_10" and
// "jail_20". "learn_10" follows jail_20. Only the SECOND intro is
// eligible; no global keyboard hook and no change to in-game cutscenes.
inline bool is_second_intro(const char* level) {
    if (!level) return false;
    constexpr char expected[] = "jail_20";
    for (std::size_t i = 0; i < sizeof(expected); ++i) {
        const unsigned char c = static_cast<unsigned char>(level[i]);
        const unsigned char lower = c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
        if (lower != static_cast<unsigned char>(expected[i])) return false;
        if (expected[i] == '\0') break;
    }
    return true;
}

// A deliberate key-down edge AFTER entering jail_20 is needed. Holding
// Enter from the New Game menu must not auto-skip the cinematic.
// The latch resets when the level changes, supporting later new games.
// Do not force the jail_20 -> learn_10 transition when the native
// story already reports handover. Let the game's original scene
// transition resolve that state without changing the saved flags.
constexpr bool intro_direct_transition_allowed(bool story_known,
                                                bool already_handed_over) noexcept {
    return !story_known || !already_handed_over;
}

// Replay fallback is deliberately separate from the direct level request.
// Only the original cinematic action is eligible, never gameplay,
// dialogue advance, a background window or an unknown engine mode.
constexpr bool intro_replay_native_action_allowed(bool replay_only,
    bool in_jail20, bool foreground, bool playing, bool skip_down,
    unsigned ipt_index) noexcept {
    return replay_only && in_jail20 && foreground && playing &&
           skip_down && ipt_index==20u;
}

struct IntroSkipGuard {
    bool inside = false;
    bool released = false;
    bool requested = false;
    // A fresh press can be queued while the engine is still loading.
    // Once Playing is observed, exactly ONE native change is issued.
    bool pending = false;

    // engine_ready must come from the original engine's Playing state,
    // not a guessed wall-clock delay. Default preserves existing tests.
    bool update(const char* level, bool any_skip_key_down,
                bool engine_ready = true) {
        if (!is_second_intro(level)) {
            inside = released = requested = pending = false;
            return false;
        }
        if (!inside) {
            inside = true;
            released = requested = pending = false;
            return false;
        }
        if (!any_skip_key_down) released = true;
        if (released && any_skip_key_down && !requested) {
            requested = true;
            pending = true;
        }
        if (pending && engine_ready) {
            pending = false;
            return true;
        }
        return false;
    }
};

// The third *sequence* happens inside the first playable level, Learn_10.
// Unlike jail_20, a level-change request here would restart the sequence or
// skip gameplay. Only arm on a fresh press while the native intro is active;
// one request per physical key press, with the scene marked complete
// by the game itself. A failed lookup must not permanently disarm the key.
inline bool is_first_playable_level(const char* level) {
    if (!level) return false;
    constexpr char expected[] = "learn_10";
    for (std::size_t i = 0; i < sizeof(expected); ++i) {
        const unsigned char c = static_cast<unsigned char>(level[i]);
        const unsigned char lower = c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
        if (lower != static_cast<unsigned char>(expected[i])) return false;
        if (expected[i] == '\0') break;
    }
    return true;
}

inline std::size_t story_word_index(unsigned bit_one_based) {
    return bit_one_based ? (bit_one_based - 1u) / 32u : 0u;
}

inline unsigned story_word_mask(unsigned bit_one_based) {
    return bit_one_based ? 1u << ((bit_one_based - 1u) % 32u) : 0u;
}

struct LearnIntroGuard {
    bool inside = false;
    bool released = false;
    bool requested = false;

    bool update(const char* level, bool scene_active, bool enter_down) {
        if (!is_first_playable_level(level) || !scene_active) {
            inside = released = requested = false;
            return false;
        }
        if (!inside) {
            inside = true;
            released = false;
            requested = false;
            return false;
        }
        if (!enter_down) {
            // Re-arm only after a genuine key release. A rejected/unsafe
            // attempt must not consume Enter for the entire level.
            released = true;
            requested = false;
            return false;
        }
        if (released && !requested) {
            requested = true;
            return true;
        }
        return false;
    }
};
} // namespace r2fix
