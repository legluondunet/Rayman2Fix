// Experimental NATIVE menu information replacement, opt-in CI artifact only.
// The original Fix localization pointer table is changed in memory (no
// game file modifications, no code-section detours). Two original x86
// dispatcher-table slots are hooked to suppress ONLY four validated
// video-selection script calls and one native save function call.
// All unsupported states leave the original game unchanged.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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
#include "rayman2fix/native_graphics_static_text_policy.hpp"
#include "rayman2fix/native_video_disable_choices.hpp"
#include "rayman2fix/native_graphics_info_proto.hpp"
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
#include "rayman2fix/standalone_video_page.hpp"
#include "rayman2fix/standalone_video_transition.hpp"
#include "rayman2fix/standalone_video_cursor_policy.hpp"
#include "rayman2fix/main_menu_site.hpp"
#include "rayman2fix/pause_video_observer.hpp"
#endif

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
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
// The native ESC pause belongs to a different scene/controller than
// the approved Menu.sna normal-5 page. Treat its generic-menu flag
// as a diagnostic ONLY, never permission to rewrite scene scripts.
constexpr unsigned kGenericEscMenuFlagRva=0x0B9BC8u;
#endif
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
std::atomic<bool> g_video_prepare_active{false}; // validated normal-3 pre-VIDEO state
std::atomic<bool> g_brightness_transition_disabled{false};
std::atomic<int> g_info_focus_row{-1}; // read-only V17 snapshot for material probe
std::atomic<bool> g_cached_video_text_in_brightness{false};
std::atomic<unsigned> g_brightness_ghost_glyphs_suppressed{0};
std::atomic<unsigned> g_brightness_meter_quads_suppressed{0};
std::atomic<unsigned> g_pre_video_old_glyphs_suppressed{0};
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
std::array<std::uintptr_t,7> g_old_text{};
unsigned g_info_text_slots_swapped=0;
char g_hidden_brightness_label[2]{' ',0};
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
    // Obtain the display adapter label from Win32/Wine only. The game
    // renders through GameFrame's Glide2 wrapper; a D3D9 adapter probe
    // reported atidx9loader32.dll (AMD's D3D driver) even though this is
    // NOT the renderer. Never load d3d9.dll just to populate this page.
    // Under Wine the reported display adapter may be virtual/translated
    // and is not proof of the host's Mesa/RADV/NVIDIA driver version.
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
    // repeating a second information title collides with the rows below.
    std::snprintf(g_rows[0].data(),g_rows[0].size()," ");
    char line[100]{};
    // The launcher knows whether the player requested 4:3 or Auto.
    // Do not infer this from the output dimensions: a physical 4:3
    // monitor can legitimately run in Auto as well.
    // OUTPUT is the launcher-approved display surface, not the 16-bit
    // legacy Glide frame buffer. The bit depth read from Win32's
    // active desktop mode is shown with OUTPUT only when available.
    // "32BIT" is a compatibility desktop pixel format, not proof of
    // 32-bit precision in the game's historical 3D rendering.
    const char* aspect=g_classic_4_3.load(std::memory_order_acquire)?
        "4:3":"AUTO";
    if(depth)
        std::snprintf(line,sizeof(line),"%ux%u %u:%u %s %uBIT",
                      render_w,render_h,x,y,aspect,depth);
    else
        std::snprintf(line,sizeof(line),"%ux%u %u:%u %s",
                      render_w,render_h,x,y,aspect);
    text_row(1,"OUTPUT:",line);
    text_row(2,"GPU:",adapter.data());
    text_row(3,"RENDER:", "GAMEFRAME GLIDE2");
    // The original Glide mode is separate from the desktop format:
    // 1024x768x16 is the internal RGB565 legacy game rendering mode.
    // Native handle 384 ("OTHER") is not drawn on the FR menu.
    std::snprintf(g_rows[4].data(),g_rows[4].size(),
                  "GLIDE: 1024x768x16");
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
// The previous memory-only script-clone preflight was rejected under Wine.
// Report WHICH validation gate disagrees, without cloning, swapping or
// writing ANY game byte. Use the hash-locked approved normal-5 model only.
void trace_native_static_script_preflight(const MenuAiModel& model) noexcept {
    if(!model.valid() || !approved() || !g_logger)return;
    const auto behavior=model.normal_entries+5u*12u;
    std::uint32_t list=0;
    std::uint8_t count=0;
    if(!read32(behavior,list) || !read8(behavior+8u,count) ||
       count!=5u || !menu_safe_ptr(list,20u)) {
        g_logger("RUNTIME-STATIC-SCRIPT-DIAG: status=normal5-list-unverified read-only=1");
        return;
    }
    for(const auto& plan:kNativeInfoTextPlans) {
        const auto slot=std::uintptr_t(list)+plan.script_index*4u;
        std::uint32_t original=0;
        const bool slot_writable=writable_slot(slot);
        const bool ptr_ok=read32(slot,original) &&
            menu_safe_ptr(original,plan.node_count*8u);
        std::array<NativeMenuScriptNode,517> local{};
        const bool bytes_ok=ptr_ok && read_bytes(original,local.data(),
           plan.node_count*sizeof(NativeMenuScriptNode));
        const auto observed_hash=bytes_ok ?
            native_menu_script_fingerprint(local.data(),plan.node_count):
            0ULL;
        const bool hash_ok=bytes_ok &&
            observed_hash==plan.scene_fingerprint;
        const bool tail_ok=bytes_ok &&
            native_video_font_tail_valid(local.data(),plan);
        // The compact tail opcode/argument probe distinguishes a wrong
        // script-list slot from a matching terminal call whose unrelated
        // absolute/relocated parameters invalidate its full fingerprint.
        // No absolute process address or game text is logged.
        const auto& command=local[plan.jfftxt_node_index];
        const auto& value=local[plan.jfftxt_node_index+1u];
        char line[300]{};
        std::snprintf(line,sizeof(line),
            "RUNTIME-STATIC-SCRIPT-DIAG: script=%u nodes=%u slot_write=%u ptr_valid=%u bytes=%u hash=%u tail=%u observed_fingerprint=%08X%08X opcode=%u kind=%u argtype=%u argindex=%u read-only=1",
            plan.script_index,plan.node_count,
            unsigned(slot_writable),unsigned(ptr_ok),
            unsigned(bytes_ok),unsigned(hash_ok),unsigned(tail_ok),
            unsigned(observed_hash>>32),unsigned(observed_hash),
            unsigned(command.parameter),unsigned(command.type),
            unsigned(value.type),unsigned(value.parameter));
        g_logger(line);
    }
}
// Live VIDEO option-state suppression. Disabled choices use the game's
// own V14 UI state=1 (the same disabled state as the original inactive
// save-resolution entry). Brightness stays state=2, V17 is NEVER forced.
// All three relevant normal-5 scripts are copied and verified;
// the original script memory, source assets and code sections stay intact.
struct NativeChoicesPatch {
    struct Slot {
        std::uintptr_t pointer_slot=0;
        std::uintptr_t original=0;
        NativeMenuScriptNode* copy=nullptr;
    };
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
    // Four normal-5 script copies plus exactly one verified normal-3
    // VIDEO-open script. All are preflighted before any slot is swapped.
    static constexpr unsigned kSlotCount=5u;
#else
    static constexpr unsigned kSlotCount=4u;
#endif
    std::array<Slot,kSlotCount> slots{};
    bool attempted=false;
    bool active=false;
    bool ever_swapped=false;
};
bool install_native_disabled_video_choices(
    NativeChoicesPatch& state,const MenuAiModel& model) noexcept {
    if(!approved() || !model.valid() || state.attempted)return false;
    state.attempted=true;
    const std::uintptr_t entry=model.normal_entries+5u*12u;
    std::uint32_t scripts=0;
    std::uint8_t count=0;
    if(!read32(entry,scripts) || !read8(entry+8u,count) ||
       count!=5u || !menu_safe_ptr(scripts,5u*4u)) {
        g_logger("RUNTIME-GRAPHICS-CHOICES: status=disabled-patch-refused reason=normal5-script-list read-only-original=1");
        return false;
    }
    std::array<NativeChoicesPatch::Slot,NativeChoicesPatch::kSlotCount>
        prepared{};
    bool valid=true;
    const char* reason="unverified";
    for(unsigned k=0u;k<4u;++k) {
        const bool is_brightness_transition=k==1u;
        const auto& plan=kVideoDisablePlans[k==0u?0u:(k-1u)];
        const unsigned script_index=is_brightness_transition ?
            kNativeBrightnessTransitionScript : plan.script_index;
        const unsigned script_nodes=is_brightness_transition ?
            kNativeBrightnessTransitionNodes : plan.count;
        const auto slot=std::uintptr_t(scripts)+4u*script_index;
        std::uint32_t original=0;
        const SIZE_T bytes=SIZE_T(script_nodes)*sizeof(NativeMenuScriptNode);
        if(!writable_slot(slot) || !read32(slot,original) ||
           !menu_safe_ptr(original,unsigned(bytes))) {
            valid=false;reason="slot-or-script-pointer";break;
        }
        auto* copy=static_cast<NativeMenuScriptNode*>(
            VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,
                         PAGE_READWRITE));
        if(!copy) {
            valid=false;reason="allocation";break;
        }
        prepared[k]={slot,original,copy};
        if(!read_bytes(original,copy,bytes)) {
            valid=false;reason="unreadable-script";break;
        }
        const auto observed=native_video_script_shape_hash(
            copy,script_nodes);
        const auto required_shape=is_brightness_transition ?
            kNativeBrightnessTransitionShape : plan.original_shape_hash;
        if(observed!=required_shape) {
            char line[220]{};
            std::snprintf(line,sizeof(line),
                "RUNTIME-GRAPHICS-CHOICES: status=script-shape-mismatch script=%u observed=%08X%08X approved=%08X%08X no-write=1",
                script_index,unsigned(observed>>32),unsigned(observed),
                unsigned(required_shape>>32),
                unsigned(required_shape));
            g_logger(line);
            valid=false;reason="script-shape";break;
        }
        // Normal-5 script 1 also feeds native parent Options labels.
        // Never change its V14 status or JFFTXT title/focus nodes.
        // Its independently verified V20=3 bounds the obsolete VIDEO
        // selection and must instead be 0 BEFORE the AI consumes Down.
        if(k==0u) {
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
            if(!standalone_video_script1_singleton(copy,plan)) {
                valid=false;reason="video-script1-singleton";break;
            }
#endif
            continue;
        }
        const auto* font_plan=k==3u ? &kNativeInfoTextPlans[1] : nullptr;
        if(font_plan && !native_video_static_font_preflight(
                copy,plan,*font_plan)) {
            valid=false;reason="font-focus-preflight";break;
        }
        if(is_brightness_transition) {
            if(!suppress_native_video_brightness_transition(
                 copy,script_nodes,
                 model.normal_entries+18u*12u,
                 model.reflex_entries+5u*12u)) {
                valid=false;reason="brightness-transition-script";break;
            }
            continue;
        }
        // V51 is the native Enter/confirm latch. The obsolete
        // SAVE RESOLUTION row must not become visually enabled for
        // a single frame when Enter is pressed on our static page.
        // This patch only changes V14[4]=2 to 1 in verified VIDEO
        // script copies 3/4; the parent Options script 1 is untouched.
        if(!neutralize_native_video_enter_save_status(copy,plan)) {
            valid=false;reason="enter-save-status";break;
        }
        if(!disable_native_video_choices(copy,plan)) {
            valid=false;reason="script-assignments";break;
        }
        if(k>=2u && !restrict_native_video_navigation(
                copy,kVideoNavigationPlans[k-2u])) {
            valid=false;reason="navigation-assignments";break;
        }
        if(font_plan && !freeze_native_video_info_font(
                copy,*font_plan)) {
            valid=false;reason="font-focus-assignment";break;
        }
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
        // Stop both old VIDEO widget scripts in validated PRIVATE clones;
        // the independent fix.dll Glide panel now owns their presentation.
        if(k>=2u && !standalone_remove_legacy_video_widgets(
                  copy,plan.script_index,script_nodes)) {
            valid=false;reason="standalone-script-termination";break;
        }
#endif
    }
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
    if(valid) {
        // SOURCE-LEVEL VIDEO entry replacement. Normal-3 script 2
        // is also used by parent Options: its V17!=0 branches are
        // retained byte-for-byte. Only V17==0 is shortened to its
        // original native normal-5 transition calls. The source SNA
        // (1,383 nodes) is authenticated before ANY live pointer CAS.
        const std::uintptr_t n3=model.normal_entries+3u*12u;
        std::uint32_t list=0,script=0;
        std::uint8_t count=0;
        // Use the verified *runtime* behavior entry layout (script-list
        // pointer at +0, count at +8). This differs from the encoded
        // CD scene descriptor layout, so no guessed direct pointer.
        if(!read32(n3,list) || !read8(n3+8u,count) ||
           count<3u || count>4u || !menu_safe_ptr(list,3u*4u)) {
            valid=false;reason="normal3-script-list";
        } else {
            const auto slot=std::uintptr_t(list)+2u*4u;
            const SIZE_T bytes=SIZE_T(kStandalonePreparationOriginalNodes)*
                sizeof(NativeMenuScriptNode);
            if(!writable_slot(slot) || !read32(slot,script) ||
               !menu_safe_ptr(script,unsigned(bytes))) {
                valid=false;reason="normal3-script-slot";
            } else {
                auto* copy=static_cast<NativeMenuScriptNode*>(
                    VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,
                                 PAGE_READWRITE));
                if(!copy) {
                    valid=false;reason="normal3-script-allocation";
                } else {
                    prepared[4]={slot,script,copy};
                    if(!read_bytes(script,copy,bytes) ||
                       !standalone_skip_legacy_video_open(
                           copy,kStandalonePreparationOriginalNodes)) {
                        valid=false;reason="normal3-video-open-shape";
                    }
                }
            }
        }
    }
#endif
    if(!valid) {
        for(auto& item:prepared)
            if(item.copy)VirtualFree(item.copy,0,MEM_RELEASE);
        char line[160]{};
        std::snprintf(line,sizeof(line),
            "RUNTIME-GRAPHICS-CHOICES: status=disabled-patch-refused reason=%s original-scripts-unchanged=1",
            reason);
        g_logger(line);
        return false;
    }
    // Swap only on an approved MENU state OUTSIDE normal-5 script
    // execution. Own all previously published clones until process exit:
    // an in-flight original game script may still hold their addresses.
    // Hold the broader approved MENU dispatcher guard only during the
    // atomic switch. Old exact child addresses stop matching when a
    // private copy is installed; never expose an unprotected VIDEO 356
    // or save 261 call in that narrow interval.
    const bool prior_fallback=g_guard_menu_video_dispatch.exchange(
        true,std::memory_order_acq_rel);
    unsigned exchanged=0u;
    for(;exchanged<prepared.size();++exchanged) {
        const auto& item=prepared[exchanged];
        if(!swap_slot(item.pointer_slot,item.original,item.copy))
            break;
    }
    if(exchanged!=prepared.size()) {
        // No reader can see a freed clone: retain every allocation once
        // even one script-list pointer was published.
        unsigned confirmed=0u;
        for(unsigned i=0;i<exchanged;++i) {
            const auto& item=prepared[i];
            if(!writable_slot(item.pointer_slot))continue;
            const auto observed=InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(item.pointer_slot),
                reinterpret_cast<PVOID>(item.original),
                reinterpret_cast<PVOID>(item.copy));
            if(observed==reinterpret_cast<PVOID>(item.copy) ||
               observed==reinterpret_cast<PVOID>(item.original))
                ++confirmed;
        }
        state.ever_swapped=exchanged>0u;
        state.active=confirmed!=exchanged;
        if(state.ever_swapped)state.slots=prepared;
        else for(auto& item:prepared)
            if(item.copy)VirtualFree(item.copy,0,MEM_RELEASE);
        if(!state.active)
            g_guard_menu_video_dispatch.store(
                prior_fallback,std::memory_order_release);
        char line[220]{};
        std::snprintf(line,sizeof(line),
            "RUNTIME-GRAPHICS-CHOICES: status=disabled-patch-refused reason=script-list-cas attempted=%u restored=%u guard=%u",
            exchanged,confirmed,unsigned(state.active));
        g_logger(line);
        return false;
    }
    state.slots=prepared;
    state.ever_swapped=true;
    state.active=true;
    // Script 3 is clone 2. Retarget its exact VID_SelectResolution and
    // VID_SaveCurrentResolution guarded children before dropping fallback.
    // Script 1, script 2 and script 4 contain no video-mode call at these
    // offsets. The original normal-19 selection child is unchanged.
    static constexpr unsigned kSelectOffsets[3]{0x140u,0x190u,0x1E0u};
    for(unsigned i=0;i<3u;++i)
        g_select_children[i].store(
            reinterpret_cast<std::uintptr_t>(prepared[2].copy)+
                kSelectOffsets[i]+8u,std::memory_order_relaxed);
    g_save_child.store(
        reinterpret_cast<std::uintptr_t>(prepared[2].copy)+0xD60u+8u,
        std::memory_order_release);
    g_guard_menu_video_dispatch.store(false,std::memory_order_release);
    g_brightness_transition_disabled.store(true,std::memory_order_release);
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
    g_logger("RUNTIME-STANDALONE-VIDEO: old-widgets-not-constructed normal3-script2=direct-video-open normal5-scripts3,4=terminated normal5-script1=singleton1..1-JFFTXT-unchanged cursor=game-thread-guarded other-options-branches=retained selection-dispatch=guarded");
#else
    g_logger("RUNTIME-GRAPHICS-CHOICES: status=readonly-video-script-copies-installed scripts=1-original,2,3,4 brightness-transition=suppressed font-focus=static-0-video4-only enter-save-status=inert parent-options-preserved=1 original-butterfly=untouched navigation-range=5..5 protected-dispatch-retargeted=1");
#endif
    return true;
}
void restore_native_disabled_video_choices(
    NativeChoicesPatch& state) noexcept {
    if(!state.ever_swapped)return;
    g_brightness_transition_disabled.store(false,std::memory_order_release);
    // Keep mode mutation blocked while the script list transitions back
    // from clone addresses to the original game addresses.
    g_guard_menu_video_dispatch.store(true,std::memory_order_release);
    unsigned restored=0u;
    for(auto& item:state.slots) {
        if(!item.pointer_slot || !writable_slot(item.pointer_slot))
            continue;
        const auto found=InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(item.pointer_slot),
            reinterpret_cast<PVOID>(item.original),
            reinterpret_cast<PVOID>(item.copy));
        if(found==reinterpret_cast<PVOID>(item.copy) ||
           found==reinterpret_cast<PVOID>(item.original))
            ++restored;
    }
    // Never free a published copy; race-free reclamation requires an AI
    // interpreter quiescence guarantee that this experiment does not have.
    char line[170]{};
    std::snprintf(line,sizeof(line),
        "RUNTIME-GRAPHICS-CHOICES: status=script-copies-restore-attempt confirmed=%u/%u copied-buffers-retained=1",
        restored,unsigned(state.slots.size()));
    g_logger(line);
    state=NativeChoicesPatch{};
}
// The seventh VIDEO localization entry (385) is the obsolete
// Luminosité label. It must disappear from the informational page,
// without touching any original game resource or the 3D butterfly.
bool patch_text_table() noexcept {
    if(g_language_table)return true;
    const auto table=text_slot_table();
    if(!table)return false;
    const unsigned slots=g_brightness_transition_disabled.load(
        std::memory_order_acquire) ? 7u : 6u;
    std::array<std::uintptr_t,7> originals{};
    for(unsigned i=0;i<slots;++i) {
        const auto slot=table+(379u+i)*4u;
        std::uint32_t p=0;
        if(!writable_slot(slot) || !read32(slot,p) ||
           !menu_safe_ptr(p) || !original_text_valid(p))return false;
        originals[i]=p;
    }
    unsigned patched=0;
    for(unsigned i=0;i<slots;++i) {
        const auto slot=table+(379u+i)*4u;
        void* replacement=i==6u ?
            static_cast<void*>(g_hidden_brightness_label) :
            static_cast<void*>(g_rows[i].data());
        if(!swap_slot(slot,originals[i],replacement))break;
        ++patched;
    }
    if(patched!=slots) {
        for(unsigned i=0;i<patched;++i) {
            const auto slot=table+(379u+i)*4u;
            if(writable_slot(slot))
                InterlockedCompareExchangePointer(
                    reinterpret_cast<PVOID volatile*>(slot),
                    reinterpret_cast<PVOID>(originals[i]),
                    i==6u ? static_cast<PVOID>(g_hidden_brightness_label) :
                             static_cast<PVOID>(g_rows[i].data()));
        }
        return false;
    }
    g_old_text=originals;
    g_info_text_slots_swapped=slots;
    g_language_table=table;
    return true;
}
void restore_text() noexcept {
    if(!g_language_table)return;
    for(unsigned i=0;i<g_info_text_slots_swapped;++i) {
        const auto slot=g_language_table+(379u+i)*4u;
        if(writable_slot(slot))
            InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(slot),
                reinterpret_cast<PVOID>(g_old_text[i]),
                i==6u ? static_cast<PVOID>(g_hidden_brightness_label) :
                         static_cast<PVOID>(g_rows[i].data()));
    }
    g_language_table=0;
    g_info_text_slots_swapped=0;
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
    bool reported_brightness_panel=false;
    bool reported_static_script_preflight=false;
    NativeChoicesPatch disabled_choices{};
    unsigned last_latch=~0u;
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
    int prev_pause=-1;
    unsigned pause_reports=0u;
#endif
    // Keep the guard active while the process lives. Revert script hooks
    // when the original Menu level is unloaded; never silently expose
    // the original clickable settings after a diagnostic timeout.
    for(;;) {
        const DWORD now=GetTickCount();
        const bool in_menu=is_native_menu();
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
        if(!in_menu && pause_reports<16u) {
            std::uint8_t generic_pause=0;
            if(approved() &&
               read8(exe_base()+kGenericEscMenuFlagRva,generic_pause)) {
                const int state=generic_pause!=0u?1:0;
                if(state!=prev_pause) {
                    char level[16]{};
                    read_bytes(exe_base()+kLevelNameRva,level,15u);
                    for(char& ch:level)
                        if(ch && (static_cast<unsigned char>(ch)<32u ||
                                  static_cast<unsigned char>(ch)>126u))
                            ch='_';
                    char line[260]{};
                    std::snprintf(line,sizeof(line),
                       "RUNTIME-PAUSE-VIDEO: status=observed generic-esc-menu=%d level=%.15s scope=passive-diagnostic-only no-pause-menu-patch=1",
                       state,level[0]?level:"unknown");
                    g_logger(line);
                    ++pause_reports;
                    prev_pause=state;
                }
            }
        }
        if(in_menu) prev_pause=-1;
#endif
        if(!in_menu) {
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
            standalone_video_page_set_visible(false);
#endif
            if(disabled_choices.ever_swapped)
                restore_native_disabled_video_choices(disabled_choices);
            else
                // A failed preflight applies to one loaded MENU only.
                // A later MENU load may use a different valid scene.
                disabled_choices=NativeChoicesPatch{};
            if(guarded) {
                g_protected.store(false,std::memory_order_release);
                g_layout_active.store(false,std::memory_order_release);
                g_video_prepare_active.store(false,std::memory_order_release);
                g_info_focus_row.store(-1,std::memory_order_release);
                g_cached_video_text_in_brightness.store(
                    false,std::memory_order_release);
                showed_information_in_menu=false;
                reported_brightness_panel=false;
                reported_static_script_preflight=false;
                was_visible=false;
                last_latch=~0u;
                restore_text();
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
                restore_main_menu_site();
#endif
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
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
            if(guarded && controller_ready)
                install_main_menu_site(text_slot_table(),g_logger);
#endif
            if(guarded && controller_ready &&
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
               mind.normal!=3 && mind.normal!=5 &&
#else
               mind.normal!=5 &&
#endif
               !disabled_choices.attempted) {
                // Never replace an actively executing script: the
                // independent build also replaces verified normal-3
                // script-2 BEFORE its V17==0 VIDEO opening branch.
                // An all-or-nothing five-script preflight precedes CAS.
                install_native_disabled_video_choices(disabled_choices,model);
            }
            // All 379..385 JFFTXT references in the approved Menu.sna
            // belong to the VIDEO preparation/choice/return behaviors
            // (normal 3, 5, 13 and 19). Installing the replacement
            // pointers only after entering normal 3 lets the native widget
            // render its original labels for a frame before it caches the
            // new ones. Once the four verified scripts are installed,
            // pin the 7 VIDEO-only strings for this MENU lifetime.
            // Styling and input remain restricted to the verified VIDEO
            // state; strings are restored on MENU unload.
#if !defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) || !RAYMAN2FIX_STANDALONE_VIDEO_PAGE
            if(graphics_info_pin_text_for_menu(
                   approved(),in_menu,guarded,controller_ready,
                   disabled_choices.active,g_language_table!=0) &&
               patch_text_table())
                g_logger("RUNTIME-GRAPHICS-INFO: status=menu-video-text-pinned scope=verified-menu-controller before-video-entry=1 restored-on-menu-unload=1");
#endif
            if(guarded && controller_ready && !disabled_choices.active &&
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
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
                // The new page is independent of old native text handles,
                // V17 selection, font quad remapping and VIDEO widgets.
                const bool page=standalone_video_page_state(
                    approved(),in_menu,valid_controller,
                    disabled_choices.active,current.normal,current.reflex);
                standalone_video_page_set_visible(page);
                g_layout_active.store(false,std::memory_order_release);
                g_video_prepare_active.store(false,std::memory_order_release);
                g_cached_video_text_in_brightness.store(
                    false,std::memory_order_release);
                if(page!=was_visible) {
                    g_logger(page ?
                        "RUNTIME-STANDALONE-VIDEO: state=visible own-page=1 native-choice-script=disabled" :
                        "RUNTIME-STANDALONE-VIDEO: state=hidden native-options-restored=1");
                    was_visible=page;
                }
#else
                const auto vars=valid_controller ?
                    observe_menu_selection(current.mind,&read32,&read8) :
                    MenuSelectionVars{};
                const bool readonly_page=g_brightness_transition_disabled.load(
                    std::memory_order_acquire);
                const bool video_page=graphics_info_readonly_video_page(
                    approved(),in_menu,valid_controller,
                    current.normal,current.reflex,
                    vars.selection_valid,vars.selection,
                    vars.activate_valid,vars.activate,readonly_page);
                const bool brightness_panel=graphics_info_brightness_with_panel(
                    approved(),in_menu,valid_controller,current.normal,
                    current.reflex,vars.selection_valid,vars.selection,
                    g_language_table!=0,showed_information_in_menu);
                const bool video_prepare=
                    graphics_info_readonly_preparation(
                        approved(),in_menu,valid_controller,
                        current.normal,current.reflex,
                        vars.selection_valid,vars.selection,
                        vars.activate_valid,vars.activate,readonly_page) ||
                    graphics_info_video_preparation(
                        approved(),in_menu,valid_controller,
                        current.normal,current.reflex,
                        vars.selection_valid,vars.selection,
                        vars.activate_valid,vars.activate);
                if(video_page || video_prepare) {
                    if(video_page && !reported_static_script_preflight) {
                        reported_static_script_preflight=true;
                        trace_native_static_script_preflight(current_model);
                    }
                    reported_brightness_panel=false;
                    // The frozen information panel is not a choice
                    // list. Give the downstream material renderer one
                    // stable, valid focus ID, unrelated to the native
                    // V17 cursor. No game-state or input value is written.
                    g_info_focus_row.store(
                        video_page ? (readonly_page?1:vars.selection) : -1,
                        std::memory_order_release);
                    g_cached_video_text_in_brightness.store(
                        false,std::memory_order_release);
                    if(native_graphics_text_transition(true,g_language_table!=0)==
                       NativeGraphicsTextTransition::install_information_pointers) {
                        if(patch_text_table()) {
                            showed_information_in_menu=true;
                            g_logger(video_prepare ?
                              "RUNTIME-GRAPHICS-INFO: status=pre-video-information-text-installed scope=verified-normal3-preparation handles=379..384 obsolete-brightness385=hidden-if-verified" :
                              "RUNTIME-GRAPHICS-INFO: status=native-text-installed handles=379..384 obsolete-brightness385=hidden-if-verified scope=verified-video-page content=information pointer-swap=1");
                        } else if(!reported_text_issue &&
                                  DWORD(now-began)>7000u) {
                            reported_text_issue=true;
                            g_logger("RUNTIME-GRAPHICS-INFO: status=text-table-unverified no-replacement");
                        }
                    }
                    // normal-3 also renders the parent Options labels.
                    // It is not a trustworthy rendering identity for
                    // VIDEO. Do not drop any normal-3 image or glyph:
                    // the verified normal-5 page suppresses only its
                    // separately identified obsolete brightness meter.
                    g_video_prepare_active.store(
                        false,std::memory_order_release);
                    g_layout_active.store(
                        video_page && g_language_table!=0,
                        std::memory_order_release);
                    if(video_page && g_language_table &&
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
                } else if(brightness_panel) {
                    // Keep the already-installed information pointers and
                    // their 4:3 glyph reflow active. The native brightness
                    // slider uses its own untouched localization handle
                    // 385 and remains interactive. No V17 or script writes.
                    // Keep the last authenticated VIDEO focus (1..4) only
                    // for the static white palette, not navigation.
                    g_cached_video_text_in_brightness.store(
                        false,std::memory_order_release);
                    g_video_prepare_active.store(false,
                        std::memory_order_release);
                    g_layout_active.store(true,std::memory_order_release);
                    if(!reported_brightness_panel) {
                        reported_brightness_panel=true;
                        g_logger("RUNTIME-GRAPHICS-INFO: status=brightness-information-panel-retained native=18,5 original-slider=1 original-butterfly=1 v17-unmodified=1");
                    }
                } else {
                    reported_brightness_panel=false;
                    g_info_focus_row.store(-1,std::memory_order_release);
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
                    g_video_prepare_active.store(false,std::memory_order_release);
                    const bool menu_pinned=
                        !graphics_info_restore_pinned_text(
                            in_menu,
                            g_brightness_transition_disabled.load(
                                std::memory_order_acquire));
                    if(was_visible && g_language_table)
                        g_logger(menu_pinned ?
                            "RUNTIME-GRAPHICS-INFO: status=video-page-exit pointers=pinned-until-menu-unload" :
                            "RUNTIME-GRAPHICS-INFO: status=video-page-exit original-label-pointers-restored=1");
                    was_visible=false;
                    last_latch=~0u;
                    if(!menu_pinned &&
                       native_graphics_text_transition(false,g_language_table!=0)==
                          NativeGraphicsTextTransition::restore_original_pointers)
                        restore_text();
                }
#endif
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
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
void native_graphics_info_stabilize_video_cursor_at_flip() noexcept {
    // Called ONLY on the game's approved native flip thread, never the
    // asynchronous model monitor. The page has no selectable widgets.
    if(!g_protected.load(std::memory_order_acquire) ||
       !g_brightness_transition_disabled.load(std::memory_order_acquire) ||
       !is_native_menu())return;
    MenuAiModel model{};
    MenuAiState state{};
    if(!get_verified_menu_controller(model,state) ||
       state.normal!=5 || state.reflex!=0)return;
    std::uintptr_t cell=0,lower_cell=0,upper_cell=0;
    if(!locate_verified_video_selection_bounds(state.mind,
            &read32,&read8,cell,lower_cell,upper_cell) ||
       !writable_slot(cell) || !writable_slot(lower_cell) ||
       !writable_slot(upper_cell))return;
    std::uint32_t lower=0,upper=0;
    if(!read32(lower_cell,lower) || !read32(upper_cell,upper))return;
    // Source-clone uses valid [1,1] at AI execution time; fallback
    // only for unexpected original [0,3] ranges in the same submenu.
    // Bound the native selector *before its next input evaluation*:
    // V17 resetting after each processed key was not sufficient.
    if(standalone_video_upper_bound_needs_reset(lower,upper)) {
        const LONG prior=InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(upper_cell),1,3);
        if(prior==3)
            InterlockedCompareExchange(
                reinterpret_cast<volatile LONG*>(lower_cell),1,0);
        if(prior==3 && g_logger) {
            static std::atomic<bool> logged_bounds{false};
            if(!logged_bounds.exchange(true,std::memory_order_relaxed))
                g_logger("RUNTIME-STANDALONE-VIDEO: native-cursor-range-frozen=1..1 scope=verified-normal5 source=game-flip parent-options=untouched");
        }
    }
    // V70 is the reflex-0 navigation interpolation target. Original
    // code can animate the butterfly from V70 even after V17 was
    // clamped to its singleton at the previous frame boundary.
    // Scope to source-verified normal5/reflex0 and V70 scalar only.
    std::uintptr_t target_cell=0;
    if(locate_verified_video_navigation_target_cell(state.mind,
           &read32,&read8,target_cell) && writable_slot(target_cell)) {
        std::uint32_t target=0;
        if(read32(target_cell,target) && target<=5u && target!=1u) {
            const LONG prior=InterlockedCompareExchange(
                reinterpret_cast<volatile LONG*>(target_cell),1,
                static_cast<LONG>(target));
            if(prior==static_cast<LONG>(target) && g_logger) {
                static std::atomic<bool> logged_target{false};
                if(!logged_target.exchange(true,
                                          std::memory_order_relaxed))
                    g_logger("RUNTIME-STANDALONE-VIDEO: navigation-target-V70-frozen=1 source=reflex0-script2 scope=verified-normal5 no-other-menu-changes");
            }
        }
    }
    // The approved SNA reflex0/script2 uses V69 to index the
    // butterfly's interpolation position, then copies it to V70
    // and V17. The previous V70-only clamp did not prevent visual
    // Down navigation. Keep the *source* index in the same verified
    // singleton, only while normal5/reflex0 owns the VIDEO page.
    std::uintptr_t source_cell=0;
    if(locate_verified_video_navigation_source_cell(state.mind,
           &read32,&read8,source_cell) && writable_slot(source_cell)) {
        std::uint32_t source=0;
        if(read32(source_cell,source) && source<=5u && source!=1u) {
            const LONG previous=InterlockedCompareExchange(
                reinterpret_cast<volatile LONG*>(source_cell),1,
                static_cast<LONG>(source));
            if(previous==static_cast<LONG>(source) && g_logger) {
                static std::atomic<bool> once{false};
                if(!once.exchange(true,std::memory_order_relaxed))
                    g_logger("RUNTIME-STANDALONE-VIDEO: source-navigation-V69-frozen=1 scope=verified-normal5/reflex0 parent-options=untouched");
            }
        }
    }
    std::uint32_t selected=0;
    if(!read32(cell,selected) ||
       !standalone_video_cursor_needs_reset(selected))return;
    const auto previous=InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(cell),1,
        static_cast<LONG>(selected));
    if(previous==static_cast<LONG>(selected)) {
        static std::atomic<bool> logged{false};
        if(!logged.exchange(true,std::memory_order_relaxed) && g_logger)
            g_logger("RUNTIME-STANDALONE-VIDEO: obsolete-cursor-reset=1 scope=verified-normal5/reflex0 source=game-native-flip-only parent-options=untouched");
    }
}
#endif
int native_graphics_info_focus_row() noexcept {
    return g_layout_active.load(std::memory_order_acquire) ?
        g_info_focus_row.load(std::memory_order_acquire) : -1;
}
bool suppress_legacy_video_preparation_glyph(
    float xmin,float ymin,float xmax,float ymax) noexcept {
    const bool verified=
        g_video_prepare_active.load(std::memory_order_acquire) &&
        g_brightness_transition_disabled.load(std::memory_order_acquire);
    if(!legacy_resolution_preparation_glyph(
           verified,{xmin,ymin,xmax,ymax}))return false;
    const auto n=g_pre_video_old_glyphs_suppressed.fetch_add(
        1,std::memory_order_relaxed);
    if(n==0u && g_logger)
        g_logger("RUNTIME-GRAPHICS-INFO: status=pre-video-legacy-glyph-hidden scope=verified-normal3-resolutions only=old-font-bands video-title=untouched butterfly=untouched");
    return true;
}
bool suppress_readonly_video_brightness_meter(unsigned producer,
    float xmin,float ymin,float xmax,float ymax) noexcept {
    // Only while the approved game is in the stable, noninteractive
    // VIDEO information page and exact native brightness entry
    // suppression has been committed. The original 3D butterfly is
    // independent of this two-quad ScreenImage producer.
    const bool verified=
        (g_layout_active.load(std::memory_order_acquire) ||
         g_video_prepare_active.load(std::memory_order_acquire)) &&
        g_brightness_transition_disabled.load(std::memory_order_acquire);
    if(!readonly_video_brightness_meter_quad(verified,producer,
          {xmin,ymin,xmax,ymax}))return false;
    const unsigned n=g_brightness_meter_quads_suppressed.fetch_add(
        1,std::memory_order_relaxed);
    if(n==0u && g_logger)
        g_logger("RUNTIME-GRAPHICS-INFO: status=obsolete-brightness-meter-hidden route=verified-screen-image split=variable between=70..80 prep-or-video=1 butterfly=untouched");
    return true;
}
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
#if defined(RAYMAN2FIX_STANDALONE_VIDEO_PAGE) && RAYMAN2FIX_STANDALONE_VIDEO_PAGE
    standalone_video_page_configure(logger,output_width,output_height,classic_4_3);
    start_pause_video_observer(logger); // separate verified model; read-only
#endif
    logger("RUNTIME-GRAPHICS-INFO: status=started experimental=1 no-file-patch=1 fail-closed=1 native-menu=1");
    HANDLE t=CreateThread(nullptr,0,&worker,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    else logger("RUNTIME-GRAPHICS-INFO: status=no-worker no-changes");
}
} // namespace r2fix
