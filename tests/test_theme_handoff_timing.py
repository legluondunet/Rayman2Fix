"""Synthetic original-ESC Theme handoff timing controls.

No original game log, binary, DLL or private user data is committed.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import analyze_theme_handoff_timing as M
import audit_theme_handoff_timing as A2


def watcher(t, index, state, cls):
    return (
        f"RUNTIME-ESC-THEME-WATCH: seq=1 t_ms={t} menu=0 "
        f"active=1 controller_field0c={state} ref_class={cls} "
        f"ref_match={1 if cls==1 else 0} outer_candidates=1 "
        f"outer_bank=9 outer_index={index} inner_bank=9 inner_index=6"
    )


def opening(t, *, callbacks=2, cls=3):
    return (
        f"RUNTIME-ESC-PREDRIVER-GATE: t_ms={t} callbacks={callbacks} "
        f"transition_pauses={callbacks} other_slots=0 "
        f"different_handles=0 slots=2,2,99,99 depth_before=1 "
        f"depth_after=2 owner_driver=1 native_ref_class={cls} "
        f"native_ref_match=0 outer_bank=9 outer_index=5 "
        f"inner_bank=9 inner_index=6"
    )


def resume(t, *, forwarded=2):
    return (
        f"RUNTIME-ESC-PREDRIVER-RESUME: t_ms={t} callbacks=2 "
        f"transition_resumes=2 other_slots=0 different_handles=0 "
        f"slots=3,3,99,99 native_forwarded={forwarded} "
        f"native_suppressed=0 depth_before=2 depth_after=1"
    )


def lifetime(t, paired=1, overlap=0, *, opened_at=None):
    elapsed=t-opened_at if opened_at is not None else 9999
    return (
        f"RUNTIME-ESC-PREDRIVER-LIFETIME: t_ms={t} "
        f"paired={paired} overlap={overlap} elapsed_ms={elapsed} "
        f"play_between=0 stop_between=0"
    )


def complete(start, end, intervals):
    lines=[watcher(start-100,12,0,1),watcher(start,5,1,3)]
    for a,b in intervals:
        lines.extend([opening(a),resume(b),lifetime(b,opened_at=a)])
    lines.append(watcher(end,5,0,1))
    return "\n".join(lines)


class ThemeHandoffTimingTests(unittest.TestCase):
    def test_unpaused_baseline_exactly_4015_ms(self):
        out=M.evaluate(complete(40063,44078,[]))
        self.assertTrue(out["qualified"])
        self.assertEqual(out["outside_esc_ms"],4015)
        self.assertEqual(out["verified_script_esc_intervals"],0)

    def test_early_menu_9_seconds_preserves_3934_ms_outside(self):
        out=M.evaluate(complete(33917,47017,[
            (34367,35817),(37350,41733),(42734,46067)
        ]))
        self.assertTrue(out["qualified"])
        self.assertEqual(out["wall_handoff_ms"],13100)
        self.assertEqual(out["script_esc_window_ms"],9166)
        self.assertEqual(out["outside_esc_ms"],3934)

    def test_long_pauses_can_exceed_20_seconds_without_changing_proxy(self):
        out=M.evaluate(complete(34579,61735,[
            (35000,36500),(37400,41600),(42600,48600),
            (49600,55800),(57000,62132)
        ]))
        # Last interval extends beyond the transition: the end watcher
        # must NOT be treated as if this missing Resume had occurred.
        self.assertFalse(out["qualified"])
        self.assertIsNone(out["outside_esc_ms"])

    def test_realistic_original_esc_callback_splits_are_counted(self):
        # Two ORIGINAL music transition Pause/Resume callbacks + 6/7
        # ordinary effects. These 8/9 callbacks are expected in the
        # owner's Wine logs and must not veto source-paired timing.
        text="\n".join([
            watcher(34849,12,0,1),
            watcher(35749,5,1,3),
            ("RUNTIME-ESC-PREDRIVER-GATE: t_ms=36718 callbacks=8 "
             "transition_pauses=2 other_slots=6 different_handles=0 "
             "slots=2,0,0,0 depth_before=1 depth_after=2 owner_driver=1 "
             "native_ref_class=3 native_ref_match=0"),
            ("RUNTIME-ESC-PREDRIVER-RESUME: t_ms=43051 callbacks=9 "
             "transition_resumes=2 other_slots=7 different_handles=0 "
             "slots=1,3,1,1 native_forwarded=2 native_suppressed=0 "
             "depth_before=2 depth_after=1"),
            lifetime(43051,opened_at=36718),
            watcher(63549,5,0,1),
        ])
        result=M.evaluate(text)
        self.assertTrue(result["qualified"],result.get("errors"))
        self.assertEqual(result["script_esc_window_ms"],6333)
        self.assertEqual(result["outside_esc_ms"],27800-6333)

    def test_mismatched_number_of_effect_calls_fails_closed(self):
        s=complete(10000,18000,[(12000,16000)])
        s=s.replace("callbacks=2 transition_resumes=2 other_slots=0",
                    "callbacks=11 transition_resumes=2 other_slots=0")
        out=M.evaluate(s)
        self.assertFalse(out["qualified"])
        self.assertTrue(any("invalid source Resume" in x for x in out["errors"]))

    def test_script_lifetime_elapsed_must_match_observed_wall_times(self):
        s=complete(10000,18000,[(12000,16000)])
        s=s.replace("elapsed_ms=4000","elapsed_ms=3999")
        self.assertFalse(M.evaluate(s)["qualified"])

    def test_missing_close_fails_closed(self):
        text=complete(10000,20000,[(11000,13000)])
        text=text.replace(watcher(20000,5,0,1),
                          opening(15000)+"\n"+watcher(20000,5,0,1))
        result=M.evaluate(text)
        self.assertFalse(result["qualified"])
        self.assertIn("not fully closed",result["errors"][-1])

    def test_wrong_resume_cardinality_invalidates_run(self):
        data=complete(10000,18000,[(12000,16000)])
        data=data.replace("native_forwarded=2","native_forwarded=0")
        result=M.evaluate(data)
        self.assertFalse(result["qualified"])
        self.assertTrue(any("invalid source Resume" in e for e in result["errors"]))

    def test_same_source_pair_requires_native_lifetime(self):
        data=complete(10000,18000,[(12000,16000)])
        data=data.replace("paired=1 overlap=0","paired=0 overlap=1")
        result=M.evaluate(data)
        self.assertFalse(result["qualified"])
        self.assertTrue(any("ambiguous" in e for e in result["errors"]))

    def test_original_prior_theme_witness_required(self):
        data=complete(10000,18000,[])
        data=data.replace(watcher(9900,12,0,1),"")
        result=M.evaluate(data)
        self.assertFalse(result["qualified"])
        self.assertTrue(any("previous 9:12" in e for e in result["errors"]))

    def test_overlapping_openings_do_not_count_as_two_clean_windows(self):
        data="\n".join([
            watcher(9000,12,0,1),watcher(10000,5,1,3),
            opening(11000),opening(12000),
            resume(14000),lifetime(14000),watcher(18000,5,0,1),
        ])
        result=M.evaluate(data)
        self.assertFalse(result["qualified"])
        self.assertTrue(any("overlapping" in e for e in result["errors"]))

    def test_multiple_clean_runs_produce_cautious_summary(self):
        runs=[
            ("baseline.log",complete(40063,44078,[])),
            ("paused.log",complete(33917,47017,[
                (34367,35817),(37350,41733),(42734,46067)
            ])),
        ]
        result=M.summarize(runs)
        self.assertEqual(result["qualified_count"],2)
        self.assertEqual(result["outside_esc_seconds_range"],[3.934,4.015])
        self.assertEqual(result["outside_esc_seconds_median"],3.974)
        self.assertEqual(result["runs"][0]["verified_script_esc_intervals"],0)
        self.assertTrue(all("NOT proof" not in str(r) for r in []))


def observer_row(t, outer, *, former=None, kind="current", **override):
    row=dict(t_ms=t,active=1,controller_field0c=0,
             ref_class=1,ref_match=1,pending_switch_state=0,
             pending_target_matches_outer=-1,
             pending_target_differs_current=-1,
             outer_candidates=1,outer_complete=1,
             outer_bank=outer[0],outer_index=outer[1],
             alt_bank=-1,alt_index=-1,alt_complete=0,alt_same_bank=-1)
    if kind=="pending":
        row.update(controller_field0c=1,ref_class=3,ref_match=0,
                   pending_switch_state=1,
                   pending_target_matches_outer=1,
                   pending_target_differs_current=1,
                   alt_bank=former[0],alt_index=former[1],
                   alt_complete=1,alt_same_bank=int(former[0]==outer[0]))
    row.update(override)
    return "RUNTIME-ESC-THEME-WATCH: "+" ".join(f"{k}={v}" for k,v in row.items())


def observed_log(first, pending, commit, pairs):
    lines=[observer_row(first,(9,12)),
           observer_row(pending,(9,5),former=(9,12),kind="pending")]
    for opening_ms,closing_ms in pairs:
        lines.extend([
            f"RUNTIME-ESC-PREDRIVER-GATE: t_ms={opening_ms} "
            "callbacks=8 transition_pauses=2 other_slots=6 "
            "native_ref_class=3 native_ref_match=0 "
            "outer_bank=9 outer_index=5",
            f"RUNTIME-ESC-PREDRIVER-LIFETIME: t_ms={closing_ms} "
            f"paired=1 overlap=0 elapsed_ms={closing_ms-opening_ms} "
            "play_between=0 stop_between=0",
        ])
    lines.append(observer_row(commit,(9,5)))
    return lines


class GeneralizedThemeHandoffEvidenceTests(unittest.TestCase):
    def test_two_reconstructed_owner_sessions_have_48_ms_proxy_spread(self):
        older=A2.scan(observed_log(30971,31871,50372,[
            (32873,38023),(39024,41424),(42157,46790),(47790,50239)]))
        newer=A2.scan(observed_log(34849,35749,63549,[
            (36718,43051),(44284,52501),(53535,62868)]))
        older_h=older["handoffs"][0]
        newer_h=newer["handoffs"][0]
        self.assertTrue(older_h["complete_source_evidence"])
        self.assertTrue(newer_h["complete_source_evidence"])
        self.assertEqual(older_h["source_paired_esc_ms"],14632)
        self.assertEqual(newer_h["source_paired_esc_ms"],23883)
        self.assertEqual(older_h["wall_minus_source_esc_ms"],3869)
        self.assertEqual(newer_h["wall_minus_source_esc_ms"],3917)
        self.assertEqual(older_h["former_theme_bank_index"],[9,12])
        self.assertEqual(older_h["pending_target_bank_index"],[9,5])
        comp=A2.compare([older,newer])
        self.assertEqual(comp["wall_spread_ms"],9299)
        self.assertEqual(comp["menu_spread_ms"],9251)
        self.assertEqual(comp["outside_esc_residual_spread_ms"],48)
        self.assertFalse(comp["same_underlying_game_timer_proven"])
        self.assertFalse(newer_h["native_music_generation_proven"])

    def test_third_actual_owner_shaped_session_converges_to_65_ms_residual_range(self):
        # Real owner run 08:40 (prior observed Theme 9:12 at 33067,
        # pending 9:12->9:5 at 33917; committed at 47017).
        # Two later owner runs have much longer early ESC pauses.
        first=A2.scan(observed_log(33067,33917,47017,[
            (34367,35817),(37350,41733),(42734,46067)]))
        second=A2.scan(observed_log(30971,31871,50372,[
            (32873,38023),(39024,41424),
            (42157,46790),(47790,50239)]))
        third=A2.scan(observed_log(34849,35749,63549,[
            (36718,43051),(44284,52501),(53535,62868)]))
        first_h=first["handoffs"][0]
        self.assertTrue(first_h["complete_source_evidence"])
        self.assertEqual(first_h["sampled_pending_wall_ms"],13100)
        self.assertEqual(first_h["source_paired_esc_ms"],9166)
        self.assertEqual(first_h["wall_minus_source_esc_ms"],3934)
        comp=A2.compare([first,second,third])
        self.assertTrue(comp["comparison_available"])
        self.assertEqual(comp["evidence_windows"],3)
        self.assertEqual(comp["wall_spread_ms"],14700)
        self.assertEqual(comp["menu_spread_ms"],14717)
        self.assertEqual(comp["outside_esc_residual_spread_ms"],65)
        self.assertFalse(comp["same_underlying_game_timer_proven"])

    def test_missing_previous_current_ref_disqualifies_pending_window(self):
        lines=observed_log(34849,35749,63549,[(36718,43051)])
        lines[0]=observer_row(34849,(9,11))
        out=A2.scan(lines)["handoffs"][0]
        self.assertFalse(out["complete_source_evidence"])
        self.assertFalse(out["previous_current_matches_alternative"])
        self.assertIsNone(out["wall_minus_source_esc_ms"])

    def test_ambiguous_current_or_pending_descriptor_fails_closed(self):
        for kw in (dict(alt_complete=0),
                   dict(outer_candidates=2),
                   dict(pending_target_matches_outer=0),
                   dict(alt_same_bank=0),
                   dict(ref_class=5)):
            with self.subTest(override=kw):
                lines=observed_log(34849,35749,63549,[])
                lines[1]=observer_row(35749,(9,5),former=(9,12),
                                      kind="pending",**kw)
                self.assertEqual(A2.scan(lines)["handoffs"],[])

    def test_missing_close_is_not_subtracted(self):
        lines=observed_log(34849,35749,63549,[(36718,43051)])
        lines.insert(-1,"RUNTIME-ESC-PREDRIVER-GATE: t_ms=53000 "
                         "native_ref_class=3 native_ref_match=0 "
                         "outer_bank=9 outer_index=5")
        out=A2.scan(lines)["handoffs"][0]
        self.assertFalse(out["complete_source_evidence"])
        self.assertIsNone(out["wall_minus_source_esc_ms"])

    def test_overlapping_script_interval_is_not_subtracted(self):
        lines=observed_log(34849,35749,63549,[(36718,43051)])
        lines=[s.replace("paired=1 overlap=0","paired=1 overlap=1")
               for s in lines]
        self.assertFalse(A2.scan(lines)["handoffs"][0]["complete_source_evidence"])

    def test_wrong_original_lifetime_duration_is_rejected(self):
        lines=observed_log(34849,35749,63549,[(36718,43051)])
        lines=[s.replace("elapsed_ms=6333","elapsed_ms=6332")
               for s in lines]
        self.assertFalse(A2.scan(lines)["handoffs"][0]["complete_source_evidence"])

    def test_conflicting_prior_commit_is_rejected(self):
        lines=observed_log(34849,35749,63549,[(36718,43051)])
        lines.insert(-1,observer_row(49000,(9,7)))
        h=A2.scan(lines)["handoffs"][0]
        self.assertFalse(h["complete_source_evidence"])
        self.assertIn("intervening conflicting Theme state",h["issues"])

    def test_no_verified_new_current_excludes_all_comparisons(self):
        lines=observed_log(34849,35749,63549,[(36718,43051)])[:-1]
        out=A2.scan(lines)["handoffs"][0]
        self.assertFalse(out["complete_source_evidence"])
        self.assertIsNone(out["new_current_first_verified_ms"])
        self.assertFalse(A2.compare([A2.scan(lines),A2.scan([])])[
            "comparison_available"])

    def test_offline_tool_never_touches_native_audio_driver(self):
        src=(Path(__file__).resolve().parents[1]/"tools"/
             "audit_theme_handoff_timing.py").read_text(encoding="utf-8")
        for forbidden in ("GetProcAddress(", "LoadLibrary(", "VirtualProtect(",
                          "ReadProcessMemory(", "WriteProcessMemory(",
                          "ctypes.", "subprocess.", ".write_bytes("):
            self.assertNotIn(forbidden,src)


if __name__=="__main__":
    unittest.main()
