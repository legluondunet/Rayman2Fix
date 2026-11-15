"""Synthetic original-ESC Theme handoff timing controls.

No original game log, binary, DLL or private user data is committed.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import analyze_theme_handoff_timing as M


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


if __name__=="__main__":
    unittest.main()
