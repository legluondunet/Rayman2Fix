#include "rayman2fix/learn10_counter_experiment.hpp"
#include <cassert>

int main() {
    r2fix::Learn10CounterExitInput c{};
    assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.enabled=true;c.foreground=true;c.full_native_shape=true;
    c.director_unique=true;c.director_in_behaviour3=true;
    c.rayman_suspended=true;c.standin_available=true;
    c.story_not_finished=true;c.elapsed_ms=1500;
    c.counter=0;c.total=7;c.pulse=0;c.signal=0;
    assert(r2fix::can_experiment_learn10_counter_exit(c));
    c.counter=5;assert(r2fix::can_experiment_learn10_counter_exit(c));
    c.counter=6;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.counter=-1;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.counter=0;c.total=8;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.total=7;c.elapsed_ms=999;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.elapsed_ms=1500;c.signal=2;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.signal=0;c.pulse=1;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.pulse=0;c.rayman_suspended=false;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.rayman_suspended=true;c.director_unique=false;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.director_unique=true;c.standin_available=false;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.standin_available=true;c.full_native_shape=false;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.full_native_shape=true;c.story_not_finished=false;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.story_not_finished=true;c.foreground=false;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    c.foreground=true;c.enabled=false;assert(!r2fix::can_experiment_learn10_counter_exit(c));
    return 0;
}
