// Clean-room Rayman2Fix x86 runtime. Only the single approved retail
// executable build is eligible for a Hor+ camera hook. The launcher checks
// both full-file SHA256 hashes first. This runtime independently checks the
// loaded PE timestamp, image layout and exact original hook bytes.
// Do not perform any patching under the Windows loader lock.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include "rayman2fix/fov_math.hpp"
#include "rayman2fix/hud_math.hpp"
#include "rayman2fix/screen_camera.hpp"
#include "rayman2fix/intro_skip.hpp"
#include "rayman2fix/story_lookup.hpp"
#include "rayman2fix/scene_intro.hpp"
#include "rayman2fix/scene_script_probe.hpp"
#include "rayman2fix/intro_gate_timeline.hpp"
#include "rayman2fix/intro_integer_reader.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <climits>
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
// Shared screen-world UI camera pointer owned by the original engine.
// Constructed by 0x4052C0 with FOV (1.5,0.75), displayed through
// secondary 4:3 viewport 0x5004D4. It persists across game levels.
constexpr uintptr_t kScreenCameraSlotRva=0x10054C; // VA 0x50054C
// Original game's native scene names and deferred level-change API.
// These addresses apply ONLY to the single hash-verified retail executable.
constexpr uintptr_t kLevelNameRva = 0x100381; // VA 0x500381, char[30]
constexpr uintptr_t kRequestLevelRva = 0x54D0; // VA 0x4054D0
constexpr char kAfterIntroLevel[] = "learn_10";
// GOG/retail Rayman 2 engine: Fix actor 6 owns the progress-bit array
// (designer variable 42). Read dynamically; do NOT assume heap pointers.
constexpr uintptr_t kFixActorListRva = 0x0B7300; // VA 0x4B7300: count, then direct actor pointers
constexpr unsigned kLearnHandoverBit = 1091;
constexpr unsigned kNoMoviesCheatBit = 1188; // Rayman 2's NOMOVIES flag
// Read-only diagnostics for natural Learn_10 handover.
constexpr uintptr_t kRaymanSuperObjectRva = 0x100578; // VA 0x500578
constexpr uintptr_t kFixCameraActorRva = 0x0B7308; // VA 0x4B7308
constexpr DWORD kNaturalHandoverFollowupMillis = 1000;
// Checkpoints are elapsed wall time, not cutscene progress: the native
// handover has been observed at both 25s and 40s. Skipped deadlines are
// discarded, never replayed in a burst. Never force a scene exit.
constexpr DWORD kNaturalTraceTimes[] = {
    250, 1000, 2500, 5000, 10000, 20000,
    21000, 22000, 23000, 24000, 24500, 25000, 25500,
    27000, 35000, 50000
};
constexpr std::size_t kHookLength = 7;
constexpr BYTE kExpected[kHookLength] = {0x83, 0xEC, 0x0C, 0x8B, 0x4C, 0x24, 0x14};
// Rayman 2's GLI_tdstCamera (owner's approved Win32 executable).
// 3 longs + POS_tdstCompletePosition (88 bytes) => alphaX at 100, alphaY at 104.
constexpr std::size_t kAlphaXOffset = 100;
constexpr std::size_t kAlphaYOffset = 104;
using CameraAdjust = void (__cdecl*)(void*, void*, void*);
CameraAdjust g_original = nullptr;
std::uintptr_t g_screen_camera_slot = 0;
std::atomic<bool> g_native_ui_camera_seen{false};
std::atomic<bool> g_world_camera_seen{false};
std::atomic<bool> g_ui_viewport_adjusted{false};
std::atomic<bool> g_ui_viewport_rejected{false};
using RequestLevel = void (__cdecl*)(const char*, unsigned char);
std::atomic<RequestLevel> g_request_intro_level{nullptr};
const char* g_current_level = nullptr;
r2fix::IntroSkipGuard g_intro_guard;
bool g_intro_level_logged = false;
bool g_learn_logged = false;
bool g_learn_intro_seen = false;
bool g_learn_handover = false;
DWORD g_learn_handover_start = 0;
DWORD g_learn_intro_start = 0;
unsigned g_learn_trace_index = 0;
bool g_learn_first_valid_seen = false;
bool g_learn_script_probe_done = false;
bool g_learn_nomovies_conditions_logged = false;
// Cached *read-only* pointers are sampled only during Learn_10 and
// validated with ReadProcessMemory before every use.
std::uintptr_t g_observed_director_intelligence = 0;
std::uintptr_t g_observed_director_behaviours = 0;
int g_observed_director_index = -1;
bool g_have_ray_state = false;
std::uint32_t g_last_ray_custom = 0;
std::uint8_t g_last_ray_flags = 0;
unsigned g_learn_change_snapshots = 0;
constexpr unsigned kMaxLearnChangeSnapshots = 8;
// Cached read-only byte locations from the *verified* director's integer
// variables. A failed ReadProcessMemory simply stops sampling.
struct ObservedDirectorByte {
    std::uintptr_t location = 0;
    unsigned type = 0;
    unsigned previous = 0;
};
ObservedDirectorByte g_director_var50{};
ObservedDirectorByte g_director_var110{};
ObservedDirectorByte g_director_var83{};
ObservedDirectorByte g_director_var14{};
ObservedDirectorByte g_director_var16{};
ObservedDirectorByte g_director_var95{};
ObservedDirectorByte g_director_var96{};
r2fix::IntroGateTimeline<48> g_director_gate_history{};
bool g_director_gate_history_reported = false;
std::uintptr_t g_progress_words = 0;
float g_aspect = 4.0f / 3.0f;
bool g_installed = false;
bool g_log = false;
float g_hud_scale = 1.0f;
std::atomic<void*> g_plain_sprite_original{nullptr};
std::atomic<void*> g_uv_sprite_original{nullptr};
std::atomic<void*> g_color_sprite_original{nullptr};
// Native GLI 2D is the single layer shared by character quads,
// screen images and frame quads. The source percent coords are
// corrected here, not in each individual text/arrow/panel branch.
constexpr uintptr_t kNative2DSpriteRva=0x29670; // VA 0x429670
constexpr BYTE kNative2DExpected[7]={
    0x83,0xEC,0x50,0x8B,0x44,0x24,0x54
};
using Native2DSprite=void (__cdecl*)(void*,float,float,float,float,void*);
Native2DSprite g_original_2d_sprite=nullptr;
thread_local unsigned g_native_2d_call_depth=0;
std::atomic<unsigned> g_native_2d_calls{0};
std::atomic<unsigned> g_native_2d_passthrough_plain{0};
std::atomic<unsigned> g_native_2d_passthrough_uv{0};
std::atomic<unsigned> g_native_2d_passthrough_color{0};
std::atomic<bool> g_native_2d_logged{false};
std::atomic<bool> g_native_2d_invalid_logged{false};
// From the exact SHA-locked retail EXE, the 2D function is called by
// three distinct engine producers. Compare ONLY call-site *identity*
// to understand their viewport and coordinate spaces; not for scaling
// or scene-specific logic. Counter values are bounded and read-only.
enum class Native2DProducer : unsigned { Glyph=0, ScreenImage=1, Frame=2, Other=3 };
std::atomic<unsigned> g_native_2d_producers[4]{};
std::atomic<unsigned> g_native_2d_geometry_samples_mask{0};
std::atomic<DWORD> g_native_2d_probe_start_ms{0};
// Bounded, render-only measurements of NATIVE (before any 4:3 map)
// bottom-of-screen 2D geometry, independently per source route.
// This does not modify a single rendered vertex or font.
struct Bottom2DStats {
    std::atomic<unsigned> calls{0};
    std::atomic<int> left_min{INT_MAX};
    std::atomic<int> right_max{INT_MIN};
    std::atomic<unsigned> examples{0};
};
Bottom2DStats g_native_bottom_2d[4];
// Associate the native percent inputs with the *same call's* GLI
// driver vertices, without changing the existing thread-local
// double-correction bypass. Native 2D callbacks are synchronous.
thread_local unsigned g_current_native_route=3;
thread_local float g_native_input_left=0.0f;
thread_local float g_native_input_right=0.0f;
thread_local float g_native_mapped_left=0.0f;
thread_local float g_native_mapped_right=0.0f;
std::atomic<unsigned> g_bottom_driver_samples[4]{};
// Unrouted sprite submissions may include screen-world dialogue actors
// and the blue arrow. Probe only, NEVER use screen bounds or reciprocal-Z
// to decide whether to scale world geometry.
std::atomic<unsigned> g_unrouted_sprite_totals[3]{};
std::atomic<unsigned> g_unrouted_lower_counts[3][3]{};
std::atomic<unsigned> g_unrouted_lower_examples[3][3]{};
// UV sprites have not entered the tested lower-screen pixel region.
// Sample their real geometric ranges on each side of the first native
// bottom-frame submission, without making any rendering decisions.
std::atomic<unsigned> g_uv_phase_calls[2]{};
std::atomic<unsigned> g_uv_phase_valid[2]{};
std::atomic<unsigned> g_uv_phase_near_canvas[2]{};
std::atomic<unsigned> g_uv_phase_samples[2]{};
// Render-thread hint only: remembers the last camera passed to the
// game's own camera-adjust hook. Cannot identify sprite ownership alone.
thread_local r2fix::RenderCameraRole g_last_adjusted_camera_role=
    r2fix::RenderCameraRole::Unknown;
std::atomic<unsigned> g_camera_adjust_calls[3]{};
std::atomic<unsigned> g_postframe_plain_by_camera[3]{};
std::atomic<unsigned> g_postframe_lower_by_camera[3][3]{};
std::atomic<unsigned> g_postframe_lower_camera_examples[3][3]{};
// Native font glyphs can be drawn outside the narrow bottom-screen
// sample window; distinguish that from taking another geometry path.
std::atomic<unsigned> g_postframe_glyph_y_counts[4]{};
std::atomic<unsigned> g_postframe_glyph_y_examples[4]{};
// The owner's 94s Wine trace first draws its lower frame AFTER the
// hardcoded 40/60s snapshots. Start a frame-relative clock and measure
// glyphs whose original percent-Y actually overlaps that frame.
// This is diagnostic, not a dialogue/level identifier.
std::atomic<DWORD> g_first_lower_frame_tick{0};
std::atomic<int> g_first_lower_frame_xmin100{0};
std::atomic<int> g_first_lower_frame_xmax100{0};
std::atomic<int> g_first_lower_frame_ymin100{0};
std::atomic<int> g_first_lower_frame_ymax100{0};
std::atomic<unsigned> g_frame_overlap_glyphs{0};
std::atomic<unsigned> g_frame_overlap_outside_x{0};
std::atomic<int> g_frame_overlap_min_x100{INT_MAX};
std::atomic<int> g_frame_overlap_max_x100{INT_MIN};
std::atomic<unsigned> g_frame_overlap_examples{0};
// Record bounded engine code callsites for unnested plain sprites, not
// the last adjusted camera. Fingerprints are return RVAs (no pointers).
std::atomic<std::uintptr_t> g_approved_exe_base{0};
std::atomic<std::uint32_t> g_approved_exe_size{0};
struct PlainCallerStats {
    std::atomic<std::uint32_t> code_key{0}; // RVA+1; zero=empty
    std::atomic<unsigned> count{0};
    std::atomic<unsigned> lower{0};
    std::atomic<unsigned> samples{0};
};
PlainCallerStats g_postframe_plain_callers[8]{};
std::atomic<unsigned> g_postframe_plain_external{0};
std::atomic<unsigned> g_postframe_plain_overflow{0};



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

// Read-only samples of the genuine game handover. The previous experimental
// behaviour-3->1 shortcut corrupted graphics despite story bit 1091 changing.
// No native scene function is called and no game state is changed here.
bool read_superobject_position(std::uintptr_t so, float (&xyz)[3]) {
    std::uint32_t matrix = 0;
    if (!so || !read_u32(so + 0x24, matrix) || !matrix) return false;
    for (unsigned i = 0; i < 3; ++i) {
        std::uint32_t bits = 0;
        if (!read_u32(std::uintptr_t(matrix) + 4 + 4 * i, bits)) return false;
        std::memcpy(&xyz[i], &bits, sizeof(bits));
        if (!std::isfinite(xyz[i]) || std::fabs(xyz[i]) > 100000.0f)
            return false;
    }
    return true;
}
// A read-only scan of the actual scene, formerly used to locate the
// director. It now observes the native animation without changing it.
struct NaturalDirectorSample {
    unsigned matches=0,visited=0;
    r2fix::IntroDirectorScanStats counters{};
    std::uintptr_t standin=0;
    std::uintptr_t superobject=0;
    std::uintptr_t intelligence=0;
    std::uintptr_t behaviours=0;
};
NaturalDirectorSample sample_natural_director(std::uintptr_t ray_so) {
    NaturalDirectorSample sample{};
    if (!ray_so) return sample;
    std::uintptr_t root=ray_so;
    std::unordered_set<std::uintptr_t> parents;
    for (unsigned i=0;i<16;++i) {
        if (!parents.insert(root).second) return {};
        std::uint32_t up=0;
        if (!read_u32(root+0x1c,up) || !up) break;
        root=up;
    }
    std::vector<std::uintptr_t> pending{root};
    std::unordered_set<std::uintptr_t> seen;
    while (!pending.empty() && seen.size()<4096) {
        const auto so=pending.back();
        pending.pop_back();
        if (!so || !seen.insert(so).second) continue;
        ++sample.visited;
        std::uint32_t child=0,next=0;
        if (read_u32(so+0x08,child) && child) pending.push_back(child);
        if (read_u32(so+0x14,next) && next) pending.push_back(next);
        auto director=r2fix::recognize_intro_director(
            so,ray_so,
            [](std::uintptr_t p,std::uint32_t& v){return read_u32(p,v);},
            [](std::uintptr_t p,std::uint8_t& v){return read_u8(p,v);},
            &sample.counters);
        if (director.valid()) {
            ++sample.matches;
            sample.standin=director.standin_superobject;
            sample.superobject=director.superobject;
            sample.intelligence=director.intelligence;
            // ending_behaviour is index 1 in the validated model array.
            sample.behaviours=director.ending_behaviour-12;
        }
    }
    if (sample.matches!=1) {
        sample.standin=0;
        sample.superobject=0;
        sample.intelligence=0;
        sample.behaviours=0;
    }
    return sample;
}
// Observe the compiled director's *own* behaviour scripts. This is not
// an interpreter and NEVER invokes procedures, changes behaviour or
// reads any node parameter that points to an actor or game resource.
// Opcode ID/indent and local-model behaviour index only are logged.
void log_compiled_director_script(unsigned behaviour, const char* kind,
                                  unsigned rule, std::uintptr_t script_slot,
                                  std::uintptr_t list, unsigned list_count,
                                  unsigned& line_budget) {
    if (!line_budget || !g_log) return;
    const bool long_rule=(behaviour==3 && std::strcmp(kind,"rule")==0 &&
                          rule==2);
    const bool nomovies_rule=(behaviour==3 &&
                             std::strcmp(kind,"rule")==0 && rule==1);
    const DWORD started=GetTickCount();
    const auto p=r2fix::observe_scene_script(
        script_slot,
        [](std::uintptr_t a,std::uint32_t& v){return read_u32(a,v);},
        [](std::uintptr_t a,std::uint8_t& v){return read_u8(a,v);},
        long_rule ? 8192u : 1024u,
        long_rule ? 870u : (nomovies_rule ? 100u : 0u),
        long_rule ? 1100u : (nomovies_rule ? 265u : 0u));
    const DWORD probe_ms=DWORD(GetTickCount()-started);
    char message[300]{};
    std::snprintf(message,sizeof(message),
        "RUNTIME-INTRO: compiled-script b=%u %s=%u nodes=%u complete=%u malformed=%u selected=%u total=%u critical=%u truncated=%u scan_ms=%u (read-only)",
        behaviour,kind,rule,p.visited,unsigned(p.complete),
        unsigned(p.malformed),p.selected,p.total_selected,p.critical_total,
        unsigned(p.truncated),unsigned(probe_ms));
    log_line(message);
    --line_budget;
    if (!p.complete && !long_rule) return;
    // The cheat lives inside the game's progress array, which may be
    // saved. Verify the script gate without ever modifying it.
    if (nomovies_rule && line_budget) {
        const auto nm=r2fix::inspect_learn10_nomovies_gate(p);
        std::snprintf(message,sizeof(message),
            "RUNTIME-INTRO: native-nomovies-gate match=%u ifnot=%u get_bool=%u progress_array=%u bit1188=%u then=%u (read-only)",
            unsigned(nm.matched()),unsigned(nm.ifnot),
            unsigned(nm.get_boolean),unsigned(nm.progress_array),
            unsigned(nm.cheat_bit),unsigned(nm.then_branch));
        log_line(message);
        --line_budget;
        // The game does not merely check the cheat; it has a separate
        // top-level ELSE arm. Probe its structure before considering
        // any emulation of the native NOMOVIES feature.
        const auto other=r2fix::inspect_learn10_nomovies_alternative(p);
        if (line_budget) {
            std::snprintf(message,sizeof(message),
                "RUNTIME-INTRO: native-nomovies-alternative match=%u else=%u mode119=%u then=%u behaviour_change=%u (read-only; not auto-skip)",
                unsigned(other.matched()),unsigned(other.top_level_else),
                unsigned(other.mode_condition),unsigned(other.continuation),
                unsigned(other.native_behaviour_change));
            log_line(message);
            --line_budget;
        }
        if (line_budget) {
            const auto pair=r2fix::inspect_learn10_nomovies_buttons(p);
            std::snprintf(message,sizeof(message),
                "RUNTIME-INTRO: native-nomovies-buttons known=%u first_gate=%u second_gate=%u same_operand=%u first_opaque=%u second_opaque=%u (pointer values never logged, no key mapping inferred)",
                unsigned(pair.known()),unsigned(pair.first_gate),
                unsigned(pair.second_gate),unsigned(pair.same_operand),
                unsigned(pair.first_opaque),unsigned(pair.second_opaque));
            log_line(message);
            --line_budget;
        }
    }
    // The signature is a read-only diagnostic, never an enablement gate
    // for a shortcut. The actual engine's cleanup must still run.
    if (long_rule && line_budget) {
        const auto shape=r2fix::inspect_learn10_native_exit(p,list,list_count);
        std::snprintf(message,sizeof(message),
            "RUNTIME-INTRO: native-exit-shape match=%u exit_gate=%u counter=%u terminal=%u actor=%u rayman=%u retire=%u (read-only; NOT a skip)",
            unsigned(shape.matched()),unsigned(shape.exit_gate),
            unsigned(shape.counter_branch),unsigned(shape.terminal_signal),
            unsigned(shape.actor_reactivation),unsigned(shape.rayman_restoration),
            unsigned(shape.native_retirement));
        log_line(message);
        --line_budget;
    }
    if (long_rule && line_budget) {
        std::snprintf(message,sizeof(message),
            "RUNTIME-INTRO: phase110-predicates count=%u first_node=%u first_condition=%u (script-only, no state writes)",
            p.phase110_predicates,p.phase110_first_node,
            p.phase110_first_condition);
        log_line(message);
        --line_budget;
    }

    char tokens[260]{};
    unsigned used=0;
    for (unsigned i=0;i<p.selected && line_budget;++i) {
        const auto& e=p.events[i];
        char token[54]{};
        const char* prefix="?";
        bool has_number=true;
        std::int32_t value=std::int32_t(e.param);
        switch (e.type) {
            case 0:prefix="K";break; // keyword
            case 1:prefix="C";break; // condition
            case 2:prefix="O";break; // operator
            case 3:prefix="F";break; // function
            case 4:prefix="P";break; // procedure
            case 5:prefix="A";break; // frame-spanning meta-action
            case 9:prefix="H";break; // actor field
            case 11:prefix="V";break; // designer variable
            case 12:prefix="I";break; // integer literal
            case 14:prefix="button";has_number=(e.param<=255u);break;
            case 17:prefix="M";break; // mask
            case 23:prefix="actor";has_number=false;break;
            case 39:prefix="macro";has_number=false;break;
            case 28: // Map pointer only if it targets this exact model.
                prefix="B";has_number=false;
                if (e.param>=list && e.param<list+list_count*12u &&
                    (e.param-list)%12u==0u) {
                    value=std::int32_t((e.param-list)/12u);
                    has_number=true;
                }
                break;
        }
        if (has_number)
            std::snprintf(token,sizeof(token)," %u.%u:%s%d",e.node,e.indent,prefix,int(value));
        else
            std::snprintf(token,sizeof(token)," %u.%u:%s",e.node,e.indent,prefix);
        const unsigned n=unsigned(std::strlen(token));
        if (used && used+n>=sizeof(tokens)-1) {
            std::snprintf(message,sizeof(message),
                "RUNTIME-INTRO: compiled-script b=%u %s=%u %s",
                behaviour,kind,rule,tokens);
            log_line(message);
            --line_budget;
            tokens[0]=0;
            used=0;
        }
        if (!line_budget) break;
        if (n<sizeof(tokens)-used) {
            std::memcpy(tokens+used,token,n+1);
            used+=n;
        }
    }
    if (used && line_budget) {
        std::snprintf(message,sizeof(message),
            "RUNTIME-INTRO: compiled-script b=%u %s=%u %s",
            behaviour,kind,rule,tokens);
        log_line(message);
        --line_budget;
    }
    // The original rule has an EARLIER exit branch (v96==104)
    // independent of the six v96==110 stage pulses. Verify exact
    // opcode nesting and native cleanup, without writing v96 or
    // calling AI functions. A positive shape is a fact about the
    // compiled script, NOT proof that forcing the branch is safe.
    if (long_rule && line_budget) {
        const auto early=r2fix::observe_scene_script(
            script_slot,
            [](std::uintptr_t a,std::uint32_t& v){return read_u32(a,v);},
            [](std::uintptr_t a,std::uint8_t& v){return read_u8(a,v);},
            8192u,156u,249u);
        const auto shape=r2fix::inspect_learn10_early_exit(early);
        std::snprintf(message,sizeof(message),
            "RUNTIME-INTRO: native-early-exit-shape match=%u phase104=%u signal=%u actors=%u rayman=%u control=%u (read-only; NOT a skip)",
            unsigned(shape.matched()),unsigned(shape.phase104_branch),
            unsigned(shape.exit_signal),unsigned(shape.actor_reactivation),
            unsigned(shape.rayman_restoration),
            unsigned(shape.control_restoration));
        log_line(message);
        --line_budget;
    }
    // Only the previously capped long rule needs the extra output.
    // Ring-order chronology is reconstructed using total counters,
    // while pointer-typed operands are replaced by labels.
    if ((!long_rule && !nomovies_rule) || !line_budget) return;
    const auto emit_excerpt=[&](const char* label,
                                const r2fix::SceneScriptEvent* events,
                                unsigned total,unsigned count,
                                unsigned capacity) {
        if (!count || !line_budget) return;
        char payload[260]{};
        unsigned used=0;
        const unsigned first=(total-count)%capacity;
        for (unsigned i=0;i<count && line_budget;++i) {
            const auto& e=events[(first+i)%capacity];
            const char* opcode="?";
            bool show_number=true;
            int val=int(e.param);
            switch(e.type) {
                case 0:opcode="K";break;
                case 1:opcode="C";break;
                case 2:opcode="O";break;
                case 3:opcode="F";break;
                case 4:opcode="P";break;
                case 5:opcode="A";break;
                case 9:opcode="H";break;
                case 11:opcode="V";break;
                case 12:opcode="I";break;
                case 14:opcode="button";show_number=(e.param<=255u);break;
                case 17:opcode="M";break;
                case 23:opcode="actor";show_number=false;break;
                case 39:opcode="macro";show_number=false;break;
                case 28:
                    opcode="B";show_number=false;
                    if (e.param>=list && e.param<list+list_count*12u &&
                        (e.param-list)%12u==0u) {
                        val=int((e.param-list)/12u);
                        show_number=true;
                    }
                    break;
                default:show_number=false;break;
            }
            char token[52]{};
            if (show_number)
                std::snprintf(token,sizeof(token),
                              " %u.%u:%s%d",e.node,e.indent,opcode,val);
            else
                std::snprintf(token,sizeof(token),
                              " %u.%u:%s",e.node,e.indent,opcode);
            const unsigned len=unsigned(std::strlen(token));
            if (used && used+len>=sizeof(payload)-1) {
                std::snprintf(message,sizeof(message),
                    "RUNTIME-INTRO: compiled-script b=%u rule=%u %s %s",
                    behaviour,rule,label,payload);
                log_line(message);
                --line_budget;
                used=0;
                payload[0]=0;
            }
            if (!line_budget) break;
            if (len<sizeof(payload)-used) {
                std::memcpy(payload+used,token,len+1);
                used+=len;
            }
        }
        if (used && line_budget) {
            std::snprintf(message,sizeof(message),
                "RUNTIME-INTRO: compiled-script b=%u rule=%u %s %s",
                behaviour,rule,label,payload);
            log_line(message);
            --line_budget;
        }
    };
    // The first NOMOVIES rule was previously truncated after the
    // first 128 selected nodes; report its later instructions read-only.
    if (nomovies_rule) {
        if (p.complete && !p.malformed)
            emit_excerpt("nomovies-rule-tail",p.context.data(),
                         p.context_selected,p.context_selected,
                         unsigned(p.context.size()));
        return;
    }
    // A second bounded, read-only excerpt is produced ONLY if the
    // script actually contains a condition comparing V96 against 110.
    // It targets the missing middle of the large original rule rather
    // than dumping a second copy of its 1000+ nodes.
    if (p.phase110_predicates && line_budget) {
        const unsigned first=p.phase110_first_node;
        const unsigned begin=first>22 ? first-22 : 1;
        const unsigned end=first+38;
        const auto around=r2fix::observe_scene_script(
            script_slot,
            [](std::uintptr_t a,std::uint32_t& v){return read_u32(a,v);},
            [](std::uintptr_t a,std::uint8_t& v){return read_u8(a,v);},
            8192u,begin,end);
        if (around.complete && !around.malformed)
            emit_excerpt("phase110-context",around.context.data(),
                         around.context_selected,
                         around.context_selected,
                         unsigned(around.context.size()));
    }
    // The complete stage decision tree was missing between the
    // first-128-node prefix and native exit at node 870. Only for the
    // single approved exact shape, copy sanitized OPCODES from three
    // bounded, non-overlapping windows. This is a one-time read-only
    // diagnostic; it neither changes state nor invokes AI procedures.
    if (p.complete && !p.malformed && p.phase110_predicates==1 &&
        p.phase110_first_node==317 && line_budget) {
        struct Window { unsigned first,last; const char* label; };
        constexpr Window windows[]={
            {134u,383u,"middle-134-383"},
            {384u,639u,"middle-384-639"},
            {640u,869u,"middle-640-869"}
        };
        for (const auto& w : windows) {
            if (!line_budget) break;
            const auto middle=r2fix::observe_scene_script(
                script_slot,
                [](std::uintptr_t a,std::uint32_t& v){return read_u32(a,v);},
                [](std::uintptr_t a,std::uint8_t& v){return read_u8(a,v);},
                8192u,w.first,w.last);
            if (!middle.complete || middle.malformed ||
                middle.context_selected==0) continue;
            emit_excerpt(w.label,middle.context.data(),
                         middle.context_selected,
                         middle.context_selected,
                         unsigned(middle.context.size()));
        }
    }
    emit_excerpt("exit-context",p.context.data(),
                 p.context_selected,p.context_selected,
                 unsigned(p.context.size()));
    emit_excerpt("last",p.tail.data(),p.total_selected,
                 p.tail_selected,unsigned(p.tail.size()));
    emit_excerpt("procedures",p.critical.data(),p.critical_total,
                 p.critical_selected,unsigned(p.critical.size()));
}
void log_compiled_director_model(std::uintptr_t behaviours) {
    if (!g_log || !behaviours) return;
    log_line("RUNTIME-INTRO: compiled-script legend K=keyword C=condition O=operator F=function P=procedure A=meta H=field V=var I=int M=mask B=local-behaviour button=engine-input-id (values >255 and pointer types redacted)");
    // A capped once-per-intro full middle-branch dump adds up to roughly
    // 50 lines. Preserve the other director behaviours in the same log.
    unsigned budget=160;
    for (unsigned behaviour : {1u,3u,5u}) {
        const auto b=behaviours+behaviour*12u;
        std::uint32_t rules=0,schedule=0;
        std::uint8_t count=0;
        if (!read_u32(b,rules) || !read_u32(b+4,schedule) ||
            !read_u8(b+8,count)) {
            log_line("RUNTIME-INTRO: compiled-script behaviour metadata unavailable");
            continue;
        }
        char meta[150]{};
        std::snprintf(meta,sizeof(meta),
            "RUNTIME-INTRO: compiled-script behaviour=%u rules=%u schedule=%u",
            behaviour,unsigned(count),unsigned(schedule!=0));
        log_line(meta);
        if (schedule)
            log_compiled_director_script(behaviour,"schedule",0,
                                         schedule,behaviours,6,budget);
        // Rules are an array of four-byte script headers, not an array
        // of pointers to those headers. Keep the probe size bounded.
        if (rules && count<=16) {
            for (unsigned j=0;j<count && j<8 && budget;++j)
                log_compiled_director_script(behaviour,"rule",j,
                    std::uintptr_t(rules)+j*4u,behaviours,6,budget);
        } else if (count) {
            log_line("RUNTIME-INTRO: compiled-script rules unavailable or count excessive");
        }
    }
}

// Read a single low byte ONLY from known integer-type designer variables;
// no pointer-valued variables are logged. This is passive observation to
// understand whether the original exit branch is triggered by a flag.
bool read_director_integer_low_byte(std::uintptr_t so,unsigned index,
                                    unsigned& kind,unsigned& low,
                                    std::uintptr_t* location=nullptr) {
    std::uint32_t actor=0,brain=0,mind=0,memory=0,decl_ref=0;
    std::uint32_t decl=0,infos=0,buffer=0,offset=0,type=0;
    std::uint8_t count=0,value=0;
    if (!so || index>127 ||
        !read_u32(so+4,actor) || !actor ||
        !read_u32(std::uintptr_t(actor)+0x0c,brain) || !brain ||
        !read_u32(brain,mind) || !mind ||
        !read_u32(std::uintptr_t(mind)+0x0c,memory) || !memory ||
        !read_u32(memory,decl_ref) || !decl_ref ||
        !read_u32(decl_ref,decl) || !decl ||
        !read_u32(std::uintptr_t(decl)+4,infos) || !infos ||
        !read_u8(std::uintptr_t(decl)+0x0c,count) || index>=count ||
        !read_u32(std::uintptr_t(memory)+8,buffer) || !buffer ||
        !read_u32(std::uintptr_t(infos)+index*16u,offset) ||
        !read_u32(std::uintptr_t(infos)+index*16u+4,type) ||
        offset>65536)
        return false;
    // Publish the declared kind even when the contents are non-integer.
    // Never read a pointer/array/float value as an integer.
    kind=unsigned(type);
    if (type>6 || !read_u8(std::uintptr_t(buffer)+offset,value))
        return false;
    low=unsigned(value);
    if (location) *location=std::uintptr_t(buffer)+offset;
    return true;
}
// Exact-width, READ-ONLY values for scalar AI phase variables. Non-integer
// types (arrays, pointers, objects and float data) are deliberately skipped;
// this does not inspect or follow variable contents.
bool read_director_phase_integer(std::uintptr_t so,unsigned index,
                                 unsigned& kind,std::int32_t& value,
                                 std::uintptr_t* location=nullptr) {
    unsigned low=0;
    std::uintptr_t address=0;
    if (!read_director_integer_low_byte(so,index,kind,low,&address))
        return false;
    const bool ok=r2fix::read_intro_integer(address,kind,
        [](std::uintptr_t p,std::uint8_t& v){return read_u8(p,v);},
        [](std::uintptr_t p,std::uint32_t& v){return read_u32(p,v);},
        value);
    if (ok && location) *location=address;
    return ok;
}

bool log_natural_intro_snapshot(const char* stage, DWORD t) {
    if (!g_log) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!base) return false;
    std::uint32_t ray_so=0,ray_actor=0,standard=0,custom=0;
    std::uint8_t flags=0;
    if (!read_u32(base+kRaymanSuperObjectRva,ray_so) || !ray_so ||
        !read_u32(std::uintptr_t(ray_so)+4,ray_actor) || !ray_actor ||
        !read_u32(std::uintptr_t(ray_actor)+4,standard) || !standard ||
        !read_u32(std::uintptr_t(standard)+0x24,custom) ||
        !read_u8(std::uintptr_t(standard)+0x29,flags)) {
        char missing[120]{};
        std::snprintf(missing,sizeof(missing),
            "RUNTIME-INTRO: natural=%s t=%ums unavailable (Rayman data)",stage,unsigned(t));
        log_line(missing);
        return false;
    }
    float xyz[3]{},camera[3]{},standin[3]{};
    const bool ray_ok=read_superobject_position(ray_so,xyz);
    std::uint32_t cam_actor=0,cam_standard=0,cam_so=0;
    const bool cam_ok=
        read_u32(base+kFixCameraActorRva,cam_actor) && cam_actor &&
        read_u32(std::uintptr_t(cam_actor)+4,cam_standard) && cam_standard &&
        read_u32(std::uintptr_t(cam_standard)+0x0c,cam_so) && cam_so &&
        read_superobject_position(cam_so,camera);
    char msg[320]{};
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-INTRO: natural=%s t=%ums ray_custom=0x%08X ray_flags=0x%02X ray_xyz_valid=%u ray=(%.2f,%.2f,%.2f) camera_xyz_valid=%u camera=(%.2f,%.2f,%.2f)",
        stage,unsigned(t),unsigned(custom),unsigned(flags),unsigned(ray_ok),
        double(xyz[0]),double(xyz[1]),double(xyz[2]),unsigned(cam_ok),
        double(camera[0]),double(camera[1]),double(camera[2]));
    log_line(msg);
    const auto director=sample_natural_director(ray_so);
    if (director.matches==1 && director.intelligence && director.behaviours) {
        g_observed_director_intelligence=director.intelligence;
        g_observed_director_behaviours=director.behaviours;
        g_observed_director_index=director.counters.current_behaviour_index;
        if (director.counters.current_behaviour_index==3 &&
            !g_learn_nomovies_conditions_logged) {
            g_learn_nomovies_conditions_logged=true;
            // The first-visit branch checks progress(V49) && V11.
            // NOMOVIES's ELSE branch additionally checks !V119 and
            // a time guard(V93). Observe only integer scalar values,
            // never follow arbitrary pointer/array designer variables.
            unsigned k11=0,k49=0,k93=0,k119=0;
            std::int32_t v11=0,v49=0,v93=0,v119=0;
            const bool ok11=read_director_phase_integer(
                director.superobject,11,k11,v11);
            const bool ok49=read_director_phase_integer(
                director.superobject,49,k49,v49);
            const bool ok93=read_director_phase_integer(
                director.superobject,93,k93,v93);
            const bool ok119=read_director_phase_integer(
                director.superobject,119,k119,v119);
            char conditions[320]{};
            std::snprintf(conditions,sizeof(conditions),
                "RUNTIME-INTRO: native-nomovies-conditions at-first-b3 V11=%d(valid=%u,type=%u) V49=%d(valid=%u,type=%u) V93=%d(valid=%u,type=%u) V119=%d(valid=%u,type=%u) (read-only)",
                int(v11),unsigned(ok11),k11,int(v49),unsigned(ok49),k49,
                int(v93),unsigned(ok93),k93,int(v119),unsigned(ok119),k119);
            log_line(conditions);
        }
        // Run the one-time script probe only after the normal intro's
        // behaviour 3 is live; diagnostic is skipped if never observed.
        if (!g_learn_script_probe_done &&
            director.counters.current_behaviour_index==3) {
            g_learn_script_probe_done=true;
            log_compiled_director_model(director.behaviours);
        }
    }
    const bool standin_ok=director.standin &&
        read_superobject_position(director.standin,standin);
    unsigned t91=0,v91=0,t47=0,v47=0,t50=0,v50=0,t110=0,v110=0;
    unsigned t83=0,v83=0,t14=0,v14=0;
    std::uintptr_t address50=0,address110=0,address83=0,address14=0;
    const bool has91=director.superobject &&
        read_director_integer_low_byte(director.superobject,91u,t91,v91);
    const bool has47=director.superobject &&
        read_director_integer_low_byte(director.superobject,47u,t47,v47);
    const bool has50=director.superobject &&
        read_director_integer_low_byte(director.superobject,50u,t50,v50,&address50);
    const bool has110=director.superobject &&
        read_director_integer_low_byte(director.superobject,110u,t110,v110,&address110);
    const bool has83=director.superobject &&
        read_director_integer_low_byte(director.superobject,83u,t83,v83,&address83);
    const bool has14=director.superobject &&
        read_director_integer_low_byte(director.superobject,14u,t14,v14,&address14);
    // What changes *before* the counter advances? The failed opt-in
    // var83=0->6 test froze the whole intro without var50/var110 pulses.
    // Sample only scalar integers used near the native phase decision,
    // not pointer- or array-valued variables, and only at existing
    // natural checkpoints / state changes. No extra polling or writes.
    unsigned k16=0,k17=0,k95=0,k96=0;
    std::int32_t n16=0,n17=0,n95=0,n96=0;
    std::uintptr_t address16=0,address95=0,address96=0;
    const bool phase16=director.superobject &&
        read_director_phase_integer(director.superobject,16,k16,n16,&address16);
    const bool phase17=director.superobject &&
        read_director_phase_integer(director.superobject,17,k17,n17);
    const bool phase95=director.superobject &&
        read_director_phase_integer(director.superobject,95,k95,n95,&address95);
    const bool phase96=director.superobject &&
        read_director_phase_integer(director.superobject,96,k96,n96,&address96);
    // Cache only verified integer locations; sample them again solely at
    // the native gate changes. Never dereference variable values as pointers.
    if (phase16 && address16 && g_director_var16.location!=address16)
        g_director_var16={address16,k16,std::uint32_t(n16)};
    if (phase95 && address95 && g_director_var95.location!=address95)
        g_director_var95={address95,k95,std::uint32_t(n95)};
    if (phase96 && address96 && g_director_var96.location!=address96)
        g_director_var96={address96,k96,std::uint32_t(n96)};
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-INTRO: director-phase=%s t=%ums v16_valid=%u v16_type=%u v16=%d v17_valid=%u v17_type=%u v17=%d v95_valid=%u v95_type=%u v95=%d v96_valid=%u v96_type=%u v96=%d (read-only)",
        stage,unsigned(t),unsigned(phase16),k16,int(n16),
        unsigned(phase17),k17,int(n17),
        unsigned(phase95),k95,int(n95),
        unsigned(phase96),k96,int(n96));
    log_line(msg);
    // Only initialize a watched variable when its exact verified address
    // changes; do not overwrite the comparison state during snapshots.
    if (has50 && address50 && g_director_var50.location!=address50)
        g_director_var50={address50,t50,v50};
    // Type 5 in Rayman 2 is a *32-bit integer*. The old low-byte
    // diagnostic saw repeated 0->2 pulses; track the FULL value rather
    // than assuming the low byte describes its entire state.
    std::uint32_t full110=0;
    const bool has_full110=has110 && t110==5 && address110 &&
        read_u32(address110,full110);
    if (has110 && has_full110 &&
        g_director_var110.location!=address110)
        g_director_var110={address110,t110,unsigned(full110)};
    // The native rule compares var83 against (var14-1) and increments
    // var83 while it is below that threshold. Observe both full signed
    // values; never set them or assume a terminal value.
    std::int32_t full83=0,full14=0;
    const auto read_integer=[](std::uintptr_t address,unsigned type,
                               std::int32_t& out) {
        return r2fix::read_intro_integer(address,type,
            [](std::uintptr_t p,std::uint8_t& v){return read_u8(p,v);},
            [](std::uintptr_t p,std::uint32_t& v){return read_u32(p,v);},out);
    };
    const bool has_full83=has83 && address83 &&
        read_integer(address83,t83,full83);
    const bool has_full14=has14 && address14 &&
        read_integer(address14,t14,full14);
    if (has_full83 && g_director_var83.location!=address83)
        g_director_var83={address83,t83,static_cast<unsigned>(full83)};
    if (has_full14 && g_director_var14.location!=address14)
        g_director_var14={address14,t14,static_cast<unsigned>(full14)};
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-INTRO: director-vars=%s t=%ums var91_valid=%u var91_type=%u var91_low8=%u var47_valid=%u var47_type=%u var47_low8=%u (read-only)",
        stage,unsigned(t),unsigned(has91),t91,v91,unsigned(has47),t47,v47);
    log_line(msg);
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-INTRO: director-gates=%s t=%ums var50_valid=%u var50_type=%u var50_low8=%u var110_valid=%u var110_type=%u var110_low8=%u var110_u32_valid=%u var110_u32=%u (read-only)",
        stage,unsigned(t),unsigned(has50),t50,v50,unsigned(has110),t110,v110,
        unsigned(has_full110),unsigned(full110));
    log_line(msg);
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-INTRO: director-counter=%s t=%ums var83_valid=%u var83_type=%u var83=%d var14_valid=%u var14_type=%u var14=%d (read-only)",
        stage,unsigned(t),unsigned(has_full83),t83,int(full83),
        unsigned(has_full14),t14,int(full14));
    log_line(msg);
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-INTRO: director=%s t=%ums visited=%u matches=%u identified=%u armed=%u behaviour=%d suspended=%u standin_valid=%u standin=(%.2f,%.2f,%.2f)",
        stage,unsigned(t),director.visited,director.matches,
        director.counters.identified_directors,
        director.counters.armed_directors,
        director.counters.current_behaviour_index,
        director.counters.rayman_suspended,
        unsigned(standin_ok),double(standin[0]),double(standin[1]),double(standin[2]));
    log_line(msg);
    return true;
}
// Cheap change detector: the camera callback may not run for many
// seconds during an engine-managed sequence, so fixed wall-clock
// checkpoints cannot reconstruct that gap. When frames ARE observed,
// capture state transitions immediately, without writing to any actor.
void trace_natural_intro_changes(DWORD elapsed) {
    if (!g_log || g_learn_change_snapshots >= kMaxLearnChangeSnapshots)
        return;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uint32_t ray=0,actor=0,standard=0,custom=0;
    std::uint8_t flags=0;
    if (base &&
        read_u32(base+kRaymanSuperObjectRva,ray) && ray &&
        read_u32(std::uintptr_t(ray)+4,actor) && actor &&
        read_u32(std::uintptr_t(actor)+4,standard) && standard &&
        read_u32(std::uintptr_t(standard)+0x24,custom) &&
        read_u8(std::uintptr_t(standard)+0x29,flags)) {
        if (!g_have_ray_state) {
            g_have_ray_state=true;
            g_last_ray_custom=custom;
            g_last_ray_flags=flags;
        } else if (custom!=g_last_ray_custom || flags!=g_last_ray_flags) {
            g_last_ray_custom=custom;
            g_last_ray_flags=flags;
            ++g_learn_change_snapshots;
            log_natural_intro_snapshot("rayman-state-change",elapsed);
        }
    }
    if (g_observed_director_intelligence && g_observed_director_behaviours &&
        g_learn_change_snapshots < kMaxLearnChangeSnapshots) {
        std::uint32_t current=0;
        if (read_u32(g_observed_director_intelligence+8,current)) {
            const auto first=g_observed_director_behaviours;
            const auto active=std::uintptr_t(current);
            if (active>=first && active<first+64u*12u &&
                (active-first)%12u==0u) {
                const int index=int((active-first)/12u);
                if (index!=g_observed_director_index) {
                    g_observed_director_index=index;
                    ++g_learn_change_snapshots;
                    char msg[160]{};
                    std::snprintf(msg,sizeof(msg),
                        "RUNTIME-INTRO: director behaviour transition t=%ums index=%d (observed only)",
                        unsigned(elapsed),index);
                    log_line(msg);
                    log_natural_intro_snapshot("director-state-change",elapsed);
                }
            }
        }
    }
}

// Read-only counter/exit signals. Director var50 type 0 is an 8-bit
// integer, var110 type 5 is a signed 32-bit integer; var83 and var14
// use their own exact type widths. Watch the counter increments and the
// natural exit without modifying any script-owned value.
void trace_native_exit_gates(DWORD elapsed) {
    if (!g_log) return;
    const auto read_cached=[](const ObservedDirectorByte& v,
                              std::int32_t& out) {
        return v.location && r2fix::read_intro_integer(v.location,v.type,
            [](std::uintptr_t p,std::uint8_t& b){return read_u8(p,b);},
            [](std::uintptr_t p,std::uint32_t& w){return read_u32(p,w);},
            out);
    };
    // Read v16/95/96 only on a native change, once per callback. Reuse
    // the existing 48-entry event ring; no additional per-frame tracing.
    bool sampled=false;
    r2fix::IntroPhaseSample phase{};
    const auto capture_phase=[&]() -> const r2fix::IntroPhaseSample& {
        if (!sampled) {
            if (read_cached(g_director_var16,phase.v16)) phase.valid|=1;
            if (read_cached(g_director_var95,phase.v95)) phase.valid|=2;
            if (read_cached(g_director_var96,phase.v96)) phase.valid|=4;
            sampled=true;
        }
        return phase;
    };
    const auto sample=[&](unsigned index,ObservedDirectorByte& v) {
        if (!v.location || (index==50 && v.type!=0) ||
            (index==110 && v.type!=5)) return;
        std::int32_t signed_value=0;
        if (!read_cached(v,signed_value)) return;
        const std::uint32_t value=static_cast<std::uint32_t>(signed_value);
        if (value==v.previous) return;
        g_director_gate_history.record(
            elapsed,std::uint8_t(index),
            std::uint32_t(v.previous),value,capture_phase());
        v.previous=value;
    };
    sample(50,g_director_var50);
    sample(110,g_director_var110);
    sample(83,g_director_var83);
    sample(14,g_director_var14);
}

// Flush at least one second *after* natural handover: printing dozens
// of lines while the renderer is handing Rayman control back would itself
// perturb frame scheduling. No reads from freed director pointers.
void log_native_gate_history(DWORD elapsed) {
    if (!g_log || g_director_gate_history_reported) return;
    g_director_gate_history_reported=true;
    const auto& trace=g_director_gate_history;
    char message[320]{};
    std::snprintf(message,sizeof(message),
        "RUNTIME-INTRO: director-gate-history t=%ums total=%u retained=%u dropped=%u var110_eq1=%u var83_last=%d var14_last=%d (read-only, last changes)",
        unsigned(elapsed),unsigned(trace.total),
        unsigned(trace.retained()),unsigned(trace.dropped()),
        unsigned(trace.var110_equal_one),
        int(static_cast<std::int32_t>(g_director_var83.previous)),
        int(static_cast<std::int32_t>(g_director_var14.previous)));
    log_line(message);
    for(std::size_t i=0;i<trace.retained();++i) {
        const auto& e=trace.chronological(i);
        std::snprintf(message,sizeof(message),
            "RUNTIME-INTRO: director-gate-event t=%ums var%u old=%u new=%u phase_valid=0x%02X v16=%d v95=%d v96=%d (observed, not written)",
            unsigned(e.elapsed_ms),unsigned(e.variable),
            unsigned(e.previous),unsigned(e.current),
            unsigned(e.phase.valid),int(e.phase.v16),
            int(e.phase.v95),int(e.phase.v96));
        log_line(message);
    }
}

void poll_learn_intro_skip() {
    if (!g_current_level) return;
    if (!r2fix::is_first_playable_level(g_current_level)) {
        g_progress_words = 0;
        g_learn_logged = false;
        g_learn_intro_seen = false;
        g_learn_handover = false;
        g_learn_handover_start = 0;
        g_learn_intro_start = 0;
        g_learn_trace_index = 0;
        g_learn_first_valid_seen = false;
        g_learn_script_probe_done = false;
        g_learn_nomovies_conditions_logged = false;
        g_observed_director_intelligence = 0;
        g_observed_director_behaviours = 0;
        g_observed_director_index = -1;
        g_have_ray_state = false;
        g_last_ray_custom = 0;
        g_last_ray_flags = 0;
        g_learn_change_snapshots = 0;
        g_director_var50={};
        g_director_var110={};
        g_director_var83={};
        g_director_var14={};
        g_director_var16={};
        g_director_var95={};
        g_director_var96={};
        g_director_gate_history={};
        g_director_gate_history_reported=false;
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
            log_line("RUNTIME-INTRO: learn_10 progress unavailable; skip disabled");
        }
        return;
    }
    bool handed_over = false;
    if (!game_story_bit(g_progress_words, kLearnHandoverBit, handed_over))
        return;
    if (!g_learn_logged) {
        bool nomovies=false;
        const bool known=game_story_bit(g_progress_words,
                                        kNoMoviesCheatBit,nomovies);
        char flag[180]{};
        std::snprintf(flag,sizeof(flag),
            "RUNTIME-INTRO: nomovies-flag at-entry known=%u enabled=%u bit=1188 (read-only; never modified)",
            unsigned(known),unsigned(nomovies));
        log_line(flag);
        g_learn_logged = true;
        g_learn_intro_seen = !handed_over;
        g_learn_intro_start = GetTickCount();
        log_line(handed_over
            ? "RUNTIME-INTRO: learn_10 already playable; skip disabled"
            : "RUNTIME-INTRO: learn_10 skip disabled after visual regression; natural scene unchanged");
        if (g_learn_intro_seen)
            g_learn_first_valid_seen=log_natural_intro_snapshot("opening",0);
    }
    // Sample state changes whenever the callback actually runs; no timer
    // or secondary thread is created. A handover may take 25-40+ seconds.
    const DWORD elapsed=DWORD(GetTickCount()-g_learn_intro_start);
    if (g_learn_intro_seen && !g_learn_handover && g_log) {
        trace_natural_intro_changes(elapsed);
        // The native exit can remove the director between frames. Stop
        // reading its cached bytes at the FIRST observed exit state.
        if (!handed_over && g_observed_director_index!=1)
            trace_native_exit_gates(elapsed);
    }

    // Consume overdue checkpoints in ONE pass. An earlier build replayed
    // every missed deadline at 32s, creating nine near-duplicate snapshots.
    if (!handed_over && g_learn_intro_seen && g_log &&
        g_learn_trace_index<sizeof(kNaturalTraceTimes)/sizeof(kNaturalTraceTimes[0])) {
        const unsigned before=g_learn_trace_index;
        while (g_learn_trace_index <
                   sizeof(kNaturalTraceTimes)/sizeof(kNaturalTraceTimes[0]) &&
               elapsed>=kNaturalTraceTimes[g_learn_trace_index])
            ++g_learn_trace_index;
        if (g_learn_trace_index!=before) {
            if (g_learn_trace_index-before>1) {
                char gap[176]{};
                std::snprintf(gap,sizeof(gap),
                    "RUNTIME-INTRO: %u missed time checkpoints skipped at t=%ums (no retrospective samples)",
                    g_learn_trace_index-before-1,unsigned(elapsed));
                log_line(gap);
            }
            if (log_natural_intro_snapshot("during",elapsed) &&
                !g_learn_first_valid_seen) {
                g_learn_first_valid_seen=true;
                log_line("RUNTIME-INTRO: first valid natural state sampled");
            }
        }
    }
    if (handed_over && g_learn_intro_seen && !g_learn_handover) {
        g_learn_handover = true;
        g_learn_handover_start = GetTickCount();
        log_line("RUNTIME-INTRO: learn_10 natural handover observed (story bit 1091), no skip requested");
        bool nomovies=false;
        const bool known=game_story_bit(g_progress_words,
                                       kNoMoviesCheatBit,nomovies);
        char flag[180]{};
        std::snprintf(flag,sizeof(flag),
            "RUNTIME-INTRO: nomovies-flag at-handover known=%u enabled=%u bit=1188 (read-only; never modified)",
            unsigned(known),unsigned(nomovies));
        log_line(flag);
        log_natural_intro_snapshot("handover",DWORD(GetTickCount()-g_learn_intro_start));
    }
    if (handed_over && g_learn_handover_start &&
        DWORD(GetTickCount() - g_learn_handover_start) >= kNaturalHandoverFollowupMillis) {
        g_learn_handover_start = 0;
        log_natural_intro_snapshot("handover-plus-1s",DWORD(GetTickCount()-g_learn_intro_start));
        log_native_gate_history(DWORD(GetTickCount()-g_learn_intro_start));
    }
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
    // The engine has a separate screen-world 3D camera for overlay
    // actors, including dialogue glyph geometry. Preserve its NATIVE
    // 4:3 projection. Hor+ is exclusively for other 3D world cameras.
    void* screen_camera=nullptr;
    if (g_screen_camera_slot)
        std::memcpy(&screen_camera,
            reinterpret_cast<const void*>(g_screen_camera_slot),
            sizeof(screen_camera));
    const auto observed_role=r2fix::classify_render_camera(
        camera,screen_camera);
    g_last_adjusted_camera_role=observed_role;
    g_camera_adjust_calls[static_cast<unsigned>(observed_role)]
        .fetch_add(1,std::memory_order_relaxed);
    const bool native_ui=observed_role==
        r2fix::RenderCameraRole::ScreenWorld;
    if (camera && native_ui) {
        if (!g_native_ui_camera_seen.exchange(true,
                                  std::memory_order_relaxed)) {
            const auto* bytes=static_cast<const unsigned char*>(camera);
            float ax=0.0f,ay=0.0f;
            std::memcpy(&ax,bytes+kAlphaXOffset,sizeof(ax));
            std::memcpy(&ay,bytes+kAlphaYOffset,sizeof(ay));
            char line[180]{};
            std::snprintf(line,sizeof(line),
                "RUNTIME-FOV: native 4:3 screen-world camera preserved alphaX=%.3f alphaY=%.3f (Hor+ bypass)",
                double(ax),double(ay));
            log_line(line);
        }
    } else if (camera && g_aspect > 1.334f) {
        auto* bytes=static_cast<unsigned char*>(camera);
        float ay=0.0f,ax=0.0f;
        std::memcpy(&ay,bytes+kAlphaYOffset,sizeof(ay));
        std::memcpy(&ax,bytes+kAlphaXOffset,sizeof(ax));
        const float wide=r2fix::projection_alpha_x(
            ax,ay,g_aspect,false);
        if (valid_angle(wide) && wide!=ax) {
            std::memcpy(bytes+kAlphaXOffset,&wide,sizeof(wide));
            if (!g_world_camera_seen.exchange(true,
                                   std::memory_order_relaxed))
                log_line("RUNTIME-FOV: Hor+ enabled for world cameras; UI screen camera excluded");
        }
    }
    // Screen-world objects, including dialogue letters and the blue
    // continuation arrow, are projected through a separate viewport.
    // The original GLI viewport spans the entire output even when the
    // special camera itself retains its native FOV. Use the same
    // centered 4:3 horizontal transform as the 2D HUD renderer.
    // 0x4228D0 hands 0x422AB0 a 0x70-byte STACK-LOCAL copy of the
    // native viewport. Modify ONLY an extra local shadow here, and
    // let the untouched original routine produce the projection.
    // Native offsets: height +0x08, width +0x0C, X +0x58, Y +0x5C.
    void* projection_viewport=viewport;
    alignas(4) std::uint8_t ui_projection[0x70]{};
    if(native_ui && viewport && g_hud_scale<0.9999f) {
        const auto rect=r2fix::read_native_screen_viewport(viewport);
        r2fix::ScreenWorldViewport centered{};
        if(r2fix::center_screen_world_viewport(
              rect,g_hud_scale,centered)) {
            std::memcpy(ui_projection,viewport,sizeof(ui_projection));
            r2fix::write_centered_native_viewport(ui_projection,centered);
            projection_viewport=ui_projection;
            if(!g_ui_viewport_adjusted.exchange(
                    true,std::memory_order_relaxed)) {
                char message[224]{};
                std::snprintf(message,sizeof(message),
                    "RUNTIME-VIEWPORT: screen-world 4:3 centered native_width=%d native_x=%d projected_width=%d projected_x=%d height=%d (stack-only projection)",
                    int(rect.width),int(rect.left),int(centered.width),
                    int(centered.left),int(rect.height));
                log_line(message);
            }
        } else if(!g_ui_viewport_rejected.exchange(
                    true,std::memory_order_relaxed)) {
            char message[200]{};
            std::snprintf(message,sizeof(message),
                "RUNTIME-VIEWPORT: screen-world geometry rejected width=%d height=%d x=%d y=%d (native projection retained)",
                int(rect.width),int(rect.height),int(rect.left),
                int(rect.top));
            log_line(message);
        }
    }
    g_original(device, projection_viewport, camera);
    // Camera adjustment executes on the game's own render/main thread.
    // Use it only as a safe tick to request a *deferred* native transition.
    poll_second_intro_skip();
    poll_learn_intro_skip();
}
Native2DProducer classify_native_2d_producer(const void* retaddr) {
    // Literal *return* addresses after the verified game callsites:
    // 0x4664D4: JFFTXT_vDrawString -> 0x4664D9 (font glyphs);
    // 0x460653: primary image renderer -> 0x460658.
    // 0x4606DF/0x460743/0x46076D: CLIPPED screen-image fragments
    // -> 0x4606E4/0x460748/0x460772. All belong to UI images.
    // 0x429876: GLI_vDisplayFrame -> 0x42987B (frames).
    const auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto addr=reinterpret_cast<std::uintptr_t>(retaddr);
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    if (module && addr>=base) {
        switch (addr-base) {
            case 0x664D9: return Native2DProducer::Glyph;
            case 0x60658:
            case 0x606E4:
            case 0x60748:
            case 0x60772:
                return Native2DProducer::ScreenImage;
            case 0x2987B: return Native2DProducer::Frame;
            default: break;
        }
    }
    return Native2DProducer::Other;
}

void record_bottom_2d_geometry(unsigned route,
                                float xmin,float ymin,
                                float xmax,float ymax) {
    // We need actual caption/continue-arrow inputs, not the first
    // unrelated top-of-screen HUD glyph or image. Geometry only.
    // These diagnostic screen bands NEVER control rendering behavior.
    if(route>=4 || !std::isfinite(xmin) || !std::isfinite(xmax) ||
       !std::isfinite(ymin) || !std::isfinite(ymax) ||
       xmin < -100.0f || xmax > 200.0f ||
       ymin < 72.0f || ymax > 105.0f ||
       ymin > ymax || xmin > xmax)
        return;
    if(route==2 &&
       !g_first_lower_frame_tick.load(std::memory_order_acquire)) {
        // Single synchronous render producer; publish all geometry
        // BEFORE the release-stored frame timestamp.
        g_first_lower_frame_xmin100.store(
            int(std::lround(xmin*100.0f)),std::memory_order_relaxed);
        g_first_lower_frame_xmax100.store(
            int(std::lround(xmax*100.0f)),std::memory_order_relaxed);
        g_first_lower_frame_ymin100.store(
            int(std::lround(ymin*100.0f)),std::memory_order_relaxed);
        g_first_lower_frame_ymax100.store(
            int(std::lround(ymax*100.0f)),std::memory_order_relaxed);
        g_first_lower_frame_tick.store(
            GetTickCount()|1u,std::memory_order_release);
        char event[220]{};
        std::snprintf(event,sizeof(event),
            "RUNTIME-HUD-PANEL-LINK: first_lower_frame x=%.2f..%.2f y=%.2f..%.2f (original native percent; read-only, not verified dialogue identity)",
            double(xmin),double(xmax),double(ymin),double(ymax));
        log_line(event);
    }
    auto& stats=g_native_bottom_2d[route];
    stats.calls.fetch_add(1,std::memory_order_relaxed);
    const int left=static_cast<int>(std::lround(xmin*100.0f));
    const int right=static_cast<int>(std::lround(xmax*100.0f));
    int low=stats.left_min.load(std::memory_order_relaxed);
    while(left<low && !stats.left_min.compare_exchange_weak(
                     low,left,std::memory_order_relaxed)) {}
    int high=stats.right_max.load(std::memory_order_relaxed);
    while(right>high && !stats.right_max.compare_exchange_weak(
                     high,right,std::memory_order_relaxed)) {}
    const unsigned example=stats.examples.fetch_add(
        1,std::memory_order_relaxed);
    if(example<4) {
        const char* source[4]={"glyph","screen-image","frame","other"};
        char log[240]{};
        std::snprintf(log,sizeof(log),
            "RUNTIME-HUD-BOTTOM: sample=%u source=%s x=%.2f..%.2f y=%.2f..%.2f (original percent geometry, read-only)",
            example+1,source[route],double(xmin),double(xmax),
            double(ymin),double(ymax));
        log_line(log);
    }
}
void probe_native_glyph_after_frame(float xmin,float ymin,
                                    float xmax,float ymax) {
    if(g_native_bottom_2d[2].calls.load(
           std::memory_order_relaxed)==0) return;
    const unsigned band=r2fix::native_glyph_y_band(ymin,ymax);
    if(band>=4) return;
    // Compare to the FIRST native lower frame. The shared 4:3 map
    // preserves inclusion relations; if a glyph is already outside
    // in original percent space, shrinking both will not fix it.
    const DWORD first=g_first_lower_frame_tick.load(
        std::memory_order_acquire);
    if(first) {
        const float fx0=float(g_first_lower_frame_xmin100.load(
            std::memory_order_relaxed))/100.0f;
        const float fx1=float(g_first_lower_frame_xmax100.load(
            std::memory_order_relaxed))/100.0f;
        const float fy0=float(g_first_lower_frame_ymin100.load(
            std::memory_order_relaxed))/100.0f;
        const float fy1=float(g_first_lower_frame_ymax100.load(
            std::memory_order_relaxed))/100.0f;
        if(r2fix::native_glyph_overlaps_frame_y(
             ymin,ymax,fy0,fy1)) {
            g_frame_overlap_glyphs.fetch_add(
                1,std::memory_order_relaxed);
            const bool outside=r2fix::native_glyph_outside_frame_x(
                xmin,xmax,fx0,fx1);
            if(outside)g_frame_overlap_outside_x.fetch_add(
                1,std::memory_order_relaxed);
            const int left=int(std::lround(xmin*100.0f));
            const int right=int(std::lround(xmax*100.0f));
            int old=g_frame_overlap_min_x100.load(
                std::memory_order_relaxed);
            while(left<old && !g_frame_overlap_min_x100.compare_exchange_weak(
                  old,left,std::memory_order_relaxed)) {}
            old=g_frame_overlap_max_x100.load(std::memory_order_relaxed);
            while(right>old && !g_frame_overlap_max_x100.compare_exchange_weak(
                  old,right,std::memory_order_relaxed)) {}
            const unsigned sample=g_frame_overlap_examples.fetch_add(
                1,std::memory_order_relaxed);
            if(sample<4 && g_log) {
                char line[220]{};
                std::snprintf(line,sizeof(line),
                    "RUNTIME-HUD-PANEL-LINK: overlapping_glyph sample=%u original_x=%.2f..%.2f original_y=%.2f..%.2f outside_frame_x=%u (read-only)",
                    sample+1,double(xmin),double(xmax),
                    double(ymin),double(ymax),unsigned(outside));
                log_line(line);
            }
        }
    }
    g_postframe_glyph_y_counts[band].fetch_add(
        1,std::memory_order_relaxed);
    const unsigned sample=g_postframe_glyph_y_examples[band]
        .fetch_add(1,std::memory_order_relaxed);
    if(sample>=2 || !g_log) return;
    char message[220]{};
    std::snprintf(message,sizeof(message),
        "RUNTIME-HUD-GLYPH-BANDS: sample=%u y_band=%u original_x=%.2f..%.2f original_y=%.2f..%.2f (native percent; after first bottom frame; read-only)",
        sample+1,band,double(xmin),double(xmax),
        double(ymin),double(ymax));
    log_line(message);
}

void __cdecl native_2d_percent_sprite(void* viewport,
           float xmin,float ymin,float xmax,float ymax,void* material) {
    // This exact native entry is reached by:
    // - JFFTXT_vDrawString's per-character quads (VA 0x4664D4),
    // - the screen-image pass (VA 0x460653), and
    // - GLI_vDisplayFrame's panel rectangles (VA 0x429876).
    // The arguments are in PERCENT, 0..100 (multiplied by 0.01
    // inside GLI), not in the 1000-unit text descriptor system.
    // Transform X once *before* these routes diverge into different
    // driver callbacks. No Y/font/text/asset changes and no 3D.
    const auto original=g_original_2d_sprite;
    if(!original)return;
    // The original six-argument x86 ABI was checked with objdump.
    // Record separately which native producer actually reaches this
    // detour, and its ORIGINAL screen-space percent/viewport values.
    // Do not ever log glyph text, texture names or raw memory pointers.
    const auto producer=classify_native_2d_producer(
            _ReturnAddress());
    const unsigned route=static_cast<unsigned>(producer);
    g_native_2d_producers[route].fetch_add(
        1,std::memory_order_relaxed);
    record_bottom_2d_geometry(route,xmin,ymin,xmax,ymax);
    if(route==0u)
        probe_native_glyph_after_frame(xmin,ymin,xmax,ymax);
    const DWORD started=g_native_2d_probe_start_ms.load(
        std::memory_order_acquire);
    const DWORD elapsed=started ? GetTickCount()-started : 0;
    // Record the first call of each source early and after the 25s
    // Learn_10 natural cinematic. At most eight geometry-only lines.
    const unsigned sample_bit=1u<<(route+(elapsed>=25000 ? 4u:0u));
    if(!(g_native_2d_geometry_samples_mask.fetch_or(
                sample_bit,std::memory_order_relaxed)&sample_bit) &&
       viewport) {
        const char* names[4]={"glyph","screen-image","frame","other"};
        // The original GLI_vDraw2DSpriteWithPercent reads the native
        // viewport width at +0x0C and height at +0x08 itself.
        std::int32_t width=0,height=0;
        const auto* bytes=static_cast<const BYTE*>(viewport);
        std::memcpy(&height,bytes+0x08,sizeof(height));
        std::memcpy(&width,bytes+0x0C,sizeof(width));
        char sample[286]{};
        std::snprintf(sample,sizeof(sample),
            "RUNTIME-HUD-ROUTE: source=%s period=%s viewport_width=%d viewport_height=%d x_min=%.2f x_max=%.2f y_min=%.2f y_max=%.2f (input percents; no memory addresses)",
            names[route],elapsed>=25000 ? "after25s":"early",
            int(width),int(height),double(xmin),double(xmax),
            double(ymin),double(ymax));
        log_line(sample);
    }
    float left=0.0f,right=0.0f;
    if(g_hud_scale>=0.9999f ||
       !r2fix::center_native_2d_percent(
           xmin,xmax,g_hud_scale,left,right)) {
        if(g_hud_scale<0.9999f &&
           !g_native_2d_invalid_logged.exchange(
               true,std::memory_order_relaxed))
            log_line("RUNTIME-HUD: native 2D input geometry invalid; original percent coordinates retained");
        original(viewport,xmin,ymin,xmax,ymax,material);
        return;
    }
    g_native_2d_calls.fetch_add(1,std::memory_order_relaxed);
    if(!g_native_2d_logged.exchange(true,
                                     std::memory_order_relaxed))
        log_line("RUNTIME-HUD: native 2D percent canvas centered 4:3 (shared font/image/frame entry; downstream double-scaling bypassed)");
    const unsigned previous_route=g_current_native_route;
    const float prev_input_l=g_native_input_left;
    const float prev_input_r=g_native_input_right;
    const float prev_mapped_l=g_native_mapped_left;
    const float prev_mapped_r=g_native_mapped_right;
    g_current_native_route=route;
    g_native_input_left=xmin;
    g_native_input_right=xmax;
    g_native_mapped_left=left;
    g_native_mapped_right=right;
    ++g_native_2d_call_depth;
    original(viewport,left,ymin,right,ymax,material);
    --g_native_2d_call_depth;
    g_current_native_route=previous_route;
    g_native_input_left=prev_input_l;
    g_native_input_right=prev_input_r;
    g_native_mapped_left=prev_mapped_l;
    g_native_mapped_right=prev_mapped_r;
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
        pe->OptionalHeader.SizeOfImage < kCameraRva + 64 ||
        pe->OptionalHeader.SizeOfImage < kScreenCameraSlotRva + sizeof(void*))
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
    g_screen_camera_slot=reinterpret_cast<std::uintptr_t>(
        module+kScreenCameraSlotRva);

    DWORD old_target = 0;
    if (!VirtualProtect(target, kHookLength, PAGE_EXECUTE_READWRITE, &old_target)) {
        g_original = nullptr;
        g_screen_camera_slot=0;
        VirtualFree(gateway, 0, MEM_RELEASE);
        return 4;
    }
    write_jump(target, reinterpret_cast<void*>(&camera_hor_plus));
    target[5] = target[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, kHookLength);
    DWORD ignored = 0;
    VirtualProtect(target, kHookLength, old_target, &ignored);
    g_installed = true;
    log_line("RUNTIME-FOV: world Hor+ camera detour installed (dedicated screen-world UI camera excluded)");
    return 1;
}


void install_native_2d_percent_hook() {
    // Only call after the SHA/PE/byte validated camera hook was
    // installed for our exact supported, unprotected executable.
    // Original native 2D function has two complete x86 instructions
    // covering exactly 7 bytes: sub esp,50h; mov eax,[esp+54h].
    auto* module=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if(!module || g_original_2d_sprite)return;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return;
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        module+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->OptionalHeader.SizeOfImage<kNative2DSpriteRva+64)
        return;
    auto* target=module+kNative2DSpriteRva;
    if(std::memcmp(target,kNative2DExpected,sizeof(kNative2DExpected))!=0) {
        log_line("RUNTIME-HUD: native 2D entry bytes not approved; old sprite callbacks retained");
        return;
    }
    auto* gateway=static_cast<BYTE*>(VirtualAlloc(nullptr,64,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!gateway)return;
    std::memcpy(gateway,target,sizeof(kNative2DExpected));
    write_jump(gateway+sizeof(kNative2DExpected),
               target+sizeof(kNative2DExpected));
    DWORD old=0;
    if(!VirtualProtect(gateway,64,PAGE_EXECUTE_READ,&old)) {
        VirtualFree(gateway,0,MEM_RELEASE);
        return;
    }
    DWORD previous=0;
    if(!VirtualProtect(target,sizeof(kNative2DExpected),
                       PAGE_EXECUTE_READWRITE,&previous)) {
        VirtualFree(gateway,0,MEM_RELEASE);
        return;
    }
    g_original_2d_sprite=reinterpret_cast<Native2DSprite>(gateway);
    g_native_2d_probe_start_ms.store(
        GetTickCount(),std::memory_order_release);
    write_jump(target,reinterpret_cast<void*>(&native_2d_percent_sprite));
    target[5]=target[6]=0x90;
    FlushInstructionCache(GetCurrentProcess(),target,
                          sizeof(kNative2DExpected));
    DWORD unused=0;
    VirtualProtect(target,sizeof(kNative2DExpected),previous,&unused);
    log_line("RUNTIME-HUD: native 2D percent entry hook installed (all levels, shared font/image/frame geometry)");
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
    log_line("RUNTIME-INTRO: native Jail_20 skip armed; Learn_10 skip disabled after visual regression");
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

// Diagnostic only. The native GLI entry calls the downstream driver
// synchronously, so the same render thread holds the percent inputs.
// Compare original/mapped percent X to the final DRIVER pixel X.
void probe_native_2d_driver(const r2fix::HudVertex* quad,
                            const char* driver) {
    if (!quad || !g_native_2d_call_depth ||
        g_current_native_route>=4)return;
    float min_x=quad[0].x,max_x=quad[0].x;
    float min_y=quad[0].y,max_y=quad[0].y;
    for(unsigned i=0;i<4;++i) {
        if(!std::isfinite(quad[i].x) || !std::isfinite(quad[i].y) ||
           !std::isfinite(quad[i].reciprocal_z) ||
           std::fabs(quad[i].x)>16384.0f ||
           std::fabs(quad[i].y)>16384.0f)
            return;
        if(quad[i].x<min_x)min_x=quad[i].x;
        if(quad[i].x>max_x)max_x=quad[i].x;
        if(quad[i].y<min_y)min_y=quad[i].y;
        if(quad[i].y>max_y)max_y=quad[i].y;
    }
    // Diagnostic Y range only: no effect on rendering policy.
    if(min_y<550.0f || max_y>850.0f)return;
    const unsigned sample=g_bottom_driver_samples[
        g_current_native_route].fetch_add(1,std::memory_order_relaxed);
    if(sample>=3)return;
    const char* route[4]={"glyph","screen-image","frame","other"};
    char line[290]{};
    std::snprintf(line,sizeof(line),
        "RUNTIME-HUD-DRIVER: sample=%u route=%s callback=%s original_x=%.2f..%.2f mapped_x=%.2f..%.2f vertex_x=%.1f..%.1f vertex_y=%.1f..%.1f (read-only)",
        sample+1,route[g_current_native_route],driver,
        double(g_native_input_left),double(g_native_input_right),
        double(g_native_mapped_left),double(g_native_mapped_right),
        double(min_x),double(max_x),double(min_y),double(max_y));
    log_line(line);
}
// Read-only: sample sprites which bypass GLI_vDraw2DSpriteWithPercent.
// The native 2D font/panel quads are already verified at driver level.
// The unknown UV path may have reciprocal-Z=0; do NOT auto-scale it.
void probe_unrouted_lower_sprite(const r2fix::HudVertex* quad,
                                 unsigned kind,const void* caller) {
    if (!quad || kind>=3) return;
    g_unrouted_sprite_totals[kind].fetch_add(1,std::memory_order_relaxed);
    const DWORD started=g_native_2d_probe_start_ms.load(
        std::memory_order_acquire);
    if (!started || GetTickCount()-started<25000) return;
    r2fix::ScreenQuadGeometry b{};
    const bool valid=r2fix::inspect_screen_quad_geometry(quad,b);
    if(kind==1) {
        // This frame counter is already read-only geometry telemetry.
        // It is NOT proof that the bottom frame belongs to Murfy.
        const unsigned phase=g_native_bottom_2d[2].calls.load(
            std::memory_order_relaxed)>0 ? 1u : 0u;
        g_uv_phase_calls[phase].fetch_add(1,std::memory_order_relaxed);
        if(valid) g_uv_phase_valid[phase].fetch_add(
            1,std::memory_order_relaxed);
        const bool near_canvas=valid &&
            b.left>=-256.0f && b.right<=1280.0f &&
            b.top>=-256.0f && b.bottom<=1024.0f;
        if(near_canvas) g_uv_phase_near_canvas[phase].fetch_add(
            1,std::memory_order_relaxed);
        const unsigned sample=g_uv_phase_samples[phase].fetch_add(
            1,std::memory_order_relaxed);
        if(sample<4 && g_log) {
            char msg[300]{};
            std::snprintf(msg,sizeof(msg),
                "RUNTIME-HUD-UV-GEOMETRY: phase=%s sample=%u valid=%u near_canvas=%u x=%.1f..%.1f y=%.1f..%.1f reciprocal_z=%.4f..%.4f (read-only; sprite identity unknown)",
                phase ? "after-bottom-frame" : "before-bottom-frame",
                sample+1,unsigned(valid),unsigned(near_canvas),
                valid ? double(b.left) : 0.0,
                valid ? double(b.right) : 0.0,
                valid ? double(b.top) : 0.0,
                valid ? double(b.bottom) : 0.0,
                valid ? double(b.min_reciprocal_z) : 0.0,
                valid ? double(b.max_reciprocal_z) : 0.0);
            log_line(msg);
        }
    }
    if(kind==0 &&
       g_native_bottom_2d[2].calls.load(std::memory_order_relaxed)>0) {
        std::uint32_t rva=0;
        const auto base=g_approved_exe_base.load(std::memory_order_acquire);
        const auto size=g_approved_exe_size.load(std::memory_order_acquire);
        if(!r2fix::executable_callsite_rva(
              reinterpret_cast<std::uintptr_t>(caller),base,size,rva) ||
           rva==UINT32_MAX) {
            g_postframe_plain_external.fetch_add(
                1,std::memory_order_relaxed);
        } else {
            const auto key=rva+1u;
            bool found=false;
            for(auto& slot:g_postframe_plain_callers) {
                auto current=slot.code_key.load(std::memory_order_acquire);
                if(!current) {
                    slot.code_key.compare_exchange_strong(
                        current,key,std::memory_order_acq_rel);
                    current=slot.code_key.load(std::memory_order_acquire);
                }
                if(current!=key)continue;
                found=true;
                slot.count.fetch_add(1,std::memory_order_relaxed);
                if(valid && r2fix::is_lower_screen_probe(b)) {
                    slot.lower.fetch_add(1,std::memory_order_relaxed);
                    const unsigned n=slot.samples.fetch_add(
                        1,std::memory_order_relaxed);
                    if(n<2 && g_log) {
                        char message[240]{};
                        std::snprintf(message,sizeof(message),
                            "RUNTIME-HUD-PLAIN-CALLSITE: rva=0x%08X sample=%u x=%.1f..%.1f y=%.1f..%.1f (native game code, read-only; sprite identity unknown)",
                            unsigned(rva),n+1,
                            double(b.left),double(b.right),
                            double(b.top),double(b.bottom));
                        log_line(message);
                    }
                }
                break;
            }
            if(!found)g_postframe_plain_overflow.fetch_add(
                1,std::memory_order_relaxed);
        }
    }
    if(kind==0 && valid &&
       g_native_bottom_2d[2].calls.load(std::memory_order_relaxed)>0) {
        const unsigned role=static_cast<unsigned>(g_last_adjusted_camera_role);
        g_postframe_plain_by_camera[role].fetch_add(
            1,std::memory_order_relaxed);
        if(r2fix::is_lower_screen_probe(b)) {
            const unsigned region=r2fix::screen_quad_probe_sector(
                b,kInternalGlideWidth);
            g_postframe_lower_by_camera[role][region].fetch_add(
                1,std::memory_order_relaxed);
            const unsigned sample=g_postframe_lower_camera_examples[role][region]
                .fetch_add(1,std::memory_order_relaxed);
            if(sample<2 && g_log) {
                const char* labels[3]={"unknown","world","screen-world"};
                const char* regions[3]={"left","center","right"};
                char line[290]{};
                std::snprintf(line,sizeof(line),
                    "RUNTIME-HUD-CAMERA-CORRELATION: last_adjusted=%s region=%s sample=%u x=%.1f..%.1f y=%.1f..%.1f reciprocal_z=%.3f..%.3f (read-only; last-camera hint, not sprite identity)",
                    labels[role],regions[region],sample+1,
                    double(b.left),double(b.right),
                    double(b.top),double(b.bottom),
                    double(b.min_reciprocal_z),
                    double(b.max_reciprocal_z));
                log_line(line);
            }
        }
    }
    if(!valid || !r2fix::is_lower_screen_probe(b)) return;
    const unsigned sector=r2fix::screen_quad_probe_sector(
        b,kInternalGlideWidth);
    g_unrouted_lower_counts[kind][sector].fetch_add(
        1,std::memory_order_relaxed);
    const unsigned index=g_unrouted_lower_examples[kind][sector]
        .fetch_add(1,std::memory_order_relaxed);
    if(index>=2 || !g_log) return;
    const char* types[3]={"plain","uv","colored"};
    const char* regions[3]={"left","center","right"};
    char message[300]{};
    std::snprintf(message,sizeof(message),
        "RUNTIME-HUD-UNROUTED: callback=%s region=%s sample=%u x=%.1f..%.1f y=%.1f..%.1f reciprocal_z=%.4f..%.4f native_2d_screen_quad=%u (read-only; may be world sprite)",
        types[kind],regions[sector],index+1,
        double(b.left),double(b.right),double(b.top),double(b.bottom),
        double(b.min_reciprocal_z),double(b.max_reciprocal_z),
        unsigned(r2fix::is_screen_sprite_quad(quad)));
    log_line(message);
}

void __cdecl hud_plain_sprite(r2fix::HudVertex* quad, float z, void* globals) {
    const auto original = reinterpret_cast<PlainSpriteFn>(
        g_plain_sprite_original.load(std::memory_order_acquire));
    if (!original) return;
    report_first(g_plain_seen, "RUNTIME-HUD: plain sprite callback reached");
    if (g_native_2d_call_depth!=0) {
        probe_native_2d_driver(quad,"plain");
        // The shared native percent entry already transformed X.
        // Never apply the same 4:3 affine transform twice.
        g_native_2d_passthrough_plain.fetch_add(1,std::memory_order_relaxed);
        original(quad,z,globals);
        return;
    }
    probe_unrouted_lower_sprite(quad,0,_ReturnAddress());
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
    if (g_native_2d_call_depth!=0) {
        probe_native_2d_driver(quad,"uv");
        g_native_2d_passthrough_uv.fetch_add(1,std::memory_order_relaxed);
        original(quad,uv,z,globals);
        return;
    }
    probe_unrouted_lower_sprite(quad,1,_ReturnAddress());
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
    if (g_native_2d_call_depth!=0) {
        probe_native_2d_driver(quad,"colored");
        g_native_2d_passthrough_color.fetch_add(1,std::memory_order_relaxed);
        original(quad,colors,z,globals);
        return;
    }
    probe_unrouted_lower_sprite(quad,2,_ReturnAddress());
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

void report_panel_glyph_geometry(const char* when,
                                 DWORD elapsed_ms) {
    const unsigned glyphs=g_frame_overlap_glyphs.load(
        std::memory_order_relaxed);
    const int min_x=g_frame_overlap_min_x100.load(
        std::memory_order_relaxed);
    const int max_x=g_frame_overlap_max_x100.load(
        std::memory_order_relaxed);
    char event[290]{};
    std::snprintf(event,sizeof(event),
        "RUNTIME-HUD-PANEL-LINK: checkpoint=%s after_first_frame_ms=%lu glyphs_overlap_y=%u glyphs_outside_x=%u glyph_x_min=%.2f glyph_x_max=%.2f (original percent; first lower frame as reference, identity unverified)",
        when,static_cast<unsigned long>(elapsed_ms),glyphs,
        g_frame_overlap_outside_x.load(std::memory_order_relaxed),
        glyphs ? double(min_x)/100.0 : 0.0,
        glyphs ? double(max_x)/100.0 : 0.0);
    log_line(event);
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
    // Diagnostic callback return RVAs use the PE bounds validated above.
    // No absolute process address is written to the log.
    g_approved_exe_base.store(
        reinterpret_cast<std::uintptr_t>(module),
        std::memory_order_release);
    g_approved_exe_size.store(
        pe->OptionalHeader.SizeOfImage,std::memory_order_release);
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
    unsigned frame_relative_reports=0;
    constexpr DWORD kAfterFrameProbeMs[]={2000,5000,10000};
    for (unsigned iteration = 0;; ++iteration) {
        const DWORD first=g_first_lower_frame_tick.load(
            std::memory_order_acquire);
        if(first && frame_relative_reports<3) {
            const DWORD elapsed=GetTickCount()-first;
            if(elapsed>=kAfterFrameProbeMs[frame_relative_reports]) {
                const char* labels[]={"plus2s","plus5s","plus10s"};
                report_panel_glyph_geometry(
                    labels[frame_relative_reports],elapsed);
                ++frame_relative_reports;
            }
        }
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
        if (iteration == 10 || iteration == 80 || iteration == 120 ||
            iteration == 150 || iteration == 170 || iteration == 180) {
            // No raw addresses or personal data; useful if callbacks never
            // become available, or become available but never execute.
            char status[160]{};
            std::snprintf(status, sizeof(status),
                          "RUNTIME-HUD: callback installed plain=%d uv=%d colors=%d; invoked plain=%d uv=%d colors=%d",
                          plain_logged ? 1 : 0, uv_logged ? 1 : 0, color_logged ? 1 : 0,
                          g_plain_seen.load() ? 1 : 0, g_uv_seen.load() ? 1 : 0,
                          g_color_seen.load() ? 1 : 0);
            log_line(status);
            char native_2d_status[236]{};
            std::snprintf(native_2d_status,sizeof(native_2d_status),
                "RUNTIME-HUD: native2d at=%us total=%u downstream_plain=%u downstream_uv=%u downstream_colored=%u",
                unsigned(iteration/2),
                g_native_2d_calls.load(std::memory_order_relaxed),
                g_native_2d_passthrough_plain.load(std::memory_order_relaxed),
                g_native_2d_passthrough_uv.load(std::memory_order_relaxed),
                g_native_2d_passthrough_color.load(std::memory_order_relaxed));
            log_line(native_2d_status);
            char source_counts[235]{};
            std::snprintf(source_counts,sizeof(source_counts),
                "RUNTIME-HUD-ROUTE: at=%us glyph=%u image=%u frame=%u other=%u (native input producers)",
                unsigned(iteration/2),
                g_native_2d_producers[0].load(std::memory_order_relaxed),
                g_native_2d_producers[1].load(std::memory_order_relaxed),
                g_native_2d_producers[2].load(std::memory_order_relaxed),
                g_native_2d_producers[3].load(std::memory_order_relaxed));
            log_line(source_counts);
            if (iteration>=80) {
                const DWORD first=g_first_lower_frame_tick.load(
                    std::memory_order_acquire);
                if(first)report_panel_glyph_geometry(
                    "wall-time",GetTickCount()-first);
                const char* callback[3]={"plain","uv","colored"};
                for(unsigned k=0;k<3;++k) {
                    char extra[235]{};
                    std::snprintf(extra,sizeof(extra),
                        "RUNTIME-HUD-UNROUTED: at=%us callback=%s total=%u lower_left=%u lower_center=%u lower_right=%u (read-only)",
                        unsigned(iteration/2),callback[k],
                        g_unrouted_sprite_totals[k].load(
                            std::memory_order_relaxed),
                        g_unrouted_lower_counts[k][0].load(
                            std::memory_order_relaxed),
                        g_unrouted_lower_counts[k][1].load(
                            std::memory_order_relaxed),
                        g_unrouted_lower_counts[k][2].load(
                            std::memory_order_relaxed));
                    log_line(extra);
                }
                for(unsigned phase=0;phase<2;++phase) {
                    char uv_status[220]{};
                    std::snprintf(uv_status,sizeof(uv_status),
                        "RUNTIME-HUD-UV-GEOMETRY: at=%us phase=%s calls=%u valid=%u near_canvas=%u (read-only)",
                        unsigned(iteration/2),
                        phase ? "after-bottom-frame" : "before-bottom-frame",
                        g_uv_phase_calls[phase].load(
                            std::memory_order_relaxed),
                        g_uv_phase_valid[phase].load(
                            std::memory_order_relaxed),
                        g_uv_phase_near_canvas[phase].load(
                            std::memory_order_relaxed));
                    log_line(uv_status);
                }
                const char* camera_names[3]={
                    "unknown","world","screen-world"
                };
                for(unsigned role=0;role<3;++role) {
                    char diagnostic[290]{};
                    std::snprintf(diagnostic,sizeof(diagnostic),
                        "RUNTIME-HUD-CAMERA-CORRELATION: at=%us last_adjusted=%s camera_adjust_calls=%u plain_after_frame=%u lower_left=%u lower_center=%u lower_right=%u (read-only; thread-local hint)",
                        unsigned(iteration/2),camera_names[role],
                        g_camera_adjust_calls[role].load(
                            std::memory_order_relaxed),
                        g_postframe_plain_by_camera[role].load(
                            std::memory_order_relaxed),
                        g_postframe_lower_by_camera[role][0].load(
                            std::memory_order_relaxed),
                        g_postframe_lower_by_camera[role][1].load(
                            std::memory_order_relaxed),
                        g_postframe_lower_by_camera[role][2].load(
                            std::memory_order_relaxed));
                    log_line(diagnostic);
                }
                const char* bands[4]={
                    "y<30","30<=y<60","60<=y<72","y>=72"
                };
                for(unsigned k=0;k<4;++k) {
                    char glyphs[210]{};
                    std::snprintf(glyphs,sizeof(glyphs),
                        "RUNTIME-HUD-GLYPH-BANDS: at=%us after_first_bottom_frame band=%s glyphs=%u (read-only; no font ownership implied)",
                        unsigned(iteration/2),bands[k],
                        g_postframe_glyph_y_counts[k].load(
                            std::memory_order_relaxed));
                    log_line(glyphs);
                }
                for(const auto& slot:g_postframe_plain_callers) {
                    const auto key=slot.code_key.load(
                        std::memory_order_acquire);
                    if(!key)continue;
                    char caller[210]{};
                    std::snprintf(caller,sizeof(caller),
                        "RUNTIME-HUD-PLAIN-CALLSITE: at=%us rva=0x%08X count=%u lower=%u (approved game return RVA, read-only)",
                        unsigned(iteration/2),unsigned(key-1),
                        slot.count.load(std::memory_order_relaxed),
                        slot.lower.load(std::memory_order_relaxed));
                    log_line(caller);
                }
                char others[220]{};
                std::snprintf(others,sizeof(others),
                    "RUNTIME-HUD-PLAIN-CALLSITE: at=%us external=%u distinct_rva_overflow=%u (read-only)",
                    unsigned(iteration/2),
                    g_postframe_plain_external.load(std::memory_order_relaxed),
                    g_postframe_plain_overflow.load(std::memory_order_relaxed));
                log_line(others);
                const char* routes[3]={"glyph","screen-image","frame"};
                for(unsigned k=0;k<3;++k) {
                    const auto& stats=g_native_bottom_2d[k];
                    const unsigned n=stats.calls.load(
                        std::memory_order_relaxed);
                    const int low=stats.left_min.load(
                        std::memory_order_relaxed);
                    const int high=stats.right_max.load(
                        std::memory_order_relaxed);
                    char geo[240]{};
                    std::snprintf(geo,sizeof(geo),
                        "RUNTIME-HUD-BOTTOM: at=%us source=%s count=%u raw_min_x=%.2f raw_max_x=%.2f (native percent, before 4:3)",
                        unsigned(iteration/2),routes[k],n,
                        n?double(low)/100.0:0.0,
                        n?double(high)/100.0:0.0);
                    log_line(geo);
                }
            }
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
        // Hook the common native 2D entry before the game draws any
        // UI; the downstream driver callback monitor still handles
        // screen sprites submitted through other engine paths.
        if(g_hud_scale<0.9999f)install_native_2d_percent_hook();
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
