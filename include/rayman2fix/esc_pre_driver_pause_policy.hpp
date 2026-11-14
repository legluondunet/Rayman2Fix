#pragma once
// Research-only policy for a possible ESC source-scoped, pre-driver pause
// bridge. Pure C++17, does not hook or call any game/driver code.
//
// IMPORTANT: not connected to runtime.cpp. A deferred native Pause must be
// replayed *before* any unrelated driver action, in original order, and only
// while the exact original stream still exists. A skipped script Pause must
// never be paired with a forwarded original script Resume for the same
// stream. These rules are prerequisites, not a claim that the Wine/Windows
// driver bridge is ready.
#include <cstdint>
#include "rayman2fix/esc_music_wine_prototype.hpp"

namespace r2fix::pre_driver_pause {

using r2fix::esc_proto::Identity;

enum class State : std::uint8_t {
    idle, awaiting_first, holding_one, holding_pair,
    menu_committed, original_passthrough, retired
};
struct Decision {
    // The intercepted native operation is forwarded unless this is true.
    bool suppress_current=false;
    // Replay the 1 or 2 held *original* PauseTransitionSxd calls, in order,
    // BEFORE forwarding any current native driver operation. A future
    // runtime bridge must reverify the exact original stream at replay time.
    unsigned replay_held_pauses=0;
    bool impossible_replay=false;
};

class Policy {
public:
    static constexpr unsigned kExpectedPauses=2u;
    // Fail closed if the real game never shows the verified ESC actor.
    // This is an *upper bound for rollback*, never a delay to authorize
    // resume or to mask a mismatch. No timers or OS calls here.
    static constexpr std::uint32_t kMenuConfirmationDeadlineMs=100u;

    bool begin(bool confirmed_script_esc_origin, bool foreground,
               bool native_menu_closed) noexcept {
        if(state_!=State::idle || !confirmed_script_esc_origin ||
           !foreground || !native_menu_closed)return false;
        state_=State::awaiting_first;
        identity_={};
        held_=0;
        suppressed_resumes_=0;
        return true;
    }

    // Called strictly on the original script thread, before the native
    // driver call. The original Theme/SXD provenance is a *provisional*
    // unique owner; unlike a post-hoc synthetic Resume, we do not claim
    // that the controller's alternate live Theme pointer is equivalent.
    Decision script_pause(const Identity& live,bool unique_owner) noexcept {
        if(state_!=State::awaiting_first &&
           state_!=State::holding_one &&
           state_!=State::holding_pair)return {};
        if(!unique_owner || !live.valid() ||
           (held_ && !live.same(identity_)) ||
           held_>=kExpectedPauses) {
            return fallback(live.same(identity_));
        }
        if(!held_)identity_=live;
        ++held_;
        state_=(held_==kExpectedPauses) ?
            State::holding_pair : State::holding_one;
        return {true,0,false};
    }

    // The native script Pause wrapper MUST complete. One intercepted
    // callback is never considered an acceptable pair.
    Decision end_script_pause() noexcept {
        if(state_==State::holding_one)
            return fallback(true);
        if(state_==State::awaiting_first)
            state_=State::original_passthrough;
        return {};
    }

    // The menu flag alone does not prove ESC. Commit only on the actual
    // ESC actor and the same native Theme/SXD driver ownership. The
    // verified menu may not appear until AFTER the native wrapper returns.
    Decision menu_observation(bool menu_known,bool visible_esc_actor,
                              bool foreground,bool same_original_stream,
                              std::uint32_t age_ms) noexcept {
        if(state_!=State::holding_pair)return {};
        if(menu_known && visible_esc_actor && foreground &&
           same_original_stream) {
            state_=State::menu_committed;
            return {};
        }
        // A lost foreground invalidates ESC ownership immediately, even
        // before the render thread has observed the actual menu actor.
        // Holding back a native Pause during Alt+Tab is not acceptable.
        // The original stream still has to be rechecked before replay.
        if(!foreground ||
           (menu_known && visible_esc_actor && !same_original_stream) ||
           age_ms>=kMenuConfirmationDeadlineMs) {
            return fallback(same_original_stream);
        }
        return {};
    }

    // Any unrelated native operation while Pauses are merely held must
    // preserve driver call ordering. Focus callbacks are always native;
    // they are NEVER suppressed by this ESC-specific model.
    Decision other_native_operation(bool same_original_stream) noexcept {
        if(state_==State::holding_one ||
           state_==State::holding_pair)
            return fallback(same_original_stream);
        return {};
    }

    // The paired native resume must be consumed ONLY for the exact stream
    // whose two original pauses never reached the driver, and only from
    // the script-resume source. Unknown or focus-origin callbacks pass.
    Decision script_resume(const Identity& live,
                           bool confirmed_script_resume_source) noexcept {
        if(state_!=State::menu_committed ||
           !confirmed_script_resume_source)return {};
        if(!live.same(identity_) ||
           suppressed_resumes_>=kExpectedPauses) {
            state_=State::retired;
            return {};
        }
        ++suppressed_resumes_;
        if(suppressed_resumes_==kExpectedPauses)
            state_=State::retired;
        return {true,0,false};
    }

    // Stops and new stream owners cancel the escrow; never replay a
    // deferred Pause on a handle that may now belong to different music.
    // An original Stop during escrow invalidates any deferred Pause.
    // Silently retiring while a native Pause is held would hide a
    // bookkeeping debt: physical rollback is impossible after the stream
    // disappears. Explicitly report that barrier to a future bridge.
    // NOTE: this policy is pure research and is NOT connected to runtime.
    Decision native_stop(std::uint32_t handle) noexcept {
        if(identity_.handle!=handle || state_==State::idle)return {};
        const bool lost_held_pause=held_!=0u &&
            (state_==State::holding_one || state_==State::holding_pair);
        held_=0;
        state_=State::retired;
        return {false,0,lost_held_pause};
    }
    void exit_script_resume() noexcept {
        // A missing second Resume is not evidence for synthesizing a
        // driver call. Prefer native passthrough for subsequent events.
        if(state_!=State::idle)state_=State::retired;
    }
    void retire() noexcept {state_=State::retired;}
    void reset() noexcept {state_=State::idle;identity_={};held_=0; suppressed_resumes_=0;}

    State state() const noexcept{return state_;}
    unsigned held_pauses() const noexcept{return held_;}
    unsigned suppressed_resumes() const noexcept{return suppressed_resumes_;}
    Identity identity() const noexcept{return identity_;}
private:
    Decision fallback(bool same_original_stream) noexcept {
        const unsigned replay=same_original_stream ? held_ : 0u;
        const bool impossible=held_!=0u && !same_original_stream;
        held_=0;
        state_=State::original_passthrough;
        return {false,replay,impossible};
    }
    State state_=State::idle;
    Identity identity_{};
    unsigned held_=0;
    unsigned suppressed_resumes_=0;
};

} // namespace r2fix::pre_driver_pause
