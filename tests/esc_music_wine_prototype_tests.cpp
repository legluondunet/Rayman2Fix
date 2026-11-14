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
    for(unsigned i=0;i<8;++i)
        CHECK(x.native_resume(123,Origin::script_resume));
    CHECK(x.native_resume(123,Origin::focus_resume));
    CHECK(!x.current(77)); // no resumed synthetic generation
    x.native_stop(123);
    CHECK(x.phase()==Phase::idle);
    CHECK(x.native_resume(123,Origin::script_resume));

    auto y=pending();
    CHECK(y.menu_tick(true,true,true,true).command==Command::resume);
    CHECK(y.native_resume(123,Origin::unknown)); // unknown owner passes
    CHECK(y.phase()==Phase::quarantined);
    CHECK(y.native_resume(123,Origin::script_resume));
    CHECK(!y.focus_resume());
    y.native_stop(123);
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
    strict_gates();menu_open_close();focus_during_esc();
    stop_and_unknown();uncertainty_restores_silence();
    quarantine_never_permanently_mutes_gameplay();distinct_sfx();
    std::cout<<"experimental ESC music FSM: strict gates, paired calls, focus, Stop OK\n";
}
