// Clean-room Rayman2Fix x86 runtime. Only the single approved retail
// executable build is eligible for a Hor+ camera hook. The launcher checks
// both full-file SHA256 hashes first. This runtime independently checks the
// loaded PE timestamp, image layout and exact original hook bytes.
// Do not perform any patching under the Windows loader lock.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#if !defined(_M_IX86)
#error Rayman2Fix camera runtime requires Windows x86
#endif

namespace {
constexpr DWORD kConfigMagic = 0x32584652; // "RFX2" in little endian
constexpr DWORD kOriginalTimestamp = 0x3807B68B;
constexpr uintptr_t kCameraRva = 0x22AB0; // verified VA=0x422AB0 for image base 0x400000
constexpr std::size_t kHookLength = 7;
constexpr BYTE kExpected[kHookLength] = {0x83, 0xEC, 0x0C, 0x8B, 0x4C, 0x24, 0x14};
// Rayman 2's GLI_tdstCamera (owner's approved Win32 executable).
// 3 longs + POS_tdstCompletePosition (88 bytes) => alphaX at 100, alphaY at 104.
constexpr std::size_t kAlphaXOffset = 100;
constexpr std::size_t kAlphaYOffset = 104;
using CameraAdjust = void (__cdecl*)(void*, void*, void*);
CameraAdjust g_original = nullptr;
float g_aspect = 4.0f / 3.0f;
bool g_installed = false;
bool g_log = false;

struct RuntimeOptions {
    DWORD size;
    DWORD magic;
    DWORD output_width;
    DWORD output_height;
    DWORD logging;
};

bool valid_angle(float v) {
    return std::isfinite(v) && v > 0.05f && v < 3.0f;
}
void log_line(const char* message) {
    if (!g_log) return;
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    // The loaded game is ...\Rayman2Fix\rayman2.exe; parent log sits one
    // level up beside Rayman2Fix.exe. Use append-only, no extra log file.
    wchar_t* last = wcsrchr(path, L'\\');
    if (!last) return;
    wcscpy_s(last + 1, MAX_PATH - static_cast<std::size_t>(last + 1 - path),
             L"..\\Rayman2Fix.log");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}
void __cdecl camera_hor_plus(void* device, void* viewport, void* camera) {
    // Preserve the vertical viewing angle and expand the horizontal angle
    // as 4:3 -> the actual monitor aspect. Never modify 4:3 camera state.
    if (camera && g_aspect > 1.334f) {
        auto* bytes = static_cast<unsigned char*>(camera);
        float horizontal = 0.0f;
        float vertical = 0.0f;
        std::memcpy(&horizontal, bytes + kAlphaXOffset, sizeof(horizontal));
        std::memcpy(&vertical, bytes + kAlphaYOffset, sizeof(vertical));
        // The game's original xAlphaY is scaled by the reference ratio
        // of 0.75 (height/width). Reconstruct the 4:3 horizontal FOV
        // before transforming to the current output aspect.
        constexpr float reference_aspect = 4.0f / 3.0f;
        const float baseline = vertical / 0.75f;
        if (valid_angle(vertical) && valid_angle(baseline) &&
            valid_angle(horizontal)) {
            const float expanded = 2.0f * std::atan(
                std::tan(baseline * 0.5f) * (g_aspect / reference_aspect));
            if (valid_angle(expanded)) {
                std::memcpy(bytes + kAlphaXOffset, &expanded, sizeof(expanded));
            }
        }
    }
    g_original(device, viewport, camera);
}
void write_jump(BYTE* at, const void* destination) {
    at[0] = 0xE9;  // JMP rel32
    const uintptr_t after = reinterpret_cast<uintptr_t>(at + 5);
    const uintptr_t dest = reinterpret_cast<uintptr_t>(destination);
    const std::uint32_t relative = static_cast<std::uint32_t>(dest - after);
    std::memcpy(at + 1, &relative, sizeof(relative));
}
DWORD install_camera_hook() {
    if (g_installed) return 1;
    const auto* module = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (!module) return 3;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew >= 0x1000) return 3;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        module + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->FileHeader.TimeDateStamp != kOriginalTimestamp ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.AddressOfEntryPoint == 0 ||
        pe->OptionalHeader.SizeOfImage < kCameraRva + 64)
        return 3;

    auto* target = const_cast<BYTE*>(module + kCameraRva);
    if (std::memcmp(target, kExpected, kHookLength) != 0) return 3;

    auto* gateway = static_cast<BYTE*>(
        VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!gateway) return 4;
    std::memcpy(gateway, target, kHookLength);
    write_jump(gateway + kHookLength, target + kHookLength);
    DWORD old_gateway = 0;
    if (!VirtualProtect(gateway, 64, PAGE_EXECUTE_READ, &old_gateway)) {
        VirtualFree(gateway, 0, MEM_RELEASE);
        return 4;
    }
    g_original = reinterpret_cast<CameraAdjust>(gateway);

    DWORD old_target = 0;
    if (!VirtualProtect(target, kHookLength, PAGE_EXECUTE_READWRITE, &old_target)) {
        g_original = nullptr;
        VirtualFree(gateway, 0, MEM_RELEASE);
        return 4;
    }
    write_jump(target, reinterpret_cast<void*>(&camera_hor_plus));
    target[5] = target[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, kHookLength);
    DWORD ignored = 0;
    VirtualProtect(target, kHookLength, old_target, &ignored);
    g_installed = true;
    log_line("RUNTIME-FOV: Hor+ camera detour installed on approved Rayman 2 executable");
    return 1;
}
} // namespace

extern "C" __declspec(dllexport) unsigned int __cdecl Rayman2Fix_RuntimeApiVersion() {
    return 2;
}
extern "C" __declspec(dllexport) const char* __cdecl Rayman2Fix_RuntimeStatus() {
    return "Rayman2Fix x86 experimental Hor+ camera runtime";
}

// Called on a separate thread after LoadLibrary, NEVER in DllMain.
// Return 1 on installed hook, 2 for 4:3 (no hook), 3 for an unknown
// executable/bytes, 4 for a memory-protection error and 5 for bad settings.
extern "C" __declspec(dllexport)
DWORD WINAPI Rayman2Fix_Initialize(void* input) {
    if (!input) return 5;
    const auto* settings = static_cast<const RuntimeOptions*>(input);
    if (settings->size != sizeof(RuntimeOptions) ||
        settings->magic != kConfigMagic ||
        settings->output_width < 640 || settings->output_width > 16384 ||
        settings->output_height < 480 || settings->output_height > 8640)
        return 5;
    const float aspect = static_cast<float>(settings->output_width) /
                         static_cast<float>(settings->output_height);
    if (!std::isfinite(aspect) || aspect <= 0.1f || aspect > 10.0f) return 5;
    g_aspect = aspect;
    g_log = settings->logging != 0;
    if (aspect <= 1.334f) return 2;
    return install_camera_hook();
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    // DllMain runs under the Windows loader lock. Never patch code, create
    // threads, perform disk I/O or load dependencies here.
    return TRUE;
}
