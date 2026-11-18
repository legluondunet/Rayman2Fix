"""Fail-closed checks for the offline Learn_10 NOMOVIES trace auditor."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from learn10_trace_validator import audit


def sample(*, duration=3125, explicit=True, exposed=2828):
    lines = [
        "RUNTIME-SESSION: test session",
        "RUNTIME-LEARN10-PROTOTYPE: enabled=1 route=native-NOMOVIES "
        "native-else-input-gate=1 max_armed_ms=5000",
        "RUNTIME-INTRO: nomovies-flag at-entry known=1 enabled=0 bit=1188",
        "RUNTIME-LEARN10-PROTOTYPE: phase=armed t=281ms "
        "nomovies_bit1188=1 original=0 director=3",
        "RUNTIME-LEARN10-PROTOTYPE: phase=physical-enter-edge t=3078ms "
        "count=1",
        "RUNTIME-INTRO: director behaviour transition t=3078ms index=7",
        "RUNTIME-LEARN10-PROTOTYPE: phase=restored "
        "reason=director-left-b3 nomovies_bit1188=0 "
        "other_progress_bits=preserved enter_edges=1 "
        f"armed_duration_ms={exposed}",
        "RUNTIME-INTRO: director behaviour transition t=3109ms index=1",
    ]
    if explicit:
        lines.append(
            "RUNTIME-LEARN10-PROTOTYPE: phase=handover elapsed_ms=3125 "
            "story1091=1 ever_armed=1 restored=1 still_armed=0 enter_edges=1"
        )
    lines.extend([
        "RUNTIME-INTRO: nomovies-flag at-handover known=1 enabled=0 bit=1188",
        f"RUNTIME-INTRO: natural=handover t={duration}ms ray_custom=0x80000102",
        "RUNTIME-INTRO: director=handover t=3125ms visited=305 "
        "matches=0 behaviour=1 suspended=0",
    ])
    if explicit:
        lines.append(
            "RUNTIME-LEARN10-PROTOTYPE: phase=handover-verification "
            "status=pass known=1 bit1188=0 rollback_complete=1"
        )
    lines.append("RUNTIME-INTRO: natural=handover-plus-1s t=4125ms")
    return "\n".join(lines) + "\n"


class TraceValidationTests(unittest.TestCase):
    def test_passes_legacy_20261117_style_while_warning_about_unverified_saves(self):
        trace = sample(explicit=False).replace(" armed_duration_ms=2828", "")
        result = audit(trace)
        self.assertEqual(result["status"], "CANDIDATE")
        self.assertIsNone(result["exposure_ms"])
        self.assertFalse(result["visual_verified"])
        self.assertFalse(result["saves_verified"])
        self.assertTrue(any("Legacy" in w for w in result["warnings"]))

    def test_passes_explicit_verification_and_reports_timing(self):
        result = audit(sample())
        self.assertEqual(result["status"], "CANDIDATE")
        self.assertEqual(result["handover_ms"], 3125)
        self.assertEqual(result["armed_ms"], 281)
        self.assertEqual(result["enter_edges"], 1)
        self.assertEqual(result["exposure_ms"], 2828)
        self.assertFalse(result["saves_verified"])

    def test_early_enter_is_warning_not_timer_proof(self):
        text = sample().replace("t=3078ms", "t=843ms")
        result = audit(text)
        self.assertEqual(result["status"], "CANDIDATE")
        self.assertEqual(result["first_enter_since_arm_ms"], 843-281)
        self.assertTrue(any("timing proxy" in w for w in result["warnings"]))

    def test_enter_after_rollback_reported_without_rearming(self):
        text = sample().replace(
            "RUNTIME-INTRO: director behaviour transition t=3109ms",
            "RUNTIME-LEARN10-PROTOTYPE: "
            "phase=physical-enter-after-rollback t=6100ms count=1\\n"
            "RUNTIME-INTRO: director behaviour transition t=3109ms",
        )
        result = audit(text)
        self.assertEqual(result["post_rollback_enter_edges"], 1)
        self.assertTrue(any("after" in w and "rollback" in w
                            for w in result["warnings"]))

    def test_failure_if_handover_bit_is_not_clear(self):
        bad = sample().replace(
            "nomovies-flag at-handover known=1 enabled=0",
            "nomovies-flag at-handover known=1 enabled=1",
        )
        self.assertEqual(audit(bad)["status"], "UNSAFE")

    def test_failure_if_rollback_is_missing(self):
        lines = [x for x in sample().splitlines() if "phase=restored " not in x]
        self.assertEqual(audit("\n".join(lines))["status"], "UNSAFE")

    def test_failure_if_restore_pending_even_when_later_restored(self):
        bad = sample().replace(
            "RUNTIME-LEARN10-PROTOTYPE: phase=restored ",
            "RUNTIME-LEARN10-PROTOTYPE: phase=restore-pending "
            "reason=unsafe\nRUNTIME-LEARN10-PROTOTYPE: phase=restored ",
        )
        self.assertEqual(audit(bad)["status"], "UNSAFE")

    def test_failure_if_in_memory_verification_failed(self):
        bad = sample().replace("status=pass known=1", "status=FAIL known=1")
        self.assertEqual(audit(bad)["status"], "UNSAFE")

    def test_failure_if_flag_exposed_longer_than_watchdog(self):
        self.assertEqual(audit(sample(exposed=5001))["status"], "UNSAFE")
        self.assertEqual(audit(sample(exposed=5000))["status"], "CANDIDATE")

    def test_failure_if_two_sessions_accumulated(self):
        self.assertEqual(audit(sample() + sample())["status"], "UNSAFE")

    def test_failure_if_rolled_back_after_handover(self):
        lines = sample().splitlines()
        rollback = next(x for x in lines if "phase=restored" in x)
        lines.remove(rollback)
        lines.append(rollback)
        self.assertEqual(audit("\n".join(lines))["status"], "UNSAFE")

    def test_incomplete_without_physical_enter_edge(self):
        lines = [x for x in sample().splitlines()
                 if "phase=physical-enter-edge" not in x]
        self.assertEqual(audit("\n".join(lines))["status"], "INCOMPLETE")

    def test_incomplete_if_handover_took_natural_duration(self):
        self.assertEqual(audit(sample(duration=24984))["status"], "INCOMPLETE")

    def test_incomplete_if_director_or_rayman_not_restored(self):
        bad = sample().replace("behaviour=1 suspended=0", "behaviour=3 suspended=1")
        self.assertEqual(audit(bad)["status"], "INCOMPLETE")

    def test_not_applicable_if_not_explicit_prototype(self):
        self.assertEqual(
            audit("RUNTIME-SESSION: test\nRUNTIME-CONFIG: research_probes=1\n")["status"],
            "NOT_APPLICABLE",
        )


if __name__ == "__main__":
    unittest.main()
