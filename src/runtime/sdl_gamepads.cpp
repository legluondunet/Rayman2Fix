#include "rayman2fix/sdl_gamepads.hpp"
#include "rayman2fix/sha256.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <memory>

namespace r2fix {
namespace {
constexpr auto kSha = "6d7e653aa9c762ceda4fffc054a504dcce273e016eb9f1fe079b6409a058a9f7";
constexpr std::uint32_t kGamepad = 0x2000;
constexpr int kVersion = 3004020;
// SDL3 SDL_gamepad.h public ABI: signed Sint16 axes, int enum arguments.
// Keep semantic positions independent of the printed labels.
enum Axis { LeftX, LeftY, RightX, RightY, LT, RT };
enum Button { South, East, West, North, Back, Guide, Start, LeftStick,
              RightStick, LeftShoulder, RightShoulder, Up, Down, Left, Right };
template<class T> bool bind(HMODULE dll, T& out, const char* name) noexcept {
    out = reinterpret_cast<T>(GetProcAddress(dll, name));
    return out != nullptr;
}
struct FileLock {
    HANDLE file = INVALID_HANDLE_VALUE;
    ~FileLock() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};
} // namespace

struct SdlGamepads::Api {
    HMODULE dll = nullptr;
    bool initialized = false;
    int (__cdecl* version)() = nullptr;
    void (__cdecl* ready)() = nullptr;
    bool (__cdecl* init)(std::uint32_t) = nullptr;
    void (__cdecl* quit)(std::uint32_t) = nullptr;
    void (__cdecl* events)(bool) = nullptr;
    void (__cdecl* update)() = nullptr;
    std::uint32_t* (__cdecl* ids)(int*) = nullptr;
    void (__cdecl* release)(void*) = nullptr;
    void* (__cdecl* open)(std::uint32_t) = nullptr;
    void (__cdecl* close)(void*) = nullptr;
    bool (__cdecl* connected)(void*) = nullptr;
    std::uint32_t (__cdecl* id)(void*) = nullptr;
    int (__cdecl* type)(void*) = nullptr;
    std::int16_t (__cdecl* axis)(void*, int) = nullptr;
    bool (__cdecl* button)(void*, int) = nullptr;
    ~Api() {
        if (initialized) quit(kGamepad);
        if (dll) FreeLibrary(dll);
    }
};

bool SdlGamepads::initialize(const std::filesystem::path& path,
                             SdlPadLogger logger) noexcept {
    if (api_) return owner_ == GetCurrentThreadId();
    logger_ = logger;
    const auto fail = [&](const char* message) {
        if (logger_) logger_(message);
        return false;
    };
    try {
        if (!path.is_absolute() || GetModuleHandleW(L"SDL3.dll"))
            return fail("RUNTIME-SDL3: init-refused=nonabsolute-or-already-loaded");
        FileLock lock{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        BY_HANDLE_FILE_INFORMATION info{};
        if (lock.file == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandle(lock.file, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            info.nFileSizeHigh || info.nFileSizeLow > 32u * 1024u * 1024u ||
            sha256_file(path) != kSha)
            return fail("RUNTIME-SDL3: init-refused=missing-or-unverified-dll");
        auto api = std::make_unique<Api>();
        api->dll = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!api->dll) return fail("RUNTIME-SDL3: init-refused=load-failed");
        auto dll = api->dll;
        if (!bind(dll, api->version, "SDL_GetVersion") ||
            !bind(dll, api->ready, "SDL_SetMainReady") ||
            !bind(dll, api->init, "SDL_InitSubSystem") ||
            !bind(dll, api->quit, "SDL_QuitSubSystem") ||
            !bind(dll, api->events, "SDL_SetGamepadEventsEnabled") ||
            !bind(dll, api->update, "SDL_UpdateGamepads") ||
            !bind(dll, api->ids, "SDL_GetGamepads") ||
            !bind(dll, api->release, "SDL_free") ||
            !bind(dll, api->open, "SDL_OpenGamepad") ||
            !bind(dll, api->close, "SDL_CloseGamepad") ||
            !bind(dll, api->connected, "SDL_GamepadConnected") ||
            !bind(dll, api->id, "SDL_GetGamepadID") ||
            !bind(dll, api->type, "SDL_GetGamepadType") ||
            !bind(dll, api->axis, "SDL_GetGamepadAxis") ||
            !bind(dll, api->button, "SDL_GetGamepadButton") ||
            api->version() != kVersion)
            return fail("RUNTIME-SDL3: init-refused=abi-mismatch");
        api->ready();
        if (!api->init(kGamepad))
            return fail("RUNTIME-SDL3: init-refused=gamepad-subsystem");
        api->initialized = true;
        api->events(false);
        owner_ = GetCurrentThreadId();
        api_ = api.release();
        scan_due_ = true;
        if (logger_) logger_("RUNTIME-SDL3: initialized=1 version=3.4.20 gamepad-only=1 events=0 native-input=unchanged");
        return true;
    } catch (...) {
        return fail("RUNTIME-SDL3: init-refused=verification-exception");
    }
}

PadSample SdlGamepads::poll(bool foreground, PadContext context) noexcept {
    PadSample sample{};
    if (!api_ || owner_ != GetCurrentThreadId()) return sample;
    api_->update();
    if (pad_ && !api_->connected(pad_)) {
        api_->close(pad_);
        pad_ = nullptr;
        family_ = PadLabelFamily::generic;
        scan_due_ = true;
        if (logger_) logger_("RUNTIME-SDL3: disconnected=1");
        return sample; // publish a neutral frame before a new owner
    }
    const DWORD now = GetTickCount();
    if (!pad_ && (scan_due_ || static_cast<LONG>(now - next_scan_) >= 0)) {
        scan_due_ = false;
        next_scan_ = now + 500;
        int count = 0;
        auto* ids = api_->ids(&count);
        if (ids) {
            if (count > 0 && count <= 256) {
                std::sort(ids, ids + count);
                for (int i = 0; i < count && !pad_; ++i) {
                    if (!ids[i]) continue;
                    void* candidate = api_->open(ids[i]);
                    if (candidate && api_->connected(candidate) && api_->id(candidate))
                        pad_ = candidate;
                    else if (candidate) api_->close(candidate);
                }
            }
            api_->release(ids);
        }
        if (pad_) {
            const int type = api_->type(pad_);
            family_ = (type == 2 || type == 3) ? PadLabelFamily::xbox :
                      (type >= 4 && type <= 6) ? PadLabelFamily::playstation :
                      PadLabelFamily::generic;
            if (logger_) {
                char msg[160]{};
                std::snprintf(msg, sizeof(msg), "RUNTIME-SDL3: connected=1 id=%u type=%d family=%s",
                    unsigned(api_->id(pad_)), type,
                    family_ == PadLabelFamily::xbox ? "xbox" :
                    family_ == PadLabelFamily::playstation ? "playstation" : "generic");
                logger_(msg);
            }
        }
    }
    if (!pad_) return sample;
    sample.connected = true;
    sample.device_id = api_->id(pad_);
    sample.foreground = foreground;
    sample.context = context;
    if (!foreground) return sample; // no background actions
    sample.left_x = api_->axis(pad_, LeftX);
    sample.left_y = api_->axis(pad_, LeftY);
    sample.right_x = api_->axis(pad_, RightX);
    sample.left_trigger = api_->axis(pad_, LT);
    sample.right_trigger = api_->axis(pad_, RT);
    sample.south = api_->button(pad_, South);
    sample.east = api_->button(pad_, East);
    sample.west = api_->button(pad_, West);
    sample.north = api_->button(pad_, North);
    sample.start = api_->button(pad_, Start);
    sample.left_shoulder = api_->button(pad_, LeftShoulder);
    sample.right_shoulder = api_->button(pad_, RightShoulder);
    sample.dpad_up = api_->button(pad_, Up);
    sample.dpad_down = api_->button(pad_, Down);
    sample.dpad_left = api_->button(pad_, Left);
    sample.dpad_right = api_->button(pad_, Right);
    return sample;
}

void SdlGamepads::shutdown() noexcept {
    if (!api_ || owner_ != GetCurrentThreadId()) return;
    if (pad_) api_->close(pad_);
    pad_ = nullptr;
    delete api_;
    api_ = nullptr;
    owner_ = 0;
    family_ = PadLabelFamily::generic;
    scan_due_ = true;
}
} // namespace r2fix
