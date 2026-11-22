#include "rayman2fix/native_graphics_static_text_policy.hpp"
#include <array>
#include <cstdlib>
#include <cstdint>

using namespace r2fix;
static void verify(bool value) {if(!value)std::abort();}
template <std::size_t N>
std::array<NativeMenuScriptNode,N> script_fixture(unsigned jff) {
    std::array<NativeMenuScriptNode,N> nodes{};
    for(std::size_t i=0;i<N;++i) {
        nodes[i].parameter=static_cast<std::uint32_t>(i*7u+1u);
        nodes[i].padding=0;
        nodes[i].indent=2u;
        nodes[i].type=12u;
    }
    nodes[jff]={506u,0u,3u,4u};
    nodes[jff+1u]={17u,0u,4u,11u};
    nodes[jff+2u]={1u,0u,4u,12u};
    nodes[jff+3u]={0x3F800000u,0u,4u,13u};
    nodes[N-1u]={0u,0u,0u,8u};
    return nodes;
}
template <std::size_t N>
void check_copy_patch(unsigned jff) {
    auto original=script_fixture<N>(jff);
    const auto plan=NativeInfoStaticTextPlan{
        1u,static_cast<unsigned>(N),jff,
        native_menu_script_fingerprint(original.data(),static_cast<unsigned>(N))};
    auto copy=original;
    verify(make_static_video_font_script(copy.data(),plan));
    verify(copy[jff+1u].parameter==0u);
    verify(copy[jff+1u].type==12u);
    verify(original[jff+1u].parameter==17u);
    verify(original[jff+1u].type==11u);
    for(unsigned i=0;i<N;++i) {
        if(i!=jff+1u) {
            verify(copy[i].parameter==original[i].parameter);
            verify(copy[i].padding==original[i].padding);
            verify(copy[i].indent==original[i].indent);
            verify(copy[i].type==original[i].type);
        }
    }
    auto refused=original;
    refused[jff+1u].parameter=18u;
    verify(!make_static_video_font_script(refused.data(),plan));
    refused=original;
    refused[jff].parameter=357u; // brightness must never be touched
    verify(!make_static_video_font_script(refused.data(),plan));
    refused=original;
    refused[10].parameter^=1u; // reject wrong full-script fingerprint
    verify(!make_static_video_font_script(refused.data(),plan));
    refused=original;
    refused[N-1u].type=12u;
    verify(!make_static_video_font_script(refused.data(),plan));
    verify(!make_static_video_font_script(nullptr,plan));
    const auto short_plan=NativeInfoStaticTextPlan{1u,5u,jff,0u};
    verify(!make_static_video_font_script(original.data(),short_plan));
}
int main() {
    static_assert(sizeof(NativeMenuScriptNode)==8u);
    verify(kNativeInfoTextPlans[0].script_index==1u);
    verify(kNativeInfoTextPlans[0].node_count==324u);
    verify(kNativeInfoTextPlans[0].jfftxt_node_index==313u);
    verify(kNativeInfoTextPlans[1].script_index==4u);
    verify(kNativeInfoTextPlans[1].node_count==517u);
    verify(kNativeInfoTextPlans[1].jfftxt_node_index==512u);
    check_copy_patch<324u>(313u);
    check_copy_patch<517u>(512u);
    // Relocated pointers are deliberately omitted from the normalized
    // script fingerprint, but integer values / node IDs are not.
    auto nodes=script_fixture<324u>(313u);
    nodes[8].type=23u;
    auto baseline=native_menu_script_fingerprint(nodes.data(),324u);
    nodes[8].parameter=0x78563412u;
    verify(native_menu_script_fingerprint(nodes.data(),324u)==baseline);
    nodes[8].type=12u;
    verify(native_menu_script_fingerprint(nodes.data(),324u)!=baseline);
    verify(native_menu_script_fingerprint(nullptr,324u)==0u);
    verify(native_menu_script_fingerprint(nodes.data(),0u)==0u);
}
