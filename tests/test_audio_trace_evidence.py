"""Synthetic-only tests for offline Rayman2Fix audio trace evidence reports."""
import importlib.util
from pathlib import Path
import sys
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "analyze_rayman2_audio_trace.py"
SPEC = importlib.util.spec_from_file_location("rayman2_audio_trace_evidence", SCRIPT)
M = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = M
SPEC.loader.exec_module(M)


def trace(event, sample, ms, depth=1, fg=1, origin="game-script-pause"):
    return (f"RUNTIME-AUDIO-TRACE: event={event} sample={sample} "
            f"t_ms={ms} global_pause_depth={depth} foreground={fg} "
            f"esc_down=1 audio_origin={origin} private_path=KEEP_PRIVATE")


def owner(event, sample, inner=50, bank=73, resource=48,
          candidates=1, matches=1, ref_class=3):
    return (f"RUNTIME-AUDIO-THEME-OWNER: channel={event} sample={sample} "
            f"outer_candidates={candidates} outer_slot=6 theme_bank={bank} "
            f"theme_resource={resource} inner_slot=1 inner_ref_class={ref_class} "
            f"inner_bank={bank} inner_resource={inner} inner_active=1 "
            f"inner_transition=1 inner_handle_matches={matches} "
            f"theme_owned_driver=1 private_handle=KEEP_PRIVATE")


class TraceEvidenceTests(unittest.TestCase):
    def test_double_pause_resume_and_changed_inner_are_not_menu_proof(self):
        data = ["RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
                trace("PauseTransitionSxd", 1, 87431),
                owner("PauseTransitionSxd", 1),
                trace("PauseTransitionSxd", 2, 87432),
                owner("PauseTransitionSxd", 2),
                trace("ResumeTransitionSxd", 1, 106431, origin="game-script-resume"),
                owner("ResumeTransitionSxd", 1),
                trace("ResumeTransitionSxd", 2, 106432, origin="game-script-resume"),
                owner("ResumeTransitionSxd", 2),
                trace("PauseTransitionSxd", 3, 133663),
                owner("PauseTransitionSxd", 3, inner=51),
                trace("StopTransitionSxd", 1, 138032, depth=2, origin="unknown"),
                owner("StopTransitionSxd", 1, inner=51)]
        result = M.summarize("\n".join(data))
        self.assertTrue(result["schema_2_header_seen"])
        self.assertEqual(result["events_sampled"], 6)
        self.assertEqual(result["ownership_records_validated"], 6)
        self.assertEqual(len(result["close_same_driver_event_pairs"]), 2)
        self.assertEqual(len(result["owned_stops_at_nested_depth"]), 1)
        roots = result["theme_controller_resources_observed"]
        self.assertEqual(roots, [{
            "outer_resource": [73, 48],
            "observed_inner_resources": [[73, 50], [73, 51]],
            "inner_resource_changed": True
        }])
        self.assertTrue(result["script_origin_does_not_prove_ESC_menu"])
        self.assertTrue(result["esc_key_down_does_not_prove_ESC_menu"])
        self.assertTrue(result["focus_loss_not_proven_when_missing"])
        self.assertNotIn("KEEP_PRIVATE", str(result))

    def test_pre_driver_menu_snapshots_are_observations_not_esc_proof(self):
        events = [
            trace("PauseTransitionSxd", 1, 8000) +
            " ui_in_game_menu_at_call=0 ui_engine_paused_at_call=0",
            owner("PauseTransitionSxd", 1),
            trace("PauseTransitionSxd", 2, 8001) +
            " ui_in_game_menu_at_call=1 ui_engine_paused_at_call=0",
            owner("PauseTransitionSxd", 2),
        ]
        result = M.summarize("\n".join(events))
        self.assertEqual(len(result["close_same_driver_event_pairs"]), 1)
        pair = result["close_same_driver_event_pairs"][0]
        self.assertEqual(pair["menu_state_at_calls"], [0, 1])
        self.assertEqual(pair["engine_pause_state_at_calls"], [0, 0])
        records = result["generic_script_driver_ui_snapshots"]
        self.assertEqual(
            [x["native_menu_flag_at_call"] for x in records], [0, 1])
        self.assertTrue(all(x["game_menu_visibility_not_proven"]
                            for x in records))
        self.assertNotIn("KEEP_PRIVATE", str(result))

    def test_older_trace_lacking_pre_driver_ui_fields_reports_unknown(self):
        result = M.summarize("\n".join((
            trace("PauseTransitionSxd", 1, 8000),
            owner("PauseTransitionSxd", 1),
        )))
        self.assertEqual(
            result["generic_script_driver_ui_snapshots"][0][
                "native_menu_flag_at_call"], -1)
        self.assertEqual(
            result["generic_script_driver_ui_snapshots"][0][
                "native_engine_pause_at_call"], -1)

    def test_owner_key_mismatch_and_ambiguous_owner_fail_closed(self):
        data = "\n".join((
            trace("PauseTransitionSxd", 1, 10),
            owner("ResumeTransitionSxd", 1),
            trace("PauseTransitionSxd", 2, 11),
            owner("PauseTransitionSxd", 2, candidates=2),
            trace("PauseTransitionSxd", 3, 12),
            owner("PauseTransitionSxd", 3, ref_class=1),
        ))
        result = M.summarize(data)
        self.assertEqual(result["events_sampled"], 3)
        self.assertEqual(result["ownership_lines_unmatched"], 1)
        self.assertEqual(result["ownership_records_validated"], 0)
        self.assertEqual(result["close_same_driver_event_pairs"], [])

    def test_foreground_zero_is_only_observed_field_not_real_focus_origin(self):
        result = M.summarize(trace("PauseSxd", 1, 6, fg=0, origin="unknown"))
        self.assertTrue(result["foreground_zero_seen_in_sampled_events"])
        self.assertFalse(result["focus_loss_not_proven_when_missing"])
        self.assertTrue(result["theme_ownership_does_not_prove_audible_music"])

    def test_native_menu_and_engine_pause_samples_cannot_prove_esc_or_f1(self):
        data = "\n".join((
            "RUNTIME-AUDIO-UI-STATE: t_ms=700 in_game_menu=0 "
            "engine_paused=0 foreground=1 private_path=KEEP_PRIVATE",
            "RUNTIME-AUDIO-UI-STATE: t_ms=900 in_game_menu=1 "
            "engine_paused=0 foreground=1",
            "RUNTIME-AUDIO-UI-STATE: t_ms=1200 in_game_menu=1 "
            "engine_paused=1 foreground=0",
            "RUNTIME-AUDIO-UI-STATE: t_ms=1400 in_game_menu=9 "
            "engine_paused=0 foreground=1",
        ))
        result = M.summarize(data)
        self.assertEqual(
            [(x["in_game_menu"],x["engine_paused"],x["foreground"])
             for x in result["native_ui_state_samples"]],
            [(0,0,1),(1,0,1),(1,1,0)],
        )
        self.assertTrue(result["in_game_menu_flag_cannot_distinguish_ESC_from_F1"])
        self.assertNotIn("KEEP_PRIVATE",str(result))

    def test_empty_log(self):
        result = M.summarize("irrelevant log\nRUNTIME-AUDIO-TRACE: enabled=1 schema=2")
        self.assertEqual(result["events_sampled"], 0)
        self.assertFalse(result["foreground_zero_seen_in_sampled_events"])
        self.assertEqual(result["theme_controller_resources_observed"], [])
        self.assertFalse(result["no_runtime_audio_behavior_changed"] is False)

    def test_non_overlapping_pair_grouping(self):
        data = []
        for n in (1, 2, 3):
            data.extend((trace("PauseTransitionSxd", n, 100+n),
                         owner("PauseTransitionSxd", n)))
        result = M.summarize("\n".join(data))
        # Three records are not evidence of TWO independent double dispatches.
        self.assertEqual(len(result["close_same_driver_event_pairs"]), 1)


if __name__ == "__main__":
    unittest.main()
