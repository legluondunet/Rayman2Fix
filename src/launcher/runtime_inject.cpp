#include "rayman2fix/runtime_inject.hpp"

#include <cstdint>
#include <filesystem>
#include <cwchar>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;
namespace r2fix {
namespace {
#ifdef _WIN32
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    explicit Handle(HANDLE v = nullptr) : value(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct RemoteData {
    HANDLE process;
    void* memory = nullptr;
    RemoteData(HANDLE p, const void* bytes, SIZE_T size) : process(p) {
        memory = VirtualAllocEx(p, nullptr, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        if (!memory) return;
        SIZE_T written = 0;
        if (!WriteProcessMemory(p, memory, bytes, size, &written) || written != size) {
            VirtualFreeEx(p, memory, 0, MEM_RELEASE);
            memory = nullptr;
        }
    }
    ~RemoteData() { if (memory) VirtualFreeEx(process, memory, 0, MEM_RELEASE); }
    RemoteData(const RemoteData&) = delete;
    RemoteData& operator=(const RemoteData&) = delete;
};
using ThreadEntry = LPTHREAD_START_ROUTINE;

// Do not assume that Windows or Wine maps kernel32 and kernelbase at the
// same absolute address in the launcher and the child process.
void* remote_system_entry(DWORD pid, FARPROC local) {
    HMODULE local_module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(local), &local_module))
        return nullptr;
    wchar_t module[MAX_PATH]{};
    const DWORD count = GetModuleFileNameW(local_module, module, MAX_PATH);
    if (!count || count >= MAX_PATH) return nullptr;
    const wchar_t* basename = wcsrchr(module, L'\\');
    basename = basename ? basename + 1 : module;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                            pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) return nullptr;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.value, &entry)) return nullptr;
    do {
        if (_wcsicmp(entry.szModule, basename) != 0) continue;
        const uintptr_t offset = reinterpret_cast<uintptr_t>(local) -
                                 reinterpret_cast<uintptr_t>(local_module);
        if (offset >= entry.modBaseSize) return nullptr;
        return reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(entry.modBaseAddr) + offset);
    } while (Module32NextW(snapshot.value, &entry));
    return nullptr;
}
bool remote_call(HANDLE process, void* routine, void* parameter,
                 DWORD& code, DWORD& win_error, bool& timed_out) {
    if (!routine) { win_error = ERROR_PROC_NOT_FOUND; return false; }
    Handle thread(CreateRemoteThread(process, nullptr, 0,
                                    reinterpret_cast<ThreadEntry>(routine),
                                    parameter, 0, nullptr));
    if (!thread.value) {
        win_error = GetLastError();
        return false;
    }
    const DWORD outcome = WaitForSingleObject(thread.value, 10000);
    if (outcome == WAIT_TIMEOUT) {
        timed_out = true;
        win_error = WAIT_TIMEOUT;
        return false;
    }
    if (outcome != WAIT_OBJECT_0) {
        win_error = GetLastError();
        return false;
    }
    if (!GetExitCodeThread(thread.value, &code)) {
        win_error = GetLastError();
        return false;
    }
    return true;
}
struct Init {
    DWORD size, magic, width, height, logging;
};
#endif
} // namespace

FovHookResult initialize_game_fov(
    std::uintptr_t process_handle, unsigned long pid,
    const fs::path& dll, std::uint32_t width, std::uint32_t height,
    bool log_enabled) {
    FovHookResult result{};
#ifdef _WIN32
    result.attempted = true;
    HANDLE process = reinterpret_cast<HANDLE>(process_handle);
    if (!process || width < 640 || height < 480 || !fs::is_regular_file(dll) ||
        fs::is_symlink(dll)) {
        result.win32_error = ERROR_FILE_NOT_FOUND;
        return result;
    }
    const auto fullpath = fs::absolute(dll).wstring();
    RemoteData path(process, fullpath.c_str(), (fullpath.size()+1)*sizeof(wchar_t));
    if (!path.memory) {
        result.win32_error = GetLastError();
        return result;
    }
    // LoadLibraryW is hosted by kernel32 or forwarded to kernelbase depending
    // on OS. Find the actual function owner in the target, then add its RVA.
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const FARPROC local_loader = kernel32 ?
        GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
    auto loader = local_loader ?
        remote_system_entry(static_cast<DWORD>(pid), local_loader) : nullptr;
    DWORD base = 0;
    bool timeout = false;
    if (!remote_call(process, loader, path.memory, base, result.win32_error, timeout)) {
        if (timeout) path.memory = nullptr; // Do not free while a remote thread uses it.
        return result;
    }
    if (!base) { result.win32_error = ERROR_MOD_NOT_FOUND; return result; }
    result.dll_loaded = true;

    // Resolve the local export RVA without running the DLL's DllMain in the
    // launcher process. The same verified x86 image is now in both processes.
    HMODULE local = LoadLibraryExW(fullpath.c_str(), nullptr,
                                  DONT_RESOLVE_DLL_REFERENCES);
    if (!local) { result.win32_error = GetLastError(); return result; }
    FARPROC init = GetProcAddress(local, "Rayman2Fix_Initialize");
    if (!init) init = GetProcAddress(local, "_Rayman2Fix_Initialize@4");
    const auto entry = init ? reinterpret_cast<void*>(
        static_cast<uintptr_t>(base) +
        (reinterpret_cast<uintptr_t>(init) -
         reinterpret_cast<uintptr_t>(local))) : nullptr;
    FreeLibrary(local);
    if (!entry) { result.win32_error = ERROR_PROC_NOT_FOUND; return result; }

    const Init options{sizeof(Init), 0x32584652u, width, height,
                       log_enabled ? 1u : 0u};
    RemoteData arguments(process, &options, sizeof(options));
    if (!arguments.memory) {
        result.win32_error = GetLastError();
        return result;
    }
    DWORD status = 0;
    if (!remote_call(process, entry, arguments.memory, status,
                     result.win32_error, timeout)) {
        if (timeout) arguments.memory = nullptr;
        return result;
    }
    result.init_status = status;
    result.active = status == 1 || status == 2;
#else
    (void)process_handle;
    (void)pid;
    (void)dll;
    (void)width;
    (void)height;
    (void)log_enabled;
#endif
    return result;
}
} // namespace r2fix
