#include "rayman2fix/video_navigation_input.hpp"
#include "rayman2fix/video_navigation_input_policy.hpp"
#include "rayman2fix/standalone_video_page.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifdef RAYMAN2FIX_BUNDLED_SDL3
#include "rayman2fix/gamepad_menu_bridge.hpp"
std::uint32_t r2fix_sdl_menu_actions(bool video) noexcept;
bool r2fix_sdl_controller_page() noexcept;
void r2fix_sdl_note_pause_delivery() noexcept;
#endif

#if !defined(_M_IX86)
#error This hook uses exact approved Rayman 2 Windows x86 instructions
#endif

namespace r2fix {
namespace {
// The original executable (image base 0x400000) calls
// IDirectInputDeviceA::GetDeviceState(0x100, EBX) twice, through
// 0x49669C and 0x4966B0. The calls converge at 0x4966B3.
// Intercept AFTER the native COM call, BEFORE the game's keyboard
// navigation interpretation. No keyboard driver, COM object, IAT,
// system-wide key state is patched. Scoped SDL3 input can be OR-ed into this buffer.
constexpr std::uintptr_t kPostKeyboardStateRva=0x966B3u;
constexpr std::uintptr_t kContinueNormalRva=0x966BAu;
constexpr std::uintptr_t kContinueGreaterRva=0x966C5u;
constexpr DWORD kApprovedExeTimestamp=0x3807B68Bu;
constexpr DWORD kApprovedImageSize=0x001D7000u;
constexpr unsigned char kExpected[7]{
    0x3Du,0x0Cu,0x00u,0x07u,0x80u,0x7Fu,0x0Bu
};
VideoNavigationLogger g_log=nullptr;
std::uintptr_t g_continue_normal=0;
std::uintptr_t g_continue_greater=0;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_ever_filtered{false};

bool writable_256(void* source) noexcept {
    if(!source)return false;
    MEMORY_BASIC_INFORMATION page{};
    if(VirtualQuery(source,&page,sizeof(page))!=sizeof(page) ||
       page.State!=MEM_COMMIT ||
       (page.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD mode=page.Protect&0xFFu;
    if(mode!=PAGE_READWRITE && mode!=PAGE_WRITECOPY &&
       mode!=PAGE_EXECUTE_READWRITE &&
       mode!=PAGE_EXECUTE_WRITECOPY)return false;
    const std::uintptr_t base=
        reinterpret_cast<std::uintptr_t>(page.BaseAddress);
    const std::uintptr_t ptr=reinterpret_cast<std::uintptr_t>(source);
    return ptr>=base &&
        page.RegionSize>=kVideoKeyboardBytes &&
        ptr-base<=page.RegionSize-kVideoKeyboardBytes;
}
bool foreground_game() noexcept {
    const HWND foreground=GetForegroundWindow();
    DWORD pid=0;
    return foreground &&
        GetWindowThreadProcessId(foreground,&pid) &&
        pid==GetCurrentProcessId();
}
void __cdecl filter_approved_video_keys(void* keyboard,
                                        std::int32_t result) noexcept {
    bool menu=false,pause=false;
    const bool video=standalone_video_page_owns_navigation(menu,pause);
    if(!writable_256(keyboard) || result!=0)return;
    const bool foreground=foreground_game();
#ifdef RAYMAN2FIX_BUNDLED_SDL3
    const bool controller=::r2fix_sdl_controller_page();
    const auto actions=::r2fix_sdl_menu_actions(video);
    const auto merged=merge_pad_menu_keys(controller ? PadContext::controller_info :
        video ? PadContext::video : PadContext::menu,
        foreground,result,kVideoKeyboardBytes,
        static_cast<std::uint8_t*>(keyboard),actions);
    if(controller && foreground) {
        // Read-only information: retain native Escape, block calibration
        // activation through the verified keyboard state only.
        const auto blocked=suppress_controller_info_keys(controller,foreground,result,
            kVideoKeyboardBytes,static_cast<std::uint8_t*>(keyboard));
        static std::atomic<bool> blocked_reported{false},back_reported{false};
        if(blocked && !blocked_reported.exchange(true) && g_log)
            g_log("RUNTIME-CONTROLLER-MENU: keyboard-calibration-blocked=1 native-Escape-preserved=1");
        if(merged && !back_reported.exchange(true) && g_log)
            g_log("RUNTIME-CONTROLLER-MENU: SDL3-back-delivered=1 native-DirectInput256");
        return;
    }
    if(merged && !video && (actions & pad_action_bit(PadAction::cancel)))
        ::r2fix_sdl_note_pause_delivery();
    // Bounded proof of actual native buffer delivery, not just SDL sampling.
    static std::atomic<bool> menu_reported{false},video_reported{false};
    if(merged && !(video ? video_reported : menu_reported).exchange(true) && g_log)
        g_log(video ?
            "RUNTIME-SDL3: native-route=VIDEO-back merged=1 scope=verified-DirectInput256" :
            "RUNTIME-SDL3: native-route=MENU merged=1 scope=verified-DirectInput256");
#endif
    // VIDEO suppression is applied after the merge. Physical Escape and
    // native HRESULT semantics are preserved, as before.
    if(!video)return;
    const unsigned consumed=suppress_video_navigation_keys(
        menu,pause,foreground,result,kVideoKeyboardBytes,
        static_cast<std::uint8_t*>(keyboard));
    if(consumed && !g_ever_filtered.exchange(true) && g_log)
        g_log("RUNTIME-VIDEO-INPUT: source=retail-x86-DirectInput-GetDeviceState256 navigation-arrows-suppressed=1 scope=authenticated-MENU-or-ESC-VIDEO preserved=Escape,Enter,other-keys game-input=untouched");
}

// Naked hook (MSVC x86): all native general registers and flags are
// preserved around the callback, including GetDeviceState's HRESULT.
// Replay the two original instructions as exact x86 branches:
//   CMP EAX,0x8007000C; JG 0x4966C5
//   otherwise continue at 0x4966BA.
// DirectInput original error handling remains byte-for-byte equivalent.
__declspec(naked) void post_native_keyboard_state() {
    __asm {
        pushfd
        pushad
        push eax
        push ebx
        call filter_approved_video_keys
        add esp, 8
        popad
        popfd
        cmp eax, 0x8007000C
        jg result_greater
        jmp dword ptr [g_continue_normal]
    result_greater:
        jmp dword ptr [g_continue_greater]
    }
}
} // namespace

bool install_video_navigation_input(VideoNavigationLogger logger) noexcept {
    g_log=logger;
    if(g_installed.load(std::memory_order_acquire))return true;
    const auto image=reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    if(!image)return false;
    const auto* mz=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if(mz->e_magic!=IMAGE_DOS_SIGNATURE ||
       mz->e_lfanew<sizeof(IMAGE_DOS_HEADER) ||
       mz->e_lfanew>0x400u)return false;
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        image+std::uintptr_t(mz->e_lfanew));
    if(pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->FileHeader.TimeDateStamp!=kApprovedExeTimestamp ||
       pe->OptionalHeader.SizeOfImage!=kApprovedImageSize ||
       pe->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;
    auto* site=reinterpret_cast<BYTE*>(image+kPostKeyboardStateRva);
    if(std::memcmp(site,kExpected,sizeof(kExpected))!=0) {
        if(g_log)g_log(
            "RUNTIME-VIDEO-INPUT: hook-refused=keyboard-site-signature source=approved-x86 no-input-modification=1");
        return false;
    }
    g_continue_normal=image+kContinueNormalRva;
    g_continue_greater=image+kContinueGreaterRva;
    DWORD protection=0;
    if(!VirtualProtect(site,sizeof(kExpected),
                       PAGE_EXECUTE_READWRITE,&protection))return false;
    unsigned char patch[7]{0xE9u,0,0,0,0,0x90u,0x90u};
    const auto after=reinterpret_cast<std::uintptr_t>(site)+5u;
    const auto dest=reinterpret_cast<std::uintptr_t>(
        &post_native_keyboard_state);
    const std::uint32_t displacement=
        static_cast<std::uint32_t>(dest-after);
    std::memcpy(patch+1u,&displacement,4u);
    // Only approved 7 source bytes; never touch COM/DirectInput.dll.
    std::memcpy(site,patch,sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(),site,sizeof(patch));
    DWORD ignored=0;
    VirtualProtect(site,sizeof(kExpected),protection,&ignored);
    g_installed.store(true,std::memory_order_release);
    if(g_log)g_log(
        "RUNTIME-VIDEO-INPUT: status=installed source=approved-GetDeviceState256-return-rva=0x966B3 arrows=Up,Down,Left,Right active=verified-MENU5-or-Pause9-only Escape=preserved no-system-input-hook=1");
    return true;
}
} // namespace r2fix
