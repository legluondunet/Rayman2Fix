#pragma once
// Fail-closed, source-verified removal of the original in-game ESC
// Options -> VIDEO preparation widgets. Only the approved 23/6
// shared Fix.sna controller normal-6/script-2 (1303 nodes) qualifies.
// In-memory copies ONLY; original games and parent Options remain intact.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "rayman2fix/native_graphics_static_text_policy.hpp"
namespace r2fix {
constexpr unsigned kPauseVideoPrepOriginalNodes=1303u;
constexpr unsigned kPauseVideoPrepBranchStart=11u;
constexpr unsigned kPauseVideoPrepTransfer=498u;
constexpr unsigned kPauseVideoPrepNewNodes=
    kPauseVideoPrepBranchStart+
    kPauseVideoPrepOriginalNodes-kPauseVideoPrepTransfer;
constexpr std::uint64_t kPauseVideoPrepOriginalShape=
    0xd201f1c8bc668803ULL;
inline std::uint64_t pause_video_prep_script_shape(
     const NativeMenuScriptNode* nodes,unsigned count) noexcept {
    if(!nodes || count!=kPauseVideoPrepOriginalNodes)return 0u;
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned i=0;i<count;++i) {
        const auto& n=nodes[i];
        for(unsigned b=0;b<2u;++b) {
            hash^=(n.padding>>(b*8u))&255u;
            hash*=1099511628211ULL;
        }
        hash^=n.indent;hash*=1099511628211ULL;
        hash^=n.type;hash*=1099511628211ULL;
    }
    return hash;
}
inline bool pause_skip_obsolete_video_preparation(
    NativeMenuScriptNode* copy,unsigned count,
    std::uint64_t expected=kPauseVideoPrepOriginalShape) noexcept {
    if(!copy || count!=kPauseVideoPrepOriginalNodes ||
       pause_video_prep_script_shape(copy,count)!=expected)
        return false;
    const auto matches=[&](unsigned i,std::uint8_t t,
                           std::uint8_t depth,std::uint32_t value) {
        const auto& n=copy[i];
        return n.padding==0u && n.type==t &&
               n.indent==depth && n.parameter==value;
    };
    if(!matches(0,0u,1u,0u) ||
       !matches(1,1u,2u,45u) ||
       !matches(4,5u,2u,0u) ||
       !matches(7,1u,3u,4u) ||
       !matches(8,11u,4u,16u) ||
       !matches(9,12u,4u,0u) ||
       !matches(10,0u,2u,8u) ||
       !matches(49,27u,4u,379u) ||
       !matches(79,27u,4u,371u) ||
       !matches(498,4u,3u,70u) ||
       !matches(500,4u,3u,69u) ||
       copy[499].type!=28u || copy[499].indent!=4u ||
       copy[499].padding!=0u ||
       copy[499].parameter<0x10000u ||
       copy[501].type!=28u || copy[501].indent!=4u ||
       copy[501].padding!=0u ||
       copy[501].parameter<0x10000u ||
       copy[499].parameter-copy[501].parameter!=0x1f6d4u ||
       !matches(502,0u,2u,0u) ||
       !matches(503,1u,3u,4u) ||
       !matches(504,11u,4u,16u) ||
       !matches(505,12u,4u,1u) ||
       !matches(count-1u,8u,0u,0u))
        return false;
    // Preserve the source VIDEO predicate and its body delimiter
    // at nodes 0..10; move native behavior transfer (70,69) and
    // every other Options branch (sound, controls, etc) forwards.
    // The original old-resolution JFFTXT nodes 49..79 and actor
    // positioning never run, so the new Glide page is not overlayed
    // atop already constructed native VIDEO choices.
    std::memmove(copy+kPauseVideoPrepBranchStart,
                 copy+kPauseVideoPrepTransfer,
                 (count-kPauseVideoPrepTransfer)*
                    sizeof(NativeMenuScriptNode));
    return copy[kPauseVideoPrepNewNodes-1u].type==8u;
}
} // namespace r2fix
