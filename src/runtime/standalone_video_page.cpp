#include "rayman2fix/standalone_video_page.hpp"
#include "rayman2fix/standalone_video_device_policy.hpp"
#include "rayman2fix/standalone_video_layout_policy.hpp"
#include "rayman2fix/video_navigation_input.hpp"
#include "rayman2fix/wine_vulkan_driver.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <cmath>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

namespace r2fix {
bool pause_video_live_active() noexcept;
namespace {
// Keep the independent page below the animated native 3D butterfly.
// The approved Glide internal canvas is 1024x768, regardless of the
// output desktop's aspect ratio or scaling (e.g. 1920x1200).
// Center on the approved 1024x768 Glide frame, below the native VIDEO title.
constexpr unsigned kW=kVideoInfoWidth,kH=kVideoInfoHeight;
constexpr unsigned kX=kVideoInfoX,kY=kVideoInfoY;
static_assert(kX==212u && kY==284u,
              "Both VIDEO panels use the same lower geometry");
static_assert(kX+kW<=kVideoCanvasWidth &&
              video_info_panel_y(true)+kH<=kVideoCanvasHeight,
              "Both VIDEO panel placements must fit the Glide canvas");
constexpr int kBackBuffer=1,kRgb565=0;
constexpr unsigned kSampleScale=4u,kSamples=kSampleScale*kSampleScale;
constexpr unsigned kCornerRadius=12u,kBorderWidth=2u;
constexpr unsigned kMaskW=kW*kSampleScale,kMaskH=kH*kSampleScale;
struct PanelCoverage { unsigned char fill, border; };
// Integer subpixel centres; inner and outer arcs share corner centres.
constexpr bool rounded_sample(int x,int y,int inset) noexcept {
    constexpr int unit=int(kSampleScale*2u);
    const int left=inset*unit,top=left;
    const int right=int(kW)*unit-left,bottom=int(kH)*unit-top;
    const int radius=(int(kCornerRadius)-inset)*unit;
    if(x<left || x>=right || y<top || y>=bottom)return false;
    const int dx=x<left+radius?left+radius-x:
                 x>right-radius?x-(right-radius):0;
    const int dy=y<top+radius?top+radius-y:
                 y>bottom-radius?y-(bottom-radius):0;
    return dx*dx+dy*dy<=radius*radius;
}
constexpr PanelCoverage panel_coverage(unsigned x,unsigned y) noexcept {
    PanelCoverage c{};
    for(unsigned sy=0;sy<kSampleScale;++sy)
        for(unsigned sx=0;sx<kSampleScale;++sx) {
            const int px=int((x*kSampleScale+sx)*2u+1u);
            const int py=int((y*kSampleScale+sy)*2u+1u);
            if(rounded_sample(px,py,int(kBorderWidth)))++c.fill;
            else if(rounded_sample(px,py,0))++c.border;
        }
    return c;
}
static_assert(panel_coverage(0,0).fill==0 &&
              panel_coverage(0,0).border==0,"Outside corners remain untouched");
static_assert(panel_coverage(kW/2,0).border==kSamples &&
              panel_coverage(kW/2,2).fill==kSamples,"Two-pixel straight border");
static_assert(panel_coverage(kW/2,kH/2).fill==kSamples,"Solid panel interior");
static_assert(panel_coverage(3,3).border>0 &&
              panel_coverage(3,3).border<kSamples,"Fractional corner coverage");
static_assert(panel_coverage(3,3).border==panel_coverage(kW-4,kH-4).border,
              "Symmetric rounded corners");
using GlideRead=std::int32_t (WINAPI*)(int,unsigned,unsigned,unsigned,
                                       unsigned,unsigned,void*);
using GlideWrite=std::int32_t (WINAPI*)(int,unsigned,unsigned,int,unsigned,
                                        unsigned,int,void*);
StandaloneVideoLogger g_log=nullptr;
std::atomic<bool> g_visible{false};
std::atomic<bool> g_pause_visible{false};
std::atomic<bool> g_configured{false};
std::atomic<bool> g_drew{false};
std::atomic<bool> g_glide_missing{false};
std::atomic<bool> g_write_error{false};
std::atomic<bool> g_pause_draw_reported{false};
std::atomic<bool> g_pause_drawn_this_visit{false};
// Once an authenticated Pause VIDEO page was rendered, the original
// brightness ScreenImage actors can survive the menu's destruction.
// Keep the exact two-rectangle filter armed through resumed gameplay,
// but never before that verified page was displayed.
std::atomic<bool> g_pause_gauge_leak_guard{false};
std::atomic<bool> g_pause_native_glyphs_suppressed{false};
std::atomic<bool> g_pause_title_restored_logged{false};
std::atomic<bool> g_pause_title_band_sampled{false};
GlideRead g_read=nullptr;
GlideWrite g_write=nullptr;
std::array<std::array<char,128>,kVideoInfoRowCount> g_lines{};
constexpr const char* kRowLabels[kVideoInfoRowCount]{"SORTIE","GPU","PILOTE","RENDU","INTERNE","FIX"};
std::atomic<bool> g_title_preserved_logged{false};
std::vector<std::uint16_t> g_pixels;
std::vector<std::uint8_t> g_text_mask;
std::vector<PanelCoverage> g_panel_mask;
bool g_prepared=false;
bool g_prepare_failed=false;
bool g_native_windows=false;
DISPLAY_DEVICEA g_primary_display{};
thread_local bool g_pause_frame_checked=false,g_pause_frame_active=false;
thread_local bool g_pause_early_failed=false;

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
// Read only the installed Windows display driver version of an
// unambiguously matched active PCI adapter. Under Wine the synthetic
// Windows adapter must not be mistaken for the host Mesa/RADV driver.
constexpr GUID kDisplayClassGuid={
    0x4d36e968u,0xe325u,0x11ceu,
    {0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
bool read_registry_string(HKEY key,const char* name,
                          char* out,DWORD capacity) noexcept {
    if(!key || !out || capacity<2u)return false;
    DWORD kind=0,bytes=capacity;
    const LSTATUS status=RegQueryValueExA(
        key,name,nullptr,&kind,reinterpret_cast<BYTE*>(out),&bytes);
    if(status!=ERROR_SUCCESS ||
       (kind!=REG_SZ && kind!=REG_EXPAND_SZ) ||
       !bytes || bytes>capacity)return false;
    out[capacity-1u]='\0';
    return out[0]!='\0';
}
void primary_display_driver(const DISPLAY_DEVICEA& primary,
                            char* friendly,unsigned friendly_capacity,
                            char* version,unsigned version_capacity) noexcept {
    if(!primary.DeviceID[0] || !friendly || !version ||
       friendly_capacity<2u || version_capacity<2u)return;
    const HDEVINFO devices=SetupDiGetClassDevsA(
        &kDisplayClassGuid,nullptr,nullptr,DIGCF_PRESENT);
    if(devices==INVALID_HANDLE_VALUE)return;
    unsigned matches=0;
    char matched_name[128]{},matched_version[96]{};
    for(DWORD i=0;i<32u;++i) {
        SP_DEVINFO_DATA item{};item.cbSize=sizeof(item);
        if(!SetupDiEnumDeviceInfo(devices,i,&item))break;
        char instance_id[512]{};
        if(!SetupDiGetDeviceInstanceIdA(devices,&item,instance_id,
                DWORD(sizeof(instance_id)),nullptr) ||
           !matching_display_pci_identity(primary.DeviceID,instance_id))
            continue;
        ++matches;
        if(matches>1u)break; // Do not attribute another GPU's driver.
        DWORD type=0;
        char name[128]{};
        if(!SetupDiGetDeviceRegistryPropertyA(devices,&item,
                SPDRP_FRIENDLYNAME,&type,
                reinterpret_cast<PBYTE>(name),DWORD(sizeof(name)),nullptr) ||
           type!=REG_SZ || !name[0]) {
            name[0]='\0';type=0;
            SetupDiGetDeviceRegistryPropertyA(devices,&item,
                SPDRP_DEVICEDESC,&type,
                reinterpret_cast<PBYTE>(name),DWORD(sizeof(name)),nullptr);
        }
        name[sizeof(name)-1u]='\0';
        if(name[0])std::snprintf(matched_name,sizeof(matched_name),"%s",name);
        const HKEY key=SetupDiOpenDevRegKey(devices,&item,
            DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_READ);
        if(key!=INVALID_HANDLE_VALUE) {
            read_registry_string(key,"DriverVersion",
                matched_version,DWORD(sizeof(matched_version)));
            RegCloseKey(key);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    if(matches!=1u)return;
    if(matched_name[0])
        std::snprintf(friendly,friendly_capacity,"%s",matched_name);
    if(matched_version[0])
        std::snprintf(version,version_capacity,"%s",matched_version);
}
// Resolve the GPU from GameFrame's own live OpenGL context, not from
// the primary desktop adapter (which can be a different GPU).
void update_native_renderer_name() noexcept {
    if(!g_native_windows)return;
    char key[64]{},renderer[128]{};
    std::snprintf(key,sizeof(key),"RAYMAN2FIX_GL_RENDERER_%lu",
                  static_cast<unsigned long>(GetCurrentProcessId()));
    const DWORD n=GetEnvironmentVariableA(key,renderer,DWORD(sizeof(renderer)));
    if(!n || n>=sizeof(renderer))return;
    DISPLAY_DEVICEA matched{};
    unsigned matches=0;
    for(DWORD i=0;i<32u;++i) {
        DISPLAY_DEVICEA d{};d.cb=sizeof(d);
        if(!EnumDisplayDevicesA(nullptr,i,&d,0))break;
        if(d.DeviceID[0] && lstrcmpiA(d.DeviceString,renderer)==0) {
            matched=d;++matches;
        }
    }
    char friendly[128]{},version[96]{};
    if(matches==1u)
        primary_display_driver(matched,friendly,unsigned(sizeof(friendly)),
                               version,unsigned(sizeof(version)));
    if(version[0])
        std::snprintf(g_lines[2].data(),128,"%s",version);
    else if(g_lines[2][0] && lstrcmpiA(g_lines[1].data(),renderer)!=0) {
        // No exact association: keep the existing display driver
        // version explicitly labelled as display, not renderer.
        char value[128]{};
        std::snprintf(value,sizeof(value),"%.100s (ecran)",g_lines[2].data());
        std::snprintf(g_lines[2].data(),128,"%s",value);
    }
    std::snprintf(g_lines[1].data(),128,"%s",renderer);
    // Some drivers expose only a family name even via GL_RENDERER.
    // A uniquely matched PCI identifier is useful without guessing SKU.
    if(matches==1u &&
       (lstrcmpiA(renderer,"AMD Radeon(TM) Graphics")==0 ||
        lstrcmpiA(renderer,"AMD Radeon Graphics")==0)) {
        const auto v=pci_token(matched.DeviceID,"VEN_");
        const auto d=pci_token(matched.DeviceID,"DEV_");
        if(v && d)std::snprintf(g_lines[1].data(),128,
            "%s [%.4s:%.4s]",renderer,v,d);
    }
    if(g_log) {
        char msg[330]{};
        std::snprintf(msg,sizeof(msg),
            "RUNTIME-VIDEO-GPU: source=GameFrame-GL_RENDERER name=%s driver-matched=%u display-name=%s",
            g_lines[1].data(),unsigned(version[0]!=0),g_primary_display.DeviceString);
        g_log(msg);
    }
}
bool prepare_mask() noexcept {
    if(g_prepared)return true;
    if(g_prepare_failed)return false;
    update_native_renderer_name();
    HDC screen=GetDC(nullptr);
    if(!screen){g_prepare_failed=true;return false;}
    HDC dc=CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=LONG(kMaskW);
    bmi.bmiHeader.biHeight=-LONG(kMaskH);
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
    std::memset(bits,0,kMaskW*kMaskH*4u);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(255,255,255));
    // The title uses the original Rayman 2 JFFTXT glyphs / animation,
    // not a GDI imitation. Only the data rows are rasterized here.
    // Fit one consistent font size to the actual longest GPU / driver
    // string, using the active GDI font metrics (also valid under Wine).
    // This avoids the previous truncated "AMD Radeon RX 6600M (RADV N..."
    // while retaining the approved blue panel and consistent six-row spacing.
    HFONT font=nullptr;
    int font_px=0;
    bool all_lines_fit=false;
    for(int px=kVideoInfoFontPreferredPx;
        px>=kVideoInfoFontMinPx;--px) {
        HFONT candidate=CreateFontA(-px*int(kSampleScale),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,"Segoe UI");
        if(!candidate)continue;
        const auto previous=SelectObject(dc,candidate);
        bool fits=true;
        for(unsigned i=0;i<g_lines.size();++i) {
            SIZE label_size{},value_size{};
            const char* value=g_lines[i].data();
            const int label_len=int(std::strlen(kRowLabels[i]));
            const int value_len=int(std::strlen(value));
            if(!GetTextExtentPoint32A(dc,kRowLabels[i],label_len,
                                      &label_size) ||
               !GetTextExtentPoint32A(dc,value,value_len,&value_size) ||
               label_size.cx>(kVideoInfoValueLeft-kVideoInfoLabelLeft-
                                 kVideoInfoTextPadding)*int(kSampleScale) ||
               value_size.cx>(kVideoInfoValueRight-kVideoInfoValueLeft-
                                 kVideoInfoTextPadding)*int(kSampleScale))
                fits=false;
        }
        SelectObject(dc,previous);
        if(fits || px==kVideoInfoFontMinPx) {
            font=candidate;
            font_px=px;
            all_lines_fit=fits;
            break;
        }
        DeleteObject(candidate);
    }
    if(!font) {
        SelectObject(dc,prior);DeleteObject(bitmap);DeleteDC(dc);
        ReleaseDC(nullptr,screen);
        g_prepare_failed=true;return false;
    }
    const auto old_font=SelectObject(dc,font);
    for(unsigned i=0;i<g_lines.size();++i) {
        const int y=video_info_row_y(i);
        constexpr int scale=int(kSampleScale);
        RECT label{kVideoInfoLabelLeft*scale,y*scale,166*scale,(y+kVideoInfoRowHeight)*scale};
        RECT value{kVideoInfoValueLeft*scale,y*scale,kVideoInfoValueRight*scale,
                   (y+(all_lines_fit?kVideoInfoRowHeight:kVideoInfoWrappedRowHeight))*scale};
        DrawTextA(dc,kRowLabels[i],-1,&label,
                  DT_SINGLELINE|DT_NOPREFIX|DT_VCENTER);
        if(all_lines_fit)
            DrawTextA(dc,g_lines[i].data(),-1,&value,
                      DT_SINGLELINE|DT_NOPREFIX|DT_VCENTER);
        else
            // Extra-long GPU descriptions can use two/three smaller
            // lines rather than cutting off meaningful information.
            DrawTextA(dc,g_lines[i].data(),-1,&value,
                      DT_WORDBREAK|DT_NOPREFIX);
    }
    if(g_log) {
        char msg[180]{};
        std::snprintf(msg,sizeof(msg),
            "RUNTIME-STANDALONE-VIDEO: info-font-px=%d value-column-width=%d "
            "all-lines-single-row-fit=%u source=measured-GDI fallback=word-wrap",
            font_px,kVideoInfoValueRight-kVideoInfoValueLeft,
            unsigned(all_lines_fit));
        g_log(msg);
    }
    SelectObject(dc,old_font);
    DeleteObject(font);
    // Finish GDI operations before accessing DIB memory directly.
    GdiFlush();
    // Box-filter the supersampled DIB to grayscale coverage, avoiding
    // ClearType colour fringes. Cache both masks once, not per frame.
    g_text_mask.resize(kW*kH);
    g_panel_mask.resize(kW*kH);
    const auto* samples=static_cast<const std::uint32_t*>(bits);
    for(unsigned y=0;y<kH;++y)for(unsigned x=0;x<kW;++x) {
        unsigned sum=0;
        for(unsigned sy=0;sy<kSampleScale;++sy)
            for(unsigned sx=0;sx<kSampleScale;++sx) {
                const auto v=samples[(y*kSampleScale+sy)*kMaskW+x*kSampleScale+sx];
                sum+=(v&255u)+((v>>8)&255u)+((v>>16)&255u);
            }
        g_text_mask[y*kW+x]=std::uint8_t((sum+3u*kSamples/2u)/(3u*kSamples));
        g_panel_mask[y*kW+x]=panel_coverage(x,y);
    }
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
    DISPLAY_DEVICEA primary{};primary.cb=sizeof(primary);
    for(DWORD i=0;i<8u;++i) {
        device={};device.cb=sizeof(device);
        if(!EnumDisplayDevicesA(nullptr,i,&device,0))break;
        if((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) &&
           (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            primary=device;break;
        }
    }
    char gpu[128]{},driver_version[96]{};
    std::snprintf(gpu,sizeof(gpu),"%s",
        primary.DeviceString[0]?primary.DeviceString:"Non disponible");
    const bool wine=[]() noexcept {
        const HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
        return ntdll && GetProcAddress(ntdll,"wine_get_version")!=nullptr;
    }();
    g_native_windows=!wine;
    g_primary_display=primary;
    if(!wine) {
        primary_display_driver(primary,gpu,unsigned(sizeof(gpu)),
                               driver_version,unsigned(sizeof(driver_version)));
    } else {
        char reason[96]{};
        const bool matched=wine_vulkan_driver_for_display(
            primary.DeviceString,primary.DeviceID,
            driver_version,unsigned(sizeof(driver_version)),
            reason,unsigned(sizeof(reason)));
        if(g_log) {
            char msg[260]{};
            std::snprintf(msg,sizeof(msg),
                "RUNTIME-WINE-VIDEO-DRIVER: host-ICD-source=winevulkan "
                "status=%s reason=%s information=%s no-external-command=1",
                matched?"matched":"unavailable",reason,
                matched?driver_version:"not-attributed");
            g_log(msg);
        }
    }
    std::snprintf(g_lines[0].data(),128,"%u x %u  %u:%u  %s",
        out_w,out_h,a,b,classic?"4:3":"AUTO");
    std::snprintf(g_lines[1].data(),128,"%s",gpu);
    std::snprintf(g_lines[2].data(),128,"%s",
        driver_version[0]?driver_version:
        wine?"Pilote Vulkan non identifie (Wine)":
             "Version non disponible");
    std::snprintf(g_lines[3].data(),128,"GAMEFRAME / GLIDE2");
    std::snprintf(g_lines[4].data(),128,"1024 x 768 x 16");
    std::snprintf(g_lines[5].data(),128,"Rayman2Fix %s-%s",
                  RAYMAN2FIX_VERSION,RAYMAN2FIX_BUILD_COMMIT);
    g_configured.store(true,std::memory_order_release);
    install_video_navigation_input(g_log);
    if(g_log)g_log("RUNTIME-STANDALONE-VIDEO: configured independent-page=1 native-title=original-video centered-panel=1 rows=6 win32-driver=verified-setupapi renderer=Glide2-LFB");
}

void standalone_video_page_set_visible(bool value) noexcept {
    // A new verified MENU VIDEO visit means the previous in-game actor
    // lifetime has ended. Re-arm only after a fresh Pause VIDEO draw.
    if(value)g_pause_gauge_leak_guard.store(false,
                                            std::memory_order_release);
    g_visible.store(value,std::memory_order_release);
}
void standalone_video_page_set_pause_visible(bool value) noexcept {
    if(!value)g_pause_drawn_this_visit.store(false,
                                            std::memory_order_release);
    g_pause_visible.store(value,std::memory_order_release);
}
bool standalone_video_page_pause_is_drawn() noexcept {
    return g_pause_visible.load(std::memory_order_acquire) &&
           g_pause_drawn_this_visit.load(std::memory_order_acquire);
}
void standalone_video_page_set_leaked_gauge_visible(bool suppress) noexcept {
    g_pause_gauge_leak_guard.store(suppress,std::memory_order_release);
}
bool standalone_video_page_suppress_leaked_gauge() noexcept {
    return g_pause_gauge_leak_guard.load(std::memory_order_acquire);
}
bool standalone_video_page_owns_navigation(bool& main,
                                           bool& pause) noexcept {
    main=g_visible.load(std::memory_order_acquire) &&
         g_drew.load(std::memory_order_acquire);
    pause=standalone_video_page_pause_is_drawn();
    return g_configured.load(std::memory_order_acquire) &&
           (main||pause);
}
// Same render-thread frame as the native UI. Only the previously
// authenticated, installed Pause model may enable early suppression.
bool standalone_video_page_pause_render_ready() noexcept {
    if(!g_pause_frame_checked) {
        g_pause_frame_checked=true;
        g_pause_frame_active=pause_video_live_active();
    }
    return g_pause_frame_active && !g_pause_early_failed &&
           g_configured.load(std::memory_order_acquire) &&
           find_glide() && prepare_mask();
}
bool standalone_video_page_filter_native_glyphs(float& xmin,float& ymin,float& xmax,float& ymax) noexcept {
    // MENU retains its existing successful-draw guard. Pause samples
    // live state before native drawing and preflights the replacement
    // resources; a failed LFB draw disables early filtering next frame.
    const bool menu=g_visible.load(std::memory_order_acquire);
    const bool pause=!menu && standalone_video_page_pause_render_ready();
    const bool drew=menu ? g_drew.load(std::memory_order_acquire) : pause;
    const bool visible=(menu||pause) && drew;
    const float source_ymin=ymin,source_ymax=ymax;
    const bool title=visible &&
        standalone_video_stabilize_native_title_for_context(
            pause,xmin,ymin,xmax,ymax);
    if(menu && title &&
       !g_title_preserved_logged.exchange(true) && g_log)
        g_log("RUNTIME-STANDALONE-VIDEO: native-title-stabilized=1 source=JFFTXT title-row=20..25 video-page-only");
    if(pause && visible && title &&
       !g_pause_title_restored_logged.exchange(true) && g_log) {
        char msg[240]{};
        std::snprintf(msg,sizeof(msg),
            "RUNTIME-PAUSE-VIDEO-PAGE: native-VIDEO-title-restored=1 source=verified-JFFTXT original-y=%.2f..%.2f shared-title-y=20..25 no-game-assets=1",
            double(source_ymin),double(source_ymax));
        g_log(msg);
    }
    if(pause && visible && !title &&
       !g_pause_title_band_sampled.exchange(true) && g_log &&
       xmin>25.0f && xmax<75.0f &&
       source_ymin>=8.0f && source_ymax<=34.0f) {
        char msg[240]{};
        std::snprintf(msg,sizeof(msg),
            "RUNTIME-PAUSE-VIDEO-PAGE: original-heading-candidate-unmatched=1 source=JFFTXT x=%.2f..%.2f y=%.2f..%.2f",
            double(xmin),double(xmax),
            double(source_ymin),double(source_ymax));
        g_log(msg);
    }
    if(pause && visible && !title &&
       !g_pause_native_glyphs_suppressed.exchange(true) && g_log)
        g_log("RUNTIME-PAUSE-VIDEO-PAGE: source-glyph-suppressed=1 source=verified-native-JFFTXT scope=only-normal9-reflex2 parent-Options-untouched=1 title=preserved");
    // This callback is reached only by the approved JFFTXT source
    // renderer. Suppress menu Options glyphs (SON / CONTROLES) while
    // the read-only Pause VIDEO panel is visible, but preserve VIDEO
    // itself, all game/world objects and the return-to-Options screen.
    return standalone_video_hide_native_glyphs(visible,drew,true,title);
}
bool standalone_video_page_draw_before_flip() noexcept {
    const bool menu=g_visible.load(std::memory_order_acquire);
    const bool pause=!menu && pause_video_live_active();
    g_pause_frame_checked=false;
    g_pause_frame_active=false;
    standalone_video_page_set_pause_visible(pause);
    if(!pause)g_pause_early_failed=false;
    if(!(menu || pause) ||
       !g_configured.load(std::memory_order_acquire))return false;
    if(!find_glide()) {
        if(!g_glide_missing.exchange(true) && g_log)
            g_log("RUNTIME-STANDALONE-VIDEO: glide-lfb-unavailable original-flip-preserved=1");
        return false;
    }
    if(!prepare_mask())return false;
    const unsigned panel_y=video_info_panel_y(pause);
    if(!g_read(kBackBuffer,kX,panel_y,kW,kH,kW*2u,g_pixels.data())) {
        if(pause)g_pause_early_failed=true;
        return false;
    }
    // Preserve the animated 3D scene behind a subtle translucent
    // midnight-blue panel. Glide2's LFB presents red/blue reversed:
    // input (74,38,18) is the desired visual RGB(18,38,74).
    // Composite on the private LFB copy only; the native scene,
    // butterfly and game resources remain completely untouched.
    for(unsigned y=0;y<kH;++y)for(unsigned x=0;x<kW;++x) {
        const unsigned i=y*kW+x;
        constexpr unsigned kPanelR=74u,kPanelG=38u,kPanelB=18u;
        constexpr unsigned kPanelAlpha=kVideoInfoPanelAlpha; // 40% tint, 60% live scene
        constexpr unsigned kBorderR=255u,kBorderG=218u,kBorderB=145u;
        const auto c=g_panel_mask[i];
        auto pix=g_pixels[i];
        if(c.fill==kSamples)
            pix=blend_565(pix,kPanelR,kPanelG,kPanelB,kPanelAlpha);
        else if(c.border==kSamples)
            pix=blend_565(pix,kBorderR,kBorderG,kBorderB,255u);
        else if(c.fill || c.border) {
            // Disjoint subpixel areas: combine tint and opaque outline
            // against the original live scene in a single blend.
            const unsigned f=c.fill*kPanelAlpha,b=c.border*255u,a=f+b;
            pix=blend_565(pix,
                (f*kPanelR+b*kBorderR+a/2u)/a,
                (f*kPanelG+b*kBorderG+a/2u)/a,
                (f*kPanelB+b*kBorderB+a/2u)/a,
                (a+kSamples/2u)/kSamples);
        }
        const unsigned m=g_text_mask[i];
        if(m)pix=blend_565(pix,255u,255u,255u,m);
        g_pixels[i]=pix;
    }
    if(!g_write(kBackBuffer,kX,panel_y,kRgb565,kW,kH,
                int(kW*2u),g_pixels.data())) {
        if(pause)g_pause_early_failed=true;
        if(!g_write_error.exchange(true) && g_log)
            g_log("RUNTIME-STANDALONE-VIDEO: LFB-write-refused original-flip-preserved=1");
        return false;
    }
    if(!g_drew.exchange(true) && g_log)
        g_log("RUNTIME-STANDALONE-VIDEO: frame-rendered=1 source=own-Glide-LFB background=translucent-midnight-blue alpha=102/255 opacity=40% blue-outline=verified-BGR565-blue border-width=2 controls=Escape-only source=MENU-or-verified-ESC-script corner-radius=12 coverage=4x text-supersampling=4x");
    if(pause) {
        g_pause_early_failed=false;
        g_pause_drawn_this_visit.store(true,std::memory_order_release);
        g_pause_gauge_leak_guard.store(true,std::memory_order_release);
    }
    if(pause && !g_pause_draw_reported.exchange(true) && g_log)
        g_log("RUNTIME-PAUSE-VIDEO-PAGE: frame-rendered=1 source=own-Glide-LFB state=verified-normal9-reflex2 background=translucent-midnight-blue original-scene=preserved panel-y=284 panel-height=368 rows=6 layout=shared-with-MENU no-actor-transform=1");
    return true;
}
} // namespace r2fix
