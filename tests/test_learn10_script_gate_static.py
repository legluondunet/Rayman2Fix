"""Fail-closed static guards for the read-only Learn_10 script-gate research branch.

The CD runtime must never patch shared AIModel script nodes merely because
an opt-in test flag is set. This test does not execute a commercial binary.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ScriptGateMemorySafety(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = (ROOT / "src/runtime/runtime.cpp").read_text(encoding="utf-8")
        cls.header = (ROOT / "include/rayman2fix/learn10_model_alias.hpp").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

    def test_shared_script_writes_disabled_at_compile_time(self):
        self.assertRegex(
            self.header, r"constexpr\s+bool\s+kLearn10SharedScriptWritePermitted\s*=\s*false\s*;")
        self.assertNotRegex(
            self.header, r"kLearn10SharedScriptWritePermitted\s*=\s*true\s*;")

    def test_fail_closed_guard_dominates_first_script_cas(self):
        start = self.cpp.index("void learn10_script_gate_arm(")
        end = self.cpp.index("void learn10_script_gate_poll()", start)
        arm = self.cpp[start:end]
        guard = arm.index("if (!r2fix::kLearn10SharedScriptWritePermitted)")
        blocked = arm.index("phase=blocked reason=shared-AIModel-ownership-unproven")
        cas = arm.index("learn10_proto_compare_exchange(node,before,next)")
        self.assertLess(guard, blocked)
        self.assertLess(blocked, cas)
        # Never allow a bypass that turns the census into a writer gate.
        self.assertNotIn("if (ownership.apparently_unique_now())", arm)
        self.assertNotIn("VirtualProtect(", arm)
        self.assertNotIn("SetBooleanInArray", arm)

    def test_research_build_is_exclusive_with_progress_bit_variant(self):
        self.assertIn(
            "RAYMAN2FIX_LEARN10_SCRIPT_GATE_PROTO AND RAYMAN2FIX_LEARN10_NOMOVIES_PROTO",
            self.cmake)
        self.assertIn(
            "RAYMAN2FIX_LEARN10_SCRIPT_GATE_PROTO AND NOT RAYMAN2FIX_RESEARCH_PROBES",
            self.cmake)
        self.assertIn(
            "RAYMAN2FIX_LEARN10_SCRIPT_GATE_PROTOTYPE", self.cpp)

    def test_live_alias_survey_never_authorizes_writes(self):
        start = self.cpp.index("r2fix::Learn10ModelAliasCensus learn10_script_gate_census()")
        end = self.cpp.index("void learn10_script_gate_poll()", start)
        body = self.cpp[start:end]
        self.assertIn("survey_learn10_model_aliases", body)
        self.assertIn("phase=model-alias-census", body)
        self.assertIn("does not establish lifetime ownership", body)
        self.assertIn("phase=blocked", body)


if __name__ == "__main__":
    unittest.main()
