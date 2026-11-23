#include "rayman2fix/standalone_video_page.hpp"
#include <cstdlib>
#include <initializer_list>
#include <array>
#include "rayman2fix/native_video_disable_choices.hpp"
using namespace r2fix;
static void check(bool yes){if(!yes)std::abort();}
int main() {
    check(standalone_video_page_state(true,true,true,true,5,0));
    for(unsigned missing=0;missing<4;++missing)
        check(!standalone_video_page_state(missing!=0,missing!=1,
                                           missing!=2,missing!=3,5,0));
    for(int n: {1,3,11,13,18,19})
        check(!standalone_video_page_state(true,true,true,true,n,0));
    check(!standalone_video_page_state(true,true,true,true,5,5));
    std::array<NativeMenuScriptNode,430> a{};
    a[0]={0u,0u,1u,0u};
    a[1]={45u,0u,2u,1u};
    a[16]={506u,0u,3u,4u};
    a.back()={0u,0u,0u,8u};
    check(standalone_remove_legacy_video_widgets(a.data(),3u,430u));
    check(a[0].type==8u);
    std::array<NativeMenuScriptNode,517> b{};
    b[0]={4u,0u,1u,0u};
    b[1]={1u,0u,2u,12u};
    b[3]={503u,0u,2u,4u};
    b[15]={503u,0u,2u,4u};
    b[26]={21u,0u,3u,12u};
    b[27]={11u,0u,2u,2u};
    b.back()={0u,0u,0u,8u};
    check(standalone_remove_legacy_video_widgets(b.data(),4u,517u));
    check(b[27].type==8u && b[3].parameter==503u);
    b[27]={11u,0u,2u,2u};
    b[3].parameter=357u;
    check(!standalone_remove_legacy_video_widgets(b.data(),4u,517u));
    check(b[27].type==2u);
    check(!standalone_remove_legacy_video_widgets(a.data(),1u,430u));
    return 0;
}
