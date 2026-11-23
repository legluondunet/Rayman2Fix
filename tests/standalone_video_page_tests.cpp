#include "rayman2fix/standalone_video_page.hpp"
#include <cstdlib>
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
    return 0;
}
