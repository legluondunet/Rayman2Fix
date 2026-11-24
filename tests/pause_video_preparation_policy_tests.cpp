#include "rayman2fix/pause_video_preparation_policy.hpp"
#include <array>
#include <cstdlib>
using namespace r2fix;
static void check(bool b) {if(!b)std::abort();}
int main() {
    static_assert(kPauseVideoPrepNewNodes==816u);
    std::array<NativeMenuScriptNode,kPauseVideoPrepOriginalNodes> a{};
    for(auto& n:a)n={0u,0u,3u,12u};
    a[0]={0u,0u,1u,0u};
    a[1]={45u,0u,2u,1u};
    a[4]={0u,0u,2u,5u};
    a[7]={4u,0u,3u,1u};
    a[8]={16u,0u,4u,11u};
    a[9]={0u,0u,4u,12u};
    a[10]={8u,0u,2u,0u};
    a[49]={379u,0u,4u,27u};
    a[79]={371u,0u,4u,27u};
    a[498]={70u,0u,3u,4u};
    a[499]={36447912u,0u,4u,28u};
    a[500]={69u,0u,3u,4u};
    a[501]={36319188u,0u,4u,28u};
    a[502]={0u,0u,2u,0u};
    a[503]={4u,0u,3u,1u};
    a[504]={16u,0u,4u,11u};
    a[505]={1u,0u,4u,12u};
    a.back()={0u,0u,0u,8u};
    const auto original=a;
    const auto hash=pause_video_prep_script_shape(a.data(),a.size());
    check(hash!=0u);
    check(pause_skip_obsolete_video_preparation(a.data(),a.size(),hash));
    for(unsigned i=0u;i<kPauseVideoPrepBranchStart;++i) {
        check(a[i].parameter==original[i].parameter);
        check(a[i].type==original[i].type);
        check(a[i].indent==original[i].indent);
    }
    for(unsigned i=kPauseVideoPrepBranchStart;i<kPauseVideoPrepNewNodes;++i) {
        const auto& n=original[i+kPauseVideoPrepTransfer-kPauseVideoPrepBranchStart];
        check(a[i].parameter==n.parameter);
        check(a[i].type==n.type);
        check(a[i].indent==n.indent);
        check(a[i].padding==n.padding);
    }
    check(a[11].type==4u && a[11].parameter==70u);
    check(a[13].type==4u && a[13].parameter==69u);
    check(a[15].type==0u && a[15].parameter==0u);
    check(a[kPauseVideoPrepNewNodes-1u].type==8u);
    check(!pause_skip_obsolete_video_preparation(a.data(),a.size(),hash));
    for(unsigned k:{7u,8u,9u,10u,49u,498u,499u,500u,501u,502u,503u,504u,505u}) {
        auto invalid=original;
        invalid[k].parameter^=1u;
        check(!pause_skip_obsolete_video_preparation(
            invalid.data(),invalid.size(),hash));
    }
    auto bad=original;
    bad[501].parameter=0xFFFFFFFFu;
    check(!pause_skip_obsolete_video_preparation(bad.data(),bad.size(),hash));
    check(!pause_skip_obsolete_video_preparation(nullptr,1303u,hash));
    check(!pause_skip_obsolete_video_preparation(bad.data(),100u,hash));
    return 0;
}
