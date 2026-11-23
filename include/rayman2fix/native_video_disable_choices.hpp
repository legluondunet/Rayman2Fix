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
    // Normal-5 script #1 initializes the PARENT Options menu.
    // Its V14[0..2] are VIDEO, SOUND and OTHER OPTIONS, not resolution
    // choices. Never disable them: that hid Options items on 23 Nov.
    if(p.script_index==1u)return false;
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
// A standalone VIDEO page must not construct the original choice
// widgets. The caller authenticated the full, original 430/517-node
// script shapes and native dispatch before touching its private copy.
// This fails closed if the script prefix differs from the CD resource.
inline bool standalone_remove_legacy_video_widgets(
    NativeMenuScriptNode* copy,unsigned script,unsigned node_count) noexcept {
    if(!copy || (script!=3u && script!=4u) ||
       node_count!=(script==3u?430u:517u) ||
       copy[node_count-1u].type!=8u ||
       copy[node_count-1u].indent!=0u)return false;
    if(script==3u) {
        // Source script-3 both creates selection widgets and can call
        // the now protected resolution functions. A single END at its
        // entry makes that behavior inert.
        if(copy[0].type!=0u || copy[0].indent!=1u ||
           copy[1].type!=1u || copy[1].parameter!=45u ||
           copy[16].type!=4u || copy[16].parameter!=506u)
            return false;
        copy[0]={0u,0u,0u,8u};
        return true;
    }
    // Preserve the first two native SetAGO scene-position operations,
    // which may own the animated menu butterfly. Stop before ANY
    // V11 text-handle assignments and selection-status widget creation.
    if(copy[0].type!=0u || copy[0].parameter!=4u ||
       copy[1].type!=12u || copy[1].parameter!=1u ||
       copy[3].type!=4u || copy[3].parameter!=503u ||
       copy[15].type!=4u || copy[15].parameter!=503u ||
       copy[26].type!=12u || copy[26].parameter!=21u ||
       copy[27].type!=2u || copy[27].parameter!=11u)
        return false;
    copy[27]={0u,0u,0u,8u};
    return true;
}

// The approved PC Menu.sna script 3/4 updates V14[4] when V51
// (native Enter/confirm latch) is set: 1=disabled, 2=enabled. This is
// the old SAVE RESOLUTION widget, now reused by the read-only info
// panel. Re-enabling it for one frame on Enter can restart its visual
// animation even though all actual video/save actions are guarded.
// Keep BOTH assignment branches in disabled state=1, without changing
// V51, the keyboard, the parent Options script or ESC navigation.
// Source nodes: normal-5 script3 366..379, script4 326..339;
// verified from the owner's exact 638463-byte Menu.sna.
inline bool neutralize_native_video_enter_save_status(
    NativeMenuScriptNode* copy,const NativeVideoDisablePlan& plan)
    noexcept {
    const unsigned enabled=plan.script_index==3u ? 373u :
                           plan.script_index==4u ? 333u : 0u;
    if(!copy || !enabled || enabled+6u>=plan.count ||
       plan.count<20u || plan.count>1024u ||
       native_video_script_shape_hash(copy,plan.count)!=
           plan.original_shape_hash)return false;
    const auto branch=copy[enabled-7u];
    const auto pressed=copy[enabled-6u];
    const auto active_branch=copy[enabled-5u];
    const auto idle_branch=copy[enabled+1u];
    const auto validate_assignment=[&](unsigned value,unsigned status) {
        const auto& op=copy[value-4u];
        const auto& array=copy[value-3u];
        const auto& var=copy[value-2u];
        const auto& row=copy[value-1u];
        const auto& state=copy[value];
        return op.type==2u && op.parameter==11u &&
            array.type==2u && array.parameter==26u &&
            var.type==11u && var.parameter==14u &&
            row.type==12u && row.parameter==4u &&
            state.type==12u && state.parameter==status &&
            op.padding==0u && array.padding==0u &&
            var.padding==0u && row.padding==0u &&
            state.padding==0u &&
            op.indent+1u==array.indent &&
            array.indent+1u==var.indent &&
            row.indent==var.indent &&
            state.indent==array.indent;
    };
    if(branch.type!=0u || branch.parameter!=0u ||
       pressed.type!=11u || pressed.parameter!=51u ||
       active_branch.type!=0u || active_branch.parameter!=8u ||
       idle_branch.type!=0u || idle_branch.parameter!=9u ||
       !validate_assignment(enabled,2u) ||
       !validate_assignment(enabled+6u,1u))return false;
    // Only the typed native UI state for the old save row changes.
    copy[enabled].parameter=1u;
    return true;
}

// The original normal-5 scripts 1 and 4 end in a JFFTXT call whose
// focus argument is live V17. Even after VIDEO is made read-only the
// native widget still performs a one-shot focus animation on Enter.
// The 10:18 log confirms no native VIDEO state/mode transition on Enter.
// For the approved, shape-verified script copies only, keep the
// existing font call but select its inert fixed index 0 instead of V17.
// Do not modify keyboard input, title drawing or the butterfly actor.
// This is a *separate* safety gate: validate BEFORE V14/V19/V20 patches
// because script 4's V73 replacement changes its node type.
inline bool native_video_static_font_preflight(
    const NativeMenuScriptNode* copy,
    const NativeVideoDisablePlan& choices,
    const NativeInfoStaticTextPlan& font) noexcept {
    // Only script 4 belongs to the live VIDEO selection widget.
    // Script 1 is the parent Options initialization and must retain
    // its original focus-animation wiring and menu selection.
    return copy && choices.script_index==4u &&
        choices.script_index==font.script_index &&
        choices.count==font.node_count &&
        native_video_script_shape_hash(copy,choices.count)==
            choices.original_shape_hash &&
        native_video_font_tail_valid(copy,font);
}
inline bool freeze_native_video_info_font(
    NativeMenuScriptNode* copy,
    const NativeInfoStaticTextPlan& font) noexcept {
    if(font.script_index!=4u ||
       !native_video_font_tail_valid(copy,font))return false;
    copy[font.jfftxt_node_index+1u].parameter=0u;
    copy[font.jfftxt_node_index+1u].type=12u; // immutable text index 0
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
// Standalone VIDEO intentionally leaves the original normal-5 script1
// unchanged. Its native JFFTXT call and V14 state also feed the parent
// Options labels; altering them made "VIDEO" disappear intermittently.
// Navigation is now scoped to the verified active VIDEO state at the
// engine's own frame boundary instead (never on a polling worker).
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
    // No native menu entry can have the positive index 0x7FFFFFFF.
    // The interpreter will not enter the brightness-specific branch.
    // Other animation scripts and the animated butterfly are untouched.
    copy[3].parameter=0x7FFFFFFFu;
    return true;
}
} // namespace r2fix
