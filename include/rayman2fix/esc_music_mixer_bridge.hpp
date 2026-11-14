#pragma once

// A strictly isolated, opt-in *contract* for delivering logical ESC-music
// pause/resume actions to an already validated native audio driver.
// This header does NOT load or hook any DLL and has no enabled default.
// It refuses all actions until the original-CD owner, mixer callback
// contract, driver handle and nonzero generation token are independently
// verified by a future Windows x86 adapter. No SFX are addressed.
#include "rayman2fix/esc_music_shadow.hpp"
#include <cstdint>

namespace r2fix::esc_music {

struct MixerProof {
    bool original_cd_verified=false;
    bool current_music_verified=false;
    bool mixer_pause_resume_contract_verified=false;
    bool exact_handle_verified=false;
    // Native logs show two Theme/SXD routes. This does not independently
    // prove physical mixer callback cardinality: require explicit evidence.
    std::uint32_t pause_calls=0;
    std::uint32_t resume_calls=0;
};

enum class Delivery : std::uint8_t {
    ignored, rejected, delivered, uncertain
};

// Physical dispatch is provided by an injected adapter, never this code.
// A callback returns true only when the native call succeeded completely.
class MixerBridge final {
public:
    bool bind(const Generation& g, std::uint32_t handle,
              const MixerProof& proof) noexcept {
        if(bound_ || !g.valid() || handle==0 || handle==0xffffffffu ||
           !proof.original_cd_verified || !proof.current_music_verified ||
           !proof.mixer_pause_resume_contract_verified ||
           !proof.exact_handle_verified ||
           proof.pause_calls==0 || proof.resume_calls==0 ||
           proof.pause_calls>2 || proof.resume_calls>2)
            return false;
        owner_=g;
        handle_=handle;
        pause_calls_=proof.pause_calls;
        resume_calls_=proof.resume_calls;
        bound_=true;
        uncertain_=false;
        return true;
    }

    template<class Call>
    Delivery deliver(const Generation& g, Action action, Call&& call) noexcept {
        if(!bound_ || owner_!=g) return Delivery::rejected;
        if(uncertain_) return Delivery::uncertain;
        if(action==Action::none) return Delivery::ignored;
        // Stop, native passthrough, pending callbacks and unknowns are
        // handled OUTSIDE this bridge; none can be converted to resume.
        if(action!=Action::pause && action!=Action::resume)
            return Delivery::rejected;
        const std::uint32_t count=action==Action::pause
                                     ? pause_calls_ : resume_calls_;
        for(std::uint32_t i=0;i<count;++i) {
            // A partial callback batch is unsafe. Fail closed and do NOT
            // issue further resumes on this generation after a failure.
            if(!call(action,handle_)) {
                uncertain_=true;
                return Delivery::uncertain;
            }
        }
        return Delivery::delivered;
    }

    bool retire(const Generation& g) noexcept {
        if(!bound_ || owner_!=g) return false;
        bound_=false;
        owner_={};
        handle_=0;
        pause_calls_=resume_calls_=0;
        uncertain_=false;
        return true;
    }
    bool bound() const noexcept { return bound_; }
    bool uncertain() const noexcept { return uncertain_; }

private:
    Generation owner_{};
    std::uint32_t handle_=0;
    std::uint32_t pause_calls_=0;
    std::uint32_t resume_calls_=0;
    bool bound_=false;
    bool uncertain_=false;
};

} // namespace r2fix::esc_music
