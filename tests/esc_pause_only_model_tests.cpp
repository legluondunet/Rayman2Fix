// Synthetic flag-only evidence: never link to original DLL or game code.
#include "rayman2fix/esc_pause_only_model.hpp"
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
using namespace r2fix::esc_pause_only_research;

void require(bool condition) { if (!condition) std::abort(); }

int main() {
    // Exhaust all four immediate-suppression combinations. One remaining
    // original Pause sets the same physical flag, preventing music during
    // the menu. Both original Resumes still clear that same flag on exit.
    for (unsigned mask=0; mask<4; ++mask) {
        const bool skip_first=(mask&1u)!=0;
        const bool skip_second=(mask&2u)!=0;
        const auto o=evaluate(Evidence::stable_single_voice,
                              false,skip_first,skip_second);
        require(o.model_applicable);
        require(o.both_pauses_suppressed==(mask==3u));
        require(o.paused_while_esc==(mask!=3u));
        require(!o.paused_after_native_resume);
        require(!o.runtime_interception_authorized);
    }

    // An already paused record might belong to focus / another pause;
    // it is OUTSIDE the model and must not be unpaused speculatively.
    const auto already=evaluate(Evidence::stable_single_voice,true,true,true);
    require(!already.model_applicable);
    require(!already.runtime_interception_authorized);

    // A missing native Resume is unproven even in this narrow model.
    for (unsigned n : {0u,1u,3u}) {
        const auto o=evaluate(Evidence::stable_single_voice,
                              false,true,true,n);
        require(!o.model_applicable);
        require(!o.runtime_interception_authorized);
    }

    // Real-world invalidation cases are *unknown*, not successful bypasses:
    // asynchronous retire/reuse (ABA), focus, nested pause, new music,
    // and non-paired / unexpected native callback sequences.
    for (Evidence bad : {
             Evidence::unknown_handle_or_epoch,
             Evidence::focus_or_nested_pause,
             Evidence::native_voice_changed,
             Evidence::unexpected_callback_sequence}) {
        for (unsigned mask=0;mask<4;++mask) {
            const auto o=evaluate(bad,false,(mask&1u)!=0,(mask&2u)!=0);
            require(!o.model_applicable);
            require(!o.runtime_interception_authorized);
        }
    }
    return 0;
}
