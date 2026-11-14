#pragma once
// Experimental, build-time-gated native ESC music compensator policy.
// Pure C++17: no Windows APIs and NO real driver calls.
// An explicit opt-in x86 integration must independently verify the current
// Theme player, loaded bank, foreground and native ESC script path.
#include <cstdint>

namespace r2fix::esc_proto {

enum class Phase : std::uint8_t {
    idle, pending_menu, audible_menu, muted_focus, closing_unfocused, quarantined
};
enum class Origin : std::uint8_t {
    unknown, script_pause, script_resume, focus_pause, focus_resume
};
enum class Command : std::uint8_t { none, pause, resume };
struct Dispatch {
    Command command=Command::none;
    std::uint32_t handle=0;
    unsigned count=0;
    std::uint64_t generation=0;
    explicit operator bool() const noexcept { return command!=Command::none; }
};
struct Identity {
    std::uint32_t handle=0;
    std::uint32_t native_theme_key=0;
    std::uint32_t native_inner_key=0;
    std::uint64_t generation=0;
    bool valid() const noexcept {
        return handle!=0 && handle!=0xffffffffu && generation!=0 &&
               native_theme_key!=0 && native_inner_key!=0;
    }
    bool same(const Identity& other) const noexcept {
        return handle==other.handle &&
               native_theme_key==other.native_theme_key &&
               native_inner_key==other.native_inner_key &&
               generation==other.generation;
    }
};

// This policy assumes the game's original paired Theme/SXD calls have
// already reached the real driver. A matching *experimental* resume batch
// starts only AFTER native AI_g_bInGameMenu rises, never from Escape key alone.
// Driver call cardinality is observed (exactly two) and NOT independently
// established as correct for the user's installed APM backend.
// No production setting or music modification is enabled.
class Coordinator {
public:
    bool begin_script_pause(bool escape_pressed, bool foreground,
                            bool native_menu_initially_closed) noexcept {
        if(phase_!=Phase::idle || !escape_pressed || !foreground ||
           !native_menu_initially_closed) return false;
        phase_=Phase::pending_menu;
        identity_={};
        observed_pauses_=0;
        ignored_resumes_=0;
        menu_open_=false;
        return true;
    }
    void observe_native_pause(const Identity& id, bool verified_current_theme) noexcept {
        if(phase_!=Phase::pending_menu)return;
        if(!verified_current_theme || !id.valid() ||
           (observed_pauses_ && !id.same(identity_)) ||
           observed_pauses_>=2) {
            quarantine();
            return;
        }
        identity_=id;
        ++observed_pauses_;
    }
    // Called after the game's native wrapper returns. If only a single
    // callback was seen, never assume the APM adapter supports one call.
    void end_script_pause() noexcept {
        if(phase_==Phase::pending_menu && observed_pauses_!=2)
            quarantine();
    }
    Dispatch menu_tick(bool known, bool menu_open, bool foreground,
                       bool owner_same) noexcept {
        if(phase_==Phase::idle || phase_==Phase::quarantined)return {};
        if(!known || !owner_same) {
            quarantine();
            return {};
        }
        if(phase_==Phase::pending_menu) {
            if(!menu_open)return {}; // native callbacks precede UI flag rise
            if(!foreground || observed_pauses_!=2) {
                quarantine();
                return {};
            }
            menu_open_=true;
            phase_=Phase::audible_menu;
            return make(Command::resume);
        }
        if(menu_open_ && !menu_open && ignored_resumes_!=2) {
            // Unexpected menu removal while the physical driver was
            // compensated: fail closed rather than creating a bare resume.
            quarantine();
            return {};
        }
        menu_open_=menu_open;
        if(phase_==Phase::audible_menu && !foreground) {
            phase_=Phase::muted_focus;
            return make(Command::pause);
        }
        if(phase_==Phase::muted_focus && foreground && menu_open_) {
            phase_=Phase::audible_menu;
            return make(Command::resume);
        }
        if(phase_==Phase::closing_unfocused && foreground && !menu_open_) {
            const Dispatch result=make(Command::resume);
            clear();
            return result;
        }
        if(phase_==Phase::audible_menu && !menu_open_ && ignored_resumes_==2)
            clear();
        return {};
    }
    Dispatch focus_pause() noexcept {
        if(phase_==Phase::audible_menu) {
            phase_=Phase::muted_focus;
            return make(Command::pause);
        }
        return {};
    }
    Dispatch focus_resume() noexcept {
        if(phase_==Phase::muted_focus && menu_open_) {
            phase_=Phase::audible_menu;
            return make(Command::resume);
        }
        if(phase_==Phase::closing_unfocused && !menu_open_) {
            const Dispatch result=make(Command::resume);
            clear();
            return result;
        }
        return {};
    }
    // Return false only for the same live music stream while our physical
    // compensation is in charge. All other native sounds pass through.
    bool native_resume(std::uint32_t handle, Origin source) noexcept {
        if(handle!=identity_.handle ||
           (phase_!=Phase::audible_menu && phase_!=Phase::muted_focus &&
            phase_!=Phase::closing_unfocused))return true;
        if(source==Origin::script_resume && menu_open_) {
            if(ignored_resumes_>=2) {
                quarantine();
                return true;
            }
            ++ignored_resumes_;
            return false;
        }
        if(source==Origin::focus_resume)return false;
        // Unknown owners are never allowed to unmute the compensated
        // stream when the game may be inactive or a pause is still held.
        quarantine();
        return false;
    }
    // The game may resume script audio *before* the visible menu flag
    // goes to zero. Count the actual suppressed native resume calls.
    void end_script_resume() noexcept {
        if(phase_==Phase::audible_menu || phase_==Phase::muted_focus) {
            if(ignored_resumes_!=2) {quarantine();return;}
            if(phase_==Phase::muted_focus)
                phase_=Phase::closing_unfocused;
        }
    }
    // Any matching Stop wins over synthetic resume, including a Stop while
    // global native pause depth is still two.
    void native_stop(std::uint32_t handle) noexcept {
        if(handle==identity_.handle && phase_!=Phase::idle)clear();
    }
    // Unrelated native script pause, contradictory UI evidence, or driver
    // failure: never issue a synthetic resume again for this generation.
    void quarantine() noexcept {
        if(phase_!=Phase::idle)phase_=Phase::quarantined;
    }
    bool current(std::uint64_t token) const noexcept {
        return phase_!=Phase::idle && phase_!=Phase::quarantined &&
               identity_.generation==token && token!=0;
    }
    Phase phase() const noexcept {return phase_;}
    std::uint32_t current_handle() const noexcept{return identity_.handle;}
    unsigned native_pause_count() const noexcept{return observed_pauses_;}
    unsigned ignored_resume_count() const noexcept{return ignored_resumes_;}
    bool menu_open() const noexcept{return menu_open_;}
    void reset() noexcept {clear();}
private:
    Dispatch make(Command c) const noexcept {
        return {c,identity_.handle,observed_pauses_,identity_.generation};
    }
    void clear() noexcept {
        phase_=Phase::idle;
        identity_={};
        observed_pauses_=0;
        ignored_resumes_=0;
        menu_open_=false;
    }
    Phase phase_=Phase::idle;
    Identity identity_{};
    unsigned observed_pauses_=0;
    unsigned ignored_resumes_=0;
    bool menu_open_=false;
};
} // namespace r2fix::esc_proto
