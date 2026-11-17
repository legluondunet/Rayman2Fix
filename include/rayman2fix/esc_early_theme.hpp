#pragma once
// One-run, explicitly opted-in experiment for Rayman 2's *initial*
// pending Theme handoff. This is NOT proof of the underlying APMMX
// physical voice epoch. The normal class-1 path is completely unchanged.
// Never use this gate for direct driver callbacks or unknown PE builds.
#include <cstdint>

namespace r2fix::esc_early_theme {

struct Evidence {
    bool experimental_enabled=false;
    bool pending_menu=false;
    bool foreground=false;
    bool menu_open=false;
    bool esc_actor_open=false;
    std::uint32_t pending_age_ms=0xffffffffu;
    unsigned pause_callbacks=0;
    unsigned other_callbacks=0;
    unsigned different_handles=0;
    int transition_type=-1;
    int depth_before=-1;
    int depth_after=-1;
    bool global_play_stop_stable=false;
    bool direct_snapshot=false;
    unsigned direct_refs=0;
    unsigned direct_candidates=0;
    unsigned direct_stamped=0;
    bool theme_snapshot=false;
    unsigned type11_sources=0;
    unsigned type11_stamped=0;
    unsigned unknown_types=0;
    unsigned owner_driver=0;
    int native_ref_class=-1;
    int native_pending_state=-1;
    int native_target_matches_outer=-1;
    int native_target_differs_current=-1;
    unsigned alternate_complete=0;
    unsigned alternate_candidates=0;
    int alternate_same_bank=-1;
    unsigned outer_complete=0;
    unsigned inner_complete=0;
    unsigned outer_candidates=0;
    unsigned inner_candidates=0;
    bool original_adpcm_voice_sampled=false;
    int voice_active=-1;
    int voice_paused=-1;
    int voice_other_gate=-1;
};

// This only gates *one* experimental resume decision on a trusted
// render thread. Native Pauses and script Resumes are still owned by
// the original game. The runtime revalidates live Theme/SXD identity
// before each compensating driver call and keeps its existing Stop,
// focus and stream-change quarantine. No generic source snapshots are
// presumed atomic with the original callback dispatch.
constexpr bool can_try_pending_theme_resume(const Evidence& e) noexcept {
    return e.experimental_enabled && e.pending_menu &&
        e.foreground && e.menu_open && e.esc_actor_open &&
        e.pending_age_ms<=100u &&
        e.pause_callbacks==2u && e.other_callbacks==0u &&
        e.different_handles==0u && e.transition_type==4 &&
        e.depth_before==1 && e.depth_after==2 &&
        e.global_play_stop_stable &&
        e.direct_snapshot && e.direct_refs==1u &&
        e.direct_candidates==1u && e.direct_stamped==1u &&
        e.theme_snapshot && e.type11_sources==1u &&
        e.type11_stamped==1u && e.unknown_types==0u &&
        e.owner_driver==1u && e.native_ref_class==3 &&
        e.native_pending_state==1 &&
        e.native_target_matches_outer==1 &&
        e.native_target_differs_current==1 &&
        e.alternate_complete==1u && e.alternate_candidates==1u &&
        e.alternate_same_bank==1 &&
        e.outer_complete==1u && e.inner_complete==1u &&
        e.outer_candidates==1u && e.inner_candidates==1u &&
        e.original_adpcm_voice_sampled &&
        e.voice_active==1 && e.voice_paused==1 &&
        e.voice_other_gate==0;
}

// Once the experiment is approved for a particular ESC attempt,
// re-check the live controller on *every* physical driver call.
// This is only valid while the game's current Theme is the verified
// pending predecessor and its target remains the unique active Theme.
// A synchronized class-1 transition is handled by the normal strict
// path, not by this experimental predicate.
constexpr bool same_pending_theme_owner(
    bool enabled, bool approved_this_attempt,
    unsigned owner_driver, int native_ref_class,
    int pending_state, int target_matches_outer,
    int target_differs_current,
    unsigned alternate_complete, unsigned alternate_candidates,
    int alternate_same_bank) noexcept {
    return enabled && approved_this_attempt &&
           owner_driver==1u && native_ref_class==3 &&
           pending_state==1 && target_matches_outer==1 &&
           target_differs_current==1 &&
           alternate_complete==1u && alternate_candidates==1u &&
           alternate_same_bank==1;
}
} // namespace r2fix::esc_early_theme
