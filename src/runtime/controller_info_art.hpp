#pragma once
// Original vector art for Rayman2Fix's enchanted controller page.
// All physical control coordinates are shared with the layout contract.
// Rasterize into a private premultiplied-alpha DIB at twice logical size.
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
    g.FillPath(&b,&p);Pen rim(rgb(43,29,72),1.5f);g.DrawPath(&rim,&p);
    curve(g,rgb(235,225,255,140),1.2f,{x+radius,y+2},{x+w*.35f,y+.8f},
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
    disk(g,x,y+2,r+2,rgb(25,18,56,210));
    LinearGradientBrush b(PointF(x,y-r),PointF(x,y+r),top,bottom);
    g.FillEllipse(&b,x-r,y-r,r*2,r*2);
    ring(g,x,y,r,rgb(194,201,245),.9f);
    SolidBrush shine(rgb(255,255,245,130));
    g.FillEllipse(&shine,x-r*.56f,y-r*.72f,r*.78f,r*.38f);
}
inline void leaf(Graphics& g,float x,float y,float dx,float dy) {
    GraphicsPath p;
    p.AddBezier(PointF{x,y},PointF{x+dx*.15f-dy*.36f,y+dy*.15f+dx*.36f},
        PointF{x+dx*.7f-dy*.36f,y+dy*.7f+dx*.36f},PointF{x+dx,y+dy} );
    p.AddBezier(PointF{x+dx,y+dy},PointF{x+dx*.68f+dy*.3f,y+dy*.68f-dx*.3f},
        PointF{x+dx*.18f+dy*.2f,y+dy*.18f-dx*.2f},PointF{x,y} );p.CloseFigure();
    LinearGradientBrush b(PointF(x,y),PointF(x+dx,y+dy),rgb(32,96,87),rgb(168,222,82));
    g.FillPath(&b,&p);Pen edge(rgb(46,120,98),.8f);g.DrawPath(&edge,&p);
    curve(g,rgb(220,240,128,180),.9f,{x,y},{x+dx*.3f,y+dy*.4f},{x+dx*.7f,y+dy*.7f},{x+dx,y+dy});
}
inline void ornaments(Graphics& g) {
    // Edge-only botanical accents. Stay outside all label rectangles.
    for(int side=0;side<2;++side) {
        const float x=side?902.f:18.f,s=side?-1.f:1.f;
        curve(g,rgb(60,201,235,150),2,{x,38},{x+s*8,8},{x+s*55,8},{x+s*89,17});
        leaf(g,x+s*5,18,s*29,-10);leaf(g,x+s*27,13,s*25,13);
        disk(g,x+s*69,19,3,rgb(166,239,255,220));
        curve(g,rgb(70,205,232,150),1.7f,{x,372},{x+s*4,361},{x+s*13,367},{x+s*13,377});
        disk(g,x+s*13,370,3,rgb(158,240,255,180));
    }
    // Carved-looking name plaque; live status text is drawn later.
    pill(g,349,3,222,39,16,rgb(113,90,142),rgb(51,44,92));
    spiral(g,367,22,8,rgb(237,192,103),1.8f);
    spiral(g,553,22,8,rgb(237,192,103),1.8f,true);
    spiral(g,460,398,13,rgb(113,224,247,215),2.4f);
    curve(g,rgb(81,199,229,150),1.5f,{398,406},{422,407},{434,388},{448,399});
    curve(g,rgb(81,199,229,150),1.5f,{522,406},{498,407},{486,388},{472,399});
}
inline void shell(Graphics& g,PadLabelFamily family) {
    const bool xbox=family==PadLabelFamily::xbox;
    GraphicsPath p;
    // Slightly different body/shoulder shape; controls stay at exact anchors.
    const float shoulder=xbox?112.f:105.f;
    p.AddBezier(PointF{292,216},PointF{291,161},PointF{305,shoulder+20},PointF{335,shoulder} );
    p.AddBezier(PointF{335,shoulder},PointF{365,shoulder-9},PointF{382,120},PointF{411,124} );
    p.AddBezier(PointF{411,124},PointF{438,130},PointF{482,130},PointF{509,124} );
    p.AddBezier(PointF{509,124},PointF{538,120},PointF{555,shoulder-9},PointF{585,shoulder} );
    p.AddBezier(PointF{585,shoulder},PointF{615,shoulder+20},PointF{629,161},PointF{628,216} );
    p.AddBezier(PointF{628,216},PointF{641,268},PointF{642,322},PointF{615,342} );
    p.AddBezier(PointF{615,342},PointF{588,361},PointF{567,329},PointF{549,310} );
    p.AddBezier(PointF{549,310},PointF{531,293},PointF{508,299},PointF{487,300} );
    p.AddBezier(PointF{487,300},PointF{468,301},PointF{452,301},PointF{433,300} );
    p.AddBezier(PointF{433,300},PointF{412,299},PointF{389,293},PointF{371,310} );
    p.AddBezier(PointF{371,310},PointF{353,329},PointF{332,361},PointF{305,342} );
    p.AddBezier(PointF{305,342},PointF{278,322},PointF{279,268},PointF{292,216} );
    p.CloseFigure();
    // Soft rim light and shaded ivory/lavender body.
    Pen shadow(rgb(24,19,55,145),7);shadow.SetLineJoin(LineJoinRound);g.DrawPath(&shadow,&p);
    LinearGradientBrush surface(PointF(340,105),PointF(560,350),rgb(242,222,200),rgb(82,91,167));
    Color colors[]={rgb(244,226,208),rgb(172,159,213),rgb(225,190,169),rgb(102,111,178)};
    REAL stops[]={0,.35f,.68f,1};surface.SetInterpolationColors(colors,stops,4);
    g.FillPath(&surface,&p);
    Pen rim(rgb(116,207,239),1.6f);rim.SetLineJoin(LineJoinRound);g.DrawPath(&rim,&p);
    auto saved=g.Save();g.SetClip(&p,CombineModeIntersect);
    curve(g,rgb(255,249,230,190),2.3f,{312,165},{323,134},{356,118},{391,128});
    curve(g,rgb(247,239,255,150),2.2f,{529,128},{565,116},{596,133},{608,164});
    spiral(g,319,295,19,rgb(112,97,175,170),3.8f);
    spiral(g,601,295,19,rgb(112,97,175,170),3.8f,true);
    g.Restore(saved);
}
inline void draw(Graphics& g,PadLabelFamily family) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    ornaments(g);
    for(auto r:{PadInfoRect{315,81,361,108},PadInfoRect{559,81,605,108},
                PadInfoRect{307,112,354,139},PadInfoRect{566,112,613,139}})
        pill(g,float(r.left),float(r.top),float(r.right-r.left),float(r.bottom-r.top),
             9,rgb(115,100,171),rgb(47,35,93));
    shell(g,family);
    // Front bumpers remain visible over the shell, triggers behind.
    for(auto r:{PadInfoRect{307,112,354,139},PadInfoRect{566,112,613,139}})
        pill(g,float(r.left),float(r.top),float(r.right-r.left),float(r.bottom-r.top),
             9,rgb(129,112,182),rgb(52,39,103));
    const auto hw=pad_hardware_layout(family);
    for(auto stick:{hw.left_stick,hw.right_stick}) {
        const float x=float(stick.x),y=float(stick.y);
        disk(g,x,y+2,28,rgb(59,43,102));
        ring(g,x,y,26,rgb(247,209,174),2);
        disk(g,x,y,23,rgb(34,34,81));
        gem(g,x,y,19,rgb(83,211,234),rgb(21,88,158));
        spiral(g,x,y,13,rgb(155,239,250,200),1.7f);
    }
    const float x=float(hw.dpad.x),y=float(hw.dpad.y);
    disk(g,x,y,33,rgb(87,72,135));
    GraphicsPath cross;
    PointF points[]={{x-10,y-30},{x+10,y-30},{x+10,y-10},{x+30,y-10},
      {x+30,y+10},{x+10,y+10},{x+10,y+30},{x-10,y+30},{x-10,y+10},
      {x-30,y+10},{x-30,y-10},{x-10,y-10}};
    cross.AddPolygon(points,12);
    LinearGradientBrush cross_fill(PointF(x,y-30),PointF(x,y+30),rgb(123,101,182),rgb(46,30,91));
    g.FillPath(&cross_fill,&cross);Pen cp(rgb(36,27,78),1.7f);cp.SetLineJoin(LineJoinRound);g.DrawPath(&cp,&cross);
    if(family==PadLabelFamily::playstation) {
        pill(g,432,117,56,28,6,rgb(109,95,155),rgb(48,38,92));
        for(int j=122;j<141;j+=4)for(int i=438;i<484;i+=4)disk(g,float(i),float(j),.6f,rgb(163,150,204));
    }else {
        gem(g,460,131,11,rgb(155,251,232),rgb(37,122,128));
        spiral(g,460,131,6,rgb(255,254,225),1.2f);
    }
    for(auto key:{hw.back,hw.start})
        pill(g,float(key.x-19),float(key.y-10),39,21,9,rgb(122,107,170),rgb(50,34,96));
    const Color tops[]={rgb(177,235,82),rgb(255,127,111),rgb(100,202,255),rgb(255,225,88)};
    const Color bottoms[]={rgb(48,125,51),rgb(166,35,61),rgb(32,92,184),rgb(191,119,26)};
    for(unsigned i=0;i<4;++i) {
        const auto a=hw.face[i];
        gem(g,float(a.x),float(a.y),13,
            family==PadLabelFamily::xbox?tops[i]:rgb(125,111,174),
            family==PadLabelFamily::xbox?bottoms[i]:rgb(50,36,94));
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
