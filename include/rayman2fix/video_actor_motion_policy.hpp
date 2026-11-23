#pragma once
// Exact source-confirmed removal of the two SetAGO presentation moves
// in the original MENU normal5/script1 and Fix.sna Pause normal9/script1.
// Script is a private, preflight-verified clone only. Other menu behaviors
// and original game files remain intact.
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "rayman2fix/native_graphics_static_text_policy.hpp"
namespace r2fix {
inline std::uint64_t video_actor_motion_shape(
    const NativeMenuScriptNode* nodes,unsigned count) noexcept {
    if(!nodes || (count!=304u && count!=324u))return 0u;
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned i=0u;i<count;++i) {
        const auto& n=nodes[i];
        for(unsigned b=0u;b<2u;++b){
            h^=(n.padding>>(b*8u))&255u;
            h*=1099511628211ULL;
        }
        h^=n.indent;h*=1099511628211ULL;
        h^=n.type;h*=1099511628211ULL;
    }
    return h;
}
inline bool remove_video_only_actor_moves(
    NativeMenuScriptNode* copy,unsigned count,
    unsigned first,std::uint64_t expected_shape) noexcept {
    if(!copy || (count!=324u && count!=304u) ||
       first!=(count==324u?176u:157u) ||
       video_actor_motion_shape(copy,count)!=expected_shape ||
       copy[count-1u].type!=8u || copy[count-1u].indent!=0u)
        return false;
    for(unsigned k=0u;k<2u;++k) {
        const unsigned i=first+12u*k;
        const auto ok=[&](unsigned offset,std::uint8_t type,
                          std::uint8_t indent) noexcept {
            const auto& node=copy[i+offset];
            return node.padding==0u && node.type==type &&
                   node.indent==indent;
        };
        if(!ok(0u,4u,2u) || copy[i].parameter!=503u ||
           !ok(1u,12u,3u) ||
           copy[i+1u].parameter!=(k?120u:50u) ||
           !ok(2u,16u,3u) || !ok(6u,16u,3u) ||
           !ok(10u,32u,3u) || !ok(11u,12u,3u) ||
           copy[i+11u].parameter!=(count==324u?0u:3u))
            return false;
        for(unsigned j:{3u,4u,5u,7u,8u,9u})
            if(!ok(j,13u,4u))return false;
    }
    // Each SetAGO plus its 11 arguments occupies exactly 12 nodes.
    // Remove BOTH complete expression subtrees and compact all later
    // original statements, including the ESC / parent Options path.
    // The terminal EndScript moves to count-24; the original interpreter
    // never reads the now unreachable padding nodes.
    constexpr unsigned removed=24u;
    std::memmove(copy+first,copy+first+removed,
                 (count-first-removed)*sizeof(NativeMenuScriptNode));
    for(unsigned k=count-removed;k<count;++k)
        copy[k]={0u,0u,0u,8u};
    return copy[count-removed-1u].type==8u &&
           copy[count-removed-1u].indent==0u;
}
} // namespace r2fix
