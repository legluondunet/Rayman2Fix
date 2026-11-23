#pragma once
// Experimental, fully fail-closed native VIDEO option-state policy.
// The original PC Menu.sna declares states 2=enabled, 3=active,
// 1=disabled for the three mode entries. Its fourth, save-related
// entry is disabled when no mode change is pending. Keep only the
// separate native brightness entry enabled; NEVER rewrite V17 or the
// executable's disk assets. This file validates copied ScriptNodes.
#include <cstdint>
#include "rayman2fix/native_graphics_static_text_policy.hpp"

namespace r2fix {
struct NativeVideoDisablePlan {
    unsigned script_index;
    unsigned count;
    std::uint64_t original_shape_hash;
    unsigned initialized[3];
    unsigned highlighted[3];
};
// Both normal-5 scripts are validated against the exact owner-supplied
// Menu.sna 8-byte node structure (including node kind/depth/padding).
// Pointer params are deliberately omitted from the SHAPE hash because
// loader relocation makes their values process-dependent. Individual
// relevant operation and literal params must additionally match.
constexpr NativeVideoDisablePlan kVideoDisablePlans[]{
    // Script 1 also initializes the three enabled VIDEO choices.
    // Its terminal display loop has no per-mode 'focused' literal.
    {1u,324u,0x5dd56bd5973fe711ULL,{130u,135u,140u},
                                  {0u,0u,0u}},
    {3u,430u,0xf4cc82cd7d6a21a4ULL,{254u,259u,264u},
                                  {292u,306u,320u}},
    {4u,517u,0xd6fbfd33f17ec60bULL,{214u,219u,224u},
                                  {252u,266u,280u}}
};
inline std::uint64_t native_video_script_shape_hash(
     const NativeMenuScriptNode* nodes,unsigned count) noexcept {
    if(!nodes || count<20u || count>1024u)return 0u;
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned i=0;i<count;++i) {
        const auto& n=nodes[i];
        const std::uint32_t parts[3]{n.padding,n.indent,n.type};
        const unsigned bytes[3]{2u,1u,1u};
        for(unsigned j=0;j<3;++j)
            for(unsigned b=0;b<bytes[j];++b) {
                hash^=(parts[j]>>(8u*b))&255u;
                hash*=1099511628211ULL;
            }
    }
    return hash;
}
constexpr bool native_video_status_assignment(
    const NativeMenuScriptNode* nodes,unsigned count,unsigned operand,
    unsigned row,unsigned native_state) noexcept {
    if(!nodes || row<1u || row>3u || operand<4u || operand>=count)
        return false;
    // Op11 Affect -> Op26 Array -> Var14 -> Int(row) -> Int(value)
    const auto& op=nodes[operand-4u];
    const auto& array=nodes[operand-3u];
    const auto& var=nodes[operand-2u];
    const auto& index=nodes[operand-1u];
    const auto& value=nodes[operand];
    return op.type==2u && op.parameter==11u &&
        array.type==2u && array.parameter==26u &&
        var.type==11u && var.parameter==14u &&
        index.type==12u && index.parameter==row &&
        value.type==12u && value.parameter==native_state &&
        index.padding==0u && value.padding==0u &&
        var.indent==index.indent &&
        value.indent==array.indent && op.indent+1u==array.indent &&
        array.indent+1u==var.indent;
}
inline bool disable_native_video_choices(
    NativeMenuScriptNode* copy,const NativeVideoDisablePlan& p) noexcept {
    if(!copy || p.count<20u || p.count>1024u ||
       native_video_script_shape_hash(copy,p.count)!=p.original_shape_hash ||
       copy[p.count-1u].type!=8u || copy[p.count-1u].indent!=0u)
        return false;
    for(unsigned row=1u;row<=3u;++row)
        if(!native_video_status_assignment(copy,p.count,
             p.initialized[row-1u],row,2u) ||
           (p.highlighted[row-1u]!=0u &&
            !native_video_status_assignment(copy,p.count,
               p.highlighted[row-1u],row,3u)))
            return false;
    // All checks precede any mutation; no partial patched copy on error.
    for(unsigned row=0u;row<3u;++row) {
        copy[p.initialized[row]].parameter=1u;
        if(p.highlighted[row]!=0u)
            copy[p.highlighted[row]].parameter=1u;
    }
    return true;
}
// A separate, approved navigation-range experiment. The original
// Rayman 2 menu scripts set V19=1,V20=5 (five navigable entries) and
// other original MENU scripts use V19=V20=1 for a one-entry menu.
// Here the three former resolution entries are informational-only,
// and only the native brightness choice (index 5) must be reachable.
// Do not change V17 asynchronously from the runtime worker.
struct NativeVideoNavigationPlan {
    unsigned script_index;
    unsigned count;
    std::uint64_t original_shape_hash;
    unsigned first_selectable_literal;
    unsigned last_selectable_literal;
    unsigned current_mode_selection_literals[6];
    unsigned restore_selection_operand; // 0 means none
};
constexpr NativeVideoNavigationPlan kVideoNavigationPlans[]{
    {3u,430u,0xf4cc82cd7d6a21a4ULL,412u,415u,
        {295u,309u,323u,333u,346u,359u},0u},
    {4u,517u,0xd6fbfd33f17ec60bULL,372u,375u,
        {255u,269u,283u,293u,306u,319u},388u}
};
// For a fixed, typed source expression:
//
// Operator_Affect (kind 2, param 11)
//   Variable(V19, V20 or V17; kind 11)
//   Integer(expected; kind 12)
//
inline bool native_video_direct_assignment(
    const NativeMenuScriptNode* nodes,unsigned count,unsigned operand,
    unsigned variable,std::uint32_t expected,unsigned expected_kind=12u)
    noexcept {
    if(!nodes || operand<2u || operand>=count)return false;
    const auto& operation=nodes[operand-2u];
    const auto& destination=nodes[operand-1u];
    const auto& source=nodes[operand];
    return operation.type==2u && operation.parameter==11u &&
        destination.type==11u && destination.parameter==variable &&
        source.type==expected_kind && source.parameter==expected &&
        operation.padding==0u && destination.padding==0u &&
        source.padding==0u &&
        destination.indent==source.indent &&
        operation.indent+1u==destination.indent;
}
inline bool restrict_native_video_navigation(
    NativeMenuScriptNode* copy,const NativeVideoNavigationPlan& plan)
    noexcept {
    if(!copy || plan.count<20u || plan.count>1024u ||
       copy[plan.count-1u].type!=8u ||
       copy[plan.count-1u].indent!=0u ||
       native_video_script_shape_hash(copy,plan.count)!=
          plan.original_shape_hash ||
       !native_video_direct_assignment(copy,plan.count,
          plan.first_selectable_literal,19u,1u) ||
       !native_video_direct_assignment(copy,plan.count,
          plan.last_selectable_literal,20u,5u))
        return false;
    constexpr std::uint32_t sources[6]{1u,2u,3u,3u,2u,1u};
    for(unsigned i=0;i<6u;++i)
        if(!native_video_direct_assignment(copy,plan.count,
             plan.current_mode_selection_literals[i],17u,sources[i]))
            return false;
    if(plan.restore_selection_operand!=0u &&
       !native_video_direct_assignment(copy,plan.count,
           plan.restore_selection_operand,17u,73u,11u))
        return false;
    // Every expected opcode, destination, operand and AST nesting was
    // checked above. Mutate ONLY the private copy's literal indices.
    // V19=V20=5 uses the native one-item range pattern found elsewhere
    // in this approved Menu.sna; leave its brightness procedure intact.
    copy[plan.first_selectable_literal].parameter=5u;
    for(unsigned i=0;i<6u;++i)
        copy[plan.current_mode_selection_literals[i]].parameter=5u;
    if(plan.restore_selection_operand!=0u) {
        auto& restored=copy[plan.restore_selection_operand];
        restored.parameter=5u;
        restored.type=12u; // literal 5 rather than saved V73
    }
    return true;
}
// The native normal-5 script #2 (33 nodes) is the *only*
// brightness-page transition in the owner's original MENU behavior:
// if (V17==5), SetAGO... and Proc_ChangeMyComportAndMyReflex
// (normal=18/reflex=5). It is not a setting we want to expose on the
// new read-only VIDEO information page. Keep the original script and
// butterfly scene actor intact; neutralize that specific *condition*
// only in a private copy, without changing any other script opcode.
constexpr unsigned kNativeBrightnessTransitionScript=2u;
constexpr unsigned kNativeBrightnessTransitionNodes=33u;
constexpr std::uint64_t kNativeBrightnessTransitionShape=
    0x9902892c1e0da3c5ULL;
inline bool suppress_native_video_brightness_transition(
    NativeMenuScriptNode* copy,unsigned node_count,
    std::uintptr_t expected_normal18,std::uintptr_t expected_reflex5)
    noexcept {
    if(!copy || node_count!=kNativeBrightnessTransitionNodes ||
       native_video_script_shape_hash(copy,node_count)!=
         kNativeBrightnessTransitionShape)return false;
    const auto& branch=copy[0];
    const auto& test=copy[1];
    const auto& variable=copy[2];
    const auto& selection=copy[3];
    const auto& transition=copy[29];
    const auto& next_normal=copy[30];
    const auto& next_reflex=copy[31];
    const auto& end=copy[32];
    if(branch.type!=0u || branch.indent!=1u ||
       test.type!=1u || test.parameter!=4u || test.indent!=2u ||
       variable.type!=11u || variable.parameter!=17u ||
       variable.indent!=3u || selection.type!=12u ||
       selection.parameter!=5u || selection.indent!=3u ||
       transition.type!=4u || transition.parameter!=71u ||
       transition.indent!=2u || next_normal.type!=28u ||
       next_normal.indent!=3u ||
       std::uintptr_t(next_normal.parameter)!=expected_normal18 ||
       next_reflex.type!=28u || next_reflex.indent!=3u ||
       std::uintptr_t(next_reflex.parameter)!=expected_reflex5 ||
       end.type!=8u || end.indent!=0u ||
       branch.padding!=0u || test.padding!=0u ||
       variable.padding!=0u || selection.padding!=0u ||
       transition.padding!=0u || next_normal.padding!=0u ||
       next_reflex.padding!=0u || end.padding!=0u)return false;
    // No native menu entry can have the unsigned index 0xFFFFFFFF.
    // The interpreter will not enter the brightness-specific branch.
    // Other animation scripts and the animated butterfly are untouched.
    copy[3].parameter=0xFFFFFFFFu;
    return true;
}
} // namespace r2fix
