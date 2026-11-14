#pragma once

// C++17 experimental Rayman 2 ESC-music state machine.
// NO Win32 calls, game hooks, driver calls or side effects. This header
// does NOT authorize audio bypass: the runtime must independently verify
// ESC script origin, live music generation and native mixer semantics.
#include <array>
#include <cstdint>
#include <limits>

namespace r2fix::esc_music {

enum class Action : std::uint8_t {
    none, pause, resume, forward_native, forward_stop,
    suppress_paired_native, hold_unknown
};
enum class Cause : std::uint8_t { esc, focus, other, unknown };
enum class Route : std::uint8_t { theme, sxd };
enum class Kind : std::uint8_t { pause, resume };

struct Generation {
    std::uint32_t bank=0;
    std::uint32_t theme_index=0;
    std::uint32_t inner_bank=0;
    std::uint32_t inner_index=0;
    std::uint64_t token=0;

    bool valid() const noexcept {
        return token!=0 && bank<=0xffffu && theme_index<=0xffffu &&
               inner_bank<=0xffffu && inner_index<=0xffffu;
    }
    bool operator==(const Generation& o) const noexcept {
        return token==o.token && bank==o.bank &&
               theme_index==o.theme_index && inner_bank==o.inner_bank &&
               inner_index==o.inner_index;
    }
    bool operator!=(const Generation& o) const noexcept { return !(*this==o); }
};

struct Proof {
    bool current_level_music=false;
    bool esc_lifetime=false;
    bool mixer_adapter=false;
    bool focus_known=false;
    bool foreground=false;
};

// All actions are LOGICAL requests. The external APM mixer may require
// different callback cardinality; these must not be physically dispatched
// before that contract is verified on the original PC driver under Wine.
class Shadow final {
public:
    bool attach(const Generation& g, const Proof& p) noexcept {
        if(active_ || !g.valid() || !p.current_level_music ||
           !p.esc_lifetime || !p.mixer_adapter ||
           !p.focus_known || !p.foreground) return false;
        owner_=g;
        active_=true;
        foreground_=true;
        focus_known_=true;
        esc_open_=false;
        quarantined_=false;
        logical_paused_=false;
        other_depth_=0;
        callback_counts_={};
        return true;
    }

    Action menu_open(bool verified) noexcept {
        if(!active_) return Action::forward_native;
        if(!verified) return unknown();
        esc_open_=true;
        return reconcile();
    }
    Action menu_close(bool verified) noexcept {
        if(!active_) return Action::forward_native;
        if(!verified) return unknown();
        esc_open_=false;
        return reconcile();
    }
    Action focus(bool known, bool foreground) noexcept {
        if(!active_) return Action::none;
        focus_known_=known;
        foreground_=known && foreground;
        return reconcile();
    }
    Action other_pause() noexcept {
        if(!active_) return Action::none;
        if(other_depth_==std::numeric_limits<std::uint32_t>::max())
            return unknown();
        ++other_depth_;
        return reconcile();
    }
    Action other_resume() noexcept {
        if(!active_) return Action::none;
        if(other_depth_==0) return unknown();
        --other_depth_;
        return reconcile();
    }
    Action unknown() noexcept {
        if(!active_) return Action::forward_native;
        quarantined_=true;
        (void)reconcile(); // Never produce an artificial resume afterwards.
        return Action::hold_unknown;
    }
    Action callback(const Generation& g, Kind kind, Route route,
                    Cause cause) noexcept {
        if(!active_ || owner_!=g) return Action::forward_native;
        if(cause==Cause::unknown) return unknown();
        // Upstream classification MUST happen before the driver callback.
        // A generic game-script opcode or keyboard snapshot is not ESC proof.
        if((cause==Cause::esc &&
             (kind==Kind::pause ? !esc_open_ : esc_open_)) ||
           (cause==Cause::focus &&
             (kind==Kind::pause ? (focus_known_ && foreground_)
                                : (!focus_known_ || !foreground_))) ||
           (cause==Cause::other &&
             (kind==Kind::pause ? other_depth_==0 : other_depth_!=0)))
            return unknown();
        const unsigned k=kind==Kind::pause ? 0u : 1u;
        const unsigned r=route==Route::theme ? 0u : 1u;
        if(callback_counts_[k][r]!=std::numeric_limits<std::uint32_t>::max())
            ++callback_counts_[k][r];
        else
            return unknown();
        return quarantined_ ? Action::hold_unknown
                            : Action::suppress_paired_native;
    }
    Action stop(const Generation& g) noexcept {
        // Native Stop is ALWAYS forwarded, even for unknown/stale owners.
        if(!active_ || owner_!=g) return Action::forward_stop;
        active_=false;
        owner_={};
        esc_open_=false;
        foreground_=false;
        focus_known_=false;
        quarantined_=false;
        logical_paused_=false;
        other_depth_=0;
        callback_counts_={};
        return Action::forward_stop;
    }

    bool active() const noexcept { return active_; }
    bool logical_paused() const noexcept { return logical_paused_; }
    bool quarantined() const noexcept { return quarantined_; }
    bool foreground() const noexcept { return foreground_ && focus_known_; }
    bool esc_open() const noexcept { return esc_open_; }
    std::uint32_t other_depth() const noexcept { return other_depth_; }
    std::uint32_t observed_count(Kind k, Route r) const noexcept {
        return callback_counts_[k==Kind::pause?0u:1u][r==Route::theme?0u:1u];
    }

private:
    Action reconcile() noexcept {
        if(!active_) return Action::none;
        const bool want_pause = !focus_known_ || !foreground_ ||
                                other_depth_!=0 || quarantined_;
        if(want_pause==logical_paused_) return Action::none;
        logical_paused_=want_pause;
        return want_pause ? Action::pause : Action::resume;
    }
    Generation owner_{};
    bool active_=false;
    bool focus_known_=false;
    bool foreground_=false;
    bool esc_open_=false;
    bool quarantined_=false;
    bool logical_paused_=false;
    std::uint32_t other_depth_=0;
    std::array<std::array<std::uint32_t,2>,2> callback_counts_{};
};

} // namespace r2fix::esc_music
