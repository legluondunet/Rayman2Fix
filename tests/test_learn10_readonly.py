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
        # The research observer is source-retained but MUST NOT be
        # executed for ordinary release builds. This test is intentionally
        # structural so the same safety policy applies on Linux CI.
        self.assertIn("#define RAYMAN2FIX_RESEARCH_PROBES 0", source)
        self.assertIn("#if RAYMAN2FIX_RESEARCH_PROBES\n    // This is exclusively the disabled Learn_10 research observer.", source)
        self.assertIn("#if RAYMAN2FIX_RESEARCH_PROBES\n        // Read-only hook at VA 0x4710C0", source)
        self.assertIn("if(kResearchProbes && first && frame_relative_reports<4)", source)
        self.assertIn("if (!kResearchProbes) return;", source)
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
        self.assertIn("long_rule ? 8192u : 1024u",source)
        self.assertIn('emit_excerpt("last"',source)
        self.assertIn('emit_excerpt("procedures"',source)
        self.assertIn('emit_excerpt("exit-context"',source)
        self.assertIn("read_director_integer_low_byte(",source)
        self.assertIn("var50_low8=",source)
        self.assertIn("var110_low8=",source)
        self.assertIn("trace_native_exit_gates(elapsed)",block)
        self.assertIn("IntroGateTimeline<48>",source)
        self.assertIn("log_native_gate_history(",source)
        self.assertIn("var110_u32=",source)
        self.assertIn("director-gate-event",source)
        self.assertIn("director-counter=",source)
        self.assertIn("var83_last=",source)
        self.assertIn("var14_last=",source)
        self.assertIn("read_intro_integer(",source)
        self.assertIn("sample(83,g_director_var83)",source)
        self.assertIn("sample(14,g_director_var14)",source)
        self.assertNotIn("kMaxDirectorGateChanges",source)
        self.assertIn("observed, not written",source)
        self.assertIn("var91_low8=",source)
        self.assertIn("scan_ms=",source)
        self.assertIn("read-only",source)
        self.assertNotIn("AI_fn_ucChangeComportIntell",source)

    def test_rejected_counter_only_experiment_is_gone(self):
        runtime=(ROOT/"src/runtime/runtime.cpp").read_text(encoding="utf-8")
        self.assertNotIn("WriteProcessMemory(",runtime)
        self.assertNotIn("RAYMAN2FIX_EXPERIMENT_LEARN10_COUNTER_EXIT",runtime)
        self.assertNotIn("poll_learn10_counter_experiment(",runtime)
        self.assertIn("read_director_phase_integer(",runtime)
        self.assertIn("director-phase=",runtime)
        self.assertIn("phase_valid=0x%02X",runtime)
        self.assertIn("capture_phase()",runtime)
        self.assertIn("g_director_var95={}",runtime)
        self.assertIn("kind=unsigned(type)",runtime)
        self.assertIn("read_director_phase_integer(director.superobject,96",runtime)

    def test_retains_jail_20_skip_only(self):
        source=(ROOT/"src/runtime/runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("void poll_second_intro_skip()",source)
        self.assertIn("skip requested for jail_20 -> learn_10",source)

if __name__ == "__main__":
    unittest.main()
