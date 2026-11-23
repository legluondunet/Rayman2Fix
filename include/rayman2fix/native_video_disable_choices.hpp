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
           !native_video_status_assignment(copy,p.count,
             p.highlighted[row-1u],row,3u))
            return false;
    // All checks precede any mutation; no partial patched copy on error.
    for(unsigned row=0u;row<3u;++row) {
        copy[p.initialized[row]].parameter=1u;
        copy[p.highlighted[row]].parameter=1u;
    }
    return true;
}
} // namespace r2fix
