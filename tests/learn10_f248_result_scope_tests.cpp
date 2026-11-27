#include "rayman2fix/learn10_f248_result_scope.hpp"
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#define CHECK(x) do { if(!(x)) {std::cerr << "FAILED: " #x << '\n'; std::abort();} }while(false)

int main() {
    using namespace r2fix;
    Learn10F248Sample s{};
    s.actor_so=s.expected_director_so=0x4000;
    s.intelligence=s.expected_intelligence=0x5000;
    s.behaviour=s.expected_behaviour_b3=0x6030;
    s.root_node=0x8000;
    s.function_node=0x8008;
    s.function_id=248; s.output_type=6; s.output_value=0;
    s.elapsed_ms=2200;
    s.approved_cd=s.skip_cinematics_enabled=true;
    s.exact_script=s.live_learn10=s.first_visit=true;
    s.nomovies_bit_known_off=s.handover_incomplete=true;
    s.root_is_ifnot=s.function_node_is_type3=s.function_result_known=true;
    CHECK(learn10_f248_enabled_for_ini(1,1)); // default: no debug override
    CHECK(!learn10_f248_enabled_for_ini(1,0)); // DisableLearn10Skip=1
    CHECK(!learn10_f248_enabled_for_ini(0,1)); // SkipCinematics=0
    CHECK(!learn10_f248_enabled_for_ini(0,0));
    CHECK(!learn10_f248_enabled_for_ini(2,1));
    CHECK(!learn10_f248_enabled_for_ini(1,2));
    CHECK(!learn10_f248_enabled_for_ini(0xffffffffu,1));
    CHECK(learn10_f248_scope_matches(s));
    CHECK(kLearn10F248FunctionType==3);
    CHECK(kLearn10F248IntegerResultType==6);
    auto c=s;
    c.actor_so=0x9999; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.intelligence=0x6000; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.behaviour=0x6018; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.function_node=s.root_node+16; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.function_id=249; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.output_type=7; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.output_value=1; CHECK(!learn10_f248_scope_matches(c));
    // Regression: the third owner test attempted a skip after the old
    // five-second deadline. Age alone must not disable the native branch.
    for (auto elapsed : {5000u,5001u,10000u,20000u,25044u,60000u,0xffffffffu}) {
        c=s; c.elapsed_ms=elapsed; CHECK(learn10_f248_scope_matches(c));
        c.handover_incomplete=false; CHECK(!learn10_f248_scope_matches(c));
        c=s; c.elapsed_ms=elapsed; c.live_learn10=false;
        CHECK(!learn10_f248_scope_matches(c));
        c=s; c.elapsed_ms=elapsed; c.behaviour=0x6018;
        CHECK(!learn10_f248_scope_matches(c));
    }
    Learn10F248Lifetime life{};
    life.enabled=life.live_learn10=life.director_readable=true;
    life.same_behaviour=life.same_rule_root=life.story_readable=true;
    CHECK(learn10_f248_disarm_reason(life)==nullptr);
    auto stopped=life;
    stopped.enabled=false;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"feature-disabled")==0);
    stopped=life; stopped.live_learn10=false;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"level-left")==0);
    stopped=life; stopped.story_readable=false;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"story-unreadable")==0);
    stopped=life; stopped.handed_over=true;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"story1091-handover")==0);
    stopped=life; stopped.nomovies=true;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"story1188-changed")==0);
    stopped=life; stopped.director_readable=false;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"director-unreadable")==0);
    stopped=life; stopped.same_behaviour=false;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"director-left-b3")==0);
    stopped=life; stopped.same_rule_root=false;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"script-context-changed")==0);
    c=s; c.exact_script=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.approved_cd=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.first_visit=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.nomovies_bit_known_off=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.handover_incomplete=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.root_is_ifnot=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.function_node_is_type3=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.function_result_known=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.skip_cinematics_enabled=false; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.root_node=0; CHECK(!learn10_f248_scope_matches(c));
    c=s; c.root_node=~std::uintptr_t(0); CHECK(!learn10_f248_scope_matches(c));
    // A set story bit is not sufficient to authorize a replay hook.
    c=s;c.first_visit=false;
    CHECK(!learn10_f248_scope_matches(c));
    c.verified_replay_director=true;
    CHECK(learn10_f248_scope_matches(c));
    c.handover_incomplete=false;CHECK(!learn10_f248_scope_matches(c));
    c=s;c.first_visit=false;c.verified_replay_director=true;c.exact_script=false;
    CHECK(!learn10_f248_scope_matches(c));
    c=s;c.first_visit=false;c.verified_replay_director=true;c.actor_so=0x9999;
    CHECK(!learn10_f248_scope_matches(c));
    stopped=life;stopped.handed_over=true;stopped.verified_replay_director=true;
    CHECK(learn10_f248_disarm_reason(stopped)==nullptr);
    stopped.nomovies=true;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"story1188-changed")==0);
    stopped=life;stopped.handed_over=true;stopped.verified_replay_director=true;
    stopped.same_rule_root=false;
    CHECK(std::strcmp(learn10_f248_disarm_reason(stopped),"script-context-changed")==0);
    Learn10ReplayVisit replay;
    replay.observe(false,true,false,100);CHECK(!replay.active()); // save load
    replay.queue(100);replay.observe(true,false,false,200);CHECK(!replay.active());
    replay.observe(false,false,true,300);replay.observe(false,true,false,500);
    CHECK(replay.active());
    replay.clear();replay.observe(false,true,false,600);CHECK(!replay.active());
    replay.queue(1000);replay.observe(false,false,false,1100);
    replay.observe(false,true,false,1200);CHECK(!replay.active()); // wrong route
    replay.queue(1000);replay.observe(false,true,false,11001);CHECK(!replay.active());
    replay.queue(0xfffffff0u);replay.observe(false,true,false,30u);CHECK(replay.active());
    replay.observe(false,false,true,40u);CHECK(!replay.active());
    CHECK(!learn10_replay_handover(true,true,true,true,false,true,244));
    CHECK(!learn10_replay_handover(true,true,true,true,true,false,244));
    CHECK(!learn10_replay_handover(true,true,true,false,true,true,10000));
    CHECK(!learn10_replay_handover(true,true,false,true,true,true,10000));
    CHECK(!learn10_replay_handover(true,false,true,true,true,true,10000));
    CHECK(!learn10_replay_handover(false,true,true,true,true,true,10000));
    CHECK(learn10_replay_handover(true,true,true,true,true,true,2000));
    CHECK(learn10_replay_handover(true,true,true,true,true,false,3000));
    return 0;
}
