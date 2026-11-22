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
#include <cmath>
#include <cwchar>
#include "rayman2fix/graphics_menu_target.hpp"
#include "rayman2fix/graphics_menu_glyph_layout.hpp"
#include "rayman2fix/native_graphics_text_storage.hpp"
#include "rayman2fix/graphics_menu_controller_trace.hpp"
#include "rayman2fix/graphics_menu_controller_model.hpp"
#include "rayman2fix/graphics_menu_controller_vars.hpp"
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
// Until the live MENU AI script is verified, guard only the two approved
// VIDEO dispatcher IDs within MENU; switch to exact script children once
// the model is resolved. This prevents early user mode changes.
std::atomic<bool> g_guard_menu_video_dispatch{false};
std::atomic<bool> g_layout_active{false};
std::atomic<bool> g_cached_video_text_in_brightness{false};
std::atomic<unsigned> g_brightness_ghost_glyphs_suppressed{0};
std::atomic<unsigned> g_reflowed_glyphs{0};
// 3 non-glyph routes, up to 8 distinct geometries per route.
std::array<std::array<std::atomic<std::uint32_t>,8>,3> g_image_quad_samples{};
std::array<std::atomic<unsigned>,3> g_image_route_seen{};
std::atomic<unsigned> g_output_w{0};
std::atomic<unsigned> g_output_h{0};
std::atomic<bool> g_classic_4_3{false};
std::array<std::atomic<std::uintptr_t>,4> g_select_children{};
std::atomic<std::uintptr_t> g_save_child{0};
std::array<std::array<char,56>,6> g_rows{};
std::array<std::uintptr_t,6> g_old_text{};
std::uintptr_t g_language_table=0;
GraphicsInfoLogger g_logger=nullptr;
std::atomic<unsigned> g_select_suppressed{0};
std::atomic<unsigned> g_save_suppressed{0};
std::atomic<unsigned> g_other_forwarded{0};
std::atomic<unsigned> g_preflight_reported{0};
bool rejected(const char* reason) noexcept {
    if(g_logger && g_preflight_reported.fetch_add(1,std::memory_order_relaxed)<8u) {
        char message[180]{};
        std::snprintf(message,sizeof(message),
           "RUNTIME-GRAPHICS-INFO: status=preflight-refused reason=%s unchanged=1",
           reason);
        g_logger(message);
    }
    return false;
}

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
    // Image .data and stack memory may be copy-on-write under Wine and
    // Windows. Use the same page policy as the established F248 slot hook.
    if(!p || (p&(alignof(LONG)-1u))!=0u)return false;
    MEMORY_BASIC_INFORMATION m{};
    if(VirtualQuery(reinterpret_cast<const void*>(p),&m,sizeof(m))!=sizeof(m) ||
       m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))
        return false;
    const DWORD flags=m.Protect&0xffu;
    if(flags!=PAGE_READWRITE && flags!=PAGE_WRITECOPY &&
       flags!=PAGE_EXECUTE_READWRITE && flags!=PAGE_EXECUTE_WRITECOPY)
        return false;
    const auto begin=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
    const auto size=static_cast<std::uintptr_t>(m.RegionSize);
    return p>=begin && size>=sizeof(LONG) && p-begin<=size-sizeof(LONG);
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
    if(!model.valid())return rejected("model-invalid");
    if(!script(model.normal_entries,5,3,normal5))
        return rejected("normal5-script-unavailable");
    if(!script(model.normal_entries,19,2,normal19))
        return rejected("normal19-script-unavailable");
    // Compare both type byte and native command identifier per SNA node.
    const auto node=[&](std::uintptr_t where,std::uint32_t command,
                        std::uint8_t type)->bool {
        std::uint32_t id=0;std::uint8_t kind=0;
        return read32(where,id) && read8(where+7u,kind) &&
               id==command && kind==type;
    };
    const unsigned offsets[]{0x140u,0x190u,0x1E0u};
    for(unsigned i=0;i<3;++i) {
        if(!node(normal5+offsets[i],356u,4u))
            return rejected("normal5-select-script-mismatch");
        select[i]=normal5+offsets[i]+8u;
    }
    if(!node(normal19+0x48u,356u,4u))
        return rejected("normal19-select-script-mismatch");
    if(!node(normal5+0xD60u,261u,3u))
        return rejected("normal5-save-script-mismatch");
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
        const bool guard_dispatch=
            g_guard_menu_video_dispatch.load(std::memory_order_acquire);
        for(const auto& expected:g_select_children) {
            if(ptr && protected_menu_video_dispatch(
                   true,true,expected.load(std::memory_order_relaxed)==ptr,
                   guard_dispatch,NativeVideoCall::selection)) {
                // The exact child pointers were script-fingerprint verified
                // before the dispatcher slots were installed. The native
                // behavior may already have advanced by the time this x86
                // callback runs; requiring its *current* index here made
                // a video-setting action capable of slipping through.
                // Only this Menu-level ID 356 script is suppressed. Other
                // dispatcher calls are still forwarded without changes.
                if(!g_eval)break;
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
    // The saved child is the exact F261 script node from a validated
    // Menu AI model. Protect it even if the native behavior transitions
    // before this function returns; never intercept unrelated F261 calls.
    if(g_protected.load(std::memory_order_acquire) && is_native_menu() &&
       ptr && protected_menu_video_dispatch(
           true,true,ptr==g_save_child.load(std::memory_order_relaxed),
           g_guard_menu_video_dispatch.load(std::memory_order_acquire),
           NativeVideoCall::save) &&
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
    g_guard_menu_video_dispatch.store(false,std::memory_order_release);
    g_layout_active.store(false,std::memory_order_release);
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
bool install_callbacks(const MenuAiModel* model) noexcept {
    const auto base=exe_base();
    if(!approved() || !base)return rejected("executable-not-approved");
    BYTE proc[sizeof(kProcSignature)]{},fn[sizeof(kSaveSignature)]{},
         evaluator[sizeof(kEvalSignature)]{};
    if(!read_bytes(base+kProcSelectRva,proc,sizeof(proc)) ||
       std::memcmp(proc,kProcSignature,sizeof(proc)))
        return rejected("select-procedure-opcodes");
    if(!read_bytes(base+kFuncSaveRva,fn,sizeof(fn)) ||
       std::memcmp(fn,kSaveSignature,sizeof(fn)))
        return rejected("save-function-opcodes");
    if(!read_bytes(base+kScriptEvalRva,evaluator,sizeof(evaluator)) ||
       std::memcmp(evaluator,kEvalSignature,sizeof(evaluator)))
        return rejected("script-evaluator-opcodes");
    std::uint32_t native_proc=0,native_fn=0;
    if(!read32(base+kProcSelectSlot,native_proc) ||
       native_proc!=base+kProcSelectRva)
        return rejected("select-slot-target");
    if(!read32(base+kFuncSaveSlot,native_fn) ||
       native_fn!=base+kFuncSaveRva)
        return rejected("save-slot-target");
    if(!writable_slot(base+kProcSelectSlot))
        return rejected("select-slot-protection");
    if(!writable_slot(base+kFuncSaveSlot))
        return rejected("save-slot-protection");
    std::array<std::uintptr_t,4> children{};
    std::uintptr_t save=0;
    // When the MENU controller is still being located, use the native
    // dispatcher IDs verified by this approved EXE fingerprint instead
    // of leaving the original selectable modes live. Never intercept
    // GLI_DRV_hChangeMode, required during game initialization.
    const bool exact=model && model->valid();
    if(exact && !original_script_children(*model,children,save))return false;
    for(unsigned i=0;i<4;++i)
        g_select_children[i].store(children[i],std::memory_order_relaxed);
    g_save_child.store(save,std::memory_order_release);
    g_original_select=reinterpret_cast<SelectProc>(base+kProcSelectRva);
    g_original_save=reinterpret_cast<SaveFunc>(base+kFuncSaveRva);
    g_eval=reinterpret_cast<Evaluate>(base+kScriptEvalRva);
    if(!swap_slot(base+kProcSelectSlot,base+kProcSelectRva,
                  reinterpret_cast<void*>(&protected_select)))
        return rejected("select-slot-compare-exchange");
    if(!swap_slot(base+kFuncSaveSlot,base+kFuncSaveRva,
                  reinterpret_cast<void*>(&protected_save))) {
        restore_callbacks();
        return rejected("save-slot-compare-exchange");
    }
    g_guard_menu_video_dispatch.store(!exact,std::memory_order_release);
    g_preflight_reported.store(0,std::memory_order_relaxed);
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
    // Hard ceiling for native menu text: complete detected values remain
    // available to the launcher/log, never projected beyond the 4:3 canvas.
    g_rows[n][35]='\0';
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
    // The native game can render into a centered 4:3 window even when the
    // desktop is 16:9. Always show the launcher-approved GLIDE output,
    // not the EnumDisplaySettings physical desktop dimensions.
    const unsigned output_w=g_output_w.load(std::memory_order_acquire);
    const unsigned output_h=g_output_h.load(std::memory_order_acquire);
    const unsigned render_w=output_w?output_w:screen_w;
    const unsigned render_h=output_h?output_h:screen_h;
    const unsigned gcd=std::gcd(render_w,render_h);
    unsigned x=gcd?render_w/gcd:0u,y=gcd?render_h/gcd:0u;
    // Exact conventional 16:10 is labeled 16:10, not reduced 8:5.
    if(x==8u && y==5u){x=16u;y=10u;}
    // The original VIDEO heading remains in the scene. The original
    // RESOLUTION label shares a text row with a native video choice;
    // repeating a second information title collides with D3D9.
    std::snprintf(g_rows[0].data(),g_rows[0].size()," ");
    char line[100]{};
    // The launcher knows whether the player requested 4:3 or Auto.
    // Do not infer this from the output dimensions: a physical 4:3
    // monitor can legitimately run in Auto as well.
    std::snprintf(line,sizeof(line),"%ux%u %u:%u %s",
                  render_w,render_h,x,y,
                  g_classic_4_3.load(std::memory_order_acquire)?
                      "4:3":"AUTO");
    text_row(1,"OUTPUT:",line);
    text_row(2,"GPU:",adapter.data());
    text_row(3,"D3D9:",driver.data());
    // Native handle 384 ("OTHER") is not drawn on the verified FR menu.
    // Place both the native engine resolution and the desktop bit depth
    // in handle 383, the fourth actually visible information row.
    std::snprintf(g_rows[4].data(),g_rows[4].size(),
                  "GLIDE 1024x768x16 / DESKTOP %u",depth);
    std::snprintf(g_rows[5].data(),g_rows[5].size()," ");
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
    // Experimental Wine result 2026-11-22: staging ORIGINAL content in
    // replacement buffers before page entry displays the original labels
    // even after swapping buffer contents to info. The native JFFTXT
    // widget appears to cache its glyph string at construction.
    // Change the localization slot to the information pointer only
    // during the verified video-page behavior and restore on exit.
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
    bool was_visible=false;
    bool showed_information_in_menu=false;
    unsigned last_latch=~0u;
    // Keep the guard active while the process lives. Revert script hooks
    // when the original Menu level is unloaded; never silently expose
    // the original clickable settings after a diagnostic timeout.
    for(;;) {
        const DWORD now=GetTickCount();
        const bool in_menu=is_native_menu();
        if(!in_menu) {
            if(guarded) {
                g_protected.store(false,std::memory_order_release);
                g_layout_active.store(false,std::memory_order_release);
                g_cached_video_text_in_brightness.store(
                    false,std::memory_order_release);
                showed_information_in_menu=false;
                was_visible=false;
                last_latch=~0u;
                restore_text();
                restore_callbacks();
                guarded=false;
                attempts=0;
                next_retry=now+500u;
                g_logger("RUNTIME-GRAPHICS-INFO: status=menu-unloaded native-text-restored=1 guards-disabled=1");
            }
        } else {
            MenuAiModel model{};MenuAiState mind{};
            const bool controller_ready=get_verified_menu_controller(model,mind);
            if(!guarded && static_cast<LONG>(now-next_retry)>=0) {
                next_retry=now+1000u;
                ++attempts;
                if(install_callbacks(controller_ready ? &model : nullptr)) {
                    guarded=true;
                    // Guard video writes even before the AI model scan
                    // finishes. Never change brightness (procedure 357).
                    g_protected.store(true,std::memory_order_release);
                    g_logger(controller_ready ?
                        "RUNTIME-GRAPHICS-INFO: status=commands-guarded scope=verified-script-children native-select-id=356 native-save-id=261 brightness-id=357-unmodified" :
                        "RUNTIME-GRAPHICS-INFO: status=commands-guarded scope=MENU-video-dispatch native-select-id=356 native-save-id=261 brightness-id=357-unmodified controller=pending");
                } else if(attempts==1 || attempts%5u==0)
                    g_logger("RUNTIME-GRAPHICS-INFO: status=callbacks-unavailable original-menu-retained preflight-log=1");
            }
            if(guarded && controller_ready &&
               g_guard_menu_video_dispatch.load(std::memory_order_acquire)) {
                // Narrow to the four exact selection children and one save
                // child as soon as the loaded AI model is verified.
                std::array<std::uintptr_t,4> children{};
                std::uintptr_t save=0;
                if(original_script_children(model,children,save)) {
                    for(unsigned i=0;i<4;++i)
                        g_select_children[i].store(children[i],std::memory_order_relaxed);
                    g_save_child.store(save,std::memory_order_release);
                    g_guard_menu_video_dispatch.store(false,std::memory_order_release);
                    g_logger("RUNTIME-GRAPHICS-INFO: status=commands-narrowed scope=verified-script-children controller=validated");
                }
            }
            if(guarded) {
                MenuAiModel current_model{};MenuAiState current{};
                // The original text handles are global to MENU. Do not
                // replace them in Options, Brightness or unrelated pages.
                // Normal 5/reflex 0 is a verified video-choice candidate,
                // though the final page identity needs visual validation.
                const bool valid_controller=get_verified_menu_controller(
                        current_model,current);
                const auto vars=valid_controller ?
                    observe_menu_selection(current.mind,&read32,&read8) :
                    MenuSelectionVars{};
                const bool video_page=graphics_info_video_page(
                    approved(),in_menu,valid_controller,
                    current.normal,current.reflex,
                    vars.selection_valid,vars.selection,
                    vars.activate_valid,vars.activate);
                if(video_page) {
                    g_cached_video_text_in_brightness.store(
                        false,std::memory_order_release);
                    if(native_graphics_text_transition(true,g_language_table!=0)==
                       NativeGraphicsTextTransition::install_information_pointers) {
                        if(patch_text_table()) {
                            showed_information_in_menu=true;
                            g_logger("RUNTIME-GRAPHICS-INFO: status=native-text-installed handles=379..384 brightness385=untouched scope=verified-video-page content=information pointer-swap=1");
                        } else if(!reported_text_issue &&
                                  DWORD(now-began)>7000u) {
                            reported_text_issue=true;
                            g_logger("RUNTIME-GRAPHICS-INFO: status=text-table-unverified no-replacement");
                        }
                    }
                    g_layout_active.store(g_language_table!=0,
                                          std::memory_order_release);
                    if(g_language_table &&
                       (!was_visible || last_latch!=vars.activate)) {
                        char event[180]{};
                        std::snprintf(event,sizeof(event),
                            "RUNTIME-GRAPHICS-INFO: status=video-page-visible row=%d latch=%u return_visit=%u controls=guarded",
                            int(vars.selection),unsigned(vars.activate),
                            unsigned(was_visible));
                        g_logger(event);
                        was_visible=true;
                        last_latch=vars.activate;
                    }
                } else {
                    // The original MENU still retains rendered choices after
                    // a localization pointer restoration. Suppress ONLY
                    // their cached glyph quads while the verified brightness
                    // behavior is active, never any other menu or scene.
                    const bool brightness=valid_controller &&
                        current.normal==18 && current.reflex==5;
                    g_cached_video_text_in_brightness.store(
                        brightness && showed_information_in_menu,
                        std::memory_order_release);
                    // Preserve all other Options screens: restore the
                    // original localized POINTERS when leaving VIDEO.
                    g_layout_active.store(false,std::memory_order_release);
                    if(was_visible && g_language_table)
                        g_logger("RUNTIME-GRAPHICS-INFO: status=video-page-exit original-label-pointers-restored=1");
                    was_visible=false;
                    last_latch=~0u;
                    if(native_graphics_text_transition(false,g_language_table!=0)==
                       NativeGraphicsTextTransition::restore_original_pointers)
                        restore_text();
                }
            }
        }
        if(static_cast<LONG>(now-next_report)>=0) {
            next_report=now+5000u;
            if(guarded) {
                char line[320]{};
                std::snprintf(line,sizeof(line),
                   "RUNTIME-GRAPHICS-INFO: status=active native_labels=%u video_actions_blocked=%u saves_blocked=%u original_video_calls_forwarded=%u reflowed_glyphs=%u observed_non_glyph_routes=%u,%u,%u",
                   unsigned(g_layout_active.load(std::memory_order_acquire)),
                   g_select_suppressed.load(std::memory_order_relaxed),
                   g_save_suppressed.load(std::memory_order_relaxed),
                   g_other_forwarded.load(std::memory_order_relaxed),
                   g_reflowed_glyphs.load(std::memory_order_relaxed),
                   g_image_route_seen[0].load(std::memory_order_relaxed),
                   g_image_route_seen[1].load(std::memory_order_relaxed),
                   g_image_route_seen[2].load(std::memory_order_relaxed));
                g_logger(line);
            }
        }
        Sleep(100);
    }
    return 0;
}
} // namespace
void observe_native_graphics_info_non_glyph(unsigned route,
    float xmin,float ymin,float xmax,float ymax) noexcept {
    if(route<1u || route>3u ||
       !g_layout_active.load(std::memory_order_acquire) || !g_logger)return;
    const unsigned idx=route-1u;
    g_image_route_seen[idx].fetch_add(1u,std::memory_order_relaxed);
    if(!std::isfinite(xmin) || !std::isfinite(xmax) ||
       !std::isfinite(ymin) || !std::isfinite(ymax) ||
       xmin< -5.0f || xmax>110.0f || ymin< -5.0f || ymax>95.0f ||
       xmax-xmin<0.5f || xmax-xmin>40.0f ||
       ymax-ymin<0.5f || ymax-ymin>40.0f)return;
    // Quantize to stable 2% geometry buckets and cap each route to 8
    // unique observations. No material identities, raw pointers, or
    // unbounded per-frame logs; never change the original quads.
    const auto q=[](float value)->std::uint32_t {
        const int rounded=int(value*0.5f+5.5f);
        return std::uint32_t(rounded<0?0:(rounded>255?255:rounded));
    };
    const std::uint32_t key=q(xmin)|(q(xmax)<<8)|
                            (q(ymin)<<16)|(q(ymax)<<24);
    static constexpr const char* names[]{"screen-image","frame","other"};
    for(auto& slot:g_image_quad_samples[idx]) {
        std::uint32_t observed=slot.load(std::memory_order_relaxed);
        if(observed==key)return;
        if(observed!=0u)continue;
        if(!slot.compare_exchange_strong(observed,key,
              std::memory_order_relaxed))continue;
        char line[230]{};
        std::snprintf(line,sizeof(line),
            "RUNTIME-GRAPHICS-QUAD: route=%s x=%.2f..%.2f y=%.2f..%.2f candidate=unverified read-only=1",
            names[idx],double(xmin),double(xmax),double(ymin),double(ymax));
        g_logger(line);
        return;
    }
}
bool suppress_cached_native_graphics_info_glyph(
    float xmin,float ymin,float xmax,float ymax) noexcept {
    if(!g_cached_video_text_in_brightness.load(
           std::memory_order_acquire))return false;
    if(!cached_video_glyph_in_brightness(true,true,
           {xmin,ymin,xmax,ymax}))return false;
    if(g_brightness_ghost_glyphs_suppressed.fetch_add(
           1,std::memory_order_relaxed)==0 && g_logger)
        g_logger("RUNTIME-GRAPHICS-INFO: status=brightness-cached-video-glyph-filter first=1 title=untouched slider=untouched butterfly=untouched");
    return true;
}
bool remap_native_graphics_info_glyph(float& xmin,float& ymin,
                                      float& xmax,float& ymax) noexcept {
    if(!g_layout_active.load(std::memory_order_acquire))return false;
    GraphicsInfoQuad out{};
    if(!layout_graphics_info_quad({xmin,ymin,xmax,ymax},out))
        return false;
    xmin=out.left;ymin=out.top;xmax=out.right;ymax=out.bottom;
    g_reflowed_glyphs.fetch_add(1,std::memory_order_relaxed);
    return true;
}
void start_native_graphics_info_proto(GraphicsInfoLogger logger,
                                       unsigned output_width,
                                       unsigned output_height,
                                       bool classic_4_3) noexcept {
    if(!logger || g_started.exchange(true,std::memory_order_acq_rel))return;
    g_output_w.store(output_width,std::memory_order_release);
    g_output_h.store(output_height,std::memory_order_release);
    g_classic_4_3.store(classic_4_3,std::memory_order_release);
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
