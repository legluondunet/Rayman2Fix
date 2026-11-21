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
#include "rayman2fix/graphics_menu_cursor_actor_anchor.hpp"

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
ScanResult locate_controller() noexcept {
    ScanResult out{};
    MenuAiModel direct{};
    if(check_model(kSceneModelHint,direct)) {
        out.model=direct;
        out.matches=1;
        // Do not assume the scene VA equals the loaded model VA: the hint
        // is used only if it passes the SAME full script validation.
    }
    std::array<BYTE,kChunk+32> buf{};
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto last=std::min<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress),
        0x7FFF0000u);
    std::uintptr_t cursor=0x10000u;
    unsigned processed=0;
    while(cursor<last && processed<kMemoryBudget) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(cursor),
                         &info,sizeof(info)) || !info.RegionSize)break;
        const auto start=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto end=std::min(last,
           start+static_cast<std::uintptr_t>(info.RegionSize));
        if(end<=cursor)break;
        cursor=end;
        if(info.State!=MEM_COMMIT || info.Type!=MEM_PRIVATE ||
           !readable_writable(info.Protect))continue;
        for(auto at=start;at<end && processed<kMemoryBudget;) {
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
                    if(candidate==kSceneModelHint && out.matches==1u)
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
            processed+=static_cast<unsigned>(advance);
        }
        if(out.matches>1u)break;
    }
    out.scanned_mib=(processed+1024u*1024u-1u)/(1024u*1024u);
    if(out.matches==1u) {
        // In the approved SNA, Mind immediately precedes AIModel by 24
        // bytes, but this adjacency is ONLY a candidate: require the
        // normal and reflex intelligence structures to verify both.
        if(out.model.model>=0x10018u)
            check_mind(out.model,out.model.model-24u,out.mind);
    }
    return out;
}
// The owner's Menu.sna inventory identifies a dedicated
// YAM_CurseurMenu_I1 / ARG_CurseurMenu scene actor. Scan for the exact
// four co-located name-table markers after the independently verified
// AI controller is found. The live name table may be relocated: no
// absolute pointer, rendering callback or actor visibility is assumed.
// This is ONE bounded, read-only pass in the research build only.
struct CursorNameScan {
    unsigned matches=0, unreadable=0, scanned_mib=0;
};
CursorNameScan locate_cursor_name_anchor() noexcept {
    CursorNameScan out{};
    std::array<BYTE,kChunk+kMenuCursorActorAnchorWindow> bytes{};
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto last=std::min<std::uintptr_t>(
       reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress),
       0x7FFF0000u);
    std::uintptr_t current=0x10000u;
    unsigned processed=0;
    while(current<last && processed<kMemoryBudget &&
          out.matches<2u) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<const void*>(current),
                         &region,sizeof(region)) ||
           !region.RegionSize)break;
        const auto begin=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const auto end=std::min(last,
            begin+static_cast<std::uintptr_t>(region.RegionSize));
        if(end<=current)break;
        current=end;
        if(region.State!=MEM_COMMIT || region.Type!=MEM_PRIVATE ||
           !readable_writable(region.Protect))continue;
        for(auto at=begin;at<end && processed<kMemoryBudget &&
            out.matches<2u;) {
            const auto length=static_cast<SIZE_T>(
                std::min<std::uintptr_t>(
                    kChunk+kMenuCursorActorAnchorWindow,end-at));
            SIZE_T copied=0;
            const BOOL ok=ReadProcessMemory(GetCurrentProcess(),
                  reinterpret_cast<const void*>(at),
                  bytes.data(),length,&copied);
            if(ok && copied>=kMenuCursorActorAnchorWindow) {
                for(std::size_t j=0;j<kChunk &&
                    j+kMenuCursorActorAnchorWindow<=copied;++j) {
                    if(bytes[j]!='Y')continue;
                    if(!verified_menu_cursor_name_anchor(
                           bytes.data()+j,copied-j))continue;
                    ++out.matches;
                    if(out.matches>=2u)break; // ambiguous: fail closed
                }
            } else ++out.unreadable;
            const auto advance=std::min<std::uintptr_t>(kChunk,end-at);
            at+=advance;
            processed+=static_cast<unsigned>(advance);
        }
    }
    out.scanned_mib=(processed+1024u*1024u-1u)/(1024u*1024u);
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
    int prior_normal=-200,prior_reflex=-200;
    int prior_selection=-200,prior_activate=-200;
    bool announced=false;
    for(unsigned iteration=0;iteration<kSeconds*5u && reports<kMaxReports;
        ++iteration) {
        const DWORD now=GetTickCount();
        if(!model.valid() && tries<kMaxScans &&
           static_cast<LONG>(now-next_scan)>=0) {
            next_scan=now+kScanPeriodMs;
            ++tries;
            const auto scanned=locate_controller();
            if(scanned.matches==1u && scanned.mind.valid()) {
                model=scanned.model;
                mind=scanned.mind;
                g_verified_mind.store(mind.mind,std::memory_order_release);
                g_verified_model.store(model.model,std::memory_order_release);
                char line[245]{};
                std::snprintf(line,sizeof(line),
                   "RUNTIME-MENU-AI: status=controller-found scan=%u models=1 mind=validated normal_behaviors=27 reflex_behaviors=7 level_hint=%s scanned_mib=%u (read-only)",
                   tries,native_level_hint(),scanned.scanned_mib);
                g_logger(line);
                ++reports;
                // This name-table evidence does NOT prove which GLI
                // callback renders the butterfly, so never use it to
                // suppress geometry or bypass native selection.
                const auto cursor_names=locate_cursor_name_anchor();
                char cursor_line[245]{};
                std::snprintf(cursor_line,sizeof(cursor_line),
                    "RUNTIME-MENU-CURSOR-ACTOR: name-table-matches=%u scanned_mib=%u unreadable_chunks=%u verified-actor-renderer=0 mutation=0 (one-shot read-only)",
                    cursor_names.matches,cursor_names.scanned_mib,
                    cursor_names.unreadable);
                g_logger(cursor_line);
            } else if(tries==1u || tries==kMaxScans ||
                      scanned.matches>1u || (tries%4u)==0u) {
                char line[240]{};
                std::snprintf(line,sizeof(line),
                   "RUNTIME-MENU-AI: scan=%u matches=%u mind_valid=%u unreadable_chunks=%u scanned_mib=%u level_hint=%s (read-only; no fixed scene address)",
                   tries,scanned.matches,unsigned(scanned.mind.valid()),
                   scanned.unreadable,scanned.scanned_mib,native_level_hint());
                g_logger(line);
                ++reports;
            }
        }
        if(model.valid() && mind.valid()) {
            MenuAiState current{};
            if(!check_mind(model,mind.mind,current)) {
                g_verified_model.store(0,std::memory_order_release);
                g_logger("RUNTIME-MENU-AI: status=controller-inaccessible; awaiting new level load");
                ++reports;
                model={}; mind={};
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
                g_logger(line);
                ++reports;
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
    logger("RUNTIME-MENU-AI: status=started read-only=1 model=script-fingerprint mind=validated relocation=dynamic max_seconds=180");
    HANDLE thread=CreateThread(nullptr,0,&worker,nullptr,0,nullptr);
    if(thread)CloseHandle(thread);
    else logger("RUNTIME-MENU-AI: status=thread-failed; no sampling");
}
} // namespace r2fix
