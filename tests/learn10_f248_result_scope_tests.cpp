#include "rayman2fix/learn10_f248_result_scope.hpp"
#include <cstdlib>
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
    CHECK(learn10_f248_enabled_for_ini(1));
    CHECK(!learn10_f248_enabled_for_ini(0));
    CHECK(!learn10_f248_enabled_for_ini(2));
    CHECK(!learn10_f248_enabled_for_ini(0xffffffffu));
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
    c=s; c.elapsed_ms=5001; CHECK(!learn10_f248_scope_matches(c));
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
    return 0;
}
