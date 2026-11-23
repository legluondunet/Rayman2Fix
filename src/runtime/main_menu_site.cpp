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
std::uintptr_t g_patched_table=0;
std::uintptr_t g_original_text=0;
SiteLogger g_logger=nullptr;
char g_site_label[96]{};
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
    return found==expected;
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
bool install_main_menu_site(std::uintptr_t table,SiteLogger logger) noexcept {
    if(g_active.load(std::memory_order_acquire))return true;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!base || !table || !logger || !writable(table+4u*kWebsiteHandle))
        return false;
    const HMODULE shell=GetModuleHandleW(L"shell32.dll");
    const auto original=reinterpret_cast<ShellExecuteFn>(
        shell?GetProcAddress(shell,"ShellExecuteA"):nullptr);
    if(!original)return false;
    const auto slot=table+4u*kWebsiteHandle;
    std::uint32_t source=0;
    char original_label[48]{};
    if(!read_exact(slot,&source,4u) || source<0x10000u ||
       !read_exact(source,original_label,sizeof(original_label)-1u) ||
       !approved_main_menu_site_text(original_label)) {
        return false;
    }
    // Original native text parser requires /C: for menu caption.
    std::snprintf(g_site_label,sizeof(g_site_label),
        "/C:Rayman2Fix");
    g_logger=logger;
    g_original_shell.store(original,std::memory_order_release);
    if(!replace_iat(reinterpret_cast<void*>(original),
                    reinterpret_cast<void*>(&website_shell_execute))) {
        g_original_shell.store(nullptr,std::memory_order_release);
        return false; // never change visible link without working action
    }
    const auto found=InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot),
        static_cast<void*>(g_site_label),
        reinterpret_cast<void*>(std::uintptr_t(source)));
    if(found!=reinterpret_cast<void*>(std::uintptr_t(source))) {
        restore_iat(reinterpret_cast<void*>(&website_shell_execute),
                    reinterpret_cast<void*>(original));
        g_original_shell.store(nullptr,std::memory_order_release);
        return false;
    }
    g_patched_table=table;
    g_original_text=source;
    g_active.store(true,std::memory_order_release);
    logger("RUNTIME-MENU-SITE: status=installed handle=362 locales=5 label=Rayman2Fix url=github verified-native-return-rva=0x187267 browser=ShellExecuteA-redirect");
    return true;
}
void restore_main_menu_site() noexcept {
    g_active.store(false,std::memory_order_release);
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
    if(original) {
        restore_iat(reinterpret_cast<void*>(&website_shell_execute),
                    reinterpret_cast<void*>(original));
        g_original_shell.store(nullptr,std::memory_order_release);
    }
}
} // namespace r2fix
