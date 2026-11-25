#pragma once
// Pure geometry for the original controller diagram. All positions are
// in the 1024x768 native 4:3 canvas, relative to this panel.
#include <array>
#include <cmath>
#include "rayman2fix/standalone_video_layout_policy.hpp"
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
       x0<12.f||x1>88.f||y0<(pause?12.f:16.f)||y1>32.f||
       x1<=x0||x1-x0>10.f||y1<=y0||y1-y0>9.f)return false;
    y0=20.f;y1=25.f;return true;
}
// Candidate common header companion position, above the panel. Only
// the two established 3D menu-actor source routes and center band.
inline bool align_pad_info_companion(bool page,bool viewport,unsigned route,float x,float& y,float depth) noexcept {
    if(!page||!viewport||route>1u||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(depth)||
       x<0.40f||x>0.64f||y<0.34f||y>0.64f||depth<0.f||depth>0.25f)return false;
    y-=0.18f;return true;
}
constexpr PadInfoRect kPadInfoStatus{12,8,908,42};
constexpr PadInfoRect kPadInfoBack{675,380,908,412};
constexpr PadInfoRect kPadInfoPause{345,365,575,410};
constexpr PadInfoRect kPadInfoPending{12,380,245,412};
constexpr PadInfoRect kPadInfoBody{277,80,643,350};
struct PadInfoCallout { unsigned row; PadInfoRect box; PadInfoPoint anchor; bool left; };
inline constexpr std::array<PadInfoCallout,10> kPadInfoCallouts{{
    {6,{12,52,252,104},{338,94},true},
    {3,{12,116,252,168},{330,125},true},
    {8,{12,180,252,232},{366,177},true},
    {9,{12,244,252,296},{366,217},true},
    {0,{12,308,252,360},{409,271},true},
    {7,{668,52,908,104},{582,94},false},
    {4,{668,116,908,168},{554,167},false},
    {2,{668,180,908,232},{587,197},false},
    {1,{668,244,908,296},{554,227},false},
    {5,{668,308,908,360},{512,271},false},
}};
constexpr PadInfoPoint kPadInfoStart{460,218};
constexpr bool pad_info_inside(PadInfoRect r) noexcept {
    return r.left>=0 && r.top>=0 && r.right<=kPadInfoWidth &&
        r.bottom<=kPadInfoHeight && r.left<r.right && r.top<r.bottom;
}
constexpr bool pad_info_overlap(PadInfoRect a,PadInfoRect b) noexcept {
    return a.left<b.right && b.left<a.right && a.top<b.bottom && b.top<a.bottom;
}
constexpr bool pad_info_layout_valid() noexcept {
    if(kPadInfoX<0 || kPadInfoY<0 || kPadInfoX+kPadInfoWidth>1024 ||
       kPadInfoY+kPadInfoHeight>768 || !pad_info_inside(kPadInfoBody))return false;
    const PadInfoRect fixed[]{kPadInfoStatus,kPadInfoBack,kPadInfoPause,kPadInfoPending};
    for(unsigned i=0;i<4;++i) {
        if(!pad_info_inside(fixed[i]) || pad_info_overlap(fixed[i],kPadInfoBody))return false;
        for(unsigned j=0;j<i;++j)if(pad_info_overlap(fixed[i],fixed[j]))return false;
    }
    unsigned seen=1u<<10;
    for(unsigned i=0;i<kPadInfoCallouts.size();++i) {
        const auto& a=kPadInfoCallouts[i];
        if(a.row>=10 || (seen&(1u<<a.row)) || !pad_info_inside(a.box) ||
           pad_info_overlap(a.box,kPadInfoBody) || a.anchor.x<kPadInfoBody.left ||
           a.anchor.x>=kPadInfoBody.right || a.anchor.y<kPadInfoBody.top ||
           a.anchor.y>=kPadInfoBody.bottom)return false;
        seen|=1u<<a.row;
        for(auto r:fixed)if(pad_info_overlap(a.box,r))return false;
        for(unsigned j=0;j<i;++j)if(pad_info_overlap(a.box,kPadInfoCallouts[j].box))return false;
    }
    return seen==0x7ffu;
}
static_assert(pad_info_layout_valid(),"controller diagram fits 4:3 with distinct callouts");
} // namespace r2fix
