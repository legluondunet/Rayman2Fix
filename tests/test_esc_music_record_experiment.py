"""Isolated speculative two-record/native-depth experiment, no game patch."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys
import unittest

path=Path(__file__).resolve().parents[1]/"tools"/"esc_music_record_experiment.py"
spec=spec_from_file_location("esc_record_experiment",path)
module=module_from_spec(spec)
sys.modules[spec.name]=module
spec.loader.exec_module(module)
M=module.EscMusicRecordExperiment


def open_esc(m):
    return m.enter_esc(
        music_verified=True,menu_origin_verified=True,foreground=True)


class EscNativeRecordExperimentTests(unittest.TestCase):
    def test_observed_retail_native_baseline_has_two_calls(self):
        m=M()
        m.native_pause()
        self.assertEqual((m.depth,m.driver_balance),(2,2))
        self.assertEqual(m.driver_calls,["pause","pause"])
        self.assertFalse(m.playing)
        m.native_resume()
        self.assertEqual((m.depth,m.driver_balance),(1,0))
        self.assertEqual(m.driver_calls,
                         ["pause","pause","resume","resume"])

    def test_hypothetical_esc_exception_keeps_music_running(self):
        m=M()
        self.assertTrue(open_esc(m))
        self.assertEqual((m.depth,m.driver_balance),(2,0))
        self.assertEqual([r.pause_mark for r in m.records],[0,0])
        self.assertTrue(m.playing)
        m.exit_esc()
        self.assertTrue(m.playing)
        self.assertEqual((m.depth,m.driver_calls),(1,[]))

    def test_esc_open_focus_loss_gain_then_esc_exit(self):
        m=M()
        self.assertTrue(open_esc(m))
        m.focus_loss()
        self.assertFalse(m.playing)
        self.assertEqual((m.depth,m.driver_balance),(3,2))
        m.focus_gain()
        self.assertTrue(m.playing)
        self.assertEqual((m.depth,m.driver_balance),(2,0))
        m.exit_esc()
        self.assertEqual((m.depth,m.driver_balance),(1,0))
        self.assertEqual(m.driver_calls,
                         ["pause","pause","resume","resume"])

    def test_esc_closes_while_unfocused_no_early_resume(self):
        m=M()
        self.assertTrue(open_esc(m))
        m.focus_loss()
        m.exit_esc()
        self.assertFalse(m.playing)
        self.assertEqual([r.pause_mark for r in m.records],[1,1])
        m.focus_gain()
        self.assertTrue(m.playing)
        self.assertEqual((m.depth,m.driver_balance),(1,0))

    def test_unknown_script_or_music_identity_fails_closed(self):
        for args in (
            {"music_verified":False,"menu_origin_verified":True,
             "foreground":True},
            {"music_verified":True,"menu_origin_verified":False,
             "foreground":True},
            {"music_verified":True,"menu_origin_verified":True,
             "foreground":False},
        ):
            with self.subTest(args=args):
                m=M()
                self.assertFalse(m.enter_esc(**args))
                self.assertEqual((m.depth,m.driver_balance),(1,0))
                m.native_pause()
                self.assertFalse(m.playing)
                m.native_resume()
                self.assertTrue(m.playing)

    def test_externally_paused_music_cannot_enter_esc_exception(self):
        m=M()
        m.native_pause()
        self.assertFalse(open_esc(m))
        self.assertFalse(m.playing)
        self.assertEqual((m.depth,m.driver_balance),(2,2))

    def test_stop_and_level_transition_never_resume_dead_owner(self):
        m=M()
        self.assertTrue(open_esc(m))
        m.focus_loss()
        m.authoritative_stop()
        m.focus_gain()
        m.exit_esc()
        self.assertFalse(m.playing)
        self.assertEqual(m.driver_calls,
                         ["pause","pause","stop"])
        self.assertTrue(all(not r.active for r in m.records))

    def test_ordinary_nested_pause_during_esc_still_temporarily_silences(self):
        m=M()
        self.assertTrue(open_esc(m))
        m.native_pause()
        self.assertEqual(m.driver_balance,2)
        m.native_resume()
        self.assertEqual(m.driver_balance,0)
        m.exit_esc()
        self.assertTrue(m.playing)

    def test_esc_exit_with_other_pause_leaves_driver_silent(self):
        m=M()
        self.assertTrue(open_esc(m))
        m.native_pause()
        m.exit_esc()
        self.assertFalse(m.playing)
        m.native_resume()
        self.assertTrue(m.playing)

    def test_unsupported_multiple_external_pause_causes_is_known_hazard(self):
        # Critical counterexample: the naive "rebase native pause mark"
        # works for ONE focus reason but not interleaving focus loss and
        # other nested script/game reasons. The model is a PoC only.
        m=M()
        self.assertTrue(open_esc(m))
        m.focus_loss()
        m.exit_esc()
        m.native_pause()  # additional OTHER reason while focus still lost
        self.assertFalse(m.playing)
        m.focus_gain()
        # Original native marks now allow resume even though the other
        # reason remains. This MUST prohibit shipping this prototype.
        self.assertTrue(m.playing)
        self.assertEqual(m.depth,2)


if __name__=="__main__":
    unittest.main()
