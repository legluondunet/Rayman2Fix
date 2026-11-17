#include <cstdlib>
#include <iostream>
#include "rayman2fix/esc_early_theme.hpp"

using namespace r2fix::esc_early_theme;
#define CHECK(x) do { if(!(x)) { std::cerr << "line " << __LINE__ << ": " << #x << "\n"; std::abort(); } } while(0)
Evidence approved() {
    Evidence e{};
    e.experimental_enabled=true;
    e.pending_menu=true;
    e.foreground=true;
    e.menu_open=true;
    e.esc_actor_open=true;
    e.pending_age_ms=16u;
    e.pause_callbacks=2;
    e.other_callbacks=0;
    e.different_handles=0;
    e.transition_type=4;
    e.depth_before=1;
    e.depth_after=2;
    e.global_play_stop_stable=true;
    e.direct_snapshot=true;
    e.direct_refs=e.direct_candidates=e.direct_stamped=1;
    e.theme_snapshot=true;
    e.type11_sources=e.type11_stamped=1;
    e.unknown_types=0;
    e.owner_driver=1;
    e.native_ref_class=3;
    e.native_pending_state=1;
    e.native_target_matches_outer=1;
    e.native_target_differs_current=1;
    e.alternate_complete=e.alternate_candidates=1;
    e.alternate_same_bank=1;
    e.outer_complete=e.inner_complete=1;
    e.outer_candidates=e.inner_candidates=1;
    e.original_adpcm_voice_sampled=true;
    e.voice_active=e.voice_paused=1;
    e.voice_other_gate=0;
    return e;
}
#define DENY(field,value) do { auto n=approved();n.field=value;CHECK(!can_try_pending_theme_resume(n)); } while(0)
int main() {
    const auto normal=approved();
    CHECK(can_try_pending_theme_resume(normal));
    // Every individual safety prerequisite is an independent fail-close.
    DENY(experimental_enabled,false);
    DENY(pending_menu,false);
    DENY(foreground,false);
    DENY(menu_open,false);
    DENY(esc_actor_open,false);
    DENY(pending_age_ms,101u);
    DENY(pause_callbacks,1u);
    DENY(pause_callbacks,3u);
    DENY(other_callbacks,1u);
    DENY(different_handles,1u);
    DENY(transition_type,2);
    DENY(depth_before,0);
    DENY(depth_after,3);
    DENY(global_play_stop_stable,false);
    DENY(direct_snapshot,false);
    DENY(direct_refs,0u);
    DENY(direct_refs,2u);
    DENY(direct_candidates,0u);
    DENY(direct_stamped,0u);
    DENY(theme_snapshot,false);
    DENY(type11_sources,2u);
    DENY(type11_stamped,0u);
    DENY(unknown_types,1u);
    DENY(owner_driver,0u);
    DENY(native_ref_class,1);
    DENY(native_pending_state,0);
    DENY(native_target_matches_outer,0);
    DENY(native_target_differs_current,0);
    DENY(alternate_complete,0u);
    DENY(alternate_candidates,2u);
    DENY(alternate_same_bank,0);
    DENY(outer_complete,0u);
    DENY(inner_complete,0u);
    DENY(outer_candidates,2u);
    DENY(inner_candidates,2u);
    DENY(original_adpcm_voice_sampled,false);
    DENY(voice_active,0);
    DENY(voice_paused,0);
    DENY(voice_other_gate,1);
    CHECK(same_pending_theme_owner(true,true,1,3,1,1,1,1,1,1));
    CHECK(!same_pending_theme_owner(false,true,1,3,1,1,1,1,1,1));
    CHECK(!same_pending_theme_owner(true,false,1,3,1,1,1,1,1,1));
    CHECK(!same_pending_theme_owner(true,true,1,1,0,1,1,1,1,1));
    CHECK(!same_pending_theme_owner(true,true,1,3,0,1,1,1,1,1));
    CHECK(!same_pending_theme_owner(true,true,1,3,1,0,1,1,1,1));
    CHECK(!same_pending_theme_owner(true,true,1,3,1,1,0,1,1,1));
    CHECK(!same_pending_theme_owner(true,true,1,3,1,1,1,1,2,1));
    CHECK(!same_pending_theme_owner(true,true,1,3,1,1,1,1,1,0));
    std::cout << "early Theme safety gate: normal and failure cases OK\n";
}
