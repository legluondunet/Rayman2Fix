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
    // Script 1's V14 rows belong to parent Options, not VIDEO.
    // The modifier must reject it without changing even one byte.
    if(script_index==1u) {
        check(!disable_native_video_choices(patched.data(),plan));
        for(unsigned i=0u;i<N;++i)
            check(patched[i].parameter==before[i].parameter &&
                  patched[i].type==before[i].type &&
                  patched[i].indent==before[i].indent &&
                  patched[i].padding==before[i].padding);
        return;
    }
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
// Synthetic owner-script footprint. The real menu script must match
// both the complete type/depth/padding shape AND this exact terminal
// focus-dependent JFFTXT before the engine receives an inert copy.
template<std::size_t N>
void check_static_font(unsigned index,unsigned tail) {
    std::array<NativeMenuScriptNode,N> before{};
    for(auto& n:before)n={0u,0u,2u,12u};
    before[tail]={506u,0u,3u,4u};
    before[tail+1u]={17u,0u,4u,11u};
    before[tail+2u]={1u,0u,4u,12u};
    before[tail+3u]={0x3F800000u,0u,4u,13u};
    before[N-1]={0u,0u,0u,8u};
    const NativeVideoDisablePlan selection{
        index,unsigned(N),
        native_video_script_shape_hash(before.data(),unsigned(N)),
        {0u,0u,0u},{0u,0u,0u}};
    const NativeInfoStaticTextPlan font{
        index,unsigned(N),tail,0u};
    if(index==1u) {
        check(!native_video_static_font_preflight(before.data(),
            selection,font));
        auto unchanged=before;
        check(!freeze_native_video_info_font(unchanged.data(),font));
        for(unsigned i=0u;i<N;++i)
            check(unchanged[i].parameter==before[i].parameter &&
                  unchanged[i].type==before[i].type);
        return;
    }
    check(native_video_static_font_preflight(before.data(),
        selection,font));
    auto copy=before;
    check(freeze_native_video_info_font(copy.data(),font));
    for(unsigned i=0u;i<N;++i) {
        check(copy[i].parameter==(i==tail+1u?0u:before[i].parameter));
        check(copy[i].type==(i==tail+1u?12u:before[i].type));
        check(copy[i].padding==before[i].padding);
        check(copy[i].indent==before[i].indent);
    }
    auto bad=before;
    bad[tail+1u].parameter=51u;
    check(!native_video_static_font_preflight(bad.data(),selection,font));
    check(!freeze_native_video_info_font(bad.data(),font));
    bad=before;
    bad[tail+1u].indent=3u;
    check(!native_video_static_font_preflight(bad.data(),selection,font));
    bad=before;
    bad[8].type=4u;
    check(!native_video_static_font_preflight(bad.data(),selection,font));
    bad=before;
    bad[tail].parameter=357u;
    check(!native_video_static_font_preflight(bad.data(),selection,font));
    check(!native_video_static_font_preflight(nullptr,selection,font));
}
// A source-shaped synthetic normal-5 VIDEO script: V51 branches
// switch the obsolete SAVE RESOLUTION status V14[4] from 1 to 2.
// The read-only page must not visually react to Enter.
template<std::size_t N>
void check_enter_status(unsigned script_index,unsigned enabled) {
    std::array<NativeMenuScriptNode,N> nodes{};
    for(auto& node:nodes)node={0u,0u,2u,12u};
    const unsigned d=script_index==3u?4u:3u;
    nodes[enabled-7u]={0u,0u,static_cast<std::uint8_t>(d-1u),0u};
    nodes[enabled-6u]={51u,0u,static_cast<std::uint8_t>(d),11u};
    nodes[enabled-5u]={8u,0u,static_cast<std::uint8_t>(d-1u),0u};
    nodes[enabled+1u]={9u,0u,static_cast<std::uint8_t>(d-1u),0u};
    const auto put=[&](unsigned at,unsigned status) {
        nodes[at-4u]={11u,0u,static_cast<std::uint8_t>(d),2u};
        nodes[at-3u]={26u,0u,static_cast<std::uint8_t>(d+1u),2u};
        nodes[at-2u]={14u,0u,static_cast<std::uint8_t>(d+2u),11u};
        nodes[at-1u]={4u,0u,static_cast<std::uint8_t>(d+2u),12u};
        nodes[at]={status,0u,static_cast<std::uint8_t>(d+1u),12u};
    };
    put(enabled,2u);
    put(enabled+6u,1u);
    nodes[N-1u]={0u,0u,0u,8u};
    const NativeVideoDisablePlan plan{script_index,unsigned(N),
       native_video_script_shape_hash(nodes.data(),unsigned(N)),
       {0u,0u,0u},{0u,0u,0u}};
    auto patched=nodes;
    check(neutralize_native_video_enter_save_status(patched.data(),plan));
    for(unsigned i=0u;i<N;++i) {
        const unsigned expected=i==enabled?1u:nodes[i].parameter;
        check(patched[i].parameter==expected);
        check(patched[i].type==nodes[i].type);
        check(patched[i].indent==nodes[i].indent);
    }
    check(patched[enabled-6u].parameter==51u); // input untouched
    check(patched[enabled+6u].parameter==1u);  // idle unchanged
    auto invalid=nodes;
    invalid[enabled-6u].parameter=17u; // not the Enter latch
    check(!neutralize_native_video_enter_save_status(
        invalid.data(),plan));
    invalid=nodes;
    invalid[enabled+6u].parameter=2u; // incorrect else arm
    check(!neutralize_native_video_enter_save_status(
        invalid.data(),plan));
    invalid=nodes;
    invalid[enabled-1u].parameter=3u; // not SAVE RESOLUTION
    check(!neutralize_native_video_enter_save_status(
        invalid.data(),plan));
    invalid=nodes;
    invalid[9].type=4u; // incorrect shape
    check(!neutralize_native_video_enter_save_status(
        invalid.data(),plan));
    check(!neutralize_native_video_enter_save_status(nullptr,plan));
    auto parent=plan;
    parent.script_index=1u;
    check(!neutralize_native_video_enter_save_status(
        nodes.data(),parent));
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
    check_static_font<324u>(1u,313u);
    check_static_font<517u>(4u,512u);
    check_enter_status<430u>(3u,373u);
    check_enter_status<517u>(4u,333u);
    return 0;
}
