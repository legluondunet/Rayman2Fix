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

## Paired native Resume observation (14 November 2026, passive only)

The owner's fresh Windows-native run `Rayman2Fix(20261114-172750).log`
captured **ten** qualified script Pause wrappers. Every one contained
exactly two `PauseTransitionSxd` callbacks with the same native handle,
no unrelated driver slot, and original depth bookkeeping `1 -> 2`.
Five first-menu candidates were rejected due to the verified alternate
native Theme reference `9:12` while the active outer/inner resources were
`9:5` / `9:6`; the five later candidates requested compensation after
the active inner moved to `9:7` or later. A stream change triggered a
`phase=6` native-passthrough quarantine. The owner confirmed the **level
music resumed on every menu exit and never vanished during gameplay**.
These facts make the current safety rollback a useful stable baseline.

The next uncertainty is the **native script Resume pair**, especially for
menus rejected at the controller-identity gate. An independent, bounded
read-only `RUNTIME-ESC-PREDRIVER-RESUME` report now records, for at most
the first 16 source-identified script Resume wrappers:

- `callbacks`, `transition_resumes`, `other_slots`,
  `different_handles` and first four driver slot IDs;
- `native_forwarded` / `native_suppressed` for slot 3. These are
  observations of the **existing** coordinator's decisions, not controls;
- original global pause depth before/after the unchanged native wrapper
  and coordinator phase before/after exit.

The tiny thread-local observer counts at the native callback boundary;
it never scans banks or logs there. A pending snapshot is written **after**
the original script Resume returns. Disk output is postponed to the next
render tick, after normal audio compensation, even if the coordinator has
already returned to idle. No Pause, Resume or Stop implementation, argument,
handle, call count, authorization guard, or INI default was changed.

An additional owner run should test an early first ESC and several
subsequent exits (preferably two while `9:6` is still playing, then two
after `9:7`), with `MusicInPauseMenu=1` and `Log=1`.
For early rejected menus expect native Resume calls to be **forwarded**;
for stable compensated menus the existing paired Resume suppression is
expected. These are predictions to compare against real logs, not asserted
results. If the report shows unexpected slots, mixed handles, missing
pairs, or incompatible depth bookkeeping, a physical pre-driver suppression
experiment remains prohibited. Even matching Resume pairs are not enough
to solve the stream-handoff `impossible_replay` blocker in the research
model. Keep this mode diagnostic-only and the PR draft.

## Native Resume paired evidence and focus-loss no-go rule (14 November, 18:09)

The follow-up owner's Windows native log
`Rayman2Fix(20261114-180950).log` completed the passive observation:

- 20 genuine ESC script pause candidates; first 16 logged Pause wrapper
  pairs, and first 16 logged Resume wrapper pairs.
- Every logged Pause wrapper had exactly **two slot-2 callbacks** for one
  driver handle, no unrelated slot, with pause depth `1 -> 2`.
- Every logged Resume wrapper had exactly **two slot-3 callbacks** for one
  driver handle, no unrelated slot, with pause depth `2 -> 1`.
- **Seven** first-menu rejections occurred with alternate controller
  Theme `9:12`, outer Theme `9:5`, inner SXD `9:6`. All seven matching
  Resume pairs were forwarded to the original driver (`2/0`).
- After the controller matched Theme `9:5` and SXD advanced to `9:7`,
  the next five Resume pairs were suppressed by the existing coordinator
  (`0/2`) in sessions where the corresponding synthetic resume batch
  had been authorized.
- Two `phase=6` quarantined sessions returned to **native forwarded**
  Resume pairs (`2/0`), followed by two subsequent successfully
  compensated sessions (`0/2`). No mixed forwarded/suppressed pair
  appears in these 16 samples.
- Four `driver-command-rejected` stream-generation events occurred in
  the full log; a single session can switch SXD resources during an
  open menu. Do not mistake every `paired-native-resume-requested`
  entry for a guaranteed complete stable menu session.

These results establish strong source-specific callback pairing but do
**not** establish safe physical rollback. A native stream may disappear
or switch after an intercepted original Pause, before a verified ESC menu
actor is observed, even when both Pause callbacks arrived as a pair.
If that original native Pause is withheld and the exact source stream
no longer exists, a later replay to a recycled handle could pause
an unrelated song. The pure model signals `impossible_replay`;
it has no safe native-driver recovery for that case.

As an extra research-only safeguard, the pure model now immediately
falls back on **any loss of foreground** during pre-actor menu
confirmation, even if the menu actor is unknown or never observed.
It replays held native Pauses only if the identical original stream
is still valid; otherwise it explicitly reports impossible replay
and emits no stale driver calls. Two synthetic tests cover this
fail-closed decision and subsequent fresh-session rearming.

**No-go for active physical pre-driver Pause withholding** until an
independent proof establishes safe ordering for the stream-handoff and
missing-original-Pause case. No build-time opt-in or live hook should
enable it yet. The normal coordinator already preserves level music
through safe gameplay exits; its existing `phase=6` native pass-through
must remain intact. Keep production audio untouched.

## Native Play/Stop interval audit and backend boundary (14 November, follow-up)

The 18:09 owner run showed perfectly paired source-specific Pause and
Resume callbacks (first 16 each), but **four stream-generation changes
while menus were open**. That disproves the assumption that the entire
ESC menu interval necessarily owns one unchanging SXD resource. It
does not establish whether a particular transition happened *before*
the menu actor was confirmed, nor what a native driver does internally.

Relevant external material distinguishes the ADPCM/APM decoder layer from
the selectable CPA audio output layer:

- The community thread
  https://raymanpc.com/forum/threads/tonic-trouble-soundtrack.5981/page-5
  reports `[DLL_Adpcm]` decoder drivers (`APMmx`, `APMP5`) separately
  from `[DLL_Sxd]` output drivers and notes WinMM `waveOut` imports
  in the SXD driver family. This is **community observation**, not a
  disassembly of the owner's approved CD DLL.
- FFmpeg's APM demuxer/decoder patch
  https://ffmpeg.org/pipermail/ffmpeg-devel/2020-February/257381.html
  concerns compressed APM media and does **not** document native
  `PauseTransitionSxd` / `ResumeTransitionSxd` semantics.
- `Snd_Cpa.ini` can select output backends, so the effect of dropping
  or replaying a pause must not be assumed to be identical under the
  WinMM, DirectSound and other installed drivers.

The installer source `src/launcher/auto_flow.cpp` establishes one more
edition-specific detail: a fresh install copies
`dll/snd_cpa_mmx.ini` to `dll/snd_cpa.ini`. This makes the MMX
configuration the installed default, **not** proof of the exact active
`[DLL_Sxd]` runtime driver or of its Stop/Play semantics. The selected
ini contents and exact installed driver binary must be inspected before
any backend-specific claim. Existing installations may preserve their
already configured sound driver.

In the standard opt-in runtime, the next bounded **passive** log line is
`RUNTIME-ESC-PREDRIVER-WINDOW`. It samples already observed, global
monotonic event counts at three boundaries: (1) immediately before the
original script Pause wrapper, (2) immediately after it, and (3) at the
next ESC menu tick. It reports:

- `stop_during_wrapper`, `stop_after_wrapper`: native StopSxd or
  StopTransitionSxd callbacks recorded through the existing hooks;
- `play_during_wrapper`, `play_after_wrapper`: calls of the existing
  original PlayTransitionEx hook; and
- `menu_known`, `esc_actor_confirmed`: the already established native
  menu flag and separate verified ESC actor on the render tick.

The counters are **global to observed driver callbacks** and may include
other sounds. A nonzero delta means an apparently quiet interval
was not actually free of tracked Play/Stop events; it does **not**
prove a specific music track changed. A zero delta does **not** prove
SXD stability: some stream changes may not use those tracked APIs,
and events may occur after the logged tick. The log is emitted only
**after** the current audio compensation path, not inside the native
driver callback; it cannot delay a native pause for disk I/O.

The pure `pre_driver_pause::Policy` also now reports
`impossible_replay=true` if a native Stop retires the exact stream while
one or both original Pauses are held. Previously this `void` Stop path
silently discarded the debt. Both one-Pause and two-Pause cases have
pure synthetic tests. This does **not** repair the driver; it prevents
research from mistaking an unbalanced Stop for a safe rollback.

**Consequence:** keep active pre-driver suppression disabled. The next
owner run can collect this new log to see whether native Stop/Play
occurs before actor confirmation, but it will not solve the unavoidable
stream-retirement problem by itself. A *real* driver-specific audit of
the exact installed SXD binary and lifecycle contract is still needed
before the risky experiment can be enabled.
