"""Research-only proof of concept: ESC music exception at native record layer.

NOT a playable patch. The retail-CD 0x4580B0 / 0x458110 routines are
modelled by their demonstrated mark/depth order. The owner-identified
Theme and inner SXD both route to one transition driver handle.

Hypotheses still needing actual x86 validation: caller origin is
reliably ESC-menu-specific (not merely generic opcode 0x1AA), both
record ownership/generation are known, and native driver pause/resume
has balanced semantics. Never use this model alone to patch audio.
"""
from dataclasses import dataclass, field


@dataclass
class NativeRecord:
    pause_mark: int = 0
    active: bool = True


@dataclass
class EscMusicRecordExperiment:
    # The retail binary initializes global depth to 1. Two eligible
    # records (outer Theme + inner SXD) can address the same driver.
    depth: int = 1
    records: list[NativeRecord] = field(
        default_factory=lambda: [NativeRecord(), NativeRecord()])
    esc_pending: bool = False
    paused_for_external_reason: bool = False
    driver_balance: int = 0
    driver_calls: list[str] = field(default_factory=list)
    stopped: bool = False

    def _call(self, kind: str):
        if self.stopped:
            raise AssertionError("driver callback after authoritative Stop")
        self.driver_calls.append(kind)
        if kind == "pause":
            self.driver_balance += 1
        elif kind == "resume":
            self.driver_balance -= 1
            if self.driver_balance < 0:
                raise AssertionError("unmatched driver resume")

    def native_pause(self):
        if self.stopped:
            return
        for rec in self.records:
            if rec.active and rec.pause_mark == 0:
                rec.pause_mark = self.depth
                self._call("pause")
        self.depth += 1

    def native_resume(self):
        if self.stopped:
            return
        self.depth -= 1
        if self.depth < 0:
            raise AssertionError("negative native pause depth")
        for rec in self.records:
            if rec.active and rec.pause_mark <= self.depth:
                rec.pause_mark = 0
                self._call("resume")

    def enter_esc(self, *, music_verified: bool, menu_origin_verified: bool,
                  foreground: bool) -> bool:
        # Do not reinterpret arbitrary game pauses as ESC. Require
        # the exact initial safe state and a verified live owner.
        if (self.stopped or self.esc_pending or not foreground
                or not music_verified or not menu_origin_verified
                or self.depth != 1 or self.driver_balance != 0
                or any(not r.active or r.pause_mark != 0
                       for r in self.records)):
            return False
        # Proposed per-record exception: skip BOTH Theme and inner
        # driver pauses/marks, but retain the native global depth.
        self.esc_pending = True
        self.depth += 1
        return True

    def focus_loss(self):
        if self.stopped:
            return
        self.native_pause()
        self.paused_for_external_reason = True

    def focus_gain(self):
        if self.stopped:
            return
        self.native_resume()
        self.paused_for_external_reason = False

    def exit_esc(self):
        if self.stopped or not self.esc_pending:
            return
        # Special case ONLY for verified ESC script resume; SFX still
        # passes through the original native resume path untouched.
        # Decrement global depth, but preserve the *physical* state
        # and avoid a spurious music resume when no music pause happened.
        self.depth -= 1
        for rec in self.records:
            if not rec.active:
                continue
            if rec.pause_mark != 0:
                # ESC closed while focus is lost (or another pause
                # remains): rebase mark on the surviving native depth
                # so the later focus resume can unblock exactly once.
                rec.pause_mark = self.depth - 1
        self.esc_pending = False

    def authoritative_stop(self):
        # Native transition Stop and level replacement always win,
        # independent of menu/depth/driver balance. The real runtime
        # must forward Stop unchanged, not synthesize a resume.
        self.stopped = True
        for r in self.records:
            r.active = False
        self.esc_pending = False
        self.driver_balance = 0
        self.driver_calls.append("stop")

    @property
    def playing(self) -> bool:
        return not self.stopped and self.driver_balance == 0


def main():
    m=EscMusicRecordExperiment()
    assert m.enter_esc(music_verified=True,menu_origin_verified=True,
                       foreground=True)
    assert m.playing and m.depth == 2
    m.focus_loss()
    assert not m.playing and m.depth == 3
    m.exit_esc()  # Close while unfocused: no premature resume.
    assert not m.playing and m.depth == 2
    m.focus_gain()
    assert m.playing and m.depth == 1
    assert m.driver_calls == ["pause","pause","resume","resume"]
    print("conceptual two-record ESC/focus sequence: PASS")


if __name__ == "__main__":
    main()
