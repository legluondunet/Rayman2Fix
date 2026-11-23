#pragma once
// Source-confirmed original shared Fix.sna website selection.
// 23 normal/6 reflex controller, normal7 script9, 1282 8-byte nodes.
// The native old-site action is Proc369 at node 1158 behind V16==3.
// Never use guessed text/coordinate-only site triggers in gameplay.
#include <cstdint>
#include <cstddef>
#include "rayman2fix/pause_video_model_policy.hpp"
#include "rayman2fix/native_graphics_static_text_policy.hpp"
namespace r2fix {
constexpr unsigned kPauseSiteOriginalScriptNodes=1282u;
constexpr unsigned kPauseSiteActionNode=1158u;
constexpr unsigned kPauseSiteUrlNode=1159u;
constexpr std::uint64_t kPauseSiteOriginalScriptShape=
    0xa318b1055ee76c9fULL;
inline std::uint64_t pause_site_script_shape(
    const NativeMenuScriptNode* nodes,unsigned count) noexcept {
    if(!nodes || count!=kPauseSiteOriginalScriptNodes)return 0u;
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned i=0u;i<count;++i) {
        const auto& n=nodes[i];
        for(unsigned b=0u;b<2u;++b) {
            h^=(n.padding>>(8u*b))&255u;
            h*=1099511628211ULL;
        }
        h^=n.indent;h*=1099511628211ULL;
        h^=n.type;h*=1099511628211ULL;
    }
    return h;
}
inline bool authenticated_pause_site_script(
    const NativeMenuScriptNode* nodes,unsigned count,
    std::uint64_t expected=kPauseSiteOriginalScriptShape) noexcept {
    if(!nodes || count!=kPauseSiteOriginalScriptNodes ||
       pause_site_script_shape(nodes,count)!=expected)return false;
    const auto at=[&](unsigned i,std::uint32_t p,
                      std::uint8_t depth,std::uint8_t type) noexcept {
        const auto& node=nodes[i];
        return node.parameter==p && node.padding==0u &&
               node.indent==depth && node.type==type;
    };
    return at(1153u,0u,2u,0u) &&
        at(1154u,4u,3u,1u) &&
        at(1155u,16u,4u,11u) &&
        at(1156u,3u,4u,12u) &&
        at(1157u,8u,2u,0u) &&
        at(kPauseSiteActionNode,369u,3u,4u) &&
        nodes[kPauseSiteUrlNode].type==20u &&
        nodes[kPauseSiteUrlNode].indent==4u &&
        nodes[kPauseSiteUrlNode].padding==0u &&
        menu_safe_ptr(nodes[kPauseSiteUrlNode].parameter) &&
        at(count-1u,0u,0u,8u);
}
inline bool pause_site_action_eligible(int normal,int reflex,
                                       std::uint32_t selection) noexcept {
    return normal==7 && reflex==2 && selection==3u;
}
// Source DsgVar16: 86 entries, scalar kind 5 at offset 0x110.
// Validate reciprocal Mind/AI link separately via inspect_pause_video_model.
template<class R32,class R8>
bool locate_verified_pause_site_selection(
    std::uintptr_t mind,R32 read32,R8 read8,
    std::uintptr_t& slot) noexcept {
    slot=0;
    if(!menu_safe_ptr(mind,16u))return false;
    std::uint32_t memory=0,ref=0,declaration=0,infos=0,values=0;
    std::uint32_t offset=0,type=0;
    std::uint8_t count=0;
    if(!read32(mind+12u,memory) || !menu_safe_ptr(memory,12u) ||
       !read32(memory,ref) || !menu_safe_ptr(ref) ||
       !read32(ref,declaration) || !menu_safe_ptr(declaration,16u) ||
       !read32(declaration+4u,infos) ||
       !read8(declaration+12u,count) || count!=86u ||
       !menu_safe_ptr(infos,86u*16u) ||
       !read32(memory+8u,values) || !menu_safe_ptr(values,0xF4u) ||
       !read32(std::uintptr_t(infos)+16u*16u,offset) ||
       !read32(std::uintptr_t(infos)+16u*16u+4u,type) ||
       offset!=0x110u || type!=5u ||
       !menu_safe_ptr(std::uintptr_t(values)+offset))
        return false;
    slot=std::uintptr_t(values)+offset;
    return true;
}
} // namespace r2fix
