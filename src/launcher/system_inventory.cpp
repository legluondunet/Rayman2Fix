#include "rayman2fix/startup.hpp"

#include <chrono>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <d3d9.h>
#include <dinput.h>
#include <xinput.h>
#endif

namespace r2fix {
namespace {

#ifdef _WIN32
// Passive Win32 hardware and compatibility inventory: no input acquisition,
// no shell calls, no user/host identifiers, no environment path disclosure.
std::string safe(const char* value) {
    if (!value || !*value) return "Unavailable";
    std::string result;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value);
         *p && result.size() < 150; ++p) {
        if (*p < 32 || *p == 127) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == ' ' ||
            *p == '.' || *p == '_' || *p == '-' || *p == '+' ||
            *p == ':' || *p == '(' || *p == ')' || *p == ',')
            result.push_back(static_cast<char>(*p));
        else result.push_back('?');
    }
    return result.empty() ? "Unavailable" : result;
}

std::string safe_w(const wchar_t* w) {
    if (!w || !*w) return "Unavailable";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0 || bytes > 1024) return "Unavailable";
    std::vector<char> data(static_cast<std::size_t>(bytes));
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, data.data(), bytes, nullptr, nullptr))
        return "Unavailable";
    return safe(data.data());
}

std::string t(unsigned long n) {
    return std::to_string(n);
}

bool env_exists(const wchar_t* key) {
    return GetEnvironmentVariableW(key, nullptr, 0) > 0;
}

std::string env_label(const wchar_t* key) {
    wchar_t raw[128]{};
    const DWORD size = GetEnvironmentVariableW(key, raw, 128);
    if (!size || size >= 128) return "Unavailable";
    for (const wchar_t* p = raw; *p; ++p)
        if (*p == '/' || *p == '\\' || *p == '@') return "Unavailable";
    return safe_w(raw);
}

void log_os(Startup& s) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using Version = LONG (WINAPI *)(OSVERSIONINFOW*);
    using WineVersion = const char* (__cdecl *)(void);
    using WineHost = void (__cdecl *)(const char**, const char**);
    auto rtl = reinterpret_cast<Version>(ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr);
    auto wine = reinterpret_cast<WineVersion>(ntdll ? GetProcAddress(ntdll, "wine_get_version") : nullptr);
    auto host = reinterpret_cast<WineHost>(ntdll ? GetProcAddress(ntdll, "wine_get_host_version") : nullptr);
    const bool is_wine = wine != nullptr;
    s.log(std::string("SYS-OS: runtime=") + (is_wine ? "Wine" : "Windows native"));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtl && rtl(&info) == 0) {
        const std::string label = info.dwMajorVersion == 10 && info.dwBuildNumber >= 22000
            ? "Windows 11-compatible" : "Win32 OS";
        s.log("SYS-OS: Win32=" + t(info.dwMajorVersion) + "." + t(info.dwMinorVersion) +
              " build=" + t(info.dwBuildNumber) + " label=" + label +
              (is_wine ? " (Wine-emulated, not host OS)" : ""));
    } else s.log("SYS-OS: Win32=Unavailable");
    if (is_wine) {
        s.log("SYS-WINE: version=" + safe(wine()));
        const char* name = nullptr;
        const char* release = nullptr;
        if (host) host(&name, &release);
        s.log("SYS-HOST: OS=" + safe(name) + " kernel=" + safe(release) +
              " (reported by Wine)");
        const bool steam = env_exists(L"STEAM_COMPAT_DATA_PATH") ||
                           env_exists(L"STEAM_COMPAT_TOOL_PATHS") ||
                           env_exists(L"PROTON_VERSION");
        s.log(std::string("SYS-PROTON: environment_hint=") + (steam ? "yes" : "none") +
              " version=" + (steam ? env_label(L"PROTON_VERSION") : "Unavailable") +
              " (environment hint only, not verified)");
        s.log("SYS-DESKTOP: DE=" + env_label(L"XDG_CURRENT_DESKTOP") +
              " session=" + env_label(L"XDG_SESSION_TYPE") + " (environment hints)");
    } else {
        s.log("SYS-WINE: not detected");
        s.log("SYS-PROTON: not detected");
        s.log("SYS-HOST: OS=Windows native");
    }
}

void log_cpu(Startup& s) {
    HKEY registry = nullptr;
    wchar_t model[256]{};
    DWORD bytes = sizeof(model), kind = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &registry) == ERROR_SUCCESS) {
        if (RegQueryValueExW(registry, L"ProcessorNameString", nullptr,
                            &kind, reinterpret_cast<BYTE*>(model), &bytes) != ERROR_SUCCESS ||
            kind != REG_SZ) model[0] = 0;
        model[255] = 0;
        RegCloseKey(registry);
    }
    SYSTEM_INFO cpu{};
    GetSystemInfo(&cpu);
    s.log("SYS-CPU: model=" + safe_w(model) +
          " logical_processors=" + t(cpu.dwNumberOfProcessors));
    DWORD required = 0;
    unsigned physical = 0;
    if (!GetLogicalProcessorInformation(nullptr, &required) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
        required >= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) &&
        required <= 1024u * 1024u) {
        std::vector<unsigned char> memory(required);
        auto* entries = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION>(memory.data());
        if (GetLogicalProcessorInformation(entries, &required)) {
            for (DWORD i = 0; i < required / sizeof(*entries); ++i)
                if (entries[i].Relationship == RelationProcessorCore) ++physical;
        }
    }
    s.log("SYS-CPU: physical_cores=" + (physical ? t(physical) : "Unavailable"));
    MEMORYSTATUSEX ram{};
    ram.dwLength = sizeof(ram);
    if (GlobalMemoryStatusEx(&ram))
        s.log("SYS-RAM: total_MiB=" + std::to_string(ram.ullTotalPhys / 1048576) +
              " available_MiB=" + std::to_string(ram.ullAvailPhys / 1048576) +
              " (Win32-reported, possibly Wine-emulated)");
    else s.log("SYS-RAM: Unavailable");
}

void log_screens(Startup& s) {
    s.log("SYS-DISPLAYS: count=" + t(GetSystemMetrics(SM_CMONITORS)) +
          " primary=" + t(GetSystemMetrics(SM_CXSCREEN)) + "x" +
          t(GetSystemMetrics(SM_CYSCREEN)) + " virtual=" +
          t(GetSystemMetrics(SM_CXVIRTUALSCREEN)) + "x" +
          t(GetSystemMetrics(SM_CYVIRTUALSCREEN)));
    unsigned count = 0;
    for (DWORD i = 0; i < 16 && count < 8; ++i) {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, i, &adapter, 0)) break;
        if (!(adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        ++count;
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        std::string info = "SYS-DISPLAY: index=" + t(count) +
                           " primary=" +
                           ((adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? "yes" : "no") +
                           " adapter=" + safe_w(adapter.DeviceString);
        if (EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &mode))
            info += " resolution=" + t(mode.dmPelsWidth) + "x" + t(mode.dmPelsHeight) +
                    " refresh_Hz=" + t(mode.dmDisplayFrequency);
        else info += " resolution=Unavailable";
        s.log(info);
    }
}

void log_gpu(Startup& s) {
    HMODULE module = LoadLibraryW(L"d3d9.dll");
    using Factory = IDirect3D9* (WINAPI *)(UINT);
    auto factory = reinterpret_cast<Factory>(module ? GetProcAddress(module, "Direct3DCreate9") : nullptr);
    IDirect3D9* api = factory ? factory(D3D_SDK_VERSION) : nullptr;
    if (!api) {
        s.log("SYS-GPU: D3D9 adapter=Unavailable");
        s.log("SYS-GRAPHICS-DRIVER: unavailable (Windows cannot report Linux Mesa driver)");
    } else {
        const UINT count = api->GetAdapterCount();
        for (UINT i = 0; i < count && i < 8; ++i) {
            D3DADAPTER_IDENTIFIER9 id{};
            if (FAILED(api->GetAdapterIdentifier(i, 0, &id))) continue;
            s.log("SYS-GPU: adapter=" + t(i) + " model=" + safe(id.Description) +
                  " vendor=0x" + [&]() {
                      char code[20]{};
                      snprintf(code, sizeof(code), "%04lX", static_cast<unsigned long>(id.VendorId));
                      return std::string(code);
                  }());
            s.log("SYS-GRAPHICS-DRIVER: adapter=" + t(i) +
                  " module=" + safe(id.Driver) +
                  " (D3D9 reported; may be Wine/DXVK, not host Mesa)");
        }
        api->Release();
    }
    if (module) FreeLibrary(module);
}

struct Pads {
    Startup* startup;
    unsigned count;
};
BOOL CALLBACK enumerate_di(const DIDEVICEINSTANCEW* instance, void* data) {
    auto* pads = static_cast<Pads*>(data);
    if (!pads || !instance) return DIENUM_STOP;
    if (pads->count >= 12) return DIENUM_STOP;
    ++pads->count;
    const unsigned long product = instance->guidProduct.Data1;
    char ident[24]{};
    snprintf(ident, sizeof(ident), "%04X:%04X",
             static_cast<unsigned>(LOWORD(product)),
             static_cast<unsigned>(HIWORD(product)));
    pads->startup->log("SYS-CONTROLLER: API=DirectInput8 slot=" +
                       t(pads->count) + " model=" + safe_w(instance->tszProductName) +
                       " VID:PID=" + ident + " (enumeration only)");
    return DIENUM_CONTINUE;
}

void log_gamepads(Startup& s) {
    const wchar_t* libraries[] = {
        L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"
    };
    using GetCaps = DWORD (WINAPI *)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    unsigned xcount = 0;
    HMODULE xlib = nullptr;
    GetCaps caps = nullptr;
    for (auto* name : libraries) {
        xlib = LoadLibraryW(name);
        caps = xlib ? reinterpret_cast<GetCaps>(GetProcAddress(xlib, "XInputGetCapabilities")) : nullptr;
        if (caps) break;
        if (xlib) FreeLibrary(xlib);
        xlib = nullptr;
    }
    if (caps) {
        for (DWORD i = 0; i < 4; ++i) {
            XINPUT_CAPABILITIES info{};
            if (caps(i, 0, &info) == ERROR_SUCCESS) {
                s.log("SYS-CONTROLLER: API=XInput slot=" + t(i) +
                      " subtype=" + t(info.SubType) + " model=Unavailable");
                ++xcount;
            }
        }
    }
    if (xlib) FreeLibrary(xlib);

    HMODULE din = LoadLibraryW(L"dinput8.dll");
    using Create = HRESULT (WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto create = reinterpret_cast<Create>(din ? GetProcAddress(din, "DirectInput8Create") : nullptr);
    IDirectInput8W* api = nullptr;
    Pads pads{&s, 0};
    if (create && SUCCEEDED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                                   IID_IDirectInput8W, reinterpret_cast<void**>(&api), nullptr)) && api) {
        const HRESULT result = api->EnumDevices(DI8DEVCLASS_GAMECTRL,
                                                enumerate_di, &pads, DIEDFL_ATTACHEDONLY);
        if (FAILED(result)) s.log("SYS-CONTROLLER: DirectInput8 enumeration unavailable");
        api->Release();
    } else s.log("SYS-CONTROLLER: DirectInput8 enumeration unavailable");
    if (din) FreeLibrary(din);
    s.log("SYS-CONTROLLER: XInput=" + t(xcount) + " DirectInput8=" + t(pads.count) +
          " (may describe the same device; no input acquisition)");
}

#endif

} // namespace

void log_system_inventory(Startup& s) {
    const auto started = std::chrono::steady_clock::now();
    s.log("SYS-INFO: BEGIN (passive startup diagnostics; no personal identifiers)");
#ifdef _WIN32
    log_os(s);
    log_cpu(s);
    log_screens(s);
    log_gpu(s);
    log_gamepads(s);
#else
    s.log("SYS-OS: non-Windows host test environment");
    s.log("SYS-INFO: Windows/Wine hardware details available in Windows x86 launcher only");
#endif
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    s.log("SYS-INFO: END elapsed_ms=" + std::to_string(elapsed));
}

} // namespace r2fix
