// Set the game's taskbar icon from WITHIN rayman2.exe.
// Handles for icons created in the launcher cannot be assumed to work when
// passed to another process's HWND under Wine. No EXE files are modified.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "rayman2fix/game_window_icon.hpp"

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace r2fix {
namespace {
using Logger=void (*)(const char*);
std::atomic<bool> g_worker_started{false};
Logger g_log=nullptr;

void report(const char* msg) {
    if(g_log)g_log(msg);
}
struct GroupName {
    bool found=false;
    bool integer=false;
    WORD id=0;
    std::wstring text;
    LPCWSTR value() const {
        return integer ? MAKEINTRESOURCEW(id) : text.c_str();
    }
};
BOOL CALLBACK first_group(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR data) {
    auto& group=*reinterpret_cast<GroupName*>(data);
    group.found=true;
    group.integer=IS_INTRESOURCE(name)!=0;
    if(group.integer)
        group.id=static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
    else
        group.text=name;
    return FALSE;
}
unsigned word_at(const BYTE* data) {
    return unsigned(data[0])|(unsigned(data[1])<<8);
}
struct Icons {
    HICON big=nullptr;
    HICON small=nullptr;
};
Icons load_self_icons() {
    Icons result;
    const HMODULE game=GetModuleHandleW(nullptr);
    if(!game) {
        report("RUNTIME-ICON: stage=no-game-module");
        return result;
    }
    GroupName group;
    EnumResourceNamesW(game,MAKEINTRESOURCEW(14),first_group,
                       reinterpret_cast<LONG_PTR>(&group));
    if(!group.found) {
        report("RUNTIME-ICON: stage=no-group-icon-in-rayman2.exe");
        return result;
    }
    result.big=static_cast<HICON>(LoadImageW(
        game,group.value(),IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),0));
    result.small=static_cast<HICON>(LoadImageW(
        game,group.value(),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0));
    const HRSRC group_resource=FindResourceW(
        game,group.value(),MAKEINTRESOURCEW(14));
    const DWORD group_bytes=group_resource ?
        SizeofResource(game,group_resource) : 0;
    const HGLOBAL handle=group_resource ?
        LoadResource(game,group_resource) : nullptr;
    const BYTE* group_data=handle ?
        static_cast<const BYTE*>(LockResource(handle)) : nullptr;
    const bool valid=group_data && group_bytes>=20u &&
        group_bytes<=65536u && word_at(group_data)==0 &&
        word_at(group_data+2)==1 && word_at(group_data+4)>0 &&
        word_at(group_data+4)<=128u &&
        6u+14u*word_at(group_data+4)<=group_bytes;
    const auto from_id=[&](unsigned id,int width,int height)->HICON {
        if(!id)return nullptr;
        HRSRC entry=FindResourceW(
            game,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(3));
        if(!entry)return nullptr;
        const DWORD size=SizeofResource(game,entry);
        if(size<40 || size>2*1024*1024)return nullptr;
        HGLOBAL image=LoadResource(game,entry);
        auto* bits=image ? static_cast<PBYTE>(LockResource(image)) : nullptr;
        return bits ? CreateIconFromResourceEx(
            bits,size,TRUE,0x00030000,width,height,LR_DEFAULTCOLOR)
            : nullptr;
    };
    const auto from_directory=[&](int width,int height)->HICON {
        if(!valid)return nullptr;
        const int id=LookupIconIdFromDirectoryEx(
            const_cast<PBYTE>(group_data),TRUE,width,height,LR_DEFAULTCOLOR);
        HICON found=from_id(id>0 ? static_cast<unsigned>(id) : 0u,width,height);
        if(found)return found;
        for(unsigned i=0;i<word_at(group_data+4);++i) {
            const unsigned other=word_at(group_data+6u+14u*i+12u);
            if(other!=static_cast<unsigned>(id)) {
                found=from_id(other,width,height);
                if(found)return found;
            }
        }
        return nullptr;
    };
    if(!result.big)
        result.big=from_directory(GetSystemMetrics(SM_CXICON),
                                   GetSystemMetrics(SM_CYICON));
    if(!result.small)
        result.small=from_directory(GetSystemMetrics(SM_CXSMICON),
                                     GetSystemMetrics(SM_CYSMICON));
    if(!result.big && result.small)result.big=CopyIcon(result.small);
    if(!result.small && result.big)result.small=CopyIcon(result.big);
    char line[240]{};
    std::snprintf(line,sizeof(line),
        "RUNTIME-ICON: stage=load-self source=rayman2.exe "
        "group=1 group_valid=%u big=%u small=%u owner=game-process",
        unsigned(valid),unsigned(result.big!=nullptr),
        unsigned(result.small!=nullptr));
    report(line);
    return result;
}
struct Candidate {
    DWORD pid=0;
    HWND hwnd=nullptr;
    std::uint64_t score=0;
};
BOOL CALLBACK find_window(HWND hwnd, LPARAM context) {
    auto& result=*reinterpret_cast<Candidate*>(context);
    DWORD pid=0;
    GetWindowThreadProcessId(hwnd,&pid);
    if(pid!=result.pid || !IsWindowVisible(hwnd) ||
       GetWindow(hwnd,GW_OWNER) ||
       (GetWindowLongPtrW(hwnd,GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
        return TRUE;
    RECT rect{};
    if(!GetClientRect(hwnd,&rect))return TRUE;
    const auto width=rect.right-rect.left;
    const auto height=rect.bottom-rect.top;
    if(width<320 || height<200)return TRUE;
    wchar_t klass[100]{};
    GetClassNameW(hwnd,klass,100);
    const bool known=std::wstring(klass)==L"Rayman2" ||
                     std::wstring(klass)==L"GlideWrapper";
    const std::uint64_t score=
        static_cast<std::uint64_t>(width)*height+
        (known ? (1ull<<48) : 0ull);
    if(score>result.score) {
        result.hwnd=hwnd;
        result.score=score;
    }
    return TRUE;
}
struct Apply {
    bool sent=false;
    bool readback=false;
    bool class_big=false;
    bool class_small=false;
    DWORD win32=0;
};
Apply apply_to_window(HWND hwnd,const Icons& icon) {
    Apply result;
    // These HICONs belong to the SAME PROCESS as the game window. Wine
    // no longer needs to interpret opaque HICON handles from Rayman2Fix.exe.
    // Set the window class fallback even if its UI thread is busy.
    SetLastError(0);
    const LONG_PTR old_big=SetClassLongPtrW(
        hwnd,GCLP_HICON,reinterpret_cast<LONG_PTR>(icon.big));
    result.class_big=(old_big!=0 || GetLastError()==0);
    SetLastError(0);
    const LONG_PTR old_small=SetClassLongPtrW(
        hwnd,GCLP_HICONSM,reinterpret_cast<LONG_PTR>(icon.small));
    result.class_small=(old_small!=0 || GetLastError()==0);

    DWORD_PTR previous_big=0,previous_small=0;
    const BOOL big=SendMessageTimeoutW(
        hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon.big),
        SMTO_ABORTIFHUNG|SMTO_BLOCK,200,&previous_big);
    const DWORD big_error=big ? 0 : GetLastError();
    const BOOL small=SendMessageTimeoutW(
        hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon.small),
        SMTO_ABORTIFHUNG|SMTO_BLOCK,200,&previous_small);
    result.win32=!small ? GetLastError() : big_error;
    result.sent=big && small;
    // If the native UI event queue does not process a synchronous WM_SETICON
    // under Wine, request the same update asynchronously as best effort.
    if(!big)PostMessageW(hwnd,WM_SETICON,ICON_BIG,
                         reinterpret_cast<LPARAM>(icon.big));
    if(!small)PostMessageW(hwnd,WM_SETICON,ICON_SMALL,
                           reinterpret_cast<LPARAM>(icon.small));
    DWORD_PTR actual_big=0,actual_small=0;
    const BOOL read_big=SendMessageTimeoutW(
        hwnd,WM_GETICON,ICON_BIG,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,
        100,&actual_big);
    const BOOL read_small=SendMessageTimeoutW(
        hwnd,WM_GETICON,ICON_SMALL,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,
        100,&actual_small);
    result.readback=read_big && read_small &&
        actual_big==reinterpret_cast<DWORD_PTR>(icon.big) &&
        actual_small==reinterpret_cast<DWORD_PTR>(icon.small);
    return result;
}
DWORD WINAPI icon_worker(void*) {
    const Icons icon=load_self_icons();
    if(!icon.big || !icon.small) {
        report("RUNTIME-ICON: stage=icon-unavailable; game-launch-unmodified");
        return 0;
    }
    // Icon objects remain owned by the game process until process exit.
    // Do not destroy an icon that is still referenced by a window/class.
    DWORD last_report=0;
    for(unsigned iteration=0; iteration<60; ++iteration) {
        Candidate target;
        target.pid=GetCurrentProcessId();
        EnumWindows(find_window,reinterpret_cast<LPARAM>(&target));
        if(target.hwnd) {
            const auto status=apply_to_window(target.hwnd,icon);
            if(iteration==0 || iteration%5==0 ||
               status.readback || (status.class_big && status.class_small)) {
                char line[310]{};
                std::snprintf(line,sizeof(line),
                    "RUNTIME-ICON: stage=apply-self class=game-window "
                    "attempt=%u send_ok=%u readback=%u class_big=%u "
                    "class_small=%u win32_error=%lu owner=game-process",
                    iteration+1,unsigned(status.sent),
                    unsigned(status.readback),unsigned(status.class_big),
                    unsigned(status.class_small),
                    static_cast<unsigned long>(status.win32));
                report(line);
                last_report=GetTickCount();
            }
            if(status.readback || (status.sent && status.class_big &&
                                   status.class_small))
                return 1;
        }
        Sleep(250);
    }
    if(last_report==0)
        report("RUNTIME-ICON: stage=no-visible-game-window-after-15s");
    return 0;
}
} // namespace

void start_game_window_icon_worker(void (*logger)(const char*)) {
    if(g_worker_started.exchange(true,std::memory_order_acq_rel))return;
    g_log=logger;
    HANDLE thread=CreateThread(nullptr,0,icon_worker,nullptr,0,nullptr);
    if(thread)CloseHandle(thread);
    else {
        g_worker_started.store(false,std::memory_order_release);
        report("RUNTIME-ICON: stage=could-not-create-worker");
    }
}
} // namespace r2fix
