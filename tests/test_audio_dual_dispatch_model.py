"""Research-only deterministic two-record model of original-CD native pause routing.

No DLL hooks, no audio stream mutations, no claim this reproduces every native
state. Tests structural consequences *if* both outer Theme and inner SXD
records qualify in the 64-slot pause loop.
"""
from dataclasses import dataclass, field
import unittest


@dataclass
class SoundRecord:
    kind: str  # "theme" or "transition" or "sfx"
    active: bool = True
    ignored: bool = False
    mark: int = 0


@dataclass
class NativeDispatcher:
    depth: int = 1
    records: list[SoundRecord] = field(default_factory=lambda: [
        SoundRecord("transition"), SoundRecord("theme")
    ])
    theme_controller_active: bool = True
    emitted: list[str] = field(default_factory=list)

    def emit_for_record(self, record: SoundRecord, action: str):
        if record.kind == "theme":
            # Native type 11 callback 0x459C40 / 0x459C60 delegates
            # directly to the inner record at controller+0x30.
            if self.theme_controller_active:
                self.emitted.append(action + ":inner")
        elif record.kind == "transition":
            self.emitted.append(action + ":inner")
        else:
            self.emitted.append(action + ":sfx")

    def pause(self) -> list[str]:
        before = len(self.emitted)
        # 0x4580B0 marks each eligible record with the *old* depth.
        for r in self.records:
            if r.active and not r.ignored and r.mark == 0:
                r.mark = self.depth
                self.emit_for_record(r, "pause")
        self.depth += 1
        return self.emitted[before:]

    def resume(self) -> list[str]:
        before = len(self.emitted)
        # 0x458110 decrements depth, then checks each active record mark.
        self.depth -= 1
        for r in self.records:
            if r.active and not r.ignored and r.mark <= self.depth:
                r.mark = 0
                self.emit_for_record(r, "resume")
        return self.emitted[before:]


class DualDispatchModelTests(unittest.TestCase):
    def test_two_record_paths_pause_same_inner_driver(self):
        d = NativeDispatcher()
        self.assertEqual(d.pause(), ["pause:inner", "pause:inner"])
        self.assertEqual(d.depth, 2)
        self.assertEqual([r.mark for r in d.records], [1, 1])
        self.assertEqual(d.resume(), ["resume:inner", "resume:inner"])
        self.assertEqual(d.depth, 1)

    def test_nested_focus_does_not_pause_again_at_depth_one(self):
        d = NativeDispatcher()
        d.pause()               # ESC script
        self.assertEqual(d.pause(), [])   # Win32 loses focus
        self.assertEqual(d.depth, 3)
        # Native focus gain may issue two resumes even if ESC remains open.
        self.assertEqual(d.resume(), ["resume:inner", "resume:inner"])
        self.assertEqual(d.depth, 2)
        # Exact consequences of a driver-only ESC bypass are unsafe.
        # The feature must never be implemented by dropping the driver call.

    def test_esc_close_after_focus_gain_can_resume_again_without_mark(self):
        d = NativeDispatcher()
        self.assertEqual(len(d.pause()), 2)  # ESC open
        self.assertEqual(d.pause(), [])       # Focus loss
        self.assertEqual(len(d.resume()), 2)  # Focus gain with ESC still open
        # Native +0x1c marks were cleared at focus gain; the closing
        # script resume also passes the mark <= depth test.
        self.assertEqual(d.resume(), ["resume:inner", "resume:inner"])
        self.assertEqual(d.depth, 1)
        self.assertEqual([r.mark for r in d.records], [0, 0])

    def test_outer_theme_inactive_causes_only_one_driver_pause(self):
        d = NativeDispatcher(theme_controller_active=False)
        self.assertEqual(d.pause(), ["pause:inner"])

    def test_outer_ignored_causes_only_one_driver_pause(self):
        d = NativeDispatcher(records=[
            SoundRecord("transition"), SoundRecord("theme", ignored=True)
        ])
        self.assertEqual(d.pause(), ["pause:inner"])

    def test_inner_ignored_but_outer_theme_active_can_still_call_inner(self):
        d = NativeDispatcher(records=[
            SoundRecord("transition", ignored=True), SoundRecord("theme")
        ])
        self.assertEqual(d.pause(), ["pause:inner"])

    def test_first_pause_at_depth_zero_can_dispatch_again(self):
        d = NativeDispatcher(depth=0)
        self.assertEqual(d.pause(), ["pause:inner", "pause:inner"])
        self.assertEqual([r.mark for r in d.records], [0, 0])
        self.assertEqual(d.pause(), ["pause:inner", "pause:inner"])

    def test_sfx_never_becomes_theme_driver(self):
        d = NativeDispatcher(records=[
            SoundRecord("transition"), SoundRecord("sfx"), SoundRecord("theme")
        ])
        self.assertEqual(d.pause(), ["pause:inner", "pause:sfx", "pause:inner"])


if __name__ == "__main__":
    unittest.main()
