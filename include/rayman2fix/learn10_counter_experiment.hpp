#pragma once
#include <cstdint>

namespace r2fix {

// Experimental-only native counter exit. A valid plan DOES NOT prove
// gameplay safety: it merely avoids the demonstrably broken direct 3->1
// director transition. The engine must still execute its own exit script.
struct Learn10CounterExitInput {
    bool enabled=false;
    bool foreground=false;
    bool full_native_shape=false;
    bool director_unique=false;
    bool director_in_behaviour3=false;
    bool rayman_suspended=false;
    bool standin_available=false;
    bool story_not_finished=false;
    std::uint32_t elapsed_ms=0;
    std::int32_t counter=0;
    std::int32_t total=0;
    std::int32_t pulse=0;
    std::int32_t signal=0;
};
inline bool can_experiment_learn10_counter_exit(
    const Learn10CounterExitInput& c) {
    return c.enabled && c.foreground && c.full_native_shape &&
           c.director_unique && c.director_in_behaviour3 &&
           c.rayman_suspended && c.standin_available &&
           c.story_not_finished && c.elapsed_ms>=1000 &&
           c.total==7 && c.counter>=0 && c.counter<c.total-1 &&
           c.pulse==0 && c.signal==0;
}
} // namespace r2fix
