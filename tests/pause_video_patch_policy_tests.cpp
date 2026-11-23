#include "rayman2fix/pause_video_patch_policy.hpp"
#include <vector>
#include <cstdlib>
using namespace r2fix;
static void check(bool ok) {if(!ok)std::abort();}
static void run(unsigned k) {
    auto p=kPauseVideoPlans[k];
    std::vector<NativeMenuScriptNode> x(p.count);
    for(auto& n:x)n={0u,0u,2u,12u};
    x.back()={0u,0u,0u,8u};
    if(p.behavior==9u && p.script==1u) {
        x[141]={11u,0u,2u,2u};
        x[142]={17u,0u,3u,11u};
        x[143]={3u,0u,3u,12u};
        x[144]={11u,0u,2u,2u};
        x[145]={16u,0u,3u,11u};
        x[146]={0u,0u,3u,12u};
        for(unsigned k=0u;k<2u;++k) {
            const unsigned i=157u+k*12u;
            x[i]={503u,0u,2u,4u};
            x[i+1u]={k?120u:50u,0u,3u,12u};
            x[i+2u]={0u,0u,3u,16u};
            x[i+6u]={0u,0u,3u,16u};
            for(unsigned j:{3u,4u,5u,7u,8u,9u})
                x[i+j]={0u,0u,4u,13u};
            x[i+10u]={0x2175e80u+4u*k,0u,3u,32u};
            x[i+11u]={3u,0u,3u,12u};
        }
    } else if(p.behavior==9u && p.script==2u) {
        x[0]={0u,0u,1u,0u};
        x[1]={45u,0u,2u,1u};
        for(unsigned i:{40u,50u,60u})x[i]={356u,0u,3u,4u};
        x[71]={379u,0u,3u,27u};
        x[101]={371u,0u,3u,27u};
        x[422]={261u,0u,3u,3u};
    } else if(p.behavior==9u && p.script==3u) {
        x[0]={0u,0u,1u,0u};
        x[1]={4u,0u,2u,1u};
        x[2]={16u,0u,3u,11u};
        x[3]={5u,0u,3u,12u};
        x[29]={71u,0u,2u,4u};
    } else if(p.behavior==9u && p.script==4u) {
        x[0]={3u,0u,1u,0u};
        x[3]={503u,0u,2u,4u};
        x[15]={503u,0u,2u,4u};
        x[27]={11u,0u,2u,2u};
        x[34]={379u,0u,3u,27u};
    } else {
        x[0]={0u,0u,1u,0u};
        x[1]={45u,0u,2u,1u};
        x[6]={356u,0u,2u,4u};
    }
    p.shape=native_video_script_shape_hash(x.data(),p.count);
    auto changed=x;
    check(rewrite_pause_video_script(changed.data(),p));
    if(p.behavior==9u && p.script==1u) {
        // Preserve V17=3 and V16=0, whose original values control
        // return to parent Options. Only remove two complete SetAGO
        // subtrees and compact the remainder of the script.
        check(changed[143].parameter==3u && changed[146].parameter==0u);
        for(unsigned i=0;i<157u;++i)
            check(changed[i].parameter==x[i].parameter &&
                  changed[i].type==x[i].type);
        for(unsigned i=157u;i<280u;++i)
            check(changed[i].parameter==x[i+24u].parameter &&
                  changed[i].type==x[i+24u].type);
        check(changed[279].type==8u && changed[279].indent==0u);
        for(unsigned i=280u;i<changed.size();++i)
            check(changed[i].type==8u && changed[i].indent==0u);
    } else {
        const unsigned stop=p.behavior==9u && p.script==4u?27u:0u;
        check(changed[stop].type==8u && changed[stop].indent==0u);
        if(stop==27u)for(unsigned i=0;i<27u;++i)
            check(changed[i].parameter==x[i].parameter &&
                  changed[i].type==x[i].type);
    }
    auto bad=x;
    bad[0].padding=1u;
    check(!rewrite_pause_video_script(bad.data(),p));
    check(!rewrite_pause_video_script(nullptr,p));
    bad=x;p.shape^=1u;
    check(!rewrite_pause_video_script(bad.data(),p));
}
int main() {
    for(unsigned k=0u;k<5u;++k)run(k); // preserve native V16 and ESC
    return 0;
}
