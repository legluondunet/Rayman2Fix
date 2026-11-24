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
#include "rayman2fix/pause_menu_site_policy.hpp"
#include "rayman2fix/main_menu_site.hpp"
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
                        // Reject impossible reciprocal Mind owners from
                        // the local chunk before expensive process reads.
                        // At a chunk boundary keep the original full check.
                        if(i>=24u) {
                            std::uint32_t owner=0;
                            std::memcpy(&owner,buffer.data()+i-24u,4u);
                            if(owner!=at+i)continue;
                        }
                        std::uint32_t nlist=0,rlist=0;
                        std::memcpy(&nlist,buffer.data()+i,4u);
                        std::memcpy(&rlist,buffer.data()+i+4u,4u);
                        if(!menu_safe_ptr(nlist,8u) ||
                           !menu_safe_ptr(rlist,8u))continue;
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
bool g_preflight_failed=false;
bool g_retry_after_video=false;
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
    if(g_preflight_failed)return false;
    if(!model.valid())return false;
    std::array<PatchSlot,5> prepared{};
    unsigned built=0u;
    const char* reason="unverified";
    bool valid=true;
    unsigned failed_plan=~0u;
    std::uint64_t observed_shape=0u,zero_padding_shape=0u;
    unsigned nonzero_padding=0u;
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
            valid=false;reason="script-layout";failed_plan=k;break;
        }
        const auto slot=std::uintptr_t(list)+4u*p.script;
        if(!writable(slot)) {valid=false;reason="pointer-slot";failed_plan=k;break;}
        const SIZE_T bytes=p.count*sizeof(NativeMenuScriptNode);
        auto* clone=static_cast<NativeMenuScriptNode*>(
            VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,
                         PAGE_READWRITE));
        if(!clone) {valid=false;reason="clone-allocation";failed_plan=k;break;}
        prepared[k]={slot,original,clone};
        ++built;
        SIZE_T done=0;
        if(!ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<void*>(std::uintptr_t(original)),
                clone,bytes,&done) || done!=bytes) {
            valid=false;reason="unreadable-script";failed_plan=k;break;
        }
        observed_shape=native_video_script_shape_hash(clone,p.count);
        if(!rewrite_pause_video_script(clone,p)) {
            valid=false;reason=observed_shape!=p.shape ?
                "script-shape-mismatch" : "source-node-mismatch";
            failed_plan=k;
            // Diagnostic only: distinguish structural changes from
            // padding differences, without accepting either blindly.
            for(unsigned i=0;i<p.count;++i) {
                if(clone[i].padding)++nonzero_padding;
                clone[i].padding=0;
            }
            zero_padding_shape=native_video_script_shape_hash(clone,p.count);
            break;
        }
    }
    if(!valid) {
        g_preflight_failed=true; // never loop over a rejected script
        // A first scan may complete only after VIDEO has already
        // entered. Allow one recheck on returning to native Options,
        // not while the rejected script could still be executing.
        g_retry_after_video=(model.normal==9 && model.reflex==2);
        for(unsigned i=0;i<built;++i)
            if(prepared[i].clone)
                VirtualFree(prepared[i].clone,0,MEM_RELEASE);
        if(g_log) {
            char msg[430]{};
            const auto p=failed_plan<5u ? kPauseVideoPlans[failed_plan] :
                PauseVideoScriptPlan{};
            std::snprintf(msg,sizeof(msg),
                "RUNTIME-PAUSE-VIDEO-PAGE: status=preflight-refused reason=%s behavior=%u script=%u nodes=%u observed-shape=%08X%08X expected-shape=%08X%08X zero-padding-shape=%08X%08X nonzero-padding=%u normal=%d reflex=%d retry=%s no-patch=1",
                reason,p.behavior,p.script,p.count,
                unsigned(observed_shape>>32),unsigned(observed_shape),
                unsigned(p.shape>>32),unsigned(p.shape),
                unsigned(zero_padding_shape>>32),unsigned(zero_padding_shape),
                nonzero_padding,model.normal,model.reflex,
                g_retry_after_video?"once-on-options-return":"next-scene-only");
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
        "RUNTIME-PAUSE-VIDEO-PAGE: status=installed parent-Options-construction=original normal9-script1=V16-native+SetAGO-navigation-removed scripts2,3,4=inactive normal15-script2=inactive old-video-actions=unreachable original-title-retained=1");
    return true;
}
void restore_pause_video_scripts() noexcept {
    standalone_video_page_set_pause_visible(false);
    g_retry_after_video=false;
    g_preflight_failed=false;
    if(!g_scripts_installed)return;
    for(const auto& entry:g_scripts)
        if(entry.slot && entry.original && entry.clone &&
           writable(entry.slot))
            InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(entry.slot),
                reinterpret_cast<PVOID>(entry.original),entry.clone);
    g_scripts={};
    g_scripts_installed=false;
    g_preflight_failed=false;
    if(g_log)g_log(
        "RUNTIME-PAUSE-VIDEO-PAGE: status=scene-exit original-script-pointers-restored=attempted published-private-copies-retained=1");
}
bool authenticate_pause_site_script(const PauseVideoModel& model,
    const char*& reason,bool& full_shape) noexcept {
    reason="invalid-model";
    full_shape=false;
    if(!model.valid() || !menu_safe_ptr(model.normal_entries+7u*12u))
        return false;
    const auto entry=model.normal_entries+7u*12u;
    std::uint32_t list=0,script=0;
    std::uint8_t count=0;
    reason="script-list";
    if(!read32(entry,list) || !read8(entry+8u,count) ||
       count!=10u || !menu_safe_ptr(list,10u*4u) ||
       !read32(std::uintptr_t(list)+9u*4u,script) ||
       !menu_safe_ptr(script,kPauseSiteOriginalScriptNodes*
                              sizeof(NativeMenuScriptNode)))
        return false;
    std::array<NativeMenuScriptNode,kPauseSiteOriginalScriptNodes> source{};
    SIZE_T done=0;
    reason="script-unreadable";
    if(!ReadProcessMemory(GetCurrentProcess(),
       reinterpret_cast<const void*>(std::uintptr_t(script)),
       source.data(),sizeof(source),&done) || done!=sizeof(source))
        return false;
    // The independent website branch (normal7/script9 nodes1153..1159)
    // is strong enough to approve only the fixed project site action.
    // Other original AI branches can be relocated or altered by the
    // engine; still report the WHOLE shape for troubleshooting.
    full_shape=authenticated_pause_site_script(
        source.data(),source.size());
    reason="site-branch-mismatch";
    if(!authenticated_pause_site_branch(source.data(),source.size()))
        return false;
    constexpr char old_url[]="http://www.rayman2.com/";
    constexpr char new_url[]="https://github.com/legluondunet/Rayman2Fix";
    const auto url=source[kPauseSiteUrlNode].parameter;
    char old_seen[sizeof(old_url)]{};
    reason="native-url-unreadable";
    if(!menu_safe_ptr(url,sizeof(old_seen)) ||
       !ReadProcessMemory(GetCurrentProcess(),
             reinterpret_cast<const void*>(std::uintptr_t(url)),
             old_seen,sizeof(old_seen),&done) ||
       done!=sizeof(old_seen))
        return false;
    if(std::memcmp(old_seen,old_url,sizeof(old_seen))==0) {
        reason="verified-original-url";
        return true;
    }
    // An already redirected in-memory original action remains valid.
    char redirected[sizeof(new_url)]{};
    reason="native-url-mismatch";
    if(menu_safe_ptr(url,sizeof(redirected)) &&
       ReadProcessMemory(GetCurrentProcess(),
             reinterpret_cast<const void*>(std::uintptr_t(url)),
             redirected,sizeof(redirected),&done) &&
       done==sizeof(redirected) &&
       std::memcmp(redirected,new_url,sizeof(redirected))==0) {
        reason="verified-redirected-url";
        return true;
    }
    return false;
}
bool current_pause_site_selected(const PauseVideoModel& model) noexcept {
    std::uintptr_t slot=0;
    std::uint32_t choice=0;
    return locate_verified_pause_site_selection(
        model.mind,&read32,&read8,slot) &&
        read32(slot,choice) &&
        pause_site_action_eligible(model.normal,model.reflex,choice);
}
bool game_is_foreground() noexcept {
    const HWND window=GetForegroundWindow();
    DWORD pid=0;
    return window && GetWindowThreadProcessId(window,&pid) &&
           pid==GetCurrentProcessId();
}
void launch_pause_site() noexcept {
    const HMODULE shell=GetModuleHandleW(L"shell32.dll");
    using ShellExecuteFn=HINSTANCE(WINAPI*)(
        HWND,LPCSTR,LPCSTR,LPCSTR,LPCSTR,INT);
    const auto original=reinterpret_cast<ShellExecuteFn>(
        shell?GetProcAddress(shell,"ShellExecuteA"):nullptr);
    if(!original) {
        if(g_log)g_log(
            "RUNTIME-PAUSE-SITE: browser=unavailable shell32-export=missing");
        return;
    }
    if(g_log)g_log(
        "RUNTIME-PAUSE-SITE: explicit-user-action=Enter source=verified-normal7-reflex2-V16=3 url=github");
    const auto result=original(nullptr,"open",
         "https://github.com/legluondunet/Rayman2Fix",
         nullptr,nullptr,SW_SHOWNORMAL);
    if(g_log)g_log(reinterpret_cast<std::intptr_t>(result)>32 ?
         "RUNTIME-PAUSE-SITE: browser-launch-dispatched=1":
         "RUNTIME-PAUSE-SITE: browser-launch-failed=1");
}
DWORD WINAPI worker(void*) {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!base || !g_log)return 0;
    g_log("RUNTIME-PAUSE-VIDEO-MODEL: observer=started expected-normal=23 expected-reflex=6 original-assets=untouched scoped-script-copies=preflight-required");
    PauseVideoModel state{};
    unsigned tries=0,reports=0;
    DWORD next_scan=0;
    int prior_normal=-100,prior_reflex=-100,prior_pause=-1;
    bool site_script_verified=false;
    unsigned site_auth_tries=0;
    DWORD site_next_auth=0;
    bool previous_enter=false,previous_space=false;
    DWORD last_site_selected_tick=0;
    DWORD last_website_launch=0;
    for(;;) {
        const bool enter=(GetAsyncKeyState(VK_RETURN)&0x8000)!=0;
        const bool space=(GetAsyncKeyState(VK_SPACE)&0x8000)!=0;
        const bool enter_edge=(enter && !previous_enter) ||
                              (space && !previous_space);
        previous_enter=enter;
        previous_space=space;
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
                g_preflight_failed=false;
                state={};tries=0;next_scan=0;
                site_script_verified=false;
                site_auth_tries=0;
                site_next_auth=0;
                last_site_selected_tick=0;
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
                site_script_verified=false;
                site_auth_tries=0;
                site_next_auth=0;
                last_site_selected_tick=0;
            } else {
                const DWORD site_now=GetTickCount();
                if(!site_script_verified && site_auth_tries<4u &&
                   static_cast<LONG>(site_now-site_next_auth)>=0) {
                    ++site_auth_tries;
                    site_next_auth=site_now+300u;
                    const char* reason="unverified";
                    bool full=false;
                    site_script_verified=
                        authenticate_pause_site_script(current,reason,full);
                    if(g_log) {
                        char msg[260]{};
                        std::snprintf(msg,sizeof(msg),
                            "RUNTIME-PAUSE-SITE: original-site-action=%s reason=%s full-script-shape=%u authenticated-branch=normal7/script9/V16-3 retry=%u",
                            site_script_verified?"authenticated":"unverified",
                            reason,unsigned(full),site_auth_tries);
                        g_log(msg);
                    }
                }
                const DWORD selected_now=GetTickCount();
                const bool website_row=
                    current_pause_site_selected(current);
                if(website_row)last_site_selected_tick=selected_now;
                // The original engine may consume Enter and transition
                // away from normal7 in the same 16-ms tick. Only a
                // directly authenticated website row from the preceding
                // 100 ms can survive that exact normal behavior switch.
                const bool just_left_website_row=
                    current.normal!=7 &&
                    last_site_selected_tick!=0u &&
                    DWORD(selected_now-last_site_selected_tick)<=100u;
                if(site_script_verified && enter_edge &&
                   game_is_foreground() &&
                   (website_row || just_left_website_row)) {
                    if(DWORD(selected_now-last_website_launch)>=1500u &&
                       claim_project_site_dispatch()) {
                        last_website_launch=selected_now;
                        launch_pause_site();
                    }
                }
                if(!website_row && !just_left_website_row)
                    last_site_selected_tick=0u;
                // Only retry a failed late preflight after returning
                // to the authenticated parent Options (normal6).
                if(g_retry_after_video && current.normal==6 &&
                   current.reflex==2) {
                    g_retry_after_video=false;
                    g_preflight_failed=false;
                    if(g_log)g_log("RUNTIME-PAUSE-VIDEO-PAGE: retry=options-return original-signatures-required=1");
                }
                if(!g_scripts_installed && !g_preflight_failed)
                    install_pause_video_scripts(current);
                const bool candidate=current.normal==9 && current.reflex==2;
                standalone_video_page_set_pause_visible(
                    candidate && g_scripts_installed);
                if((current.normal!=prior_normal ||
                    current.reflex!=prior_reflex) && reports<64u) {
                char msg[240]{};
                std::snprintf(msg,sizeof(msg),
                    "RUNTIME-PAUSE-VIDEO-MODEL: controller=verified normal=%d reflex=%d candidate-video=%u page=%s",
                    current.normal,current.reflex,unsigned(candidate),
                    g_scripts_installed?"script-verified":"not-installed");
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
        Sleep(state.valid() ? 16u : 250u);
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
