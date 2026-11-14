// Research model only. No Rayman 2 process or native SXD driver is called.
#include <cstdlib>
#include <iostream>
#include <vector>
#include "rayman2fix/esc_pre_driver_pause_policy.hpp"

using namespace r2fix::pre_driver_pause;
using r2fix::esc_proto::Identity;
#define CHECK(x) do { if(!(x)){std::cerr<<"line "<<__LINE__<<": "<<#x<<"\n";std::abort();} }while(0)

static constexpr Identity kOriginal{123,0x00090005u,0x00090006u,77};
static constexpr Identity kAltInner{123,0x00090005u,0x00090007u,77};
static constexpr Identity kOtherHandle{456,0x00090005u,0x00090006u,77};

void confirmed_pair_has_no_physical_pause_or_resume() {
    Policy p;
    CHECK(!p.begin(false,true,true));
    CHECK(!p.begin(true,false,true));
    CHECK(!p.begin(true,true,false));
    CHECK(p.begin(true,true,true));
    CHECK(!p.begin(true,true,true)); // nested entry blocked
    auto one=p.script_pause(kOriginal,true);
    CHECK(one.suppress_current && !one.replay_held_pauses);
    CHECK(p.held_pauses()==1);
    auto two=p.script_pause(kOriginal,true);
    CHECK(two.suppress_current && !two.replay_held_pauses);
    CHECK(p.held_pauses()==2);
    CHECK(p.state()==State::holding_pair);
    CHECK(!p.end_script_pause().replay_held_pauses);
    CHECK(!p.menu_observation(true,false,true,true,50).replay_held_pauses);
    CHECK(!p.menu_observation(true,true,true,true,55).replay_held_pauses);
    CHECK(p.state()==State::menu_committed);

    // Native focus/unknown calls cannot consume the skipped ESC ledger.
    CHECK(!p.script_resume(kOriginal,false).suppress_current);
    CHECK(!p.script_resume(kOtherHandle,true).suppress_current);
    CHECK(!p.other_native_operation(true).suppress_current);
    CHECK(p.state()==State::retired); // ownership changed: fail closed
}
void paired_script_resumes_are_swallowed_once() {
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    p.end_script_pause();
    p.menu_observation(true,true,true,true,1);
    CHECK(p.state()==State::menu_committed);
    CHECK(p.script_resume(kOriginal,true).suppress_current);
    CHECK(p.suppressed_resumes()==1);
    CHECK(p.script_resume(kOriginal,true).suppress_current);
    CHECK(p.suppressed_resumes()==2);
    CHECK(p.state()==State::retired);
    CHECK(!p.script_resume(kOriginal,true).suppress_current);
    CHECK(!p.begin(true,true,true));
    p.reset();
    CHECK(p.begin(true,true,true)); // new generation, no stale debt
}
void incomplete_pair_is_replayed_in_order() {
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    const auto rollback=p.end_script_pause();
    CHECK(!rollback.suppress_current);
    CHECK(rollback.replay_held_pauses==1);
    CHECK(!rollback.impossible_replay);
    CHECK(p.state()==State::original_passthrough);
    CHECK(!p.script_resume(kOriginal,true).suppress_current);
    CHECK(!p.menu_observation(true,true,true,true,10).replay_held_pauses);
}
void mismatched_second_cannot_be_misclassified_as_same_track() {
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    // Replaying to a recycled handle may pause an unrelated new SXD.
    const auto mismatch=p.script_pause(kAltInner,true);
    CHECK(!mismatch.suppress_current);
    CHECK(mismatch.replay_held_pauses==0);
    CHECK(mismatch.impossible_replay);
    CHECK(p.state()==State::original_passthrough);
    CHECK(!p.script_resume(kAltInner,true).suppress_current);
}
void third_callback_requires_replay_before_current() {
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    const auto third=p.script_pause(kOriginal,true);
    CHECK(!third.suppress_current);
    CHECK(third.replay_held_pauses==2);
    CHECK(!third.impossible_replay);
    CHECK(p.state()==State::original_passthrough);
}
void menu_not_verified_rolls_back_at_deadline_not_completes() {
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    p.end_script_pause();
    CHECK(!p.menu_observation(true,false,true,true,
        Policy::kMenuConfirmationDeadlineMs-1).replay_held_pauses);
    const auto late=p.menu_observation(true,false,true,true,
        Policy::kMenuConfirmationDeadlineMs);
    CHECK(late.replay_held_pauses==2);
    CHECK(!late.impossible_replay);
    CHECK(p.state()==State::original_passthrough);
}
void actor_rejected_or_focus_requires_original_driver() {
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    p.end_script_pause();
    const auto unfocused=p.menu_observation(true,true,false,true,0);
    CHECK(unfocused.replay_held_pauses==2);
    CHECK(p.state()==State::original_passthrough);

    Policy q;
    CHECK(q.begin(true,true,true));
    CHECK(q.script_pause(kOriginal,true).suppress_current);
    CHECK(q.script_pause(kOriginal,true).suppress_current);
    const auto other=q.other_native_operation(true);
    CHECK(other.replay_held_pauses==2);
    CHECK(!other.suppress_current);
    CHECK(q.state()==State::original_passthrough);
}
void moving_track_cannot_receive_stale_rollback_or_resume() {
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    p.end_script_pause();
    const auto changed=p.menu_observation(true,true,true,false,3);
    CHECK(changed.impossible_replay);
    CHECK(changed.replay_held_pauses==0);
    CHECK(!p.script_resume(kAltInner,true).suppress_current);
    CHECK(p.state()==State::original_passthrough);

    Policy q;
    CHECK(q.begin(true,true,true));
    CHECK(q.script_pause(kOriginal,true).suppress_current);
    CHECK(q.script_pause(kOriginal,true).suppress_current);
    q.menu_observation(true,true,true,true,1);
    q.native_stop(kOtherHandle.handle);
    CHECK(q.state()==State::menu_committed);
    q.native_stop(kOriginal.handle);
    CHECK(q.state()==State::retired);
    CHECK(!q.script_resume(kOriginal,true).suppress_current);
}
void verified_9_12_to_9_5_mismatch_needs_no_player_pointer_bypass() {
    // The *native controller* may still reference a different Theme in
    // the same bank (9:12) while the unique active SXD owner is 9:5/9:6.
    // This model only ever considers the ORIGINAL driver callback's own
    // identity. It never changes or calls the Theme controller.
    Policy p;
    CHECK(p.begin(true,true,true));
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    CHECK(p.script_pause(kOriginal,true).suppress_current);
    p.end_script_pause();
    p.menu_observation(true,true,true,true,2);
    CHECK(p.state()==State::menu_committed);
    CHECK(!p.script_resume(kAltInner,true).suppress_current);
    CHECK(p.state()==State::retired);
}
int main() {
    confirmed_pair_has_no_physical_pause_or_resume();
    paired_script_resumes_are_swallowed_once();
    incomplete_pair_is_replayed_in_order();
    mismatched_second_cannot_be_misclassified_as_same_track();
    third_callback_requires_replay_before_current();
    menu_not_verified_rolls_back_at_deadline_not_completes();
    actor_rejected_or_focus_requires_original_driver();
    moving_track_cannot_receive_stale_rollback_or_resume();
    verified_9_12_to_9_5_mismatch_needs_no_player_pointer_bypass();
    std::cout<<"pre-driver ESC model: pair, rollback, provenance, focus, Stop OK\n";
    return 0;
}
