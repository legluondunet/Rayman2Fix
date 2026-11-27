#include "rayman2fix/standalone_video_page.hpp"
#include "rayman2fix/controller_info_renderer.hpp"
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
#include <chrono>

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
constexpr const char* kRowLabels[kVideoInfoRowCount]{"SORTIE","GPU","PILOTE","RENDU","INTERNE","Rayman2Fix"};
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
// Some display drivers store these strings as UTF-16 REG_BINARY.
bool read_adapter_property(HKEY key,const wchar_t* property,
                           char* out,unsigned capacity) noexcept {
    if(!key || !out || capacity<2u)return false;
    wchar_t value[256]{};
    DWORD type=0,bytes=DWORD(sizeof(value)-sizeof(wchar_t));
    if(RegQueryValueExW(key,property,nullptr,&type,
            reinterpret_cast<BYTE*>(value),&bytes)!=ERROR_SUCCESS ||
       (type!=REG_SZ && type!=REG_BINARY) || bytes<sizeof(wchar_t) ||
       bytes%sizeof(wchar_t))return false;
    value[bytes/sizeof(wchar_t)]=L'\0';
    for(const wchar_t* p=value;*p;++p)
        if(*p<32 || *p==127)return false;
    if(!value[0])return false;
    return WideCharToMultiByte(CP_ACP,0,value,-1,out,int(capacity),
                              nullptr,nullptr)>1;
}
bool generic_amd_renderer(const char* name) noexcept {
    return name && (lstrcmpiA(name,"AMD Radeon(TM) Graphics")==0 ||
                    lstrcmpiA(name,"AMD Radeon Graphics")==0 ||
                    lstrcmpiA(name,"AMD Radeon(TM) Vega Graphics")==0);
}
void primary_display_driver(const DISPLAY_DEVICEA& primary,
                            char* friendly,unsigned friendly_capacity,
                            char* version,unsigned version_capacity,
                            char* chip=nullptr,unsigned chip_capacity=0,
                            int* revision=nullptr) noexcept {
    if(!primary.DeviceID[0] || !friendly || !version ||
       friendly_capacity<2u || version_capacity<2u)return;
    const HDEVINFO devices=SetupDiGetClassDevsA(
        &kDisplayClassGuid,nullptr,nullptr,DIGCF_PRESENT);
    if(devices==INVALID_HANDLE_VALUE)return;
    unsigned matches=0;
    char matched_name[128]{},matched_version[96]{},matched_chip[128]{};
    int matched_revision=-1;
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
        matched_revision=display_pci_revision(instance_id);
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
            char adapter[128]{};
            if(read_adapter_property(key,L"HardwareInformation.AdapterString",
                                     adapter,unsigned(sizeof(adapter))) &&
               (!matched_name[0] ||
                (generic_amd_renderer(matched_name) && !generic_amd_renderer(adapter))))
                std::snprintf(matched_name,sizeof(matched_name),"%s",adapter);
            read_adapter_property(key,L"HardwareInformation.ChipType",
                                  matched_chip,unsigned(sizeof(matched_chip)));
            RegCloseKey(key);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    if(matches!=1u)return;
    if(matched_name[0])
        std::snprintf(friendly,friendly_capacity,"%s",matched_name);
    if(matched_version[0])
        std::snprintf(version,version_capacity,"%s",matched_version);
    if(chip && chip_capacity && matched_chip[0])
        std::snprintf(chip,chip_capacity,"%s",matched_chip);
    if(revision)*revision=matched_revision;
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
    bool have_identity=false,ambiguous=false;
    unsigned matching_outputs=0;
    for(DWORD i=0;i<32u;++i) {
        DISPLAY_DEVICEA d{};d.cb=sizeof(d);
        if(!EnumDisplayDevicesA(nullptr,i,&d,0))break;
        if(lstrcmpiA(d.DeviceString,renderer)!=0)continue;
        ++matching_outputs;
        const auto v=pci_token(d.DeviceID,"VEN_");
        const auto p=pci_token(d.DeviceID,"DEV_");
        if(!v || !p)continue;
        if(!have_identity) {
            matched=d;
            have_identity=true;
        } else if(!matching_display_pci_identity(matched.DeviceID,d.DeviceID)) {
            ambiguous=true;
        }
        // Different desktop outputs can name the same PCI GPU family.
        // Prefer the primary output only within the same PCI identity.
        if(!ambiguous && (d.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE))
            matched=d;
    }
    const bool unique_identity=have_identity && !ambiguous;
    char friendly[128]{},version[96]{},chip[128]{};
    int revision=unique_identity?display_pci_revision(matched.DeviceID):-1;
    int setup_revision=-1;
    if(unique_identity)
        primary_display_driver(matched,friendly,unsigned(sizeof(friendly)),
                               version,unsigned(sizeof(version)),
                               chip,unsigned(sizeof(chip)),&setup_revision);
    if(revision<0)revision=setup_revision;
    else if(setup_revision>=0 && setup_revision!=revision)revision=-1;
    if(version[0])
        std::snprintf(g_lines[2].data(),128,"%s",version);
    else if(g_lines[2][0] && (ambiguous ||
            lstrcmpiA(g_lines[1].data(),renderer)!=0)) {
        char value[128]{};
        std::snprintf(value,sizeof(value),"%.100s (ecran)",g_lines[2].data());
        std::snprintf(g_lines[2].data(),128,"%s",value);
    }
    std::snprintf(g_lines[1].data(),128,"%s",renderer);
    const auto v=unique_identity?pci_token(matched.DeviceID,"VEN_"):nullptr;
    const auto d=unique_identity?pci_token(matched.DeviceID,"DEV_"):nullptr;
    const char* model_source="GL_RENDERER";
    if(generic_amd_renderer(renderer)) {
        char identity[64]{};
        if(v && d && revision>=0)
            std::snprintf(identity,sizeof(identity),
                "PCI\\VEN_%.4s&DEV_%.4s&REV_%02X",v,d,unsigned(revision));
        const char* model=amd_hawkpoint_model(identity);
        if(model) {
            std::snprintf(g_lines[1].data(),128,"%s",model);
            model_source="AMD-PCI-device-revision";
        } else if(unique_identity && friendly[0] &&
                  !generic_amd_renderer(friendly) &&
                  !std::strstr(friendly,"Graphics Processor")) {
            std::snprintf(g_lines[1].data(),128,"%s",friendly);
            model_source="matched-SetupAPI";
        } else {
            // Never expand a generic description into a long technical
            // string. Keep PCI and chip details in the log, not the panel.
            if(v && d && matching_display_pci_identity(matched.DeviceID,
                                      "PCI\\VEN_1002&DEV_1900"))
                std::snprintf(g_lines[1].data(),128,"AMD Radeon 760M / 780M");
            else
                std::snprintf(g_lines[1].data(),128,"AMD Radeon Graphics");
            model_source="model-unresolved";
        }
    }
    // Remove trademark and API suffixes, without inventing a model.
    for(const char* token:{"(TM)","(tm)","(R)","(r)"}) {
        char* at=nullptr;
        while((at=std::strstr(g_lines[1].data(),token))!=nullptr)
            std::memmove(at,at+std::strlen(token),std::strlen(at+std::strlen(token))+1u);
    }
    char* name=g_lines[1].data();
    if(char* suffix=std::strstr(name,"/PCIe/SSE2"))*suffix='\0';
    if(char* suffix=std::strstr(name," (RADV"))*suffix='\0';
    if(char* suffix=std::strstr(name," (radeonsi"))*suffix='\0';
    for(char* p=name;*p;++p)
        while(*p==' ' && p[1]==' ')std::memmove(p,p+1,std::strlen(p));
    // NVIDIA/AMD retail names remain intact; strip only the redundant
    // final Graphics suffix from names that contain a numerical model.
    constexpr const char* graphics=" Graphics";
    const auto len=std::strlen(name),tail=std::strlen(graphics);
    bool has_digit=false;
    for(const char* p=name;*p;++p)if(*p>='0'&&*p<='9')has_digit=true;
    if(has_digit && len>tail && std::strcmp(name+len-tail,graphics)==0)
        name[len-tail]='\0';
    if(g_log) {
        char msg[640]{};
        std::snprintf(msg,sizeof(msg),
            "RUNTIME-VIDEO-GPU: source=GameFrame-GL_RENDERER name=%s model-source=%s matching-outputs=%u unique-pci=%u ambiguous=%u pci=%.4s:%.4s revision=%d setupapi-name=%s chip=%s driver-matched=%u",
            g_lines[1].data(),model_source,matching_outputs,
            unsigned(unique_identity),unsigned(ambiguous),
            v?v:"none",d?d:"none",revision,friendly[0]?friendly:"none",
            chip[0]?chip:"none",unsigned(version[0]!=0));
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
    std::snprintf(g_lines[5].data(),128,"%s-%s",
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
namespace {
using PadPerfClock=std::chrono::steady_clock;
unsigned pad_perf_us(PadPerfClock::time_point begin,PadPerfClock::time_point end) noexcept {
    return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count());
}
struct ControllerOverlayCache {
    std::vector<PadOverlayPixel> overlay;
    std::vector<PadPreparedPixel> prepared;
    unsigned prepared_width=0;
    std::vector<std::uint16_t> scene;
    bool valid=false;
    PadInfoLocale locale=PadInfoLocale::en;
    PadLabelFamily family=PadLabelFamily::generic;
    bool connected=false;
    bool matches(PadInfoLocale l,bool c,PadLabelFamily f) const noexcept {
        return valid && locale==l && connected==c && family==f;
    }
    bool prepare(PadInfoLocale l,bool c,PadLabelFamily f,const char* source) noexcept {
        if(matches(l,c,f))return true;
        const auto begin=PadPerfClock::now();
        if(!prepare_controller_info_overlay(make_pad_info_view(l,c,f),overlay))return false;
        valid=true;locale=l;connected=c;family=f;prepared_width=0;
        if(g_log) {
            char line[180]{};
            std::snprintf(line,sizeof(line),
                "RUNTIME-CONTROLLER-PERF: stage=prepare source=%s locale=%u family=%u connected=%u elapsed_us=%u",
                source,unsigned(l),unsigned(f),unsigned(c),pad_perf_us(begin,PadPerfClock::now()));
            g_log(line);
        }
        return true;
    }
    bool prepare_width(unsigned width) noexcept {
        if(width && prepared_width==width)return true;
        if(!prepare_controller_composite(overlay.data(),overlay.size(),width,prepared))return false;
        prepared_width=width;
        return true;
    }
};
// Both callers run on the authenticated camera/render thread. No worker,
// Glide access or game memory writes are needed to prepare the static art.
ControllerOverlayCache& controller_overlay_cache(PadLabelFamily family) {
    // At most three entries: one per physical layout. Rebuild only a
    // changed locale/connection state/width, never on a family round-trip.
    static std::array<ControllerOverlayCache,3> caches;
    return caches[family==PadLabelFamily::xbox?1u:family==PadLabelFamily::playstation?2u:0u];
}
}
bool warm_controller_info_overlay(PadInfoLocale locale,bool connected,PadLabelFamily family,float hud_scale) noexcept {
    if(!g_configured.load(std::memory_order_acquire))return false;
    auto& cache=controller_overlay_cache(family);
    const auto placement=pad_info_placement(hud_scale);
    return placement.width && cache.prepare(locale,connected,family,"prewarm") && cache.prepare_width(placement.width);
}
bool draw_controller_info_glide(PadInfoLocale locale,bool connected,PadLabelFamily family,float hud_scale,PadInfoActivity activity) noexcept {
    if(!g_configured.load(std::memory_order_acquire) || !find_glide())return false;
    const auto placement=pad_info_placement(hud_scale);
    if(!placement.width)return false;
    try {
        auto& cache=controller_overlay_cache(family);
        const auto begin=PadPerfClock::now();
        const bool cache_hit=cache.matches(locale,connected,family) && cache.prepared_width==placement.width;
        if(!cache.prepare(locale,connected,family,"draw") || !cache.prepare_width(placement.width))return false;
        auto& scene=cache.scene;
        scene.resize(std::size_t(placement.width)*kPadInfoHeight);
        const auto ready=PadPerfClock::now();
        if(!g_read(kBackBuffer,placement.x,kPadInfoY,placement.width,kPadInfoHeight,
                   placement.width*2u,scene.data()))return false;
        const auto read_done=PadPerfClock::now();
        // GameFrame readback is RGB565 even with the game's ABGR
        // write format. Otherwise the transparent scene swaps R/B.
        for(auto& pixel:scene)pixel=pad_scene_read_to_write565(pixel);
        if(!composite_prepared_controller(scene.data(),scene.size(),cache.prepared.data(),cache.prepared.size(),placement.width))
            return false;
        if(!apply_pad_info_lights(scene.data(),scene.size(),placement.width,family,connected?activity:PadInfoActivity{}))return false;
        const auto composite_done=PadPerfClock::now();
        const bool ok=g_write(kBackBuffer,placement.x,kPadInfoY,kRgb565,placement.width,kPadInfoHeight,
                       placement.width*2,scene.data())!=0;
        const auto end=PadPerfClock::now();
        static unsigned reports=0,slow_reports=0,frame=0;
        const unsigned total=pad_perf_us(begin,end);
        ++frame;
        const bool regular=reports<24u && (reports<4u || frame%120u==0u || !cache_hit);
        const bool slow=slow_reports<12u && total>=16000u;
        if(g_log && (regular || slow)) {
            if(regular)++reports;
            else ++slow_reports;
            char line[256]{};
            std::snprintf(line,sizeof(line),
                "RUNTIME-CONTROLLER-PERF: stage=draw cache_hit=%u prepare_us=%u read_us=%u composite_us=%u write_us=%u total_us=%u ok=%u",
                unsigned(cache_hit),pad_perf_us(begin,ready),pad_perf_us(ready,read_done),
                pad_perf_us(read_done,composite_done),pad_perf_us(composite_done,end),total,unsigned(ok));
            g_log(line);
        }
        return ok;
    }catch(...){return false;}
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
