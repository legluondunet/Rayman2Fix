#include "rayman2fix/standalone_video_transition.hpp"
#include <array>
#include <initializer_list>
#include <cstdlib>
#include <cstdint>
using namespace r2fix;
void require(bool value) {if(!value)std::abort();}
int main() {
    static_assert(kStandalonePreparationNewNodes==875u,
                  "VIDEO source branch should shrink to 875 nodes");
    std::array<NativeMenuScriptNode,kStandalonePreparationOriginalNodes> a{};
    for(auto& n:a)n={0u,0u,3u,12u};
    a[0]={0u,0u,1u,0u};
    a[1]={45u,0u,2u,1u};
    a[4]={0u,0u,2u,5u};
    a[7]={4u,0u,3u,1u};
    a[8]={17u,0u,4u,11u};
    a[9]={0u,0u,4u,12u};
    a[10]={8u,0u,2u,0u};
    a[518]={70u,0u,3u,4u};
    a[519]={0x07956E18u,0u,4u,28u};
    a[520]={69u,0u,3u,4u};
    a[521]={0x07921948u,0u,4u,28u};
    a[522]={0u,0u,2u,0u};
    a[523]={4u,0u,3u,1u};
    a[524]={17u,0u,4u,11u};
    a[525]={1u,0u,4u,12u};
    a.back()={0u,0u,0u,8u};
    const auto old=a;
    const auto hash=standalone_script_shape_hash(a.data(),unsigned(a.size()));
    require(hash!=0);
    require(standalone_skip_legacy_video_open(a.data(),unsigned(a.size()),
        0x07921948u,hash));
    for(unsigned n=0;n<10u;++n) {
        require(a[n].parameter==old[n].parameter);
        require(a[n].type==old[n].type);
        require(a[n].indent==old[n].indent);
    }
    for(unsigned n=10u;n<kStandalonePreparationNewNodes;++n) {
        const auto& before=old[n+518u-10u];
        require(a[n].parameter==before.parameter);
        require(a[n].type==before.type);
        require(a[n].indent==before.indent);
    }
    require(a[10].parameter==70u && a[12].parameter==69u);
    require(a[14].parameter==0u && a[14].type==0u);
    require(a[kStandalonePreparationNewNodes-1u].type==8u);
    require(!standalone_skip_legacy_video_open(a.data(),1383u,
        0x07921948u,hash)); // not valid twice
    for(unsigned changed : {7u,518u,520u,521u,522u}) {
        auto corrupt=old;
        corrupt[changed].parameter^=1u;
        require(!standalone_skip_legacy_video_open(corrupt.data(),
            unsigned(corrupt.size()),0x07921948u,hash));
        require(corrupt[518].parameter==
                (changed==518u?71u:70u)); // no half-patch
    }
    require(!standalone_skip_legacy_video_open(nullptr,1383u,
        0x07921948u,hash));
    auto unchanged=old;
    require(!standalone_skip_legacy_video_open(unchanged.data(),100u,
        0x07921948u,hash));
    return 0;
}
