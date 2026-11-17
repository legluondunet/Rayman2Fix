#include "rayman2fix/learn10_nomovies_proto.hpp"
#include <cstdlib>
#include <cstdint>
#include <iostream>

#define CHECK(x) do { if (!(x)) { \
  std::cerr << "CHECK failed: " #x << '\n'; std::abort(); \
} } while (false)

int main() {
    using namespace r2fix;
    CHECK(story_word_index(kLearn10NomoviesBit)==37u);
    CHECK(story_word_mask(kLearn10NomoviesBit)==(1u<<3u));
    std::uint32_t next=0u;
    CHECK(!may_try_learn10_nomovies(false,true,true,true,true,false));
    CHECK(!may_try_learn10_nomovies(true,false,true,true,true,false));
    CHECK(!may_try_learn10_nomovies(true,true,false,true,true,false));
    CHECK(!may_try_learn10_nomovies(true,true,true,false,true,false));
    CHECK(!may_try_learn10_nomovies(true,true,true,true,false,false));
    CHECK(!may_try_learn10_nomovies(true,true,true,true,true,true));
    CHECK(may_try_learn10_nomovies(true,true,true,true,true,false));
    const std::uint32_t original=0xC0FFEEA0u & ~story_word_mask(kLearn10NomoviesBit);
    CHECK(plan_learn10_nomovies_arm(original,next));
    CHECK(next==(original | story_word_mask(kLearn10NomoviesBit)));
    std::uint32_t dummy=0x76543210u;
    CHECK(!plan_learn10_nomovies_arm(next,dummy));
    CHECK(dummy==0x76543210u);
    // Native scripts may legitimately change other progress bits while
    // our temporary flag is set: the restore can only clear bit 1188.
    const auto changed=next | (1u<<30u);
    CHECK(plan_learn10_nomovies_restore(changed,dummy));
    CHECK(dummy==(original | (1u<<30u)));
    CHECK((changed ^ dummy)==story_word_mask(kLearn10NomoviesBit));
    CHECK(!plan_learn10_nomovies_restore(dummy,next));
    // Never mutate even one bit in a foreign (already NOMOVIES-enabled)
    // progress array, and no one-shot skip is implied by this policy.
    CHECK(!plan_learn10_nomovies_arm(0xFFFFFFFFu,next));
    // Original ELSE still has an independent native JustPressedBut and
    // one-second time guard. Bound exposure for an explicit physical press.
    CHECK(!restore_learn10_nomovies_due(1000u,1000u,false));
    CHECK(!restore_learn10_nomovies_due(5999u,1000u,false));
    CHECK( restore_learn10_nomovies_due(6000u,1000u,false));
    CHECK( restore_learn10_nomovies_due(1001u,1000u,true));
    // Unsigned subtraction also handles the 32-bit GetTickCount rollover.
    CHECK(!restore_learn10_nomovies_due(1000u,0xfffffff0u,false));
    CHECK( restore_learn10_nomovies_due(6000u,0xfffffff0u,false));
    CHECK(kLearn10NomoviesLatestStartMs==3000u);
    CHECK(kLearn10NomoviesMaxArmedMs==5000u);
    return 0;
}
