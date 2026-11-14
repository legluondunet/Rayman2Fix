// Synthetic ESC compensation regression tests; no real APM DLL or game code.
#include "rayman2fix/esc_music_wine_prototype.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAILED line "<<__LINE__<<": " #x "\n"; std::abort();}}while(false)
using namespace r2fix::esc_proto;
constexpr Identity kMusic{123,0x00090005,0x00090007,77};
constexpr Identity kNewMusic{123,0x00090005,0x00090007,78};
Coordinator pending() {
    Coordinator x;
    CHECK(x.begin_script_pause(true,true,true));
    x.observe_native_pause(kMusic,true);
    x.observe_native_pause(kMusic,true);
    x.end_script_pause();
    CHECK(x.phase()==Phase::pending_menu && x.native_pause_count()==2);
    return x;
}
void intro_theme_reference_watch_is_bounded_and_read_only() {
    // Reproduction 2026-11-14 14:30: multiple early ESC presses while
    // native current Theme ref=bank 9:12, outer Theme=9:5 and SXD=9:6.
    // A native record's *descriptor* changes without its slot moving.
    // Treat menu toggles as context only, otherwise 24 events are spent
    // before the real 9:6 -> 9:7 transition.
    ThemeRefWatchPolicy watch;
    ThemeRefWatchInput before{
        1,1,0x004a1000,0x004b1000,0x004c1000,0x004d1000,0
    };
    CHECK(watch.observe(before,0));
    CHECK(!watch.observe(before,40));
    auto first_menu=before;
    first_menu.menu_flag=1;
    CHECK(!watch.observe(first_menu,50)); // menu isn't a Theme change
    first_menu.menu_flag=0;
    CHECK(!watch.observe(first_menu,100));
    CHECK(watch.events()==1);

    // In-place outer Theme descriptor replacement, while the nested
    // sound-table pointer and native player ref are unchanged.
    auto changed=first_menu;
    changed.outer_descriptor_ref=0x004c2000;
    CHECK(watch.observe(changed,120));
    CHECK(watch.events()==2);
    auto inner=changed;
    inner.inner_descriptor_ref=0x004d2000;
    CHECK(watch.observe(inner,140)); // SXD 9:6 -> 9:7
    auto state=inner;
    state.controller_field0c=0;
    CHECK(watch.observe(state,160)); // transition state -> settled
    CHECK(watch.events()==4);
    CHECK(!watch.observe(state,161));
    state.menu_flag=1;
    CHECK(!watch.observe(state,165));
    CHECK(!watch.observe(state,ThemeRefWatchPolicy::kWindowMs+1));

    ThemeRefWatchPolicy bounded;
    ThemeRefWatchInput changing{};
    for(unsigned i=0;i<ThemeRefWatchPolicy::kMaxEvents;++i) {
        changing.inner_descriptor_ref=i+1;
        CHECK(bounded.observe(changing,i*40));
    }
    CHECK(bounded.events()==ThemeRefWatchPolicy::kMaxEvents);
    changing.inner_descriptor_ref=999;
    CHECK(!bounded.observe(changing,ThemeRefWatchPolicy::kMaxEvents*40));
}
void strict_gates() {
    Coordinator x;
    CHECK(!x.begin_script_pause(false,true,true));
    CHECK(!x.begin_script_pause(true,false,true));
    CHECK(!x.begin_script_pause(true,true,false));
    CHECK(x.phase()==Phase::idle);
    CHECK(x.begin_script_pause(true,true,true));
    x.observe_native_pause(kMusic,true);
    x.end_script_pause();
    CHECK(x.phase()==Phase::idle); // no compensating call: native passthrough
    CHECK(!x.menu_tick(true,true,true,true));
    CHECK(x.begin_script_pause(true,true,true));
    x.observe_native_pause(kMusic,true);
    x.observe_native_pause(kNewMusic,true); // recycled transport handle
    CHECK(x.phase()==Phase::idle); // reject this cycle, retry next ESC
}
void menu_open_close() {
    auto x=pending();
    CHECK(!x.menu_tick(true,false,true,true)); // audio already paused, menu not up
    CHECK(x.phase()==Phase::pending_menu);
    auto a=x.menu_tick(true,true,true,true);
    CHECK(a.command==Command::resume && a.count==2 && a.handle==123 && a.generation==77);
    CHECK(x.phase()==Phase::audible_menu);
    CHECK(!x.native_resume(123,Origin::script_resume));
    CHECK(!x.native_resume(123,Origin::script_resume));
    x.end_script_resume();
    CHECK(x.phase()==Phase::audible_menu);
    CHECK(!x.menu_tick(true,false,true,true)); // UI flag clears AFTER the resumes
    CHECK(x.phase()==Phase::idle);
    CHECK(x.native_resume(123,Origin::script_resume)); // no stale suppression
}
void focus_during_esc() {
    auto x=pending();
    CHECK(x.menu_tick(true,true,true,true).command==Command::resume);
    auto p=x.focus_pause();
    CHECK(p.command==Command::pause && p.count==2);
    CHECK(!x.native_resume(123,Origin::focus_resume));
    CHECK(x.focus_resume().command==Command::resume);
    CHECK(x.phase()==Phase::audible_menu);
    CHECK(x.focus_pause().command==Command::pause);
    CHECK(!x.native_resume(123,Origin::script_resume));
    CHECK(!x.native_resume(123,Origin::script_resume));
    x.end_script_resume();
    CHECK(x.phase()==Phase::closing_unfocused);
    CHECK(!x.menu_tick(true,false,false,true));
    CHECK(x.focus_resume().command==Command::resume);
    CHECK(x.phase()==Phase::closing_recovered);
    CHECK(x.current(77));
    x.complete_focus_recovery(77);
    CHECK(x.phase()==Phase::idle);
}
void stop_and_unknown() {
    auto x=pending();
    CHECK(x.menu_tick(true,true,true,true).command==Command::resume);
    x.native_stop(124);
    CHECK(x.phase()==Phase::audible_menu);
    x.native_stop(123);
    CHECK(x.phase()==Phase::idle);
    CHECK(!x.current(77));
    auto y=pending();
    CHECK(!y.menu_tick(false,true,true,true));
    CHECK(y.phase()==Phase::idle);
    CHECK(!y.menu_tick(true,true,true,true));
    CHECK(y.native_resume(123,Origin::script_resume)); // no compensation yet: native passthrough
    y.native_stop(123);
    CHECK(y.phase()==Phase::idle);
}
void uncertainty_restores_silence() {
    auto x=pending();
    CHECK(x.menu_tick(true,true,true,true).command==Command::resume);
    x.quarantine();
    const auto pause=x.take_safety_pause();
    CHECK(pause.command==Command::pause && pause.count==2);
    CHECK(x.safety_current(pause.generation));
    CHECK(!x.current(pause.generation)); // no artificial resumes after uncertainty
    CHECK(!x.take_safety_pause()); // only one safety pause batch
    CHECK(x.native_resume(123,Origin::focus_resume));
    // The original game owns its own audio resumption in quarantine:
    // never suppress an unbounded sequence of native driver resumes.
    x.native_stop(123);
    CHECK(x.phase()==Phase::idle);
}
void synthetic_commands_are_bound_to_exact_original_theme_and_sxd() {
    // Owner's 14:43 Wine log: after ESC compensation, the live inner
    // track advanced 9:7 -> 9:8 in the *same* Theme and driver handle.
    // A safety Pause from the previous generation must NOT reach it.
    auto x=pending();
    const auto resume=x.menu_tick(true,true,true,true);
    CHECK(resume.command==Command::resume);
    CHECK(kMusic.same_dispatch(resume));
    const Identity inner_advanced{123,0x00090005,0x00090008,77};
    const Identity other_theme{123,0x0009000c,0x00090007,77};
    CHECK(!inner_advanced.same_dispatch(resume));
    CHECK(!other_theme.same_dispatch(resume));
    CHECK(!x.owner_same(inner_advanced));
    CHECK(!x.owner_same(other_theme));
    CHECK(x.owner_same(kMusic));

    // The first moment the original Theme/SXD provenance changes, the
    // coordinator must stop suppressing or dispatching sound commands.
    CHECK(!x.menu_tick(true,true,true,x.owner_same(inner_advanced)));
    CHECK(x.phase()==Phase::quarantined);
    const auto safety=x.take_safety_pause();
    CHECK(safety.command==Command::pause);
    CHECK(kMusic.same_dispatch(safety));
    CHECK(!inner_advanced.same_dispatch(safety));
    CHECK(!other_theme.same_dispatch(safety));
    CHECK(x.native_resume(123,Origin::script_resume));
    CHECK(!x.focus_resume());
    CHECK(!x.current(77));
    x.end_script_resume();
    CHECK(!x.menu_tick(true,false,true,true));
    CHECK(x.phase()==Phase::idle);

    // A newly started independent ESC never inherits the stale stream.
    CHECK(x.begin_script_pause(true,true,true));
    x.observe_native_pause(inner_advanced,true);
    x.observe_native_pause(inner_advanced,true);
    x.end_script_pause();
    CHECK(x.owner_same(inner_advanced));
    const auto next=x.menu_tick(true,true,true,true);
    CHECK(inner_advanced.same_dispatch(next));
    CHECK(!kMusic.same_dispatch(next));
}
void quarantine_never_permanently_mutes_gameplay() {
    // The owner observed eight consecutive ResumeTransitionSxd calls
    // swallowed in phase=quarantined, after which level music never
    // resumed. This must not recur, even when paired verification fails.
    auto x=pending();
    CHECK(x.menu_tick(true,true,true,true).command==Command::resume);
    CHECK(!x.native_resume(123,Origin::script_resume));
    x.end_script_resume(); // one of two expected resumes missing
    CHECK(x.phase()==Phase::quarantined);
    CHECK(!x.focus_resume()); // no synthetic resume on uncertainty
    CHECK(!x.take_safety_pause()); // script exit must not introduce mute
    for(unsigned i=0;i<8;++i)
        CHECK(x.native_resume(123,Origin::script_resume));
    CHECK(x.native_resume(123,Origin::focus_resume));
    CHECK(!x.current(77)); // no resumed synthetic generation
    CHECK(!x.menu_tick(true,false,true,true));
    CHECK(x.phase()==Phase::idle); // re-armed for next ESC in same level
    CHECK(x.begin_script_pause(true,true,true));
    CHECK(x.phase()==Phase::pending_menu);
    x.reset();
    CHECK(x.native_resume(123,Origin::script_resume));

    auto y=pending();
    CHECK(y.menu_tick(true,true,true,true).command==Command::resume);
    CHECK(y.native_resume(123,Origin::unknown)); // unknown owner passes
    CHECK(y.phase()==Phase::quarantined);
    CHECK(y.native_resume(123,Origin::script_resume));
    CHECK(!y.focus_resume());
    CHECK(!y.menu_tick(true,false,true,true));
    CHECK(y.phase()==Phase::quarantined); // no confirmed script close
    y.end_script_resume();
    CHECK(!y.take_safety_pause());
    CHECK(!y.menu_tick(true,false,true,true));
    CHECK(y.phase()==Phase::idle);
}
void distinct_sfx() {
    auto x=pending();
    CHECK(x.menu_tick(true,true,true,true).command==Command::resume);
    CHECK(x.native_resume(456,Origin::script_resume));
    CHECK(!x.native_resume(123,Origin::script_resume));
    CHECK(x.ignored_resume_count()==1);
    x.end_script_resume();
    CHECK(x.phase()==Phase::quarantined);
}
int main() {
    intro_theme_reference_watch_is_bounded_and_read_only();
    strict_gates();menu_open_close();focus_during_esc();
    stop_and_unknown();uncertainty_restores_silence();
    quarantine_never_permanently_mutes_gameplay();distinct_sfx();
    synthetic_commands_are_bound_to_exact_original_theme_and_sxd();
    std::cout<<"experimental ESC music FSM: strict gates, paired calls, focus, Stop OK\n";
}
