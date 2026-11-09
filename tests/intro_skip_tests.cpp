#include "rayman2fix/intro_skip.hpp"
#include <cassert>

int main() {
    assert(r2fix::is_second_intro("jail_20"));
    assert(r2fix::is_second_intro("Jail_20"));
    assert(!r2fix::is_second_intro("jail_10"));
    assert(!r2fix::is_second_intro("learn_10"));
    assert(!r2fix::is_second_intro("jail_201"));
    assert(!r2fix::is_second_intro(""));
    assert(!r2fix::is_second_intro(nullptr));

    r2fix::IntroSkipGuard guard;
    assert(!guard.update("menu_10", true));
    assert(!guard.update("jail_10", true));
    // If Enter is being held during the second intro load, do not skip.
    assert(!guard.update("jail_20", true));
    assert(!guard.update("jail_20", true));
    assert(!guard.update("jail_20", false));
    assert(guard.update("jail_20", true));
    // A single press can only request one level transition.
    assert(!guard.update("jail_20", true));
    assert(!guard.update("jail_20", false));
    assert(!guard.update("jail_20", true));
    // Re-entering a new game re-arms the feature.
    assert(!guard.update("learn_10", false));
    assert(!guard.update("jail_20", false));
    assert(!guard.update("jail_20", false));
    assert(guard.update("jail_20", true));
    return 0;
}
