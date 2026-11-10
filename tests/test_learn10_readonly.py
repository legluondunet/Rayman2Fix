"""Regressions for the disabled unsafe Learn_10 cinematic shortcut."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class Learn10SafeFallback(unittest.TestCase):
    def test_runtime_contains_no_native_scene_handover(self):
        source=(ROOT/"src/runtime/runtime.cpp").read_text(encoding="utf-8")
        for dangerous in (
            "request_native_scene_finish(",
            "AI_fn_ucChangeComportIntell",
            "find_intro_director(",
        ):
            self.assertNotIn(dangerous, source)
        begin=source.index("void poll_learn_intro_skip()")
        end=source.index("void poll_second_intro_skip()",begin)
        block=source[begin:end]
        for dangerous in (
            "WriteProcessMemory(", "VirtualProtect(",
            "GetAsyncKeyState(", "g_request_intro_level",
        ):
            self.assertNotIn(dangerous, block)
        self.assertIn("skip disabled after visual regression",block)
        self.assertIn('log_natural_intro_snapshot("handover",',block)
        self.assertIn('log_natural_intro_snapshot("during",',block)
        self.assertIn("24500, 25000, 25500",source)
        self.assertIn("g_learn_trace_index",block)
        self.assertIn("while (g_learn_trace_index <",block)
        self.assertIn("missed time checkpoints skipped",block)
        self.assertIn("trace_natural_intro_changes(elapsed)",block)
        self.assertIn("kMaxLearnChangeSnapshots = 8",source)
        self.assertNotIn("CreateThread(",block)
        self.assertIn("sample_natural_director(",source)
        self.assertIn("observe_scene_script(",source)
        self.assertIn("log_compiled_director_model(",source)
        self.assertIn("g_learn_script_probe_done",source)
        self.assertIn("read-only",source)
        self.assertNotIn("AI_fn_ucChangeComportIntell",source)

    def test_retains_jail_20_skip_only(self):
        source=(ROOT/"src/runtime/runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("void poll_second_intro_skip()",source)
        self.assertIn("skip requested for jail_20 -> learn_10",source)

if __name__ == "__main__":
    unittest.main()
