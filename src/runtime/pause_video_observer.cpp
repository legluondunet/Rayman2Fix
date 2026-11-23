// Bounded shared Fix.sna model observer and experimental, fail-closed
// in-memory VIDEO page replacement. No disk assets or executable code
// are modified. Normal gameplay and unrelated menus are untouched.
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
#include "rayman2fix/pause_video_patch_policy.hpp"
#include "rayman2fix/standalone_video_page.hpp"
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
// Each published private script remains allocated for process lifetime.
// This is required because the original AI interpreter may hold a reader
// pointer to the previous script during the atomic menu transition.
struct PatchSlot {
    std::uintptr_t slot=0, original=0;
    NativeMenuScriptNode* clone=nullptr;
};
std::array<PatchSlot,5> g_scripts{};
bool g_scripts_installed=false;
bool writable(std::uintptr_t at) noexcept {
    if(!at || at%4u)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof(info))!=
       sizeof(info) || info.State!=MEM_COMMIT ||
       !memory_page(info.Protect))return false;
    const auto start=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    return at>=start && static_cast<std::uintptr_t>(info.RegionSize)>=4u &&
           at-start<=static_cast<std::uintptr_t>(info.RegionSize)-4u;
}
bool install_pause_video_scripts(const PauseVideoModel& model) noexcept {
    if(g_scripts_installed)return true;
    if(!model.valid())return false;
    std::array<PatchSlot,5> prepared{};
    unsigned built=0u;
    const char* reason="unverified";
    bool valid=true;
    for(unsigned k=0;k<5u;++k) {
        const auto& p=kPauseVideoPlans[k];
        const auto entries=model.normal_entries+p.behavior*12u;
        std::uint32_t list=0,original=0;
        std::uint8_t count=0;
        if(!read32(entries,list) ||
           !read8(entries+8u,count) ||
           count!=(p.behavior==9u?5u:3u) ||
           !menu_safe_ptr(list,4u*(unsigned(count))) ||
           !read32(std::uintptr_t(list)+4u*p.script,original) ||
           !menu_safe_ptr(original,p.count*sizeof(NativeMenuScriptNode))) {
            valid=false;reason="script-layout";break;
        }
        const auto slot=std::uintptr_t(list)+4u*p.script;
        if(!writable(slot)) {valid=false;reason="pointer-slot";break;}
        const SIZE_T bytes=p.count*sizeof(NativeMenuScriptNode);
        auto* clone=static_cast<NativeMenuScriptNode*>(
            VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,
                         PAGE_READWRITE));
        if(!clone) {valid=false;reason="clone-allocation";break;}
        prepared[k]={slot,original,clone};
        ++built;
        SIZE_T done=0;
        if(!ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<void*>(std::uintptr_t(original)),
                clone,bytes,&done) || done!=bytes ||
           !rewrite_pause_video_script(clone,p)) {
            valid=false;reason="original-script-signature";break;
        }
    }
    if(!valid) {
        for(unsigned i=0;i<built;++i)
            if(prepared[i].clone)
                VirtualFree(prepared[i].clone,0,MEM_RELEASE);
        if(g_log) {
            char msg[200]{};
            std::snprintf(msg,sizeof(msg),
                "RUNTIME-PAUSE-VIDEO-PAGE: status=preflight-refused reason=%s no-patch=1",
                reason);
            g_log(msg);
        }
        return false;
    }
    unsigned published=0u;
    for(;published<prepared.size();++published) {
        const auto& entry=prepared[published];
        auto* found=InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(entry.slot),
            entry.clone,reinterpret_cast<PVOID>(entry.original));
        if(found!=reinterpret_cast<PVOID>(entry.original))break;
    }
    if(published!=prepared.size()) {
        for(unsigned i=0;i<published;++i) {
            const auto& item=prepared[i];
            if(writable(item.slot))
                InterlockedCompareExchangePointer(
                    reinterpret_cast<PVOID volatile*>(item.slot),
                    reinterpret_cast<PVOID>(item.original),item.clone);
        }
        // Clones that have EVER been visible cannot be freed while a
        // game AI script could be executing them.
        for(unsigned i=published;i<prepared.size();++i)
            if(prepared[i].clone)
                VirtualFree(prepared[i].clone,0,MEM_RELEASE);
        if(g_log)g_log(
            "RUNTIME-PAUSE-VIDEO-PAGE: status=script-publish-refused rollback=attempted no-render=1");
        return false;
    }
    g_scripts=prepared;
    g_scripts_installed=true;
    if(g_log)g_log(
        "RUNTIME-PAUSE-VIDEO-PAGE: status=installed normal9=readonly-source-script1-singleton-1..1 scripts2,3,4=inactive normal15-script2=inactive old-video-actions=unreachable original-title-retained=1");
    return true;
}
void restore_pause_video_scripts() noexcept {
    standalone_video_page_set_pause_visible(false);
    if(!g_scripts_installed)return;
    for(const auto& entry:g_scripts)
        if(entry.slot && entry.original && entry.clone &&
           writable(entry.slot))
            InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(entry.slot),
                reinterpret_cast<PVOID>(entry.original),entry.clone);
    g_scripts={};
    g_scripts_installed=false;
    if(g_log)g_log(
        "RUNTIME-PAUSE-VIDEO-PAGE: status=scene-exit original-script-pointers-restored=attempted published-private-copies-retained=1");
}
DWORD WINAPI worker(void*) {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!base || !g_log)return 0;
    g_log("RUNTIME-PAUSE-VIDEO-MODEL: observer=started expected-normal=23 expected-reflex=6 original-assets=untouched scoped-script-copies=preflight-required");
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
            standalone_video_page_set_pause_visible(false);
            prior_normal=-100;prior_reflex=-100;
            // The shared Fix scene may be recreated on return to MENU.
            if(in_menu) {
                restore_pause_video_scripts();
                state={};tries=0;next_scan=0;
            }
            Sleep(250);
            continue;
        }
        if(state.valid()) {
            PauseVideoModel current{};
            if(!inspect_pause_video_model(state.model,&read32,&read8,current)) {
                restore_pause_video_scripts();
                state={};
                tries=0;
            } else {
                if(!g_scripts_installed)
                    install_pause_video_scripts(current);
                const bool candidate=current.normal==9 && current.reflex==2;
                standalone_video_page_set_pause_visible(
                    candidate && g_scripts_installed);
                if((current.normal!=prior_normal ||
                    current.reflex!=prior_reflex) && reports<64u) {
                char msg[240]{};
                std::snprintf(msg,sizeof(msg),
                    "RUNTIME-PAUSE-VIDEO-MODEL: controller=verified normal=%d reflex=%d candidate-video=%u page=script-verified",
                    current.normal,current.reflex,unsigned(candidate));
                g_log(msg);
                ++reports;
                prior_normal=current.normal;
                prior_reflex=current.reflex;
                }
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
                    "RUNTIME-PAUSE-VIDEO-MODEL: status=controller-found unique=1 normal-count=23 reflex-count=6 fingerprints=verified modifications=private-script-clones-only");
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
