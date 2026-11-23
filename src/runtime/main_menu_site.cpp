// Main-menu website replacement for the owner's verified PC executable.
// Only the exact localized handle 362 and the original SHELL32 import
// are modified in private game-process memory. No game asset changes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include "rayman2fix/main_menu_site_policy.hpp"
#include "rayman2fix/main_menu_site.hpp"
#include "rayman2fix/main_menu_site_script_policy.hpp"
#include "rayman2fix/graphics_menu_controller_trace.hpp"
#include "rayman2fix/standalone_video_cursor_policy.hpp"
#ifndef RAYMAN2FIX_VERSION
#define RAYMAN2FIX_VERSION "unknown"
#endif
#ifndef RAYMAN2FIX_BUILD_COMMIT
#define RAYMAN2FIX_BUILD_COMMIT "unknown"
#endif
namespace r2fix {
namespace {
constexpr std::uintptr_t kShellExecuteIatRva=0x0009C220u;
// Unique original ShellExecuteA indirect CALL in the 1D7000-byte
// approved executable: VA 0x587261 / return RVA 0x187267.
constexpr std::uintptr_t kSiteOpenReturnRva=0x00187267u;
constexpr unsigned kWebsiteHandle=362u;
constexpr const char* kProjectUrl=
    "https://github.com/legluondunet/Rayman2Fix";
using ShellExecuteFn=HINSTANCE (WINAPI*)(
    HWND,LPCSTR,LPCSTR,LPCSTR,LPCSTR,INT);
std::atomic<ShellExecuteFn> g_original_shell{nullptr};
std::atomic<bool> g_active{false};
std::atomic<bool> g_input_worker_started{false};
std::atomic<unsigned> g_site_install_attempts{0};
std::uintptr_t g_patched_table=0;
std::uintptr_t g_original_text=0;
std::uintptr_t g_site_script_slot=0;
std::uintptr_t g_site_script_original=0;
NativeMenuScriptNode* g_site_script_copy=nullptr;
// Published script copies remain allocated for process lifetime: an
// already executing AI interpreter may still hold the old pointer.
SiteLogger g_logger=nullptr;
char g_site_label[96]{};
bool read_site_byte(std::uintptr_t address,std::uint8_t& out) noexcept {
    SIZE_T done=0;
    return address>=0x10000u &&
           ReadProcessMemory(GetCurrentProcess(),
               reinterpret_cast<const void*>(address),
               &out,1u,&done)!=FALSE && done==1u;
}
bool read_exact(std::uintptr_t address,void* out,std::size_t bytes) noexcept {
    SIZE_T done=0;
    return address>=0x10000u &&
           ReadProcessMemory(GetCurrentProcess(),
              reinterpret_cast<const void*>(address),
              out,bytes,&done)!=FALSE && done==bytes;
}
bool writable(std::uintptr_t address) noexcept {
    MEMORY_BASIC_INFORMATION m{};
    if(!address || address%4u ||
       VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))!=sizeof(m) ||
       m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))
        return false;
    const DWORD flags=m.Protect&0xFFu;
    return flags==PAGE_READWRITE || flags==PAGE_WRITECOPY ||
           flags==PAGE_EXECUTE_READWRITE || flags==PAGE_EXECUTE_WRITECOPY;
}
bool replace_iat(void* expected,void* replacement) noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto slot=base+kShellExecuteIatRva;
    if(!base || !expected || !replacement)return false;
    std::uint32_t existing=0;
    if(!read_exact(slot,&existing,4u) ||
       existing!=reinterpret_cast<std::uintptr_t>(expected))return false;
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(slot),4u,
                       PAGE_READWRITE,&old))return false;
    void* found=InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot),replacement,expected);
    DWORD ignored=0;
    VirtualProtect(reinterpret_cast<void*>(slot),4u,old,&ignored);
    return found==expected || found==replacement;
}
bool restore_iat(void* expected,void* replacement) noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto slot=base+kShellExecuteIatRva;
    if(!base || !expected || !replacement)return false;
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(slot),4u,
                       PAGE_READWRITE,&old))return false;
    void* found=InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot),replacement,expected);
    DWORD ignored=0;
    VirtualProtect(reinterpret_cast<void*>(slot),4u,old,&ignored);
    return found==expected || found==replacement;
}
__declspec(noinline) HINSTANCE WINAPI website_shell_execute(
    HWND owner,LPCSTR operation,LPCSTR file,LPCSTR parameters,
    LPCSTR directory,INT show) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const bool authenticated_site_action=
        base!=0 && caller==base+kSiteOpenReturnRva;
    const auto original=g_original_shell.load(std::memory_order_acquire);
    if(!original)return reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
    // The old hook required a 255-byte readable buffer AND one of four
    // historical URLs; both may reject the game's authentic site action.
    // The approved x86 EXE has a SINGLE original ShellExecuteA callsite.
    // Redirect by that exact native return RVA instead, never by a URL
    // prefix or guessed text. All unrelated calls forward unchanged.
    if(g_active.load(std::memory_order_acquire) &&
       authenticated_site_action) {
        if(g_logger)g_logger(
            "RUNTIME-MENU-SITE: original-site-action=1 caller-rva=0x187267 redirect=github default-browser=1");
        const auto result=original(owner,"open",kProjectUrl,nullptr,
                                   nullptr,SW_SHOWNORMAL);
        if(reinterpret_cast<std::intptr_t>(result)<=32 && g_logger)
            g_logger("RUNTIME-MENU-SITE: browser-open-failed result-leq-32");
        return result;
    }
    return original(owner,operation,file,parameters,directory,show);
}
} // namespace
// The original Rayman2 website action (proc369) was never observed to
// reach ShellExecuteA in the owner's logs, even after replacing its
// original URL literal. Reuse native main-menu selection and OS input
// rather than relying on obsolete browser integration. This worker
// never captures mouse/keyboard input, and only the verified HOME MENU
// site row can open the fixed project URL.
DWORD WINAPI site_input_worker(void*) noexcept {
    bool last_enter=false,last_click=false;
    DWORD last_open=0;
    while(true) {
        const bool enter=(GetAsyncKeyState(VK_RETURN)&0x8000)!=0;
        const bool click=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
        const bool pressed_enter=enter&&!last_enter;
        const bool pressed_click=click&&!last_click;
        last_enter=enter;
        last_click=click;
        if(!g_active.load(std::memory_order_acquire) ||
           (!pressed_enter && !pressed_click)) {
            Sleep(16);
            continue;
        }
        const auto window=GetForegroundWindow();
        DWORD pid=0;
        if(!window || !GetWindowThreadProcessId(window,&pid) ||
           pid!=GetCurrentProcessId()) {Sleep(16);continue;}
        MenuAiModel model{};
        MenuAiState state{};
        if(!get_verified_menu_controller(model,state) ||
           state.normal!=11 || state.reflex!=0) {
            Sleep(16);continue;
        }
        std::uintptr_t cell=0;
        std::uint32_t choice=0;
        if(!locate_verified_video_cursor_cell(state.mind,
              &read_exact,&read_site_byte,cell) ||
           !read_exact(cell,&choice,sizeof(choice))) {
            Sleep(16);continue;
        }
        bool click_hit=false;
        if(pressed_click) {
            RECT client{};
            POINT cursor{};
            if(GetClientRect(window,&client) &&
               GetCursorPos(&cursor) &&
               ScreenToClient(window,&cursor)) {
                const int w=client.right-client.left;
                const int h=client.bottom-client.top;
                if(w>0 && h>0) {
                    // Verifiable Rayman 2 main-menu site caption band,
                    // not a global mouse shortcut or generic button.
                    const double x=double(cursor.x-client.left)/double(w);
                    const double y=double(cursor.y-client.top)/double(h);
                    click_hit=x>=0.26 && x<=0.74 &&
                              y>=0.625 && y<=0.72;
                }
            }
        }
        if(!((pressed_enter && choice==4u) ||
             (pressed_click && click_hit))) {
            Sleep(16);continue;
        }
        const DWORD now=GetTickCount();
        if(DWORD(now-last_open)<1500u) {Sleep(16);continue;}
        last_open=now;
        const auto original=g_original_shell.load(std::memory_order_acquire);
        if(!original) {Sleep(16);continue;}
        if(g_logger)
            g_logger("RUNTIME-MENU-SITE: site-user-action=1 source=verified-normal11 input=enter-or-caption-click url=github");
        const auto result=original(nullptr,"open",kProjectUrl,
                                   nullptr,nullptr,SW_SHOWNORMAL);
        if(g_logger)
            g_logger(reinterpret_cast<std::intptr_t>(result)>32 ?
                "RUNTIME-MENU-SITE: browser-launch-dispatched=1":
                "RUNTIME-MENU-SITE: browser-launch-failed=1");
        Sleep(16);
    }
    return 0;
}
void start_site_input_worker() noexcept {
    if(g_input_worker_started.exchange(true))return;
    HANDLE thread=CreateThread(nullptr,0,&site_input_worker,
                               nullptr,0,nullptr);
    if(thread) {
        CloseHandle(thread);
        if(g_logger)g_logger(
            "RUNTIME-MENU-SITE: input-monitor=started user-action-only=1 site-row=verified-normal11/selection4");
    } else if(g_logger)
        g_logger("RUNTIME-MENU-SITE: input-monitor=thread-failed");
}
bool install_site_script_literal() noexcept {
    MenuAiModel model{};
    MenuAiState state{};
    if(!get_verified_menu_controller(model,state) || !model.valid())
        return false;
    const auto entry=model.normal_entries+11u*12u;
    std::uint32_t scripts=0;
    std::uint8_t count=0;
    if(!read_exact(entry,&scripts,4u) ||
       !read_exact(entry+8u,&count,1u) || count!=1u ||
       !writable(scripts))return false;
    std::uint32_t original=0;
    if(!read_exact(scripts,&original,4u) || original<0x10000u)
        return false;
    constexpr SIZE_T bytes=kSiteScriptNodes*sizeof(NativeMenuScriptNode);
    auto* copy=static_cast<NativeMenuScriptNode*>(
        VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!copy)return false;
    if(!read_exact(original,copy,bytes)) {
        VirtualFree(copy,0,MEM_RELEASE);
        return false;
    }
    const std::uintptr_t original_url=copy[kSiteUrlNode].parameter;
    char text[sizeof("http://www.rayman2.com/")]{};
    if(!read_exact(original_url,text,sizeof(text)) ||
       std::strcmp(text,"http://www.rayman2.com/")!=0 ||
       !redirect_menu_site_script(copy,kSiteScriptNodes,original_url,
                                  reinterpret_cast<std::uintptr_t>(kProjectUrl))) {
        VirtualFree(copy,0,MEM_RELEASE);
        return false;
    }
    if(InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(std::uintptr_t(scripts)),
            copy,reinterpret_cast<void*>(std::uintptr_t(original)))!=
       reinterpret_cast<void*>(std::uintptr_t(original))) {
        VirtualFree(copy,0,MEM_RELEASE);
        return false;
    }
    g_site_script_slot=scripts;
    g_site_script_original=original;
    g_site_script_copy=copy;
    if(g_logger)
        g_logger("RUNTIME-MENU-SITE: verified-original-site-script=1 normal11 script0 node1835=proc369 node1836=github-literal source=private-copy");
    return true;
}
void restore_site_script_literal() noexcept {
    if(g_site_script_slot && g_site_script_original &&
       g_site_script_copy && writable(g_site_script_slot))
        InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(g_site_script_slot),
            reinterpret_cast<void*>(g_site_script_original),
            g_site_script_copy);
    g_site_script_slot=0;
    g_site_script_original=0;
    // Do not VirtualFree: the private script might still be in-flight.
    g_site_script_copy=nullptr;
}
bool install_main_menu_site(std::uintptr_t table,SiteLogger logger) noexcept {
    if(g_active.load(std::memory_order_acquire))return true;
    const unsigned attempt=++g_site_install_attempts;
    const auto refuse=[&](const char* reason) noexcept -> bool {
        if(g_logger && (attempt<=3u || attempt%40u==0u)) {
            char line[200]{};
            std::snprintf(line,sizeof(line),
                "RUNTIME-MENU-SITE: install-refused reason=%s attempt=%u",
                reason,attempt);
            g_logger(line);
        }
        return false;
    };
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_logger=logger;
    if(!base || !table || !logger || !writable(table+4u*kWebsiteHandle))
        return refuse("text-table-unavailable");
    const HMODULE shell=GetModuleHandleW(L"shell32.dll");
    const auto original=reinterpret_cast<ShellExecuteFn>(
        shell?GetProcAddress(shell,"ShellExecuteA"):nullptr);
    if(!original)return refuse("shell32-export-unavailable");
    const auto slot=table+4u*kWebsiteHandle;
    std::uint32_t source=0;
    char original_label[48]{};
    if(!read_exact(slot,&source,4u) || source<0x10000u ||
       !read_exact(source,original_label,sizeof(original_label)-1u) ||
       !approved_main_menu_site_text(original_label)) {
        return refuse("original-menu-label-mismatch");
    }
    // Original native text parser requires /C: for menu caption.
    std::snprintf(g_site_label,sizeof(g_site_label),
        "/C:Rayman2Fix");
    g_logger=logger;
    g_original_shell.store(original,std::memory_order_release);
    // Source-owned URL replacement also works when the original game
    // never reaches its imported ShellExecute call before menu teardown.
    if(!install_site_script_literal()) {
        g_original_shell.store(nullptr,std::memory_order_release);
        return refuse("site-script-validation");
    }
    if(!replace_iat(reinterpret_cast<void*>(original),
                    reinterpret_cast<void*>(&website_shell_execute))) {
        restore_site_script_literal();
        g_original_shell.store(nullptr,std::memory_order_release);
        return refuse("shell32-import-swap"); // fail closed
    }
    const auto found=InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot),
        static_cast<void*>(g_site_label),
        reinterpret_cast<void*>(std::uintptr_t(source)));
    if(found!=reinterpret_cast<void*>(std::uintptr_t(source))) {
        restore_iat(reinterpret_cast<void*>(&website_shell_execute),
                    reinterpret_cast<void*>(original));
        restore_site_script_literal();
        g_original_shell.store(nullptr,std::memory_order_release);
        return refuse("label-pointer-compare-exchange");
    }
    g_patched_table=table;
    g_original_text=source;
    g_active.store(true,std::memory_order_release);
    start_site_input_worker();
    logger("RUNTIME-MENU-SITE: status=installed handle=362 locales=5 label=Rayman2Fix url=github verified-native-return-rva=0x187267 browser=ShellExecuteA-redirect source-script=verified-url-literal");
    return true;
}
void restore_main_menu_site() noexcept {
    g_active.store(false,std::memory_order_release);
    restore_site_script_literal();
    if(g_patched_table && g_original_text &&
       writable(g_patched_table+4u*kWebsiteHandle)) {
        InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(g_patched_table+
                                               4u*kWebsiteHandle),
            reinterpret_cast<void*>(g_original_text),
            static_cast<void*>(g_site_label));
    }
    g_patched_table=0;
    g_original_text=0;
    const auto original=g_original_shell.load(std::memory_order_acquire);
    // Keep only the verified import redirection installed for the
    // process lifetime. With g_active=false it forwards every call
    // unchanged. Reinstalling in a newly loaded MENU therefore never
    // races the original ShellExecute IAT restoration.
    (void)original;
}
} // namespace r2fix
