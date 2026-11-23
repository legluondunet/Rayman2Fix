#include "rayman2fix/native_video_disable_choices.hpp"
#include <array>
#include <cstdlib>
#include <cstdint>
using namespace r2fix;
static void check(bool ok) {if(!ok)std::abort();}
template<std::size_t N>
static void check_script(unsigned script_index,const unsigned (&init)[3],
                         const unsigned (&active)[3]) {
    std::array<NativeMenuScriptNode,N> before{};
    for(auto& n:before){n.type=12u;n.parameter=0u;n.padding=0u;}
    for(unsigned row=1u;row<=3u;++row) {
        for(const auto index:{init[row-1u],active[row-1u]}) {
            if(index==0u)continue;
            const bool initial=index==init[row-1u];
            const std::uint8_t indent=initial?3u:4u;
            before[index-4u]={11u,0u,indent,2u};
            before[index-3u]={26u,0u,static_cast<std::uint8_t>(indent+1u),2u};
            before[index-2u]={14u,0u,static_cast<std::uint8_t>(indent+2u),11u};
            before[index-1u]={row,0u,static_cast<std::uint8_t>(indent+2u),12u};
            before[index]={initial?2u:3u,0u,
                static_cast<std::uint8_t>(indent+1u),12u};
        }
    }
    before[N-1u]={0u,0u,0u,8u};
    auto plan=NativeVideoDisablePlan{script_index,unsigned(N),
        native_video_script_shape_hash(before.data(),unsigned(N)),
        {init[0],init[1],init[2]},
        {active[0],active[1],active[2]}};
    auto patched=before;
    check(disable_native_video_choices(patched.data(),plan));
    for(unsigned i=0u;i<N;++i) {
        bool target=false;
        for(unsigned row=0u;row<3u;++row)
            if(i==init[row] || (active[row]!=0u && i==active[row]))
                target=true;
        check(patched[i].parameter==(target?1u:before[i].parameter));
        check(patched[i].padding==before[i].padding);
        check(patched[i].indent==before[i].indent);
        check(patched[i].type==before[i].type);
    }
    check(before[init[0]].parameter==2u);
    auto wrong=before;
    wrong[init[0]].parameter=7u;
    check(!disable_native_video_choices(wrong.data(),plan));
    check(wrong[init[1]].parameter==2u); // fail atomically
    if(active[1]!=0u) {
        wrong=before;
        wrong[active[1]].parameter=8u;
        check(!disable_native_video_choices(wrong.data(),plan));
    }
    wrong=before;
    wrong[10].type=5u;
    check(!disable_native_video_choices(wrong.data(),plan));
    wrong=before;
    wrong[N-1u].type=12u;
    check(!disable_native_video_choices(wrong.data(),plan));
    wrong=before;
    plan.original_shape_hash^=1u;
    check(!disable_native_video_choices(wrong.data(),plan));
    check(!disable_native_video_choices(nullptr,plan));
}
int main(){
    static_assert(kVideoDisablePlans[0].script_index==1u);
    static_assert(kVideoDisablePlans[0].count==324u);
    static_assert(kVideoDisablePlans[1].script_index==3u);
    static_assert(kVideoDisablePlans[1].count==430u);
    static_assert(kVideoDisablePlans[2].script_index==4u);
    static_assert(kVideoDisablePlans[2].count==517u);
    check_script<324u>(1u,kVideoDisablePlans[0].initialized,
                           kVideoDisablePlans[0].highlighted);
    check_script<430u>(3u,kVideoDisablePlans[1].initialized,
                           kVideoDisablePlans[1].highlighted);
    check_script<517u>(4u,kVideoDisablePlans[2].initialized,
                           kVideoDisablePlans[2].highlighted);
    return 0;
}
