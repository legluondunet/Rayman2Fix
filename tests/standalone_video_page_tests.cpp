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
    // Source-scoped policy, not screen coordinates: never suppress
    // fonts outside the standalone page, on a failed LFB renderer,
    // or for non-glyph native 2D producers (butterfly and background).
    check(standalone_video_hide_native_glyphs(true,true,true,false));
    check(!standalone_video_hide_native_glyphs(false,true,true,false));
    check(!standalone_video_hide_native_glyphs(true,false,true,false));
    check(!standalone_video_hide_native_glyphs(true,true,false,false));
    check(standalone_video_native_title_glyph(35.3f,20.0f,39.5f,25.0f));
    check(!standalone_video_hide_native_glyphs(true,true,true,true));
    check(!standalone_video_native_title_glyph(35.3f,40.0f,39.5f,45.0f));
    check(!standalone_video_native_title_glyph(35.3f,58.0f,39.5f,63.0f));
    check(!standalone_video_native_title_glyph(2.0f,20.0f,8.0f,25.0f));
    check(!standalone_video_native_title_glyph(35.3f,35.0f,39.5f,33.0f));
    float x0=35.3f,y0=19.66f,x1=39.5f,y1=25.34f;
    check(standalone_video_stabilize_native_title(x0,y0,x1,y1));
    check(x0==35.3f && x1==39.5f && y0==20.f && y1==25.f);
    x0=35.3f;y0=40.f;x1=39.5f;y1=45.f;
    check(!standalone_video_stabilize_native_title(x0,y0,x1,y1));
    check(y0==40.f && y1==45.f);
    check(standalone_video_hide_native_glyphs(true,true,true,false));
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
    // Script 4 now stops before the two source SetAGO butterfly moves.
    check(b[0].type==8u && b[0].indent==0u &&
          b[3].parameter==503u && b[27].type==2u);
    b[0]={4u,0u,1u,0u};
    b[3].parameter=357u;
    check(!standalone_remove_legacy_video_widgets(b.data(),4u,517u));
    check(b[27].type==2u);
    check(!standalone_remove_legacy_video_widgets(a.data(),1u,430u));
    return 0;
}
