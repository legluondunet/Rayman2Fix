#pragma once
// Original vector art for Rayman2Fix's enchanted controller page.
// All physical control coordinates are shared with the layout contract.
// Build-time original artwork source; the runtime loads three transparent HD PNGs.
#include "rayman2fix/controller_info_layout.hpp"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
namespace r2fix::controller_art {
using namespace Gdiplus;
inline Color rgb(unsigned r,unsigned g,unsigned b,unsigned a=255) {
    return Color(BYTE(a),BYTE(r),BYTE(g),BYTE(b));
}
inline void disk(Graphics& g,float x,float y,float r,Color c) {
    SolidBrush brush(c);g.FillEllipse(&brush,x-r,y-r,r*2,r*2);
}
inline void ring(Graphics& g,float x,float y,float r,Color c,float width=1) {
    Pen p(c,width);g.DrawEllipse(&p,x-r,y-r,r*2,r*2);
}
inline void curve(Graphics& g,Color c,float width,PointF a,PointF b,PointF d,PointF e) {
    Pen p(c,width);p.SetStartCap(LineCapRound);p.SetEndCap(LineCapRound);
    g.DrawBezier(&p,a,b,d,e);
}
inline void round_path(GraphicsPath& p,float x,float y,float w,float h,float r) {
    const float z=2*r;
    p.AddArc(x,y,z,z,180,90);p.AddArc(x+w-z,y,z,z,270,90);
    p.AddArc(x+w-z,y+h-z,z,z,0,90);p.AddArc(x,y+h-z,z,z,90,90);
    p.CloseFigure();
}
inline void pill(Graphics& g,float x,float y,float w,float h,float radius,Color top,Color bottom) {
    GraphicsPath p;round_path(p,x,y,w,h,radius);
    LinearGradientBrush b(PointF(x,y),PointF(x,y+h),top,bottom);
    g.FillPath(&b,&p);Pen rim(rgb(54,19,83),1.5f);g.DrawPath(&rim,&p);
    curve(g,rgb(242,199,255,150),1.2f,{x+radius,y+2},{x+w*.35f,y+.8f},
          {x+w*.65f,y+.8f},{x+w-radius,y+2});
}
inline void spiral(Graphics& g,float x,float y,float radius,Color color,float width,bool mirror=false) {
    PointF pts[65]{};
    constexpr float pi=3.14159265359f;
    for(unsigned i=0;i<65;++i) {
        const float t=float(i)/64.f, angle=t*2.15f*pi;
        const float rad=radius*(1.f-t);
        pts[i]={x+(mirror?-1.f:1.f)*rad*std::cos(angle),y+rad*std::sin(angle)};
    }
    Pen pen(color,width);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);
    g.DrawLines(&pen,pts,65);
}
inline void gem(Graphics& g,float x,float y,float r,Color top,Color bottom) {
    disk(g,x,y+2,r+2,rgb(39,12,56,210));
    LinearGradientBrush b(PointF(x,y-r),PointF(x,y+r),top,bottom);
    g.FillEllipse(&b,x-r,y-r,r*2,r*2);
    GraphicsPath dome;dome.AddEllipse(x-r,y-r,r*2,r*2);
    PathGradientBrush gloss(&dome);Color dark=rgb(28,8,47,175);INT count=1;
    gloss.SetSurroundColors(&dark,&count);gloss.SetCenterPoint(PointF(x-r*.32f,y-r*.42f));
    gloss.SetCenterColor(rgb(255,255,239,100));g.FillPath(&gloss,&dome);
    ring(g,x,y,r,rgb(211,156,244),.9f);
    SolidBrush shine(rgb(255,255,245,110));
    g.FillEllipse(&shine,x-r*.56f,y-r*.72f,r*.78f,r*.38f);
}
inline void shell(Graphics& g,PadLabelFamily family) {
    const bool xbox=family==PadLabelFamily::xbox;
    GraphicsPath p;
    // Slightly different body/shoulder shape; controls stay at exact anchors.
    const bool ps=family==PadLabelFamily::playstation;
    const float shoulder=xbox?112.f:ps?110.f:115.f;
    p.AddBezier(PointF{292,216},PointF{291,161},PointF{305,shoulder+20},PointF{335,shoulder} );
    p.AddBezier(PointF{335,shoulder},PointF{365,shoulder-9},PointF{382,120},PointF{411,124} );
    p.AddBezier(PointF{411,124},PointF{438,130},PointF{482,130},PointF{509,124} );
    p.AddBezier(PointF{509,124},PointF{538,120},PointF{555,shoulder-9},PointF{585,shoulder} );
    p.AddBezier(PointF{585,shoulder},PointF{615,shoulder+20},PointF{629,161},PointF{628,216} );
    p.AddBezier(PointF{628,216},PointF{ps?644.f:641.f,268},PointF{ps?641.f:642.f,ps?337.f:322.f},PointF{615,ps?348.f:342.f} );
    p.AddBezier(PointF{615,ps?348.f:342.f},PointF{588,ps?363.f:361.f},PointF{567,329},PointF{549,310} );
    p.AddBezier(PointF{549,310},PointF{531,293},PointF{508,299},PointF{487,300} );
    p.AddBezier(PointF{487,300},PointF{468,301},PointF{452,301},PointF{433,300} );
    p.AddBezier(PointF{433,300},PointF{412,299},PointF{389,293},PointF{371,310} );
    p.AddBezier(PointF{371,310},PointF{353,329},PointF{332,ps?363.f:361.f},PointF{305,ps?348.f:342.f} );
    p.AddBezier(PointF{305,ps?348.f:342.f},PointF{ps?279.f:278.f,ps?337.f:322.f},PointF{ps?276.f:279.f,268},PointF{292,216} );
    p.CloseFigure();
    // Glossy golden-yellow shell; full-height opaque gradient prevents a tiled seam.
    Pen shadow(rgb(62,30,8,175),7);shadow.SetLineJoin(LineJoinRound);g.DrawPath(&shadow,&p);
    // The brush spans the entire bounding box on a vertical axis. Never
    // use a short diagonal gradient which repeats on the lower right grip.
    SolidBrush base(rgb(202,133,19));g.FillPath(&base,&p);
    LinearGradientBrush surface(RectF(270,95,380,270),rgb(255,215,70),rgb(193,108,10),LinearGradientModeVertical);
    Color colors[]={rgb(226,153,23),rgb(255,218,72),rgb(255,237,124),rgb(208,124,13)};
    REAL stops[]={0,.23f,.64f,1};surface.SetInterpolationColors(colors,stops,4);
    g.FillPath(&surface,&p);
    auto saved=g.Save();g.SetClip(&p,CombineModeIntersect);
    // Soft light across the sculpted plastic, with warm amber perimeter.
    PathGradientBrush volume(&p); Color edge=rgb(141,65,8,180); INT n=1;
    volume.SetSurroundColors(&edge,&n);volume.SetCenterColor(rgb(255,226,93,195));
    volume.SetCenterPoint(PointF(467,221));volume.SetFocusScales(.38f,.25f);
    g.FillPath(&volume,&p);
    const auto hw=pad_hardware_layout(family);
    // Soft occlusion shadows beneath every physical control.
    auto recess=[&](float x,float y,float radius){
        for(int i=8;i>=1;--i)disk(g,x+1,y+4,radius+float(i)*1.5f,rgb(0,20,53,10+unsigned(8-i)*2));
    };
    recess(float(hw.left_stick.x),float(hw.left_stick.y),26);
    recess(float(hw.right_stick.x),float(hw.right_stick.y),26);
    recess(float(hw.dpad.x),float(hw.dpad.y),31);
    for(auto f:hw.face)recess(float(f.x),float(f.y),12);
    // Sculpted outer grip shading, soft central glow, and painted curls.
    for(int w=34;w>=4;w-=5) {
        const unsigned alpha=unsigned(7+(34-w)/6);
        curve(g,rgb(151,76,7,alpha),float(w),{301,164},{278,223},{299,303},{313,340});
        curve(g,rgb(151,76,7,alpha),float(w),{619,164},{642,223},{621,303},{607,340});
    }
    for(int w=38;w>=8;w-=6)
        curve(g,rgb(255,248,172,12),float(w),{388,257},{437,215},{472,200},{514,177});
    curve(g,rgb(255,246,177,155),6,{309,184},{317,146},{350,129},{381,132});
    curve(g,rgb(255,233,147,125),5,{539,132},{574,124},{601,145},{611,181});
    curve(g,rgb(255,252,217,155),2,{313,165},{325,139},{356,122},{391,128});
    curve(g,rgb(255,247,194,135),2,{529,128},{565,116},{596,133},{608,164});
    // Sweeping spiral decals on both grips; keep the central shell clean.
    spiral(g,324,295,27,rgb(92,148,34,220),8.5f);
    spiral(g,596,295,27,rgb(92,148,34,220),8.5f,true);
    curve(g,rgb(255,226,133,120),2,{390,298},{431,290},{489,290},{530,298});
    g.Restore(saved);
    Pen rim(rgb(255,218,95),1.1f);rim.SetLineJoin(LineJoinRound);g.DrawPath(&rim,&p);
}
inline void draw(Graphics& g,PadLabelFamily family) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    for(auto r:{PadInfoRect{315,81,361,108},PadInfoRect{559,81,605,108},
                PadInfoRect{307,112,354,139},PadInfoRect{566,112,613,139}})
        pill(g,float(r.left),float(r.top),float(r.right-r.left),float(r.bottom-r.top),
             9,rgb(167,93,211),rgb(70,25,112));
    shell(g,family);
    // Front bumpers remain visible over the shell, triggers behind.
    for(auto r:{PadInfoRect{307,112,354,139},PadInfoRect{566,112,613,139}})
        pill(g,float(r.left),float(r.top),float(r.right-r.left),float(r.bottom-r.top),
             9,rgb(184,109,228),rgb(78,31,128));
    const auto hw=pad_hardware_layout(family);
    for(auto stick:{hw.left_stick,hw.right_stick}) {
        const float x=float(stick.x),y=float(stick.y);
        disk(g,x,y+2,28,rgb(76,36,26));
        ring(g,x,y,26,rgb(150,194,49),2);
        disk(g,x,y,23,rgb(52,20,75));
        gem(g,x,y,19,rgb(179,108,222),rgb(78,27,125));
        spiral(g,x,y,13,rgb(222,169,255,200),1.7f);
    }
    const float x=float(hw.dpad.x),y=float(hw.dpad.y);
    disk(g,x,y,33,rgb(142,85,21));
    ring(g,x,y,32,rgb(87,43,15),1.6f);
    GraphicsPath cross;
    PointF points[]={{x-10,y-30},{x+10,y-30},{x+10,y-10},{x+30,y-10},
      {x+30,y+10},{x+10,y+10},{x+10,y+30},{x-10,y+30},{x-10,y+10},
      {x-30,y+10},{x-30,y-10},{x-10,y-10}};
    cross.AddPolygon(points,12);
    LinearGradientBrush cross_fill(PointF(x,y-30),PointF(x,y+30),rgb(171,99,211),rgb(65,21,105));
    g.FillPath(&cross_fill,&cross);
    curve(g,rgb(240,191,255,175),1.2f,{x-8,y-28},{x-3,y-29},{x+3,y-29},{x+8,y-28});
    Pen cp(rgb(55,17,85),1.7f);cp.SetLineJoin(LineJoinRound);g.DrawPath(&cp,&cross);
    if(family==PadLabelFamily::playstation) {
        pill(g,425,117,70,29,5,rgb(148,79,192),rgb(66,22,101));
        for(int j=122;j<142;j+=4)for(int i=431;i<491;i+=4)disk(g,float(i),float(j),.6f,rgb(203,139,240));
    }else {
        gem(g,460,131,11,rgb(174,222,72),rgb(55,113,26));
        spiral(g,460,131,6,rgb(255,254,225),1.2f);
    }
    for(auto key:{hw.back,hw.start})
        pill(g,float(key.x-19),float(key.y-10),39,21,9,rgb(163,87,206),rgb(69,24,112));
    for(unsigned i=0;i<4;++i) {
        const auto a=hw.face[i];
        gem(g,float(a.x),float(a.y),13,
            rgb(170,97,218),rgb(68,22,111));
    }
}
inline bool paint(HDC target,PadLabelFamily family) noexcept {
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return false;
    struct Shutdown {ULONG_PTR token;~Shutdown(){GdiplusShutdown(token);}} shutdown{token};
    constexpr int w=kPadInfoWidth*2,h=kPadInfoHeight*2;
    BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=w;bmi.bmiHeader.biHeight=-h;
    bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HDC mem=CreateCompatibleDC(target);
    if(!mem)return false;
    HBITMAP dib=CreateDIBSection(mem,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dib||!bits){if(dib)DeleteObject(dib);DeleteDC(mem);return false;}
    auto old=SelectObject(mem,dib);
    bool ok=old&&old!=HGDI_ERROR;
    if(ok) {
        Bitmap bitmap(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
        Graphics g(&bitmap);
        ok=bitmap.GetLastStatus()==Ok&&g.GetLastStatus()==Ok&&
           g.Clear(Color(0,0,0,0))==Ok&&g.ScaleTransform(2,2)==Ok;
        if(ok){draw(g,family);ok=g.GetLastStatus()==Ok;g.Flush(FlushIntentionSync);}
    }
    if(ok) {
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        ok=AlphaBlend(target,0,0,kPadInfoWidth,kPadInfoHeight,mem,0,0,w,h,blend)!=FALSE;
    }
    if(old&&old!=HGDI_ERROR)SelectObject(mem,old);
    DeleteObject(dib);DeleteDC(mem);
    return ok;
}
} // namespace r2fix::controller_art
