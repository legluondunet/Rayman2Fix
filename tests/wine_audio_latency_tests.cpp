#include "rayman2fix/wine_audio_latency.hpp"

#include <iostream>
#include <stdexcept>

using r2fix::WineAudioLatencyAction;
using r2fix::select_wine_audio_latency;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main() {
    check(select_wine_audio_latency(true,true,false) ==
              WineAudioLatencyAction::request_60ms,
          "verified Wine with no existing setting requests 60 ms");
    check(select_wine_audio_latency(true,true,true) ==
              WineAudioLatencyAction::unchanged_user_configured,
          "an existing user setting always wins");
    check(select_wine_audio_latency(true,false,false) ==
              WineAudioLatencyAction::unchanged_native,
          "native Windows must remain unchanged");
    check(select_wine_audio_latency(true,false,true) ==
              WineAudioLatencyAction::unchanged_native,
          "native Windows remains unchanged even with existing env var");
    check(select_wine_audio_latency(false,true,false) ==
              WineAudioLatencyAction::unchanged_unverified,
          "direct unchecked launch never adds an audio override");
    check(select_wine_audio_latency(false,true,true) ==
              WineAudioLatencyAction::unchanged_unverified,
          "unchecked launch must not override a custom value");
    std::cout << "Wine audio child latency policy: all tests passed\n";
    return 0;
}
