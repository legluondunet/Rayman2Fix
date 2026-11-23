#include "rayman2fix/standalone_video_transition.hpp"
#include <array>
#include <initializer_list>
#include <cstdlib>
#include <cstdint>
using namespace r2fix;
void require(bool value) {if(!value)std::abort();}
int main() {
    static_assert(kStandalonePreparationNewNodes==876u,
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
        hash));
    for(unsigned n=0;n<11u;++n) {
        require(a[n].parameter==old[n].parameter);
        require(a[n].type==old[n].type);
        require(a[n].indent==old[n].indent);
    }
    for(unsigned n=10u;n<kStandalonePreparationNewNodes;++n) {
        const auto& before=old[n+518u-11u];
        require(a[n].parameter==before.parameter);
        require(a[n].type==before.type);
        require(a[n].indent==before.indent);
    }
    // The conditional VIDEO branch must still have its original
    // body delimiter. Without this, Video fails to open and choosing
    // Son/Contrôles/Langue can instead open the new page.
    require(a[10].parameter==8u && a[10].indent==2u &&
            a[10].type==0u);
    require(a[11].parameter==70u && a[11].indent==3u);
    require(a[13].parameter==69u && a[13].indent==3u);
    require(a[15].parameter==0u && a[15].indent==2u &&
            a[15].type==0u);
    require(a[kStandalonePreparationNewNodes-1u].type==8u);
    require(!standalone_skip_legacy_video_open(a.data(),1383u,
        hash)); // not valid twice
    for(unsigned changed : {7u,518u,520u,521u,522u}) {
        auto corrupt=old;
        corrupt[changed].parameter^=1u;
        require(!standalone_skip_legacy_video_open(corrupt.data(),
            unsigned(corrupt.size()),hash));
        require(corrupt[518].parameter==
                (changed==518u?71u:70u)); // no half-patch
    }
    require(!standalone_skip_legacy_video_open(nullptr,1383u,
        hash));
    auto unchanged=old;
    require(!standalone_skip_legacy_video_open(unchanged.data(),100u,
        hash));
    return 0;
}
