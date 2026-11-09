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
} // namespace r2fix
