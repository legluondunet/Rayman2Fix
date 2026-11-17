#include "rayman2fix/intro_native_signal.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr << " (" << __LINE__ << ")\\n"; \
        std::abort(); \
    } \
} while(false)

int main() {
    using r2fix::IntroNativeSignalKind;
    r2fix::IntroNativeSignalTimeline<4> t{};
    t.observe(0,false,0,false);
    t.observe(100,true,0,false);
    t.observe(110,true,0,true);
    t.observe(120,true,0,true);
    t.observe(130,true,0,false);
    t.observe(140,true,110,false);
    t.observe(150,true,0,false);
    t.observe(160,false,104,false); // read failure is NOT signal 104
    t.observe(170,true,104,false);
    t.observe(180,true,104,true);
    t.observe(190,true,104,true);
    t.observe(200,true,0,false);
    CHECK(t.signal_known && t.initial_signal==0 && t.last_signal==0);
    CHECK(t.enter_presses==2 && t.enters_110==1 && t.enters_104==1);
    CHECK(t.signal_changes==4 && t.invalid_samples==2);
    CHECK(t.total==6 && t.retained()==4 && t.dropped()==2);
    CHECK(t.chronological(0).kind==IntroNativeSignalKind::signal_change);
    CHECK(t.chronological(0).signal==0);
    CHECK(t.chronological(1).signal==104);
    CHECK(t.chronological(2).kind==IntroNativeSignalKind::enter_press);
    CHECK(t.chronological(3).signal==0);
    t={};
    CHECK(t.total==0 && !t.signal_known && !t.key_known);
    t.observe(0,true,104,true);
    CHECK(t.initial_signal==104 && t.enters_104==0 &&
          t.enter_presses==0 && t.total==0);
    t.observe(1,true,0,false);
    t.observe(2,true,104,true);
    CHECK(t.enters_104==1 && t.enter_presses==1);
    CHECK(t.chronological(0).signal==0);
    CHECK(t.chronological(1).kind==IntroNativeSignalKind::enter_press);
    CHECK(t.chronological(2).signal==104);
    return 0;
}
