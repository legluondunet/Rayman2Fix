#pragma once
#include <cstdint>
#include "rayman2fix/intro_skip.hpp"

namespace r2fix {

// EXPERIMENT ONLY. The original Learn_10 director has an alternate
// NOMOVIES path, but its gameplay effects are still unvalidated.
// This policy contains no memory access, input injection or file I/O.
// The actual process-memory transition is performed only by an opt-in
// Win32 research build, never by the normal Rayman2Fix runtime.
constexpr unsigned kLearn10NomoviesBit = 1188u;
// Only a short, one-time exposure of the in-memory progress bit is
// allowed. The ELSE route contains its own native JustPressedBut and
// timing condition; a physical key must reach that original input gate.
constexpr std::uint32_t kLearn10NomoviesLatestStartMs = 3000u;
constexpr std::uint32_t kLearn10NomoviesMaxArmedMs = 5000u;
inline bool restore_learn10_nomovies_due(std::uint32_t current_ms,
                                        std::uint32_t armed_ms,
                                        bool director_left_behaviour3) {
    return director_left_behaviour3 ||
           std::uint32_t(current_ms-armed_ms)>=kLearn10NomoviesMaxArmedMs;
}

inline bool may_try_learn10_nomovies(bool opted_in, bool debug_log,
                                    bool first_visit, bool in_behaviour3,
                                    bool approved_script, bool already_set) {
    return opted_in && debug_log && first_visit && in_behaviour3 &&
           approved_script && !already_set;
}
inline bool plan_learn10_nomovies_arm(std::uint32_t word,
                                    std::uint32_t& replacement) {
    const auto mask = story_word_mask(kLearn10NomoviesBit);
    if ((word & mask) != 0u) return false;
    replacement = word | mask;
    return (replacement ^ word) == mask;
}
inline bool plan_learn10_nomovies_restore(std::uint32_t word,
                                        std::uint32_t& replacement) {
    const auto mask = story_word_mask(kLearn10NomoviesBit);
    if ((word & mask) == 0u) return false;
    replacement = word & ~mask;
    return (replacement ^ word) == mask;
}

} // namespace r2fix
