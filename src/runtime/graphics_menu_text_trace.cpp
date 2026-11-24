// Experimental x86 read-only-semantics text lookup trace.
// Installs a guarded 7-byte detour but ALWAYS forwards the native result.
// Compiled only in the optional graphics menu research launcher.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "rayman2fix/graphics_menu_target.hpp"
#include "rayman2fix/menu_text_probe_model.hpp"
#include "rayman2fix/graphics_menu_text_trace.hpp"
#if !defined(_M_IX86)
#error Native menu text probe requires 32-bit Windows
#endif
namespace r2fix {
namespace {
constexpr unsigned kTextRva=0x94C70u; // VA 0x494C70
constexpr BYTE kSignature[17]{0x8B,0x44,0x24,0x04,0x83,0xF8,0xFF,
 0x75,0x03,0x33,0xC0,0xC3,0x3D,0x20,0x4E,0x00,0x00};
using Lookup=const char*(__cdecl*)(int);
Lookup g_original=nullptr;
NativeMenuTextLogger g_logger=nullptr;
std::atomic<bool> g_attempted{false};
std::atomic<unsigned> g_hits[11]{},g_ai_hits[11]{};

void jump(BYTE* from,const void* target) noexcept {
    const auto dest=static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(target));
    const auto src=static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(from+5));
    const std::uint32_t rel=dest-src;
    from[0]=0xE9;
    std::memcpy(from+1,&rel,sizeof(rel));
}
bool accepted(const BYTE* base) noexcept {
    if(!base)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 ||
       dos->e_lfanew>0x1000)return false;
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        base+dos->e_lfanew);
    return pe->Signature==IMAGE_NT_SIGNATURE &&
      graphics_menu_accepts_deprotected_image({
        pe->FileHeader.Machine,pe->FileHeader.NumberOfSections,
        pe->FileHeader.TimeDateStamp,pe->OptionalHeader.Magic,
        pe->OptionalHeader.AddressOfEntryPoint,
        pe->OptionalHeader.SizeOfImage});
}
const char* __cdecl intercepted_lookup(int handle) {
    const int slot=menu_text_handle_slot(handle);
    if(slot>=0) {
        g_hits[slot].fetch_add(1,std::memory_order_relaxed);
        const auto base=reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(base && caller>=base &&
           is_ai_text_return_rva(static_cast<unsigned>(caller-base)))
            g_ai_hits[slot].fetch_add(1,std::memory_order_relaxed);
    }
    return g_original(handle); // no text substitution, native ABI preserved
}
DWORD WINAPI observer(void*) {
    const DWORD began=GetTickCount();
    unsigned last[11]{};
    unsigned emitted=0;
    for(unsigned sample=0;sample<600 && emitted<120;++sample) {
        Sleep(500);
        unsigned current[11]{};
        bool changed=false;
        for(unsigned i=0;i<11;++i) {
            current[i]=g_hits[i].load(std::memory_order_relaxed);
            changed=changed || current[i]!=last[i];
        }
        if(!changed)continue;
        std::memcpy(last,current,sizeof(last));
        DWORD pid=0;
        const HWND foreground=GetForegroundWindow();
        if(foreground)GetWindowThreadProcessId(foreground,&pid);
        char msg[375]{};
        std::snprintf(msg,sizeof(msg),
           "RUNTIME-MENU-TEXT: t_ms=%lu fg=%u language370=%u video371=%u sound372=%u other373=%u resolution379=%u save380=%u low381=%u medium382=%u high383=%u other384=%u brightness385=%u ai379=%u ai380=%u (text unchanged; submenu unverified)",
           static_cast<unsigned long>(GetTickCount()-began),
           unsigned(pid==GetCurrentProcessId()),current[0],current[1],
           current[2],current[3],current[4],current[5],current[6],
           current[7],current[8],current[9],current[10],
           g_ai_hits[4].load(std::memory_order_relaxed),
           g_ai_hits[5].load(std::memory_order_relaxed));
        g_logger(msg);
        ++emitted;
    }
    g_logger("RUNTIME-MENU-TEXT: status=finished passthrough=1");
    return 0;
}
} // namespace

void start_native_menu_text_trace(NativeMenuTextLogger logger) noexcept {
    if(!logger || g_attempted.exchange(true,std::memory_order_acq_rel))return;
    BYTE* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if(!accepted(base)) {
        logger("RUNTIME-MENU-TEXT: status=unsupported-deprotected-image; no hook");return;
    }
    BYTE* entry=base+kTextRva;
    if(std::memcmp(entry,kSignature,sizeof(kSignature))!=0) {
        logger("RUNTIME-MENU-TEXT: status=unapproved-opcodes; no hook");return;
    }
    BYTE* gateway=static_cast<BYTE*>(VirtualAlloc(nullptr,64,
       MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!gateway)return;
    std::memcpy(gateway,entry,7); // two complete, position-independent instructions
    jump(gateway+7,entry+7);
    DWORD old=0;
    if(!VirtualProtect(gateway,64,PAGE_EXECUTE_READ,&old)) {
        VirtualFree(gateway,0,MEM_RELEASE);return;
    }
    DWORD prior=0;
    if(!VirtualProtect(entry,7,PAGE_EXECUTE_READWRITE,&prior)) {
        VirtualFree(gateway,0,MEM_RELEASE);return;
    }
    g_original=reinterpret_cast<Lookup>(gateway);
    g_logger=logger;
    jump(entry,reinterpret_cast<void*>(&intercepted_lookup));
    entry[5]=entry[6]=0x90;
    FlushInstructionCache(GetCurrentProcess(),entry,7);
    DWORD ignored=0;
    VirtualProtect(entry,7,prior,&ignored);
    logger("RUNTIME-MENU-TEXT: status=installed va=0x494C70 verified_original_bytes=17 forwarding=1 max_seconds=300 max_reports=120");
    HANDLE worker=CreateThread(nullptr,0,&observer,nullptr,0,nullptr);
    if(worker)CloseHandle(worker);
    else logger("RUNTIME-MENU-TEXT: status=worker-failed; native results still forwarded");
}
} // namespace r2fix
