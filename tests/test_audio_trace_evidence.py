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
            "inner_resource_changed": True,
            "these_are_native_record_Id_halves_not_authenticated_bank_indices": True
        }])
        self.assertTrue(result["script_origin_does_not_prove_ESC_menu"])
        self.assertTrue(result["esc_key_down_does_not_prove_ESC_menu"])
        self.assertTrue(result["focus_loss_not_proven_when_missing"])
        self.assertNotIn("KEEP_PRIVATE", str(result))

    def test_live_like_pause_menu_flag_timing_and_owned_stop_are_not_resume(self):
        # Synthetic timing modeled after an opt-in, read-only Wine trace.
        # Engine pause depth 2 may outlive the visual menu flag. A stop
        # in that window must NEVER be inferred to imply a driver resume.
        bank = (" outer_native_bank=9 outer_native_index=5"
                " outer_bank_candidates=1 outer_bank_list_complete=1"
                " inner_native_bank=9 inner_native_index=6"
                " inner_bank_candidates=1 inner_bank_list_complete=1")
        data = [
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            trace("PauseTransitionSxd", 1, 38779) +
            " ui_in_game_menu_at_call=0 ui_engine_paused_at_call=0",
            owner("PauseTransitionSxd", 1, inner=49) + bank,
            trace("PauseTransitionSxd", 2, 38780) +
            " ui_in_game_menu_at_call=0 ui_engine_paused_at_call=0",
            owner("PauseTransitionSxd", 2, inner=49) + bank,
            "RUNTIME-AUDIO-UI-STATE: t_ms=39162 in_game_menu=1 "
            "engine_paused=0 foreground=1",
            trace("StopTransitionSxd", 1, 50497, depth=2,
                  origin="unknown") +
            " ui_in_game_menu_at_call=0 ui_engine_paused_at_call=0",
            owner("StopTransitionSxd", 1, inner=49) + bank,
            "RUNTIME-AUDIO-UI-STATE: t_ms=50864 in_game_menu=0 "
            "engine_paused=0 foreground=1",
        ]
        result = M.summarize("\n".join(data))
        self.assertEqual(result["observed_transition_counts"],
                         {"pause": 2, "resume": 0, "stop": 1})
        self.assertTrue(result["zero_recorded_resume_is_not_proof_of_no_native_resume"])
        self.assertEqual(result["ownership_has_native_loaded_bank_provenance"], 3)
        self.assertEqual(len(result["close_same_driver_event_pairs"]), 1)
        self.assertEqual(
            result["close_same_driver_event_pairs"][0][
                "outer_loaded_bank_and_index"], [9, 5])
        stop = result["owned_stops_at_nested_depth"][0]
        self.assertEqual(stop["global_depth"], 2)
        self.assertEqual(stop["native_menu_flag_at_call"], 0)
        self.assertEqual(stop["engine_pause_flag_at_call"], 0)
        self.assertEqual(stop["outer_loaded_bank_and_index"], [9, 5])
        self.assertEqual(stop["inner_loaded_bank_and_index"], [9, 6])
        self.assertTrue(
            stop["menu_flag_zero_is_not_a_resume_or_stop_reason_proof"])
        self.assertTrue(result["in_game_menu_flag_cannot_distinguish_ESC_from_F1"])

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

    def test_real_log_pattern_menu_flag_bracketing_without_ESC_claim(self):
        # Synthetic reduction of the owner's Nov 13 timing: scripted
        # driver pause precedes positive VM menu flag; native stop at
        # depth 2 observes flag already zero before next UI poll.
        observations = [
            {"session": 1, "ms": 1332, "value": 0,
             "source": "periodic-ui-poll-change"},
            {"session": 1, "ms": 38779, "value": 0,
             "source": "before-sampled-audio-driver-call"},
            {"session": 1, "ms": 38780, "value": 0,
             "source": "before-sampled-audio-driver-call"},
            {"session": 1, "ms": 39162, "value": 1,
             "source": "periodic-ui-poll-change"},
            {"session": 1, "ms": 50497, "value": 0,
             "source": "before-sampled-audio-driver-call"},
            {"session": 1, "ms": 50864, "value": 0,
             "source": "periodic-ui-poll-change"},
        ]
        windows, invalid = M.bracket_observed_menu_flag_changes(observations)
        self.assertEqual(invalid, 0)
        self.assertEqual(len(windows), 2)
        opened, closed = windows
        self.assertEqual(opened["observed_change"], "0-to-1")
        self.assertEqual((opened["earliest_bound_ms"], opened["latest_bound_ms"]),
                         (38780, 39162))
        self.assertEqual(opened["bounds_width_ms"], 382)
        self.assertEqual(closed["observed_change"], "1-to-0")
        self.assertEqual((closed["earliest_bound_ms"], closed["latest_bound_ms"]),
                         (39162, 50497))
        self.assertEqual(closed["new_sample_source"],
                         "before-sampled-audio-driver-call")
        self.assertTrue(all(w["not_proof_of_ESC_or_F1_menu"] for w in windows))
        self.assertTrue(all(w["intermediate_state_changes_cannot_be_excluded"]
                            for w in windows))
        self.assertTrue(all(w["exact_change_time_not_observed"] for w in windows))

    def test_unknown_snapshot_breaks_continuity_and_sessions_stay_isolated(self):
        observations = [
            {"session": 1, "ms": 5, "value": 0, "source": "poll"},
            {"session": 1, "ms": 6, "value": -1, "source": "call"},
            {"session": 2, "ms": 5, "value": 1, "source": "poll"},
            {"session": 1, "ms": 7, "value": 1, "source": "call"},
            {"session": 1, "ms": 8, "value": 0, "source": "poll"},
        ]
        windows, skipped=M.bracket_observed_menu_flag_changes(observations)
        self.assertEqual(skipped,0)
        self.assertEqual(len(windows),1)
        self.assertEqual(windows[0]["session"],1)
        self.assertEqual(windows[0]["observed_change"],"1-to-0")
        self.assertEqual((windows[0]["earliest_bound_ms"],
                          windows[0]["latest_bound_ms"]), (7,8))
        self.assertTrue(windows[0]["not_proof_of_ESC_or_F1_menu"])

    def test_old_trace_without_ui_fields_cannot_invent_change_bounds(self):
        summary=M.summarize("\n".join((
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            "RUNTIME-AUDIO-UI-STATE: t_ms=5 in_game_menu=0 "
            "engine_paused=0 foreground=1",
            trace("PauseTransitionSxd", 1, 6), # old schema2 lacks UI fields
            "RUNTIME-AUDIO-UI-STATE: t_ms=7 in_game_menu=1 "
            "engine_paused=0 foreground=1",
        )))
        self.assertEqual(summary["observed_native_menu_flag_change_windows"], [])

    def test_timestamp_reset_does_not_create_false_menu_flag_interval(self):
        observations = [
            {"session": 1, "ms": 200, "value": 1, "source": "poll"},
            {"session": 1, "ms": 12, "value": 0, "source": "call"},
            {"session": 1, "ms": 13, "value": 1, "source": "poll"},
        ]
        windows, nonmonotonic = M.bracket_observed_menu_flag_changes(
            observations)
        self.assertEqual(nonmonotonic, 1)
        self.assertEqual(len(windows), 1)
        self.assertEqual((windows[0]["earliest_bound_ms"],
                          windows[0]["latest_bound_ms"]), (12,13))

    def test_parser_combines_audio_call_and_poll_snapshots_in_file_order(self):
        lines = [
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2",
            "RUNTIME-AUDIO-UI-STATE: t_ms=100 in_game_menu=0 "
            "engine_paused=0 foreground=1",
            trace("PauseTransitionSxd",1,1000) +
            " ui_in_game_menu_at_call=0 ui_engine_paused_at_call=0",
            "RUNTIME-AUDIO-UI-STATE: t_ms=1382 in_game_menu=1 "
            "engine_paused=0 foreground=1",
            trace("StopTransitionSxd",1,2000,depth=2,origin="unknown") +
            " ui_in_game_menu_at_call=0 ui_engine_paused_at_call=0",
            "RUNTIME-AUDIO-UI-STATE: t_ms=2382 in_game_menu=0 "
            "engine_paused=0 foreground=1",
        ]
        summary=M.summarize("\n".join(lines))
        windows=summary["observed_native_menu_flag_change_windows"]
        self.assertEqual([x["observed_change"] for x in windows],
                         ["0-to-1","1-to-0"])
        self.assertEqual([(x["earliest_bound_ms"],x["latest_bound_ms"])
                          for x in windows],[(1000,1382),(1382,2000)])
        self.assertTrue(summary[
            "menu_windows_are_not_confirmed_ESC_UI_lifetimes"])

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
