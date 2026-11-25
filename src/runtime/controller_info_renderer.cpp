#include "rayman2fix/controller_info_renderer.hpp"
#ifdef _WIN32
#include <string>
#include <array>
#include "controller_info_art.hpp"
namespace r2fix {
namespace {
constexpr COLORREF bg=RGB(15,29,52), blue=RGB(75,149,237),
                   ink=RGB(255,239,211), muted=RGB(152,226,248),
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
void solid_circle(HDC dc,int x,int y,int radius,COLORREF color){
    GdiObject brush{CreateSolidBrush(color)};
    if(!brush.handle)return;
    Selection b(dc,brush.handle),p(dc,GetStockObject(NULL_PEN));
    circle(dc,x,y,radius);
}
bool label(HDC dc,const PadInfoView& v,unsigned row,PadInfoRect r,UINT align){
    const auto& item=v.rows[row];
    const auto action=item.action;
    // Subtle offset shadow behind action labels, then the original fitted text.
    text_fit(dc,action,{r.left,r.top+1,r.right,r.top+26},19,12,RGB(8,24,43),true,align);
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
        if(!controller_art::paint(dc,view.family))return false;
        if(!face(dc,view.family))return false;
        const auto back_name=view.family==PadLabelFamily::playstation?"Share":"Back";
        const auto start_name=view.family==PadLabelFamily::playstation?"Options":"Start";
        if(!text_fit(dc,back_name,{hw.back.x-18,hw.back.y-9,hw.back.x+19,hw.back.y+10},11,8,ink,false,DT_CENTER) ||
           !text_fit(dc,start_name,{hw.start.x-18,hw.start.y-9,hw.start.x+19,hw.start.y+10},11,8,ink,false,DT_CENTER))return false;
        bool ok=text_fit(dc,view.status,kPadInfoStatus,28,14,ink,true,DT_CENTER);
        for(const auto& a:pad_info_callouts(view.family)){
            const auto endpoint=pad_callout_end(a);
            const int end=endpoint.x,y=endpoint.y;
            // Small terminal dot and restrained leader, no crossing arrowheads.
            GdiObject leader{CreatePen(PS_SOLID,1,RGB(155,227,249))};
            if(!leader.handle)return false;
            Selection lp(dc,leader.handle);
            line(dc,a.anchor.x,a.anchor.y,end,y);
            solid_circle(dc,a.anchor.x,a.anchor.y,2,RGB(200,231,249));
            ok=label(dc,view,a.row,a.box,a.left?DT_RIGHT:DT_LEFT)&&ok;
        }
        std::string back(view.back);back+=": ";back+=view.back_control;
        ok=text_fit(dc,back,kPadInfoBack,15,10,muted,false,DT_RIGHT)&&ok;
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
