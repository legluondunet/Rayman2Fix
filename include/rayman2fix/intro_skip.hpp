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
struct IntroSkipGuard {
    bool inside = false;
    bool released = false;
    bool requested = false;

    bool update(const char* level, bool any_skip_key_down) {
        if (!is_second_intro(level)) {
            inside = false;
            released = false;
            requested = false;
            return false;
        }
        if (!inside) {
            inside = true;
            released = false;
            requested = false;
            return false;
        }
        if (!any_skip_key_down) {
            released = true;
            return false;
        }
        if (released && !requested) {
            requested = true;
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
