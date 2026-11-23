#include "rayman2fix/standalone_video_page.hpp"
#include "rayman2fix/standalone_video_device_policy.hpp"
#include "rayman2fix/standalone_video_layout_policy.hpp"
#include "rayman2fix/video_navigation_input.hpp"
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
namespace {
// Keep the independent page below the animated native 3D butterfly.
// The approved Glide internal canvas is 1024x768, regardless of the
// output desktop's aspect ratio or scaling (e.g. 1920x1200).
// Center on the approved 1024x768 Glide frame, below the native VIDEO title.
constexpr unsigned kW=kVideoInfoWidth,kH=kVideoInfoHeight;
constexpr unsigned kX=kVideoInfoX,kY=kVideoMainInfoY;
static_assert(kX==212u && kY==224u,
              "Approved MENU VIDEO panel position unchanged");
static_assert(kX+kW<=kVideoCanvasWidth &&
              video_info_panel_y(true)+kH<=kVideoCanvasHeight,
              "Both VIDEO panel placements must fit the Glide canvas");
constexpr int kBackBuffer=1,kRgb565=0;
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
std::atomic<bool> g_pause_native_glyphs_suppressed{false};
GlideRead g_read=nullptr;
GlideWrite g_write=nullptr;
std::array<std::array<char,128>,5> g_lines{};
constexpr const char* kRowLabels[5]{"SORTIE","GPU","PILOTE","RENDU","INTERNE"};
std::atomic<bool> g_title_preserved_logged{false};
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
    SetTextColor(dc,RGB(255,255,255));
    // The title uses the original Rayman 2 JFFTXT glyphs / animation,
    // not a GDI imitation. Only the data rows are rasterized here.
    // Fit one consistent font size to the actual longest GPU / driver
    // string, using the active GDI font metrics (also valid under Wine).
    // This avoids the previous truncated "AMD Radeon RX 6600M (RADV N..."
    // while retaining the approved blue panel and five-row positions.
    HFONT font=nullptr;
    int font_px=0;
    bool all_lines_fit=false;
    for(int px=kVideoInfoFontPreferredPx;
        px>=kVideoInfoFontMinPx;--px) {
        HFONT candidate=CreateFontA(-px,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
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
               label_size.cx>kVideoInfoValueLeft-kVideoInfoLabelLeft-
                                 kVideoInfoTextPadding ||
               value_size.cx>kVideoInfoValueRight-kVideoInfoValueLeft-
                                 kVideoInfoTextPadding)
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
        const int y=42+int(i)*53;
        RECT label{kVideoInfoLabelLeft,y,166,y+38};
        RECT value{kVideoInfoValueLeft,y,kVideoInfoValueRight,
                   y+(all_lines_fit?38:48)};
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
    if(!wine)
        primary_display_driver(primary,gpu,unsigned(sizeof(gpu)),
                               driver_version,unsigned(sizeof(driver_version)));
    std::snprintf(g_lines[0].data(),128,"%u x %u  %u:%u  %s",
        out_w,out_h,a,b,classic?"4:3":"AUTO");
    std::snprintf(g_lines[1].data(),128,"%s",gpu);
    std::snprintf(g_lines[2].data(),128,"%s",
        wine?"Pilote hote non expose (Wine)":
        driver_version[0]?driver_version:"Version non disponible");
    std::snprintf(g_lines[3].data(),128,"GAMEFRAME / GLIDE2");
    std::snprintf(g_lines[4].data(),128,"1024 x 768 x 16");
    g_configured.store(true,std::memory_order_release);
    install_video_navigation_input(g_log);
    if(g_log)g_log("RUNTIME-STANDALONE-VIDEO: configured independent-page=1 native-title=original-video centered-panel=1 rows=5 win32-driver=verified-setupapi renderer=Glide2-LFB");
}

void standalone_video_page_set_visible(bool value) noexcept {
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
bool standalone_video_page_owns_navigation(bool& main,
                                           bool& pause) noexcept {
    main=g_visible.load(std::memory_order_acquire) &&
         g_drew.load(std::memory_order_acquire);
    pause=standalone_video_page_pause_is_drawn();
    return g_configured.load(std::memory_order_acquire) &&
           (main||pause);
}
bool standalone_video_page_filter_native_glyphs(float& xmin,float& ymin,float& xmax,float& ymax) noexcept {
    // Do not hide any native glyph before our replacement page has
    // successfully rendered at least once; unsupported Glide LFB
    // implementations keep the original UI visible, not a blank page.
    const bool menu=g_visible.load(std::memory_order_acquire);
    const bool pause=g_pause_visible.load(std::memory_order_acquire);
    const bool drew=menu ? g_drew.load(std::memory_order_acquire) :
                          g_pause_drawn_this_visit.load(std::memory_order_acquire);
    const bool visible=(menu||pause) && drew;
    const bool title=visible &&
        standalone_video_stabilize_native_title(xmin,ymin,xmax,ymax);
    if(menu && title &&
       !g_title_preserved_logged.exchange(true) && g_log)
        g_log("RUNTIME-STANDALONE-VIDEO: native-title-stabilized=1 source=JFFTXT title-row=20..25 video-page-only");
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
    const bool pause=g_pause_visible.load(std::memory_order_acquire);
    if(!(menu || pause) ||
       !g_configured.load(std::memory_order_acquire))return false;
    if(!find_glide()) {
        if(!g_glide_missing.exchange(true) && g_log)
            g_log("RUNTIME-STANDALONE-VIDEO: glide-lfb-unavailable original-flip-preserved=1");
        return false;
    }
    if(!prepare_mask())return false;
    const unsigned panel_y=video_info_panel_y(pause);
    if(!g_read(kBackBuffer,kX,panel_y,kW,kH,kW*2u,g_pixels.data()))
        return false;
    // Preserve the animated 3D scene behind a subtle translucent
    // midnight-blue panel. Glide2's LFB presents red/blue reversed:
    // input (74,38,18) is the desired visual RGB(18,38,74).
    // Composite on the private LFB copy only; the native scene,
    // butterfly and game resources remain completely untouched.
    for(unsigned y=0;y<kH;++y)for(unsigned x=0;x<kW;++x) {
        const unsigned i=y*kW+x;
        constexpr unsigned kPanelR=74u,kPanelG=38u,kPanelB=18u;
        constexpr unsigned kPanelAlpha=100u; // 39% tint, 61% live scene
        constexpr unsigned kBorderR=255u,kBorderG=218u,kBorderB=145u;
        const bool outline=x<2u || y<2u || x>=kW-2u || y>=kH-2u;
        auto pix=blend_565(g_pixels[i],
            kPanelR,kPanelG,kPanelB,kPanelAlpha);
        if(outline)pix=blend_565(pix,
            kBorderR,kBorderG,kBorderB,255u);
        const auto value=g_text_mask[i];
        const unsigned m=value&255u;
        if(m)pix=blend_565(pix,255u,255u,255u,m);
        g_pixels[i]=pix;
    }
    if(!g_write(kBackBuffer,kX,panel_y,kRgb565,kW,kH,
                int(kW*2u),g_pixels.data())) {
        if(!g_write_error.exchange(true) && g_log)
            g_log("RUNTIME-STANDALONE-VIDEO: LFB-write-refused original-flip-preserved=1");
        return false;
    }
    if(!g_drew.exchange(true) && g_log)
        g_log("RUNTIME-STANDALONE-VIDEO: frame-rendered=1 source=own-Glide-LFB background=translucent-midnight-blue alpha=100/255 blue-outline=verified-BGR565-blue border-width=2 controls=Escape-only source=MENU-or-verified-ESC-script");
    if(pause)
        g_pause_drawn_this_visit.store(true,std::memory_order_release);
    if(pause && !g_pause_draw_reported.exchange(true) && g_log)
        g_log("RUNTIME-PAUSE-VIDEO-PAGE: frame-rendered=1 source=own-Glide-LFB state=verified-normal9-reflex2 background=translucent-midnight-blue original-scene=preserved panel-y=280 native-luciole-space=+56 no-actor-transform=1");
    return true;
}
} // namespace r2fix
