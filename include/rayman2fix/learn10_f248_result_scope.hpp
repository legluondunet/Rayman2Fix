#pragma once
#include <cstdint>
#include <limits>

namespace r2fix {

// Strict scope policy for the guarded actor-scoped F248 result override.
// The real Rayman 2 interpreter evaluates K1 IfNot -> F248
// GetBooleanInArray(Fix.V42,1188). F248 produces INTEGER type 6 with
// value 0/1. Turning ONLY the ephemeral returned 0 into 1 would make
// the unchanged parent IfNot evaluate FALSE (choose native ELSE).
//
// This header does not install a hook or modify any game memory.
struct Learn10F248Sample {
    std::uintptr_t actor_so=0, expected_director_so=0;
    std::uintptr_t intelligence=0, expected_intelligence=0;
    std::uintptr_t behaviour=0, expected_behaviour_b3=0;
    std::uintptr_t root_node=0, function_node=0;
    std::uint32_t function_id=0, output_type=0, output_value=0;
    std::uint32_t elapsed_ms=0; // diagnostic only; not an authorization deadline
    bool approved_cd=false, skip_cinematics_enabled=false;
    bool exact_script=false, live_learn10=false, first_visit=false;
    bool nomovies_bit_known_off=false, handover_incomplete=false;
    bool verified_replay_director=false; // earned by guarded Jail_20 reset + live B3/suspension
    bool root_is_ifnot=false, function_node_is_type3=false;
    bool function_result_known=false;
};

constexpr std::uint32_t kLearn10F248GetBooleanInArray=248;
constexpr std::uint32_t kLearn10F248FunctionType=3;
constexpr std::uint32_t kLearn10F248IntegerResultType=6;


// Scope of a replay token: only an accepted native Jail_20 transition
// following the verified reset can create it. Null/empty level names are
// permitted only while pending, not after arrival. A second departure
// or unrelated map always cancels the authorization.
struct Learn10ReplayVisit {
    enum class Stage { none, pending, active };
    Stage stage=Stage::none;
    std::uint32_t queued_at=0;
    void queue(std::uint32_t now) noexcept { stage=Stage::pending;queued_at=now; }
    void observe(bool jail20,bool learn10,bool unknown,std::uint32_t now) noexcept {
        if(stage==Stage::pending) {
            if(std::uint32_t(now-queued_at)>10000u)stage=Stage::none;
            else if(learn10)stage=Stage::active;
            else if(!jail20 && !unknown)stage=Stage::none;
        } else if(stage==Stage::active && !learn10)stage=Stage::none;
    }
    bool active() const noexcept { return stage==Stage::active; }
    void clear() noexcept { stage=Stage::none; }
};
// Bit 1091 can become set during replay startup. For a verified replay,
// require readable live Rayman suspension flags and actual release from
// suspension. If no suspended sample was seen, allow a naturally playable
// entry only after the existing bounded director-discovery window.
constexpr bool learn10_replay_handover(bool story_known,bool story_set,
    bool playing,bool ray_known,bool ray_clear,bool saw_suspended,
    std::uint32_t elapsed) noexcept {
    return story_known && story_set && playing && ray_known && ray_clear &&
        (saw_suspended || elapsed>=3000u);
}

// Read-only classification of an individual evaluator call. A positive
// match alone never installs a native function-slot hook: SkipCinematics
// and the effective Learn_10 enable state (hidden debug opt-out) are required.
constexpr bool learn10_f248_enabled_for_ini(
    std::uint32_t skip_cinematics,std::uint32_t skip_learn10) {
    return skip_cinematics == 1u && skip_learn10 == 1u;
}

// Lifetime depends on the verified scene, never the age of an input.
struct Learn10F248Lifetime {
    bool enabled=false, live_learn10=false, director_readable=false;
    bool same_behaviour=false, same_rule_root=false;
    bool story_readable=false, nomovies=false, handed_over=false;
    bool verified_replay_director=false;
};
constexpr const char* learn10_f248_disarm_reason(const Learn10F248Lifetime& s) {
    if (!s.enabled) return "feature-disabled";
    if (!s.live_learn10) return "level-left";
    if (!s.story_readable) return "story-unreadable";
    if (s.handed_over && !s.verified_replay_director) return "story1091-handover";
    if (s.nomovies) return "story1188-changed";
    if (!s.director_readable) return "director-unreadable";
    if (!s.same_behaviour) return "director-left-b3";
    if (!s.same_rule_root) return "script-context-changed";
    return nullptr;
}

inline bool learn10_f248_scope_matches(const Learn10F248Sample& s) {
    if (!s.approved_cd || !s.skip_cinematics_enabled ||
        !s.exact_script || !s.live_learn10 ||
        (!s.first_visit && !s.verified_replay_director) ||
        !s.nomovies_bit_known_off || !s.handover_incomplete ||
        !s.root_is_ifnot || !s.function_node_is_type3 ||
        !s.function_result_known)
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

} // namespace r2fix
