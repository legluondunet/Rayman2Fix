#include "rayman2fix/pause_menu_site_policy.hpp"
#include <vector>
#include <map>
#include <cstdlib>
using namespace r2fix;
static void ck(bool ok){if(!ok)std::abort();}
int main(){
    std::vector<NativeMenuScriptNode> n(kPauseSiteOriginalScriptNodes);
    for(auto& e:n)e={0u,0u,3u,12u};
    n[1153]={0u,0u,2u,0u};
    n[1154]={4u,0u,3u,1u};
    n[1155]={16u,0u,4u,11u};
    n[1156]={3u,0u,4u,12u};
    n[1157]={8u,0u,2u,0u};
    n[1158]={369u,0u,3u,4u};
    n[1159]={0x440100u,0u,4u,20u};
    n.back()={0u,0u,0u,8u};
    const auto shape=pause_site_script_shape(n.data(),n.size());
    ck(shape!=0u);
    ck(authenticated_pause_site_script(n.data(),n.size(),shape));
    ck(authenticated_pause_site_branch(n.data(),n.size()));
    auto unrelated=n;
    unrelated[66].type=10u; // another branch may be rewritten in RAM
    ck(!authenticated_pause_site_script(
        unrelated.data(),unrelated.size(),shape));
    ck(authenticated_pause_site_branch(
        unrelated.data(),unrelated.size()));
    unrelated=n;
    unrelated[1158].parameter=370u; // never accept another procedure
    ck(!authenticated_pause_site_branch(
        unrelated.data(),unrelated.size()));
    unrelated=n;
    unrelated[1156].parameter=4u; // V16 must select exact site row
    ck(!authenticated_pause_site_branch(
        unrelated.data(),unrelated.size()));
    ck(!authenticated_pause_site_script(n.data(),n.size(),shape+1u));
    ck(!authenticated_pause_site_script(n.data(),n.size()-1u,shape));
    for(auto [ix,p]:std::vector<std::pair<unsigned,unsigned>>{
            {1155u,17u},{1156u,2u},{1158u,370u}}) {
        auto alt=n;
        alt[ix].parameter=p;
        ck(!authenticated_pause_site_script(alt.data(),alt.size(),shape));
    }
    ck(pause_site_action_eligible(7,2,3));
    for(int normal:{6,8,9})ck(!pause_site_action_eligible(normal,2,3));
    for(int reflex:{0,1,3})ck(!pause_site_action_eligible(7,reflex,3));
    for(unsigned sel:{0u,1u,2u,4u})ck(!pause_site_action_eligible(7,2,sel));
    std::map<std::uintptr_t,std::uint32_t> words;
    std::map<std::uintptr_t,std::uint8_t> bytes;
    auto w=[&](std::uintptr_t a,std::uint32_t v){words[a]=v;};
    auto b=[&](std::uintptr_t a,std::uint8_t v){bytes[a]=v;};
    auto rd32=[&](std::uintptr_t a,std::uint32_t& v){
        auto it=words.find(a);if(it==words.end())return false;
        v=it->second;return true;
    };
    auto rd8=[&](std::uintptr_t a,std::uint8_t& v){
        auto it=bytes.find(a);if(it==bytes.end())return false;
        v=it->second;return true;
    };
    constexpr std::uintptr_t mind=0x200000u,mem=0x210000u,
       ref=0x220000u,decl=0x230000u,infos=0x240000u,
       values=0x250000u;
    w(mind+12u,mem);w(mem,ref);w(ref,decl);
    w(decl+4u,infos);b(decl+12u,86u);
    w(mem+8u,values);
    w(infos+16u*16u,0x110u);
    w(infos+16u*16u+4u,5u);
    std::uintptr_t slot=0;
    ck(locate_verified_pause_site_selection(mind,rd32,rd8,slot));
    ck(slot==values+0x110u);
    w(infos+16u*16u+4u,0u);
    ck(!locate_verified_pause_site_selection(mind,rd32,rd8,slot));
    w(infos+16u*16u+4u,5u);
    w(infos+16u*16u,0x114u);
    ck(!locate_verified_pause_site_selection(mind,rd32,rd8,slot));
    b(decl+12u,85u);
    ck(!locate_verified_pause_site_selection(mind,rd32,rd8,slot));
    return 0;
}
