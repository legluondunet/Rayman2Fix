#include "rayman2fix/screen_camera.hpp"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    const auto near=[](float a,float b){return std::fabs(a-b)<0.0001f;};
    int overlay=1,world=2;
    assert(r2fix::is_native_screen_camera(&overlay,&overlay));
    assert(!r2fix::is_native_screen_camera(&world,&overlay));
    assert(!r2fix::is_native_screen_camera(nullptr,&overlay));
    assert(!r2fix::is_native_screen_camera(&overlay,nullptr));
    // Native screen camera created at VA 0x4052C0 with X=1.5,Y=0.75.
    // Global Hor+ used to overwrite X with 1.259 on 16:9 screens.
    constexpr float native_x=1.5f,native_y=.75f;
    const float forced=r2fix::hor_plus_fov(native_y,16.0f/9.0f);
    assert(forced>1.25f && forced<1.27f);
    for(const float ratio:{4.0f/3.0f,16.0f/9.0f,21.0f/9.0f}) {
        assert(near(r2fix::projection_alpha_x(native_x,native_y,
                          ratio,true),native_x));
    }
    // World camera must retain Hor+ and its own vertical FOV.
    assert(near(r2fix::projection_alpha_x(1.0f,.75f,16.0f/9.0f,false),
                forced));
    assert(near(r2fix::projection_alpha_x(1.0f,.75f,4.0f/3.0f,false),
                1.0f));
    assert(near(r2fix::projection_alpha_x(1.0f,.75f,1.0f,false),1.0f));
    assert(near(r2fix::projection_alpha_x(1.0f,
                       std::numeric_limits<float>::quiet_NaN(),
                       16.0f/9.0f,false),1.0f));
    return 0;
}
