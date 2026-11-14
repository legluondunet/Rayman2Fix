// Native callback tests use a fake mixer only; no driver, DLL or game data.
#include "rayman2fix/esc_music_mixer_bridge.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " " #x "\n"; \
    std::abort(); \
} } while(false)

using namespace r2fix::esc_music;
namespace {
constexpr Generation kStream{9,5,9,7,41};
constexpr Generation kStale{9,5,9,7,40};
constexpr MixerProof kGood{true,true,true,true,true,2,2};
void proof_gates() {
    for(unsigned n=0;n<5;++n) {
        MixerProof p=kGood;
        if(n==0)p.original_cd_verified=false;
        if(n==1)p.current_music_verified=false;
        if(n==2)p.initially_playing_verified=false;
        if(n==3)p.mixer_pause_resume_contract_verified=false;
        if(n==4)p.exact_handle_verified=false;
        MixerBridge m;
        CHECK(!m.bind(kStream,17,p));
        CHECK(!m.bound());
    }
    for(unsigned count: {0u,3u}) {
        MixerProof p=kGood; p.pause_calls=count;
        MixerBridge m;
        CHECK(!m.bind(kStream,17,p));
        p=kGood;p.resume_calls=count;
        CHECK(!m.bind(kStream,17,p));
    }
    MixerBridge m;
    CHECK(!m.bind(kStream,0,kGood));
    CHECK(!m.bind(kStream,0xffffffffu,kGood));
    CHECK(!m.bind(Generation{9,5,9,7,0},17,kGood));
}
void exact_cardinality() {
    MixerBridge m;
    CHECK(m.bind(kStream,17,kGood));
    std::vector<Action> calls;
    auto fake=[&](Action action,std::uint32_t handle)->bool {
        CHECK(handle==17);
        calls.push_back(action);
        return true;
    };
    CHECK(m.deliver(kStream,Action::none,fake)==Delivery::ignored);
    CHECK(m.deliver(kStream,Action::resume,fake)==Delivery::ignored);
    CHECK(!m.physical_paused());
    CHECK(m.deliver(kStream,Action::suppress_paired_native,fake)==Delivery::rejected);
    CHECK(m.deliver(kStream,Action::forward_stop,fake)==Delivery::rejected);
    CHECK(calls.empty());
    CHECK(m.deliver(kStale,Action::resume,fake)==Delivery::rejected);
    CHECK(m.deliver(kStream,Action::pause,fake)==Delivery::delivered);
    CHECK(m.physical_paused());
    CHECK(m.deliver(kStream,Action::pause,fake)==Delivery::ignored);
    CHECK(m.deliver(kStream,Action::resume,fake)==Delivery::delivered);
    CHECK(!m.physical_paused());
    CHECK(m.deliver(kStream,Action::resume,fake)==Delivery::ignored);
    CHECK(calls.size()==4);
    CHECK(calls[0]==Action::pause && calls[1]==Action::pause);
    CHECK(calls[2]==Action::resume && calls[3]==Action::resume);
    CHECK(!m.retire(kStale));
    CHECK(m.retire(kStream));
    CHECK(!m.bound());
    CHECK(m.deliver(kStream,Action::resume,fake)==Delivery::rejected);
    CHECK(calls.size()==4);
}
void one_call_contract() {
    MixerProof p=kGood;
    p.pause_calls=1;p.resume_calls=1;
    MixerBridge m;
    CHECK(m.bind(kStream,22,p));
    unsigned hits=0;
    auto fake=[&](Action,std::uint32_t)->bool {++hits;return true;};
    CHECK(m.deliver(kStream,Action::pause,fake)==Delivery::delivered);
    CHECK(m.deliver(kStream,Action::resume,fake)==Delivery::delivered);
    CHECK(hits==2);
}
void partial_failure_requires_stop_and_fresh_generation() {
    MixerBridge m;
    CHECK(m.bind(kStream,17,kGood));
    unsigned hits=0;
    auto fails_second=[&](Action,std::uint32_t)->bool {
        return ++hits==1;
    };
    CHECK(m.deliver(kStream,Action::pause,fails_second)==Delivery::uncertain);
    CHECK(m.uncertain());
    auto fake=[&](Action,std::uint32_t)->bool {++hits;return true;};
    CHECK(m.deliver(kStream,Action::resume,fake)==Delivery::uncertain);
    CHECK(hits==2);
    CHECK(m.retire(kStream));
    CHECK(m.bind(Generation{9,5,9,7,42},17,kGood));
    CHECK(m.deliver(Generation{9,5,9,7,42},Action::pause,fake)==Delivery::delivered);
    CHECK(hits==4);
}
void integration_with_shadow_and_fake_native_driver() {
    Shadow policy;
    constexpr Proof state_proof{true,true,true,true,true,true};
    CHECK(policy.attach(kStream,state_proof));
    MixerBridge mixer;
    CHECK(mixer.bind(kStream,17,kGood));
    unsigned physical_pauses=0,physical_resumes=0;
    const auto fake=[&](Action action,std::uint32_t handle)->bool {
        CHECK(handle==17);
        if(action==Action::pause)++physical_pauses;
        if(action==Action::resume)++physical_resumes;
        return true;
    };
    CHECK(policy.menu_open(true)==Action::none);
    CHECK(policy.callback(kStream,Kind::pause,Route::theme,Cause::esc)
          ==Action::suppress_paired_native);
    CHECK(policy.callback(kStream,Kind::pause,Route::sxd,Cause::esc)
          ==Action::suppress_paired_native);
    CHECK(physical_pauses==0); // ESC keeps the same music playing
    const Action focus_pause=policy.focus(true,false);
    CHECK(focus_pause==Action::pause);
    CHECK(mixer.deliver(kStream,focus_pause,fake)==Delivery::delivered);
    CHECK(physical_pauses==2 && physical_resumes==0);
    CHECK(policy.other_pause()==Action::none);
    CHECK(policy.menu_close(true)==Action::none);
    CHECK(policy.focus(true,true)==Action::none);
    CHECK(mixer.physical_paused()); // another native pause still owns silence
    const Action other_resume=policy.other_resume();
    CHECK(other_resume==Action::resume);
    CHECK(mixer.deliver(kStream,other_resume,fake)==Delivery::delivered);
    CHECK(physical_pauses==2 && physical_resumes==2);
    // An unknown cause from *inside* a native audio callback must not
    // recursively invoke a second driver callback: defer physical pause.
    CHECK(policy.unknown()==Action::hold_unknown);
    CHECK(policy.has_deferred_pause());
    const Action deferred=policy.take_deferred_pause();
    CHECK(deferred==Action::pause);
    CHECK(mixer.deliver(kStream,deferred,fake)==Delivery::delivered);
    CHECK(physical_pauses==4 && physical_resumes==2);
    CHECK(policy.take_deferred_pause()==Action::none);
    CHECK(policy.stop(kStream)==Action::forward_stop);
    CHECK(mixer.retire(kStream));
    CHECK(mixer.deliver(kStream,Action::resume,fake)==Delivery::rejected);
    CHECK(physical_resumes==2); // Stop never resurrects a dead stream
}
}

int main() {
    proof_gates();exact_cardinality();one_call_contract();
    partial_failure_requires_stop_and_fresh_generation();
    integration_with_shadow_and_fake_native_driver();
    std::cout<<"ESC mixer bridge fake calls: proof gates, duplication, stop, failure OK\n";
}
