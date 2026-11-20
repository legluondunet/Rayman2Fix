#include "rayman2fix/wine_host_reexec.hpp"
#include "rayman2fix/wine_host_audio.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace r2fix {
namespace {
#ifdef _WIN32
// Wine-specific, dynamically resolved APIs. Never import or call them on
// native Windows. Wine 11.19 provides both, but newer/other runners may not.
using WineGetUnixName = char* (__cdecl*)(const wchar_t*);
using WineUnixSpawn = LONG (WINAPI*)(char* const*, int);

bool wine_detected() {
    const auto ntdll=GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll,"wine_get_version");
}

bool has_any_pulse_latency_setting() {
    SetLastError(ERROR_SUCCESS);
    const DWORD size=GetEnvironmentVariableW(
        L"PULSE_LATENCY_MSEC",nullptr,0);
    // Preserve explicit empty values as well, and fail closed on errors.
    return size || GetLastError()!=ERROR_ENVVAR_NOT_FOUND;
}

std::wstring read_environment(const wchar_t* name) {
    SetLastError(ERROR_SUCCESS);
    const DWORD needed=GetEnvironmentVariableW(name,nullptr,0);
    if(!needed || needed>32768)return {};
    std::wstring buffer(needed,L'\0');
    const DWORD copied=GetEnvironmentVariableW(
        name,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!copied || copied>=buffer.size())return {};
    buffer.resize(copied);
    return buffer;
}

struct OwnedWineUnixName {
    char* name = nullptr;
    ~OwnedWineUnixName() { if(name) HeapFree(GetProcessHeap(),0,name); }
    OwnedWineUnixName(const OwnedWineUnixName&) = delete;
    OwnedWineUnixName& operator=(const OwnedWineUnixName&) = delete;
    explicit OwnedWineUnixName(char* n) : name(n) {}
};

#endif
} // namespace

std::optional<int> maybe_reexec_with_wine_host_audio(
    const std::filesystem::path& launcher, bool normal_launch,
    bool already_relaunched, bool verbose) {
#ifdef _WIN32
    const auto policy=select_wine_host_audio(normal_launch,wine_detected(),
        has_any_pulse_latency_setting(),already_relaunched);
    if(policy!=WineHostAudioAction::reexec_with_60ms)
        return std::nullopt;

    const auto ntdll=GetModuleHandleW(L"ntdll.dll");
    const auto kernel32=GetModuleHandleW(L"kernel32.dll");
    const auto spawn=ntdll ? reinterpret_cast<WineUnixSpawn>(
        GetProcAddress(ntdll,"__wine_unix_spawnvp")) : nullptr;
    const auto convert=kernel32 ? reinterpret_cast<WineGetUnixName>(
        GetProcAddress(kernel32,"wine_get_unix_file_name")) : nullptr;
    const std::wstring loader=read_environment(L"WINELOADER");
    if(!spawn || !convert || loader.empty()) {
        std::cerr << "AUDIO-WINE-HOST: fallback; Wine host-reexec APIs "
                     "or exact Wine loader unavailable\n";
        return std::nullopt;
    }

    OwnedWineUnixName unix_loader(convert(loader.c_str()));
    const auto absolute_launcher=std::filesystem::absolute(launcher).wstring();
    OwnedWineUnixName unix_launcher(convert(absolute_launcher.c_str()));
    if(!unix_loader.name || !unix_launcher.name ||
       !*unix_loader.name || !*unix_launcher.name) {
        std::cerr << "AUDIO-WINE-HOST: fallback; cannot convert Wine "
                     "loader or launcher to Unix path\n";
        return std::nullopt;
    }

    // We do not modify either the Windows environment or the Unix host
    // environment of the current process. Wine's own Unix spawn function
    // inherits the original host environment. /usr/bin/env overrides
    // PULSE_LATENCY_MSEC only for the new, exact Wine loader process.
    //
    // A hidden flag prevents a recursive loop regardless of environment
    // translation. The *second* launcher proceeds with original setup,
    // verification, FOV/runtime injection, logging and process supervision.
    std::vector<std::string> args = {
        "env", "PULSE_LATENCY_MSEC=60",
        unix_loader.name, unix_launcher.name,
        "--wine-audio-host-ready"
    };
    if(verbose)args.emplace_back("--verbose");
    std::vector<char*> argv;
    argv.reserve(args.size()+1);
    for(auto& arg:args)argv.push_back(arg.data());
    argv.push_back(nullptr);

    std::cout << "AUDIO-WINE-HOST: relaunch through original Wine loader "
                 "with PULSE_LATENCY_MSEC=60 (host environment only)\n"
              << std::flush;
    const LONG result=spawn(argv.data(),1);
    // Wine's __wine_unix_spawnvp(wait=1) returns child Unix exit status
    // (0..255) on success, otherwise a negative NTSTATUS error.
    if(result>=0 && result<=255)
        return static_cast<int>(result);
    std::cerr << "AUDIO-WINE-HOST: fallback; host process spawn failed "
                 "(Wine status=" << result << ")\n";
#else
    (void)launcher;
    (void)normal_launch;
    (void)already_relaunched;
    (void)verbose;
#endif
    return std::nullopt;
}
