#pragma once

#include <cstddef>
#include <cstdint>

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

struct IntroSkipGuard {
    bool inside = false;
    bool released = false;
    bool requested = false;
    // A fresh press can be queued while the engine is still loading.
    // Playing permits preflight, not necessarily native dispatch. Only a
    // successfully authorized request may consume the level transition.
    bool pending = false;

    // Only call these before any native level request or memory write.
    void defer_pre_dispatch() noexcept { pending = inside && requested; }
    void refuse_pre_dispatch() noexcept {
        pending = requested = released = false;
    }

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

// The authenticated Jail_20 controller's B2 initialization was observed
// to precede B1 (the only allowed writer/transition state). Never treat
// a missing/invalid behavior or any other refusal as authorization.
// Wait at most 2 s, poll the complete original preflight no more often
// than every 50 ms, and do not call the native transition during waiting.
struct ReplayOwnerWait {
    static constexpr std::uint32_t interval_ms=50, timeout_ms=2000;
    bool active=false;
    std::uint32_t started=0,last=0;
    bool defer(int behavior,std::uint32_t now) noexcept {
        if(behavior!=2 || (active && expired(now)))return false;
        if(!active){active=true;started=now;}
        last=now;
        return true;
    }
    bool expired(std::uint32_t now) const noexcept {
        return active && std::uint32_t(now-started)>=timeout_ms;
    }
    bool due(std::uint32_t now) const noexcept {
        return !active || std::uint32_t(now-last)>=interval_ms;
    }
    void clear() noexcept { *this={}; }
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

// A replay repair is eligible only after a real Learn_10 handover in this
// process, followed by menu OR an authenticated Pause restart, then
// jail_10 -> jail_20. A native queued target bridges the loading gap.
// Any unrelated level cancels the pending route; loading a save in
// Learn_10 does not earn a handover. Never infer a new game from bit 1091.
struct ReplayIntroRoute {
    enum class Stage { none, menu, pause_restart, first_intro, second_intro };
    Stage stage = Stage::none;
    bool completed = false;

    static bool equal(const char* a,const char* b) noexcept {
        if(!a || !b) return false;
        while(*a && *b) {
            unsigned char x=static_cast<unsigned char>(*a++);
            unsigned char y=static_cast<unsigned char>(*b++);
            if(x>='A' && x<='Z') x=static_cast<unsigned char>(x+32);
            if(y>='A' && y<='Z') y=static_cast<unsigned char>(y+32);
            if(x!=y)return false;
        }
        return *a==*b;
    }
    void note_handover() noexcept { completed=true; }
    void observe(const char* level, bool verified_pause_restart=false,
                 bool loading_jail10=false, bool loading_jail20=false) noexcept {
        if(!completed) { stage=Stage::none; return; }
        if(equal(level,"menu")) stage=Stage::menu;
        else if(equal(level,"jail_10")) {
            if(stage==Stage::menu || stage==Stage::pause_restart ||
               stage==Stage::first_intro)
                stage=Stage::first_intro;
            else stage=Stage::none;
        } else if(equal(level,"jail_20")) {
            if(stage==Stage::first_intro || stage==Stage::second_intro)
                stage=Stage::second_intro;
            else stage=Stage::none;
        } else if(equal(level,"learn_10") && verified_pause_restart) {
            stage=Stage::pause_restart;
        } else if((!level || !*level || equal(level,"learn_10")) &&
                  loading_jail10 &&
                  (stage==Stage::pause_restart || stage==Stage::menu)) {
            // A verified departure may unload the live Pause controller.
        } else if((!level || !*level) && loading_jail20 &&
                  stage==Stage::first_intro) {
            // Only the expected native destination bridges an empty name.
        } else stage=Stage::none;
    }
    bool ready() const noexcept {
        return completed && stage==Stage::second_intro;
    }
    void consume() noexcept { completed=false; stage=Stage::none; }
};
constexpr std::uint32_t replay_handover_mask = 4u;
constexpr std::uint32_t replay_handover_reset_word(std::uint32_t word) noexcept {
    return word & ~replay_handover_mask;
}

} // namespace r2fix
