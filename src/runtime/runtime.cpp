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
#include "rayman2fix/scene_intro.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unordered_set>
#include <vector>

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
// Runtime objects are discovered read-only at the moment the user presses
// Enter, never addressed by unsafe scene-node IDs.
constexpr uintptr_t kRaymanSuperObjectRva = 0x100578; // VA 0x500578
constexpr DWORD kSceneSkipMonitorMillis = 8000;
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
DWORD g_learn_intro_start = 0;
bool g_learn_requested = false;
bool g_learn_handover = false;
DWORD g_learn_skip_start = 0;
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


// Safe memory reads for recognized live game actors. No actor
// pointer is trusted without a checked memory access.
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

r2fix::IntroDirector find_intro_director(
    std::size_t& matched, std::size_t& visited,
    r2fix::IntroDirectorScanStats& stats) {
    matched = visited = 0;
    r2fix::IntroDirector found{};
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uint32_t rayman_so = 0;
    if (!base || !read_u32(base + kRaymanSuperObjectRva, rayman_so) || !rayman_so)
        return found;
    std::uintptr_t root = rayman_so;
    std::unordered_set<std::uintptr_t> parents;
    for (unsigned i=0;i<16;++i) {
        if (!parents.insert(root).second) return {};
        std::uint32_t parent=0;
        if (!read_u32(root+0x1c,parent) || !parent) break;
        root=parent;
    }
    std::vector<std::uintptr_t> pending{root};
    std::unordered_set<std::uintptr_t> seen;
    while (!pending.empty() && seen.size()<4096) {
        const auto so=pending.back();
        pending.pop_back();
        if (!so || !seen.insert(so).second) continue;
        ++visited;
        std::uint32_t child=0,sibling=0;
        if (read_u32(so+0x08,child) && child) pending.push_back(child);
        if (read_u32(so+0x14,sibling) && sibling) pending.push_back(sibling);
        auto candidate=r2fix::recognize_intro_director(
            so,rayman_so,
            [](std::uintptr_t p,std::uint32_t& v){return read_u32(p,v);},
            [](std::uintptr_t p,std::uint8_t& v){return read_u8(p,v);},
            &stats);
        if (candidate.valid()) {
            ++matched;
            found=candidate;
        }
    }
    // Never choose an actor if multiple cutscenes match.
    return matched==1 ? found : r2fix::IntroDirector{};
}

// AI_fn_ucChangeComportIntell, original Rayman2.exe VA=0x481080.
// Camera callback executes on the game's main thread; this is a native
// deferred script transition, NOT a manual modification of Rayman's
// coordinates, story bits, camera or saved game.
bool request_native_scene_finish(const r2fix::IntroDirector& candidate) {
    if (!candidate.valid()) return false;
    const auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (!module) return false;
    const void* native_function=module+0x81080;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(native_function,&info,sizeof(info)) ||
        info.State!=MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
        (info.Protect & PAGE_NOACCESS)) return false;
    const DWORD protection=info.Protect & 0xffu;
    if (protection!=PAGE_EXECUTE && protection!=PAGE_EXECUTE_READ &&
        protection!=PAGE_EXECUTE_READWRITE &&
        protection!=PAGE_EXECUTE_WRITECOPY) return false;
    using ChangeBehaviour=unsigned char (__cdecl*)(void*,void*);
    auto* fn=reinterpret_cast<ChangeBehaviour>(
        const_cast<void*>(native_function));
    // The return byte's meaning isn't documented; confirm actual game
    // state instead. This also avoids retrying a transition that succeeded
    // but returned zero.
    fn(reinterpret_cast<void*>(candidate.intelligence),
       reinterpret_cast<void*>(candidate.ending_behaviour));
    std::uint32_t current=0;
    return read_u32(candidate.intelligence+8,current) &&
           std::uintptr_t(current)==candidate.ending_behaviour;
}

void poll_learn_intro_skip() {
    if (!g_current_level) return;
    if (!r2fix::is_first_playable_level(g_current_level)) {
        g_learn_guard.update(g_current_level, false, false);
        g_progress_words = 0;
        g_learn_logged = false;
        g_learn_intro_start = 0;
        g_learn_requested = false;
        g_learn_handover = false;
        g_learn_skip_start = 0;
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
    bool handed_over = false;
    if (!game_story_bit(g_progress_words, kLearnHandoverBit, handed_over))
        return;
    if (!g_learn_logged) {
        g_learn_logged = true;
        g_learn_intro_start = GetTickCount();
        log_line(handed_over
            ? "RUNTIME-INTRO: learn_10 already playable, skip disabled"
            : "RUNTIME-INTRO: learn_10 intro active; Enter requests verified native end behaviour");
    }
    if (handed_over && g_learn_requested && !g_learn_handover) {
        g_learn_handover = true;
        log_line("RUNTIME-INTRO: learn_10 handover confirmed following accepted skip (story bit 1091)");
    }
    if (!handed_over && g_learn_skip_start &&
        DWORD(GetTickCount() - g_learn_skip_start) > kSceneSkipMonitorMillis) {
        g_learn_skip_start = 0;
        log_line("RUNTIME-INTRO: native end behaviour requested but handover not observed within 8s");
    }
    const bool foreground = game_window_is_foreground();
    const bool down = foreground && (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    if (!g_learn_guard.update(g_current_level, !handed_over, down) || !foreground)
        return;
    // Do not treat a failed candidate scan as a successful skip.
    // Fresh Enter presses can retry while the original scripted scene
    // is still active; holding Enter cannot repeatedly scan the tree.
    if (g_learn_requested) return;

    std::size_t matched = 0, visited = 0;
    r2fix::IntroDirectorScanStats stats{};
    const auto director = find_intro_director(matched, visited, stats);
    char message[240]{};
    std::snprintf(message, sizeof(message),
                  "RUNTIME-INTRO: Learn_10 scan t=%ums visited=%u matched=%u actors=%u armed=%u models=%u",
                  unsigned(DWORD(GetTickCount() - g_learn_intro_start)),
                  unsigned(visited), unsigned(matched), stats.actor_objects,
                  stats.armed_directors, stats.models_with_variables);
    log_line(message);
    std::snprintf(message, sizeof(message),
                  "RUNTIME-INTRO: native director identified=%u null_waypoint=%u valid_behaviours=%u active_intro=%u other_behaviour=%u",
                  stats.identified_directors,stats.null_waypoints,
                  stats.valid_behaviour_tables,stats.in_intro_behaviour,
                  stats.other_behaviours);
    log_line(message);
    if (!director.valid()) {
        log_line("RUNTIME-INTRO: no unique native behaviour-5 director; release Enter and retry while intro active");
        return;
    }
    if (!request_native_scene_finish(director)) {
        log_line("RUNTIME-INTRO: native end-behaviour request refused; release Enter and retry while intro active");
        return;
    }
    g_learn_requested = true;
    g_learn_skip_start = GetTickCount();
    log_line("RUNTIME-INTRO: native director behaviour 5->1 accepted; awaiting scripted handover");
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
    log_line("RUNTIME-INTRO: native Jail_20 skip armed; Learn_10 native end-behaviour probe enabled");
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
    return "Rayman2Fix x86 Hor+ camera, 4:3 HUD and scoped native introduction skips";
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
