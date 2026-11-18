#pragma once
#include <cstdint>

namespace r2fix {

// Independent research candidate: invert ONLY the verified first keyword
// of Learn_10 director behaviour-3 rule 1. Does NOT alter story bit 1188.
// The original rule is: IfNot(GetBooleanInArray(Fix.V42, 1188))
//   THEN normal cinematic ELSE original NOMOVIES input/time branch.
//
// Verified keyword IDs in raytools/ACP_Ray2 src/AI/AI_Const.h:
//   AI_E_kw_If=0, AI_E_kw_IfNot=1, AI_E_kw_Then=8, AI_E_kw_Else=9.
// This header ONLY computes expected atomic writes; NO memory access.
constexpr std::uint32_t kLearn10ScriptIf = 0u;
constexpr std::uint32_t kLearn10ScriptIfNot = 1u;
constexpr std::uint8_t kLearn10ScriptKeywordType = 0u;
constexpr std::uint8_t kLearn10ScriptRootDepth = 1u;
constexpr std::uint32_t kLearn10ScriptLatestStartMs = 3000u;
constexpr std::uint32_t kLearn10ScriptMaxArmedMs = 5000u;

inline bool may_try_learn10_script_gate(
    bool opted_in, bool logging, bool first_visit, bool director_b3,
    bool native_rules_verified, bool original_nomovies_disabled) {
    return opted_in && logging && first_visit && director_b3 &&
           native_rules_verified && original_nomovies_disabled;
}

// Refuse ANY non-original/root keyword; no generic IF/ELSE rewriting.
inline bool plan_learn10_script_gate_arm(
    std::uint32_t first_word, std::uint8_t depth, std::uint8_t type,
    std::uint32_t& replacement) {
    if (depth != kLearn10ScriptRootDepth ||
        type != kLearn10ScriptKeywordType ||
        first_word != kLearn10ScriptIfNot) return false;
    replacement = kLearn10ScriptIf;
    return true;
}

// Restore ONLY a node that is exactly our temporary IF=0. A modified
// or foreign node cannot be overwritten; leave it untouched and report.
inline bool plan_learn10_script_gate_restore(
    std::uint32_t first_word, std::uint8_t depth, std::uint8_t type,
    std::uint32_t& replacement) {
    if (depth != kLearn10ScriptRootDepth ||
        type != kLearn10ScriptKeywordType ||
        first_word != kLearn10ScriptIf) return false;
    replacement = kLearn10ScriptIfNot;
    return true;
}

inline bool restore_learn10_script_gate_due(
    std::uint32_t now_ms, std::uint32_t armed_ms, bool left_b3) {
    return left_b3 ||
           (std::uint32_t(now_ms - armed_ms) >= kLearn10ScriptMaxArmedMs);
}

} // namespace r2fix
