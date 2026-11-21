// Experimental NATIVE menu information replacement, opt-in CI artifact only.
// The original Fix localization pointer table is changed in memory (no
// game file modifications, no code-section detours). Two original x86
// dispatcher-table slots are hooked to suppress ONLY four validated
// video-selection script calls and one native save function call.
// All unsupported states leave the original game unchanged.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <cwchar>
#include "rayman2fix/graphics_menu_target.hpp"
#include "rayman2fix/graphics_menu_controller_trace.hpp"
#include "rayman2fix/graphics_menu_controller_model.hpp"
#include "rayman2fix/native_graphics_info_policy.hpp"
#include "rayman2fix/native_graphics_info_proto.hpp"

#if !defined(_M_IX86)
#error Experimental native graphics information requires Windows x86
#endif
namespace r2fix {
namespace {
constexpr unsigned kProcSelectSlot=0xA4F00u;
constexpr unsigned kFuncSaveSlot=0xA5C88u;
constexpr unsigned kProcSelectRva=0x6FE00u;
constexpr unsigned kFuncSaveRva=0x7AC70u;
constexpr unsigned kScriptEvalRva=0x80B90u;
constexpr unsigned kTextTableDirRva=0x10026Cu;
constexpr unsigned kLanguageIndexRva=0x100FA8u;
constexpr unsigned kLevelNameRva=0x100381u;
constexpr BYTE kProcSignature[]{0x8B,0x4C,0x24,0x08,0x8B,0x54,0x24,0x04,
    0x83,0xEC,0x10,0x8D,0x44,0x24,0x00};
constexpr BYTE kSaveSignature[]{0xE8,0xEB,0x76,0xFA,0xFF,0x8B,0x4C,0x24,
    0x0C,0x89,0x01,0x8B,0x44,0x24,0x08};
constexpr BYTE kEvalSignature[]{0x8B,0x44,0x24,0x08,0x8B,0x54,0x24,
    0x0C,0x33,0xC9,0x52};
using SelectProc=void* (__cdecl*)(void*,void*);
using SaveFunc=void* (__cdecl*)(void*,void*,void*);
using Evaluate=void* (__cdecl*)(void*,void*,void*);
SelectProc g_original_select=nullptr;
SaveFunc g_original_save=nullptr;
Evaluate g_eval=nullptr;
std::atomic<bool> g_started{false};
std::atomic<bool> g_protected{false};
std::array<std::atomic<std::uintptr_t>,4> g_select_children{};
std::atomic<std::uintptr_t> g_save_child{0};
std::array<std::array<char,56>,6> g_rows{};
std::array<std::uintptr_t,6> g_old_text{};
std::uintptr_t g_language_table=0;
GraphicsInfoLogger g_logger=nullptr;
std::atomic<unsigned> g_select_suppressed{0};
std::atomic<unsigned> g_save_suppressed{0};
std::atomic<unsigned> g_other_forwarded{0};

std::uintptr_t exe_base() noexcept {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}
bool read_bytes(std::uintptr_t ptr,void* dest,SIZE_T bytes) noexcept {
    SIZE_T done=0;
    return ptr>=0x10000u && ReadProcessMemory(GetCurrentProcess(),
       reinterpret_cast<const void*>(ptr),dest,bytes,&done)!=0 &&
       done==bytes;
}
bool read32(std::uintptr_t p,std::uint32_t& v) noexcept {
    return read_bytes(p,&v,4);
}
bool read8(std::uintptr_t p,std::uint8_t& v) noexcept {
    return read_bytes(p,&v,1);
}
bool is_native_menu() noexcept {
    const auto base=exe_base();
    char name[4]{};
    return base && read_bytes(base+kLevelNameRva,name,4) &&
       (name[0]=='M'||name[0]=='m') &&
       (name[1]=='E'||name[1]=='e') &&
       (name[2]=='N'||name[2]=='n') &&
       (name[3]=='U'||name[3]=='u');
}
bool writable_slot(std::uintptr_t p) noexcept {
    MEMORY_BASIC_INFORMATION m{};
    if(!p || !VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m)) ||
       m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))
        return false;
    const DWORD flags=m.Protect&0xffu;
    return flags==PAGE_READWRITE || flags==PAGE_EXECUTE_READWRITE;
}
bool approved() noexcept {
    const auto base=exe_base();
    IMAGE_DOS_HEADER dos{};
    if(!read_bytes(base,&dos,sizeof(dos)) ||
       dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 ||
       dos.e_lfanew>=0x1000)return false;
    IMAGE_NT_HEADERS32 pe{};
    if(!read_bytes(base+dos.e_lfanew,&pe,sizeof(pe)) ||
       pe.Signature!=IMAGE_NT_SIGNATURE)return false;
    return graphics_menu_accepts_deprotected_image({
       pe.FileHeader.Machine,pe.FileHeader.NumberOfSections,
       pe.FileHeader.TimeDateStamp,pe.OptionalHeader.Magic,
       pe.OptionalHeader.AddressOfEntryPoint,
       pe.OptionalHeader.SizeOfImage});
}
bool original_script_children(const MenuAiModel& model,
                             std::array<std::uintptr_t,4>& select,
                             std::uintptr_t& save) noexcept {
    const auto script=[&](std::uintptr_t entries,unsigned behavior,
                          unsigned script_no,std::uintptr_t& output)->bool {
        std::uint32_t list=0,ptr=0;
        if(!read32(entries+behavior*12u,list) ||
           !read32(std::uintptr_t(list)+script_no*4u,ptr) ||
           !menu_safe_ptr(ptr))return false;
        output=ptr;
        return true;
    };
    std::uintptr_t normal5=0,normal19=0;
    if(!model.valid() ||
       !script(model.normal_entries,5,3,normal5) ||
       !script(model.normal_entries,19,2,normal19))
        return false;
    // Compare both type byte and native command identifier per SNA node.
    const auto node=[&](std::uintptr_t where,std::uint32_t command,
                        std::uint8_t type)->bool {
        std::uint32_t id=0;std::uint8_t kind=0;
        return read32(where,id) && read8(where+7u,kind) &&
               id==command && kind==type;
    };
    const unsigned offsets[]{0x140u,0x190u,0x1E0u};
    for(unsigned i=0;i<3;++i) {
        if(!node(normal5+offsets[i],356u,4u))return false;
        select[i]=normal5+offsets[i]+8u;
    }
    if(!node(normal19+0x48u,356u,4u) ||
       !node(normal5+0xD60u,261u,3u))return false;
    select[3]=normal19+0x48u+8u;
    save=normal5+0xD60u+8u;
    return true;
}
// Original procedure 356 evaluates its one argument using the game's
// script dispatcher and *then* calls the video-mode mutation routine.
// For an exactly verified menu child, we evaluate and advance normally
// without executing the mode mutation.
void* __cdecl protected_select(void* actor,void* child) {
    if(g_protected.load(std::memory_order_acquire) && is_native_menu()) {
        const auto ptr=reinterpret_cast<std::uintptr_t>(child);
        for(const auto& expected:g_select_children) {
            if(expected.load(std::memory_order_relaxed)==ptr && ptr) {
                std::uint8_t temporary[16]{};
                g_select_suppressed.fetch_add(1,std::memory_order_relaxed);
                return g_eval(actor,child,temporary);
            }
        }
    }
    g_other_forwarded.fetch_add(1,std::memory_order_relaxed);
    return g_original_select(actor,child);
}
// The native F261 has no child arguments: it calls the disk-saving
// routine, writes boolean=1/type=6 to the temporary result, then returns
// its unchanged child pointer. Emulate only this verified result.
void* __cdecl protected_save(void* actor,void* child,void* result) {
    const auto ptr=reinterpret_cast<std::uintptr_t>(child);
    const auto out=reinterpret_cast<std::uintptr_t>(result);
    if(g_protected.load(std::memory_order_acquire) && is_native_menu() &&
       ptr && ptr==g_save_child.load(std::memory_order_relaxed) &&
       writable_slot(out) && writable_slot(out+12u)) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(out),1);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(out+12u),6);
        g_save_suppressed.fetch_add(1,std::memory_order_relaxed);
        return child;
    }
    return g_original_save(actor,child,result);
}
bool swap_slot(std::uintptr_t slot,std::uintptr_t original,
               void* replacement) noexcept {
    if(!writable_slot(slot))return false;
    return InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(slot),replacement,
        reinterpret_cast<PVOID>(original))==reinterpret_cast<PVOID>(original);
}
void restore_callbacks() noexcept {
    const auto base=exe_base();
    g_protected.store(false,std::memory_order_release);
    if(!base)return;
    if(writable_slot(base+kProcSelectSlot))
        InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(base+kProcSelectSlot),
            reinterpret_cast<PVOID>(base+kProcSelectRva),
            reinterpret_cast<PVOID>(&protected_select));
    if(writable_slot(base+kFuncSaveSlot))
        InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(base+kFuncSaveSlot),
            reinterpret_cast<PVOID>(base+kFuncSaveRva),
            reinterpret_cast<PVOID>(&protected_save));
}
bool install_callbacks(const MenuAiModel& model) noexcept {
    const auto base=exe_base();
    if(!approved() || !base)return false;
    BYTE proc[sizeof(kProcSignature)]{},fn[sizeof(kSaveSignature)]{},
         evaluator[sizeof(kEvalSignature)]{};
    if(!read_bytes(base+kProcSelectRva,proc,sizeof(proc)) ||
       !read_bytes(base+kFuncSaveRva,fn,sizeof(fn)) ||
       !read_bytes(base+kScriptEvalRva,evaluator,sizeof(evaluator)) ||
       std::memcmp(proc,kProcSignature,sizeof(proc)) ||
       std::memcmp(fn,kSaveSignature,sizeof(fn)) ||
       std::memcmp(evaluator,kEvalSignature,sizeof(evaluator)))
        return false;
    std::uint32_t native_proc=0,native_fn=0;
    if(!read32(base+kProcSelectSlot,native_proc) ||
       !read32(base+kFuncSaveSlot,native_fn) ||
       native_proc!=base+kProcSelectRva ||
       native_fn!=base+kFuncSaveRva)
        return false;
    std::array<std::uintptr_t,4> children{};
    std::uintptr_t save=0;
    if(!original_script_children(model,children,save))return false;
    for(unsigned i=0;i<4;++i)
        g_select_children[i].store(children[i],std::memory_order_relaxed);
    g_save_child.store(save,std::memory_order_release);
    g_original_select=reinterpret_cast<SelectProc>(base+kProcSelectRva);
    g_original_save=reinterpret_cast<SaveFunc>(base+kFuncSaveRva);
    g_eval=reinterpret_cast<Evaluate>(base+kScriptEvalRva);
    if(!swap_slot(base+kProcSelectSlot,base+kProcSelectRva,
                  reinterpret_cast<void*>(&protected_select)))
        return false;
    if(!swap_slot(base+kFuncSaveSlot,base+kFuncSaveRva,
                  reinterpret_cast<void*>(&protected_save))) {
        restore_callbacks();
        return false;
    }
    return true;
}
} // namespace
} // namespace r2fix

namespace r2fix {
namespace {
std::uintptr_t text_slot_table() noexcept {
    const auto base=exe_base();
    std::uint32_t directory=0,language=0,table=0;
    if(!base || !read32(base+kTextTableDirRva,directory) ||
       !read32(base+kLanguageIndexRva,language) ||
       !menu_safe_ptr(directory,8u) || (language&255u)>=8u ||
       !read32(std::uintptr_t(directory)+(language&255u)*8u,table) ||
       !menu_safe_ptr(table,385u*4u+4u))
        return 0;
    return table;
}
void text_row(unsigned n,const char* prefix,const char* value) noexcept {
    std::snprintf(g_rows[n].data(),g_rows[n].size(),
                  "%s %.34s",prefix,value?value:"?");
}
void build_texts() noexcept {
    wchar_t wide_name[128]{};
    std::array<char,96> adapter{};
    std::array<char,96> driver{};
    unsigned screen_w=unsigned(GetSystemMetrics(SM_CXSCREEN));
    unsigned screen_h=unsigned(GetSystemMetrics(SM_CYSCREEN));
    unsigned depth=0;
    DISPLAY_DEVICEW dev{};dev.cb=sizeof(dev);
    for(DWORD i=0;i<8;++i) {
        dev={};dev.cb=sizeof(dev);
        if(!EnumDisplayDevicesW(nullptr,i,&dev,0))break;
        if(!(dev.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ||
           !(dev.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE))continue;
        std::wcsncpy(wide_name,dev.DeviceString,127);
        DEVMODEW mode{};mode.dmSize=sizeof(mode);
        if(EnumDisplaySettingsW(dev.DeviceName,ENUM_CURRENT_SETTINGS,&mode)) {
            if(mode.dmPelsWidth && mode.dmPelsHeight) {
                screen_w=mode.dmPelsWidth;screen_h=mode.dmPelsHeight;
            }
            depth=mode.dmBitsPerPel;
        }
        break;
    }
    // Fallback: D3D9 can report a compatibility-layer adapter/driver,
    // not necessarily the underlying Linux RADV/Mesa physical driver.
    const auto module=LoadLibraryW(L"d3d9.dll");
    if(module) {
        using Factory=IDirect3D9* (WINAPI*)(UINT);
        auto api_fun=reinterpret_cast<Factory>(GetProcAddress(module,"Direct3DCreate9"));
        IDirect3D9* api=api_fun?api_fun(D3D_SDK_VERSION):nullptr;
        if(api) {
            D3DADAPTER_IDENTIFIER9 id{};
            if(SUCCEEDED(api->GetAdapterIdentifier(0,0,&id))) {
                std::snprintf(driver.data(),driver.size(),"%s",id.Driver);
                if(!wide_name[0])
                    MultiByteToWideChar(CP_ACP,0,id.Description,-1,wide_name,128);
            }
            api->Release();
        }
        FreeLibrary(module);
    }
    if(!driver[0])std::snprintf(driver.data(),driver.size(),"Win32/Glide");
    const int n=WideCharToMultiByte(CP_ACP,0,wide_name,-1,
                adapter.data(),static_cast<int>(adapter.size()),nullptr,nullptr);
    if(n<=0 || !adapter[0])
        std::snprintf(adapter.data(),adapter.size(),"Display adapter");
    if(depth==0) {
        HDC dc=GetDC(nullptr);
        if(dc) {
            depth=unsigned(GetDeviceCaps(dc,BITSPIXEL)*GetDeviceCaps(dc,PLANES));
            ReleaseDC(nullptr,dc);
        }
    }
    const unsigned gcd=std::gcd(screen_w,screen_h);
    const unsigned x=gcd?screen_w/gcd:0u,y=gcd?screen_h/gcd:0u;
    std::snprintf(g_rows[0].data(),g_rows[0].size(),"VIDEO INFORMATION");
    char line[100]{};
    std::snprintf(line,sizeof(line),"%ux%u %u:%u",screen_w,screen_h,x,y);
    text_row(1,"DISPLAY:",line);
    text_row(2,"GPU:",adapter.data());
    text_row(3,"D3D9:",driver.data());
    std::snprintf(line,sizeof(line),"%u bits (Glide 16)",depth);
    text_row(4,"DEPTH:",line);
    std::snprintf(g_rows[5].data(),g_rows[5].size(),
                  "GLIDE: 1024x768 / 4:3");
}
bool original_text_valid(std::uintptr_t p) noexcept {
    char s[72]{};
    if(!read_bytes(p,s,sizeof(s)))return false;
    bool terminated=false;
    for(unsigned i=0;i<sizeof(s);++i) {
        if(!s[i]){terminated=i>=2;break;}
    }
    return terminated;
}
bool patch_text_table() noexcept {
    if(g_language_table)return true;
    const auto table=text_slot_table();
    if(!table)return false;
    std::array<std::uintptr_t,6> originals{};
    for(unsigned i=0;i<6;++i) {
        const auto slot=table+(379u+i)*4u;
        std::uint32_t p=0;
        if(!writable_slot(slot) || !read32(slot,p) ||
           !menu_safe_ptr(p) || !original_text_valid(p))return false;
        originals[i]=p;
    }
    unsigned patched=0;
    for(unsigned i=0;i<6;++i) {
        const auto slot=table+(379u+i)*4u;
        if(!swap_slot(slot,originals[i],
              static_cast<void*>(g_rows[i].data())))break;
        ++patched;
    }
    if(patched!=6u) {
        for(unsigned i=0;i<patched;++i) {
            const auto slot=table+(379u+i)*4u;
            if(writable_slot(slot))
                InterlockedCompareExchangePointer(
                    reinterpret_cast<PVOID volatile*>(slot),
                    reinterpret_cast<PVOID>(originals[i]),
                    static_cast<PVOID>(g_rows[i].data()));
        }
        return false;
    }
    g_old_text=originals;
    g_language_table=table;
    return true;
}
void restore_text() noexcept {
    if(!g_language_table)return;
    for(unsigned i=0;i<6;++i) {
        const auto slot=g_language_table+(379u+i)*4u;
        if(writable_slot(slot))
            InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(slot),
                reinterpret_cast<PVOID>(g_old_text[i]),
                static_cast<void*>(g_rows[i].data()));
    }
    g_language_table=0;
}
DWORD WINAPI worker(void*) {
    build_texts();
    const DWORD began=GetTickCount();
    bool guarded=false;
    unsigned attempts=0;
    DWORD next_report=began+5000u;
    DWORD next_retry=began;
    bool reported_text_issue=false;
    // Keep the guard active while the process lives. Revert script hooks
    // when the original Menu level is unloaded; never silently expose
    // the original clickable settings after a diagnostic timeout.
    for(;;) {
        const DWORD now=GetTickCount();
        const bool in_menu=is_native_menu();
        if(!in_menu) {
            if(guarded) {
                g_protected.store(false,std::memory_order_release);
                restore_text();
                restore_callbacks();
                guarded=false;
                attempts=0;
                next_retry=now+500u;
                g_logger("RUNTIME-GRAPHICS-INFO: status=menu-unloaded native-text-restored=1 guards-disabled=1");
            }
        } else {
            MenuAiModel model{};MenuAiState mind{};
            if(!guarded && static_cast<LONG>(now-next_retry)>=0) {
                next_retry=now+1000u;
                if(get_verified_menu_controller(model,mind)) {
                    ++attempts;
                    if(install_callbacks(model)) {
                        guarded=true;
                        g_logger("RUNTIME-GRAPHICS-INFO: status=commands-guarded native-select-id=356 native-save-id=261 brightness-id=357-unmodified");
                    } else if(attempts==1 || attempts%5u==0)
                        g_logger("RUNTIME-GRAPHICS-INFO: status=callbacks-unavailable original-menu-retained");
                }
            }
            if(guarded && !g_language_table) {
                if(patch_text_table()) {
                    g_protected.store(true,std::memory_order_release);
                    g_logger("RUNTIME-GRAPHICS-INFO: status=native-text-installed handles=379..384 brightness385=untouched");
                } else if(!reported_text_issue &&
                         DWORD(now-began)>7000u) {
                    reported_text_issue=true;
                    g_logger("RUNTIME-GRAPHICS-INFO: status=text-table-unverified no-replacement");
                }
            }
        }
        if(static_cast<LONG>(now-next_report)>=0) {
            next_report=now+5000u;
            if(guarded) {
                char line[240]{};
                std::snprintf(line,sizeof(line),
                   "RUNTIME-GRAPHICS-INFO: status=active native_labels=%u video_actions_blocked=%u saves_blocked=%u original_video_calls_forwarded=%u",
                   unsigned(g_language_table!=0),
                   g_select_suppressed.load(std::memory_order_relaxed),
                   g_save_suppressed.load(std::memory_order_relaxed),
                   g_other_forwarded.load(std::memory_order_relaxed));
                g_logger(line);
            }
        }
        Sleep(100);
    }
    return 0;
}
} // namespace
void start_native_graphics_info_proto(GraphicsInfoLogger logger) noexcept {
    if(!logger || g_started.exchange(true,std::memory_order_acq_rel))return;
    if(!approved()) {
        logger("RUNTIME-GRAPHICS-INFO: status=unsupported-image no-changes");
        return;
    }
    g_logger=logger;
    logger("RUNTIME-GRAPHICS-INFO: status=started experimental=1 no-file-patch=1 fail-closed=1 native-menu=1");
    HANDLE t=CreateThread(nullptr,0,&worker,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    else logger("RUNTIME-GRAPHICS-INFO: status=no-worker no-changes");
}
} // namespace r2fix
