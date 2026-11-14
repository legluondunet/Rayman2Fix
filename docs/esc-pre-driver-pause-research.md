# ESC music: pre-driver Pause research (not enabled)

This document describes an **unshipped research design** for the original
retail CD x86 game. It is not a workaround for existing Wine/Windows audio
in the current Rayman2Fix launcher. The existing opt-in
`[Features] MusicInPauseMenu=1` path and its tested Theme/SXD
stream-generation guard remain authoritative.

## What the owner's logs establish

The original game may still have its controller pointing at Theme resource
`9:12` while the unique active outer Theme is `9:5`, with an inner
SXD `9:6`. This can persist across several ESC open/close cycles after
`jail_20 -> learn_10`. After the native controller eventually matches
`9:5` and SXD advances to `9:7`, our existing two-call
ResumeTransitionSxd compensation can work. This happens after *each* suitable
scene change, not necessarily after the first-ever ESC.

The current exact native controller reference comparison intentionally
rejects speculative synthetic Resume commands during this period. The
source-derived x86 control path is:

- The game-specific script Pause wrapper is at RVA `0x536D0`, calling
  original native `0x4580B0`. The paired script Resume wrapper is RVA
  `0x53700`, calling native `0x458110`.
- The native audio loop maintains pause depth at `0x4A3CC8` and dispatches
  driver operations for individual sound-table entries. Skipping the entire
  wrapper would break the original game's bookkeeping.
- Source-specific game script callsites are RVA `0x71E70` for Pause and
  `0x71E67` for Resume, distinct from Win32 activation calls.

The log `Rayman2Fix(20261114-152301).log` supports this timing
interpretation. It does **not** prove that a physical driver can tolerate a
missing Pause and Resume pair under every APM backend.

## Alternative under investigation

A source-scoped **pre-driver** strategy may avoid the audible initial pause
and the unsafe post-hoc synthetic Resume. The proof obligations are:

1. The original script wrapper, engine pause-depth bookkeeping, menus,
   Stop and SFX must still run unchanged.
2. Only after a verified ESC script source is observed may exactly two
   *PauseTransitionSxd* callbacks on one uniquely identified Theme/SXD
   transport be considered for suppression. Other native slots and handles
   must not be silently suppressed.
3. The first intercepted Pause cannot be lost if a second matching Pause
   never arrives. The native call must be replayed in original order, on
   the original stream and before any intervening driver operation.
4. After the wrapper, the real native ESC menu actor must appear within a
   bounded time. Menu uncertainty or loss of focus requires fail-closed
   rollback, **but** a stale Pause must never be applied to a new SXD
   resource or recycled driver handle.
5. Exactly two matching script-origin native Resume callbacks must balance
   two suppressed Pauses. Native focus callbacks must remain independent;
   Stop invalidates the old transport generation. Unknown sources, nested
   script pauses and unpaired resumes always fail closed.
6. The controller's alternative Theme reference is **never** patched or
   equated to the active Theme merely because it comes from the same bank.
   No bank ID, level name, intro delay or Wine-specific code is hardcoded.

The pure C++ policy
`include/rayman2fix/esc_pre_driver_pause_policy.hpp` and
`tests/esc_pre_driver_pause_tests.cpp` model these cases. They do **not**
call game code and are **not** wired into runtime audio forwarding.

## Instrumentation before attempting a real intercept

When `[Debug] Log=1` is set, the first 16 qualified ESC script pause
wrappers now gather lightweight callback telemetry **before** each native
driver forwarding, but write their reports **after** the menu tick's
time-critical audio decision:

```text
RUNTIME-ESC-PREDRIVER-GATE: t_ms=... callbacks=... transition_pauses=... other_slots=... different_handles=... slots=... depth_before=... depth_after=... owner_driver=... native_ref_class=... outer_bank=... inner_bank=...
```

Every native callback is still forwarded unchanged. This trace is designed
to answer whether the two known PauseTransitionSxd calls form a consecutive,
uniquely owned same-handle pair, whether another audio operation interleaves
and whether original depth bookkeeping remains coherent in the mismatched
`9:12`/active `9:5` state. Repeated tests are unnecessary until a
paired sequence is demonstrated.

## Blockers before a genuinely executable experiment

The pure model's `impossible_replay` case is *not* a safe physical rollback:
a stream may change while a Pause is held. Before any runtime interception,
prove how to preserve audio ordering under this case, whether the original
APM backend is sensitive to missing a pair, and how focus/Stop events
interleave with script callbacks. A separate build-time and runtime opt-in,
real-driver dry-run checks and listening tests on both Wine and Windows are
required. Do not replace the validated regular launcher with this model.
