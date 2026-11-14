#include "rayman2fix/screen_camera.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>
#include <cstdint>
#include <array>
#include <cstring>
int main() {
    const auto near=[](float a,float b){return std::fabs(a-b)<0.0001f;};
    int overlay=1,world=2;
    assert(r2fix::is_native_screen_camera(&overlay,&overlay));
    assert(!r2fix::is_native_screen_camera(&world,&overlay));
    assert(!r2fix::is_native_screen_camera(nullptr,&overlay));
    assert(!r2fix::is_native_screen_camera(&overlay,nullptr));
    using Role=r2fix::RenderCameraRole;
    assert(r2fix::classify_render_camera(&overlay,&overlay)==Role::ScreenWorld);
    assert(r2fix::classify_render_camera(&world,&overlay)==Role::World);
    assert(r2fix::classify_render_camera(&world,nullptr)==Role::Unknown);
    assert(r2fix::classify_render_camera(nullptr,&overlay)==Role::Unknown);
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
    // Original engine viewport is 1024x768, but the horizontal
    // output geometry has to match the centered 4:3 HUD canvas.
    const r2fix::ScreenWorldViewport full{1024,768,0,0};
    r2fix::ScreenWorldViewport narrow{};
    assert(r2fix::center_screen_world_viewport(full,0.75f,narrow));
    assert(narrow.width==768 && narrow.height==768);
    assert(narrow.left==128 && narrow.top==0);
    assert(near(narrow.left+240.0f/1024.0f*narrow.width,
                128.0f+240.0f*0.75f));
    assert(near(narrow.left+800.0f/1024.0f*narrow.width,
                128.0f+800.0f*0.75f));
    const r2fix::ScreenWorldViewport offset{1024,768,50,23};
    assert(r2fix::center_screen_world_viewport(offset,.75f,narrow));
    assert(narrow.width==768 && narrow.height==768 &&
           narrow.left==178 && narrow.top==23);
    assert(r2fix::center_screen_world_viewport(
        full,(4.0f/3.0f)/(21.0f/9.0f),narrow));
    assert(narrow.width>=584 && narrow.width<=586);
    assert(narrow.left>=219 && narrow.left<=220);
    assert(!r2fix::center_screen_world_viewport(full,1.0f,narrow));
    assert(!r2fix::center_screen_world_viewport(full,0.0f,narrow));
    assert(!r2fix::center_screen_world_viewport(full,
        std::numeric_limits<float>::quiet_NaN(),narrow));
    assert(!r2fix::center_screen_world_viewport(full,
        std::numeric_limits<float>::infinity(),narrow));
    const r2fix::ScreenWorldViewport invalid{64,768,0,0};
    assert(!r2fix::center_screen_world_viewport(invalid,.75f,narrow));
    const r2fix::ScreenWorldViewport giant{30000,768,0,0};
    assert(!r2fix::center_screen_world_viewport(giant,.75f,narrow));
    assert(full.width==1024 && full.height==768 &&
           full.left==0 && full.top==0); // never modify source
    // Regression test using the actual raw 0x70-byte GLI struct.
    // At 1920x1200 the earlier (wrong) offsets logged 768x1024;
    // the true engine internal viewport is width=1024,height=768.
    std::array<std::uint8_t,r2fix::kGLIViewportBytes> original{};
    original.fill(0xA6);
    const std::int32_t W=1024,H=768,X=0,Y=23;
    std::memcpy(original.data()+0x08,&H,sizeof(H));
    std::memcpy(original.data()+0x0C,&W,sizeof(W));
    std::memcpy(original.data()+0x58,&X,sizeof(X));
    std::memcpy(original.data()+0x5C,&Y,sizeof(Y));
    const auto r=r2fix::read_native_screen_viewport(original.data());
    assert(r.width==1024 && r.height==768 && r.left==0 && r.top==23);
    auto shadow=original;
    r2fix::ScreenWorldViewport adjusted{};
    assert(r2fix::center_screen_world_viewport(r,0.75f,adjusted));
    r2fix::write_centered_native_viewport(shadow.data(),adjusted);
    const auto parsed=r2fix::read_native_screen_viewport(shadow.data());
    assert(parsed.width==768 && parsed.left==128);
    assert(parsed.height==768 && parsed.top==23);
    for(std::size_t i=0;i<original.size();++i) {
        const bool width_field=(i>=0x0C && i<0x10);
        const bool x_field=(i>=0x58 && i<0x5C);
        if(!width_field && !x_field)assert(original[i]==shadow[i]);
    }
    assert(r2fix::read_native_screen_viewport(original.data()).width==1024);
    assert(r2fix::center_screen_world_viewport(
        r,(4.0f/3.0f)/(16.0f/10.0f),adjusted));
    assert(adjusted.width==853 && adjusted.left==85);
    assert(adjusted.height==768 && adjusted.top==23);
    return 0;
}
