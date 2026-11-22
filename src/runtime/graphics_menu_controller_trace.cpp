// Research-only, passive Rayman 2 Options menu AI controller observer.
// The native game owns every byte, script and mode; this code performs
// bounded cross-checked ReadProcessMemory reads and never patches anything.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "rayman2fix/graphics_menu_controller_model.hpp"
#include "rayman2fix/graphics_menu_controller_vars.hpp"
#include "rayman2fix/graphics_menu_screen_candidate.hpp"
#include "rayman2fix/graphics_menu_controller_trace.hpp"
#include "rayman2fix/graphics_menu_target.hpp"
#include "rayman2fix/graphics_menu_start_policy.hpp"

#if !defined(_M_IX86)
#error Menu controller trace requires Windows x86
#endif
namespace r2fix {
namespace {
constexpr std::uintptr_t kSceneModelHint=0x079209A8u; // hint ONLY, never trusted
constexpr unsigned kChunk=64u*1024u;
constexpr unsigned kMemoryBudget=160u*1024u*1024u;
constexpr unsigned kMaxScans=18u;
constexpr unsigned kSeconds=180u;
constexpr unsigned kMaxReports=160u;
constexpr unsigned kScanPeriodMs=5000u;
#if defined(RAYMAN2FIX_GRAPHICS_INFO_PROTO) && RAYMAN2FIX_GRAPHICS_INFO_PROTO
constexpr bool kPersistentMenuController=true;
#else
constexpr bool kPersistentMenuController=false;
#endif
std::atomic<bool> g_started{false};
MenuAiLogger g_logger=nullptr;
std::atomic<std::uintptr_t> g_verified_mind{0};
std::atomic<std::uintptr_t> g_verified_model{0};

bool read32(std::uintptr_t address,std::uint32_t& out) noexcept {
    if(!menu_safe_ptr(address))return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),
       reinterpret_cast<const void*>(address),&out,sizeof(out),&read)!=0 &&
       read==sizeof(out);
}
bool read8(std::uintptr_t address,std::uint8_t& out) noexcept {
    if(!menu_safe_ptr(address,1u))return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),
       reinterpret_cast<const void*>(address),&out,sizeof(out),&read)!=0 &&
       read==sizeof(out);
}
bool check_model(std::uintptr_t address,MenuAiModel& model) noexcept {
    return inspect_menu_model(address,&read32,&read8,model);
}
bool check_mind(const MenuAiModel& model,std::uintptr_t addr,
                MenuAiState& mind) noexcept {
    return inspect_menu_mind(model,addr,&read32,mind);
}
bool approved_exe() noexcept {
    const auto* base=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!base)return false;
    IMAGE_DOS_HEADER dos{};
    SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),base,&dos,sizeof(dos),&read) ||
       read!=sizeof(dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
       dos.e_lfanew<=0 || dos.e_lfanew>=0x1000)return false;
    IMAGE_NT_HEADERS32 pe{};
    read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),base+dos.e_lfanew,
            &pe,sizeof(pe),&read) || read!=sizeof(pe) ||
       pe.Signature!=IMAGE_NT_SIGNATURE)return false;
    return graphics_menu_accepts_deprotected_image({
        pe.FileHeader.Machine,pe.FileHeader.NumberOfSections,
        pe.FileHeader.TimeDateStamp,pe.OptionalHeader.Magic,
        pe.OptionalHeader.AddressOfEntryPoint,
        pe.OptionalHeader.SizeOfImage});
}
bool readable_writable(DWORD protect) noexcept {
    if(protect & (PAGE_GUARD|PAGE_NOACCESS))return false;
    switch(protect&0xffu) {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY: return true;
        default:return false;
    }
}
struct ScanResult {
    MenuAiModel model{};
    MenuAiState mind{};
    unsigned matches=0,unreadable=0,scanned_mib=0;
};
// Scan owned, committed, writable/private game memory only. A cheap flag
// filter avoids calling ReadProcessMemory for arbitrary 4-byte words.
// The complete script/brightness fingerprint must match before reporting.
ScanResult locate_controller(std::uintptr_t& resume_cursor,
                             std::uintptr_t& candidate_hint) noexcept {
    ScanResult out{};
    // A previously detected script-valid model may have been observed
    // before its Mind was fully initialized. Revalidate BOTH structures
    // on every retry rather than losing that candidate when the bounded
    // scan advances into a later memory region.
    for(const std::uintptr_t hint:{kSceneModelHint,candidate_hint}) {
        if(!hint)continue;
        MenuAiModel direct{};
        if(!check_model(hint,direct))continue;
        out.model=direct;
        out.matches=1;
        if(direct.model>=0x10018u &&
           check_mind(direct,direct.model-24u,out.mind)) {
            candidate_hint=0;
            return out;
        }
        break;
    }
    std::array<BYTE,kChunk+32> buf{};
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto last=std::min<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress),
        0x7FFF0000u);
    // Continue at the first unexamined region on subsequent attempts.
    // Restart from the low address only after reaching the end of the
    // 32-bit address space or when the menu level is reloaded.
    std::uintptr_t cursor=menu_scan_resume_cursor(resume_cursor,last);
    unsigned processed=0;
    while(cursor<last && processed<kMemoryBudget) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(cursor),
                         &info,sizeof(info)) || !info.RegionSize)break;
        const auto start=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto end=std::min(last,
           start+static_cast<std::uintptr_t>(info.RegionSize));
        if(end<=cursor)break;
        if(info.State!=MEM_COMMIT || info.Type!=MEM_PRIVATE ||
           !readable_writable(info.Protect)) {
            cursor=end;
            continue;
        }
        for(auto at=std::max(start,cursor);
            at<end && processed<kMemoryBudget;) {
            const SIZE_T length=static_cast<SIZE_T>(
                std::min<std::uintptr_t>(kChunk+20u,end-at));
            SIZE_T copied=0;
            const BOOL ok=ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<const void*>(at),buf.data(),length,&copied);
            if(ok && copied>=20u) {
                // Original in-memory structure is 4-byte aligned.
                unsigned first=unsigned((4u-(at&3u))&3u);
                for(unsigned j=first;j+20u<=copied;j+=4u) {
                    std::uint32_t flags=0,np=0,rp=0;
                    std::memcpy(&flags,buf.data()+j+16u,4u);
                    if(flags!=1u)continue;
                    std::memcpy(&np,buf.data()+j,4u);
                    std::memcpy(&rp,buf.data()+j+4u,4u);
                    if(!menu_safe_ptr(np,8u) ||
                       !menu_safe_ptr(rp,8u) ||
                       (np&3u)!=0u || (rp&3u)!=0u)continue;
                    const std::uintptr_t candidate=at+j;
                    if(out.matches==1u && candidate==out.model.model)
                        continue; // same model already checked by fast path
                    MenuAiModel current{};
                    if(!check_model(candidate,current))continue;
                    ++out.matches;
                    if(out.matches==1u)out.model=current;
                    if(out.matches>1u)break; // do not guess between models
                }
            } else ++out.unreadable;
            if(out.matches>1u)break;
            const auto advance=std::min<std::uintptr_t>(kChunk,end-at);
            at+=advance;
            cursor=at; // preserve exact progress within a large allocation
            processed+=static_cast<unsigned>(advance);
        }
        if(out.matches>1u)break;
    }
    resume_cursor=menu_scan_resume_cursor(cursor,last);
    out.scanned_mib=(processed+1024u*1024u-1u)/(1024u*1024u);
    if(out.matches==1u) {
        // In the approved SNA, Mind immediately precedes AIModel by 24
        // bytes, but this adjacency is ONLY a candidate: require the
        // normal and reflex intelligence structures to verify both.
        if(out.model.model>=0x10018u)
            check_mind(out.model,out.model.model-24u,out.mind);
        candidate_hint=out.mind.valid()?0:out.model.model;
    } else candidate_hint=0;
    return out;
}
const char* native_level_hint() noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    char level[30]{};
    SIZE_T copied=0;
    if(!base || !ReadProcessMemory(GetCurrentProcess(),
          reinterpret_cast<const void*>(base+0x100381u),
          level,sizeof(level),&copied) || copied!=sizeof(level))
        return "unavailable";
    // Only classify a known native level token. Do not log an arbitrary
    // loaded string or use it as proof of the actual submenu.
    const char m0=level[0],m1=level[1],m2=level[2],m3=level[3];
    return (m0=='m'||m0=='M') && (m1=='e'||m1=='E') &&
           (m2=='n'||m2=='N') && (m3=='u'||m3=='U') ?
           "menu-level" : "other-level";
}
unsigned game_foreground() noexcept {
    DWORD pid=0;
    HWND win=GetForegroundWindow();
    if(win)GetWindowThreadProcessId(win,&pid);
    return pid==GetCurrentProcessId();
}
DWORD WINAPI worker(void*) {
    const DWORD begin=GetTickCount();
    DWORD next_scan=begin;
    MenuAiModel model{};
    MenuAiState mind{};
    unsigned tries=0,reports=0;
    std::uintptr_t scan_cursor=0x10000u;
    std::uintptr_t candidate_hint=0;
    int prior_normal=-200,prior_reflex=-200;
    int prior_selection=-200,prior_activate=-200;
    bool announced=false;
    const auto report=[&](const char* line) noexcept {
        if(reports<kMaxReports) {
            g_logger(line);
            ++reports;
        }
    };
    for(unsigned iteration=0;
        menu_controller_worker_continue(kPersistentMenuController,
            iteration,reports,kSeconds*5u,kMaxReports);
        ++iteration) {
        const DWORD now=GetTickCount();
        const bool in_menu=std::strcmp(
            native_level_hint(),"menu-level")==0;
        if(kPersistentMenuController && !in_menu) {
            // A returned-to-menu game can reload ALL scene pointers.
            // Clear the stale cache and restart verification on entry.
            g_verified_model.store(0,std::memory_order_release);
            g_verified_mind.store(0,std::memory_order_release);
            model={}; mind={}; tries=0; announced=false;
            scan_cursor=0x10000u;
            candidate_hint=0;
            next_scan=now+500u;
            Sleep(200);
            continue;
        }
        if(kPersistentMenuController && in_menu && !model.valid() &&
           tries>=kMaxScans &&
           static_cast<LONG>(now-next_scan)>=0) {
            tries=0; next_scan=now+10000u;
        }
        if(!model.valid() &&
           menu_controller_scan_allowed(kPersistentMenuController,
                in_menu,tries,kMaxScans) &&
           static_cast<LONG>(now-next_scan)>=0) {
            next_scan=now+kScanPeriodMs;
            ++tries;
            const auto scanned=locate_controller(scan_cursor,candidate_hint);
            if(scanned.matches==1u && scanned.mind.valid()) {
                model=scanned.model;
                mind=scanned.mind;
                g_verified_mind.store(mind.mind,std::memory_order_release);
                g_verified_model.store(model.model,std::memory_order_release);
                char line[245]{};
                std::snprintf(line,sizeof(line),
                   "RUNTIME-MENU-AI: status=controller-found scan=%u models=1 mind=validated normal_behaviors=27 reflex_behaviors=7 level_hint=%s scanned_mib=%u (read-only)",
                   tries,native_level_hint(),scanned.scanned_mib);
                report(line);
            } else if(tries<=4u || tries==kMaxScans ||
                      scanned.matches>1u || (tries%4u)==0u) {
                // Report every attempt during the first 15-20 seconds
                // of a MENU scene. Previously scans #2 and #3 were
                // silent, making short logs look like retry never ran.
                // All reports remain behind Log=1 and globally capped.
                char line[240]{};
                std::snprintf(line,sizeof(line),
                   "RUNTIME-MENU-AI: scan=%u matches=%u mind_valid=%u unreadable_chunks=%u scanned_mib=%u level_hint=%s (read-only; no fixed scene address)",
                   tries,scanned.matches,unsigned(scanned.mind.valid()),
                   scanned.unreadable,scanned.scanned_mib,native_level_hint());
                report(line);
            }
        }
        if(model.valid() && mind.valid()) {
            MenuAiState current{};
            if(!check_mind(model,mind.mind,current)) {
                g_verified_model.store(0,std::memory_order_release);
                g_verified_mind.store(0,std::memory_order_release);
                report("RUNTIME-MENU-AI: status=controller-inaccessible; awaiting new level load");
                model={}; mind={};
                if(kPersistentMenuController)tries=0;
                next_scan=now+1500u;
                continue;
            }
            const auto vars=observe_menu_selection(current.mind,&read32,&read8);
            const int selection=vars.selection_valid ?
                ((vars.selection>=-1 && vars.selection<=31) ?
                   int(vars.selection) : -2) : -1;
            const int activate=vars.activate_valid ? int(vars.activate) : -1;
            // Conservative classification only: not proof of active page.
            const auto level=native_level_hint();
            const auto scope=classify_menu_scope(
                model.valid(),current.valid(),
                std::strcmp(level,"menu-level")==0,
                current.normal,current.reflex,
                vars.selection_valid,selection,
                vars.activate_valid,activate);
            const bool changed=current.normal!=prior_normal ||
                               current.reflex!=prior_reflex ||
                               selection!=prior_selection ||
                               activate!=prior_activate;
            if(changed || !announced || iteration%25u==0u) {
                // Preserve independent normal and reflex state; V17 is a
                // verified native scalar, but NOT yet a proven UI selection.
                char line[320]{};
                std::snprintf(line,sizeof(line),
                   "RUNTIME-MENU-AI: t_ms=%lu fg=%u normal=%d reflex=%d normal5=%u normal19=%u reflex5_brightness=%u v17_valid=%u v17=%d v51_valid=%u v51=%d scope=%s level_hint=%s (read-only; submenu=unverified)",
                   static_cast<unsigned long>(now-begin),
                   game_foreground(),current.normal,current.reflex,
                   unsigned(current.normal==5),unsigned(current.normal==19),
                   unsigned(current.reflex==5),
                   unsigned(vars.selection_valid),selection,
                   unsigned(vars.activate_valid),activate,
                   menu_scope_label(scope),level);
                report(line);
            }
            prior_normal=current.normal;
            prior_reflex=current.reflex;
            prior_selection=selection;
            prior_activate=activate;
            announced=true;
        }
        Sleep(200);
    }
    if(!model.valid())
        g_logger("RUNTIME-MENU-AI: status=no-verified-controller (no changes; no submenu inference)");
    g_logger("RUNTIME-MENU-AI: status=finished read-only=1");
    return 0;
}
} // namespace
bool get_verified_menu_controller(MenuAiModel& model,
                                  MenuAiState& state) noexcept {
    model={}; state={};
    const auto address=g_verified_model.load(std::memory_order_acquire);
    const auto mind=g_verified_mind.load(std::memory_order_acquire);
    if(!address || !mind || !check_model(address,model) ||
       !check_mind(model,mind,state)) {
        model={}; state={};
        return false;
    }
    return true;
}
void start_menu_ai_controller_trace(MenuAiLogger logger) noexcept {
    if(!logger || g_started.exchange(true,std::memory_order_acq_rel))return;
    if(!approved_exe()) {
        logger("RUNTIME-MENU-AI: status=unsupported-image; no sampling");
        return;
    }
    g_logger=logger;
    logger(kPersistentMenuController?
        "RUNTIME-MENU-AI: status=started read-only=1 model=script-fingerprint mind=validated persistent=1 scan-only-in-menu=1 reports-capped=1":
        "RUNTIME-MENU-AI: status=started read-only=1 model=script-fingerprint mind=validated relocation=dynamic max_seconds=180");
    HANDLE thread=CreateThread(nullptr,0,&worker,nullptr,0,nullptr);
    if(thread)CloseHandle(thread);
    else logger("RUNTIME-MENU-AI: status=thread-failed; no sampling");
}
} // namespace r2fix
