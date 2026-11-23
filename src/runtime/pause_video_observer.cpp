// Bounded, read-only observer for the in-game ESC/Options/VIDEO scene.
// Shared Fix.sna has a distinct 23-normal / 6-reflex menu controller.
// Never write game memory, change settings, intercept input or draw UI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "rayman2fix/pause_video_model_policy.hpp"
#include "rayman2fix/pause_video_observer.hpp"
namespace r2fix {
namespace {
constexpr std::uintptr_t kLevelRva=0x100381u;
constexpr std::uintptr_t kGenericPauseRva=0x0B9BC8u;
constexpr unsigned kChunk=64u*1024u;
constexpr unsigned kBudget=160u*1024u*1024u;
std::atomic<bool> g_started{false};
PauseVideoLogger g_log=nullptr;
bool read32(std::uintptr_t p,std::uint32_t& out) noexcept {
    if(!menu_safe_ptr(p))return false;
    SIZE_T size=0;
    return ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(p),&out,4,&size) && size==4;
}
bool read8(std::uintptr_t p,std::uint8_t& out) noexcept {
    if(!menu_safe_ptr(p,1u))return false;
    SIZE_T size=0;
    return ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(p),&out,1,&size) && size==1;
}
bool game_in_menu(std::uintptr_t base) noexcept {
    char name[4]{};
    SIZE_T size=0;
    if(!base || !ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<const void*>(base+kLevelRva),
            name,sizeof(name),&size) || size!=sizeof(name))
        return false;
    return (name[0]=='M'||name[0]=='m') &&
           (name[1]=='E'||name[1]=='e') &&
           (name[2]=='N'||name[2]=='n') &&
           (name[3]=='U'||name[3]=='u');
}
bool memory_page(DWORD flags) noexcept {
    if(flags&(PAGE_GUARD|PAGE_NOACCESS))return false;
    switch(flags&0xFFu) {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:return true;
        default:return false;
    }
}
struct Scan {PauseVideoModel found{};unsigned matches=0,scanned_mib=0;};
// No static game scene VA is trusted. Accept exactly one full model
// with verified native calls AND reciprocal Mind links.
Scan scan_controller() noexcept {
    Scan result{};
    SYSTEM_INFO sys{};
    GetSystemInfo(&sys);
    const auto last=std::min<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(sys.lpMaximumApplicationAddress),
        0x7FFF0000u);
    std::array<BYTE,kChunk+20u> buffer{};
    unsigned processed=0u;
    for(std::uintptr_t cursor=0x10000u;
        cursor<last && processed<kBudget;) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(cursor),
                         &info,sizeof(info)) || !info.RegionSize)break;
        const auto start=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto end=std::min(last,start+
            static_cast<std::uintptr_t>(info.RegionSize));
        if(end<=cursor)break;
        if(info.State==MEM_COMMIT && info.Type==MEM_PRIVATE &&
           memory_page(info.Protect)) {
            for(auto at=std::max(start,cursor);
                at<end && processed<kBudget;) {
                const auto length=static_cast<SIZE_T>(
                    std::min<std::uintptr_t>(end-at,kChunk+20u));
                SIZE_T size=0;
                if(ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<const void*>(at),
                    buffer.data(),length,&size) && size>=20u) {
                    const unsigned first=unsigned((4u-(at&3u))&3u);
                    for(unsigned i=first;i+20u<=size;i+=4u) {
                        std::uint32_t flags=0;
                        std::memcpy(&flags,buffer.data()+i+16u,4u);
                        if(flags!=1u)continue;
                        PauseVideoModel candidate{};
                        if(inspect_pause_video_model(at+i,&read32,&read8,
                                                     candidate)) {
                            ++result.matches;
                            if(result.matches==1u)result.found=candidate;
                            if(result.matches>1u)return result;
                        }
                    }
                }
                const auto step=std::min<std::uintptr_t>(kChunk,end-at);
                at+=step;
                processed+=static_cast<unsigned>(step);
            }
        }
        cursor=end;
    }
    result.scanned_mib=(processed+1024u*1024u-1u)/(1024u*1024u);
    return result;
}
DWORD WINAPI worker(void*) {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!base || !g_log)return 0;
    g_log("RUNTIME-PAUSE-VIDEO-MODEL: observer=started read-only=1 expected-normal=23 expected-reflex=6 no-game-patch=1");
    PauseVideoModel state{};
    unsigned tries=0,reports=0;
    DWORD next_scan=0;
    int prior_normal=-100,prior_reflex=-100,prior_pause=-1;
    for(;;) {
        std::uint8_t flag=0;
        const bool in_menu=game_in_menu(base);
        const bool pause=!in_menu &&
            read8(base+kGenericPauseRva,flag) && flag!=0;
        if(int(pause)!=prior_pause) {
            if(reports++<64u) {
                char msg[160]{};
                std::snprintf(msg,sizeof(msg),
                    "RUNTIME-PAUSE-VIDEO-MODEL: generic-pause=%u scope=read-only",
                    unsigned(pause));
                g_log(msg);
            }
            prior_pause=int(pause);
        }
        if(!pause) {
            prior_normal=-100;prior_reflex=-100;
            // The shared Fix scene may be recreated on return to MENU.
            if(in_menu) {state={};tries=0;next_scan=0;}
            Sleep(250);
            continue;
        }
        if(state.valid()) {
            PauseVideoModel current{};
            if(!inspect_pause_video_model(state.model,&read32,&read8,current)) {
                state={};
                tries=0;
            } else if((current.normal!=prior_normal ||
                       current.reflex!=prior_reflex) && reports<64u) {
                const bool candidate=current.normal==9 && current.reflex==0;
                char msg[240]{};
                std::snprintf(msg,sizeof(msg),
                    "RUNTIME-PAUSE-VIDEO-MODEL: controller=verified normal=%d reflex=%d candidate-video=%u verification=passive-no-render",
                    current.normal,current.reflex,unsigned(candidate));
                g_log(msg);
                ++reports;
                prior_normal=current.normal;
                prior_reflex=current.reflex;
            }
        }
        const DWORD now=GetTickCount();
        if(!state.valid() && tries<3u &&
           static_cast<LONG>(now-next_scan)>=0) {
            next_scan=now+3000u;
            ++tries;
            const auto result=scan_controller();
            if(result.matches==1u && result.found.valid()) {
                state=result.found;
                if(reports++<64u)g_log(
                    "RUNTIME-PAUSE-VIDEO-MODEL: status=controller-found unique=1 normal-count=23 reflex-count=6 fingerprints=verified no-settings-changed=1");
            } else if(reports++<64u) {
                char msg[220]{};
                std::snprintf(msg,sizeof(msg),
                    "RUNTIME-PAUSE-VIDEO-MODEL: scan=%u matches=%u scanned_mib=%u fail-closed=1",
                    tries,result.matches,result.scanned_mib);
                g_log(msg);
            }
        }
        Sleep(250);
    }
    return 0;
}
} // namespace
void start_pause_video_observer(PauseVideoLogger logger) noexcept {
    if(!logger || g_started.exchange(true,std::memory_order_acq_rel))return;
    g_log=logger;
    HANDLE t=CreateThread(nullptr,0,&worker,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    else g_log("RUNTIME-PAUSE-VIDEO-MODEL: observer-thread-failed");
}
} // namespace r2fix
