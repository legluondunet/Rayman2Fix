// Clean-room Rayman2Fix runtime skeleton: NO hooks or patches enabled yet.
// 32-bit Windows DLL only. A verified, per-build loader and the SDL3/GameFrame
// integrations will be added in separate PRs after disc fingerprints are known.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" __declspec(dllexport) unsigned int __cdecl Rayman2Fix_RuntimeApiVersion() {
    return 1;
}

extern "C" __declspec(dllexport) const char* __cdecl Rayman2Fix_RuntimeStatus() {
    return "bootstrap-only; no executable hooks or graphics/input integration";
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    // Do not spawn threads, initialize SDL, hook APIs or load other DLLs
    // from DllMain: Windows loader lock is held.
    return TRUE;
}
