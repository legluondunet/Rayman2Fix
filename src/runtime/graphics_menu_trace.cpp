// Approved *deprotected* Rayman 2 x86 executable, SHA256
// 9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713.
// The original 676352-byte CD EXE has a different SizeOfImage and is NOT the
// executable launched by the verified Rayman2Fix path. Research build ONLY.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "rayman2fix/graphics_menu_trace.hpp"
#include "rayman2fix/graphics_menu_target.hpp"

namespace r2fix {
namespace {

constexpr std::uint32_t kExpectedImageSize =
    r2fix::kDeprotectedGameImageSize;
constexpr std::uint32_t kEngineModeRva = 0x100380u;   // GAM engine, read-only
constexpr std::uint32_t kGenericMenuFlagRva = 0x0B9BC8u; // not Resolution-specific
constexpr std::uint32_t kModeCountRva = 0x0A7238u;
constexpr std::uint32_t kSelectedModeRva = 0x104318u;
constexpr std::uint32_t kModeTableRva = 0x104320u;   // records of 16 bytes
constexpr std::uint32_t kModeChangeExportSlotRva = 0x104820u; // original GLI driver
constexpr unsigned kMaxTableEntries = 32u;
constexpr unsigned kIntervalMillis = 200u;
constexpr unsigned kMaxSamples = 1500u;  // 5 minutes of read-only sampling
constexpr unsigned kMaxReports = 180u;   // bounded disk output
constexpr unsigned kHeartbeatMillis = 4000u;
std::atomic<bool> g_started{false};

// Read-only, same-process VirtualQuery-guarded snapshots; never use
// potentially stale file offsets or write to the process image.
// The previous 21-Nov probe rejected the *correct* 0x1D7000 patched image
// due to a mistaken 0x117000 original-CD image-size constant, NOT because
// ReadProcessMemory had been shown to fail. Preserve both protection and
// the detailed PE-fingerprint diagnostics introduced in the prior revision.
bool read_bytes(const BYTE* base, std::uint32_t rva, void* out,
                SIZE_T length) noexcept {
    if (!base || !out || !length || rva >= kExpectedImageSize ||
        length > kExpectedImageSize - rva) return false;
    const auto* address=base+rva;
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(address,&region,sizeof(region)) ||
        region.State!=MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))!=0)
        return false;
    const DWORD protection=region.Protect & 0xFFu;
    if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE &&
       protection!=PAGE_WRITECOPY && protection!=PAGE_EXECUTE_READ &&
       protection!=PAGE_EXECUTE_READWRITE &&
       protection!=PAGE_EXECUTE_WRITECOPY) return false;
    const auto start=reinterpret_cast<std::uintptr_t>(address);
    const auto region_start=reinterpret_cast<std::uintptr_t>(
        region.BaseAddress);
    if(start < region_start || start-region_start > region.RegionSize ||
       length > region.RegionSize - (start-region_start))
        return false;  // one wholly readable committed memory region only
    std::memcpy(out,address,length);
    return true;
}
template <typename T>
bool read_value(const BYTE* base, std::uint32_t rva, T& out) noexcept {
    return read_bytes(base,rva,&out,sizeof(out));
}
struct ImageProbe {
    const char* stage="module";
    std::uint32_t observed_timestamp=0;
    std::uint32_t observed_image_size=0;
    std::uint32_t observed_entry=0;
    unsigned observed_machine=0;
    unsigned observed_sections=0;
};
bool supported_exe(const BYTE* base,ImageProbe& probe) noexcept {
    if (!base) return false;
    IMAGE_DOS_HEADER dos{};
    probe.stage="dos-unreadable";
    if (!read_bytes(base,0,&dos,sizeof(dos))) return false;
    probe.stage="dos-signature";
    if (dos.e_magic!=IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew<=0 || dos.e_lfanew>=0x1000) return false;
    IMAGE_NT_HEADERS32 nt{};
    probe.stage="nt-unreadable";
    if(!read_bytes(base,static_cast<std::uint32_t>(dos.e_lfanew),
                   &nt,sizeof(nt)))return false;
    probe.stage="nt-fingerprint";
    probe.observed_timestamp=nt.FileHeader.TimeDateStamp;
    probe.observed_image_size=nt.OptionalHeader.SizeOfImage;
    probe.observed_entry=nt.OptionalHeader.AddressOfEntryPoint;
    probe.observed_machine=nt.FileHeader.Machine;
    probe.observed_sections=nt.FileHeader.NumberOfSections;
    if(nt.Signature!=IMAGE_NT_SIGNATURE ||
       !r2fix::graphics_menu_accepts_deprotected_image({
           nt.FileHeader.Machine, nt.FileHeader.NumberOfSections,
           nt.FileHeader.TimeDateStamp, nt.OptionalHeader.Magic,
           nt.OptionalHeader.AddressOfEntryPoint,
           nt.OptionalHeader.SizeOfImage})) return false;
    probe.stage="accepted";
    return true;
}
bool game_foreground() noexcept {
    const HWND window=GetForegroundWindow();
    DWORD pid=0;
    return window && GetWindowThreadProcessId(window,&pid)!=0 &&
        pid==GetCurrentProcessId();
}
// These are only five directional/selection/return keys used by menu
// navigation, never text or arbitrary game/application keystrokes.
// High bit alone: read-only current state, no keyboard hooks or synthesis.
unsigned menu_key_mask(bool foreground) noexcept {
    if (!foreground) return 0;
    constexpr int keys[6]{VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_RETURN,VK_ESCAPE};
    unsigned mask=0;
    for(unsigned i=0;i<6;++i)
        if((GetAsyncKeyState(keys[i]) & 0x8000)!=0) mask|=1u<<i;
    return mask;
}
struct Snapshot {
    bool valid=false;
    std::uint8_t engine=0;
    std::uint8_t generic_menu=0;
    std::int32_t count=-1;
    std::int32_t selected=-1;
    std::int32_t mode_width=-1;
    std::int32_t mode_height=-1;
    std::int32_t mode_bits=-1;
    bool driver_bound=false;
};
Snapshot sample(const BYTE* base) noexcept {
    Snapshot s{};
    std::uintptr_t mode_export=0;
    s.valid=read_value(base,kEngineModeRva,s.engine) &&
        read_value(base,kGenericMenuFlagRva,s.generic_menu) &&
        read_value(base,kModeCountRva,s.count) &&
        read_value(base,kSelectedModeRva,s.selected) &&
        read_value(base,kModeChangeExportSlotRva,mode_export);
    if (!s.valid) return s;
    s.driver_bound=mode_export!=0;
    // The original 0x422160 AddDisplayMode stores [mode-id,width,height,bpp]
    // with 16-byte stride. Out-of-range states are logged without following
    // pointers or reading beyond the table's known bounded region.
    if (s.count>0 && s.count<=static_cast<std::int32_t>(kMaxTableEntries) &&
        s.selected>=0 && s.selected<s.count) {
        const auto entry=kModeTableRva + 16u*static_cast<unsigned>(s.selected);
        std::uint32_t values[4]{};
        if(read_bytes(base,entry,values,sizeof(values))) {
            s.mode_width=static_cast<std::int32_t>(values[1]);
            s.mode_height=static_cast<std::int32_t>(values[2]);
            s.mode_bits=static_cast<std::int32_t>(values[3]);
        }
    }
    return s;
}
bool relevant_change(const Snapshot& a,const Snapshot& b) noexcept {
    return a.valid!=b.valid || a.engine!=b.engine ||
        a.generic_menu!=b.generic_menu || a.count!=b.count ||
        a.selected!=b.selected ||
        a.mode_width!=b.mode_width || a.mode_height!=b.mode_height ||
        a.mode_bits!=b.mode_bits || a.driver_bound!=b.driver_bound;
}
DWORD WINAPI monitor(void* parameter) {
    auto logger=reinterpret_cast<GraphicsTraceLogger>(parameter);
    if(!logger) return 0;
    const auto* base=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    ImageProbe probe{};
    if(!supported_exe(base,probe)) {
        char line[240]{};
        std::snprintf(line,sizeof(line),
            "RUNTIME-VIDEO-MENU-TRACE: status=unsupported-deprotected-image stage=%s timestamp=0x%08X size=0x%08X entry=0x%08X machine=0x%04X sections=%u (read-only; no sampling)",
            probe.stage,probe.observed_timestamp,
            probe.observed_image_size,probe.observed_entry,
            probe.observed_machine,probe.observed_sections);
        logger(line);
        return 0;
    }
    logger("RUNTIME-VIDEO-MENU-TRACE: status=accepted-image kind=deprotected-x86 size=0x001D7000 source=same-process-guarded-read-only");
    logger("RUNTIME-VIDEO-MENU-TRACE: enabled=1 read-only=1 menu-identity=unverified mode-write=disabled max_seconds=300 max_reports=180");
    const DWORD began=GetTickCount();
    DWORD last_report=began;
    Snapshot last{};
    bool have_last=false;
    unsigned previous_keys=0;
    unsigned emitted=0;
    for(unsigned i=0;i<kMaxSamples && emitted<kMaxReports;++i) {
        Sleep(kIntervalMillis);
        const DWORD now=GetTickCount();
        const bool fg=game_foreground();
        const unsigned keys=menu_key_mask(fg);
        const unsigned down=keys & ~previous_keys;
        previous_keys=keys;
        const Snapshot s=sample(base);
        const bool change=!have_last || relevant_change(s,last);
        const bool heartbeat=fg && DWORD(now-last_report)>=kHeartbeatMillis;
        // Read-only generic menu flag is NOT the resolution submenu.
        // Log either state changes, navigation edges or a rare heartbeat;
        // never an arbitrary string, player data or pointer value.
        if(change || (fg && down) || heartbeat) {
            char line[340]{};
            std::snprintf(line,sizeof(line),
                "RUNTIME-VIDEO-MENU-TRACE: t_ms=%lu fg=%u engine=%u generic_menu=%u mode_count=%ld selected_index=%ld selected_mode=%ldx%ldx%ld change=%u nav_edges=0x%02X driver_bound=%u (read-only; submenu=unverified)",
                static_cast<unsigned long>(DWORD(now-began)),unsigned(fg),
                unsigned(s.engine),unsigned(s.generic_menu),
                static_cast<long>(s.count),static_cast<long>(s.selected),
                static_cast<long>(s.mode_width),
                static_cast<long>(s.mode_height),static_cast<long>(s.mode_bits),
                unsigned(change),down,unsigned(s.driver_bound));
            logger(line);
            ++emitted;
            last_report=now;
        }
        last=s;
        have_last=true;
    }
    logger("RUNTIME-VIDEO-MENU-TRACE: status=finished no-mode-hooks no-input-injection no-native-video-writes");
    return 0;
}
} // namespace

void start_graphics_menu_trace(GraphicsTraceLogger logger) noexcept {
    if(!logger || g_started.exchange(true,std::memory_order_acq_rel))return;
    HANDLE thread=CreateThread(nullptr,0,&monitor,
          reinterpret_cast<void*>(logger),0,nullptr);
    if(!thread) {
        g_started.store(false,std::memory_order_release);
        logger("RUNTIME-VIDEO-MENU-TRACE: status=worker-create-failed");
        return;
    }
    CloseHandle(thread);
}
} // namespace r2fix
