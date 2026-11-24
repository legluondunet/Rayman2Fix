// Standalone Windows x86 dependency smoke test, not loaded by the game.
// Only the SDL gamepad subsystem is initialized; no video/audio or input injection.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>

static_assert(sizeof(void*) == 4, "SDL3 smoke test requires x86");
template<class Fn>
Fn bind(HMODULE dll, const char* name) {
    return reinterpret_cast<Fn>(GetProcAddress(dll, name));
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || !std::filesystem::path(argv[1]).is_absolute()) {
        std::fputs("Expected absolute audited SDL3.dll path\n", stderr);
        return 2;
    }
    HMODULE dll = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) {
        std::fprintf(stderr, "SDL3 load failed: %lu\n",
            static_cast<unsigned long>(GetLastError()));
        return 1;
    }
    const auto version = bind<int (__cdecl*)()>(dll, "SDL_GetVersion");
    const auto ready = bind<void (__cdecl*)()>(dll, "SDL_SetMainReady");
    const auto init = bind<bool (__cdecl*)(std::uint32_t)>(dll, "SDL_InitSubSystem");
    const auto quit = bind<void (__cdecl*)(std::uint32_t)>(dll, "SDL_QuitSubSystem");
    const auto events = bind<void (__cdecl*)(bool)>(dll, "SDL_SetGamepadEventsEnabled");
    const auto update = bind<void (__cdecl*)()>(dll, "SDL_UpdateGamepads");
    const auto pads = bind<std::uint32_t* (__cdecl*)(int*)>(dll, "SDL_GetGamepads");
    const auto release = bind<void (__cdecl*)(void*)>(dll, "SDL_free");
    const auto error = bind<const char* (__cdecl*)()>(dll, "SDL_GetError");
    if (!version || !ready || !init || !quit || !events || !update || !pads || !release || !error) {
        std::fputs("Missing expected SDL3 ABI exports\n", stderr);
        FreeLibrary(dll);
        return 1;
    }
    constexpr int expected_version = 3 * 1000000 + 4 * 1000 + 20;
    if (version() != expected_version) {
        std::fprintf(stderr, "Unexpected SDL3 version: %d\n", version());
        FreeLibrary(dll);
        return 1;
    }
    constexpr std::uint32_t gamepad = 0x00002000u;
    ready();
    if (!init(gamepad)) {
        std::fprintf(stderr, "SDL3 gamepad init failed: %s\n", error());
        FreeLibrary(dll);
        return 1;
    }
    events(false);
    update();
    int count = -1;
    auto* ids = pads(&count);
    const bool ok = ids && count >= 0;
    if (ok)
        std::printf("SDL3 3.4.20 x86: gamepad-only init OK, devices=%d\n", count);
    else
        std::fprintf(stderr, "SDL3 enumeration failed: %s\n", error());
    if (ids) release(ids);
    quit(gamepad);
    FreeLibrary(dll);
    return ok ? 0 : 1;
}
