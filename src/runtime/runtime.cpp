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
#include "rayman2fix/superimposed_math.hpp"
#include "rayman2fix/intro_skip.hpp"
#include "rayman2fix/story_lookup.hpp"
#include "rayman2fix/scene_intro.hpp"
#include "rayman2fix/scene_script_probe.hpp"
#include "rayman2fix/intro_gate_timeline.hpp"
#include "rayman2fix/intro_integer_reader.hpp"
#include <mutex>
#if RAYMAN2FIX_ESC_MUSIC_PROTO
#include "rayman2fix/esc_music_wine_prototype.hpp"
#endif
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

#ifndef RAYMAN2FIX_ESC_MUSIC_PROTO
#define RAYMAN2FIX_ESC_MUSIC_PROTO 0
#endif
#if RAYMAN2FIX_ESC_MUSIC_PROTO && !RAYMAN2FIX_AUDIO_TRACE
#error ESC music experimental runtime requires the separate audio trace build
#endif
#ifndef RAYMAN2FIX_RESEARCH_PROBES
#define RAYMAN2FIX_RESEARCH_PROBES 0
#endif
#ifndef RAYMAN2FIX_AUDIO_TRACE
#define RAYMAN2FIX_AUDIO_TRACE 0
#endif
#if RAYMAN2FIX_AUDIO_TRACE != 0 && RAYMAN2FIX_AUDIO_TRACE != 1
#error RAYMAN2FIX_AUDIO_TRACE must be 0 or 1
#endif
#if RAYMAN2FIX_RESEARCH_PROBES != 0 && RAYMAN2FIX_RESEARCH_PROBES != 1
#error RAYMAN2FIX_RESEARCH_PROBES must be 0 or 1
#endif

namespace {
constexpr bool kResearchProbes=RAYMAN2FIX_RESEARCH_PROBES != 0;
constexpr DWORD kConfigMagic = 0x32584652; // "RFX2" in little endian
constexpr DWORD kOriginalTimestamp = 0x3807B68B;
// Verified by the owner's CD runtime byte fingerprint (12 November 2026):
// 0x4710C0 starts with two complete x86 instructions (3+4 bytes);
// its jump table checks (procedureId-0x199) <= 4 (409..413).
// This optional hook only COUNTS native script procedure calls.
constexpr uintptr_t kSuperimposedProcRva=0x710C0;
constexpr BYTE kSuperimposedProcExpected[7]={
    0x83,0xEC,0x28,0x8D,0x44,0x24,0x30
};
using SuperimposedProc = void* (__cdecl*)(void*,void*);
SuperimposedProc g_original_superimposed_proc=nullptr;
std::atomic<unsigned> g_superimposed_proc_calls[6]{};
std::atomic<unsigned> g_superimposed_proc_after_frame[6]{};
std::atomic<unsigned> g_superimposed_proc_samples[6]{};

// Exact image-base-0x400000 retail CD instructions verified directly
// against the original executable SHA-256 7927a106...d5e3.
// All four native screen-position caller sites pass 0x5004D4;
// the source-to-3D conversion must be corrected at its common entry.
constexpr uintptr_t kNativeScreenToVertexRva=0x22940; // VA 0x422940
constexpr uintptr_t kNativeUiViewportRva=0x1004D4;   // VA 0x5004D4
constexpr BYTE kNativeScreenToVertexExpected[6]={
    0x81,0xEC,0xD4,0x00,0x00,0x00
};
using NativeScreenToVertex=void (__cdecl*)(
    void*,void*,float,float,float);
NativeScreenToVertex g_original_screen_to_vertex=nullptr;
std::atomic<bool> g_overlay_source_patch_enabled{false};
std::atomic<unsigned> g_overlay_source_corrected_calls{0};
// Runtime evidence for the source-stage fix. Installation success alone
// does not tell us whether game text/actors reach this entry point.
std::atomic<unsigned> g_overlay_source_entry_calls{0};
std::atomic<unsigned> g_overlay_source_ui_calls{0};
std::atomic<unsigned> g_overlay_source_after_frame{0};
std::atomic<unsigned> g_overlay_source_after_glyph{0};
std::atomic<unsigned> g_overlay_source_samples_before{0};
std::atomic<unsigned> g_overlay_source_samples_after{0};
// Four verified approved-CD call RETURN RVAs to 0x422940, plus other.
// This classifies source conversion origins without inspecting text,
// game actors or level-specific data.
std::atomic<unsigned> g_overlay_source_routes[5]{};
std::atomic<unsigned> g_overlay_source_routes_after_frame[5]{};
const void* g_approved_overlay_viewport=nullptr;
// The original PE stores coefficients at VA 0x49CC58 / 0x49CCA4
// (1/640) and 0x49CCA8 (1/480). Redirect only FOUR verified
// fmul operands to DLL-owned values; never overwrite shared constants.
float g_superimposed_actor_extent_coefficient=1.0f/640.0f;
float g_superimposed_text_x_coefficient=1.0f/640.0f;
float g_superimposed_text_z_coefficient=1.0f/480.0f;
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
bool g_skip_cinematics = false;
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
// The first *overlapping glyph* can appear much later than the first
// lower frame. Start a separate read-only clock at that actual event.
std::atomic<DWORD> g_first_frame_overlap_glyph_tick{0};
std::atomic<unsigned> g_frame_overlap_outside_examples{0};
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
    DWORD skip_cinematics;
};

bool valid_angle(float v) {
    return std::isfinite(v) && v > 0.05f && v < 3.0f;
}
void log_line(const char* message) {
    if (!g_log) return;
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    // The loaded game is ...\Rayman2Fix\rayman2.exe; its dedicated
    // runtime log sits one level up beside the launcher (append-only).
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

// Research-only audio pause/resume trace for the owner's exact 1999 CD build.
// No sound call is suppressed, delayed or modified. The native sound DLL is
// loaded dynamically after the game starts, so sample its *existing* resolved
// function slots on the regular camera callback. Never hook these in release.
#if RAYMAN2FIX_AUDIO_TRACE
using AudioPauseFn=void (__stdcall*)(DWORD);
constexpr unsigned kAudioSlotCount=14;
struct AudioSlot {
    const char* name;
    std::uintptr_t rva;
};
constexpr AudioSlot kAudioSlots[kAudioSlotCount]={
    {"PauseSxd",0xB8664}, {"ResumeSxd",0xB8668},
    {"PauseTransitionSxd",0xB8708}, {"ResumeTransitionSxd",0xB870C},
    {"PauseMidi",0xB87B8}, {"ResumeMidi",0xB87BC},
    {"PauseTransitionMidi",0xB87F4}, {"ResumeTransitionMidi",0xB87F0},
    {"PauseCD",0xB8834}, {"ResumeCD",0xB883C},
    {"StopSxd",0xB8660}, {"StopTransitionSxd",0xB8704},
    {"SetSoundVolumeSxd",0xB866C}, {"StopCD",0xB8840}
};
// PlayTransitionExSxd is a separate five-argument stdcall entry
// (_SND_fn_lPlayTransitionExSxd@20). Track its last returned handle as
// evidence for a later PauseTransitionSxd call. No call is modified.
constexpr std::uintptr_t kTransitionPlayExRva=0xB86F0;
using AudioPlayTransitionExFn=DWORD (__stdcall*)(DWORD,DWORD,DWORD,DWORD,DWORD);
std::atomic<void*> g_transition_play_original{nullptr};
std::atomic<DWORD> g_last_transition_play_handle{0};
std::atomic<DWORD> g_last_transition_play_descriptor{0};
// Four original descriptor DWORDs, snapshotted at native play time.
// Only equality bits are logged. Never publish raw addresses or contents.
std::atomic<DWORD> g_last_transition_desc_words[4]{};
std::atomic<unsigned> g_last_transition_desc_readable{0};
std::atomic<unsigned> g_transition_play_calls{0};
std::atomic<void*> g_audio_original[kAudioSlotCount]{};
std::atomic<unsigned> g_audio_event_counts[kAudioSlotCount]{};
std::atomic<unsigned> g_audio_hooks_installed{0};
std::atomic<DWORD> g_audio_last_poll{0};
std::atomic<DWORD> g_audio_origin{0};
std::atomic<int> g_audio_last_depth{-1};
// Optional, read-only UI research: do NOT interpret the generic menu flag
// as ESC-only, and do NOT treat the engine clock pause as a menu flag.
std::atomic<int> g_audio_last_ingame_menu{-1};
std::atomic<int> g_audio_last_engine_pause{-1};
volatile LONG g_audio_poll_guard=0;
bool executable_page(const void* addr) {
    MEMORY_BASIC_INFORMATION m{};
    if(!addr || VirtualQuery(addr,&m,sizeof(m))!=sizeof(m) ||
       m.State!=MEM_COMMIT || (m.Protect&PAGE_GUARD) ||
       (m.Protect&PAGE_NOACCESS))return false;
    switch(m.Protect&0xffu) {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:return true;
        default:return false;
    }
}
bool writable_audio_slot(const void* addr) {
    MEMORY_BASIC_INFORMATION m{};
    if(!addr || VirtualQuery(addr,&m,sizeof(m))!=sizeof(m) ||
       m.State!=MEM_COMMIT || (m.Protect&PAGE_GUARD))return false;
    switch(m.Protect&0xffu) {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:return true;
        default:return false;
    }
}
// Identify which original sound-driver module owns each callback in the
// research build. Module basename and RVA only: no user/game paths or raw
// process addresses, no DLL loading, no GetProcAddress, no game writes.
// An APMmxSVR/APMP5SVR distinction matters before we can prove the
// physical pause/resume callback cardinality.
void log_audio_driver_module(unsigned slot,const void* current) {
    if(!g_log || slot>=kAudioSlotCount || !current)return;
    HMODULE h=nullptr;
    if(!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(current),&h) || !h) {
        char msg[170]{};
        std::snprintf(msg,sizeof(msg),
            "RUNTIME-AUDIO-DRIVER: channel=%s module=unknown callback_rva=unknown (original forwarded; read-only)",
            kAudioSlots[slot].name);
        log_line(msg);
        return;
    }
    wchar_t wide[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(h,wide,MAX_PATH);
    char basename[80]{};
    const wchar_t* beginning=wide;
    if(length>0 && length<MAX_PATH) {
        for(DWORD i=0;i<length;++i)
            if(wide[i]==L'\\' || wide[i]==L'/')beginning=wide+i+1;
        unsigned index=0;
        for(const wchar_t* p=beginning;*p && index+1<sizeof(basename);++p)
            basename[index++]=(*p>=33 && *p<=126)
                                ? char(*p) : '?';
        basename[index]='\0';
    }
    if(!basename[0])std::snprintf(basename,sizeof(basename),"unknown");
    const auto address=reinterpret_cast<std::uintptr_t>(current);
    const auto module_base=reinterpret_cast<std::uintptr_t>(h);
    const unsigned rva=address>=module_base ?
        unsigned(address-module_base) : 0xffffffffu;
    char msg[240]{};
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-AUDIO-DRIVER: channel=%s module=%s callback_rva=0x%08X (module basename only; original forwarded; read-only)",
        kAudioSlots[slot].name,basename,rva);
    log_line(msg);
}

// Owner's exact CD executable, source RVA=0x4580B0: the *global*
// pause/resume dispatcher walks 64 sound records, 0x20 bytes each,
// starting at 0x4B8C98. This maps an opaque driver argument back to a
// SOUND TABLE INDEX, not a song ID. The same dispatcher is called by
// Win32 activation code at 0x401C9C and by native script dispatcher
// at 0x471E70; a key press alone cannot distinguish those sources.
// This native subsystem has its own single-handle sound sequencer, distinct
// from the 64-entry sound table. The original x86 code at 0x459AEC stores
// the table slot returned by 0x458220 at 0x4B95E8, and 0x4597E0 clears
// its active flag at 0x4B95B8. Its role (music or other sequenced sound)
// is NOT established: observe whether the sequencer handle is the one
// paused during the Escape menu, without changing driver calls.
// Native 0x4574E0 returns the ADDRESS of a 32-byte sound record,
// not its ordinal. Native 0x459AF4 stores that address at 0x4B95E8.
// Code 0x459C49 passes the pointer directly to 0x4584A0.
// Never interpret this DWORD as a numeric slot or dereference it blindly.
struct AudioNestedRecordRef {
    int table_index=-1;
    // 0=unreadable/undetermined, 1=null, 2=minus-one sentinel,
    // 3=aligned pointer into native 64-record table, 4=invalid pointer.
    unsigned pointer_class=0;
};
AudioNestedRecordRef decode_audio_nested_record_ref(
    DWORD value, const BYTE* module) {
    AudioNestedRecordRef out{};
    if(value==0u) {out.pointer_class=1u;return out;}
    if(value==0xFFFFFFFFu) {out.pointer_class=2u;return out;}
    if(!module)return out;
    constexpr std::uintptr_t kSoundTableRva=0xB8C98;
    constexpr std::uintptr_t kRecordSize=0x20;
    constexpr std::uintptr_t kRecordCount=64;
    const auto base=reinterpret_cast<std::uintptr_t>(module)+kSoundTableRva;
    const auto address=static_cast<std::uintptr_t>(value);
    if(address<base || address>=base+kRecordCount*kRecordSize ||
       (address-base)%kRecordSize!=0u) {
        out.pointer_class=4u;
        return out;
    }
    out.pointer_class=3u;
    out.table_index=static_cast<int>((address-base)/kRecordSize);
    return out;
}
struct AudioSequenceContext {
    int table_index=-1;
    unsigned active=0;
    unsigned same_handle=0;
    // 0=unreadable, 1=zero, 2=one, 3=other nonzero.
    // The native type-11 callback at 0x459C40 accepts any nonzero state.
    unsigned state_class=0;
    unsigned nested_ref_class=0;
};
AudioSequenceContext inspect_audio_sequence_context(DWORD argument) {
    AudioSequenceContext out{};
    const auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!module)return out;
    const HANDLE process=GetCurrentProcess();
    const auto read_word=[&](std::uintptr_t rva,DWORD* value) {
        SIZE_T got=0;
        return ReadProcessMemory(process,module+rva,value,sizeof(*value),
                                 &got)!=0 && got==sizeof(*value);
    };
    constexpr std::uintptr_t kSequenceActiveRva=0xB95B8; // 0x4B95B8
    constexpr std::uintptr_t kSequenceSlotRva=0xB95E8;   // 0x4B95E8
    constexpr std::uintptr_t kSoundTableRva=0xB8C98;    // 0x4B8C98
    DWORD active=0,nested_ptr=0,handle=0;
    if(!read_word(kSequenceActiveRva,&active))return out;
    out.state_class=active==0u ? 1u : (active==1u ? 2u : 3u);
    if(active==0u || !read_word(kSequenceSlotRva,&nested_ptr))return out;
    const auto ref=decode_audio_nested_record_ref(nested_ptr,module);
    out.nested_ref_class=ref.pointer_class;
    if(ref.table_index<0 ||
       !read_word(kSoundTableRva+
                  std::uintptr_t(ref.table_index)*32u,&handle) ||
       !handle || handle==0xFFFFFFFFu)return out;
    out.table_index=ref.table_index;
    out.active=1;
    out.same_handle=unsigned(argument!=0u && argument==handle);
    return out;
}
// The original-CD 0x453F90 resolver does NOT compare a loaded resource
// record's Id (+0) with its bank/index lookup key. Recover its provenance
// instead by finding the exact record address inside the loaded 92-byte
// resource table of a descriptor on the native bank registry linked list.
// This is a *bounded*, *read-only*, research-build-only snapshot. A failure,
// cycle, ambiguous overlap or traversal cap yields UNKNOWN, never a bank ID.
struct AudioNativeBankOrigin {
    int bank_id=-1;
    int resource_index=-1;
    unsigned candidates=0;
    unsigned list_complete=0;
    unsigned nodes_scanned=0;
};
AudioNativeBankOrigin inspect_audio_native_bank_origin(
    std::uintptr_t resource_address) {
    AudioNativeBankOrigin out{};
    // Every early return must clear a provisional bank identity;
    // only exhaustive unique traversal may publish an actual bank ID.
    const auto fail_closed=[&]() {
        out.bank_id=-1;
        out.resource_index=-1;
        return out;
    };
    auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!module || resource_address<0x10000u)return fail_closed();
    const auto safe_read=[&](std::uintptr_t address,void* buffer,SIZE_T bytes) {
        SIZE_T read=0;
        return address>=0x10000u &&
            ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<const void*>(address),buffer,bytes,&read)!=0 &&
            read==bytes;
    };
    // Native 0x4538C0 reads first node at [0x514D50], compares [node+4]
    // with bank ID, then follows [node+0x3C]. Each node is 0x48 bytes
    // (0x4538E2). The bank-head pointer is at [node+0x24].
    constexpr std::uintptr_t kBankListRva=0x114D50; // VA 0x514D50
    constexpr unsigned kMaxBankNodes=512;
    constexpr std::uintptr_t kNativeResourceStride=92;
    DWORD node=0;
    if(!safe_read(reinterpret_cast<std::uintptr_t>(module)+kBankListRva,
                  &node,sizeof(node)))return fail_closed();
    DWORD visited[kMaxBankNodes]{};
    for(unsigned i=0;i<kMaxBankNodes;++i) {
        if(node==0u) {
            out.list_complete=1u;
            break;
        }
        if(node<0x10000u)return fail_closed();
        for(unsigned k=0;k<i;++k)
            if(visited[k]==node)return fail_closed(); // cycle: fail closed
        visited[i]=node;
        DWORD descriptor[16]{};
        if(!safe_read(node,descriptor,sizeof(descriptor)))return fail_closed();
        ++out.nodes_scanned;
        // ACP_Ray2 SND_BANK_Loaded=1. Ignore incomplete/transitioning
        // banks: no claim about the presence of their resources.
        const DWORD state=descriptor[8];
        const DWORD head_ptr=descriptor[9];
        if(state==1u) {
            // An invalid loaded bank invalidates the entire scan. We
            // must not assert unique bank provenance if another loaded
            // node could have contained a matching resource record.
            if(head_ptr<0x10000u)return fail_closed();
            DWORD head[5]{};
            if(!safe_read(head_ptr,head,sizeof(head)))return fail_closed();
            // Native 0x453F90 checks [bank_head+0x10] count and uses
            // [bank_head+0x0C] first resource pointer + index*92.
            const std::uintptr_t first=head[3];
            const DWORD count=head[4];
            if(count>0x10000u || (count!=0u && first<0x10000u))
                return fail_closed();
            if(count!=0u && resource_address>=first) {
                const std::uintptr_t difference=resource_address-first;
                if(difference%kNativeResourceStride==0u &&
                   difference/kNativeResourceStride<count) {
                    ++out.candidates;
                    if(out.candidates==1u && descriptor[1]<=0xFFFFu) {
                        out.bank_id=static_cast<int>(descriptor[1]);
                        out.resource_index=static_cast<int>(
                            difference/kNativeResourceStride);
                    }
                }
            }
        }
        node=descriptor[15];
    }
    // Complete traversal and exactly one matching loaded bank are both
    // required; no provenance is asserted from an unbounded/cyclic list.
    if(out.list_complete==0u || out.candidates!=1u) {
        out.bank_id=-1;
        out.resource_index=-1;
    }
    return out;
}

// Read-only, source-backed link: a SND_E_TR_Theme=11 outer sound
// record's driver handle is the singleton controller address 0x4B95B8.
// Its +0x30 field is an *inner RECORD POINTER*; that record owns the
// native SXD driver handle. Neither an isolated SXD handle nor a fixed
// sound-table slot is sufficient to prove Theme ownership.
// Resource.Id upper/lower halves are *not* authenticated bank/index keys.
struct AudioThemeOwnerContext {
    unsigned outer_candidates=0;
    int outer_slot=-1;
    int theme_bank=-1;
    int theme_resource=-1;
    int inner_slot=-1;
    unsigned inner_ref_class=0;
    int inner_bank=-1;
    int inner_resource=-1;
    unsigned inner_driver_matches=0;
    unsigned inner_active=0;
    unsigned inner_transition=0;
    unsigned theme_owned_driver=0;
    // Native 0x459760 reads active Theme player at 0x4B95B8, and
    // 0x45997B compares the current Theme lookup key at 0x4B95CC.
    // -1 unknown; 0 player key differs; 1 exact native bank/index key.
    int native_theme_player_key_matches_outer=-1;
    // Native 0x45997B compares controller+0x14 to a Theme descriptor
    // POINTER, not to the origin-bank/index of the serialized resource.
    // -1=unknown, 0=other descriptor, 1=matching current Theme pointer.
    int native_theme_player_ref_matches_outer=-1;
    AudioNativeBankOrigin outer_bank_provenance{};
    AudioNativeBankOrigin inner_bank_provenance{};
};
AudioThemeOwnerContext inspect_audio_theme_owner(DWORD argument) {
    AudioThemeOwnerContext out{};
    auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!module)return out;
    const auto safe_read=[&](const void* address,void* bytes,SIZE_T size) {
        SIZE_T read=0;
        return ReadProcessMemory(GetCurrentProcess(),address,bytes,size,&read)!=0 &&
               read==size;
    };
    constexpr std::uintptr_t kControllerRva=0xB95B8; // 0x4B95B8
    constexpr std::uintptr_t kNestedSlotRva=0xB95E8; // 0x4B95E8
    constexpr std::uintptr_t kSoundTableRva=0xB8C98; // 0x4B8C98
    struct Record { DWORD words[8]; };
    Record table[64]{};
    DWORD active=0,nested_ptr=0;
    if(!safe_read(module+kControllerRva,&active,sizeof(active)) ||
       !safe_read(module+kNestedSlotRva,&nested_ptr,sizeof(nested_ptr)) ||
       !safe_read(module+kSoundTableRva,table,sizeof(table)))return out;
    const DWORD singleton=static_cast<DWORD>(
        reinterpret_cast<std::uintptr_t>(module)+kControllerRva);
    // A matching outer record must be active and point to an actual
    // SND_tdstBlockResourceMem of resource type 11 (Theme).
    std::uintptr_t outer_descriptor_address=0;
    for(unsigned i=0;i<64u;++i) {
        if(table[i].words[0]!=singleton || table[i].words[5]==0u)
            continue;
        const std::uintptr_t descriptor=table[i].words[1];
        DWORD header[2]{};
        if(descriptor<0x10000u ||
           !safe_read(reinterpret_cast<const void*>(descriptor),
                      header,sizeof(header)) || header[1]!=11u)
            continue;
        ++out.outer_candidates;
        if(out.outer_candidates==1u) {
            out.outer_slot=static_cast<int>(i);
            // These are the UNVERIFIED halves of the record's Id field,
            // *not* the original loaded bank ID and table index.
            out.theme_bank=static_cast<int>(header[0]>>16);
            out.theme_resource=static_cast<int>(header[0]&0xFFFFu);
            outer_descriptor_address=descriptor;
        }
    }
    if(out.outer_candidates!=1u) {
        out.outer_slot=-1;
        out.theme_bank=-1;
        out.theme_resource=-1;
        return out; // ambiguous or absent Theme: never claim ownership
    }
    out.outer_bank_provenance=inspect_audio_native_bank_origin(
        outer_descriptor_address);
    if(active==1u &&
       out.outer_bank_provenance.bank_id>=0 &&
       out.outer_bank_provenance.resource_index>=0) {
        constexpr std::uintptr_t kThemePlayerCurrentKeyRva=0xB95CC;
        DWORD current_theme_key=0;
        if(safe_read(module+kThemePlayerCurrentKeyRva,
                     &current_theme_key,sizeof(current_theme_key))) {
            const DWORD expected_key=
                (DWORD(out.outer_bank_provenance.bank_id)<<16) |
                DWORD(out.outer_bank_provenance.resource_index);
            out.native_theme_player_key_matches_outer=
                int(current_theme_key==expected_key);
            // 0x457F21 passes the original Theme resource descriptor to
            // 0x459950; 0x45997B compares that same incoming pointer with
            // [0x4B95CC]. Protect against pointer reuse by validating
            // the active type-11 record and loaded BNM origin each time.
            out.native_theme_player_ref_matches_outer=
                int(current_theme_key==
                    static_cast<DWORD>(outer_descriptor_address));
        }
    }
    if(active==0u)return out;
    const auto ref=decode_audio_nested_record_ref(nested_ptr,module);
    out.inner_ref_class=ref.pointer_class;
    if(ref.table_index<0)return out;
    out.inner_slot=ref.table_index;
    const auto& inner=table[ref.table_index];
    out.inner_active=unsigned(inner.words[5]!=0u);
    out.inner_transition=unsigned(inner.words[4]!=0u);
    const DWORD inner_handle=inner.words[0];
    if(inner_handle==0u || inner_handle==0xFFFFFFFFu ||
       !out.inner_active || !out.inner_transition)return out;
    for(const auto& record:table)
        if(record.words[0]==inner_handle)
            ++out.inner_driver_matches;
    const std::uintptr_t inner_desc=inner.words[1];
    DWORD inner_header[2]{};
    if(inner_desc<0x10000u ||
       !safe_read(reinterpret_cast<const void*>(inner_desc),
                  inner_header,sizeof(inner_header)) ||
       inner_header[1]!=1u)return out; // SXD sample, not MIDI
    out.inner_bank=static_cast<int>(inner_header[0]>>16);
    out.inner_resource=static_cast<int>(inner_header[0]&0xFFFFu);
    out.inner_bank_provenance=inspect_audio_native_bank_origin(inner_desc);
    out.theme_owned_driver=unsigned(
        argument!=0u && argument==inner_handle &&
        out.inner_driver_matches==1u);
    return out;
}
struct AudioSoundContext {
    int table_index=-1;
    unsigned match_count=0; // A driver pointer may be shared by sound table entries.
    unsigned transport=0;
    unsigned transition=0;
    unsigned active=0;
    unsigned descriptor_matches_play=0;
    unsigned descriptor_words_readable=0;
    unsigned descriptor_words_equal_mask=0;
    unsigned descriptor_volume_byte=256;
    unsigned play_volume_byte=256;
    // ACP_Ray2 SND_tdstBlockResourceMem: Id/eType/eStorage/ucVolume.
    // The first DWORD is a native resource record Id, never a pointer;
    // its two halves need not equal the owning bank and table index.
    int descriptor_bank_group=-1;
    int descriptor_record_index=-1;
    int play_bank_group=-1;
    int play_record_index=-1;
    unsigned descriptor_resource_id_match=0;
    int pause_depth=-1;
};
AudioSoundContext inspect_audio_sound_context(DWORD argument) {
    AudioSoundContext out{};
    auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!module)return out;
    // No dereference into a dynamic driver and no raw pointer logging.
    const HANDLE process=GetCurrentProcess();
    const auto safe_read=[&](const void* address,void* bytes,SIZE_T size) {
        SIZE_T got=0;
        return ReadProcessMemory(process,address,bytes,size,&got)!=0 && got==size;
    };
    constexpr std::uintptr_t kPauseDepthRva=0xA3CC8; // 0x4A3CC8
    DWORD depth=0;
    if(safe_read(module+kPauseDepthRva,&depth,sizeof(depth)) && depth<=16u)
        out.pause_depth=static_cast<int>(depth);
    constexpr std::uintptr_t kSoundTableRva=0xB8C98; // 0x4B8C98
    constexpr unsigned kSoundTableSlots=64u;
    struct SoundTableRecord { DWORD words[8]; };
    SoundTableRecord entries[kSoundTableSlots]{};
    if(!argument || !safe_read(module+kSoundTableRva,
       entries,sizeof(entries)))return out;
    const DWORD last_descriptor=g_last_transition_play_descriptor.load(
        std::memory_order_acquire);
    for(unsigned i=0;i<kSoundTableSlots;++i) {
        if(entries[i].words[0]!=argument)continue;
        ++out.match_count;
        // Never claim the first matching index is unique. Shared driver
        // handles can represent several game records and must be treated
        // as ambiguous when identifying music versus sound effects.
        if(out.table_index>=0)continue;
        out.table_index=static_cast<int>(i);
        // These fields are read-only observations of the game's own sound
        // table. Their interpretation is not used to change playback.
        out.transition=unsigned(entries[i].words[4]!=0);
        out.active=unsigned(entries[i].words[5]!=0);
        out.descriptor_matches_play=unsigned(last_descriptor!=0u &&
            entries[i].words[1]==last_descriptor &&
            out.transition!=0u);
        // The original game can reuse a driver handle for another sound
        // descriptor. Compare four bounded DWORDs to the play-time snapshot
        // without treating a matching field as proof of soundtrack identity.
        DWORD current_words[4]{};
        const std::uintptr_t desc_addr=
            std::uintptr_t(entries[i].words[1]);
        if(desc_addr>=0x10000u && g_last_transition_desc_readable.load(
                std::memory_order_acquire)!=0u &&
           safe_read(reinterpret_cast<const void*>(desc_addr),
                     current_words,sizeof(current_words))) {
            out.descriptor_words_readable=1u;
            for(unsigned k=0;k<4u;++k)
                if(current_words[k]==g_last_transition_desc_words[k].load(
                        std::memory_order_acquire))
                    out.descriptor_words_equal_mask|=1u<<k;
            // Word 3 includes the native +12 volume byte. A difference
            // in its other three bytes must not be called a volume change.
            out.descriptor_volume_byte=current_words[3]&0xFFu;
            out.play_volume_byte=g_last_transition_desc_words[3].load(
                std::memory_order_acquire)&0xFFu;
            // The original resource ID is a packed bank/record key,
            // as verified against ACP_Ray2 SND_tdstBlockResourceMem and
            // native 0x453F90. Never dereference that ID as an address.
            const DWORD old_id=g_last_transition_desc_words[0].load(
                std::memory_order_acquire);
            const DWORD now_id=current_words[0];
            out.play_bank_group=int(old_id>>16);
            out.play_record_index=int(old_id&0xFFFFu);
            out.descriptor_bank_group=int(now_id>>16);
            out.descriptor_record_index=int(now_id&0xFFFFu);
            out.descriptor_resource_id_match=unsigned(old_id==now_id);
        }
        DWORD type=0;
        const std::uintptr_t desc=std::uintptr_t(entries[i].words[1]);
        if(desc>=0x10000u && safe_read(
            reinterpret_cast<const void*>(desc+4u),&type,sizeof(type)) &&
            type>=1u && type<=2u)
            out.transport=type;
    }
    // Shared handles cannot identify a unique native source descriptor.
    if(out.match_count!=1u) {
        out.descriptor_matches_play=0u;
        out.descriptor_words_readable=0u;
        out.descriptor_words_equal_mask=0u;
        out.descriptor_volume_byte=256u;
        out.play_volume_byte=256u;
        out.descriptor_bank_group=-1;
        out.descriptor_record_index=-1;
        out.play_bank_group=-1;
        out.play_record_index=-1;
        out.descriptor_resource_id_match=0u;
    }
    return out;
}


// The native sound pause dispatcher can be entered either from an in-game
// script (CD script procedure 426/427) or Windows WM_ACTIVATE handling.
// Determine the *executed call path* from original retail-CD return sites,
// not a heuristic based on the Escape key being held. This is a bounded,
// read-only stack snapshot; "unknown" never authorizes a sound bypass.
__declspec(noinline) const char* inspect_audio_pause_origin(
    char* code_rva_preview,SIZE_T preview_capacity,unsigned* scanned_words,
    unsigned* wrapper_returns,unsigned* confirmed_pairs) {
    if(code_rva_preview && preview_capacity)code_rva_preview[0]='\0';
    if(scanned_words)*scanned_words=0;
    if(wrapper_returns)*wrapper_returns=0;
    if(confirmed_pairs)*confirmed_pairs=0;
    const auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!module)return "unknown";
    auto* stack=reinterpret_cast<const DWORD*>(_AddressOfReturnAddress());
    MEMORY_BASIC_INFORMATION page{};
    if(VirtualQuery(stack,&page,sizeof(page))!=sizeof(page) ||
       page.State!=MEM_COMMIT || (page.Protect&PAGE_GUARD) ||
       (page.Protect&PAGE_NOACCESS))return "unknown";
    const std::uintptr_t start=reinterpret_cast<std::uintptr_t>(stack);
    const std::uintptr_t region_end=
        reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize;
    if(region_end<=start)return "unknown";
    // A 96-DWORD window found no script/activation return site during
    // three verified ESC cycles. Allow 1,024 DWORDs (4 KiB) but never
    // read past the first committed stack region.
    constexpr SIZE_T kMaxStackWords=1024;
    const SIZE_T bytes=region_end-start<sizeof(DWORD)*kMaxStackWords ?
        static_cast<SIZE_T>(region_end-start) : sizeof(DWORD)*kMaxStackWords;
    const SIZE_T count=bytes/sizeof(DWORD);
    if(count==0)return "unknown";
    DWORD words[kMaxStackWords]{};
    SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),stack,words,
                          count*sizeof(DWORD),&got) ||
       got!=count*sizeof(DWORD))return "unknown";
    if(scanned_words)*scanned_words=unsigned(count);
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    // The approved CD's executable .text spans RVA 0x1000..0x9B1D0.
    // Sample only the first four return-address *candidates* as RVAs,
    // once or twice per channel. These are not proof of valid frames.
    constexpr std::uintptr_t kCodeStartRva=0x1000;
    constexpr std::uintptr_t kCodeEndRva=0x9B1D0;
    unsigned preview_count=0;
    SIZE_T preview_used=0;
    // The original CD wrapper at 0x4536D0 calls the global pause
    // dispatcher at 0x4536ED and leaves 0x4536F2 on the stack;
    // the resume wrapper leaves 0x453722. In both original wrappers
    // these returns are directly adjacent to the *outer* caller's
    // script/activation return address. A standalone 0x471E75 left
    // in recycled stack memory must NEVER be accepted as provenance.
    constexpr std::uintptr_t kWrapperPauseReturnRva=0x536F2;
    constexpr std::uintptr_t kWrapperResumeReturnRva=0x53722;
    constexpr std::uintptr_t kScriptPauseReturnRva=0x71E75;
    constexpr std::uintptr_t kScriptResumeReturnRva=0x71E6C;
    constexpr std::uintptr_t kWindowsPauseReturnRva=0x01CA1;
    constexpr std::uintptr_t kWindowsResumeReturnRva=0x01CEE;
    unsigned kind=0; // 1=script pause, 2=script resume,
                     // 3=Windows pause, 4=Windows resume
    unsigned roots=0,pairs=0;
    for(SIZE_T i=0;i<count;++i) {
        const auto v=std::uintptr_t(words[i]);
        if(code_rva_preview && preview_capacity>preview_used+10 &&
           v>=base+kCodeStartRva && v<base+kCodeEndRva &&
           preview_count<4u) {
            const int n=std::snprintf(code_rva_preview+preview_used,
                preview_capacity-preview_used,"%s%05X",
                preview_count ? "," : "",
                unsigned(v-base));
            if(n>0 && SIZE_T(n)<preview_capacity-preview_used) {
                preview_used+=SIZE_T(n);
                ++preview_count;
            }
        }
        unsigned match=0;
        // The second DWORD must be the caller of the verified wrapper.
        // This adjacent pair avoids false positives from old unrelated
        // return addresses in a stack snapshot.
        if(i+1<count && v==base+kWrapperPauseReturnRva) {
            ++roots;
            const auto caller=std::uintptr_t(words[i+1]);
            if(caller==base+kScriptPauseReturnRva)match=1;
            else if(caller==base+kWindowsPauseReturnRva)match=3;
        } else if(i+1<count && v==base+kWrapperResumeReturnRva) {
            ++roots;
            const auto caller=std::uintptr_t(words[i+1]);
            if(caller==base+kScriptResumeReturnRva)match=2;
            else if(caller==base+kWindowsResumeReturnRva)match=4;
        }
        if(match) {
            ++pairs;
            if(kind && kind!=match)kind=5; // conflicting pairs
            else if(!kind)kind=match;
        }
    }
    if(wrapper_returns)*wrapper_returns=roots;
    if(confirmed_pairs)*confirmed_pairs=pairs;
    if(pairs!=1 || kind==5)return pairs>1 ? "ambiguous" : "unknown";
    switch(kind) {
        case 1:return "game-script-pause";
        case 2:return "game-script-resume";
        case 3:return "windows-activation-pause";
        case 4:return "windows-activation-resume";
        default:return "unknown";
    }
}

DWORD __stdcall audio_play_transition_ex(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e) {
    const auto original=reinterpret_cast<AudioPlayTransitionExFn>(
        g_transition_play_original.load(std::memory_order_acquire));
    // The five native arguments and DWORD result are forwarded exactly.
    // Neither music nor SFX identity is assumed from this API alone.
    const DWORD result=original ? original(a,b,c,d,e) : 0xFFFFFFFFu;
    if(result && result!=0xFFFFFFFFu) {
        // The original CD at 0x4582A4 passes the source descriptor
        // to the driver. Record a 16-byte snapshot, not an asserted
        // song ID: a driver handle can outlive its originating source.
        DWORD words[4]{};
        SIZE_T got=0;
        const bool readable=a>=0x10000u && ReadProcessMemory(
            GetCurrentProcess(),reinterpret_cast<const void*>(
                std::uintptr_t(a)),words,sizeof(words),&got)!=0 &&
                got==sizeof(words);
        g_last_transition_desc_readable.store(0u,std::memory_order_release);
        for(unsigned k=0;k<4u;++k)
            g_last_transition_desc_words[k].store(
                readable ? words[k] : 0u,std::memory_order_relaxed);
        g_last_transition_play_descriptor.store(a,std::memory_order_release);
        g_last_transition_play_handle.store(result,std::memory_order_release);
        g_last_transition_desc_readable.store(
            unsigned(readable),std::memory_order_release);
    }
    const unsigned event=g_transition_play_calls.fetch_add(
        1u,std::memory_order_relaxed);
    if(g_log && event<16u) {
        // No raw pointer or sound payload is logged. On the approved
        // CD executable, descriptor+4 is the transport type (1=SXD,
        // 2=MIDI), and descriptor+12 is the native volume byte.
        // The latter is observational metadata, not a music selector.
        struct NativeDesc { DWORD id; DWORD transport; DWORD extra; BYTE volume; };
        NativeDesc desc{};
        SIZE_T got=0;
        const bool readable=a>=0x10000u && ReadProcessMemory(
            GetCurrentProcess(),reinterpret_cast<const void*>(
                std::uintptr_t(a)),&desc,sizeof(desc),&got)!=0 &&
                got==sizeof(desc);
        char line[255]{};
        std::snprintf(line,sizeof(line),
            "RUNTIME-AUDIO-TRANSITION-PLAY: sample=%u handle_valid=%u "
            "descriptor_readable=%u descriptor_transport=%u "
            "descriptor_volume=%d (five args/result forwarded unchanged; "
            "not music-identified)",
            event+1u,unsigned(result && result!=0xFFFFFFFFu),
            unsigned(readable),
            unsigned(readable && desc.transport<=2u ? desc.transport : 0u),
            readable ? int(desc.volume) : -1);
        log_line(line);
    }
    return result;
}

#if RAYMAN2FIX_ESC_MUSIC_PROTO
// An explicitly opted-in research build ONLY. The game keeps ownership of
// the pause depth and both sound-record marks. Rebalancing the observed
// paired Theme/SXD driver callbacks is deliberately experimental, not a
// confirmed contract for every APM backend.
namespace proto=r2fix::esc_proto;
std::atomic<bool> g_proto_enabled{false};
std::atomic<bool> g_proto_callsites_installed{false};
std::atomic<unsigned> g_proto_generation{0};
std::atomic<DWORD> g_proto_pending_at{0};
std::mutex g_proto_mutex;
proto::Coordinator g_proto;
thread_local proto::Origin g_proto_origin=proto::Origin::unknown;
class ProtoScope {
public:
    explicit ProtoScope(proto::Origin origin) noexcept : old_(g_proto_origin) {
        g_proto_origin=origin;
    }
    ~ProtoScope(){g_proto_origin=old_;}
private:
    proto::Origin old_;
};
void proto_log(const char* action,const char* reason="none") {
    if(!g_proto_enabled.load(std::memory_order_acquire) || !g_log)return;
    char msg[215]{};
    unsigned phase=0;
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        phase=unsigned(g_proto.phase());
    }
    std::snprintf(msg,sizeof(msg),
        "RUNTIME-ESC-PROTOTYPE: event=%s phase=%u detail=%s (experimental; original soundtrack only)",
        action,phase,reason);
    log_line(msg);
}
bool proto_game_read(std::uintptr_t rva,BYTE& out) {
    const auto* module=static_cast<const BYTE*>(
        reinterpret_cast<const void*>(GetModuleHandleW(nullptr)));
    if(!module)return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),module+rva,&out,1,&read)!=0 &&
           read==1 && out<=1;
}
bool proto_read_depth(DWORD& out) {
    const auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!module)return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),module+0xA3CC8,
        &out,sizeof(out),&read)!=0 && read==sizeof(out) && out<=16u;
}
proto::Identity proto_identity(DWORD handle,std::uint64_t generation) {
    proto::Identity id{};
    if(!handle || handle==0xffffffffu || !generation)return id;
    const auto owner=inspect_audio_theme_owner(handle);
    if(owner.theme_owned_driver!=1u ||
       owner.native_theme_player_ref_matches_outer!=1 ||
       owner.outer_bank_provenance.list_complete!=1u ||
       owner.inner_bank_provenance.list_complete!=1u ||
       owner.outer_bank_provenance.candidates!=1u ||
       owner.inner_bank_provenance.candidates!=1u ||
       owner.outer_bank_provenance.bank_id<0 ||
       owner.inner_bank_provenance.bank_id<0 ||
       owner.outer_bank_provenance.resource_index<0 ||
       owner.inner_bank_provenance.resource_index<0)return id;
    id.handle=handle;
    id.native_theme_key=(DWORD(owner.outer_bank_provenance.bank_id)<<16) |
        DWORD(owner.outer_bank_provenance.resource_index);
    id.native_inner_key=(DWORD(owner.inner_bank_provenance.bank_id)<<16) |
        DWORD(owner.inner_bank_provenance.resource_index);
    id.generation=generation;
    return id;
}
// Called on the original game thread, *outside* a native driver hook.
// Every physical operation rechecks the live generation before contacting
// a reused handle. These stdcall functions return void: no success proof.
void proto_dispatch(proto::Dispatch action) {
    if(!action || !g_proto_enabled.load(std::memory_order_acquire))return;
    if(action.count!=2 || !action.handle || !action.generation) {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        g_proto.quarantine();
        return;
    }
    const unsigned slot=action.command==proto::Command::pause ? 2u : 3u;
    const auto fn=reinterpret_cast<AudioPauseFn>(
        g_audio_original[slot].load(std::memory_order_acquire));
    if(!fn) {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        g_proto.quarantine();
        return;
    }
    for(unsigned i=0;i<action.count;++i) {
        {
            const std::lock_guard<std::mutex> lock(g_proto_mutex);
            const bool authorized=g_proto.current(action.generation) ||
                (action.command==proto::Command::pause &&
                 g_proto.safety_current(action.generation));
            if(!authorized || g_proto.current_handle()!=action.handle)return;
        }
        if(!proto_identity(action.handle,action.generation).valid()) {
            const std::lock_guard<std::mutex> lock(g_proto_mutex);
            g_proto.quarantine();
            return;
        }
        fn(action.handle);
    }
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        if(action.command==proto::Command::resume)
            g_proto.complete_focus_recovery(action.generation);
    }
    proto_log(action.command==proto::Command::pause ?
        "driver-pause-batch" : "driver-resume-batch","paired-calls=2");
}
void proto_deferred_safety() {
    proto::Dispatch d{};
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        d=g_proto.take_safety_pause();
    }
    proto_dispatch(d);
}
using NativeAudioWrapper=void (__cdecl*)();
void proto_call_original(std::uintptr_t rva) {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!base)return;
    reinterpret_cast<NativeAudioWrapper>(base+rva)();
}
void __cdecl proto_script_pause() {
    bool adopted=false;
    if(g_proto_enabled.load(std::memory_order_acquire)) {
        BYTE menu=0;
        DWORD depth=0;
        const bool candidate=game_window_is_foreground() &&
            (GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0 &&
            proto_game_read(0xB9BC8,menu) && menu==0 &&
            proto_read_depth(depth) && depth==1;
        if(candidate) {
            const std::lock_guard<std::mutex> lock(g_proto_mutex);
            adopted=g_proto.begin_script_pause(true,true,true);
            if(adopted) {
                g_proto_generation.fetch_add(1,std::memory_order_acq_rel);
                g_proto_pending_at.store(GetTickCount(),std::memory_order_release);
            }
        }
    }
    {
        ProtoScope scope(proto::Origin::script_pause);
        proto_call_original(0x536D0); // unchanged verified native wrapper
    }
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        if(adopted)g_proto.end_script_pause();
        else if(g_proto.phase()==proto::Phase::audible_menu)
            g_proto.quarantine(); // independent nested script pause
    }
    proto_deferred_safety();
    if(adopted)proto_log("script-pause-observed","await-native-menu-flag");
}
void __cdecl proto_script_resume() {
    {
        ProtoScope scope(proto::Origin::script_resume);
        proto_call_original(0x53700);
    }
    if(g_proto_enabled.load(std::memory_order_acquire)) {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        g_proto.end_script_resume();
    }
    proto_deferred_safety();
}
void __cdecl proto_focus_pause() {
    {
        ProtoScope scope(proto::Origin::focus_pause);
        proto_call_original(0x536D0);
    }
    proto::Dispatch d{};
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        d=g_proto.focus_pause();
    }
    proto_dispatch(d);
    proto_deferred_safety();
}
void __cdecl proto_focus_resume() {
    {
        ProtoScope scope(proto::Origin::focus_resume);
        proto_call_original(0x53700);
    }
    proto::Dispatch d{};
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        d=g_proto.focus_resume();
    }
    proto_dispatch(d);
    proto_deferred_safety();
}
void proto_record_pause(DWORD handle) {
    const unsigned generation=g_proto_generation.load(std::memory_order_acquire);
    const proto::Identity id=proto_identity(handle,generation);
    const std::lock_guard<std::mutex> lock(g_proto_mutex);
    g_proto.observe_native_pause(id,id.valid());
}
bool proto_allow_resume(DWORD handle) {
    const std::lock_guard<std::mutex> lock(g_proto_mutex);
    return g_proto.native_resume(handle,g_proto_origin);
}
void proto_record_stop(DWORD handle) {
    const std::lock_guard<std::mutex> lock(g_proto_mutex);
    g_proto.native_stop(handle);
}
// The original CD's four independent E8 callers carry source provenance.
// They must all match exact approved instruction bytes or NO caller is
// patched. This code never modifies the on-disk game executable.
struct ProtoCaller {
    std::uintptr_t rva;
    BYTE expected[5];
    NativeAudioWrapper replacement;
};
const ProtoCaller kProtoCallers[4]={
    {0x1C9C,{0xE8,0x2F,0x1A,0x05,0x00},proto_focus_pause},
    {0x1CE9,{0xE8,0x12,0x1A,0x05,0x00},proto_focus_resume},
    {0x71E70,{0xE8,0x5B,0x18,0xFE,0xFF},proto_script_pause},
    {0x71E67,{0xE8,0x94,0x18,0xFE,0xFF},proto_script_resume}
};
bool proto_write_caller(BYTE* at,const BYTE* bytes) {
    DWORD old=0;
    if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,bytes,5);
    FlushInstructionCache(GetCurrentProcess(),at,5);
    DWORD unused=0;
    VirtualProtect(at,5,old,&unused);
    return true;
}
void proto_install_callsites() {
    if(!g_proto_enabled.load(std::memory_order_acquire) ||
       g_proto_callsites_installed.load(std::memory_order_acquire))return;
    // Never route native game events to the research adapter until BOTH
    // transition driver callbacks and the two Stop paths are installed.
    // Otherwise an early script pause could acquire an owner that cannot
    // be resumed or retired safely by our explicit bridge.
    for(const unsigned slot: {2u,3u,10u,11u})
        if(!g_audio_original[slot].load(std::memory_order_acquire))
            return;
    auto* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if(!base)return;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 ||
       dos->e_lfanew>=0x1000)return;
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       pe->FileHeader.TimeDateStamp!=kOriginalTimestamp ||
       pe->OptionalHeader.SizeOfImage<0xB9BC9)return;
    BYTE replacement[4][5]{};
    for(unsigned i=0;i<4;++i) {
        const auto& entry=kProtoCallers[i];
        if(std::memcmp(base+entry.rva,entry.expected,5)!=0) {
            proto_log("caller-guard-rejected","original-code-bytes-mismatch");
            return;
        }
        replacement[i][0]=0xE8;
        const auto at=reinterpret_cast<std::intptr_t>(base+entry.rva+5);
        const auto to=reinterpret_cast<std::intptr_t>(entry.replacement);
        const std::int64_t distance=std::int64_t(to)-std::int64_t(at);
        if(distance<INT32_MIN || distance>INT32_MAX) {
            proto_log("caller-guard-rejected","hook-out-of-rel32-range");
            return;
        }
        const std::int32_t relative=static_cast<std::int32_t>(distance);
        std::memcpy(replacement[i]+1,&relative,sizeof(relative));
    }
    unsigned written=0;
    for(unsigned i=0;i<4;++i) {
        if(!proto_write_caller(base+kProtoCallers[i].rva,replacement[i]))break;
        ++written;
    }
    if(written!=4) {
        while(written) {
            --written;
            proto_write_caller(base+kProtoCallers[written].rva,
                               kProtoCallers[written].expected);
        }
        proto_log("caller-guard-rejected","failed-write-rolled-back");
        return;
    }
    g_proto_callsites_installed.store(true,std::memory_order_release);
    proto_log("source-hooks-installed","script-and-win32-focus-separated");
}
bool proto_esc_menu_actor_open();
void proto_menu_tick() {
    if(!g_proto_callsites_installed.load(std::memory_order_acquire))return;
    // The approved game's loaded-bank traversal is bounded but nontrivial;
    // do not rescan every 16ms on the render thread.
    static DWORD last_probe=0;
    const DWORD now=GetTickCount();
    if(last_probe && DWORD(now-last_probe)<75u)return;
    last_probe=now;
    proto::Phase phase=proto::Phase::idle;
    DWORD started=0;
    std::uint32_t handle=0;
    std::uint64_t generation=0;
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        phase=g_proto.phase();
        handle=g_proto.current_handle();
        generation=g_proto_generation.load(std::memory_order_relaxed);
        started=g_proto_pending_at.load(std::memory_order_relaxed);
    }
    if(phase==proto::Phase::idle)return;
    if(phase==proto::Phase::pending_menu &&
       DWORD(GetTickCount()-started)>1500u) {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        if(g_proto.phase()==proto::Phase::pending_menu)g_proto.reset();
        return;
    }
    BYTE menu=0;
    const bool known=proto_game_read(0xB9BC8,menu);
    // The generic menu byte is NOT ESC-specific. This is a second
    // independent observation of the live actor YAM_GenMenuMap_I1
    // whose appearance at ESC was confirmed in the owner's Nov 14 log.
    // Only inspect it once on the pending->menu-open transition.
    const bool actor_ok=!(phase==proto::Phase::pending_menu &&
                          known && menu!=0) ||
                         proto_esc_menu_actor_open();
    const bool current=actor_ok &&
        proto_identity(handle,generation).valid();
    proto::Dispatch action{};
    {
        const std::lock_guard<std::mutex> lock(g_proto_mutex);
        action=g_proto.menu_tick(known && actor_ok,menu!=0,
                                 game_window_is_foreground(),current);
    }
    proto_dispatch(action);
    proto_deferred_safety();
}
#endif // RAYMAN2FIX_ESC_MUSIC_PROTO

void audio_trace_forward(unsigned slot,DWORD arg) {
    const auto original=reinterpret_cast<AudioPauseFn>(
        g_audio_original[slot].load(std::memory_order_acquire));
    // Count and bound diagnostics BEFORE the native driver call. Crucially,
    // do not scan 64 sound records for every sound event after budget
    // exhaustion: this research tracer must stay cheap during gameplay.
    const unsigned budget=(slot==0u || slot==1u) ? 64u : 16u;
    const auto event=g_audio_event_counts[slot].fetch_add(
        1u,std::memory_order_relaxed);
    const bool sampled=g_log && event<budget;
    // Capture table state BEFORE the original stop/pause might invalidate
    // an audio handle; never alter the arguments or call ordering.
    AudioSoundContext source{};
    AudioSequenceContext sequence{};
    AudioThemeOwnerContext theme{};
    const DWORD last_play=g_last_transition_play_handle.load(
        std::memory_order_acquire);
    const unsigned last_play_match=unsigned(
        arg!=0u && last_play!=0u && arg==last_play);
    const char* audio_origin="not-sampled";
    char code_rva_preview[96]{};
    unsigned scanned_words=0,wrapper_returns=0,confirmed_pairs=0;
    // Supplement the coarse UI poll with a BEFORE-driver-call snapshot.
    // Neither native flag by itself establishes a visible ESC menu.
    int in_game_menu_at_call=-1,engine_paused_at_call=-1;
    if(sampled) {
        auto* game_module=reinterpret_cast<const BYTE*>(
            GetModuleHandleW(nullptr));
        BYTE menu_byte=0,paused_byte=0;
        SIZE_T menu_read=0,pause_read=0;
        if(game_module &&
           ReadProcessMemory(GetCurrentProcess(),game_module+0xB9BC8,
                             &menu_byte,sizeof(menu_byte),&menu_read) &&
           menu_read==sizeof(menu_byte) && menu_byte<=1u)
            in_game_menu_at_call=int(menu_byte);
        if(game_module &&
           ReadProcessMemory(GetCurrentProcess(),game_module+0x100640,
                             &paused_byte,sizeof(paused_byte),&pause_read) &&
           pause_read==sizeof(paused_byte) && paused_byte<=1u)
            engine_paused_at_call=int(paused_byte);
        source=inspect_audio_sound_context(arg);
        sequence=inspect_audio_sequence_context(arg);
        if((slot==2u || slot==3u || slot==11u) && event<8u)
            theme=inspect_audio_theme_owner(arg);
        const bool preview=event<2u && slot<4u;
        audio_origin=inspect_audio_pause_origin(
            preview ? code_rva_preview : nullptr,
            preview ? sizeof(code_rva_preview) : 0u,
            preview ? &scanned_words : nullptr,
            preview ? &wrapper_returns : nullptr,
            preview ? &confirmed_pairs : nullptr);
    }
#if RAYMAN2FIX_ESC_MUSIC_PROTO
    bool forwarded=true;
    if(g_proto_enabled.load(std::memory_order_acquire) &&
       g_proto_callsites_installed.load(std::memory_order_acquire)) {
        if(slot==3u)forwarded=proto_allow_resume(arg);
        // A live SXD part may be stopped via either the generic or
        // transition-specific native entry. Stop always takes precedence
        // over experimental menu compensation.
        if(slot==10u || slot==11u)proto_record_stop(arg);
    }
    if(original && forwarded)original(arg);
    if(g_proto_enabled.load(std::memory_order_acquire) &&
       g_proto_callsites_installed.load(std::memory_order_acquire) &&
       slot==2u && g_proto_origin==proto::Origin::script_pause)
        proto_record_pause(arg);
    if(!forwarded)proto_log("native-resume-suppressed","verified-theme-only");
#else
    if(original) original(arg);
#endif
    if(!sampled)return;
    const DWORD origin=g_audio_origin.load(std::memory_order_acquire);
    char line[1024]{};
    std::snprintf(line,sizeof(line),
        "RUNTIME-AUDIO-TRACE: event=%s sample=%u t_ms=%lu arg_zero=%u sound_slot=%d sound_matches=%u sound_transport=%u sound_transition=%u sound_active=%u sequence_slot=%d sequence_active=%u sequence_state_class=%u sequence_ref_class=%u sequence_same_handle=%u transition_last_play_match=%u transition_desc_match=%u desc_words_readable=%u desc_words_equal_mask=%u desc_volume=%u play_volume=%u desc_resource_group=%d desc_resource_index=%d play_resource_group=%d play_resource_index=%d desc_resource_id_match=%u global_pause_depth=%d audio_origin=%s foreground=%u esc_down=%u ui_in_game_menu_at_call=%d ui_engine_paused_at_call=%d (read-only origin; forwarded unchanged; UI flag alone is not ESC proof)",
        kAudioSlots[slot].name,event+1u,
        static_cast<unsigned long>(GetTickCount()-origin),
        unsigned(arg==0),source.table_index,source.match_count,source.transport,
        source.transition,source.active,sequence.table_index,
        sequence.active,sequence.state_class,sequence.nested_ref_class,
        sequence.same_handle,last_play_match,
        source.descriptor_matches_play,source.descriptor_words_readable,
        source.descriptor_words_equal_mask,source.descriptor_volume_byte,
        source.play_volume_byte,source.descriptor_bank_group,
        source.descriptor_record_index,source.play_bank_group,
        source.play_record_index,source.descriptor_resource_id_match,
        source.pause_depth,audio_origin,
        unsigned(game_window_is_foreground()),
        unsigned((GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0),
        in_game_menu_at_call,engine_paused_at_call);
    log_line(line);
    if((slot==2u || slot==3u || slot==11u) && event<8u) {
        char ownership[768]{};
        std::snprintf(ownership,sizeof(ownership),
            "RUNTIME-AUDIO-THEME-OWNER: channel=%s sample=%u outer_candidates=%u outer_slot=%d theme_bank=%d theme_resource=%d inner_slot=%d inner_ref_class=%u inner_bank=%d inner_resource=%d inner_active=%u inner_transition=%u inner_handle_matches=%u theme_owned_driver=%u outer_native_bank=%d outer_native_index=%d outer_bank_candidates=%u outer_bank_list_complete=%u inner_native_bank=%d inner_native_index=%d inner_bank_candidates=%u inner_bank_list_complete=%u outer_bank_nodes=%u inner_bank_nodes=%u native_theme_player_key_matches_outer=%d native_theme_player_ref_matches_outer=%d (Id halves NOT bank provenance) (read-only; no music bypass)",
            kAudioSlots[slot].name,event+1u,theme.outer_candidates,
            theme.outer_slot,theme.theme_bank,theme.theme_resource,
            theme.inner_slot,theme.inner_ref_class,theme.inner_bank,
            theme.inner_resource,
            theme.inner_active,theme.inner_transition,
            theme.inner_driver_matches,theme.theme_owned_driver,
            theme.outer_bank_provenance.bank_id,
            theme.outer_bank_provenance.resource_index,
            theme.outer_bank_provenance.candidates,
            theme.outer_bank_provenance.list_complete,
            theme.inner_bank_provenance.bank_id,
            theme.inner_bank_provenance.resource_index,
            theme.inner_bank_provenance.candidates,
            theme.inner_bank_provenance.list_complete,
            theme.outer_bank_provenance.nodes_scanned,
            theme.inner_bank_provenance.nodes_scanned,
            theme.native_theme_player_key_matches_outer,
            theme.native_theme_player_ref_matches_outer);
        log_line(ownership);
    }
    if(event<2u && slot<4u) {
        char stack_line[270]{};
        std::snprintf(stack_line,sizeof(stack_line),
            "RUNTIME-AUDIO-CALLSTACK: channel=%s sample=%u inspected_words=%u wrapper_returns=%u paired_callers=%u executable_rva_candidates=%s (read-only; paired returns checked)",
            kAudioSlots[slot].name,event+1u,scanned_words,
            wrapper_returns,confirmed_pairs,
            code_rva_preview[0] ? code_rva_preview : "none");
        log_line(stack_line);
    }
}
void __stdcall audio_pause_sxd(DWORD a){audio_trace_forward(0,a);}
void __stdcall audio_resume_sxd(DWORD a){audio_trace_forward(1,a);}
void __stdcall audio_pause_transition_sxd(DWORD a){audio_trace_forward(2,a);}
void __stdcall audio_resume_transition_sxd(DWORD a){audio_trace_forward(3,a);}
void __stdcall audio_pause_midi(DWORD a){audio_trace_forward(4,a);}
void __stdcall audio_resume_midi(DWORD a){audio_trace_forward(5,a);}
void __stdcall audio_pause_transition_midi(DWORD a){audio_trace_forward(6,a);}
void __stdcall audio_resume_transition_midi(DWORD a){audio_trace_forward(7,a);}
void __stdcall audio_pause_cd(DWORD a){audio_trace_forward(8,a);}
void __stdcall audio_resume_cd(DWORD a){audio_trace_forward(9,a);}
void __stdcall audio_stop_sxd(DWORD a){audio_trace_forward(10,a);}
void __stdcall audio_stop_transition_sxd(DWORD a){audio_trace_forward(11,a);}
void __stdcall audio_set_sound_volume_sxd(DWORD a){audio_trace_forward(12,a);}
void __stdcall audio_stop_cd(DWORD a){audio_trace_forward(13,a);}
using AudioHook=void (__stdcall*)(DWORD);
constexpr AudioHook kAudioHooks[kAudioSlotCount]={
    audio_pause_sxd,audio_resume_sxd,
    audio_pause_transition_sxd,audio_resume_transition_sxd,
    audio_pause_midi,audio_resume_midi,
    audio_pause_transition_midi,audio_resume_transition_midi,
    audio_pause_cd,audio_resume_cd,
    audio_stop_sxd,audio_stop_transition_sxd,
    audio_set_sound_volume_sxd,audio_stop_cd
};
// The published ACP_Ray2 object helper looks up a personal-type name
// via the GAM object-type registry, then searches the direct Actor children
// of the live and inactive dynamic-world roots. Rayman2AP independently
// names YAM_GenMenuMap_I1 as the in-game escape-menu actor candidate.
// This probe faithfully reads *only* original-CD structs, never calls game
// functions, never mutates an actor and never treats presence as ESC proof.
// Unknown (-1) on any malformed or incomplete registry/world traversal.
struct AudioMenuActorSnapshot {
    int type_found=-1;
    int dynamic_actor=-1;
    int inactive_actor=-1;
    unsigned type_nodes=0;
    unsigned dynamic_nodes=0;
    unsigned inactive_nodes=0;
};
AudioMenuActorSnapshot inspect_audio_menu_actor_readonly() {
    AudioMenuActorSnapshot out{};
    auto* module=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if(!module)return out;
    const auto read=[&](std::uintptr_t address,void* dst,SIZE_T size) {
        SIZE_T n=0;
        return address>=0x10000u &&
            ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<const void*>(address),dst,size,&n)!=0 &&
            n==size;
    };
    // GAM_tdstObjectType = three 12-byte dynamic-list anchors.
    // Personal-type anchor is VA 0x5013F8, whose first DWORD is the
    // first element. The original game uses array index as the type ID,
    // and reads element+0x0C for its name (0x407B00..0x407B1C).
    constexpr std::uintptr_t kPersonalTypesRva=0x1013F8;
    constexpr std::uintptr_t kDynamicWorldRva=0x100FD0;
    constexpr std::uintptr_t kInactiveWorldRva=0x100FC4;
    constexpr unsigned kMaxTypeNodes=4096;
    static constexpr unsigned kMaxActorNodes=4096; // static so MSVC treats the lambda array bound as constant
    constexpr char kMenuPersonalType[]="YAM_GenMenuMap_I1";
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    DWORD anchor[3]{};
    if(!read(base+kPersonalTypesRva,anchor,sizeof(anchor)) ||
       anchor[2]==0u || anchor[2]>kMaxTypeNodes ||
       anchor[0]<0x10000u)return out;
    const auto list_va=base+kPersonalTypesRva;
    DWORD type_node=anchor[0],found_index=0;
    unsigned found=0;
    DWORD seen_type[kMaxTypeNodes]{};
    for(DWORD i=0;i<anchor[2];++i) {
        if(type_node<0x10000u)return out;
        for(DWORD j=0;j<i;++j)
            if(seen_type[j]==type_node)return out;
        seen_type[i]=type_node;
        DWORD elem[4]{}; // next,prev,parent,name
        if(!read(type_node,elem,sizeof(elem)) ||
           elem[2]!=list_va)return out;
        ++out.type_nodes;
        if(elem[3]>=0x10000u) {
            char name[sizeof(kMenuPersonalType)]{};
            if(!read(elem[3],name,sizeof(name)))return out;
            if(std::memcmp(name,kMenuPersonalType,sizeof(name))==0) {
                ++found;
                found_index=i;
                if(found>1u)return out; // duplicate types: unknown
            }
        } else return out;
        type_node=elem[0];
    }
    if(type_node!=0u)return out; // incomplete/changed list
    if(found==0u) {
        out.type_found=0; // scanned registry, but no candidate name
        return out;
    }
    out.type_found=1;
    // HIE_tdstSuperObject: type+0, linked actor+4, children-first+8,
    // next-sibling+0x14. HIE_tdstEngineObject standard-game pointer+4.
    // GAM_tdstStandardGame personal type+8. Match by unique type INDEX.
    const auto scan_world=[&](std::uintptr_t root_ptr_rva,
                              unsigned* scanned) -> int {
        DWORD root=0;
        if(!read(base+root_ptr_rva,&root,sizeof(root)) ||
           root<0x10000u)return -1;
        DWORD child=0;
        if(!read(std::uintptr_t(root)+8u,&child,sizeof(child)))return -1;
        DWORD seen[kMaxActorNodes]{};
        DWORD previous=0;
        unsigned matches=0;
        for(unsigned i=0;i<kMaxActorNodes;++i) {
            if(child==0u)return matches ? 1 : 0; // complete traversal
            if(child<0x10000u)return -1;
            for(unsigned j=0;j<i;++j)
                if(seen[j]==child)return -1;
            seen[i]=child;
            DWORD so[8]{}; // type,linked,children[3],next,prev,father
            if(!read(child,so,sizeof(so)) ||
               so[6]!=previous || so[7]!=root)return -1;
            ++*scanned;
            if(so[0]==2u) { // HIE_C_Type_Actor
                if(so[1]<0x10000u)return -1;
                DWORD std_game=0,personal=0;
                if(!read(std::uintptr_t(so[1])+4u,&std_game,sizeof(std_game)) ||
                   std_game<0x10000u ||
                   !read(std::uintptr_t(std_game)+8u,
                         &personal,sizeof(personal)))return -1;
                if(personal==found_index)++matches;
            }
            previous=child;
            child=so[5];
        }
        return -1; // traversal cap reached
    };
    out.dynamic_actor=scan_world(kDynamicWorldRva,&out.dynamic_nodes);
    out.inactive_actor=scan_world(kInactiveWorldRva,&out.inactive_nodes);
    // A partial world scan is never enough to prove actor presence
    // or absence. Keep both unknown if either hierarchy fails.
    if(out.dynamic_actor<0 || out.inactive_actor<0) {
        out.dynamic_actor=-1;
        out.inactive_actor=-1;
    }
    return out;
}
#if RAYMAN2FIX_ESC_MUSIC_PROTO
bool proto_esc_menu_actor_open() {
    const auto snapshot=inspect_audio_menu_actor_readonly();
    return snapshot.type_found==1 &&
           snapshot.dynamic_actor==1 &&
           snapshot.inactive_actor==0;
}
#endif

void poll_audio_pause_trace() {
    if(!g_log || InterlockedCompareExchange(&g_audio_poll_guard,1,0)!=0)
        return;
    const DWORD now=GetTickCount();
    const DWORD last=g_audio_last_poll.load(std::memory_order_relaxed);
    if(last && DWORD(now-last)<500u) {
        InterlockedExchange(&g_audio_poll_guard,0);
        return;
    }
    g_audio_last_poll.store(now,std::memory_order_relaxed);
    auto* module=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if(!module) {InterlockedExchange(&g_audio_poll_guard,0);return;}
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 ||
       dos->e_lfanew>=0x1000) {
        InterlockedExchange(&g_audio_poll_guard,0);return;
    }
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        module+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       pe->FileHeader.TimeDateStamp!=kOriginalTimestamp ||
       pe->OptionalHeader.SizeOfImage<0xB8840) {
        InterlockedExchange(&g_audio_poll_guard,0);return;
    }
    // Independently inspect the original game's MENU flag and ENGINE pause
    // flag. 0x4B9BC8 is ACP_Ray2 AI_g_bInGameMenu, set by VM opcode 0x1F8.
    // 0x500640 belongs to GAM engine timer pause/unpause, not the ESC menu.
    // These original-CD xrefs were verified offline and are not written here.
    // Read-only, research-build-only, sample-capped. Neither flag alone
    // determines whether the ESC menu is currently visible.
    if(pe->OptionalHeader.SizeOfImage>0x100641u) {
        BYTE in_game_menu=0, engine_paused=0;
        SIZE_T menu_read=0, engine_read=0;
        const bool menu_ok=ReadProcessMemory(GetCurrentProcess(),
            module+0xB9BC8,&in_game_menu,sizeof(in_game_menu),&menu_read)!=0 &&
            menu_read==sizeof(in_game_menu);
        const bool engine_ok=ReadProcessMemory(GetCurrentProcess(),
            module+0x100640,&engine_paused,sizeof(engine_paused),
            &engine_read)!=0 && engine_read==sizeof(engine_paused);
        if(menu_ok && engine_ok && in_game_menu<=1u && engine_paused<=1u) {
            const int old_menu=g_audio_last_ingame_menu.exchange(
                static_cast<int>(in_game_menu),std::memory_order_relaxed);
            const int old_engine=g_audio_last_engine_pause.exchange(
                static_cast<int>(engine_paused),std::memory_order_relaxed);
            if(old_menu!=static_cast<int>(in_game_menu) ||
               old_engine!=static_cast<int>(engine_paused)) {
                char msg[236]{};
                std::snprintf(msg,sizeof(msg),
                    "RUNTIME-AUDIO-UI-STATE: t_ms=%lu in_game_menu=%u engine_paused=%u prior_menu=%d prior_engine_paused=%d foreground=%u (read-only; generic menu flag, not ESC visibility proof)",
                    static_cast<unsigned long>(now-g_audio_origin.load(
                        std::memory_order_acquire)),
                    unsigned(in_game_menu),unsigned(engine_paused),
                    old_menu,old_engine,unsigned(game_window_is_foreground()));
                log_line(msg);
                // Companion actor probe: the 1999 CD registry/world
                // structures are observed read-only, with strict bounds.
                // Research the ESC menu actor on native UI state changes.
                const auto actor=inspect_audio_menu_actor_readonly();
                char actor_line[350]{};
                std::snprintf(actor_line,sizeof(actor_line),
                    "RUNTIME-AUDIO-MENU-ACTOR: t_ms=%lu in_game_menu=%u engine_paused=%u esc_down=%u personal_type_found=%d dynamic_actor=%d inactive_actor=%d type_nodes=%u dynamic_nodes=%u inactive_nodes=%u (read-only; actor candidate not proof of visible ESC menu)",
                    static_cast<unsigned long>(now-g_audio_origin.load(
                        std::memory_order_acquire)),
                    unsigned(in_game_menu),unsigned(engine_paused),
                    unsigned((GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0),
                    actor.type_found,actor.dynamic_actor,
                    actor.inactive_actor,actor.type_nodes,
                    actor.dynamic_nodes,actor.inactive_nodes);
                log_line(actor_line);
            }
        }
    }
    // Sample the game's *global* nested audio-pause depth every ~500ms.
    // This does not rely on Escape being held during the pause and thus
    // shows whether the original menu ever reaches its matching resume.
    DWORD depth=0;
    SIZE_T got=0;
    if(ReadProcessMemory(GetCurrentProcess(),module+0xA3CC8,&depth,
                         sizeof(depth),&got) && got==sizeof(depth) &&
       depth<=16u) {
        const int previous=g_audio_last_depth.exchange(
            static_cast<int>(depth),std::memory_order_relaxed);
        if(previous!=static_cast<int>(depth)) {
            char msg[180]{};
            std::snprintf(msg,sizeof(msg),
                "RUNTIME-AUDIO-STATE: t_ms=%lu global_pause_depth=%lu prior_depth=%d foreground=%u esc_down=%u (read-only; key is not a menu-state detector)",
                static_cast<unsigned long>(now-g_audio_origin.load(
                    std::memory_order_acquire)),
                static_cast<unsigned long>(depth),previous,
                unsigned(game_window_is_foreground()),
                unsigned((GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0));
            log_line(msg);
        }
    }
    const auto begin=reinterpret_cast<std::uintptr_t>(module);
    const auto end=begin+pe->OptionalHeader.SizeOfImage;
    for(unsigned i=0;i<kAudioSlotCount;++i) {
        auto* ptr=reinterpret_cast<PVOID volatile*>(module+kAudioSlots[i].rva);
        if(!writable_audio_slot(const_cast<const void*>(
              reinterpret_cast<volatile void*>(ptr))))continue;
        const PVOID current=*ptr;
        const PVOID hook=reinterpret_cast<PVOID>(kAudioHooks[i]);
        if(!current || current==hook)continue;
        const auto address=reinterpret_cast<std::uintptr_t>(current);
        // Ignore uninitialized internal fallback stubs (e.g. 0x455CE0).
        if((address>=begin && address<end) ||
           !executable_page(current))continue;
        g_audio_original[i].store(current,std::memory_order_release);
        if(InterlockedCompareExchangePointer(ptr,hook,current)==current) {
            const unsigned n=g_audio_hooks_installed.fetch_add(
                1u,std::memory_order_relaxed)+1u;
            if(n<=kAudioSlotCount) {
                char line[140]{};
                std::snprintf(line,sizeof(line),
                    "RUNTIME-AUDIO-TRACE: installed=%s slot_rva=0x%06X (calls forwarded; research-only)",
                    kAudioSlots[i].name,unsigned(kAudioSlots[i].rva));
                log_line(line);
                log_audio_driver_module(i,current);
            }
        }
    }
    // Hook the five-argument transition playback API independently from
    // the one-argument pause/stop table: mixing their signatures would
    // corrupt the x86 stdcall stack.
    auto* play_slot=reinterpret_cast<PVOID volatile*>(
        module+kTransitionPlayExRva);
    if(writable_audio_slot(const_cast<const void*>(
          reinterpret_cast<volatile void*>(play_slot)))) {
        const PVOID current=*play_slot;
        const PVOID hook=reinterpret_cast<PVOID>(audio_play_transition_ex);
        const auto address=reinterpret_cast<std::uintptr_t>(current);
        if(current && current!=hook &&
           !(address>=begin && address<end) && executable_page(current)) {
            g_transition_play_original.store(current,std::memory_order_release);
            if(InterlockedCompareExchangePointer(play_slot,hook,current)==current)
                log_line("RUNTIME-AUDIO-TRANSITION-PLAY: "
                         "installed=PlayTransitionExSxd rva=0x0B86F0 "
                         "(five args/result forwarded unchanged; research-only)");
        }
    }
#if RAYMAN2FIX_ESC_MUSIC_PROTO
    proto_install_callsites();
#endif
    InterlockedExchange(&g_audio_poll_guard,0);
}
#endif // RAYMAN2FIX_AUDIO_TRACE



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
    if(!g_skip_cinematics)return;
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
    // When the four original source conversions and the native actor
    // extents are corrected at their OWNER, keeping the screen-world
    // viewport native avoids an inverse-projection double transform.
    // Preserve the previous viewport path as a fallback if the guarded
    // source patch is unavailable for any reason.
    if(native_ui && viewport && g_hud_scale<0.9999f &&
       !g_overlay_source_patch_enabled.load(std::memory_order_acquire)) {
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
#if RAYMAN2FIX_AUDIO_TRACE
    poll_audio_pause_trace();
#if RAYMAN2FIX_ESC_MUSIC_PROTO
    proto_menu_tick();
#endif
#endif
#if RAYMAN2FIX_RESEARCH_PROBES
    // This is exclusively the disabled Learn_10 research observer.
    // Never invoke it from an ordinary release camera callback.
    poll_learn_intro_skip();
#endif
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
    if (!kResearchProbes) return;
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
    if (!kResearchProbes) return;
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
            const unsigned overlap_index=g_frame_overlap_glyphs
                .fetch_add(1,std::memory_order_relaxed);
            if(overlap_index==0) {
                g_first_frame_overlap_glyph_tick.store(
                    GetTickCount()|1u,std::memory_order_release);
                log_line("RUNTIME-HUD-PANEL-LINK: first overlapping native glyph reached; monitor restarts glyph-relative clock (read-only)");
            }
            const bool outside=r2fix::native_glyph_outside_frame_x(
                xmin,xmax,fx0,fx1);
            if(outside) {
                g_frame_overlap_outside_x.fetch_add(
                    1,std::memory_order_relaxed);
                const unsigned outside_sample=
                    g_frame_overlap_outside_examples.fetch_add(
                    1,std::memory_order_relaxed);
                if(outside_sample<4 && g_log) {
                    char geometry[255]{};
                    std::snprintf(geometry,sizeof(geometry),
                        "RUNTIME-HUD-PANEL-LINK: outside_frame sample=%u side=%s glyph_x=%.2f..%.2f frame_x=%.2f..%.2f glyph_y=%.2f..%.2f (original percent; read-only)",
                        outside_sample+1,
                        xmin<fx0 && xmax>fx1 ? "both" :
                        xmin<fx0 ? "left" : "right",
                        double(xmin),double(xmax),double(fx0),
                        double(fx1),double(ymin),double(ymax));
                    log_line(geometry);
                }
            }
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
            // Capture whole text runs even when the user quits before
            // a wall-clock checkpoint. These are cumulative call counts,
            // not characters, strings, words, or identified dialogue.
            const unsigned nth=sample+1;
            if(g_log && (nth==16 || nth==64 || nth==256 ||
                         nth==1024 || nth==4096)) {
                char progress[248]{};
                std::snprintf(progress,sizeof(progress),
                    "RUNTIME-HUD-PANEL-LINK: glyph_milestone=%u x_min=%.2f x_max=%.2f frame_x=%.2f..%.2f outside_count=%u (native percent; read-only)",
                    nth,
                    double(g_frame_overlap_min_x100.load(
                        std::memory_order_relaxed))/100.0,
                    double(g_frame_overlap_max_x100.load(
                        std::memory_order_relaxed))/100.0,
                    double(fx0),double(fx1),
                    g_frame_overlap_outside_x.load(
                        std::memory_order_relaxed));
                log_line(progress);
            }
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
// Common, edition-verified native inverse 2D->3D screen-position
// conversion. All four original CD xrefs pass the secondary viewport:
// 0x4711F6 (script 409), 0x47189B (other actor setup),
// 0x475C53 and 0x475C8E (TextSuperImposed-native text).
// This changes only normalized screen X, not world 3D geometry.
void __cdecl centered_native_screen_to_vertex(
        void* viewport,void* out_vertex,float x,float y,float depth) {
    const auto original=g_original_screen_to_vertex;
    if(!original)return;
    g_overlay_source_entry_calls.fetch_add(1u,std::memory_order_relaxed);
    if(g_overlay_source_patch_enabled.load(std::memory_order_acquire) &&
       viewport==g_approved_overlay_viewport) {
        g_overlay_source_ui_calls.fetch_add(1u,std::memory_order_relaxed);
        float centered=0.0f;
        if(r2fix::center_normalized_superimposed_x(
                    x,g_hud_scale,centered)) {
            const float before=x;
            x=centered;
            g_overlay_source_corrected_calls.fetch_add(
                1u,std::memory_order_relaxed);
            const std::uintptr_t original_base=
                reinterpret_cast<std::uintptr_t>(
                    g_approved_overlay_viewport)-kNativeUiViewportRva;
            const std::uintptr_t ret=
                reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const std::uint32_t return_rva=ret>=original_base &&
                ret-original_base<=0xFFFFFFFFu ?
                static_cast<std::uint32_t>(ret-original_base) : 0u;
            unsigned route=4u;
            switch(return_rva) {
                case 0x711FBu: route=0u;break; // procedure 409
                case 0x718A0u: route=1u;break; // independent actor
                case 0x75C58u: route=2u;break; // text path A
                case 0x75C93u: route=3u;break; // text path B
                default:break;
            }
            g_overlay_source_routes[route].fetch_add(
                1u,std::memory_order_relaxed);
            const bool after_frame=g_first_lower_frame_tick.load(
                std::memory_order_acquire)!=0;
            if(after_frame) {
                g_overlay_source_routes_after_frame[route].fetch_add(
                    1u,std::memory_order_relaxed);
                g_overlay_source_after_frame.fetch_add(
                    1u,std::memory_order_relaxed);
                if(g_first_frame_overlap_glyph_tick.load(
                        std::memory_order_acquire)!=0) {
                    g_overlay_source_after_glyph.fetch_add(
                        1u,std::memory_order_relaxed);
                }
            }
            // One or two first examples per phase, not per frame or actor.
            // Only normalized numbers, never raw pointers or game text.
            auto& samples=after_frame ? g_overlay_source_samples_after :
                                        g_overlay_source_samples_before;
            if(g_log && samples.fetch_add(
                         1u,std::memory_order_relaxed)<2u) {
                char msg[248]{};
                const char* owners[]={
                    "script409","other-actor","native-text-a",
                    "native-text-b","unknown"
                };
                std::snprintf(msg,sizeof(msg),
                    "RUNTIME-OVERLAY-SOURCE: sample phase=%s route=%s caller_rva=0x%06X x_original=%.5f x_centered=%.5f y=%.5f depth=%.5f (approved UI viewport, no object identity)",
                    after_frame?"after-panel":"before-panel",
                    owners[route],unsigned(return_rva),
                    double(before),double(centered),
                    double(y),double(depth));
                log_line(msg);
            }
        }
    }
    original(viewport,out_vertex,x,y,depth);
}

// A startup-only alternative to the earlier viewport adjustment.
// Preflight EVERY original instruction and establish writable pages
// before modifying ANY original engine bytes. The 4 coefficient
// patches relocate only the float operand of a six-byte FLD/FMUL;
// no native code is transplanted and no global original constant
// is changed. The function entry trampoline replays one whole
// six-byte x86 instruction before returning to the original.
void install_approved_overlay_source_patch() {
    if(g_overlay_source_patch_enabled.load(std::memory_order_acquire) ||
       g_hud_scale>=0.9999f)return;
    auto* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if(!base)return;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 ||
       dos->e_lfanew>=0x1000)return;
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       pe->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       pe->FileHeader.TimeDateStamp!=kOriginalTimestamp)return;
    if(pe->OptionalHeader.SizeOfImage<kNativeUiViewportRva+0x70u)
        return;
    struct FactorSite {
        std::uint32_t rva;
        BYTE expected[6];
        const float* value;
    };
    const FactorSite factors[]={
        // proc 409 sets actor X and Z uniform matrix extent.
        {0x7122Au,{0xD8,0x0D,0x58,0xCC,0x49,0x00},
            &g_superimposed_actor_extent_coefficient},
        // Independent native superimposed object setup.
        {0x718CFu,{0xD8,0x0D,0x58,0xCC,0x49,0x00},
            &g_superimposed_actor_extent_coefficient},
        // Native text X/Z matrix extents; different input bases.
        {0x75BEFu,{0xD8,0x0D,0xA4,0xCC,0x49,0x00},
            &g_superimposed_text_x_coefficient},
        {0x75BF7u,{0xD8,0x0D,0xA8,0xCC,0x49,0x00},
            &g_superimposed_text_z_coefficient},
    };
    constexpr unsigned kFactorCount=sizeof(factors)/sizeof(factors[0]);
    auto* target=base+kNativeScreenToVertexRva;
    if(pe->OptionalHeader.SizeOfImage<kNativeScreenToVertexRva+
            sizeof(kNativeScreenToVertexExpected) ||
       std::memcmp(target,kNativeScreenToVertexExpected,
                   sizeof(kNativeScreenToVertexExpected))!=0) {
        log_line("RUNTIME-OVERLAY-SOURCE: native GLI screen-position entry unrecognized; original viewport retained");
        return;
    }
    for(const auto& site:factors) {
        if(pe->OptionalHeader.SizeOfImage<site.rva+sizeof(site.expected) ||
           std::memcmp(base+site.rva,site.expected,
                       sizeof(site.expected))!=0) {
            log_line("RUNTIME-OVERLAY-SOURCE: native actor/text extent signature mismatch; original viewport retained");
            return;
        }
    }
    g_superimposed_actor_extent_coefficient=(1.0f/640.0f)*g_hud_scale;
    g_superimposed_text_x_coefficient=(1.0f/640.0f)*g_hud_scale;
    g_superimposed_text_z_coefficient=(1.0f/480.0f)*g_hud_scale;
    if(!std::isfinite(g_superimposed_text_z_coefficient) ||
       g_superimposed_text_z_coefficient<=0.0f)return;

    constexpr std::size_t kEntryBytes=
        sizeof(kNativeScreenToVertexExpected);
    auto* gateway=static_cast<BYTE*>(VirtualAlloc(nullptr,64,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!gateway)return;
    std::memcpy(gateway,target,kEntryBytes);
    write_jump(gateway+kEntryBytes,target+kEntryBytes);
    DWORD ignored=0;
    if(!VirtualProtect(gateway,64,PAGE_EXECUTE_READ,&ignored)) {
        VirtualFree(gateway,0,MEM_RELEASE);
        return;
    }

    // Acquire write access to all five sites before the first edit.
    BYTE* sites[kFactorCount+1]{};
    DWORD old_protection[kFactorCount+1]{};
    for(unsigned i=0;i<kFactorCount;++i)
        sites[i]=base+factors[i].rva;
    sites[kFactorCount]=target;
    unsigned writable=0;
    for(;writable<kFactorCount+1;++writable) {
        if(!VirtualProtect(sites[writable],6,
              PAGE_EXECUTE_READWRITE,&old_protection[writable]))break;
    }
    if(writable<kFactorCount+1) {
        while(writable>0) {
            --writable;
            VirtualProtect(sites[writable],6,
                old_protection[writable],&ignored);
        }
        VirtualFree(gateway,0,MEM_RELEASE);
        log_line("RUNTIME-OVERLAY-SOURCE: cannot prepare all native code sites; fallback viewport retained");
        return;
    }
    for(unsigned i=0;i<kFactorCount;++i) {
        const std::uint32_t operand=static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(factors[i].value));
        // x86 FMUL m32real absolute: opcode D8 0D + disp32
        std::memcpy(sites[i]+2,&operand,sizeof(operand));
        FlushInstructionCache(GetCurrentProcess(),sites[i],6);
    }
    g_original_screen_to_vertex=
        reinterpret_cast<NativeScreenToVertex>(gateway);
    g_approved_overlay_viewport=base+kNativeUiViewportRva;
    write_jump(target,
        reinterpret_cast<const void*>(&centered_native_screen_to_vertex));
    target[5]=0x90;
    FlushInstructionCache(GetCurrentProcess(),target,6);
    // Reprotect in reverse order, including overlapping code pages.
    for(unsigned i=kFactorCount+1;i>0;--i) {
        const unsigned idx=i-1;
        VirtualProtect(sites[idx],6,old_protection[idx],&ignored);
    }
    g_overlay_source_patch_enabled.store(true,std::memory_order_release);
    char line[242]{};
    std::snprintf(line,sizeof(line),
        "RUNTIME-OVERLAY-SOURCE: CD native source conversion and four actor/text extents centered 4:3 at scale=%.6f; screen-world viewport left native (experimental, no per-level hacks)",
        double(g_hud_scale));
    log_line(line);
}
// The public GOG Ray2Fix source suggests three superimposed-actor
// code locations. They are NOT approved offsets for this CD executable.
// Inspect only a few bytes from executable PE sections; never hook,
// overwrite or call any candidate. The launcher has already approved
// the exact CD hashes; this runtime verifies its own PE timestamp.
void probe_superimposed_cd_candidates(const BYTE* image,
                                      const IMAGE_NT_HEADERS32* pe) {
    if(!image || !pe || pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       pe->FileHeader.TimeDateStamp!=kOriginalTimestamp ||
       pe->FileHeader.NumberOfSections==0 ||
       pe->FileHeader.NumberOfSections>32) return;
    struct Candidate {
        const char* role;
        std::uint32_t rva;
        unsigned bytes;
    };
    constexpr Candidate candidates[]={
        {"gog-superimposed-procedure",0x710C0u,96u},
        {"gog-screen-position",0x75C7Fu,32u},
        {"gog-actor-size",0x75C96u,32u},
        // 5 absolute VA branch destinations for case IDs 409..413.
        {"cd-superimposed-jump-table",0x7135Cu,20u}
    };
    const auto* sections=IMAGE_FIRST_SECTION(pe);
    const std::uint32_t image_bytes=pe->OptionalHeader.SizeOfImage;
    const auto headers_bytes=pe->OptionalHeader.SizeOfHeaders;
    const auto section_offset=reinterpret_cast<std::uintptr_t>(sections)-
        reinterpret_cast<std::uintptr_t>(image);
    const std::uint64_t section_end=
        std::uint64_t(section_offset)+
        std::uint64_t(pe->FileHeader.NumberOfSections)*
        sizeof(IMAGE_SECTION_HEADER);
    if(section_end>headers_bytes || section_end>image_bytes) return;
    for(const auto& c:candidates) {
        bool executable=false;
        if(c.bytes<=96u && c.rva<=image_bytes &&
           c.bytes<=image_bytes-c.rva) {
            for(unsigned i=0;i<pe->FileHeader.NumberOfSections;++i) {
                const auto& sec=sections[i];
                const std::uint64_t start=sec.VirtualAddress;
                const std::uint64_t size=sec.Misc.VirtualSize>
                    sec.SizeOfRawData ? sec.Misc.VirtualSize :
                                       sec.SizeOfRawData;
                const std::uint64_t end=start+size;
                if((sec.Characteristics&IMAGE_SCN_MEM_EXECUTE) &&
                   c.rva>=start && std::uint64_t(c.rva)+c.bytes<=end) {
                    executable=true;
                    break;
                }
            }
        }
        if(!executable) {
            char msg[200]{};
            std::snprintf(msg,sizeof(msg),
                "RUNTIME-OVERLAY-CODE: candidate=%s rva=0x%08X status=not-in-executable-section (GOG reference only; no hook)",
                c.role,unsigned(c.rva));
            log_line(msg);
            continue;
        }
        BYTE sample[96]{};
        SIZE_T read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),image+c.rva,
                              sample,c.bytes,&read) || read!=c.bytes) {
            char msg[180]{};
            std::snprintf(msg,sizeof(msg),
                "RUNTIME-OVERLAY-CODE: candidate=%s rva=0x%08X status=unreadable (no hook)",
                c.role,unsigned(c.rva));
            log_line(msg);
            continue;
        }
        char hex[96u*2u+1u]{};
        constexpr char digits[]="0123456789ABCDEF";
        for(unsigned i=0;i<c.bytes;++i) {
            hex[i*2u]=digits[sample[i]>>4];
            hex[i*2u+1u]=digits[sample[i]&15u];
        }
        char msg[380]{};
        std::snprintf(msg,sizeof(msg),
            "RUNTIME-OVERLAY-CODE: candidate=%s rva=0x%08X bytes=%u hex=%s (GOG reference, CD code read-only; not validated for hooking)",
            c.role,unsigned(c.rva),c.bytes,hex);
        log_line(msg);
        if(c.rva==0x7135Cu) {
            // Decode each CD-native dispatcher branch, but never call
            // it or patch it. The PE must be loaded at its verified
            // preferred image base (all table entries are absolute VAs).
            const std::uintptr_t preferred=
                static_cast<std::uintptr_t>(pe->OptionalHeader.ImageBase);
            for(unsigned i=0;i<5u;++i) {
                std::uint32_t target_va=0;
                std::memcpy(&target_va,sample+4u*i,sizeof(target_va));
                const std::uintptr_t va=target_va;
                if(va<preferred || va-preferred>image_bytes ||
                   image_bytes-(va-preferred)<64u) continue;
                const auto branch_rva=static_cast<std::uint32_t>(
                    va-preferred);
                bool branch_executable=false;
                for(unsigned j=0;j<pe->FileHeader.NumberOfSections;++j) {
                    const auto& sec=sections[j];
                    const std::uint64_t start=sec.VirtualAddress;
                    const std::uint64_t size=sec.Misc.VirtualSize>
                        sec.SizeOfRawData ? sec.Misc.VirtualSize :
                                          sec.SizeOfRawData;
                    if((sec.Characteristics&IMAGE_SCN_MEM_EXECUTE) &&
                       branch_rva>=start &&
                       std::uint64_t(branch_rva)+64u<=start+size) {
                        branch_executable=true;
                        break;
                    }
                }
                if(!branch_executable) continue;
                BYTE opcodes[64]{};
                SIZE_T read_branch=0;
                if(!ReadProcessMemory(GetCurrentProcess(),
                                      image+branch_rva,opcodes,
                                      sizeof(opcodes),&read_branch) ||
                   read_branch!=sizeof(opcodes)) continue;
                char branch_hex[129]{};
                for(unsigned j=0;j<sizeof(opcodes);++j) {
                    branch_hex[j*2u]=digits[opcodes[j]>>4];
                    branch_hex[j*2u+1u]=digits[opcodes[j]&15u];
                }
                char branch_line[265]{};
                std::snprintf(branch_line,sizeof(branch_line),
                    "RUNTIME-OVERLAY-BRANCH: procedure=%u rva=0x%08X bytes=64 hex=%s (approved CD executable; read-only)",
                    409u+i,branch_rva,branch_hex);
                log_line(branch_line);
            }
        }
    }
}

// Runtime-only evidence: the original engine reads procedure id from
// p_stTree[-1].uParam.ulValue, verified by CD disassembly at 0x4710D3
// (mov edi,[ebx-8]). No user-level identifiers or raw pointers logged.
// This captures whether the actor procedure is invoked when the lower
// dialogue panel is visible, without touching scene structures.
void* __cdecl observe_superimposed_proc(void* superobject,void* tree) {
    const auto original=g_original_superimposed_proc;
    if(!original)return nullptr;
    const std::uintptr_t address=reinterpret_cast<std::uintptr_t>(tree);
    std::uint32_t procedure=0;
    if(address>=0x10008u && read_u32(address-8u,procedure)) {
        const unsigned bucket=(procedure>=409u && procedure<=413u) ?
                              procedure-409u : 5u;
        const unsigned seen=g_superimposed_proc_calls[bucket].fetch_add(
                              1,std::memory_order_relaxed);
        const bool after_panel=g_first_lower_frame_tick.load(
                                  std::memory_order_acquire)!=0;
        if(after_panel)g_superimposed_proc_after_frame[bucket].fetch_add(
                           1,std::memory_order_relaxed);
        if(seen<2u && g_log) {
            const unsigned sample=g_superimposed_proc_samples[bucket]
                .fetch_add(1,std::memory_order_relaxed);
            if(sample<2u) {
                char message[230]{};
                std::snprintf(message,sizeof(message),
                    "RUNTIME-OVERLAY-ACTOR: sample=%u procedure=%u bucket=%u after_first_lower_frame=%u (script dispatch; read-only, not arrow identity)",
                    sample+1u,unsigned(procedure),bucket,
                    unsigned(after_panel));
                log_line(message);
            }
        }
    }
    return original(superobject,tree);
}
void install_superimposed_proc_probe() {
    // Called only after validated approved EXE camera detour. The two
    // intercepted instructions are position independent: sub esp,28h;
    // lea eax,[esp+30h]. The trampoline keeps the original ABI.
    auto* module=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if(!module || g_original_superimposed_proc)return;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 ||
       dos->e_lfanew>=0x1000)return;
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        module+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       pe->FileHeader.TimeDateStamp!=kOriginalTimestamp ||
       pe->OptionalHeader.SizeOfImage<kSuperimposedProcRva+64)
        return;
    auto* target=module+kSuperimposedProcRva;
    if(std::memcmp(target,kSuperimposedProcExpected,
                   sizeof(kSuperimposedProcExpected))!=0) {
        log_line("RUNTIME-OVERLAY-ACTOR: CD procedure entry pattern unrecognized; probe disabled");
        return;
    }
    auto* gateway=static_cast<BYTE*>(VirtualAlloc(nullptr,64,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!gateway)return;
    std::memcpy(gateway,target,sizeof(kSuperimposedProcExpected));
    write_jump(gateway+sizeof(kSuperimposedProcExpected),
               target+sizeof(kSuperimposedProcExpected));
    DWORD old=0;
    if(!VirtualProtect(gateway,64,PAGE_EXECUTE_READ,&old)) {
        VirtualFree(gateway,0,MEM_RELEASE);return;
    }
    DWORD previous=0;
    if(!VirtualProtect(target,sizeof(kSuperimposedProcExpected),
                       PAGE_EXECUTE_READWRITE,&previous)) {
        VirtualFree(gateway,0,MEM_RELEASE);return;
    }
    g_original_superimposed_proc=
        reinterpret_cast<SuperimposedProc>(gateway);
    write_jump(target,reinterpret_cast<void*>(&observe_superimposed_proc));
    target[5]=target[6]=0x90;
    FlushInstructionCache(GetCurrentProcess(),target,
                          sizeof(kSuperimposedProcExpected));
    DWORD unused=0;
    VirtualProtect(target,sizeof(kSuperimposedProcExpected),previous,&unused);
    log_line("RUNTIME-OVERLAY-ACTOR: approved CD superimposed script dispatcher monitored (procedures 409..413; read-only, no actor or geometry changes)");
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

    // Approved PE and camera bytes: collect only code-location evidence
    // for the separate superimposed-actor path. No actor changes.
    probe_superimposed_cd_candidates(module,pe);

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
    if(!g_skip_cinematics) {
        log_line("RUNTIME-INTRO: jail_20 skip disabled by Features.SkipCinematics=0; Learn_10 remains unskippable");
        return;
    }
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

void report_overlay_source_counts(const char* when) {
    char status[278]{};
    std::snprintf(status,sizeof(status),
        "RUNTIME-OVERLAY-SOURCE: checkpoint=%s installed=%u total_invocations=%u ui_viewport_invocations=%u corrected=%u after_first_frame=%u after_first_glyph=%u (native source-stage, read-only counters)",
        when,unsigned(g_overlay_source_patch_enabled.load(
                           std::memory_order_acquire)),
        g_overlay_source_entry_calls.load(std::memory_order_relaxed),
        g_overlay_source_ui_calls.load(std::memory_order_relaxed),
        g_overlay_source_corrected_calls.load(std::memory_order_relaxed),
        g_overlay_source_after_frame.load(std::memory_order_relaxed),
        g_overlay_source_after_glyph.load(std::memory_order_relaxed));
    log_line(status);
    char callers[306]{};
    std::snprintf(callers,sizeof(callers),
        "RUNTIME-OVERLAY-SOURCE-ROUTES: checkpoint=%s actor409=%u actor_other=%u text_a=%u text_b=%u other=%u after_frame_actor409=%u actor_other=%u text_a=%u text_b=%u other=%u (approved CD return RVAs)",
        when,
        g_overlay_source_routes[0].load(std::memory_order_relaxed),
        g_overlay_source_routes[1].load(std::memory_order_relaxed),
        g_overlay_source_routes[2].load(std::memory_order_relaxed),
        g_overlay_source_routes[3].load(std::memory_order_relaxed),
        g_overlay_source_routes[4].load(std::memory_order_relaxed),
        g_overlay_source_routes_after_frame[0].load(std::memory_order_relaxed),
        g_overlay_source_routes_after_frame[1].load(std::memory_order_relaxed),
        g_overlay_source_routes_after_frame[2].load(std::memory_order_relaxed),
        g_overlay_source_routes_after_frame[3].load(std::memory_order_relaxed),
        g_overlay_source_routes_after_frame[4].load(std::memory_order_relaxed));
    log_line(callers);
}

void report_superimposed_counts_after_frame(const char* when,
                                          DWORD elapsed_ms) {
    const unsigned counts[6]={
        g_superimposed_proc_calls[0].load(std::memory_order_relaxed),
        g_superimposed_proc_calls[1].load(std::memory_order_relaxed),
        g_superimposed_proc_calls[2].load(std::memory_order_relaxed),
        g_superimposed_proc_calls[3].load(std::memory_order_relaxed),
        g_superimposed_proc_calls[4].load(std::memory_order_relaxed),
        g_superimposed_proc_calls[5].load(std::memory_order_relaxed)
    };
    const unsigned after[6]={
        g_superimposed_proc_after_frame[0].load(std::memory_order_relaxed),
        g_superimposed_proc_after_frame[1].load(std::memory_order_relaxed),
        g_superimposed_proc_after_frame[2].load(std::memory_order_relaxed),
        g_superimposed_proc_after_frame[3].load(std::memory_order_relaxed),
        g_superimposed_proc_after_frame[4].load(std::memory_order_relaxed),
        g_superimposed_proc_after_frame[5].load(std::memory_order_relaxed)
    };
    char line[330]{};
    std::snprintf(line,sizeof(line),
        "RUNTIME-OVERLAY-ACTOR: checkpoint=%s after_first_frame_ms=%lu total_409..413=%u,%u,%u,%u,%u other=%u after_frame_409..413=%u,%u,%u,%u,%u other=%u (read-only, not actor identity)",
        when,static_cast<unsigned long>(elapsed_ms),
        counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],
        after[0],after[1],after[2],after[3],after[4],after[5]);
    log_line(line);
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
    unsigned glyph_relative_reports=0;
    constexpr DWORD kAfterFrameProbeMs[]={2000,5000,10000,20000};
    for (unsigned iteration = 0;; ++iteration) {
        const DWORD first=g_first_lower_frame_tick.load(
            std::memory_order_acquire);
        if(kResearchProbes && first && frame_relative_reports<4) {
            const DWORD elapsed=GetTickCount()-first;
            if(elapsed>=kAfterFrameProbeMs[frame_relative_reports]) {
                const char* labels[]={"plus2s","plus5s","plus10s","plus20s"};
                report_panel_glyph_geometry(
                    labels[frame_relative_reports],elapsed);
                report_superimposed_counts_after_frame(
                    labels[frame_relative_reports],elapsed);
                report_overlay_source_counts(
                    labels[frame_relative_reports]);
                ++frame_relative_reports;
            }
        }
        // Independent checkpoint schedule anchored to the first glyph
        // actually overlapping the frame, not to frame creation.
        const DWORD first_glyph=g_first_frame_overlap_glyph_tick.load(
            std::memory_order_acquire);
        if(kResearchProbes && first_glyph && glyph_relative_reports<3) {
            const DWORD elapsed=GetTickCount()-first_glyph;
            if(elapsed>=kAfterFrameProbeMs[glyph_relative_reports]) {
                const DWORD frame_tick=g_first_lower_frame_tick.load(
                    std::memory_order_acquire);
                const char* labels[]={
                    "after-glyph-plus2s","after-glyph-plus5s",
                    "after-glyph-plus10s"
                };
                report_panel_glyph_geometry(
                    labels[glyph_relative_reports],
                    frame_tick ? GetTickCount()-frame_tick : elapsed);
                report_overlay_source_counts(
                    labels[glyph_relative_reports]);
                ++glyph_relative_reports;
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
        if (kResearchProbes &&
            (iteration == 10 || iteration == 80 || iteration == 120 ||
             iteration == 150 || iteration == 170 || iteration == 180)) {
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
            const char* overlay_time_labels[]={
                "at5s","at40s","at60s","at75s","at85s","at90s"
            };
            const unsigned overlay_index=
                iteration==10 ? 0u : iteration==80 ? 1u :
                iteration==120 ? 2u : iteration==150 ? 3u :
                iteration==170 ? 4u : 5u;
            report_overlay_source_counts(overlay_time_labels[overlay_index]);
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
                char actor_msg[252]{};
                std::snprintf(actor_msg,sizeof(actor_msg),
                    "RUNTIME-OVERLAY-ACTOR: at=%us proc409=%u proc410=%u proc411=%u proc412=%u proc413=%u others=%u after_frame409=%u after_frame410=%u (read-only)",
                    unsigned(iteration/2),
                    g_superimposed_proc_calls[0].load(std::memory_order_relaxed),
                    g_superimposed_proc_calls[1].load(std::memory_order_relaxed),
                    g_superimposed_proc_calls[2].load(std::memory_order_relaxed),
                    g_superimposed_proc_calls[3].load(std::memory_order_relaxed),
                    g_superimposed_proc_calls[4].load(std::memory_order_relaxed),
                    g_superimposed_proc_calls[5].load(std::memory_order_relaxed),
                    g_superimposed_proc_after_frame[0].load(std::memory_order_relaxed),
                    g_superimposed_proc_after_frame[1].load(std::memory_order_relaxed));
                log_line(actor_msg);
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
#if RAYMAN2FIX_ESC_MUSIC_PROTO
    wchar_t proto_flag[8]{};
    const DWORD flag_size=GetEnvironmentVariableW(
        L"RAYMAN2FIX_ESC_AUDIO_PROTOTYPE",proto_flag,8);
    const bool explicit_opt_in=flag_size==1 && proto_flag[0]==L'1';
    g_proto_enabled.store(explicit_opt_in && g_log,std::memory_order_release);
    if(g_proto_enabled.load(std::memory_order_acquire))
        log_line("RUNTIME-ESC-PROTOTYPE: enabled=1 experiment=paired-SXD "
                 "ESC+UI-transition+Theme-player; NOT validated music identity; "
                 "preserve original game files");
#endif
#if RAYMAN2FIX_AUDIO_TRACE
    if (g_log) {
        g_audio_origin.store(GetTickCount(),std::memory_order_release);
#if RAYMAN2FIX_ESC_MUSIC_PROTO
        log_line(g_proto_enabled.load(std::memory_order_acquire) ?
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2 prototype=1 "
            "(original calls normally forwarded; identified music resumes may be suppressed)" :
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2 prototype=0 "
            "(all native audio calls forwarded unchanged)");
#else
        log_line("RUNTIME-AUDIO-TRACE: enabled=1 schema=2 inner_record_ref=checked-pointer; 14 native channels, 64 events for Sxd pause/resume, 16 per other channel; all forwarded unchanged");
#endif
    }
#endif
    if (g_log) log_line(kResearchProbes ?
        "RUNTIME-CONFIG: research_probes=1 (read-only experimental trace build)" :
        "RUNTIME-CONFIG: research_probes=0 (standard release instrumentation)");
    if(settings->skip_cinematics>1u)return 5;
    g_skip_cinematics=settings->skip_cinematics!=0;
    // This hook is also used for the targeted intro-skip poll on 4:3.
    // For narrow monitors camera_hor_plus leaves the FOV unchanged.
    const DWORD camera_status = install_camera_hook();
    // Unknown/unsupported game versions never receive additional hooks.
    if (camera_status == 1) {
        // The source transform covers all four native inverse-projection
        // callers and four independent extent operations as a single
        // guarded stage; only then bypass the older viewport squeeze.
        if(g_hud_scale<0.9999f)install_approved_overlay_source_patch();
        // Hook the common native 2D entry before the game draws any
        // UI; the downstream driver callback monitor still handles
        // screen sprites submitted through other engine paths.
        if(g_hud_scale<0.9999f)install_native_2d_percent_hook();
#if RAYMAN2FIX_RESEARCH_PROBES
        // Read-only hook at VA 0x4710C0 is not part of the actual fix.
        if(g_hud_scale<0.9999f)install_superimposed_proc_probe();
#endif
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
