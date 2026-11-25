#include "rayman2fix/controller_info_renderer.hpp"
#ifdef _WIN32
#include <string>
#include <array>
namespace r2fix {
namespace {
constexpr COLORREF bg=RGB(15,29,52), blue=RGB(75,149,237),
                   ink=RGB(235,243,255), muted=RGB(169,197,224),
                   body=RGB(38,60,89), dark=RGB(17,29,46);
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
        const auto p=points[i];circle(dc,p.x,p.y,13);
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
        // Original vector silhouettes; no product artwork bundled.
        const auto hw=pad_hardware_layout(view.family);
        {
            Selection shell_sel(dc,shell.handle);
            if(view.family==PadLabelFamily::xbox){
                POINT shape[]={{310,132},{321,108},{359,99},{412,119},{508,119},{561,99},
                    {599,108},{610,132},{628,252},{624,310},{606,339},{582,346},
                    {558,332},{522,297},{398,297},{362,332},{338,346},{314,339},{296,310},{292,252}};
                if(!Polygon(dc,shape,int(sizeof(shape)/sizeof(shape[0]))))return false;
            }else{
                POINT shape[]={{310,130},{329,104},{389,100},{416,112},{504,112},
                    {531,100},{591,104},{610,130},{628,252},{617,324},{591,345},
                    {564,333},{531,297},{389,297},{356,333},{329,345},{303,324},{292,252}};
                if(!Polygon(dc,shape,int(sizeof(shape)/sizeof(shape[0]))))return false;
            }
        }
        {
            Selection controls(dc,button.handle);
            RoundRect(dc,315,81,361,108,9,9);RoundRect(dc,559,81,605,108,9,9);
            RoundRect(dc,307,112,354,139,9,9);RoundRect(dc,566,112,613,139,9,9);
            const auto d=hw.dpad;
            POINT cross[]={{d.x-10,d.y-30},{d.x+10,d.y-30},{d.x+10,d.y-10},{d.x+30,d.y-10},
                {d.x+30,d.y+10},{d.x+10,d.y+10},{d.x+10,d.y+30},{d.x-10,d.y+30},
                {d.x-10,d.y+10},{d.x-30,d.y+10},{d.x-30,d.y-10},{d.x-10,d.y-10}};
            Polygon(dc,cross,12);
            for(auto stick:{hw.left_stick,hw.right_stick}){circle(dc,stick.x,stick.y,23);circle(dc,stick.x,stick.y,16);}
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
            const int end=a.left?a.box.right+4:a.box.left-4;
            const int y=(a.box.top+a.box.bottom)/2;
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
