#include "rayman2fix/intro_gate_timeline.hpp"
#include <cstdlib>
#include <iostream>
#include <cstdint>

// Explicitly check in both Debug and Release (assert is disabled by NDEBUG).
#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        std::abort(); \
    } \
} while (false)


int main() {
    r2fix::IntroGateTimeline<4> trace{};
    CHECK(trace.retained()==0 && trace.dropped()==0);
    trace.record(100,50,0,1);
    trace.record(100,110,0,2);
    trace.record(110,50,1,0);
    trace.record(120,110,2,0);
    CHECK(trace.total==4 && trace.retained()==4 && trace.dropped()==0);
    CHECK(trace.chronological(0).variable==50);
    trace.record(200,110,0,1,{1,2000,0,0x03});
    trace.record(210,50,0,1);
    CHECK(trace.total==6 && trace.dropped()==2);
    CHECK(trace.retained()==4 && trace.var110_equal_one==1);
    const auto& oldest=trace.chronological(0);
    const auto& newest=trace.chronological(3);
    CHECK(oldest.elapsed_ms==110 && oldest.variable==50);
    CHECK(trace.chronological(2).phase.v95==2000);
    CHECK(trace.chronological(2).phase.valid==0x03);
    CHECK(trace.chronological(3).phase.valid==0);
    CHECK(newest.elapsed_ms==210 && newest.variable==50);
    trace.record(215,50,1,1); // no change
    CHECK(trace.total==6);
    trace={};
    CHECK(trace.total==0 && trace.retained()==0 && trace.var110_equal_one==0);

    // Independent V96 observer: no inference from native V110/var83
    // changes, and no claim that an unobserved short pulse was absent.
    r2fix::IntroPhase96Timeline<3> signal{};
    CHECK(!signal.initialized && signal.samples==0);
    signal.observe(0,0);
    signal.observe(10,0);
    CHECK(signal.samples==2 && signal.changes==0);
    signal.observe(20,110);
    signal.observe(35,110);
    signal.observe(45,0);
    CHECK(signal.changes==2 && signal.samples110==2);
    CHECK(signal.samples104==0 && signal.entries104==0);
    signal.observe(60,104);
    signal.observe(70,104);
    signal.observe(80,0);
    CHECK(signal.samples104==2 && signal.entries104==1);
    CHECK(signal.first104_ms==60);
    CHECK(signal.changes==4 && signal.retained()==3 && signal.dropped()==1);
    CHECK(signal.chronological(0).elapsed_ms==45);
    CHECK(signal.chronological(1).elapsed_ms==60);
    CHECK(signal.chronological(1).current==104);
    CHECK(signal.chronological(2).elapsed_ms==80);
    signal.observe(90,104);
    CHECK(signal.entries104==2 && signal.first104_ms==60);
    CHECK(signal.chronological(2).current==104);
    signal={};
    CHECK(!signal.initialized && signal.samples==0 &&
          signal.entries104==0 && signal.changes==0);
    signal.observe(5,104); // first valid sample already sees the signal
    CHECK(signal.entries104==1 && signal.first104_ms==5 &&
          signal.changes==0 && signal.samples104==1);
    return 0;
}
