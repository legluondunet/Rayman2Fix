// Clean-room Rayman2Fix x86 runtime. Only the single approved retail
// executable build is eligible for a Hor+ camera hook. The launcher checks
// both full-file SHA256 hashes first. This runtime independently checks the
// loaded PE timestamp, image layout and exact original hook bytes.
// Do not perform any patching under the Windows loader lock.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "rayman2fix/fov_math.hpp"
#include "rayman2fix/hud_math.hpp"
#include "rayman2fix/intro_skip.hpp"
#include "rayman2fix/story_lookup.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

#if !defined(_M_IX86)
#error Rayman2Fix camera runtime requires Windows x86
#endif

namespace {
constexpr DWORD kConfigMagic = 0x32584652; // "RFX2" in little endian
constexpr DWORD kOriginalTimestamp = 0x3807B68B;
constexpr uintptr_t kCameraRva = 0x22AB0; // verified VA=0x422AB0 for image base 0x400000
// Original game's native scene names and deferred level-change API.
// These addresses apply ONLY to the single hash-verified retail executable.
constexpr uintptr_t kLevelNameRva = 0x100381; // VA 0x500381, char[30]
constexpr uintptr_t kRequestLevelRva = 0x54D0; // VA 0x4054D0
constexpr char kAfterIntroLevel[] = "learn_10";
// GOG/retail Rayman 2 engine: Fix actor 6 owns the progress-bit array
// (designer variable 42). Read dynamically; do NOT assume heap pointers.
constexpr uintptr_t kFixActorListRva = 0x0B7300; // VA 0x4B7300: count, then direct actor pointers
constexpr unsigned kLearnHandoverBit = 1091;
constexpr unsigned kNoMoviesBit = 1188;
constexpr DWORD kTemporaryCheatMillis = 2500;
constexpr std::size_t kHookLength = 7;
constexpr BYTE kExpected[kHookLength] = {0x83, 0xEC, 0x0C, 0x8B, 0x4C, 0x24, 0x14};
// Rayman 2's GLI_tdstCamera (owner's approved Win32 executable).
// 3 longs + POS_tdstCompletePosition (88 bytes) => alphaX at 100, alphaY at 104.
constexpr std::size_t kAlphaXOffset = 100;
constexpr std::size_t kAlphaYOffset = 104;
using CameraAdjust = void (__cdecl*)(void*, void*, void*);
CameraAdjust g_original = nullptr;
using RequestLevel = void (__cdecl*)(const char*, unsigned char);
std::atomic<RequestLevel> g_request_intro_level{nullptr};
const char* g_current_level = nullptr;
r2fix::IntroSkipGuard g_intro_guard;
bool g_intro_level_logged = false;
r2fix::LearnIntroGuard g_learn_guard;
bool g_learn_logged = false;
bool g_learn_requested = false;
bool g_learn_handover = false;
DWORD g_temporary_cheat_start = 0;
std::uintptr_t g_temporary_cheat_word = 0;
std::uintptr_t g_progress_words = 0;
float g_aspect = 4.0f / 3.0f;
bool g_installed = false;
bool g_log = false;
float g_hud_scale = 1.0f;
std::atomic<void*> g_plain_sprite_original{nullptr};
std::atomic<void*> g_uv_sprite_original{nullptr};
std::atomic<void*> g_color_sprite_original{nullptr};
std::atomic<bool> g_plain_seen{false};
std::atomic<bool> g_uv_seen{false};
std::atomic<bool> g_color_seen{false};
std::atomic<bool> g_plain_corrected{false};
std::atomic<bool> g_uv_corrected{false};
std::atomic<bool> g_color_corrected{false};
volatile LONG g_hud_worker_started = 0;

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
    *last = L'\0'; // Remove rayman2.exe
    last = wcsrchr(path, L'\\');
    if (!last) return;
    // Direct parent path instead of ..\\ so Win32/Wine resolve identically.
    wcscpy_s(last + 1, MAX_PATH - static_cast<std::size_t>(last + 1 - path),
             L"Rayman2Fix.log");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}
bool game_window_is_foreground() {
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    return foreground && GetWindowThreadProcessId(foreground, &pid) &&
           pid == GetCurrentProcessId();
}


// Read-only process-memory introspection avoids trusting unverified actor
// pointers. The optional, very short cheat enablement uses one writable
// game-owned progress word and restores its original bit afterwards. This
// does not patch machine code or alter any save file on disk.
bool read_u32(std::uintptr_t address, std::uint32_t& value) {
    SIZE_T copied = 0;
    return address >= 0x10000 &&
           ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &copied) &&
           copied == sizeof(value);
}
bool read_u8(std::uintptr_t address, std::uint8_t& value) {
    SIZE_T copied = 0;
    return address >= 0x10000 &&
           ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &copied) &&
           copied == sizeof(value);
}
bool write_story_word(std::uintptr_t address, std::uint32_t value) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi,
                      sizeof(mbi)) ||
        mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
        (mbi.Protect & 0xff) != PAGE_READWRITE)
        return false;
    SIZE_T written = 0;
    return WriteProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(address),
                              &value, sizeof(value), &written) &&
           written == sizeof(value);
}
// Rayman 2 Fix actor 6, variable 42: array of 45 32-bit story words. Every
// dereference is checked, and none of the game-owned pointers are cached
// across level loads.
std::uintptr_t find_story_words() {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!base) return 0;
    return r2fix::locate_story_words(
        base + kFixActorListRva,
        [](std::uintptr_t p, std::uint32_t& v) { return read_u32(p, v); },
        [](std::uintptr_t p, std::uint8_t& v) { return read_u8(p, v); });
}
bool game_story_bit(std::uintptr_t words, unsigned bit, bool& value) {
    std::uint32_t word = 0;
    if (!words || !read_u32(words + r2fix::story_word_index(bit) * 4, word))
        return false;
    value = (word & r2fix::story_word_mask(bit)) != 0;
    return true;
}
void restore_temporary_no_movies() {
    if (!g_temporary_cheat_word) return;
    const auto where = g_temporary_cheat_word;
    g_temporary_cheat_word = 0;
    // Level changes can free/reuse this memory. Only touch the same
    // validated Fix progress-array object we temporarily modified.
    if (!g_progress_words || find_story_words() != g_progress_words) {
        log_line("RUNTIME-INTRO: progress array changed; stale memory left untouched");
        return;
    }
    std::uint32_t current = 0;
    if (!read_u32(where, current) ||
        !write_story_word(where, current & ~r2fix::story_word_mask(kNoMoviesBit))) {
        log_line("RUNTIME-INTRO: cannot restore transient NoMovies flag");
    } else {
        log_line("RUNTIME-INTRO: transient NoMovies flag restored");
    }
}
// In Learn_10, the 1091 story bit is written when the scripted director
// naturally hands Rayman over. Try the built-in NOMOVIES enablement only
// briefly while Enter is freshly pressed, then immediately restore it.
// No direct actor deletion, teleport, level reload or saved-game edits.
void poll_learn_intro_skip() {
    if (!g_current_level) return;
    if (!r2fix::is_first_playable_level(g_current_level)) {
        restore_temporary_no_movies();
        g_learn_guard.update(g_current_level, false, false);
        g_progress_words = 0;
        g_learn_logged = false;
        g_learn_requested = false;
        g_learn_handover = false;
        return;
    }
    if (!g_progress_words) {
        g_progress_words = find_story_words();
        if (g_progress_words)
            log_line("RUNTIME-INTRO: learn_10 progress array located via Fix actor variable 42");
    }
    if (!g_progress_words) {
        if (!g_learn_logged) {
            g_learn_logged = true;
            log_line("RUNTIME-INTRO: learn_10 progress array unavailable, skip safely disabled");
        }
        return;
    }
    bool handed_over = false, no_movies = false;
    if (!game_story_bit(g_progress_words, kLearnHandoverBit, handed_over) ||
        !game_story_bit(g_progress_words, kNoMoviesBit, no_movies))
        return;
    if (!g_learn_logged) {
        g_learn_logged = true;
        log_line(handed_over
            ? "RUNTIME-INTRO: learn_10 already playable, skip disabled"
            : "RUNTIME-INTRO: learn_10 intro active, Enter enables transient native skip");
    }
    if (g_temporary_cheat_word &&
        (handed_over ||
         DWORD(GetTickCount() - g_temporary_cheat_start) >= kTemporaryCheatMillis)) {
        restore_temporary_no_movies();
        if (handed_over) {
            g_learn_handover = true;
            log_line("RUNTIME-INTRO: learn_10 control handover confirmed (story bit 1091)");
        } else
            log_line("RUNTIME-INTRO: native skip attempt timed out; scene left unchanged");
    }
    if (!g_temporary_cheat_word && handed_over && g_learn_requested &&
        !g_learn_handover) {
        g_learn_handover = true;
        log_line("RUNTIME-INTRO: learn_10 control handover confirmed (story bit 1091)");
    }
    const bool foreground = game_window_is_foreground();
    const bool down = foreground && (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    if (!g_learn_guard.update(g_current_level, !handed_over, down) || !foreground)
        return;
    if (g_learn_requested) return;
    g_learn_requested = true;
    if (no_movies) {
        log_line("RUNTIME-INTRO: native NOMOVIES already enabled; Enter forwarded to game");
        return;
    }
    std::uint32_t word = 0;
    const std::uintptr_t where =
        g_progress_words + r2fix::story_word_index(kNoMoviesBit) * 4;
    if (!read_u32(where, word) ||
        !write_story_word(where, word | r2fix::story_word_mask(kNoMoviesBit))) {
        log_line("RUNTIME-INTRO: native skip unavailable; no game state changed");
        return;
    }
    g_temporary_cheat_word = where;
    g_temporary_cheat_start = GetTickCount();
    log_line("RUNTIME-INTRO: transient NOMOVIES enabled for learn_10 intro (Enter)");
}

void poll_second_intro_skip() {
    const auto native_change = g_request_intro_level.load(std::memory_order_acquire);
    if (!native_change || !g_current_level) return;
    const bool in_intro = r2fix::is_second_intro(g_current_level);
    if (in_intro && !g_intro_level_logged) {
        log_line("RUNTIME-INTRO: jail_20 detected; Space/Enter/Esc can skip to learn_10");
        g_intro_level_logged = true;
    } else if (!in_intro && g_intro_level_logged) {
        g_intro_level_logged = false;
    }
    // Never read keyboard state unless the game owns the foreground.
    // Being unfocused or holding a key while entering the scene is not a
    // skip request. A fresh press is required after one key-free frame.
    const bool foreground = game_window_is_foreground();
    const bool down = foreground &&
        ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0 ||
         (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 ||
         (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0);
    if (!g_intro_guard.update(g_current_level, down) || !foreground) return;

    // The game itself queues the level change for its next engine tick.
    // Do not force a renderer switch, destroy a video window or write
    // save data. This is the same native API used by script transitions.
    log_line("RUNTIME-INTRO: skip requested for jail_20 -> learn_10 (native level change, no save)");
    native_change(kAfterIntroLevel, 0);
}

void __cdecl camera_hor_plus(void* device, void* viewport, void* camera) {
    // Preserve the vertical viewing angle and expand the horizontal angle
    // as 4:3 -> the actual monitor aspect. Never modify 4:3 camera state.
    if (camera && g_aspect > 1.334f) {
        auto* bytes = static_cast<unsigned char*>(camera);
        float vertical = 0.0f;
        std::memcpy(&vertical, bytes + kAlphaYOffset, sizeof(vertical));
        const float expanded = r2fix::hor_plus_fov(vertical, g_aspect);
        if (valid_angle(expanded))
            std::memcpy(bytes + kAlphaXOffset, &expanded, sizeof(expanded));
    }
    g_original(device, viewport, camera);
    // Camera adjustment executes on the game's own render/main thread.
    // Use it only as a safe tick to request a *deferred* native transition.
    poll_second_intro_skip();
    poll_learn_intro_skip();
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


void initialize_native_intro_skip() {
    const auto* module = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (!module) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew >= 0x1000) return;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(module + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->FileHeader.TimeDateStamp != kOriginalTimestamp ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->OptionalHeader.SizeOfImage < kLevelNameRva + 30) {
        log_line("RUNTIME-INTRO: unknown executable layout, skip disabled");
        return;
    }
    const auto* level = reinterpret_cast<const char*>(module + kLevelNameRva);
    auto* request = reinterpret_cast<const void*>(module + kRequestLevelRva);
    MEMORY_BASIC_INFORMATION level_memory{}, code_memory{};
    if (!VirtualQuery(level, &level_memory, sizeof(level_memory)) ||
        !VirtualQuery(request, &code_memory, sizeof(code_memory)) ||
        level_memory.State != MEM_COMMIT ||
        code_memory.State != MEM_COMMIT ||
        (level_memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        (code_memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return;
    const DWORD protection = code_memory.Protect & 0xffu;
    if (protection != PAGE_EXECUTE &&
        protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY)
        return;
    g_current_level = level;
    g_request_intro_level.store(
        reinterpret_cast<RequestLevel>(const_cast<void*>(request)),
        std::memory_order_release);
    log_line("RUNTIME-INTRO: native Jail_20 skip armed; Learn_10 transient native skip probe enabled");
}

// Three game-side GLI driver callback slots, verified only for the SHA256-locked
// retail executable supported by this project. Unlike a Glide-wide heuristic,
// these callbacks are used for sprites, not every 3D triangle. The additional
// xOoZ=1 check preserves perspective-projected world sprites.
constexpr uintptr_t kPlainSpriteSlotRva = 0x104530; // VA 0x504530
constexpr uintptr_t kUvSpriteSlotRva = 0x104524;    // VA 0x504524
// The colored variant is used by the GLI font/sprite path in Rayman 2.
constexpr uintptr_t kColorSpriteSlotRva = 0x104838; // VA 0x504838
constexpr float kInternalGlideWidth = 1024.0f;
using PlainSpriteFn = void (__cdecl*)(r2fix::HudVertex*, float, void*);
using UvSpriteFn = void (__cdecl*)(r2fix::HudVertex*, float*, float, void*);
using ColorSpriteFn = void (__cdecl*)(r2fix::HudVertex*, void*, float, void*);

void report_first(std::atomic<bool>& flag, const char* line) {
    if (!flag.exchange(true, std::memory_order_relaxed))
        log_line(line);
}

void __cdecl hud_plain_sprite(r2fix::HudVertex* quad, float z, void* globals) {
    const auto original = reinterpret_cast<PlainSpriteFn>(
        g_plain_sprite_original.load(std::memory_order_acquire));
    if (!original) return;
    report_first(g_plain_seen, "RUNTIME-HUD: plain sprite callback reached");
    if (g_hud_scale >= 1.0f || !r2fix::is_screen_sprite_quad(quad)) {
        original(quad, z, globals);
        return;
    }
    report_first(g_plain_corrected, "RUNTIME-HUD: plain sprite 4:3 correction applied");
    r2fix::HudVertex centered[4];
    std::memcpy(centered, quad, sizeof(centered));
    r2fix::center_screen_sprite_quad(centered, kInternalGlideWidth, g_hud_scale);
    original(centered, z, globals);
}

void __cdecl hud_uv_sprite(r2fix::HudVertex* quad, float* uv,
                           float z, void* globals) {
    const auto original = reinterpret_cast<UvSpriteFn>(
        g_uv_sprite_original.load(std::memory_order_acquire));
    if (!original) return;
    report_first(g_uv_seen, "RUNTIME-HUD: UV sprite callback reached");
    if (g_hud_scale >= 1.0f || !r2fix::is_screen_sprite_quad(quad)) {
        original(quad, uv, z, globals);
        return;
    }
    report_first(g_uv_corrected, "RUNTIME-HUD: UV sprite 4:3 correction applied");
    r2fix::HudVertex centered[4];
    std::memcpy(centered, quad, sizeof(centered));
    r2fix::center_screen_sprite_quad(centered, kInternalGlideWidth, g_hud_scale);
    original(centered, uv, z, globals);
}


void __cdecl hud_color_sprite(r2fix::HudVertex* quad, void* colors,
                              float z, void* globals) {
    const auto original = reinterpret_cast<ColorSpriteFn>(
        g_color_sprite_original.load(std::memory_order_acquire));
    if (!original) return;
    report_first(g_color_seen, "RUNTIME-HUD: colored sprite callback reached");
    if (g_hud_scale >= 1.0f || !r2fix::is_screen_sprite_quad(quad)) {
        original(quad, colors, z, globals);
        return;
    }
    report_first(g_color_corrected, "RUNTIME-HUD: colored sprite 4:3 correction applied");
    r2fix::HudVertex centered[4];
    std::memcpy(centered, quad, sizeof(centered));
    r2fix::center_screen_sprite_quad(centered, kInternalGlideWidth, g_hud_scale);
    original(centered, colors, z, globals);
}

bool executable_callback(const void* address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
        return false;
    const DWORD protection = info.Protect & 0xff;
    return protection == PAGE_EXECUTE ||
           protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

bool writable_callback_slot(std::uintptr_t address) {
    const void* slot = reinterpret_cast<const void*>(address);
    MEMORY_BASIC_INFORMATION info{};
    if (!slot || !VirtualQuery(slot, &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
        return false;
    const DWORD protection = info.Protect & 0xff;
    return protection == PAGE_READWRITE ||
           protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

void install_callback(void* volatile* slot, void* replacement,
                      std::atomic<void*>& original, const char* message,
                      bool& logged) {
    if (!writable_callback_slot(reinterpret_cast<std::uintptr_t>(slot))) return;
    void* current = InterlockedCompareExchangePointer(slot, nullptr, nullptr);
    if (!current || current == replacement || !executable_callback(current))
        return;
    original.store(current, std::memory_order_release);
    if (InterlockedCompareExchangePointer(slot, replacement, current) == current &&
        !logged) {
        logged = true;
        log_line(message);
    }
}

DWORD WINAPI monitor_hud_callbacks(void*) {
    auto* module = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (!module) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(module + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->OptionalHeader.SizeOfImage < kColorSpriteSlotRva + sizeof(void*)) {
        log_line("RUNTIME-HUD: callback slots outside approved executable image");
        return 0;
    }
    // The renderer initializes these callbacks after the early DLL injection.
    // Keep them patched if the game later recreates its Glide driver.
    auto* plain = reinterpret_cast<void* volatile*>(
        const_cast<BYTE*>(module + kPlainSpriteSlotRva));
    auto* uv = reinterpret_cast<void* volatile*>(
        const_cast<BYTE*>(module + kUvSpriteSlotRva));
    auto* colors = reinterpret_cast<void* volatile*>(
        const_cast<BYTE*>(module + kColorSpriteSlotRva));
    bool plain_logged = false;
    bool uv_logged = false;
    bool color_logged = false;
    log_line("RUNTIME-HUD: 4:3 sprite callback monitor started");
    for (unsigned iteration = 0;; ++iteration) {
        install_callback(plain, reinterpret_cast<void*>(&hud_plain_sprite),
                         g_plain_sprite_original,
                         "RUNTIME-HUD: centered 4:3 screen-space sprites enabled",
                         plain_logged);
        install_callback(uv, reinterpret_cast<void*>(&hud_uv_sprite),
                         g_uv_sprite_original,
                         "RUNTIME-HUD: centered 4:3 UV sprites enabled",
                         uv_logged);
        install_callback(colors, reinterpret_cast<void*>(&hud_color_sprite),
                         g_color_sprite_original,
                         "RUNTIME-HUD: centered 4:3 colored sprites enabled",
                         color_logged);
        if (iteration == 10 || iteration == 120) {
            // No raw addresses or personal data; useful if callbacks never
            // become available, or become available but never execute.
            char status[160]{};
            std::snprintf(status, sizeof(status),
                          "RUNTIME-HUD: callback installed plain=%d uv=%d colors=%d; invoked plain=%d uv=%d colors=%d",
                          plain_logged ? 1 : 0, uv_logged ? 1 : 0, color_logged ? 1 : 0,
                          g_plain_seen.load() ? 1 : 0, g_uv_seen.load() ? 1 : 0,
                          g_color_seen.load() ? 1 : 0);
            log_line(status);
        }
        Sleep(500);
    }
}

void start_hud_monitor() {
    if (g_hud_scale >= 1.0f ||
        InterlockedCompareExchange(&g_hud_worker_started, 1, 0) != 0)
        return;
    HANDLE thread = CreateThread(nullptr, 0, monitor_hud_callbacks,
                                 nullptr, 0, nullptr);
    if (!thread) {
        InterlockedExchange(&g_hud_worker_started, 0);
        log_line("RUNTIME-HUD: could not create callback monitor thread");
        return;
    }
    CloseHandle(thread);
}
} // namespace

extern "C" __declspec(dllexport) unsigned int __cdecl Rayman2Fix_RuntimeApiVersion() {
    return 2;
}
extern "C" __declspec(dllexport) const char* __cdecl Rayman2Fix_RuntimeStatus() {
    return "Rayman2Fix x86 Hor+ camera, 4:3 HUD and scoped introduction skips";
}

// Called on a separate thread after LoadLibrary, NEVER in DllMain.
// Return 1 on installed hook, 3 for an unknown
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
    g_hud_scale = r2fix::hud_4_3_scale(settings->output_width,
                                      settings->output_height);
    g_log = settings->logging != 0;
    // This hook is also used for the targeted intro-skip poll on 4:3.
    // For narrow monitors camera_hor_plus leaves the FOV unchanged.
    const DWORD camera_status = install_camera_hook();
    // Unknown/unsupported game versions never receive additional hooks.
    if (camera_status == 1) {
        initialize_native_intro_skip();
        start_hud_monitor();
    }
    return camera_status;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    // DllMain runs under the Windows loader lock. Never patch code, create
    // threads, perform disk I/O or load dependencies here.
    return TRUE;
}
