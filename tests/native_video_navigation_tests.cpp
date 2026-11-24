#include "rayman2fix/native_video_disable_choices.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>

using namespace r2fix;
static void verify(bool value) {if(!value)std::abort();}
static void assignment(NativeMenuScriptNode* n,unsigned node,
                       unsigned destination,unsigned value,
                       unsigned kind=12u,unsigned indent=3u) {
    n[node-2u]={11u,0u,static_cast<std::uint8_t>(indent),2u};
    n[node-1u]={destination,0u,static_cast<std::uint8_t>(indent+1u),11u};
    n[node]={value,0u,static_cast<std::uint8_t>(indent+1u),
             static_cast<std::uint8_t>(kind)};
}
template<std::size_t N>
static void test_navigation(const NativeVideoNavigationPlan& nplan,
                            const NativeVideoDisablePlan& dplan) {
    verify(nplan.count==N && dplan.count==N);
    verify(nplan.script_index==dplan.script_index);
    std::array<NativeMenuScriptNode,N> original{};
    for(auto& node:original){node={0u,0u,2u,12u};}
    // The actual SNA has six runtime V17 assignments: the first three
    // represent detected modes and the next three availability fallback.
    constexpr unsigned selections[6]{1u,2u,3u,3u,2u,1u};
    for(unsigned i=0u;i<6u;++i)
        assignment(original.data(),nplan.current_mode_selection_literals[i],
                   17u,selections[i],12u,3u+i%3u);
    assignment(original.data(),nplan.first_selectable_literal,19u,1u);
    assignment(original.data(),nplan.last_selectable_literal,20u,5u);
    if(nplan.restore_selection_operand!=0u)
        assignment(original.data(),nplan.restore_selection_operand,
                   17u,73u,11u);
    for(unsigned row=1u;row<=3u;++row) {
        const auto populate=[&](unsigned operand,unsigned state) {
            if(!operand)return;
            const auto indent=2u+row;
            original[operand-4u]={11u,0u,
                static_cast<std::uint8_t>(indent),2u};
            original[operand-3u]={26u,0u,
                static_cast<std::uint8_t>(indent+1u),2u};
            original[operand-2u]={14u,0u,
                static_cast<std::uint8_t>(indent+2u),11u};
            original[operand-1u]={row,0u,
                static_cast<std::uint8_t>(indent+2u),12u};
            original[operand]={state,0u,
                static_cast<std::uint8_t>(indent+1u),12u};
        };
        populate(dplan.initialized[row-1u],2u);
        populate(dplan.highlighted[row-1u],3u);
    }
    original[N-1u]={0u,0u,0u,8u};
    auto nav=nplan;
    nav.original_shape_hash=native_video_script_shape_hash(
        original.data(),unsigned(N));
    auto disabled=dplan;
    disabled.original_shape_hash=nav.original_shape_hash;
    auto copy=original;
    verify(disable_native_video_choices(copy.data(),disabled));
    verify(restrict_native_video_navigation(copy.data(),nav));
    verify(copy[nplan.first_selectable_literal].parameter==5u);
    verify(copy[nplan.last_selectable_literal].parameter==5u);
    for(unsigned i=0u;i<6u;++i) {
        const auto at=nplan.current_mode_selection_literals[i];
        verify(copy[at].parameter==5u && copy[at].type==12u);
    }
    if(nplan.restore_selection_operand) {
        const auto at=nplan.restore_selection_operand;
        verify(copy[at].parameter==5u && copy[at].type==12u);
        verify(original[at].parameter==73u && original[at].type==11u);
    }
    // Only the prescribed typed operands may change. No other
    // instructions, brightness handlers, or texture actors are touched.
    for(unsigned i=0u;i<N;++i) {
        bool change=false;
        for(unsigned j=0u;j<3u;++j)
            change|=(i==dplan.initialized[j] ||
                     (dplan.highlighted[j] &&
                      i==dplan.highlighted[j]));
        change|=i==nplan.first_selectable_literal;
        for(auto value:nplan.current_mode_selection_literals)
            change|=i==value;
        change|=nplan.restore_selection_operand!=0u &&
                i==nplan.restore_selection_operand;
        if(!change)verify(copy[i].parameter==original[i].parameter &&
                          copy[i].type==original[i].type);
        verify(copy[i].padding==original[i].padding &&
               copy[i].indent==original[i].indent);
    }
    auto refused=original;
    refused[nplan.first_selectable_literal].parameter=3u;
    verify(!restrict_native_video_navigation(refused.data(),nav));
    verify(refused[nplan.current_mode_selection_literals[0]].parameter==1u);
    refused=original;
    refused[nplan.current_mode_selection_literals[5]].parameter=9u;
    verify(!restrict_native_video_navigation(refused.data(),nav));
    verify(refused[nplan.first_selectable_literal].parameter==1u);
    refused=original;
    refused[nplan.last_selectable_literal-1u].parameter=22u; // not V20
    verify(!restrict_native_video_navigation(refused.data(),nav));
    if(nplan.restore_selection_operand) {
        refused=original;
        refused[nplan.restore_selection_operand].parameter=17u;
        verify(!restrict_native_video_navigation(refused.data(),nav));
    }
    refused=original;
    refused[8].type=4u; // no structural layout mutations authorized
    verify(!restrict_native_video_navigation(refused.data(),nav));
    verify(!restrict_native_video_navigation(nullptr,nav));
}
int main() {
    verify(kVideoNavigationPlans[0].script_index==3u);
    verify(kVideoNavigationPlans[1].script_index==4u);
    test_navigation<430u>(kVideoNavigationPlans[0],kVideoDisablePlans[1]);
    test_navigation<517u>(kVideoNavigationPlans[1],kVideoDisablePlans[2]);
}
