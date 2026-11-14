#include "rayman2fix/intro_gate_timeline.hpp"
#include <cassert>
#include <cstdint>

int main() {
    r2fix::IntroGateTimeline<4> trace{};
    assert(trace.retained()==0 && trace.dropped()==0);
    trace.record(100,50,0,1);
    trace.record(100,110,0,2);
    trace.record(110,50,1,0);
    trace.record(120,110,2,0);
    assert(trace.total==4 && trace.retained()==4 && trace.dropped()==0);
    assert(trace.chronological(0).variable==50);
    trace.record(200,110,0,1,{1,2000,0,0x03});
    trace.record(210,50,0,1);
    assert(trace.total==6 && trace.dropped()==2);
    assert(trace.retained()==4 && trace.var110_equal_one==1);
    const auto& oldest=trace.chronological(0);
    const auto& newest=trace.chronological(3);
    assert(oldest.elapsed_ms==110 && oldest.variable==50);
    assert(trace.chronological(2).phase.v95==2000);
    assert(trace.chronological(2).phase.valid==0x03);
    assert(trace.chronological(3).phase.valid==0);
    assert(newest.elapsed_ms==210 && newest.variable==50);
    trace.record(215,50,1,1); // no change
    assert(trace.total==6);
    trace={};
    assert(trace.total==0 && trace.retained()==0 && trace.var110_equal_one==0);
    return 0;
}
