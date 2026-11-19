"""Static safety regression checks for built-in Learn_10 F248 activation.

This checks integration invariants; it cannot replace an in-game test.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class BuiltinF248Guards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime=(ROOT / "src/runtime/runtime.cpp").read_text(encoding="utf-8")
        cls.cmake=(ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.policy=(ROOT / "include/rayman2fix/learn10_f248_result_scope.hpp").read_text(encoding="utf-8")
        cls.workflow=(ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")

    def test_normal_build_bundles_feature_unconditionally(self):
        self.assertIn("RAYMAN2FIX_LEARN10_F248_FEATURE=1",self.cmake)
        self.assertNotIn("option(RAYMAN2FIX_LEARN10_F248_PROTO",self.cmake)
        self.assertNotIn("RAYMAN2FIX_LEARN10_F248_PROTO",self.runtime)
        self.assertIn("RUNTIME-LEARN10-F248: phase=armed",self.runtime)

    def test_no_special_env_or_log_gate(self):
        self.assertNotIn("RAYMAN2FIX_LEARN10_F248_PROTOTYPE",self.runtime)
        self.assertNotIn("GetEnvironmentVariableW", self.runtime[self.runtime.index("DWORD WINAPI Rayman2Fix_Initialize"):self.runtime.index("DWORD WINAPI Rayman2Fix_Initialize")+1350])
        self.assertIn("learn10_f248_enabled_for_ini(settings->skip_cinematics)",self.runtime)
        self.assertNotRegex(self.policy,r"logging\s*=")
        self.assertNotIn("s.logging",self.policy)

    def test_disabled_user_setting_prohibits_slot_installation(self):
        self.assertIn("return skip_cinematics == 1u;",self.policy)
        self.assertIn("!g_f248_enabled.load(std::memory_order_acquire)",self.runtime)
        self.assertIn("if (!g_f248_enabled.load(std::memory_order_acquire) ||",self.runtime)
        self.assertIn("learn10_f248_scope_matches(sample)",self.runtime)
        self.assertNotIn("kLearn10F248RuntimeHookAllowed",self.runtime)

    def test_log_off_still_collects_director_identity(self):
        self.assertIn("observe_learn10_f248_director_without_log",self.runtime)
        self.assertIn("sample_natural_director(ray_so)",self.runtime)
        self.assertRegex(self.runtime,r"if\s*\(!handed_over\)\s*observe_learn10_f248_director_without_log\(elapsed\)")
        self.assertIn("g_learn_script_probe_done=true;",self.runtime)

    def test_no_obsolete_story_bit_or_script_patch(self):
        self.assertNotIn("RUNTIME-LEARN10-PROTOTYPE: phase=armed",self.runtime)
        self.assertNotIn("RUNTIME-LEARN10-SCRIPT-GATE: phase=armed",self.runtime)
        self.assertIn("game_story_bit(g_progress_words,kNoMoviesCheatBit",self.runtime)
        self.assertIn("InterlockedCompareExchangePointer(",self.runtime)

if __name__ == "__main__":
    unittest.main()
