#pragma once
// Exact source shape of normal-11/script-0 of the owner's 638463-byte
// Menu.sna. Redirect only the website literal bound to procedure 369.
// This is a private in-memory script copy, never a modified retail asset.
#include <cstdint>
#include "rayman2fix/native_graphics_static_text_policy.hpp"
namespace r2fix {
constexpr unsigned kSiteScriptNodes=1995u;
constexpr unsigned kSiteProcedureNode=1835u;
constexpr unsigned kSiteUrlNode=1836u;
constexpr std::uint64_t kSiteScriptShape=0xacbf1fd122c502cdULL;
inline std::uint64_t menu_site_script_shape(
    const NativeMenuScriptNode* nodes,unsigned count) noexcept {
    if(!nodes || count!=kSiteScriptNodes)return 0u;
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned i=0;i<count;++i) {
        const auto& n=nodes[i];
        for(unsigned j=0;j<2u;++j) {
            h^=(n.padding>>(8u*j))&255u;
            h*=1099511628211ULL;
        }
        h^=n.indent; h*=1099511628211ULL;
        h^=n.type; h*=1099511628211ULL;
    }
    return h;
}
inline bool redirect_menu_site_script(NativeMenuScriptNode* nodes,
    unsigned count,std::uintptr_t original_url,std::uintptr_t replacement,
    std::uint64_t expected=kSiteScriptShape) noexcept {
    if(!nodes || count!=kSiteScriptNodes ||
       !original_url || !replacement ||
       original_url==replacement || original_url>0xFFFFFFFFu ||
       replacement>0xFFFFFFFFu ||
       menu_site_script_shape(nodes,count)!=expected)
        return false;
    const auto proc=nodes[kSiteProcedureNode];
    const auto arg=nodes[kSiteUrlNode];
    if(proc.type!=4u || proc.parameter!=369u ||
       proc.indent!=7u || proc.padding!=0u ||
       arg.type!=20u || arg.parameter!=original_url ||
       arg.indent!=8u || arg.padding!=0u ||
       nodes[kSiteScriptNodes-1u].type!=8u ||
       nodes[kSiteScriptNodes-1u].indent!=0u)
        return false;
    nodes[kSiteUrlNode].parameter=static_cast<std::uint32_t>(replacement);
    return true;
}
} // namespace r2fix
