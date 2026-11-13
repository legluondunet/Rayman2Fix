"""Research-only model of the original Rayman 2 native per-sound pause loop.

This is a narrow transliteration of the *branch ordering* at 0x4580B0 /
0x458110 of the user's non-approved PE32 reference executable. It neither
runs the game nor implements an audio bypass; its counterexample is a
static safety constraint that must be verified against the retail-CD EXE.
"""
import unittest
from dataclasses import dataclass


@dataclass
class NativeSingleSoundModel:
    depth: int = 1  # Native global depth before ESC in owner Wine traces.
    pause_mark: int = 0  # Sound-table record +0x1C.
    active: bool = True  # record +0x14
    ignored: bool = False  # record +0x08

    def native_pause(self):
        """0x4580B0: set record mark before driver, then increase depth."""
        driver_pause_called = False
        if self.active and not self.ignored and self.pause_mark == 0:
            self.pause_mark = self.depth
            driver_pause_called = True
        self.depth += 1
        return driver_pause_called

    def native_resume(self):
        """0x458110: decrease depth before testing/clearing record mark."""
        self.depth -= 1
        driver_resume_called = False
        if self.active and not self.ignored and self.pause_mark <= self.depth:
            self.pause_mark = 0
            driver_resume_called = True
        return driver_resume_called


class NativePauseInvariants(unittest.TestCase):
    def test_esc_marks_before_driver_callback(self):
        sound = NativeSingleSoundModel(depth=1, pause_mark=0)
        self.assertTrue(sound.native_pause())
        self.assertEqual((sound.depth, sound.pause_mark), (2, 1))

    def test_skipping_only_driver_pause_breaks_focus_loss_safety(self):
        """Ignoring the ESC driver call does NOT clear the native mark."""
        sound = NativeSingleSoundModel(depth=1, pause_mark=0)
        self.assertTrue(sound.native_pause())  # ESC: proposed driver bypass
        # Game already stored pause_mark=1. Focus loss raises depth again.
        self.assertFalse(sound.native_pause())  # Focus: no native pause call
        self.assertEqual((sound.depth, sound.pause_mark), (3, 1))

    def test_focus_gain_can_resume_while_esc_remains_open(self):
        sound = NativeSingleSoundModel(depth=1, pause_mark=0)
        self.assertTrue(sound.native_pause())  # ESC
        self.assertFalse(sound.native_pause())  # Focus lost
        self.assertTrue(sound.native_resume())  # Focus gained; ESC open
        self.assertEqual((sound.depth, sound.pause_mark), (2, 0))

    def test_nonparticipating_records_still_increment_global_depth(self):
        sound = NativeSingleSoundModel(depth=1, pause_mark=0, ignored=True)
        self.assertFalse(sound.native_pause())
        self.assertEqual((sound.depth, sound.pause_mark), (2, 0))


if __name__ == "__main__":
    unittest.main()
