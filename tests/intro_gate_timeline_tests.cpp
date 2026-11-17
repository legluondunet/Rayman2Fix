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
    return 0;
}
