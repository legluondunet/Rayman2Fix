"""Synthetic-only invariants for a proposed, NOT IMPLEMENTED, audio policy."""
import importlib.util
import itertools
from pathlib import Path
import sys
import unittest

PATH = Path(__file__).resolve().parents[1] / "tools" / "audio_pause_policy_model.py"
SPEC = importlib.util.spec_from_file_location("audio_policy_model", PATH)
assert SPEC and SPEC.loader
M=importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name]=M  # needed by dataclasses on Python 3.13
SPEC.loader.exec_module(M)

class ProposedPolicyTests(unittest.TestCase):
    def running(self):
        p=M.EscThemePolicy()
        p.set_focus(known=True, foreground=True)
        self.assertEqual(p.start(0x00170003,101,owner_is_theme=True,music_identity_confirmed=True),"none")
        self.assertTrue(p.playing)
        return p

    def test_focus_toggles_during_esc(self):
        p=self.running()
        self.assertEqual(p.esc_open(True),"none")
        self.assertEqual(p.set_focus(known=True,foreground=False),"pause")
        self.assertEqual(p.set_focus(known=True,foreground=True),"resume")
        self.assertEqual(p.esc_close(),"none")

    def test_esc_close_while_unfocused(self):
        p=self.running()
        p.esc_open(True)
        p.set_focus(known=True,foreground=False)
        self.assertEqual(p.esc_close(),"none")
        self.assertFalse(p.playing)
        self.assertEqual(p.set_focus(known=True,foreground=True),"resume")

    def test_unverified_script_or_focus_fails_closed(self):
        p=self.running()
        self.assertEqual(p.esc_open(False),"pause")
        self.assertFalse(p.playing)
        self.assertEqual(p.esc_open(True),"none")
        self.assertFalse(p.esc_menu_verified)
        p.esc_close()
        self.assertEqual(p.esc_close(),"resume")
        self.assertEqual(p.set_focus(known=False,foreground=True),"pause")

    def test_stop_and_stale_handle_never_revive(self):
        p=self.running()
        p.esc_open(True)
        self.assertEqual(p.stop(100),"none")
        self.assertEqual(p.stop(101),"forget")
        self.assertEqual(p.esc_close(),"none")
        self.assertEqual(p.set_focus(known=True,foreground=True),"none")
        self.assertFalse(p.playing)
        self.assertEqual(p.start(0x00170005,102,owner_is_theme=True,music_identity_confirmed=True),"none")
        self.assertEqual(p.stop(101),"none")
        self.assertTrue(p.playing)

    def test_native_theme_bank_alone_is_not_music_identity(self):
        p=M.EscThemePolicy()
        p.set_focus(known=True,foreground=True)
        self.assertEqual(
            p.start(0x00090005,77,owner_is_theme=True),
            "none")
        self.assertIsNone(p.owner_resource_id)
        self.assertFalse(p.playing)
        # An observed Theme bank/index pair, even unique and valid,
        # is *not* evidence the stream is audible level music.
        self.assertEqual(
            p.start(0x00090005,77,owner_is_theme=True,
                    music_identity_confirmed=True),
            "none")
        self.assertTrue(p.playing)

    def test_stop_clears_menu_proof_for_replacement_generation(self):
        p=self.running()
        p.esc_open(True)
        self.assertTrue(p.esc_menu_verified)
        self.assertEqual(p.stop(101),"forget")
        self.assertFalse(p.esc_menu_verified)
        self.assertEqual(p.esc_pause_depth,1)
        # The new level's Theme may reuse a driver or old bank/index;
        # eligibility must NOT be inherited from the stopped instance.
        self.assertEqual(
            p.start(0x00170003,102,owner_is_theme=True,
                    music_identity_confirmed=True),"pause")
        self.assertFalse(p.playing)
        self.assertEqual(p.esc_close(),"resume")
        self.assertTrue(p.playing)

    def test_unverified_owner_does_not_receive_policy(self):
        p=M.EscThemePolicy()
        p.set_focus(known=True,foreground=True)
        self.assertEqual(p.start(0x234,3,owner_is_theme=False),"none")
        self.assertIsNone(p.owner_resource_id)
        self.assertEqual(p.esc_open(True),"none")

    def test_other_pause_and_unknown_reason_override_esc(self):
        p=self.running()
        p.esc_open(True)
        self.assertEqual(p.other_pause(),"pause")
        self.assertEqual(p.other_resume(),"resume")
        self.assertEqual(p.set_unknown_pause(True),"pause")
        self.assertEqual(p.set_unknown_pause(False),"resume")

    def test_all_four_event_sequences_keep_background_silent(self):
        actions=("esc_open","esc_close","focus_out","focus_in",
                 "other_pause","other_resume","unknown_on","unknown_off")
        for sequence in itertools.product(actions,repeat=4):
            p=self.running()
            for action in sequence:
                if action=="esc_open": p.esc_open(True)
                elif action=="esc_close": p.esc_close()
                elif action=="focus_out": p.set_focus(known=True,foreground=False)
                elif action=="focus_in": p.set_focus(known=True,foreground=True)
                elif action=="other_pause": p.other_pause()
                elif action=="other_resume": p.other_resume()
                elif action=="unknown_on": p.set_unknown_pause(True)
                else: p.set_unknown_pause(False)
                self.assertFalse(p.playing and not p.is_foreground,sequence)
                self.assertFalse(p.playing and p.other_pause_depth>0,sequence)
                self.assertFalse(p.playing and p.unknown_pause_reason,sequence)

if __name__=="__main__":
    unittest.main()
