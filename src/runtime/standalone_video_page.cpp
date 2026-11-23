#include "rayman2fix/standalone_video_page.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

namespace r2fix {
namespace {
constexpr unsigned kX=92u,kY=174u,kW=585u,kH=419u;
constexpr int kBackBuffer=1,kRgb565=0;
using GlideRead=std::int32_t (WINAPI*)(int,unsigned,unsigned,unsigned,
                                       unsigned,unsigned,void*);
using GlideWrite=std::int32_t (WINAPI*)(int,unsigned,unsigned,int,unsigned,
                                        unsigned,int,void*);
StandaloneVideoLogger g_log=nullptr;
std::atomic<bool> g_visible{false};
std::atomic<bool> g_configured{false};
std::atomic<bool> g_drew{false};
std::atomic<bool> g_glide_missing{false};
std::atomic<bool> g_write_error{false};
GlideRead g_read=nullptr;
GlideWrite g_write=nullptr;
std::array<std::array<char,128>,6> g_lines{};
std::vector<std::uint16_t> g_pixels;
std::vector<std::uint32_t> g_text_mask;
bool g_prepared=false;
bool g_prepare_failed=false;

FARPROC find_export(HMODULE module,const char* name,
                    const char* decorated) noexcept {
    auto f=GetProcAddress(module,name);
    if(!f && decorated)f=GetProcAddress(module,decorated);
    return f;
}
bool find_glide() noexcept {
    if(g_read && g_write)return true;
    // The launcher installs a pinned, separately licensed GameFrame
    // glide2x.dll. Never load a different renderer from search paths.
    const auto module=GetModuleHandleW(L"glide2x.dll");
    if(!module)return false;
    g_read=reinterpret_cast<GlideRead>(
        find_export(module,"grLfbReadRegion","_grLfbReadRegion@28"));
    g_write=reinterpret_cast<GlideWrite>(
        find_export(module,"grLfbWriteRegion","_grLfbWriteRegion@32"));
    return g_read && g_write;
}
std::uint16_t blend_565(std::uint16_t original,
                        unsigned red,unsigned green,unsigned blue,
                        unsigned alpha) noexcept {
    const unsigned r0=((original>>11)&31u)*255u/31u;
    const unsigned g0=((original>>5)&63u)*255u/63u;
    const unsigned b0=(original&31u)*255u/31u;
    const unsigned a=alpha>255u?255u:alpha;
    const unsigned r=(r0*(255u-a)+red*a+127u)/255u;
    const unsigned g=(g0*(255u-a)+green*a+127u)/255u;
    const unsigned b=(b0*(255u-a)+blue*a+127u)/255u;
    return std::uint16_t(((r*31u+127u)/255u)<<11 |
                         ((g*63u+127u)/255u)<<5 |
                         ((b*31u+127u)/255u));
}
bool prepare_mask() noexcept {
    if(g_prepared)return true;
    if(g_prepare_failed)return false;
    HDC screen=GetDC(nullptr);
    if(!screen){g_prepare_failed=true;return false;}
    HDC dc=CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=LONG(kW);
    bmi.bmiHeader.biHeight=-LONG(kH);
    bmi.bmiHeader.biPlanes=1;
    bmi.bmiHeader.biBitCount=32;
    bmi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP bitmap=dc?CreateDIBSection(screen,&bmi,DIB_RGB_COLORS,
                                       &bits,nullptr,0):nullptr;
    if(!dc || !bitmap || !bits) {
        if(bitmap)DeleteObject(bitmap);
        if(dc)DeleteDC(dc);
        ReleaseDC(nullptr,screen);
        g_prepare_failed=true;return false;
    }
    const auto prior=SelectObject(dc,bitmap);
    std::memset(bits,0,kW*kH*4u);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(241,245,222));
    HFONT heading=CreateFontA(-44,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,"Segoe UI");
    HFONT text=CreateFontA(-23,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,"Segoe UI");
    if(!heading || !text) {
        if(heading)DeleteObject(heading);
        if(text)DeleteObject(text);
        SelectObject(dc,prior);DeleteObject(bitmap);DeleteDC(dc);
        ReleaseDC(nullptr,screen);
        g_prepare_failed=true;return false;
    }
    auto* old_font=SelectObject(dc,heading);
    TextOutA(dc,40,22,"VIDEO",5);
    SelectObject(dc,text);
    for(unsigned i=0;i<g_lines.size();++i)
        TextOutA(dc,40,114+int(i)*43,g_lines[i].data(),
                 int(std::strlen(g_lines[i].data())));
    SelectObject(dc,old_font);
    DeleteObject(heading);DeleteObject(text);
    g_text_mask.resize(kW*kH);
    std::memcpy(g_text_mask.data(),bits,kW*kH*4u);
    SelectObject(dc,prior);DeleteObject(bitmap);DeleteDC(dc);
    ReleaseDC(nullptr,screen);
    g_pixels.resize(kW*kH);
    g_prepared=true;
    return true;
}
} // namespace

void standalone_video_page_configure(StandaloneVideoLogger logger,
    unsigned width,unsigned height,bool classic) noexcept {
    g_log=logger;
    const unsigned out_w=width?width:unsigned(GetSystemMetrics(SM_CXSCREEN));
    const unsigned out_h=height?height:unsigned(GetSystemMetrics(SM_CYSCREEN));
    unsigned a=0,b=0;
    const unsigned divisor=std::gcd(out_w,out_h);
    if(divisor){a=out_w/divisor;b=out_h/divisor;}
    if(a==8u && b==5u){a=16u;b=10u;}
    DISPLAY_DEVICEA device{};device.cb=sizeof(device);
    const char* gpu="Display adapter";
    for(DWORD i=0;i<8u;++i) {
        device={};device.cb=sizeof(device);
        if(!EnumDisplayDevicesA(nullptr,i,&device,0))break;
        if((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) &&
           (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            gpu=device.DeviceString;
            break;
        }
    }
    std::snprintf(g_lines[0].data(),128,"SORTIE    %u x %u  %u:%u  %s",
        out_w,out_h,a,b,classic?"4:3":"AUTO");
    std::snprintf(g_lines[1].data(),128,"GPU       %.44s",gpu);
    std::snprintf(g_lines[2].data(),128,"RENDU     GAMEFRAME / GLIDE2");
    std::snprintf(g_lines[3].data(),128,"INTERNE   1024 x 768 x 16");
    std::snprintf(g_lines[4].data(),128,"REGLAGES  AUTOMATIQUES");
    std::snprintf(g_lines[5].data(),128,"ECHAP     RETOUR");
    g_configured.store(true,std::memory_order_release);
    if(g_log)g_log("RUNTIME-STANDALONE-VIDEO: configured independent-page=1 native-label-pointers=unused original-video-widgets=unused renderer=Glide2-LFB");
}

void standalone_video_page_set_visible(bool value) noexcept {
    g_visible.store(value,std::memory_order_release);
}
bool standalone_video_page_draw_before_flip() noexcept {
    if(!g_visible.load(std::memory_order_acquire) ||
       !g_configured.load(std::memory_order_acquire))return false;
    if(!find_glide()) {
        if(!g_glide_missing.exchange(true) && g_log)
            g_log("RUNTIME-STANDALONE-VIDEO: glide-lfb-unavailable original-flip-preserved=1");
        return false;
    }
    if(!prepare_mask())return false;
    if(!g_read(kBackBuffer,kX,kY,kW,kH,kW*2u,g_pixels.data()))
        return false;
    // Read the actual animated menu background and darken it in-place.
    // No old VIDEO glyph is hidden here: the old script is deactivated
    // upstream before its first JFFTXT call. The 3D scene remains live.
    for(unsigned y=0;y<kH;++y)for(unsigned x=0;x<kW;++x) {
        const unsigned i=y*kW+x;
        auto pix=g_pixels[i];
        const bool edge=x<2u || y<2u || x>=kW-2u || y>=kH-2u;
        pix=blend_565(pix,edge?113u:5u,edge?173u:23u,
                      edge?188u:33u,edge?195u:128u);
        const auto value=g_text_mask[i];
        const unsigned m=value&255u;
        if(m)pix=blend_565(pix,235u,247u,219u,m);
        g_pixels[i]=pix;
    }
    if(!g_write(kBackBuffer,kX,kY,kRgb565,kW,kH,
                int(kW*2u),g_pixels.data())) {
        if(!g_write_error.exchange(true) && g_log)
            g_log("RUNTIME-STANDALONE-VIDEO: LFB-write-refused original-flip-preserved=1");
        return false;
    }
    if(!g_drew.exchange(true) && g_log)
        g_log("RUNTIME-STANDALONE-VIDEO: frame-rendered=1 source=own-Glide-LFB background=preserved-native-3D controls=Escape-only");
    return true;
}
} // namespace r2fix
