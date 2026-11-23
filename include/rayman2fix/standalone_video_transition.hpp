#pragma once
// Source-owned replacement for the approved Rayman 2 MENU normal-3,
// script-2 VIDEO opening path. Does not edit original SNA/RTB resources.
#include "rayman2fix/native_graphics_static_text_policy.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace r2fix {
constexpr unsigned kStandalonePreparationOriginalNodes=1383u;
constexpr unsigned kStandaloneVideoBranchBody=10u;
constexpr unsigned kStandaloneVideoBranchTransfer=518u;
constexpr unsigned kStandalonePreparationNewNodes=
    kStandaloneVideoBranchBody+
    kStandalonePreparationOriginalNodes-kStandaloneVideoBranchTransfer;
constexpr std::uint64_t kStandaloneNormal3Script2Shape=
    0xa73cf7f2ab1a2b17ULL;

// Hash of the original, relocated PC MENU script-node *shape*.
// Pointers and scalar arguments are separately checked at known nodes.
// All 1,383 nodes must be present; no partial script is accepted.
inline std::uint64_t standalone_script_shape_hash(
    const NativeMenuScriptNode* nodes,unsigned count) noexcept {
    if(!nodes || count!=kStandalonePreparationOriginalNodes)return 0u;
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned i=0;i<count;++i) {
        const auto& n=nodes[i];
        const std::uint32_t fields[3]{n.padding,n.indent,n.type};
        const unsigned lengths[3]{2u,1u,1u};
        for(unsigned j=0;j<3;++j)
            for(unsigned k=0;k<lengths[j];++k) {
                hash^=(fields[j]>>(8u*k))&255u;
                hash*=1099511628211ULL;
            }
    }
    return hash;
}
// Normal-3 script-2 contains a VIDEO branch (V17 == 0) whose first
// 508 nodes instantiate obsolete VIDEO choices and font animations.
// The *last* two native menu calls in the same branch (nodes 518-521)
// transition to the verified normal-5 behavior. Branches for SOUND,
// CONTROLS and LANGUAGE follow at original node 522 and are untouched.
// Keep the normal-3 entry condition and move its already-verified
// transition calls and all other branches up before the old widgets.
// The END remains at original tail and becomes node 874. The game's
// interpreter uses END to delimit a script. None of the original bytes
// or game assets are written to disk.
inline bool standalone_skip_legacy_video_open(
    NativeMenuScriptNode* nodes,unsigned count,
    std::uint32_t expected_normal5_reference,
    std::uint64_t approved_shape=kStandaloneNormal3Script2Shape) noexcept {
    if(!nodes || count!=kStandalonePreparationOriginalNodes ||
       !expected_normal5_reference ||
       standalone_script_shape_hash(nodes,count)!=approved_shape)
        return false;
    const auto matches=[&](unsigned i,std::uint8_t type,
                           std::uint8_t depth,std::uint32_t value) {
        const auto& n=nodes[i];
        return n.type==type && n.indent==depth &&
               n.padding==0u && n.parameter==value;
    };
    if(!matches(0,0u,1u,0u) ||
       !matches(1,1u,2u,45u) ||
       !matches(4,5u,2u,0u) ||
       !matches(7,1u,3u,4u) ||
       !matches(8,11u,4u,17u) ||
       !matches(9,12u,4u,0u) ||
       !matches(10,0u,2u,8u) ||
       !matches(518,4u,3u,70u) ||
       nodes[519].type!=28u || nodes[519].indent!=4u ||
       nodes[519].padding!=0u ||
       nodes[519].parameter<0x10000u ||
       !matches(520,4u,3u,69u) ||
       !matches(521,28u,4u,expected_normal5_reference) ||
       !matches(522,0u,2u,0u) ||
       !matches(count-1u,8u,0u,0u))
        return false;
    // All verification is completed before this single private write.
    std::memmove(nodes+kStandaloneVideoBranchBody,
                 nodes+kStandaloneVideoBranchTransfer,
                 (count-kStandaloneVideoBranchTransfer)*
                    sizeof(NativeMenuScriptNode));
    return nodes[kStandalonePreparationNewNodes-1u].type==8u;
}
} // namespace r2fix
