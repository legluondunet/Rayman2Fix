#pragma once
// Experimental, memory-only VIDEO information styling for the single
// approved Rayman 2 PC Menu.sna. Never modifies commercial game assets.
// This changes only a COPY of the validated script's final JFFTXT selection
// parameter. V17, native keyboard controls, brightness and exit remain native.
#include <cstddef>
#include <cstdint>

namespace r2fix {
struct NativeMenuScriptNode {
    std::uint32_t parameter;
    std::uint16_t padding;
    std::uint8_t indent;
    std::uint8_t type;
};
static_assert(sizeof(NativeMenuScriptNode)==8,
              "Rayman 2 PC script-node layout changed");

struct NativeInfoStaticTextPlan {
    unsigned script_index;
    unsigned node_count;
    unsigned jfftxt_node_index;
    std::uint64_t scene_fingerprint;
};
// The model and node positions are authenticated against the owner's
// decoded 638463-byte PC MENU SNA SHA-256 73e75488...17e82b17.
// Fingerprints ignore pointers that may relocate in live scene memory.
constexpr NativeInfoStaticTextPlan kNativeInfoTextPlans[]{
    {1u,324u,313u,0xb363803e1ef0a4dfULL},
    {4u,517u,512u,0xe3f194a21d7d5471ULL}
};
constexpr bool script_node_uses_scene_pointer(std::uint8_t type) noexcept {
    return type==14u || type==23u || type==28u ||
           type==30u || type==32u;
}
inline std::uint64_t native_menu_script_fingerprint(
    const NativeMenuScriptNode* nodes,unsigned count) noexcept {
    if(!nodes || count==0u || count>1024u)return 0u;
    std::uint64_t hash=14695981039346656037ULL;
    const auto consume=[&](std::uint32_t word,unsigned bytes) {
        for(unsigned k=0;k<bytes;++k) {
            hash^=(word>>(k*8u))&255u;
            hash*=1099511628211ULL;
        }
    };
    for(unsigned i=0;i<count;++i) {
        const auto& node=nodes[i];
        const auto scalar=script_node_uses_scene_pointer(node.type) ?
            0u : node.parameter;
        consume(scalar,4u);
        consume(node.padding,2u);
        consume(node.indent,1u);
        consume(node.type,1u);
    }
    return hash;
}
// Check the actual terminal instruction shape independently of the
// complete script fingerprint. The original preflight combined both,
// making "shape=0" misleading when only unrelated relocated nodes
// differ. This helper is read-only and never relaxes patch approval.
inline bool native_video_font_tail_valid(
    const NativeMenuScriptNode* nodes,const NativeInfoStaticTextPlan& plan)
    noexcept {
    if(!nodes || plan.node_count<8u || plan.node_count>1024u ||
       plan.jfftxt_node_index+4u>=plan.node_count)return false;
    const auto& command=nodes[plan.jfftxt_node_index];
    const auto& value=nodes[plan.jfftxt_node_index+1u];
    const auto& mode=nodes[plan.jfftxt_node_index+2u];
    const auto& amount=nodes[plan.jfftxt_node_index+3u];
    const auto& end=nodes[plan.node_count-1u];
    return command.type==4u && command.parameter==506u &&
           command.indent==3u && command.padding==0u &&
           value.type==11u && value.parameter==17u &&
           value.indent==4u && value.padding==0u &&
           mode.type==12u && mode.parameter==1u &&
           mode.indent==4u && mode.padding==0u &&
           amount.type==13u && amount.parameter==0x3F800000u &&
           amount.indent==4u && amount.padding==0u &&
           end.type==8u && end.indent==0u && end.padding==0u;
}
inline bool make_static_video_font_script(
    NativeMenuScriptNode* copied_nodes,const NativeInfoStaticTextPlan& plan)
    noexcept {
    if(!native_video_font_tail_valid(copied_nodes,plan) ||
       native_menu_script_fingerprint(copied_nodes,plan.node_count)!=
           plan.scene_fingerprint)return false;
    // Zero identifies the title/blanked VIDEO text handle, outside
    // the four native resolution-selection labels. The *actual* V17
    // continues to track the native menu input; this is presentation
    // only and cannot change a display mode.
    copied_nodes[plan.jfftxt_node_index+1u].parameter=0u;
    copied_nodes[plan.jfftxt_node_index+1u].type=12u; // integer literal
    return true;
}
} // namespace r2fix
