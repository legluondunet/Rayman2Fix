#include "rayman2fix/pause_video_companion_layout_policy.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <initializer_list>
using namespace r2fix;
static void check(bool b){if(!b)std::abort();}
int main(){
    for(unsigned route:{0u,1u}){
        for(float y0:{0.17f,0.30f,0.50f,0.63f}){
            float y=y0;
            check(lift_pause_video_companion(
                true,true,route,0.50f,y,0.015f));
            check(std::fabs(y-(y0-kPauseVideoCompanionLift))<0.00001f);
            y=y0;
            check(!lift_pause_video_companion(
                false,true,route,0.50f,y,0.015f));
            check(y==y0);
            check(!lift_pause_video_companion(
                true,false,route,0.50f,y,0.015f));
            check(y==y0);
        }
    }
    for(unsigned route:{2u,3u,4u,12u}) {
        float y=0.4f;
        check(!lift_pause_video_companion(
            true,true,route,0.5f,y,0.015f));
        check(y==0.4f);
    }
    for(float x:{-0.1f,0.1f,0.39f,0.65f,0.9f}){
        float y=0.3f;
        check(!lift_pause_video_companion(
            true,true,0u,x,y,0.015f));
        check(y==0.3f);
    }
    for(float y0:{-0.1f,0.0f,0.1f,0.65f,0.9f}){
        float y=y0;
        check(!lift_pause_video_companion(
            true,true,0u,0.5f,y,0.015f));
        check(y==y0);
    }
    for(float depth:{-0.1f,0.26f,1.0f}){
        float y=0.4f;
        check(!lift_pause_video_companion(
            true,true,0u,0.5f,y,depth));
        check(y==0.4f);
    }
    float nan=std::numeric_limits<float>::quiet_NaN();
    float y=0.4f;
    check(!lift_pause_video_companion(
        true,true,0u,nan,y,0.015f));
    check(!lift_pause_video_companion(
        true,true,0u,0.5f,y,nan));
    y=nan;
    check(!lift_pause_video_companion(
        true,true,0u,0.5f,y,0.015f));
    return 0;
}
