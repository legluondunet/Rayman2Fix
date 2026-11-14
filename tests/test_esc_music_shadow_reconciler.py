"""All cases are synthetic. No Win32 driver callback is invoked here."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import itertools
import sys
import unittest

FILE = Path(__file__).resolve().parents[1] / "tools" / "esc_music_shadow_reconciler.py"
SPEC = spec_from_file_location("esc_music_shadow", FILE)
M = module_from_spec(SPEC)
sys.modules[SPEC.name] = M
SPEC.loader.exec_module(M)
OWNER = M.MusicGeneration(9, 5, 9, 7, 812)


def adopt():
    s = M.EscMusicShadow()
    assert s.attach(
        OWNER, verified_level_music=True,
        verified_esc_lifetime=True, verified_driver_adapter=True,
        known_foreground=True, foreground=True,
    )
    return s


class ShadowDriverPolicyTests(unittest.TestCase):
    def test_no_adoption_without_each_independent_gate(self):
        args = dict(verified_level_music=True,
                    verified_esc_lifetime=True,
                    verified_driver_adapter=True,
                    known_foreground=True, foreground=True)
        for field in args:
            with self.subTest(field=field):
                s = M.EscMusicShadow()
                invalid = dict(args)
                invalid[field] = False
                self.assertFalse(s.attach(OWNER, **invalid))
                self.assertIsNone(s.owner)
                self.assertEqual(
                    s.native_driver_callback(OWNER, "pause", "theme", "esc"),
                    "forward-native")

    def test_esc_alone_preserves_playback_with_paired_native_calls(self):
        s = adopt()
        self.assertEqual(s.menu_open(verified=True), "none")
        for route in ("sxd", "theme"):
            self.assertEqual(
                s.native_driver_callback(OWNER, "pause", route, "esc"),
                "suppress-paired-native")
        self.assertFalse(s.physical_paused)
        self.assertEqual(s.menu_close(verified=True), "none")
        for route in ("theme", "sxd"):
            self.assertEqual(
                s.native_driver_callback(OWNER, "resume", route, "esc"),
                "suppress-paired-native")
        self.assertFalse(s.physical_paused)
        self.assertEqual(s.transitions, [])
        self.assertEqual(sum(s.callback_count.values()), 4)

    def test_focus_loss_and_gain_while_esc_open(self):
        s = adopt()
        s.menu_open(verified=True)
        self.assertEqual(
            s.focus_change(known=True, foreground=False), "pause")
        self.assertTrue(s.physical_paused)
        self.assertEqual(
            s.focus_change(known=True, foreground=False), "none")
        self.assertEqual(
            s.focus_change(known=True, foreground=True), "resume")
        self.assertFalse(s.physical_paused)
        s.menu_close(verified=True)
        self.assertEqual(s.transitions, ["pause", "resume"])

    def test_close_esc_while_unfocused(self):
        s = adopt()
        s.menu_open(verified=True)
        s.focus_change(known=True, foreground=False)
        self.assertEqual(s.menu_close(verified=True), "none")
        self.assertTrue(s.physical_paused)
        self.assertEqual(
            s.focus_change(known=True, foreground=True), "resume")
        self.assertEqual(s.transitions, ["pause", "resume"])

    def test_focus_loss_other_script_pause_then_focus_gain(self):
        s = adopt()
        s.menu_open(verified=True)
        self.assertEqual(s.focus_change(known=True, foreground=False), "pause")
        self.assertEqual(s.other_pause(), "none")
        s.menu_close(verified=True)
        self.assertEqual(
            s.focus_change(known=True, foreground=True), "none")
        self.assertTrue(s.physical_paused)  # prevented unsafe prior patch
        self.assertEqual(s.other_resume(), "resume")
        self.assertFalse(s.physical_paused)
        self.assertEqual(s.transitions, ["pause", "resume"])

    def test_two_other_pauses_do_not_release_on_first_resume(self):
        s = adopt()
        s.menu_open(verified=True)
        self.assertEqual(s.other_pause(), "pause")
        self.assertEqual(s.other_pause(), "none")
        self.assertEqual(s.other_resume(), "none")
        self.assertTrue(s.physical_paused)
        self.assertEqual(s.other_resume(), "resume")
        self.assertFalse(s.physical_paused)

    def test_unknown_reason_quarantines_without_future_resume(self):
        s = adopt()
        s.menu_open(verified=True)
        self.assertEqual(s.uncertain_origin(), "hold-unknown")
        self.assertTrue(s.physical_paused)
        self.assertTrue(s.quarantine)
        s.focus_change(known=True, foreground=False)
        s.focus_change(known=True, foreground=True)
        s.other_pause()
        s.other_resume()
        s.menu_close(verified=True)
        self.assertEqual(s.transitions, ["pause"])
        self.assertTrue(s.physical_paused)
        self.assertEqual(s.native_driver_callback(
            OWNER, "resume", "theme", "unknown"), "hold-unknown")
        self.assertEqual(s.transitions, ["pause"])

    def test_native_stop_is_authoritative_even_while_unfocused(self):
        s = adopt()
        s.menu_open(verified=True)
        s.focus_change(known=True, foreground=False)
        self.assertEqual(s.native_stop(OWNER), "forward-stop")
        self.assertIsNone(s.owner)
        self.assertEqual(s.transitions, ["pause", "forward-stop"])
        self.assertEqual(s.menu_close(verified=True), "forward-native")
        self.assertEqual(s.focus_change(known=True, foreground=True), "none")
        self.assertNotIn("resume", s.transitions)

    def test_reused_bank_and_driver_address_stale_generation_not_adopted(self):
        s = adopt()
        stale = M.MusicGeneration(9, 5, 9, 7, 811)
        next_gen = M.MusicGeneration(9, 5, 9, 7, 813)
        self.assertEqual(s.native_stop(stale), "forward-stop")
        self.assertEqual(s.owner, OWNER)
        self.assertEqual(s.native_stop(OWNER), "forward-stop")
        self.assertFalse(s.attach(
            next_gen, verified_level_music=True,
            verified_esc_lifetime=False,
            verified_driver_adapter=True,
            known_foreground=True, foreground=True))
        self.assertTrue(s.attach(
            next_gen, verified_level_music=True,
            verified_esc_lifetime=True,
            verified_driver_adapter=True,
            known_foreground=True, foreground=True))
        self.assertEqual(s.native_driver_callback(
            stale, "resume", "sxd", "esc"), "forward-native")

    def test_duplicate_native_Theme_and_sxd_routes_cannot_double_resume(self):
        s = adopt()
        s.menu_open(verified=True)
        s.focus_change(known=True, foreground=False)
        for _ in range(3):
            for route in ("theme", "sxd"):
                self.assertEqual(s.native_driver_callback(
                    OWNER, "pause", route, "focus"), "suppress-paired-native")
        self.assertTrue(s.physical_paused)
        s.focus_change(known=True, foreground=True)
        for route in ("sxd", "theme"):
            self.assertEqual(s.native_driver_callback(
                OWNER, "resume", route, "focus"), "suppress-paired-native")
        self.assertEqual(s.transitions, ["pause", "resume"])

    def test_missing_focus_state_is_a_pause_not_a_resume(self):
        s = adopt()
        s.menu_open(verified=True)
        self.assertEqual(s.focus_change(known=False, foreground=True),
                         "pause")
        self.assertTrue(s.physical_paused)
        self.assertEqual(s.focus_change(known=True, foreground=True),
                         "resume")

    def test_exhaustive_short_cause_sequences_never_play_while_unfocused_or_other_pause(self):
        alphabet = ("open", "close", "focus_out", "focus_in",
                    "other_pause", "other_resume", "unknown", "stop")
        # 8**4=4096 sequences. Every state transition is deterministic
        # and does not depend on sample timing or native handle identity.
        for seq in itertools.product(alphabet, repeat=4):
            s=adopt()
            for ev in seq:
                if ev == "open":
                    s.menu_open(verified=True)
                elif ev == "close":
                    s.menu_close(verified=True)
                elif ev == "focus_out":
                    s.focus_change(known=True, foreground=False)
                elif ev == "focus_in":
                    s.focus_change(known=True, foreground=True)
                elif ev == "other_pause":
                    s.other_pause()
                elif ev == "other_resume":
                    s.other_resume()
                elif ev == "unknown":
                    s.uncertain_origin()
                else:
                    s.native_stop(OWNER)
                if s.owner is not None:
                    if (not s.foreground or not s.focus_known
                            or s.other_pause_depth > 0 or s.quarantine):
                        self.assertTrue(s.physical_paused,
                                        f"unsafe event sequence {seq}")
                if s.sound_stopped:
                    self.assertIsNone(s.owner)
                    self.assertNotEqual(s.transitions[-1:] , ["resume"])


if __name__ == "__main__":
    unittest.main()
