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

    // Learn_10 is a playable level; only a pending *intro* can be skipped.
    assert(r2fix::is_first_playable_level("Learn_10"));
    assert(r2fix::is_first_playable_level("learn_10"));
    assert(!r2fix::is_first_playable_level("jail_20"));
    assert(!r2fix::is_first_playable_level("learn_101"));
    assert(!r2fix::is_first_playable_level(nullptr));
    assert(r2fix::story_word_index(1091) == 34u);
    assert(r2fix::story_word_mask(1091) == 4u);
    assert(r2fix::story_word_index(1188) == 37u);
    assert(r2fix::story_word_mask(1188) == 8u);

    r2fix::LearnIntroGuard learn;
    assert(!learn.update("jail_20", false, true));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, false));
    assert(learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, false));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", false, false));
    assert(!learn.update("learn_10", true, false));
    assert(!learn.update("learn_10", true, false));
    assert(learn.update("learn_10", true, true));
    assert(!learn.update("menu", false, false));
    return 0;
}
