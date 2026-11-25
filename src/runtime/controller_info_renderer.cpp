#include "rayman2fix/controller_info_renderer.hpp"
#ifdef _WIN32
#include <string>
#include <array>
namespace r2fix {
namespace {
constexpr COLORREF bg=RGB(15,29,52), blue=RGB(75,149,237),
                   ink=RGB(235,243,255), muted=RGB(169,197,224),
                   body=RGB(57,75,100), dark=RGB(19,29,43);
struct GdiObject {
    HGDIOBJ handle;
    ~GdiObject(){if(handle)DeleteObject(handle);}
};
struct Selection {
    HDC dc; HGDIOBJ previous;
    Selection(HDC d,HGDIOBJ h):dc(d),previous(SelectObject(d,h)){}
    ~Selection(){if(previous && previous!=HGDI_ERROR)SelectObject(dc,previous);}
};
std::wstring wide(std::string_view s) {
    if(s.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(n<=0)return {};
    std::wstring w(static_cast<std::size_t>(n),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n)!=n)return {};
    return w;
}
RECT rect(PadInfoRect r){return {r.left,r.top,r.right,r.bottom};}
bool text_fit(HDC dc,std::string_view input,PadInfoRect box,int max_px,
              int min_px,COLORREF color,bool bold,UINT align) {
    const auto s=wide(input);
    if(s.empty())return false;
    for(int px=max_px;px>=min_px;--px){
        GdiObject font{CreateFontW(-px,0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Segoe UI")};
        if(!font.handle)continue;
        Selection selected(dc,font.handle);
        if(!selected.previous || selected.previous==HGDI_ERROR)continue;
        RECT measure=rect(box);
        const UINT flags=DT_NOPREFIX|DT_WORDBREAK|align;
        const int height=DrawTextW(dc,s.c_str(),int(s.size()),&measure,flags|DT_CALCRECT);
        if(height<=0 || height>box.bottom-box.top || measure.right-measure.left>box.right-box.left)
            continue;
        RECT target=rect(box);
        target.top+=(box.bottom-box.top-height)/2;
        SetTextColor(dc,color);
        return DrawTextW(dc,s.c_str(),int(s.size()),&target,flags)>0;
    }
    return false; // never silently clip/truncate a language
}
void line(HDC dc,int x1,int y1,int x2,int y2){
    MoveToEx(dc,x1,y1,nullptr);LineTo(dc,x2,y2);
}
void circle(HDC dc,int x,int y,int radius){
    Ellipse(dc,x-radius,y-radius,x+radius+1,y+radius+1);
}
// Smooth, original silhouettes using cubic curves. Separate Xbox and
// PlayStation forms; no external artwork or trademarks in the geometry.
bool draw_shell(HDC dc,PadLabelFamily family) {
    const bool xbox=family==PadLabelFamily::xbox;
    const POINT xbox_path[]={
        {291,222},{293,171},{304,137},{329,116},
        {347,99},{378,101},{400,115},
        {422,130},{498,130},{520,115},
        {542,101},{573,99},{591,116},
        {616,137},{627,171},{629,222},
        {634,264},{633,314},{611,337},
        {592,357},{570,342},{550,321},
        {537,307},{523,300},{504,300},
        {480,300},{440,300},{416,300},
        {397,300},{383,307},{370,321},
        {350,342},{328,357},{309,337},
        {287,314},{286,264},{291,222}
    };
    const POINT ps_path[]={
        {292,219},{298,164},{309,128},{333,115},
        {357,98},{391,104},{411,117},
        {434,126},{486,126},{509,117},
        {529,104},{563,98},{587,115},
        {611,128},{622,164},{628,219},
        {638,260},{628,314},{609,336},
        {591,355},{568,346},{549,324},
        {532,305},{511,299},{489,298},
        {468,298},{452,298},{431,298},
        {409,299},{388,305},{371,324},
        {352,346},{329,355},{311,336},
        {292,314},{282,260},{292,219}
    };
    const auto* pts=xbox?xbox_path:ps_path;
    if(!BeginPath(dc))return false;
    MoveToEx(dc,pts[0].x,pts[0].y,nullptr);
    if(!PolyBezierTo(dc,pts+1,36)||!CloseFigure(dc)||!EndPath(dc)){
        AbortPath(dc);return false;
    }
    return StrokeAndFillPath(dc)!=0;
}
bool label(HDC dc,const PadInfoView& v,unsigned row,PadInfoRect r,UINT align){
    const auto& item=v.rows[row];
    std::string action(item.action);
    if(item.pending || (v.swim_pending && (row==1 || row==6)))action+=" *";
    return text_fit(dc,action,{r.left,r.top,r.right,r.top+25},19,12,ink,true,align) &&
        text_fit(dc,item.control,{r.left,r.top+25,r.right,r.bottom},15,10,muted,false,align);
}
bool face(HDC dc,PadLabelFamily family){
    const auto points=pad_hardware_layout(family).face;
    constexpr std::string_view generic[]{"S","E","W","N"};
    constexpr std::string_view xbox[]{"A","B","X","Y"};
    constexpr std::string_view ps[]{u8"\u00d7",u8"\u25cb",u8"\u25a1",u8"\u25b3"};
    bool ok=true;
    for(unsigned i=0;i<4;++i){
        const auto p=points[i];
        const COLORREF xbox_colors[]{RGB(50,129,74),RGB(166,55,58),RGB(45,98,164),RGB(171,135,40)};
        GdiObject face_fill{CreateSolidBrush(family==PadLabelFamily::xbox?xbox_colors[i]:dark)};
        if(!face_fill.handle)return false;
        Selection face_brush(dc,face_fill.handle);circle(dc,p.x,p.y,13);
        const auto symbol=family==PadLabelFamily::xbox?xbox[i]:
                          family==PadLabelFamily::playstation?ps[i]:generic[i];
        if(family==PadLabelFamily::playstation){
            GdiObject mark{CreatePen(PS_SOLID,2,ink)};
            if(!mark.handle)return false;
            Selection selected(dc,mark.handle), hollow(dc,GetStockObject(HOLLOW_BRUSH));
            if(i==0){line(dc,p.x-5,p.y-5,p.x+6,p.y+6);line(dc,p.x+5,p.y-5,p.x-6,p.y+6);}
            else if(i==1)circle(dc,p.x,p.y,6);
            else if(i==2)Rectangle(dc,p.x-6,p.y-6,p.x+7,p.y+7);
            else {POINT tri[]{{p.x,p.y-7},{p.x-7,p.y+5},{p.x+7,p.y+5},{p.x,p.y-7}};
                Polyline(dc,tri,4);}
        }else ok=text_fit(dc,symbol,{p.x-12,p.y-12,p.x+13,p.y+13},17,11,ink,true,DT_CENTER)&&ok;
    }
    return ok;
}
}
static bool draw_controller_info(HDC dc,const PadInfoView& view,COLORREF canvas,bool frame) noexcept {
    if(!dc)return false;
    const int state=SaveDC(dc);
    if(!state)return false;
    struct Restore{HDC dc;int state;~Restore(){RestoreDC(dc,state);}}restore{dc,state};
    try {
        GdiObject background{CreateSolidBrush(canvas)}, shell{CreateSolidBrush(body)},
                  button{CreateSolidBrush(dark)}, pen{CreatePen(PS_SOLID,1,blue)};
        if(!background.handle || !shell.handle || !button.handle || !pen.handle)return false;
        RECT all{0,0,kPadInfoWidth,kPadInfoHeight};
        if(!FillRect(dc,&all,static_cast<HBRUSH>(background.handle)))return false;
        Selection outline(dc,pen.handle), fill(dc,GetStockObject(HOLLOW_BRUSH));
        if(frame && !RoundRect(dc,0,0,kPadInfoWidth,kPadInfoHeight,24,24))return false;
        SetBkMode(dc,TRANSPARENT);
        const auto hw=pad_hardware_layout(view.family);
        {
            GdiObject outer{CreatePen(PS_SOLID,2,RGB(135,176,212))},
                skin{CreateSolidBrush(view.family==PadLabelFamily::xbox?RGB(65,83,108):RGB(44,56,78))};
            if(!outer.handle||!skin.handle)return false;
            Selection edge(dc,outer.handle),shell_sel(dc,skin.handle);
            if(!draw_shell(dc,view.family))return false;
        }
        // Recessed shoulder caps and subtle highlights, with the same
        // coordinates shared by the callout contract.
        {
            Selection controls(dc,button.handle);
            RoundRect(dc,315,81,361,108,14,14);RoundRect(dc,559,81,605,108,14,14);
            RoundRect(dc,307,112,354,139,12,12);RoundRect(dc,566,112,613,139,12,12);
            {
                GdiObject shine{CreatePen(PS_SOLID,1,RGB(138,164,189))};
                if(!shine.handle)return false;
                Selection hl(dc,shine.handle);
                line(dc,322,86,353,86);line(dc,566,86,597,86);
                line(dc,314,117,346,117);line(dc,573,117,605,117);
            }
            const auto d=hw.dpad;
            POINT cross[]={{d.x-10,d.y-30},{d.x+10,d.y-30},{d.x+10,d.y-10},{d.x+30,d.y-10},
                {d.x+30,d.y+10},{d.x+10,d.y+10},{d.x+10,d.y+30},{d.x-10,d.y+30},
                {d.x-10,d.y+10},{d.x-30,d.y+10},{d.x-30,d.y-10},{d.x-10,d.y-10}};
            Polygon(dc,cross,12);
            for(auto stick:{hw.left_stick,hw.right_stick}){
                circle(dc,stick.x,stick.y,25);
                GdiObject rim{CreateSolidBrush(RGB(80,96,117))}, cap{CreateSolidBrush(RGB(35,47,62))};
                if(!rim.handle||!cap.handle)return false;
                Selection ring(dc,rim.handle);circle(dc,stick.x,stick.y,21);
                Selection grip(dc,cap.handle);circle(dc,stick.x,stick.y,17);
                GdiObject highlight{CreatePen(PS_SOLID,1,RGB(151,178,202))};
                if(!highlight.handle)return false;
                Selection hi(dc,highlight.handle);
                Arc(dc,stick.x-17,stick.y-17,stick.x+18,stick.y+18,
                    stick.x+9,stick.y-15,stick.x-15,stick.y-9);
            }
            if(view.family==PadLabelFamily::playstation)RoundRect(dc,432,117,488,145,8,8);
            else if(view.family==PadLabelFamily::xbox)circle(dc,460,131,10);
            for(auto key:{hw.back,hw.start})RoundRect(dc,key.x-19,key.y-10,key.x+20,key.y+11,7,7);
            if(!face(dc,view.family))return false;
            const auto back_name=view.family==PadLabelFamily::playstation?"Share":"Back";
            const auto start_name=view.family==PadLabelFamily::playstation?"Options":"Start";
            if(!text_fit(dc,back_name,{hw.back.x-18,hw.back.y-9,hw.back.x+19,hw.back.y+10},11,8,ink,false,DT_CENTER) ||
               !text_fit(dc,start_name,{hw.start.x-18,hw.start.y-9,hw.start.x+19,hw.start.y+10},11,8,ink,false,DT_CENTER))return false;
        }
        bool ok=text_fit(dc,view.status,kPadInfoStatus,24,15,ink,true,DT_CENTER);
        for(const auto& a:pad_info_callouts(view.family)){
            const auto endpoint=pad_callout_end(a);
            const int end=endpoint.x,y=endpoint.y;
            line(dc,a.anchor.x,a.anchor.y,end,y);
            line(dc,end,y,end+(a.left?6:-6),y-4);
            line(dc,end,y,end+(a.left?6:-6),y+4);
            ok=label(dc,view,a.row,a.box,a.left?DT_RIGHT:DT_LEFT)&&ok;
        }
        std::string back(view.back);back+=": ";back+=view.back_control;
        std::string pending("* ");pending+=view.pending_label;
        ok=text_fit(dc,back,kPadInfoBack,15,10,muted,false,DT_RIGHT)&&ok;
        ok=text_fit(dc,pending,kPadInfoPending,15,10,muted,false,DT_LEFT)&&ok;
        return ok;
    }catch(...){return false;}
}
bool draw_controller_info_preview(HDC dc,const PadInfoView& view) noexcept {
    return draw_controller_info(dc,view,bg,true);
}
bool prepare_controller_info_overlay(const PadInfoView& view,
                                     std::vector<PadOverlayPixel>& output) noexcept {
    try {
        HDC dc=CreateCompatibleDC(nullptr);
        if(!dc)return false;
        struct DCGuard {HDC dc;~DCGuard(){DeleteDC(dc);}} guard{dc};
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=kPadInfoWidth;
        bmi.bmiHeader.biHeight=-kPadInfoHeight;
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        void* raw=nullptr;
        GdiObject bitmap{CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&raw,nullptr,0)};
        if(!bitmap.handle||!raw)return false;
        Selection selected(dc,bitmap.handle);
        if(!selected.previous||selected.previous==HGDI_ERROR)return false;
        const auto* pixels=static_cast<const std::uint32_t*>(raw);
        if(!draw_controller_info(dc,view,RGB(0,0,0),false)||!GdiFlush())return false;
        std::vector<std::uint32_t> black(pixels,pixels+kPadInfoPixels);
        if(!draw_controller_info(dc,view,RGB(255,255,255),false)||!GdiFlush())return false;
        std::vector<PadOverlayPixel> staged(kPadInfoPixels);
        for(std::size_t i=0;i<kPadInfoPixels;++i)staged[i]=pad_overlay_from_dib(black[i],pixels[i]);
        output.swap(staged);
        return true;
    }catch(...){return false;}
}
} // namespace r2fix
#endif
