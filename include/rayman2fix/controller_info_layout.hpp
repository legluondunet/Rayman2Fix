#pragma once
// Pure geometry for the original controller diagram. All positions are
// in the 1024x768 native 4:3 canvas, relative to this panel.
#include <array>
#include <cmath>
#include "rayman2fix/standalone_video_layout_policy.hpp"
#include "rayman2fix/gamepad_control_info.hpp"
namespace r2fix {
struct PadInfoPoint { int x,y; };
struct PadInfoRect { int left,top,right,bottom; };
constexpr int kPadInfoWidth=920, kPadInfoHeight=420;
constexpr int kPadInfoX=52, kPadInfoY=int(kVideoInfoY);
// Same vertical origin as VIDEO; correct the physical aspect at LFB write.
struct PadInfoPlacement {unsigned x=0,width=0;};
inline PadInfoPlacement pad_info_placement(float scale) noexcept {
    if(!std::isfinite(scale)||scale<=0.25f||scale>1.f)return {};
    const auto width=unsigned(float(kPadInfoWidth)*scale+0.5f);
    return {(kVideoCanvasWidth-width)/2u,width};
}
// CONTROL has a longer native heading than VIDEO. In the original MENU
// log the first glyph is at x=19.30..23.50, y=24.97..30.03.
inline bool pad_info_native_title(bool pause,float& x0,float& y0,float& x1,float& y1) noexcept {
    if(!std::isfinite(x0)||!std::isfinite(x1)||!std::isfinite(y0)||!std::isfinite(y1)||
       x0<12.f||x1>88.f||y0<(pause?12.f:7.f)||y1>32.f||
       x1<=x0||x1-x0>10.f||y1<=y0||y1-y0>9.f)return false;
    y0=20.f;y1=25.f;return true;
}
// Candidate common header companion position, above the panel. Only
// the two established 3D menu-actor source routes and center band.
inline bool align_pad_info_companion(bool page,bool viewport,unsigned route,float x,float& y,float depth,
                                     bool pause=false) noexcept {
    if(!page||!viewport||route>1u||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(depth)||
       x<0.40f||x>0.64f||depth<0.f||depth>0.25f)return false;
    if(pause) {
        if(y<0.34f||y>0.64f)return false;
        y-=0.18f;return true;
    }
    // Original MENU animation spans the old lower header position.
    // A fixed subtraction left its center on the panel edge and
    // discontinuously stopped lifting when the actor left the old band.
    // Compress only the centered native actor's vertical travel.
    if(y<0.16f||y>0.84f)return false;
    // The 063547 capture places the butterfly across the title at y=.29.
    // Reserve a separate header band above the native title (20..25%).
    y=0.145f+(y-0.5f)*0.04f;
    return true;
}
constexpr PadInfoRect kPadInfoStatus{12,8,908,42};
constexpr PadInfoRect kPadInfoBack{675,380,908,412};
constexpr PadInfoRect kPadInfoPending{12,380,245,412};
constexpr PadInfoRect kPadInfoBody{277,80,643,350};
struct PadInfoCallout { unsigned row; PadInfoRect box; PadInfoPoint anchor; bool left; };
struct PadHardwareLayout {
    PadInfoPoint left_stick,right_stick,dpad,start,back;
    std::array<PadInfoPoint,4> face; // South, East, West, North
};
constexpr PadHardwareLayout pad_hardware_layout(PadLabelFamily family) noexcept {
    return {{family==PadLabelFamily::xbox?366:409,family==PadLabelFamily::xbox?184:271},
        {512,271},{family==PadLabelFamily::xbox?408:366,family==PadLabelFamily::xbox?271:197},
        {507,155},{413,155},{{{554,227},{587,197},{521,197},{554,167}}}};
}
constexpr std::array<PadInfoCallout,11> pad_info_callouts(PadLabelFamily family) noexcept {
    const auto hw=pad_hardware_layout(family);
    const PadInfoPoint move{hw.left_stick.x-23,hw.left_stick.y};
    const PadInfoPoint up{hw.dpad.x-10,hw.dpad.y-22};
    const PadInfoPoint down{hw.dpad.x-10,hw.dpad.y+22};
    const bool xbox=family==PadLabelFamily::xbox;
    return {{
        {6,{12,52,252,104},{315,94},true},
        {3,{12,116,252,168},{307,125},true},
        {xbox?0u:8u,{12,180,252,232},xbox?move:up,true},
        {xbox?8u:9u,{12,244,252,296},xbox?up:down,true},
        {xbox?9u:0u,{12,308,252,360},xbox?down:move,true},
        {7,{668,52,908,101},{605,94},false},
        {10,{668,104,908,153},{hw.start.x+19,hw.start.y},false},
        {4,{668,156,908,205},{hw.face[3].x+13,hw.face[3].y},false},
        {2,{668,208,908,257},{hw.face[1].x+13,hw.face[1].y},false},
        {1,{668,260,908,309},{hw.face[0].x+13,hw.face[0].y},false},
        {5,{668,312,908,361},{hw.right_stick.x+23,hw.right_stick.y},false},
    }};
}
constexpr PadInfoPoint pad_callout_end(PadInfoCallout a) noexcept {
    return {a.left?a.box.right+4:a.box.left-4,(a.box.top+a.box.bottom)/2};
}
constexpr int pad_turn(PadInfoPoint a,PadInfoPoint b,PadInfoPoint c) noexcept {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
constexpr bool pad_between(int a,int b,int c) noexcept {
    return (a<=c&&c<=b)||(b<=c&&c<=a);
}
constexpr bool pad_on_segment(PadInfoPoint a,PadInfoPoint b,PadInfoPoint c) noexcept {
    return !pad_turn(a,b,c)&&pad_between(a.x,b.x,c.x)&&pad_between(a.y,b.y,c.y);
}
constexpr bool pad_segments_intersect(PadInfoPoint a,PadInfoPoint b,
                                      PadInfoPoint c,PadInfoPoint d) noexcept {
    const int p=pad_turn(a,b,c),q=pad_turn(a,b,d),r=pad_turn(c,d,a),s=pad_turn(c,d,b);
    return ((p<0&&q>0)||(p>0&&q<0))&&((r<0&&s>0)||(r>0&&s<0)) ||
        pad_on_segment(a,b,c)||pad_on_segment(a,b,d)||pad_on_segment(c,d,a)||pad_on_segment(c,d,b);
}
constexpr bool pad_callouts_disjoint(PadLabelFamily family) noexcept {
    const auto all=pad_info_callouts(family);
    for(unsigned i=0;i<all.size();++i)for(unsigned j=0;j<i;++j)
        if(pad_segments_intersect(all[i].anchor,pad_callout_end(all[i]),
                                  all[j].anchor,pad_callout_end(all[j])))return false;
    return true;
}
constexpr bool pad_info_inside(PadInfoRect r) noexcept {
    return r.left>=0 && r.top>=0 && r.right<=kPadInfoWidth &&
        r.bottom<=kPadInfoHeight && r.left<r.right && r.top<r.bottom;
}
constexpr bool pad_info_overlap(PadInfoRect a,PadInfoRect b) noexcept {
    return a.left<b.right && b.left<a.right && a.top<b.bottom && b.top<a.bottom;
}
constexpr bool pad_info_layout_valid(PadLabelFamily family=PadLabelFamily::generic) noexcept {
    const auto callouts=pad_info_callouts(family);
    if(kPadInfoX<0 || kPadInfoY<0 || kPadInfoX+kPadInfoWidth>1024 ||
       kPadInfoY+kPadInfoHeight>768 || !pad_info_inside(kPadInfoBody))return false;
    const PadInfoRect fixed[]{kPadInfoStatus,kPadInfoBack,kPadInfoPending};
    for(unsigned i=0;i<3;++i) {
        if(!pad_info_inside(fixed[i]) || pad_info_overlap(fixed[i],kPadInfoBody))return false;
        for(unsigned j=0;j<i;++j)if(pad_info_overlap(fixed[i],fixed[j]))return false;
    }
    unsigned seen=0;
    for(unsigned i=0;i<callouts.size();++i) {
        const auto& a=callouts[i];
        if(a.row>=11 || (seen&(1u<<a.row)) || !pad_info_inside(a.box) ||
           pad_info_overlap(a.box,kPadInfoBody) || a.anchor.x<kPadInfoBody.left ||
           a.anchor.x>=kPadInfoBody.right || a.anchor.y<kPadInfoBody.top ||
           a.anchor.y>=kPadInfoBody.bottom)return false;
        seen|=1u<<a.row;
        for(auto r:fixed)if(pad_info_overlap(a.box,r))return false;
        for(unsigned j=0;j<i;++j)if(pad_info_overlap(a.box,callouts[j].box))return false;
    }
    return seen==0x7ffu && pad_callouts_disjoint(family);
}
static_assert(pad_info_layout_valid() && pad_info_layout_valid(PadLabelFamily::xbox) &&
    pad_info_layout_valid(PadLabelFamily::playstation),"all controller diagrams fit 4:3");
} // namespace r2fix
