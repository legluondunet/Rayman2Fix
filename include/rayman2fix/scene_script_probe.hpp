#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace r2fix {

// A minimal, read-only shape reader for Rayman 2's 8-byte script nodes.
// Each script's first word points to its array of nodes. No game asset
// content, pointer values or executable code is emitted by this helper.
struct SceneScriptEvent {
    std::uint32_t node = 0;
    std::uint32_t param = 0; // used only for sanitized opcode formatting
    std::uint8_t indent = 0;
    std::uint8_t type = 0;
};
struct SceneScriptProbe {
    bool complete = false;
    bool malformed = false;
    bool truncated = false;
    unsigned visited = 0;
    unsigned selected = 0;
    // The first 128 events preserve older diagnostics. For long scripts
    // keep bounded ring buffers of the final events and all procedure/
    // meta-action/behaviour-reference sites, without following pointers.
    unsigned total_selected = 0;
    unsigned tail_selected = 0;
    unsigned critical_total = 0;
    unsigned critical_selected = 0;
    unsigned context_selected = 0;
    // First exact comparison of integer variable 96 to literal 110.
    // These are bounds-checked script node indices, never memory pointers.
    unsigned phase110_predicates = 0;
    unsigned phase110_first_node = 0;
    unsigned phase110_first_condition = 0;
    std::array<SceneScriptEvent, 128> events{};
    // An optional bounded context window around the native exit branch.
    // No script parameter is dereferenced or returned as a process address.
    std::array<SceneScriptEvent, 256> context{};
    std::array<SceneScriptEvent, 96> tail{};
    std::array<SceneScriptEvent, 64> critical{};
};
inline bool interesting_scene_opcode(std::uint8_t t) {
    switch(t) {
        case 0: case 1: case 2: case 3: case 4: case 5:
        case 9: case 11: case 12: case 17: case 23: case 28:
        case 39: return true;
        default: return false;
    }
}
// This intentionally never follows pointer-typed node parameters.
// It reads only the script's own bounds-checked node array.
template<class Read32, class Read8>
SceneScriptProbe observe_scene_script(std::uintptr_t script_slot,
                                     Read32 rd32, Read8 rd8,
                                     unsigned max_nodes=1024,
                                     unsigned context_first=0,
                                     unsigned context_last=0) {
    SceneScriptProbe p{};
    std::uint32_t nodes=0;
    if (!script_slot || !rd32(script_slot,nodes) || nodes<0x10000 ||
        max_nodes==0 || max_nodes>8192)
        return p;
    if (std::uintptr_t(nodes)>std::uintptr_t(0xFFFFFFFFu)-max_nodes*8u) {
        p.malformed=true;
        return p;
    }
    SceneScriptEvent previous{}, before_previous{};
    unsigned recent=0;
    for (unsigned i=0;i<max_nodes;++i) {
        const auto at=std::uintptr_t(nodes)+std::uintptr_t(i)*8u;
        std::uint32_t param=0;
        std::uint8_t indent=0,type=0;
        if (!rd32(at,param) || !rd8(at+6,indent) || !rd8(at+7,type))
            return p;
        if (type>40 || indent>63 || (i==0 && indent!=1)) {
            p.malformed=true;
            return p;
        }
        p.visited=i+1;
        if (indent==0) {
            p.complete=(type==8);
            p.malformed=!p.complete;
            return p;
        }
        // A tightly scoped read-only locator: Condition(4..7)
        //  variable(96), int(110), all at the expected expression
        //  nesting depth. A matching expression is NOT proof of a
        //  skippable cinematic; it just identifies relevant script data.
        if (recent>=2 && type==12 && param==110 &&
            previous.type==11 && previous.param==96 &&
            previous.indent==indent &&
            before_previous.type==1 &&
            before_previous.param>=4 && before_previous.param<=7 &&
            unsigned(before_previous.indent)+1==unsigned(indent)) {
            ++p.phase110_predicates;
            if (p.phase110_predicates==1) {
                p.phase110_first_node=before_previous.node;
                p.phase110_first_condition=before_previous.param;
            }
        }
        before_previous=previous;
        previous={i,param,indent,type};
        if (recent<2) ++recent;
        if (interesting_scene_opcode(type)) {
            const SceneScriptEvent event{i,param,indent,type};
            if (p.selected<p.events.size())
                p.events[p.selected++]=event;
            else
                p.truncated=true;
            if (context_last>context_first &&
                i>=context_first && i<=context_last &&
                p.context_selected<p.context.size()) {
                p.context[p.context_selected++]=event;
            }
            p.tail[p.total_selected % p.tail.size()]=event;
            ++p.total_selected;
            if (p.tail_selected<p.tail.size()) ++p.tail_selected;
            if (type==4 || type==5 || type==28) {
                p.critical[p.critical_total % p.critical.size()]=event;
                ++p.critical_total;
                if (p.critical_selected<p.critical.size())
                    ++p.critical_selected;
            }
        }
    }
    p.truncated=true;
    return p;
}

// The first rule of director behaviour 3 checks the original NOMOVIES
// cheat bit: IfNot(GetBooleanInArray(actor.V42, 1188)) Then ...
// This is a fingerprint of SCRIPT DATA, not a write authorization.
struct Learn10NoMoviesGateShape {
    bool ifnot=false, get_boolean=false, progress_array=false;
    bool cheat_bit=false, then_branch=false;
    bool matched() const {
        return ifnot && get_boolean && progress_array &&
               cheat_bit && then_branch;
    }
};
inline Learn10NoMoviesGateShape inspect_learn10_nomovies_gate(
    const SceneScriptProbe& p) {
    Learn10NoMoviesGateShape shape{};
    if (!p.complete || p.malformed || p.selected<7) return shape;
    const auto& e=p.events;
    for(unsigned i=0;i<7;++i)
        if(e[i].node!=i) return shape;
    const auto has=[&](unsigned i,unsigned depth,unsigned type,
                       std::uint32_t v) {
        return e[i].indent==depth && e[i].type==type && e[i].param==v;
    };
    shape.ifnot=has(0,1,0,1);        // IfNot
    shape.get_boolean=has(1,2,3,248); // GetBooleanInArray
    shape.progress_array=has(2,3,2,12) &&
        e[3].indent==4 && e[3].type==23 && // actor pointer not read
        has(4,4,11,42);             // actor's progression array
    shape.cheat_bit=has(5,3,12,1188);
    shape.then_branch=has(6,1,0,8);
    return shape;
}

// This is a second arm of the ORIGINAL first rule: at the top-level
// Else when the NOMOVIES flag is set, the director tests V119 and
// can run an alternate initialization / behaviour selection path.
// IMPORTANT: it is NOT proof that the silver Lum scene auto-skips.
struct Learn10NoMoviesAlternativeShape {
    bool top_level_else=false;
    bool mode_condition=false;
    bool continuation=false;
    bool native_behaviour_change=false;
    bool matched() const {
        return top_level_else && mode_condition && continuation &&
               native_behaviour_change;
    }
};
inline Learn10NoMoviesAlternativeShape inspect_learn10_nomovies_alternative(
    const SceneScriptProbe& p) {
    Learn10NoMoviesAlternativeShape result{};
    if (!p.complete || p.malformed || !p.context_selected) return result;
    const auto has=[&](unsigned node,unsigned depth,unsigned type,
                       std::uint32_t value) {
        for(unsigned i=0;i<p.context_selected;++i) {
            const auto& n=p.context[i];
            if(n.node==node && n.indent==depth &&
               n.type==type && n.param==value) return true;
        }
        return false;
    };
    const auto has_pointer_type=[&](unsigned node,unsigned depth,
                                    unsigned type) {
        for(unsigned i=0;i<p.context_selected;++i) {
            const auto& n=p.context[i];
            if(n.node==node && n.indent==depth && n.type==type)
                return true; // never compare or dereference pointer value
        }
        return false;
    };
    result.top_level_else=has(100,1,0,9);
    result.mode_condition=has(101,2,0,1) &&
                          has(102,3,11,119);
    result.continuation=has(103,2,0,8);
    result.native_behaviour_change=
        has(263,4,4,69) && has_pointer_type(264,5,28);
    return result;
}

// An early native exit case exists at v96==104, BEFORE the six normal
// v96==110 progression pulses. This is only a script fingerprint.
// It does not authorize writing designer variables or invoking AI code.
struct Learn10EarlyExitShape {
    bool phase104_branch=false;
    bool exit_signal=false;
    bool actor_reactivation=false;
    bool rayman_restoration=false;
    bool control_restoration=false;
    bool matched() const {
        return phase104_branch && exit_signal && actor_reactivation &&
               rayman_restoration && control_restoration;
    }
};
inline Learn10EarlyExitShape inspect_learn10_early_exit(
    const SceneScriptProbe& probe) {
    Learn10EarlyExitShape shape{};
    if (!probe.complete || probe.malformed || probe.context_selected==0)
        return shape;
    const auto has=[&](unsigned node,unsigned depth,unsigned type,
                       std::uint32_t value) {
        for(unsigned i=0;i<probe.context_selected;++i) {
            const auto& e=probe.context[i];
            if(e.node==node && e.indent==depth &&
               e.type==type && e.param==value) return true;
        }
        return false;
    };
    // Nested Else/If(v96==104)/Then/Else in the exact compiled rule.
    shape.phase104_branch=
        has(156,8,0,9) && has(157,9,0,0) &&
        has(158,10,1,4) && has(159,11,11,96) &&
        has(160,11,12,104) && has(161,9,0,8) &&
        has(249,9,0,9);
    // The early branch signals var110=1 and clears the active effect.
    shape.exit_signal=
        has(172,10,2,11) && has(173,11,11,110) &&
        has(174,11,12,1) && has(175,10,4,139);
    shape.actor_reactivation=
        has(217,10,4,84) && has(219,10,4,67) &&
        has(222,10,4,68) && has(237,11,4,318) &&
        has(241,11,4,312);
    shape.rayman_restoration=
        has(227,11,4,174) && has(228,12,12,14) &&
        has(229,12,12,0) && has(232,11,4,174) &&
        has(233,12,12,17) && has(234,12,12,0);
    shape.control_restoration=
        has(244,10,2,11) && has(245,11,2,12) &&
        has(247,12,11,16) && has(248,11,12,1);
    return shape;
}

// Read-only fingerprint of the exact native Learn_10 script from the approved
// executable. OpCode *order and nesting* matter; a coincidental list of
// instructions is NOT a matching exit path. Never use a match to enable a skip.
struct Learn10NativeExitShape {
    bool exit_gate = false;
    bool counter_branch = false;
    bool terminal_signal = false;
    bool actor_reactivation = false;
    bool rayman_restoration = false;
    bool native_retirement = false;

    bool matched() const {
        return exit_gate && counter_branch && terminal_signal &&
               actor_reactivation && rayman_restoration && native_retirement;
    }
};
inline Learn10NativeExitShape inspect_learn10_native_exit(
    const SceneScriptProbe& probe, std::uintptr_t behaviour_table,
    unsigned behaviour_count) {
    Learn10NativeExitShape shape{};
    if (!probe.complete || probe.malformed || probe.context_selected==0 ||
        behaviour_table < 0x10000u || behaviour_count<6 ||
        behaviour_count>64 ||
        behaviour_table > std::uintptr_t(0xFFFFFFFFu) -
                          std::uintptr_t(behaviour_count)*12u)
        return shape;
    const auto has = [&](std::uint32_t node, std::uint8_t depth,
                         std::uint8_t type, std::uint32_t value) {
        for (unsigned i=0; i<probe.context_selected; ++i) {
            const auto& e=probe.context[i];
            if (e.node==node && e.indent==depth &&
                e.type==type && e.param==value)
                return true;
        }
        return false;
    };
    // C4 is Equal, C6 is Lesser; O1 subtracts, O5 increments and
    // O11 assigns; K0 / K8 / K9 mean If / Then / Else respectively.
    // The original IF(var110==1) moves the director into behaviour 1.
    // Its ELSE branch contains the terminal counter and actor restoration.
    shape.exit_gate =
        has(926,3,0,0) && has(927,4,1,4) &&
        has(928,5,11,110) && has(929,5,12,1) &&
        has(930,3,0,8) && has(940,2,0,9);
    shape.counter_branch =
        has(956,3,0,0) && has(957,4,1,6) &&
        has(958,5,11,83) && has(959,5,2,1) &&
        has(960,6,11,14) && has(961,6,12,1) &&
        has(962,3,0,8) && has(963,4,2,5) &&
        has(964,5,11,83) && has(965,5,12,1);
    shape.terminal_signal =
        has(966,3,0,9) && has(967,4,2,11) &&
        has(968,5,11,110) && has(969,5,12,1) &&
        has(970,4,4,139);
    shape.actor_reactivation =
        has(1022,4,4,84) && has(1028,5,4,312) &&
        has(1031,4,4,67) && has(1034,4,4,68) &&
        has(1049,5,4,318);
    shape.rayman_restoration =
        has(1039,5,4,174) && has(1040,6,12,14) &&
        has(1041,6,12,0) && has(1044,5,4,174) &&
        has(1045,6,12,17) && has(1046,6,12,0);
    // The behaviour-reference operand must target index 1 of this
    // *same verified model*, not merely be an arbitrary node type 28.
    shape.native_retirement =
        has(933,5,4,174) && has(934,6,12,30) &&
        has(935,6,12,0) && has(938,4,4,69) &&
        has(939,5,28,std::uint32_t(behaviour_table+12u));
    return shape;
}

} // namespace r2fix
