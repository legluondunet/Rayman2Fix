"""Research-only shadow driver policy for Rayman 2's ESC menu music.

This is a strictly isolated state machine: NO game-memory operations and NO
playback changes. The future C++ adapter must independently prove the live
level-music resource, ESC menu origin, and driver pause/resume semantics.
No adapter is implemented here. The native soundtrack and all SFX remain
untouched until those conditions have been validated under Wine/Windows.

Do not confuse a native pair of calls for the same Theme->SXD driver with two
separate user pause actions. The policy takes cause events from upstream
wrappers, not from per-record call counts, then emits at most one *logical*
driver transition per change of desired play state. Backend call cardinality
may differ and remains unverified.
"""
from dataclasses import dataclass, field
from typing import Literal

Action = Literal["none", "pause", "resume", "forward-stop",
                 "forward-native", "hold-unknown", "suppress-paired-native"]
Cause = Literal["esc", "focus", "other", "unknown"]
Route = Literal["theme", "sxd"]


@dataclass(frozen=True)
class MusicGeneration:
    """Never identify a stream by a recycled driver handle alone."""
    bank: int
    theme_index: int
    inner_bank: int
    inner_index: int
    generation: int


@dataclass
class EscMusicShadow:
    owner: MusicGeneration | None = None
    foreground: bool = False
    focus_known: bool = False
    esc_open: bool = False
    other_pause_depth: int = 0
    quarantine: bool = False
    physical_paused: bool = False
    sound_stopped: bool = True
    # Distinct routes may issue duplicate calls to the SAME driver.
    callback_count: dict[tuple[str, str], int] = field(default_factory=dict)
    transitions: list[Action] = field(default_factory=list)

    def attach(self, generation: MusicGeneration, *,
               verified_level_music: bool,
               verified_esc_lifetime: bool,
               verified_driver_adapter: bool,
               known_foreground: bool, foreground: bool) -> bool:
        if (self.owner is not None or not verified_level_music
                or not verified_esc_lifetime or not verified_driver_adapter
                or not known_foreground or not foreground
                or generation.generation <= 0
                or min(generation.bank, generation.theme_index,
                       generation.inner_bank, generation.inner_index) < 0
                or max(generation.bank, generation.theme_index,
                       generation.inner_bank, generation.inner_index) > 65535):
            return False
        self.owner = generation
        self.foreground = foreground
        self.focus_known = True
        self.esc_open = False
        self.other_pause_depth = 0
        self.quarantine = False
        self.physical_paused = False
        self.sound_stopped = False
        self.callback_count.clear()
        return True

    def _emit(self, action: Action) -> Action:
        if action != "none":
            self.transitions.append(action)
        return action

    def _reconcile(self) -> Action:
        if self.owner is None or self.sound_stopped:
            return "none"
        # ESC does NOT demand silence. Every other native reason does.
        should_pause = (
            not self.focus_known or not self.foreground
            or self.other_pause_depth > 0 or self.quarantine
        )
        if should_pause == self.physical_paused:
            return "none"
        self.physical_paused = should_pause
        return self._emit("pause" if should_pause else "resume")

    def menu_open(self, *, verified: bool) -> Action:
        if self.owner is None:
            return "forward-native"
        if not verified:
            return self.uncertain_origin()
        self.esc_open = True
        return self._reconcile()

    def menu_close(self, *, verified: bool) -> Action:
        if self.owner is None:
            return "forward-native"
        if not verified:
            return self.uncertain_origin()
        self.esc_open = False
        return self._reconcile()

    def focus_change(self, *, known: bool, foreground: bool) -> Action:
        if self.owner is None:
            return "none"
        self.focus_known = known
        self.foreground = foreground if known else False
        return self._reconcile()

    def other_pause(self) -> Action:
        if self.owner is None:
            return "none"
        self.other_pause_depth += 1
        return self._reconcile()

    def other_resume(self) -> Action:
        if self.owner is None:
            return "none"
        if self.other_pause_depth == 0:
            return self.uncertain_origin()
        self.other_pause_depth -= 1
        return self._reconcile()

    def uncertain_origin(self) -> Action:
        if self.owner is None:
            return "forward-native"
        self.quarantine = True
        self._reconcile()
        # Quarantine prevents any future artificial resume. Only a
        # native Stop or a fresh independently verified generation clears it.
        return "hold-unknown"

    def native_driver_callback(self, generation: MusicGeneration,
                               kind: Literal["pause", "resume"],
                               route: Route, cause: Cause) -> Action:
        if self.owner != generation or self.sound_stopped:
            return "forward-native"
        if route not in ("theme", "sxd") or kind not in ("pause", "resume"):
            return self.uncertain_origin()
        if cause == "unknown":
            return self.uncertain_origin()
        # These callbacks are expected to be emitted after upstream
        # cause tracking; they cannot discover a cause by themselves.
        self.callback_count[(kind, route)] = (
            self.callback_count.get((kind, route), 0) + 1
        )
        if self.quarantine:
            return "hold-unknown"
        return "suppress-paired-native"

    def native_stop(self, generation: MusicGeneration) -> Action:
        # Native stop ALWAYS wins; never synthesize a resume or carry the
        # menu eligibility to a new Theme with an identical bank index.
        if self.owner != generation:
            return "forward-stop"
        self.owner = None
        self.sound_stopped = True
        self.esc_open = False
        self.foreground = False
        self.focus_known = False
        self.quarantine = False
        self.physical_paused = False
        self.other_pause_depth = 0
        self.callback_count.clear()
        return self._emit("forward-stop")
