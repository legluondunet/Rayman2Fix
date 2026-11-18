#include "rayman2fix/learn10_model_alias.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << '\n'; std::abort(); } } while(false)

int main() {
  using namespace r2fix;
  using A=std::uintptr_t;
  std::unordered_map<A,std::uint32_t> mem;
  auto put=[&](A a,std::uint32_t x){mem[a]=x;};
  auto rd=[&](A a,std::uint32_t& x)->bool {
    auto it=mem.find(a);
    if (it==mem.end()) return false;
    x=it->second;
    return true;
  };
  const A active=0x1000,inactive=0x2000,director=0x3000;
  put(active+8,director);
  put(inactive+8,0);
  auto actor=[&](A so,A parent,A next,A engine,A brain,A mind,A model) {
    put(so,2); put(so+0x1c,parent);put(so+8,0);put(so+0x14,next);
    put(so+4,engine);put(engine+0x0c,brain);put(brain,mind);put(mind,model);
  };
  actor(director,active,0,0x4000,0x5000,0x6000,0x7000);
  auto r=survey_learn10_model_aliases(active,inactive,director,0x7000,rd);
  CHECK(r.complete && r.director_seen && r.director_matches_model);
  CHECK(r.actors_seen==1 && r.matching_model_actors==1);
  CHECK(r.apparently_unique_now() && !r.shared_now());
  CHECK(!kLearn10SharedScriptWritePermitted); // current uniqueness is NOT ownership

  // Another actor using the same AIModel makes the sharing explicit.
  put(director+0x14,0x3100);
  actor(0x3100,active,0,0x4100,0x5100,0x6100,0x7000);
  r=survey_learn10_model_aliases(active,inactive,director,0x7000,rd);
  CHECK(r.complete && r.shared_now() && r.matching_model_actors==2);
  CHECK(!r.apparently_unique_now());

  // An actor with a different model does not count as an alias.
  put(0x6100,0x7100);
  r=survey_learn10_model_aliases(active,inactive,director,0x7000,rd);
  CHECK(r.complete && !r.shared_now() && r.matching_model_actors==1);

  // A malformed actor, cycle, missing hierarchy or unknown ownership
  // must never yield a complete authorisation.
  mem.erase(0x5100);
  r=survey_learn10_model_aliases(active,inactive,director,0x7000,rd);
  CHECK(!r.complete && r.unreadable_actors==1);
  put(0x5100,0x6100);
  put(0x3100+0x14,director);
  r=survey_learn10_model_aliases(active,inactive,director,0x7000,rd);
  CHECK(!r.complete);
  put(0x3100+0x14,0);
  put(0x3100+0x1c,inactive);
  r=survey_learn10_model_aliases(active,inactive,director,0x7000,rd);
  CHECK(!r.complete);
  put(0x3100+0x1c,active);
  r=survey_learn10_model_aliases(active,inactive,0x8888,0x7000,rd);
  CHECK(!r.complete && !r.director_seen);
  r=survey_learn10_model_aliases(active,active,director,0x7000,rd);
  CHECK(!r.complete);
  r=survey_learn10_model_aliases(active,inactive,director,0x7000,rd,1);
  CHECK(!r.complete);

  // Unique in a snapshot is not proof of exclusive ownership across
  // future spawns, unloaded resources or another engine registry.
  CHECK(!kLearn10SharedScriptWritePermitted);
  return 0;
}
