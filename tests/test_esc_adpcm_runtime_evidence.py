"""Synthetic source-scoped ESC evidence only; no owner logs or DLLs in CI."""
import importlib.util
from pathlib import Path
import sys
import unittest

SCRIPT=Path(__file__).resolve().parents[1]/"tools"/"analyze_esc_adpcm_trace.py"
SPEC=importlib.util.spec_from_file_location("esc_adpcm_evidence", SCRIPT)
assert SPEC and SPEC.loader
M=importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name]=M
SPEC.loader.exec_module(M)


def gate(t, ref_class, inner, *, two_pauses=True):
    return (
        f"RUNTIME-ESC-PREDRIVER-GATE: t_ms={t} "
        f"callbacks={2 if two_pauses else 1} "
        f"transition_pauses={2 if two_pauses else 1} "
        f"other_slots=0 different_handles=0 slots=2,2,99,99 "
        f"depth_before=1 depth_after=2 owner_driver=1 "
        f"outer_candidates=1 native_ref_class={ref_class} "
        f"native_ref_match={1 if ref_class==1 else 0} "
        f"outer_bank=9 outer_index=5 inner_bank=9 "
        f"inner_index={inner} transition_type_post_pause=4"
    )


def voice(t, paused, *, sampled=1, ordinal=0):
    return (
        f"RUNTIME-ESC-ADPCM-VOICE: t_ms={t} sampled={sampled} "
        f"voice_index={ordinal} paused={paused} other_gate=0 "
        f"transition_type=4 snapshot=post-dispatch"
    )


def verdict(accepted):
    phase,detail=(2,"paired-native-resume-requested") if accepted \
                 else (0,"live-theme-ref-not-verified")
    return (f"RUNTIME-ESC-MUSIC: event=menu-verdict "
            f"phase={phase} detail={detail}")


def resumed(t, accepted, paired=True):
    forwarded,suppressed=(0,2) if accepted else (2,0)
    return [
        f"RUNTIME-ESC-PREDRIVER-RESUME: t_ms={t} callbacks=2 "
        f"transition_resumes=2 other_slots=0 different_handles=0 "
        f"slots=3,3,99,99 native_forwarded={forwarded} "
        f"native_suppressed={suppressed} depth_before=2 depth_after=1",
        f"RUNTIME-ESC-PREDRIVER-LIFETIME: t_ms={t} "
        f"paired={1 if paired else 0} overlap={0 if paired else 1} "
        f"elapsed_ms=1000 play_between=0 stop_between=0",
        f"RUNTIME-ESC-SXD-LIFETIME: t_ms={t} "
        f"paired={1 if paired else 0} sampled=1 first=9:6 last=9:6 "
        f"samples=1 changes=0 unknown=0",
    ]


WATCH_PRIOR=(
    "RUNTIME-ESC-THEME-WATCH: seq=1 t_ms=15000 menu=0 "
    "ref_class=1 outer_bank=9 outer_index=12 "
    "inner_bank=239 inner_index=9 alt_bank=-1 alt_index=-1"
)
WATCH_HANDOFF=(
    "RUNTIME-ESC-THEME-WATCH: seq=2 t_ms=40000 menu=0 "
    "ref_class=3 outer_bank=9 outer_index=5 "
    "inner_bank=9 inner_index=6 alt_bank=9 alt_index=12"
)
WATCH_MATCH=(
    "RUNTIME-ESC-THEME-WATCH: seq=3 t_ms=65000 menu=0 "
    "ref_class=1 outer_bank=9 outer_index=5 "
    "inner_bank=9 inner_index=7 alt_bank=-1 alt_index=-1"
)


class EscAdpcmPostDispatchEvidenceTests(unittest.TestCase):
    def test_nov15_five_menu_pattern_without_claiming_persistent_owner(self):
        # Independent, fabricated reduction of count/state relationships.
        lines=[WATCH_PRIOR, WATCH_HANDOFF]
        for t,close in ((41079,50946),(52046,56546),(57679,64896)):
            lines.extend([gate(t,3,6),voice(t,1),verdict(False)])
            lines.extend(resumed(close,False))
        lines.append(WATCH_MATCH)
        lines.extend([gate(65929,1,7),voice(65929,0),verdict(True)])
        lines.extend(resumed(68813,True))
        lines.extend([gate(70813,1,8),voice(70813,0),verdict(True)])
        lines.extend([
            "RUNTIME-ESC-MUSIC: event=driver-command-rejected "
            "phase=6 detail=theme-or-sxd-stream-changed-native-passthrough",
            # No script Resume/lifetime for last source-qualified opening.
            "RUNTIME-ESC-THEME-WATCH: seq=4 t_ms=77000 menu=0 "
            "ref_class=1 outer_bank=278 outer_index=0 "
            "alt_bank=-1 alt_index=-1",
        ])
        result=M.analyze("\n".join(lines))
        self.assertEqual(result["open_count"],5)
        self.assertEqual(result["confirmed_complete_count"],4)
        self.assertEqual(result["unconfirmed_close_count"],1)
        self.assertEqual(result["rejected_paused_count"],3)
        self.assertEqual(result["compensated_unpaused_count"],2)
        self.assertEqual(result["phase6_open_count"],1)
        self.assertEqual(result["prior_theme_ref_count"],3)
        self.assertEqual(result["anomalies"],[])
        self.assertTrue(all(x["voice"]["ordinal"]==0
                            for x in result["observations"]))
        self.assertFalse(result["observations"][-1]["confirmed_complete"])
        self.assertIn("NOT a persistent",result["limits"][0])
        self.assertIn("post-dispatch",result["limits"][2])

    def test_missing_or_malformed_post_dispatch_voice_never_proves_mute(self):
        lines=[gate(100,3,6),voice(100,1,sampled=0),verdict(False)]
        result=M.analyze("\n".join(lines))
        self.assertEqual(result["rejected_paused_count"],0)
        result=M.analyze("\n".join([gate(100,3,6),
            voice(100,1).replace("snapshot=post-dispatch","snapshot=pre"),
            verdict(False)]))
        self.assertEqual(result["rejected_paused_count"],0)

    def test_source_pair_must_be_exactly_two_same_argument_slots(self):
        lines=[gate(100,3,6,two_pauses=False),voice(100,1),verdict(False)]
        result=M.analyze("\n".join(lines))
        self.assertFalse(result["observations"][0]["source_pair_valid"])
        self.assertEqual(result["rejected_paused_count"],0)

    def test_no_paired_lifetime_means_no_confirmed_menu_close(self):
        lines=[gate(100,1,7),voice(100,0),verdict(True)]
        lines.extend(resumed(200,True,paired=False))
        result=M.analyze("\n".join(lines))
        self.assertEqual(result["confirmed_complete_count"],0)
        self.assertEqual(result["unconfirmed_close_count"],1)
        self.assertTrue(result["observations"][0]["overlap"])

    def test_earlier_theme_without_matching_alt_is_not_related(self):
        lines=[
            WATCH_PRIOR,
            WATCH_HANDOFF.replace("alt_index=12","alt_index=11"),
            gate(41079,3,6),voice(41079,1),verdict(False),
        ]
        result=M.analyze("\n".join(lines))
        self.assertEqual(result["prior_theme_ref_count"],0)
        self.assertEqual(result["rejected_paused_count"],1)

    def test_voice_report_with_unmatched_timestamp_must_not_be_joined(self):
        result=M.analyze("\n".join([
            gate(100,3,6),voice(101,1),verdict(False),
        ]))
        self.assertEqual(result["rejected_paused_count"],0)
        self.assertEqual(len(result["anomalies"]),1)

    def test_nov15_pending_target_is_direct_once_not_three_times(self):
        # Synthetic reconstruction of relationship counts, NOT copied
        # owner log. The real tool must not reuse a one-shot owner
        # diagnosis for later source-qualified ESC openings.
        prior=WATCH_PRIOR.replace("t_ms=15000","t_ms=9106")
        pending=(
            WATCH_HANDOFF.replace("t_ms=40000","t_ms=33917") +
            " pending_switch_state=1 pending_target_matches_outer=1 "
            "pending_target_differs_current=1"
        )
        matched=(
            WATCH_MATCH.replace("t_ms=65000","t_ms=47017") +
            " pending_switch_state=0 pending_target_matches_outer=-1 "
            "pending_target_differs_current=-1"
        )
        owner_rejected=(
            "RUNTIME-ESC-OWNER-DIAG: event=rejected "
            "native_player_ref_class=3 native_player_ref_match=0 "
            "pending_switch_state=1 pending_target_matches_outer=1 "
            "pending_target_differs_current=1 theme_owned_driver=1 "
            "outer_bank=9 outer_index=5 inner_bank=9 inner_index=6"
        )
        lines=[prior,pending]
        for index,(open_t,close_t) in enumerate(
            ((34367,35817),(37350,41733),(42734,46067))
        ):
            lines.extend([gate(open_t,3,6),voice(open_t,1),verdict(False)])
            if index==0:
                lines.append(owner_rejected)
            lines.extend(resumed(close_t,False))
        lines.append(matched)
        lines.extend([
            gate(47600,1,7),voice(47600,0),verdict(True),
            "RUNTIME-ESC-OWNER-DIAG: event=accepted "
            "native_player_ref_class=1 native_player_ref_match=1 "
            "pending_switch_state=0 pending_target_matches_outer=-1 "
            "pending_target_differs_current=-1 theme_owned_driver=1 "
            "outer_bank=9 outer_index=5 inner_bank=9 inner_index=7",
            "RUNTIME-ESC-MUSIC: event=driver-command-rejected "
            "phase=6 detail=theme-or-sxd-stream-changed-native-passthrough",
        ])
        result=M.analyze("\n".join(lines))
        self.assertEqual(result["open_count"],4)
        self.assertEqual(result["confirmed_complete_count"],3)
        self.assertEqual(result["unconfirmed_close_count"],1)
        self.assertEqual(result["rejected_paused_count"],3)
        self.assertEqual(result["compensated_unpaused_count"],1)
        self.assertEqual(result["phase6_open_count"],1)
        self.assertEqual(result["direct_pending_target_confirmation_count"],1)
        self.assertEqual(result["prior_watcher_pending_match_count"],3)
        self.assertEqual(result["anomalies"],[])
        self.assertTrue(result["observations"][0]["direct_pending_target_confirmed"])
        self.assertFalse(result["observations"][1]["direct_pending_target_confirmed"])
        self.assertFalse(result["observations"][2]["direct_pending_target_confirmed"])
        self.assertTrue(all(not item["audio_intervention_authorized"]
                            for item in result["observations"]))

    def test_pending_mismatch_or_duplicate_owner_report_never_proves_target(self):
        owner=(
            "RUNTIME-ESC-OWNER-DIAG: event=rejected "
            "native_player_ref_class=3 native_player_ref_match=0 "
            "pending_switch_state=1 pending_target_matches_outer=1 "
            "pending_target_differs_current=1 theme_owned_driver=1 "
            "outer_bank=9 outer_index=5 inner_bank=9 inner_index=6"
        )
        base=[gate(100,3,6),voice(100,1),verdict(False)]
        valid=M.analyze("\n".join(base+[owner]))
        self.assertEqual(valid["direct_pending_target_confirmation_count"],1)
        for replacement in (
            owner.replace("pending_target_matches_outer=1",
                          "pending_target_matches_outer=0"),
            owner.replace("pending_target_differs_current=1",
                          "pending_target_differs_current=0"),
            owner.replace("pending_switch_state=1","pending_switch_state=0"),
            owner.replace("native_player_ref_class=3",
                          "native_player_ref_class=1"),
            owner.replace("inner_index=6","inner_index=7"),
            owner.replace("theme_owned_driver=1","theme_owned_driver=0"),
        ):
            result=M.analyze("\n".join(base+[replacement]))
            self.assertEqual(result["direct_pending_target_confirmation_count"],0)
        duplicate=M.analyze("\n".join(base+[owner,owner]))
        self.assertEqual(duplicate["direct_pending_target_confirmation_count"],0)
        self.assertTrue(duplicate["observations"][0]["owner_diag_invalid"])
        self.assertEqual(len(duplicate["anomalies"]),1)

    def test_duplicate_openings_are_not_silently_closed(self):
        lines=[gate(100,3,6),voice(100,1),verdict(False),
               gate(200,3,6),voice(200,1),verdict(False)]
        lines.extend(resumed(300,False))
        result=M.analyze("\n".join(lines))
        self.assertEqual(result["open_count"],2)
        self.assertEqual(result["unconfirmed_close_count"],2)
        self.assertTrue(result["observations"][0]["overlap"])
        self.assertEqual(result["confirmed_complete_count"],0)


if __name__=="__main__":
    unittest.main()
