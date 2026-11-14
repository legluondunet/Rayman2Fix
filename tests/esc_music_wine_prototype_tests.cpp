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
    CHECK(!x.native_resume(123,Origin::focus_resume)); // cannot wake music
    x.native_stop(123);
    CHECK(x.phase()==Phase::idle);
}
void first_esc_late_owner_verification() {
    // At the first real menu opening, original script pauses already
    // happened and the current-player pointer can still be transitioning.
    // The feature must neither quarantine early nor resume until verified.
    auto x=pending();
    CHECK(x.identity_matches(kMusic));
    CHECK(!x.identity_matches(Identity{}));
    CHECK(!x.identity_matches(Identity{123,0x00090006,0x00090007,77}));
    CHECK(!x.identity_matches(kNewMusic)); // reused handle, new generation
    CHECK(retry_live_theme_identity(x.phase(),true,true,true,true,false,0));
    CHECK(retry_live_theme_identity(x.phase(),true,true,true,true,false,1450));
    CHECK(!retry_live_theme_identity(x.phase(),true,true,true,true,false,
                                    kLiveThemeVerifyWindowMs));
    CHECK(!retry_live_theme_identity(x.phase(),true,false,true,true,false,20));
    CHECK(!retry_live_theme_identity(x.phase(),true,true,false,true,false,20));
    CHECK(!retry_live_theme_identity(x.phase(),true,true,true,false,false,20));
    CHECK(!retry_live_theme_identity(x.phase(),true,true,true,true,true,20));
    // Caller defers policy evaluation: original driver remains paused.
    CHECK(x.phase()==Phase::pending_menu && x.native_pause_count()==2);
    // A provisional current-player mismatch before the native menu
    // appears is not yet actionable and must not quarantine the stream.
    CHECK(!x.menu_tick(true,false,true,false));
    CHECK(x.phase()==Phase::pending_menu);
    // Only after EXACT live identity is re-established may paired
    // native ResumeTransitionSxd be requested.
    CHECK(x.menu_tick(true,true,true,x.identity_matches(kMusic)).command==
          Command::resume);
    CHECK(x.phase()==Phase::audible_menu);
    // If identity never stabilizes by the timeout, fail closed.
    auto y=pending();
    CHECK(!y.identity_matches(Identity{123,0x00090006,0x00090007,77}));
    y.reset();
    CHECK(y.phase()==Phase::idle);
    CHECK(y.native_resume(123,Origin::script_resume));
    auto closed=pending();
    closed.end_script_resume(); // ESC closed before live identity settled
    CHECK(closed.phase()==Phase::idle);
    CHECK(closed.native_resume(123,Origin::script_resume));
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
    strict_gates();menu_open_close();first_esc_late_owner_verification();focus_during_esc();
    stop_and_unknown();uncertainty_restores_silence();distinct_sfx();
    std::cout<<"experimental ESC music FSM: strict gates, paired calls, focus, Stop OK\n";
}
