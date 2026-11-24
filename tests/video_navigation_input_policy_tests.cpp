#include "rayman2fix/video_navigation_input_policy.hpp"
#include <array>
#include <cstdlib>
using namespace r2fix;
static void check(bool b) {if(!b)std::abort();}
int main() {
    static_assert(kVideoKeyboardBytes==0x100u);
    static_assert(kDikUp==0xC8u && kDikDown==0xD0u);
    static_assert(kDikLeft==0xCBu && kDikRight==0xCDu);
    constexpr unsigned kEsc=0x01u,kEnter=0x1Cu,kSpace=0x39u,
                       kA=0x1Eu;
    std::array<std::uint8_t,256> source{};
    source[kEsc]=0x80u;source[kEnter]=0x80u;
    source[kSpace]=0x80u;source[kA]=0x80u;
    for(unsigned i:{kDikUp,kDikDown,kDikLeft,kDikRight})
        source[i]=0x80u;
    for(bool menu:{false,true})for(bool pause:{false,true}){
        auto state=source;
        const unsigned n=suppress_video_navigation_keys(
            menu,pause,true,0,256u,state.data());
        check(n==((menu||pause)?4u:0u));
        for(unsigned i:{kDikUp,kDikDown,kDikLeft,kDikRight})
            check(state[i]==((menu||pause)?0u:0x80u));
        for(unsigned i:{kEsc,kEnter,kSpace,kA})
            check(state[i]==0x80u); // preserve all non-navigation
    }
    for(int result:{-1,1}) {
        auto state=source;
        check(suppress_video_navigation_keys(
            true,false,true,result,256u,state.data())==0u);
        check(state==source);
    }
    for(unsigned count:{0u,4u,16u,128u,257u}) {
        auto state=source;
        check(suppress_video_navigation_keys(
            true,false,true,0,count,state.data())==0u);
        check(state==source);
    }
    auto alt=source;
    check(suppress_video_navigation_keys(
        true,true,false,0,256u,alt.data())==0u);
    check(alt==source);
    check(suppress_video_navigation_keys(
        true,false,true,0,256u,nullptr)==0u);
    alt=source;
    check(suppress_video_navigation_keys(
        false,true,true,0,256u,alt.data())==4u);
    check(suppress_video_navigation_keys(
        false,true,true,0,256u,alt.data())==0u); // idempotent
    return 0;
}
