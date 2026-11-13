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
            "session": 1,
            "outer_resource": [73, 48],
            "observed_inner_resources": [[73, 50], [73, 51]],
            "inner_resource_changed": True
        }])
        self.assertTrue(result["script_origin_does_not_prove_ESC_menu"])
        self.assertTrue(result["esc_key_down_does_not_prove_ESC_menu"])
        self.assertTrue(result["focus_loss_not_proven_when_missing"])
        self.assertNotIn("KEEP_PRIVATE", str(result))

    def test_real_loaded_bank_provenance_is_separate_from_Id_halves(self):
        def mk(event, sample, timestamp, bank, index, inner_bank, inner_index):
            return (
                trace(event, sample, timestamp),
                owner(event, sample) +
                f" outer_native_bank={bank} outer_native_index={index}"
                f" outer_bank_candidates=1 outer_bank_list_complete=1"
                f" inner_native_bank={inner_bank} inner_native_index={inner_index}"
                f" inner_bank_candidates=1 inner_bank_list_complete=1"
            )

        data = ["RUNTIME-AUDIO-TRACE: enabled=1 schema=2"]
        for t in (mk("PauseTransitionSxd", 1, 200, 19, 4, 19, 5),
                  mk("PauseTransitionSxd", 2, 201, 19, 4, 19, 5)):
            data.extend(t)
        result = M.summarize("\n".join(data))
        self.assertTrue(result["record_Id_halves_are_not_authenticated_BNM_provenance"])
        self.assertEqual(result["ownership_has_native_loaded_bank_provenance"], 2)
        roots = result["theme_controller_resources_observed"]
        self.assertEqual(roots[0]["outer_resource"], [73, 48])
        self.assertTrue(roots[0][
            "these_are_native_record_Id_halves_not_authenticated_bank_indices"])
        pair = result["close_same_driver_event_pairs"][0]
        self.assertEqual(pair["outer_loaded_bank_and_index"], [19, 4])
        self.assertEqual(pair["inner_loaded_bank_and_index"], [19, 5])
        self.assertNotEqual(pair["outer_resource"],pair["outer_loaded_bank_and_index"])
        self.assertTrue(result["native_loaded_bank_provenance"][0][
            "native_bank_lookup_is_not_audio_or_ESC_proof"])

    def test_incomplete_or_ambiguous_bank_scan_is_unknown(self):
        for fields in (
            "outer_native_bank=19 outer_native_index=4 "
            "outer_bank_candidates=1 outer_bank_list_complete=0",
            "outer_native_bank=19 outer_native_index=4 "
            "outer_bank_candidates=2 outer_bank_list_complete=1",
            "outer_native_bank=19 outer_native_index=-1 "
            "outer_bank_candidates=1 outer_bank_list_complete=1",
            "outer_native_bank=65536 outer_native_index=4 "
            "outer_bank_candidates=1 outer_bank_list_complete=1",
            "",
        ):
            with self.subTest(fields=fields):
                result = M.summarize("\n".join((
                    trace("PauseTransitionSxd",1,10),
                    owner("PauseTransitionSxd",1)+" "+fields,
                )))
                self.assertEqual(result[
                    "ownership_has_native_loaded_bank_provenance"],0)

    def test_mismatched_native_loaded_bank_identity_prevents_pair(self):
        a = " outer_native_bank=19 outer_native_index=4 "
        a += "outer_bank_candidates=1 outer_bank_list_complete=1 "
        a += "inner_native_bank=19 inner_native_index=5 "
        a += "inner_bank_candidates=1 inner_bank_list_complete=1"
        b = a.replace("outer_native_bank=19", "outer_native_bank=20")
        data = "\n".join((
            trace("PauseTransitionSxd",1,10),
            owner("PauseTransitionSxd",1)+a,
            trace("PauseTransitionSxd",2,11),
            owner("PauseTransitionSxd",2)+b,
        ))
        result = M.summarize(data)
        self.assertEqual(result["ownership_has_native_loaded_bank_provenance"],2)
        self.assertEqual(result["close_same_driver_event_pairs"],[])

    def test_legacy_owner_trace_preserves_Theme_without_false_provenance(self):
        result = M.summarize("\n".join((
            trace("PauseTransitionSxd",1,10),
            owner("PauseTransitionSxd",1),
        )))
        self.assertEqual(result["ownership_records_validated"],1)
        self.assertEqual(result["ownership_has_native_loaded_bank_provenance"],0)
        self.assertEqual(result["native_loaded_bank_provenance"],[])
        self.assertTrue(result["record_Id_halves_are_not_authenticated_BNM_provenance"])

    def test_appended_runs_cannot_pair_identical_ids_from_different_sessions(self):
        data = "\n".join((
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            trace("PauseTransitionSxd", 1, 1000),
            owner("PauseTransitionSxd", 1, inner=50),
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            trace("PauseTransitionSxd", 1, 1001),
            owner("PauseTransitionSxd", 1, inner=51),
        ))
        result = M.summarize(data)
        self.assertEqual(result["explicit_sessions_detected"], 2)
        self.assertEqual(result["events_sampled"], 2)
        self.assertEqual(result["ownership_records_validated"], 2)
        self.assertEqual(result["close_same_driver_event_pairs"], [])
        roots = result["theme_controller_resources_observed"]
        self.assertEqual([x["session"] for x in roots], [1, 2])
        self.assertEqual([x["inner_resource_changed"] for x in roots],
                         [False, False])
        self.assertEqual([x["observed_inner_resources"] for x in roots],
                         [[[73, 50]], [[73, 51]]])

    def test_missing_owner_in_first_run_is_not_filled_from_second_run(self):
        result = M.summarize("\n".join((
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            trace("PauseTransitionSxd", 1, 100),
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            owner("PauseTransitionSxd", 1),  # No matching event in THIS run.
        )))
        self.assertEqual(result["explicit_sessions_detected"], 2)
        self.assertEqual(result["events_sampled"], 1)
        self.assertEqual(result["ownership_records_validated"], 0)
        self.assertEqual(result["ownership_lines_unmatched"], 1)

    def test_unmarked_prefix_does_not_share_owner_with_header_run(self):
        result = M.summarize("\n".join((
            trace("PauseTransitionSxd", 1, 100),
            owner("PauseTransitionSxd", 1),
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            trace("PauseTransitionSxd", 1, 101),
            owner("PauseTransitionSxd", 1),
        )))
        self.assertEqual(result["unmarked_prefix_events"], 1)
        self.assertEqual(result["explicit_sessions_detected"], 1)
        self.assertEqual(len(result["theme_controller_resources_observed"]), 2)
        self.assertEqual(result["close_same_driver_event_pairs"], [])

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
