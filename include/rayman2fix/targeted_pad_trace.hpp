#pragma once
#include "rayman2fix/gamepad_input_policy.hpp"
#include <array>

namespace r2fix {
// Passive, bounded per-control evidence for the remaining hardware tests.
// No input delivery or game memory is altered. Each action has its own
// budget so normal jumping cannot exhaust the F1 or diving evidence.
inline constexpr std::array<PadAction,4> kTargetedPadActions{
    PadAction::jump, PadAction::target, PadAction::world_knowledge, PadAction::murphy};
enum class PadTraceRoute : unsigned { jump, strafe, dive, surface, dialogue, f1 };
struct PadTraceState {
    bool physical=false, accepted=false;
    std::uint32_t device=0,scene=0;
};
class TargetedPadTrace {
public:
    static constexpr unsigned max_changes=64;
    template<class Report>
    void observe(const PadSample& sample,const PadTransitions& transitions,Report report) {
        const bool gameplay=sample.connected && sample.device_id &&
                            sample.context==PadContext::gameplay;
        const bool physical[]{sample.south,sample.left_trigger>kPadTriggerThreshold,
                             sample.dpad_up,sample.dpad_down};
        for(unsigned i=0;i<slots_.size();++i) {
            auto& s=slots_[i];
            const auto action=kTargetedPadActions[i];
            const bool raw=gameplay && physical[i];
            const bool accepted=gameplay && pad_eligible(sample) &&
                                (transitions.held&pad_action_bit(action));
            const PadTraceState next{raw,accepted,gameplay?sample.device_id:0u,
                                     gameplay?sample.scene_id:0u};
            const bool boundary=next.device!=s.state.device || next.scene!=s.state.scene;
            const bool changed=boundary || raw!=s.state.physical || accepted!=s.state.accepted;
            const bool relevant=raw || accepted || s.state.physical || s.state.accepted;
            if(boundary || !accepted || !s.state.accepted)s.routes=0;
            s.state=next;
            if(changed && relevant && s.changes<max_changes) {
                ++s.changes;
                report(action,next,s.changes==max_changes);
            }
        }
    }
    bool note_route(PadAction action,PadTraceRoute route) noexcept {
        for(unsigned i=0;i<slots_.size();++i) {
            if(kTargetedPadActions[i]!=action)continue;
            auto& s=slots_[i];
            const auto n=static_cast<unsigned>(route);
            if(n>static_cast<unsigned>(PadTraceRoute::f1) ||
               !s.state.accepted || s.changes>=max_changes)return false;
            const auto mask=1u<<n;
            if(s.routes&mask)return false;
            s.routes|=mask;
            return true;
        }
        return false;
    }
private:
    struct Slot { PadTraceState state{}; unsigned routes=0,changes=0; };
    std::array<Slot,4> slots_{};
};
} // namespace r2fix
