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
        cls.config=(ROOT / "src/launcher/game_config.cpp").read_text(encoding="utf-8")
        cls.launch=(ROOT / "src/launcher/game_launch.cpp").read_text(encoding="utf-8")
        cls.inject=(ROOT / "src/launcher/runtime_inject.cpp").read_text(encoding="utf-8")

    def test_normal_build_bundles_feature_unconditionally(self):
        self.assertIn("RAYMAN2FIX_LEARN10_F248_FEATURE=1",self.cmake)
        self.assertNotIn("option(RAYMAN2FIX_LEARN10_F248_PROTO",self.cmake)
        self.assertNotIn("RAYMAN2FIX_LEARN10_F248_PROTO",self.runtime)
        self.assertIn("RUNTIME-LEARN10-F248: phase=armed",self.runtime)
        self.assertIn("#if RAYMAN2FIX_LEARN10_F248_FEATURE || RAYMAN2FIX_RESEARCH_PROBES",self.runtime)
        self.assertIn("poll_learn_intro_skip();",self.runtime)

    def test_no_special_env_or_log_gate(self):
        self.assertNotIn("RAYMAN2FIX_LEARN10_F248_PROTOTYPE",self.runtime)
        # Other, unrelated experimental audio controls may legitimately
        # read environment variables; only the F248 block must not.
        start=self.runtime.index("g_f248_enabled.store(")
        f248_config=self.runtime[start:self.runtime.index("#endif",start)]
        self.assertNotIn("GetEnvironmentVariableW",f248_config)
        self.assertNotIn("g_log &&",f248_config)
        self.assertIn("settings->skip_learn10",self.runtime)
        self.assertIn("learn10_f248_enabled_for_ini(settings->skip_cinematics,",self.runtime)
        self.assertNotRegex(self.policy,r"logging\s*=")
        self.assertNotIn("s.logging",self.policy)

    def test_disabled_user_setting_prohibits_slot_installation(self):
        self.assertIn("return skip_cinematics == 1u && skip_learn10 == 1u;",self.policy)
        self.assertIn("!g_f248_enabled.load(std::memory_order_acquire)",self.runtime)
        self.assertIn("if (!g_f248_enabled.load(std::memory_order_acquire) ||",self.runtime)
        self.assertIn("learn10_f248_scope_matches(sample)",self.runtime)
        self.assertNotIn("kLearn10F248RuntimeHookAllowed",self.runtime)

    def test_hidden_default_off_debug_opt_out_is_forwarded(self):
        self.assertIn('read_ini_flag(ini,"Features","SkipCinematics","1")',self.config)
        self.assertIn('!read_ini_flag(ini,"Debug","DisableLearn10Skip","0")',self.config)
        self.assertNotIn('output.emplace_back("SkipLearn10=0")',self.config)
        self.assertNotIn('output.emplace_back("DisableLearn10Skip=0")',self.config)
        self.assertNotIn('output.emplace_back("DisableLearn10Skip=1")',self.config)
        self.assertIn('old_skip_learn10',self.config)
        self.assertIn('configured_skip_learn10(startup.ini_path())',self.launch)
        self.assertIn('display.skip_learn10, display.remove_dialogue_backgrounds',self.launch)
        self.assertIn('skip_learn10 ? 1u : 0u',self.inject)
        self.assertIn('settings->skip_learn10>1u',self.runtime)
        self.assertIn('Debug.DisableLearn10Skip',self.runtime)

    def test_legacy_runtime_message_removed(self):
        self.assertNotIn("Learn_10 skip disabled after visual regression",self.runtime)
        self.assertIn("Learn_10 F248 controlled separately",self.runtime)

    def test_log_off_still_collects_director_identity(self):
        self.assertIn("observe_learn10_f248_director_without_log",self.runtime)
        self.assertIn("sample_natural_director(ray_so)",self.runtime)
        self.assertRegex(self.runtime,r"if\s*\(!completed\)\s*observe_learn10_f248_director_without_log\(elapsed\)")
        self.assertIn("bool completed=handed_over;",self.runtime)
        self.assertIn("completed=r2fix::learn10_replay_handover(",self.runtime)
        self.assertIn("g_learn_replay_visit.active() && !g_learn_handover",self.runtime)

    def test_replay_exception_requires_native_route_and_live_director(self):
        self.assertEqual(self.runtime.count("g_learn_replay_visit.queue("),1)
        self.assertIn("if(mode_after>=0 && mode_after!=int(kIntroEnginePlaying) && next_after==1)",self.runtime)
        self.assertIn("(!handed_over || learn10_live_replay_director(actor,intel,b3))",self.runtime)
        self.assertIn("live.ending_behaviour+24u==b3 && stats.current_behaviour_index==3",self.runtime)
        self.assertIn("(!s.first_visit && !s.verified_replay_director)",self.policy)
        self.assertIn("g_learn_script_probe_done=true;",self.runtime)

    def test_no_obsolete_story_bit_or_script_patch(self):
        self.assertNotIn("RUNTIME-LEARN10-PROTOTYPE: phase=armed",self.runtime)
        self.assertNotIn("RUNTIME-LEARN10-SCRIPT-GATE: phase=armed",self.runtime)
        self.assertIn("game_story_bit(g_progress_words,kNoMoviesCheatBit",self.runtime)
        self.assertIn("InterlockedCompareExchangePointer(",self.runtime)

if __name__ == "__main__":
    unittest.main()
