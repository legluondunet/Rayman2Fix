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
COLORREF mix(COLORREF a,COLORREF b,unsigned n,unsigned den) {
    const auto blend=[&](unsigned x,unsigned y){return (x*(den-n)+y*n+den/2)/den;};
    return RGB(blend(GetRValue(a),GetRValue(b)),blend(GetGValue(a),GetGValue(b)),
               blend(GetBValue(a),GetBValue(b)));
}
void solid_rect(HDC dc,RECT r,COLORREF color){
    SetDCBrushColor(dc,color);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void solid_circle(HDC dc,int x,int y,int radius,COLORREF color){
    GdiObject brush{CreateSolidBrush(color)};
    if(!brush.handle)return;
    Selection b(dc,brush.handle),p(dc,GetStockObject(NULL_PEN));
    circle(dc,x,y,radius);
}
void inset_button(HDC dc,PadInfoRect r,COLORREF top,COLORREF bottom,int radius){
    GdiObject edge{CreatePen(PS_SOLID,1,RGB(12,20,30))},fill{CreateSolidBrush(bottom)};
    if(!edge.handle||!fill.handle)return;
    Selection p(dc,edge.handle),b(dc,fill.handle);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    GdiObject cap{CreateSolidBrush(top)};
    if(!cap.handle)return;
    Selection h(dc,cap.handle),none(dc,GetStockObject(NULL_PEN));
    RoundRect(dc,r.left+2,r.top+2,r.right-2,r.bottom-4,radius,radius);
}
// Original curves with a softly lit shell and recessed dark controls.
bool shell_path(HDC dc,PadLabelFamily family) {
    const bool xbox=family==PadLabelFamily::xbox;
    const POINT xbox_path[]={
        {288,218},{289,166},{305,134},{330,118},
        {352,103},{376,105},{401,116},
        {426,126},{494,126},{519,116},
        {544,105},{568,103},{590,118},
        {615,134},{631,166},{632,218},
        {641,269},{641,309},{620,332},
        {600,351},{578,341},{559,317},
        {545,299},{530,290},{509,290},
        {485,290},{435,290},{411,290},
        {390,290},{375,299},{361,317},
        {342,341},{320,351},{300,332},
        {279,309},{279,269},{288,218}
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
    return true;
}
bool draw_shell(HDC dc,PadLabelFamily family) {
    const bool xbox=family==PadLabelFamily::xbox;
    const auto light=xbox?RGB(181,192,203):RGB(112,125,146);
    const auto mid=xbox?RGB(111,128,146):RGB(61,75,96);
    const auto shade=xbox?RGB(47,61,79):RGB(24,34,51);
    const int saved=SaveDC(dc);
    if(!saved)return false;
    if(!shell_path(dc,family)||!SelectClipPath(dc,RGN_AND)){
        RestoreDC(dc,saved);return false;
    }
    for(int y=98;y<360;++y){
        const auto c=y<195?mix(light,mid,unsigned(std::max(0,y-98)),97):
                          mix(mid,shade,unsigned(y-195),165);
        solid_rect(dc,{277,y,643,y+1},c);
    }
    RestoreDC(dc,saved);
    // Thin neutral outline rather than a neon stroke around every surface.
    GdiObject edge{CreatePen(PS_SOLID,1,RGB(140,161,182))};
    if(!edge.handle)return false;
    Selection p(dc,edge.handle);
    return shell_path(dc,family)&&StrokePath(dc)!=0;
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
        Selection face_brush(dc,face_fill.handle);
        solid_circle(dc,p.x,p.y+1,16,RGB(19,26,37));
        solid_circle(dc,p.x,p.y,14,RGB(97,111,130));
        const auto base=family==PadLabelFamily::xbox?xbox_colors[i]:RGB(46,57,73);
        for(int radius=13;radius>=1;--radius)
            solid_circle(dc,p.x,p.y-(13-radius)/5,radius,mix(base,RGB(245,250,255),unsigned(13-radius),55));
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
            for(auto cap:{PadInfoRect{315,81,361,108},PadInfoRect{559,81,605,108},
                          PadInfoRect{307,112,354,139},PadInfoRect{566,112,613,139}})
                inset_button(dc,cap,RGB(66,82,101),RGB(22,32,46),14);
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
            solid_circle(dc,d.x,d.y,34,RGB(37,48,64));
            solid_circle(dc,d.x,d.y,31,RGB(87,103,122));
            {
                GdiObject cp{CreatePen(PS_SOLID,1,RGB(13,20,30))},cb{CreateSolidBrush(RGB(33,43,57))};
                if(!cp.handle||!cb.handle)return false;
                Selection p(dc,cp.handle),b(dc,cb.handle);
                Polygon(dc,cross,12);
                GdiObject hi{CreatePen(PS_SOLID,1,RGB(98,116,137))};
                if(!hi.handle)return false;
                Selection h(dc,hi.handle);
                line(dc,d.x-8,d.y-27,d.x+8,d.y-27);
                line(dc,d.x-27,d.y-8,d.x-11,d.y-8);
            }
            for(auto stick:{hw.left_stick,hw.right_stick}){
                solid_circle(dc,stick.x,stick.y+1,27,RGB(20,29,41));
                solid_circle(dc,stick.x,stick.y,24,RGB(112,128,147));
                solid_circle(dc,stick.x,stick.y,22,RGB(36,47,62));
                for(int rad=20;rad>=1;--rad)
                    solid_circle(dc,stick.x,stick.y,rad,mix(RGB(68,81,99),RGB(31,42,57),unsigned(20-rad),19));
                GdiObject grip{CreatePen(PS_SOLID,1,RGB(130,147,165))};
                if(!grip.handle)return false;
                Selection hi(dc,grip.handle);
                for(int i=0;i<12;++i){
                    constexpr double pi=3.141592653589793;
                    const double a=double(i)*pi/6.0;
                    line(dc,stick.x+int(17*std::cos(a)),stick.y+int(17*std::sin(a)),
                        stick.x+int(19*std::cos(a)),stick.y+int(19*std::sin(a)));
                }
            }
            if(view.family==PadLabelFamily::playstation){
                inset_button(dc,{432,117,488,145},RGB(57,70,87),RGB(19,28,40),8);
                for(int y=122;y<141;y+=4)for(int x=438;x<484;x+=4)
                    SetPixelV(dc,x,y,RGB(87,102,121));
            }else if(view.family==PadLabelFamily::xbox){
                solid_circle(dc,460,131,12,RGB(24,36,49));
                solid_circle(dc,460,131,9,RGB(104,146,115));
                solid_circle(dc,460,131,6,RGB(49,65,62));
            }
            for(auto key:{hw.back,hw.start})
                inset_button(dc,{key.x-19,key.y-10,key.x+20,key.y+11},
                    RGB(65,80,98),RGB(18,27,39),12);
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
            // Small terminal dot and restrained leader, no crossing arrowheads.
            GdiObject leader{CreatePen(PS_SOLID,1,RGB(145,199,236))};
            if(!leader.handle)return false;
            Selection lp(dc,leader.handle);
            line(dc,a.anchor.x,a.anchor.y,end,y);
            solid_circle(dc,a.anchor.x,a.anchor.y,2,RGB(200,231,249));
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
        constexpr int supersample=2;
        constexpr int pixel_width=kPadInfoWidth*supersample;
        constexpr int pixel_height=kPadInfoHeight*supersample;
        constexpr std::size_t pixel_count=std::size_t(pixel_width)*pixel_height;
        bmi.bmiHeader.biWidth=pixel_width;
        bmi.bmiHeader.biHeight=-pixel_height;
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        void* raw=nullptr;
        GdiObject bitmap{CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&raw,nullptr,0)};
        if(!bitmap.handle||!raw)return false;
        Selection selected(dc,bitmap.handle);
        if(!selected.previous||selected.previous==HGDI_ERROR)return false;
        if(!SetMapMode(dc,MM_ANISOTROPIC) ||
           !SetWindowExtEx(dc,kPadInfoWidth,kPadInfoHeight,nullptr) ||
           !SetViewportExtEx(dc,pixel_width,pixel_height,nullptr))return false;
        const auto* pixels=static_cast<const std::uint32_t*>(raw);
        if(!draw_controller_info(dc,view,RGB(0,0,0),false)||!GdiFlush())return false;
        std::vector<std::uint32_t> black(pixels,pixels+pixel_count);
        if(!draw_controller_info(dc,view,RGB(255,255,255),false)||!GdiFlush())return false;
        std::vector<PadOverlayPixel> staged(kPadInfoPixels);
        for(int y=0;y<kPadInfoHeight;++y)for(int x=0;x<kPadInfoWidth;++x){
            unsigned sum[6]{};
            for(int sy=0;sy<supersample;++sy)for(int sx=0;sx<supersample;++sx){
                const auto i=std::size_t(y*supersample+sy)*pixel_width+x*supersample+sx;
                const auto p=pad_overlay_from_dib(black[i],pixels[i]);
                const unsigned channels[]{p.r,p.g,p.b,p.ar,p.ag,p.ab};
                for(unsigned c=0;c<6;++c)sum[c]+=channels[c];
            }
            for(auto& n:sum)n=(n+2u)/4u;
            staged[std::size_t(y)*kPadInfoWidth+x]={std::uint8_t(sum[0]),std::uint8_t(sum[1]),std::uint8_t(sum[2]),
                std::uint8_t(sum[3]),std::uint8_t(sum[4]),std::uint8_t(sum[5])};
        }
        output.swap(staged);
        return true;
    }catch(...){return false;}
}
} // namespace r2fix
#endif
