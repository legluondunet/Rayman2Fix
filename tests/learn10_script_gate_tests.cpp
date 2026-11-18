#include "rayman2fix/learn10_script_gate.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if(!(x)) { std::cerr << "FAIL: " #x << "\\n"; std::abort(); } } while(false)

int main() {
  using namespace r2fix;
  std::uint32_t out = 777u;
  CHECK(!may_try_learn10_script_gate(false,true,true,true,true,true));
  CHECK(!may_try_learn10_script_gate(true,false,true,true,true,true));
  CHECK(!may_try_learn10_script_gate(true,true,false,true,true,true));
  CHECK(!may_try_learn10_script_gate(true,true,true,false,true,true));
  CHECK(!may_try_learn10_script_gate(true,true,true,true,false,true));
  CHECK(!may_try_learn10_script_gate(true,true,true,true,true,false));
  CHECK(may_try_learn10_script_gate(true,true,true,true,true,true));

  CHECK(plan_learn10_script_gate_arm(1u,1u,0u,out));
  CHECK(out==0u);
  out=777u;
  CHECK(!plan_learn10_script_gate_arm(0u,1u,0u,out));
  CHECK(out==777u);
  CHECK(!plan_learn10_script_gate_arm(1u,2u,0u,out));
  CHECK(!plan_learn10_script_gate_arm(1u,1u,1u,out));
  CHECK(!plan_learn10_script_gate_arm(2u,1u,0u,out));

  CHECK(plan_learn10_script_gate_restore(0u,1u,0u,out));
  CHECK(out==1u);
  out=777u;
  CHECK(!plan_learn10_script_gate_restore(1u,1u,0u,out));
  CHECK(!plan_learn10_script_gate_restore(0u,2u,0u,out));
  CHECK(!plan_learn10_script_gate_restore(0u,1u,1u,out));
  CHECK(!plan_learn10_script_gate_restore(3u,1u,0u,out));
  CHECK(out==777u);

  CHECK(!restore_learn10_script_gate_due(1000u,1000u,false));
  CHECK(!restore_learn10_script_gate_due(5999u,1000u,false));
  CHECK(restore_learn10_script_gate_due(6000u,1000u,false));
  CHECK(restore_learn10_script_gate_due(1001u,1000u,true));
  CHECK(!restore_learn10_script_gate_due(1000u,0xfffffff0u,false));
  CHECK(restore_learn10_script_gate_due(6000u,0xfffffff0u,false));
  CHECK(kLearn10ScriptLatestStartMs==3000u);
  CHECK(kLearn10ScriptMaxArmedMs==5000u);
  return 0;
}
