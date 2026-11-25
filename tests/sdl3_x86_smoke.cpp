// Exercise the actual gamepad acquisition implementation in isolated x86 CI.
// No game binary, video, audio or controller hardware is required.
#include "rayman2fix/sdl_gamepads.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <windows.h>

static_assert(sizeof(void*) == 4, "SDL3 smoke test requires x86");
namespace {
void report(const char* line) { std::puts(line); }
}
int wmain(int argc, wchar_t** argv) {
    namespace fs = std::filesystem;
    using namespace r2fix;
    if (argc != 2 || !fs::path(argv[1]).is_absolute()) return 2;
    const fs::path dll(argv[1]);
    SdlGamepads pads;
    if (pads.initialize(dll.parent_path() / "missing-SDL3.dll", &report) ||
        pads.initialize(fs::path(L"SDL3.dll"), &report)) return 1;
    if (!pads.initialize(dll, &report)) return 1;
    const auto active = pads.poll(true, PadContext::gameplay);
    const auto background = pads.poll(false, PadContext::gameplay);
    const bool neutral = !background.foreground && !pad_actions(background) &&
        pad_movement(background).x == 0 && pad_movement(background).y == 0;
    pads.shutdown();
    if (!neutral || pads.poll(true, PadContext::gameplay).connected ||
        pads.family() != PadLabelFamily::generic) return 1;
    if (!pads.initialize(dll, &report)) return 1;
    (void)pads.poll(true, PadContext::menu);
    pads.shutdown();
    std::printf("SDL3 3.4.20 x86 acquisition: PASS, controller=%u\n", unsigned(active.connected));
    return 0;
}
