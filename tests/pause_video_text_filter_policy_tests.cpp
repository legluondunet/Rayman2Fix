#include "rayman2fix/pause_video_text_filter_policy.hpp"
#include <cstdlib>
#include <limits>
using namespace r2fix;
static void ck(bool b){if(!b)std::abort();}
int main(){
    for(unsigned route:{2u,3u}){
        for(float y:{0.3f,0.42f,0.55f,0.8f,0.91f}){
            ck(pause_video_hide_obsolete_native_text(true,true,route,y));
            ck(!pause_video_hide_obsolete_native_text(false,true,route,y));
            ck(!pause_video_hide_obsolete_native_text(true,false,route,y));
        }
        for(float y:{-10.0f,0.0f,0.2f,0.28f,0.92f,1.5f}){
            ck(!pause_video_hide_obsolete_native_text(true,true,route,y));
        }
    }
    for(unsigned route:{0u,1u,4u,100u})
        ck(!pause_video_hide_obsolete_native_text(true,true,route,0.5f));
    ck(!pause_video_hide_obsolete_native_text(true,true,2u,
        std::numeric_limits<float>::quiet_NaN()));
    return 0;
}
