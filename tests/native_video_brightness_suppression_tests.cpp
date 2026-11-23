#include "rayman2fix/native_video_disable_choices.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>

using namespace r2fix;
static void check(bool value){if(!value)std::abort();}
int main() {
    // Synthetic AST shape only, not original copyrighted MENU script data.
    // One signed source value is validated for each brightness transition
    // target, and all 33 node kinds/nesting levels must match.
    constexpr std::uint8_t kinds[]{
        0,1,11,12,0,4,12,16,13,13,13,16,13,13,13,32,12,
        4,12,16,13,13,11,16,13,13,13,32,12,4,28,28,8};
    constexpr std::uint8_t depths[]{
        1,2,3,3,1,2,3,3,4,4,4,3,4,4,4,3,3,
        2,3,3,4,4,4,3,4,4,4,3,3,2,3,3,0};
    static_assert(sizeof(kinds)==kNativeBrightnessTransitionNodes);
    static_assert(sizeof(depths)==kNativeBrightnessTransitionNodes);
    std::array<NativeMenuScriptNode,kNativeBrightnessTransitionNodes> nodes{};
    for(unsigned i=0;i<nodes.size();++i) {
        nodes[i].type=kinds[i];
        nodes[i].indent=depths[i];
    }
    constexpr std::uintptr_t normal18=0x079219E4u;
    constexpr std::uintptr_t reflex5=0x07956E54u;
    nodes[1].parameter=4u;  // Cond_Equal
    nodes[2].parameter=17u; // V17, native current menu focus
    nodes[3].parameter=5u;  // brightness option index
    nodes[29].parameter=71u; // Proc_ChangeMyComportAndMyReflex
    nodes[30].parameter=static_cast<std::uint32_t>(normal18);
    nodes[31].parameter=static_cast<std::uint32_t>(reflex5);
    check(native_video_script_shape_hash(nodes.data(),nodes.size())==
          kNativeBrightnessTransitionShape);
    auto original=nodes;
    check(suppress_native_video_brightness_transition(
        nodes.data(),nodes.size(),normal18,reflex5));
    for(unsigned i=0;i<nodes.size();++i) {
        check(nodes[i].indent==original[i].indent &&
              nodes[i].type==original[i].type &&
              nodes[i].padding==original[i].padding);
        check(nodes[i].parameter==
             (i==3u?0x7FFFFFFFu:original[i].parameter));
    }
    const auto reject=[&](unsigned node,std::uint32_t replacement) {
        auto invalid=original;
        invalid[node].parameter=replacement;
        const auto before=invalid;
        check(!suppress_native_video_brightness_transition(
            invalid.data(),invalid.size(),normal18,reflex5));
        for(unsigned i=0;i<invalid.size();++i)
            check(invalid[i].parameter==before[i].parameter);
    };
    reject(1,8u);  // non-equality expression
    reject(2,18u); // unrelated variable
    reject(3,4u);  // different index
    reject(29,70u); // unrelated behavior-changing command
    reject(30,0x00001234u); // not the expected target behavior
    reject(31,0x00005678u); // not the exact reflex
    auto wrong=original;
    wrong[17].type=5u;
    check(!suppress_native_video_brightness_transition(
        wrong.data(),wrong.size(),normal18,reflex5));
    check(!suppress_native_video_brightness_transition(
        original.data(),original.size(),normal18+12u,reflex5));
    check(!suppress_native_video_brightness_transition(
        original.data(),original.size(),normal18,reflex5+12u));
    check(!suppress_native_video_brightness_transition(
        nullptr,original.size(),normal18,reflex5));
    check(!suppress_native_video_brightness_transition(
        wrong.data(),wrong.size()-1u,normal18,reflex5));
    return 0;
}
