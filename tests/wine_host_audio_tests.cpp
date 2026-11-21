#include "rayman2fix/wine_host_audio.hpp"

#include <iostream>
#include <stdexcept>

using r2fix::WineHostAudioAction;
using r2fix::select_wine_host_audio;

static void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}

int main() {
    check(select_wine_host_audio(true,true,false,false) ==
              WineHostAudioAction::reexec_with_60ms,
          "automatic startup on Wine should request a host-side reexec");
    check(select_wine_host_audio(false,true,false,false) ==
              WineHostAudioAction::unchanged_command,
          "--verify / --install-only / --launch-no-verify never reexec");
    check(select_wine_host_audio(true,false,false,false) ==
              WineHostAudioAction::unchanged_native_windows,
          "native Windows never launches a Unix executable");
    check(select_wine_host_audio(true,true,true,false) ==
              WineHostAudioAction::unchanged_explicit_latency,
          "user-defined PULSE_LATENCY_MSEC always wins");
    check(select_wine_host_audio(true,true,false,true) ==
              WineHostAudioAction::unchanged_already_relaunched,
          "reexecuted process never spawns recursively");
    check(select_wine_host_audio(true,false,true,true) ==
              WineHostAudioAction::unchanged_native_windows,
          "native Windows always has priority over internal marker");
    std::cout << "Wine host-audio reexec policy: OK\n";
    return 0;
}
