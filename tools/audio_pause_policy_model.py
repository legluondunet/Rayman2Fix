"""Research-only model of a possible reason-aware ESC Theme pause policy.

NOT a game patch. Does not resolve the native +0x1C per-record pause mark
or distinguish origins in the original runtime. The invariants below are
design constraints to be satisfied before any audio calls are changed.
"""
from dataclasses import dataclass

@dataclass
class EscThemePolicy:
    owner_resource_id: int | None = None
    generation: int = 0
    foreground_known: bool = False
    is_foreground: bool = False
    esc_menu_verified: bool = False
    esc_pause_depth: int = 0
    other_pause_depth: int = 0
    native_play_requested: bool = False
    playing: bool = False
    unknown_pause_reason: bool = False

    def desired_running(self) -> bool:
        return (
            self.owner_resource_id is not None
            and self.native_play_requested
            and self.foreground_known and self.is_foreground
            and not self.unknown_pause_reason
            and self.other_pause_depth == 0
            and (self.esc_pause_depth == 0 or self.esc_menu_verified)
        )

    def transition(self) -> str:
        # Only the owned Theme stream qualifies. This is a SPECIFICATION,
        # not a call to the driver or the original native pause dispatcher.
        if self.owner_resource_id is None:
            return "none"
        target = self.desired_running()
        if target == self.playing:
            return "none"
        self.playing = target
        return "resume" if target else "pause"

    def start(
        self, resource_id: int, generation: int, *,
        owner_is_theme: bool, music_identity_confirmed: bool = False,
    ) -> str:
        # A Theme controller and its unique loaded BNM index identify a
        # resource, NOT necessarily an audible soundtrack. Never relax
        # the policy merely because a driver handle is Theme-owned.
        if (not owner_is_theme or not music_identity_confirmed
                or not (0 <= resource_id <= 0xFFFFFFFF)):
            return "none"
        self.owner_resource_id = resource_id
        self.generation = generation
        self.native_play_requested = True
        self.playing = True  # Native engine has just created the stream.
        return self.transition()

    def stop(self, generation: int) -> str:
        if self.owner_resource_id is None or self.generation != generation:
            return "none"  # Do not stop a recycled driver handle.
        self.owner_resource_id = None
        self.native_play_requested = False
        self.playing = False
        self.generation = 0
        # A new stream generation must receive independent menu identity
        # evidence. Reusing an earlier "verified" ESC state across a
        # legitimate native Stop/level transition is unsafe.
        self.esc_menu_verified = False
        return "forget"  # Native StopTransition must pass through unchanged.

    def set_focus(self, *, known: bool, foreground: bool) -> str:
        self.foreground_known = known
        self.is_foreground = foreground if known else False
        return self.transition()

    def esc_open(self, verified: bool) -> str:
        # Raw ESC-key input is not proof of an in-game menu.
        prior = self.esc_pause_depth
        self.esc_pause_depth += 1
        self.esc_menu_verified = (
            bool(verified) if prior == 0
            else self.esc_menu_verified and bool(verified)
        )
        return self.transition()

    def esc_close(self) -> str:
        self.esc_pause_depth = max(0, self.esc_pause_depth - 1)
        if self.esc_pause_depth == 0:
            self.esc_menu_verified = False
        return self.transition()

    def other_pause(self) -> str:
        self.other_pause_depth += 1
        return self.transition()

    def other_resume(self) -> str:
        self.other_pause_depth = max(0, self.other_pause_depth - 1)
        return self.transition()

    def set_unknown_pause(self, unknown: bool) -> str:
        self.unknown_pause_reason = unknown
        return self.transition()
