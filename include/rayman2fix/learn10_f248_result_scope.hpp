#pragma once
#include <cstdint>
#include <limits>

namespace r2fix {

// Pure research policy for a possible actor-scoped F248 result override.
// The real Rayman 2 interpreter evaluates K1 IfNot -> F248
// GetBooleanInArray(Fix.V42,1188). F248 produces INTEGER type 6 with
// value 0/1. Turning ONLY the ephemeral returned 0 into 1 would make
// the unchanged parent IfNot evaluate FALSE (choose native ELSE).
//
// This header does NOT install a hook or modify any game memory.
struct Learn10F248Sample {
    std::uintptr_t actor_so=0, expected_director_so=0;
    std::uintptr_t intelligence=0, expected_intelligence=0;
    std::uintptr_t behaviour=0, expected_behaviour_b3=0;
    std::uintptr_t root_node=0, function_node=0;
    std::uint32_t function_id=0, output_type=0, output_value=0;
    std::uint32_t elapsed_ms=0;
    bool approved_cd=false, explicitly_opted_in=false, logging=false;
    bool exact_script=false, live_learn10=false, first_visit=false;
    bool nomovies_bit_known_off=false, handover_incomplete=false;
    bool root_is_ifnot=false, function_node_is_type3=false;
    bool function_result_known=false;
};

constexpr std::uint32_t kLearn10F248GetBooleanInArray=248;
constexpr std::uint32_t kLearn10F248FunctionType=3;
constexpr std::uint32_t kLearn10F248IntegerResultType=6;
constexpr std::uint32_t kLearn10F248MaxElapsedMs=5000;

// Read-only classification of an *individual* evaluator call.
// Even a positive result must NOT be used as permission to hook.
// No condition node, script, handler table or story-bit writes.
inline bool research_learn10_f248_scope_matches(const Learn10F248Sample& s) {
    if (!s.approved_cd || !s.explicitly_opted_in || !s.logging ||
        !s.exact_script || !s.live_learn10 || !s.first_visit ||
        !s.nomovies_bit_known_off || !s.handover_incomplete ||
        !s.root_is_ifnot || !s.function_node_is_type3 ||
        !s.function_result_known || s.elapsed_ms>kLearn10F248MaxElapsedMs)
        return false;
    if (!s.actor_so || s.actor_so!=s.expected_director_so ||
        !s.intelligence || s.intelligence!=s.expected_intelligence ||
        !s.behaviour || s.behaviour!=s.expected_behaviour_b3 ||
        !s.root_node || s.root_node>std::numeric_limits<std::uintptr_t>::max()-8 ||
        s.function_node!=s.root_node+8)
        return false;
    return s.function_id==kLearn10F248GetBooleanInArray &&
           s.output_type==kLearn10F248IntegerResultType &&
           s.output_value==0u;
}

// The candidate is available ONLY in a separately built research DLL;
// the normal build and standalone model tests always keep the hook disabled.
#if defined(RAYMAN2FIX_LEARN10_F248_PROTO) && RAYMAN2FIX_LEARN10_F248_PROTO
constexpr bool kLearn10F248RuntimeHookAllowed=true;
#else
constexpr bool kLearn10F248RuntimeHookAllowed=false;
#endif

} // namespace r2fix
