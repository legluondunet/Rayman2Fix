#include "rayman2fix/pause_video_model_policy.hpp"
#include <cstdint>
#include <map>
#include <cstdlib>
using namespace r2fix;
static void check(bool b) {if(!b)std::abort();}
int main() {
    std::map<std::uintptr_t,std::uint32_t> words;
    std::map<std::uintptr_t,std::uint8_t> bytes;
    const auto w=[&](std::uintptr_t p,std::uint32_t x){words[p]=x;};
    const auto b=[&](std::uintptr_t p,std::uint8_t x){bytes[p]=x;};
    const auto r32=[&](std::uintptr_t p,std::uint32_t& x){
        auto it=words.find(p);if(it==words.end())return false;
        x=it->second;return true;
    };
    const auto r8=[&](std::uintptr_t p,std::uint8_t& x){
        auto it=bytes.find(p);if(it==bytes.end())return false;
        x=it->second;return true;
    };
    constexpr std::uintptr_t model=0x200000, mind=model-24,
        normal=0x201000, reflex=0x201100,
        nentries=0x202000,rentries=0x203000,
        ni=0x204000,ri=0x205000,
        list9=0x206000,list15=0x207000,
        script9=0x208000,script15=0x209000;
    w(model,normal);w(model+4,reflex);w(model+16,1);
    w(normal,nentries);w(normal+4,23);
    w(reflex,rentries);w(reflex+4,6);
    w(nentries+9*12,list9);b(nentries+9*12+8,5);
    w(nentries+15*12,list15);b(nentries+15*12+8,3);
    w(list9+2*4,script9);w(list15+2*4,script15);
    const auto node=[&](std::uintptr_t start,unsigned index,
                        std::uint32_t id,std::uint8_t kind) {
        w(start+index*8,id);b(start+index*8+7,kind);
    };
    for(unsigned i:{40u,50u,60u})node(script9,i,356u,4u);
    node(script9,71u,379u,27u);node(script9,101u,371u,27u);
    node(script15,6u,356u,4u);
    w(mind,model);w(mind+4,ni);w(mind+8,ri);
    w(ni,model);w(ri,model+4);
    w(ni+8,nentries+9*12);w(ri+8,rentries);
    PauseVideoModel out{};
    check(inspect_pause_video_model(model,r32,r8,out));
    check(out.valid() && out.normal==9 && out.reflex==0);
    w(script9+50*8,357); // any one authenticating video call changed
    check(!inspect_pause_video_model(model,r32,r8,out));
    check(!out.valid());
    w(script9+50*8,356);
    w(normal+4,27); // reject MENU's different behavior count
    check(!inspect_pause_video_model(model,r32,r8,out));
    w(normal+4,23);
    w(ri,model); // reject mismatch in reciprocal reflex pointer
    check(!inspect_pause_video_model(model,r32,r8,out));
    w(ri,model+4);
    w(ni+8,nentries+23*12); // reject outside normal behaviors
    check(!inspect_pause_video_model(model,r32,r8,out));
    w(ni+8,nentries+9*12);
    check(inspect_pause_video_model(model,r32,r8,out));
    check(!inspect_pause_video_model(0,r32,r8,out));
    return 0;
}
