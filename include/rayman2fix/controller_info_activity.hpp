#pragma once
#include "rayman2fix/gamepad_input_policy.hpp"
#include "rayman2fix/controller_info_composite.hpp"
namespace r2fix {
// Read-only physical input for the diagram, independent of gameplay actions.
enum PadLight : unsigned { LightSouth,LightEast,LightWest,LightNorth,LightStart,LightBack,
    LightLB,LightRB,LightLT,LightRT,LightUp,LightDown,LightLeft,LightRight,LightLS,LightRS,LightCount };
struct PadInfoActivity { unsigned mask=0; };
constexpr PadInfoActivity pad_info_activity(const PadSample& s) noexcept {
    if(!s.connected||!s.device_id||!s.foreground||s.context!=PadContext::controller_info)return {};
    const bool bits[]{s.south,s.east,s.west,s.north,s.start,s.back_display,
        s.left_shoulder,s.right_shoulder,s.left_trigger>kPadTriggerThreshold,
        s.right_trigger>kPadTriggerThreshold,s.dpad_up,s.dpad_down,s.dpad_left,s.dpad_right,
        s.left_click_display||s.left_x>kPadAxisDeadzone||s.left_x<-kPadAxisDeadzone||
        s.left_y>kPadAxisDeadzone||s.left_y<-kPadAxisDeadzone,
        s.right_click_display||s.right_x>kPadAxisDeadzone||s.right_x<-kPadAxisDeadzone||
        s.right_y_display>kPadAxisDeadzone||s.right_y_display<-kPadAxisDeadzone};
    PadInfoActivity a{};
    for(unsigned i=0;i<LightCount;++i)if(bits[i])a.mask|=1u<<i;
    return a;
}
struct PadLightShape {PadInfoPoint center;int rx,ry;};
constexpr PadLightShape pad_light_shape(PadLabelFamily family,unsigned index) noexcept {
    const auto h=pad_hardware_layout(family);
    if(index<4)return {h.face[index],16,16};
    switch(index){
    case LightStart:return {h.start,21,12};
    case LightBack:return {h.back,21,12};
    case LightLB:return {{330,125},25,14};
    case LightRB:return {{590,125},25,14};
    case LightLT:return {{338,94},24,14};
    case LightRT:return {{582,94},24,14};
    case LightUp:return {{h.dpad.x,h.dpad.y-22},10,9};
    case LightDown:return {{h.dpad.x,h.dpad.y+22},10,9};
    case LightLeft:return {{h.dpad.x-22,h.dpad.y},9,10};
    case LightRight:return {{h.dpad.x+22,h.dpad.y},9,10};
    case LightLS:return {h.left_stick,27,27};
    case LightRS:return {h.right_stick,27,27};
    default:return {{0,0},0,0};
    }
}
constexpr std::uint16_t pad_light_tint(std::uint16_t dst,unsigned strength) noexcept {
    const unsigned b=((dst>>11)&31u)*255u/31u,g=((dst>>5)&63u)*255u/63u,r=(dst&31u)*255u/31u;
    const unsigned rr=pad_tint(r,230,strength),gg=pad_tint(g,255,strength),bb=pad_tint(b,149,strength);
    return std::uint16_t(((bb*31u+127u)/255u)<<11|((gg*63u+127u)/255u)<<5|((rr*31u+127u)/255u));
}
inline bool apply_pad_info_lights(std::uint16_t* dst,std::size_t count,unsigned width,
                                  PadLabelFamily family,PadInfoActivity activity) noexcept {
    if(!dst||!width||width>kPadInfoWidth||count!=std::size_t(width)*kPadInfoHeight)return false;
    for(unsigned i=0;i<LightCount;++i)if(activity.mask&(1u<<i)){
        const auto q=pad_light_shape(family,i);
        const float scale=float(width)/float(kPadInfoWidth);
        const float cx=float(q.center.x)*scale,rx=float(q.rx)*scale,cy=float(q.center.y),ry=float(q.ry);
        const int x0=std::max(0,int(cx-rx-2)),x1=std::min(int(width)-1,int(cx+rx+2));
        const int y0=std::max(0,int(cy-ry-2)),y1=std::min(kPadInfoHeight-1,int(cy+ry+2));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            const float dx=(float(x)+.5f-cx)/rx,dy=(float(y)+.5f-cy)/ry;
            const float qd=dx*dx+dy*dy;
            if(qd>1.12f)continue;
            const unsigned alpha=qd>.72f?unsigned(190.f*std::min(1.f,(1.12f-qd)*5.f)):38u;
            auto& pixel=dst[std::size_t(y)*width+unsigned(x)];
            pixel=pad_light_tint(pixel,alpha);
        }
    }
    return true;
}
} // namespace r2fix
