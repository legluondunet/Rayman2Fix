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

## First confirmed opening-window results and full ESC-lifetime trace (14 November, 18:55)

Owner uploaded `Rayman2Fix(20261114-185515).log` from the
`3009c84f` passive instrumentation, with 11 script-origin ESC Pause
candidates. All 11 emitted `RUNTIME-ESC-PREDRIVER-GATE` and
`RUNTIME-ESC-PREDRIVER-WINDOW` lines. All 11 windows reported

```text
stop_during_wrapper=0 stop_after_wrapper=0
play_during_wrapper=0 play_after_wrapper=0
menu_known=1 esc_actor_confirmed=1
```

including six original controller mismatches (Theme `9:12` vs active
`9:5` / SXD `9:6`) and five later valid controller matches (inner
SXD `9:7`, `9:9`, or `9:10`). All Pause callbacks remained
unmodified: two same-handle slot-2 calls per source wrapper with
original depth `1 -> 2`. Ten original source Resume wrappers were
sampled, also two same-handle slot-3 calls each with depth
`2 -> 1` and no mixed native forwarding. Three changes of SXD
resource during an ESC menu still caused the normal `phase=6`
native passthrough. No audio-listening report accompanies that log.

**Interpretation:** the measured *short* interval, from the script
Pause wrapper to the first observed native menu tick, has no tracked
original PlayTransitionEx / SXD Stop callbacks. This **does not** prove
the stream stays stable over a full menu, that the backend cannot
change tracks internally, or that withheld physical Pauses can
be recovered.

To investigate the longer interval without a driver experiment, the
standard opt-in runtime now emits a bounded read-only
`RUNTIME-ESC-PREDRIVER-LIFETIME` beside the original Resume trace:

- `paired=1` only when the native script Resume can be matched to
  one sampled script Pause on the **same Win32 thread** and coordinator
  generation, with **no overlapping script-origin Pause**;
- `elapsed_ms` measures from after the original Pause wrapper until
  after the original Resume wrapper;
- `play_between` / `stop_between` count all observed native
  PlayTransitionEx / SXD Stop callbacks from *just before* the Pause
  wrapper until *just before* the paired Resume wrapper;
- `play_during_resume` / `stop_during_resume` count tracked callbacks
  during the original script Resume wrapper itself.

If `paired=0` because of a cross-thread Resume, an intervening
Pause, or a changed generation, **all printed zero deltas are invalid
and must not be interpreted as a quiet session**. Event counts
are global: a nonzero count might be SFX, while a zero count still
cannot exclude an internal SXD change. Instrumentation is limited to
the first 16 Pause and 16 Resume script wrappers, keeps all native
audio forwarded under the existing coordinator policy, and outputs
logs from the render thread after audio decisions rather than from
the native callback.

No driver state machine modification, new compensating commands,
time delays, or risky intercepts were introduced by the lifetime
observer. `pre_driver_pause::Policy` remains completely disconnected
from production.

## Counterexample: SXD resource changes with zero native Play/Stop events (14 November, 19:10)

Owner log `Rayman2Fix(20261114-191013).log` (199 lines)
shows ten fully identified ESC Pause source wrappers, nine matched
script Resume wrappers, and nine `RUNTIME-ESC-PREDRIVER-LIFETIME`
reports with `paired=1 overlap=0`. All nine show
`play_between=0 stop_between=0 play_during_resume=0
stop_during_resume=0`. Despite these global zero counts,
a script pause starts at **71,032 ms** with inner SXD **9:8**,
the existing read-only Theme watcher reports **9:9** at
**71,375 ms** with `menu=1`, and the coordinator enters
`phase=6 theme-or-sxd-stream-changed-native-passthrough`
before the paired Resume at **73,985 ms** (whose lifetime
counters still report all zeros). At a later opening at
82,454 ms, inner SXD **9:11** transitions to **239:5**
at 83,360 ms while menu=1; the log ends before a paired
script Resume for that opening.

**This invalidates a proposed safety criterion:** absence of
observed PlayTransitionEx and StopSxd/StopTransitionSxd
does not imply unchanged nested SXD provenance. The
change may be subresource progression or backend-internal
transition; it is not proof that a new native driver handle
was allocated. Never authorize physical Pause withholding
based on these counters.

### Next read-only observation: `RUNTIME-ESC-SXD-LIFETIME`

The same standard runtime now correlates *already acquired*
Theme/SXD resource-provenance snapshots with a
source-identified ESC menu lifetime. The existing confirmed
menu-tick `inspect_audio_theme_owner` result supplies an
initial inner-bank/index baseline; the existing change-triggered
Theme watcher contributes later inner provenance samples
while the game's menu flag is 1. No new
`ReadProcessMemory` or driver operation is added by the
observer, and all sampled state remains outside audio
driver callbacks.

The `RUNTIME-ESC-SXD-LIFETIME` line is emitted with
`RUNTIME-ESC-PREDRIVER-LIFETIME` after script Resume:

- `paired`: the pre-existing same-thread/generation/unique-pair test;
  all resource counts are invalid if 0.
- `sampled`: paired with at least one complete unique inner
  provenance snapshot. If 0, first/last keys and changes
  cannot be interpreted.
- `first`, `last`: observed original loaded bank and index,
  not file paths, pointers, or synthetic music identifiers.
- `samples`, `changes`, `unknown`: bounded count of
  valid observed snapshots, changes compared to the previous
  valid sample, and samples with incomplete provenance.
- `play_between`, `stop_between`: the already recorded
  global native command counts for direct comparison.

The sampling is *not exhaustive*: the watcher is at most
40 ms cadence and change-triggered, with a 48-event /
120 s observation budget, and some game changes may happen
between observations. `changes=0` still does not prove
a stable driver stream. `changes>0` with `play_between=0`
is a useful direct counterexample to command-count-only
authorizations. The source-dependent coordinator's
`phase=6` stream-change protection is unchanged.

**Go/no-go:** retain the previously validated music coordinator
and its native passthrough on uncertainty. Active pre-driver
Pause suppression stays disabled until the exact installed
SXD backend's rollback and stream-retirement behavior is
independently proven.

## Owner 19:28 source-paired SXD results and regression cases (14 November 2026)

The latest owner log `Rayman2Fix(20261114-192852).log`
confirms the new `RUNTIME-ESC-SXD-LIFETIME` measurement:

- **10** source-identified native ESC script Pause wrappers, **9**
  completed native script Resume wrappers. All nine paired
  successfully (`paired=1`, `overlap=0`) with exact Pause depth
  `1->2`, Resume depth `2->1` and no extra native driver slots.
- Four first-menu attempts kept the strict rejected Theme
  `9:12` / active outer `9:5` / inner SXD `9:6` mismatch.
  Six subsequent attempts requested the already validated
  paired-resume compensation when the Theme reference matched.
- **Three closed, paired menu sessions** detected a real nested SXD
  resource transition while still showing the ESC menu:
  `9:8 -> 9:9`, `9:10 -> 9:11`, and `239:5 -> 239:6`.
  Each had `samples=2 changes=1 unknown=0` and emitted the
  safety `phase=6` quarantine. The game forwarded both native
  script Resume callbacks in the quarantined sessions.
- A **fourth** transition `239:8 -> 239:1` was visible from
  the Theme watcher while the menu was open; the log ended
  before a paired script Resume, so no lifetime result can
  be claimed for this cycle.
- All 9 matching lifetimes and all 10 short opening windows
  still report zero observed native Play/Stop events.

This is now a **direct source-paired counterexample** to treating
Play/Stop counters as stream-identity assurance: provenance
may progress while those counters do not. It does NOT prove
that a physical driver handle was destroyed or recycled; an
SXD index progression could be ordinary playback sequence
inside an existing Theme.

Four exact observed inner-resource changes were added to pure
`esc_music_wine_prototype_tests.cpp`. They deliberately keep
driver handle, outer Theme and generation the same while
changing only inner SXD resource identity. They assert that
the compensator quarantines immediately, never binds an
old synthetic command to the new stream, passes through
both real script Resume callbacks, and safely re-arms on
a later independent menu. These tests call no game code
and do not enable pre-driver Pause suppression.

**Next research requirement:** identify the verified, backend-specific
native audio operation that can prevent the first early
Theme-mismatch Pause *without* leaving an unrepayable
original Pause debt across an SXD resource transition.
No bypass or hard-coded level/track exception is justified.
Until then the current conservative recovery is correct.

## Actual SXD driver DLL versus configured candidates (14 November, ongoing)

The owner-specific original retail CD has `dll/snd_cpa_mmx.ini`,
which the Rayman2Fix installer copies to `dll/snd_cpa.ini`
on fresh installations. The installed file may subsequently be
edited; it is not safe to infer the active driver from an MMX
filename or the `Default` setting alone.

Community reverse-engineering of the CPA sound system shows a
configurable `[DLL_Sxd]` chain with keys
`Unconditionnal` (the original misspelling),
`Default`, `File0`, `File1` and optional `TestN`
functions. The APM/ADPCM decoder selection is separate under
`[DLL_Adpcm]`, and the audio output/mixer plugins may import
WinMM `waveOut` (though individual installed versions vary).
For background, see:

- https://raymanpc.com/forum/threads/aureal-3d-a3d-restoration-achieving-better-3d-sound-quality-in-rayman-2-and-tonic-trouble-se-retail-update-6.26077/
- https://raymanpc.com/forum/threads/tonic-trouble-soundtrack.5981/page-5

These external reports are not verification of the owner's exact
1999 CD driver. **Do not silently substitute** an A3D,
DirectSound, WinMM, or aftermarket sound DLL: community reports
also document disappearing sound and crashes with alternate
audio configurations.

The standard, opt-in `MusicInPauseMenu` runtime now emits
two separate, strictly read-only diagnostics:

1. `RUNTIME-AUDIO-CONFIG` at explicit DLL initialization,
   before audio driver hooks are installed. It reads the
   actual installed `dll/snd_cpa.ini` via the OS's INI
   read function and prints only ASCII-normalized values:
   `sxd_unconditional`, `sxd_default`,
   `sxd_file0`, `sxd_file1` and `adpcm_default`.
   Unavailable, unreadable or linked INI is reported, not repaired. This is the
   **configured candidate list**, not proof of selection.
2. `RUNTIME-AUDIO-DRIVER` for each *actually hooked*
   original native PauseTransitionSxd, ResumeTransitionSxd,
   StopSxd, and StopTransitionSxd callback. The existing
   research helper uses `GetModuleHandleExW` with
   `FROM_ADDRESS | UNCHANGED_REFCOUNT` on the original
   callback pointer (saved before the existing table hook),
   then logs the owning module **basename and RVA only**.
   The same address-based helper now runs in the **standard**
   music-enabled instrumentation as well as research mode.
   Unlike the INI, this identifies what module owns the
   actual native callback code; if it reports an EXE thunk
   or unknown module, that remains an unresolved layer.

Both diagnostics leave all DLL names, bytes, function
pointers, audio flags and call behavior unchanged. The
original INI is never rewritten. They execute outside native
driver callbacks, on the same initialization/install paths
where other diagnostic messages already exist. No additional
`LoadLibrary`, `GetProcAddress`, or driver calls were
introduced.

**Evidence needed:** one ordinary Windows/native or Wine
runtime log showing `RUNTIME-AUDIO-CONFIG` and the four
`RUNTIME-AUDIO-DRIVER` module owners, under
`MusicInPauseMenu=1` and `Log=1`. The owner need not
modify any audio INI or install any new DLL. We can then
identify the actual backend and restrict further lifecycle
research to that verified module instead of extrapolating
from community driver names. No physical pre-driver Pause
withholding is enabled.

## Theme part sequencing versus physical SXD driver stream lifetime

A public MIT-licensed Rayman 2 CPA type-definition project,
`raytools/ACP_Ray2`, independently documents the relevant engine
memory structure:

- `src/SND/SND_Resource.h` defines the original
  `SND_E_TR_Theme` resource as type **11** (matching the
  read-only type-11 descriptor guard in our approved-CD observer).
- `src/SND/SND_Theme.h` defines
  `SND_tdstTypeThemeMem` as an ordered group of Theme parts
  and an optional outro. `SND_tdstThemePart` contains its own
  resource reference, loop boundaries/loop count, transition
  kind and fade duration. Transition types include
  **Crossfade**, **AtEndOfPart**, and **Outro**.
- `src/SND/SND_ResMem.h` separately defines in-memory
  Sequence, Switch, Split, and Random resources.

MIT project and inspected sources:

- https://github.com/raytools/ACP_Ray2
- https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Resource.h
- https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Theme.h
- https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_ResMem.h

**Interpretation correction:** `RUNTIME-ESC-SXD-LIFETIME`
`first/last` tracks *nested logical sound resource provenance*.
The observed `9:8 -> 9:9`, `9:10 -> 9:11`,
`239:5 -> 239:6` changes can be ordinary movement
between pieces of a Theme. They are **not evidence by
themselves** of deallocation, replacement, stopping or
reinitialization of the physical audio-driver stream.
The zero native PlayTransitionEx/Stop callback counts are
fully compatible with such an internal Theme-part scheduler,
but they cannot prove that this is the only possible path.

Consequently the existing coordinator's conservative
`phase=6` is currently a valid *logical provenance mismatch*
check, not proof of a changed physical driver handle.
Do not replace it with optimistic passthrough, and do not
assume a deferred physical Pause can be safely replayed or
discarded merely because the selected resource changed.
We still need the owner-approved original SXD DLL's real
Pause/Resume/Stop lifecycle contract before any pre-driver
interception.

The new standard `RUNTIME-AUDIO-CONFIG` and
`RUNTIME-AUDIO-DRIVER` lines are the next essential evidence:
they distinguish configured candidate plugins from the
actual code module implementing the original native
Pause/Resume/Stop callback addresses. No audio path is
changed by this research.

## Interpreting the first standard-backend audit (no engine changes)

Apply this conservative decision table to the owner's next ordinary
`Rayman2Fix.log`:

| Observed evidence | Supported inference | What remains unknown |
| --- | --- | --- |
| `RUNTIME-AUDIO-CONFIG status=present` | The installed original CPA config was read successfully | It may specify tests/fallbacks, and does not prove the selected output plugin |
| `sxd_unconditional=WAVa1` or similar | That value was configured by the original sound INI | Whether that plugin actually loaded or was later replaced |
| `RUNTIME-AUDIO-DRIVER channel=PauseTransitionSxd module=WAVMWBVR.dll` (illustrative, not seen yet) | The captured original Pause callback PC lies inside the loaded module of that basename | Its exact bytes/version, runtime voice identity and internal Pause debt semantics |
| All four Pause/Resume/Stop callbacks have the same module basename | The callbacks are implemented inside the same loaded PE module according to their original function-pointer addresses | Whether that module owns a single or several distinct physical streams/voices |
| Different modules, `rayman2.exe`, or `module=unknown` | Hook table source or plugin dispatch needs further inspection | Which downstream driver is actually responsible; do not assume a plugin name |
| `RUNTIME-ESC-SXD-LIFETIME changes>0` with `play_between=stop_between=0` | The nested logical resource changed without a tracked original Play/Stop call | Whether the driver recycled a handle or simply sequenced Theme parts |

This table prevents promoting either a configuration default or a
sampled Theme-part key into proof of physical driver ownership.
No `RUNTIME-AUDIO-` trace is an authorization to skip native
Pause/Resume, alter `snd_cpa.ini`, use an unrelated audio DLL, or
re-enable the unproven source-scoped pre-driver prototype.

## Verified original SXD output DLL on owner's Windows install (14 November, 20:19)

An ordinary owner's native Windows run with the new standard
instrumentation, `Rayman2Fix(20261114-201946).log`,
now supplies the missing **actual original native callback module**:

```text
RUNTIME-AUDIO-CONFIG: status=present
    sxd_unconditional=None sxd_default=WAVPW
    sxd_file0=WAVMW sxd_file1=unset adpcm_default=APMPW
RUNTIME-AUDIO-DRIVER: channel=PauseTransitionSxd
    module=WAVMWbvr.dll callback_rva=0x0000C7E0
RUNTIME-AUDIO-DRIVER: channel=ResumeTransitionSxd
    module=WAVMWbvr.dll callback_rva=0x0000C830
RUNTIME-AUDIO-DRIVER: channel=StopSxd
    module=WAVMWbvr.dll callback_rva=0x0000B920
RUNTIME-AUDIO-DRIVER: channel=StopTransitionSxd
    module=WAVMWbvr.dll callback_rva=0x0000C790
```

**The validated observation is loaded original callback ownership**,
not proof of what the backend does internally. The configured
`[DLL_Sxd] Default=WAVPW` does not match the module actually
providing these four callbacks (`WAVMWbvr.dll`, corresponding
to `File0=WAVMW`). This may reflect CPA capability testing or a
fallback; we have not yet disassembled the owner's installed DLL
to establish the exact selection logic.

Nine original script Pause wrappers and eight complete source-matched
script Resume wrappers were observed, with two native slot-2 calls
at depth `1→2` per opening, two native slot-3 calls at depth
`2→1` per observed exit, and no mixed Resume suppression.
The first five strict reference mismatches are forwarded normally.
Four subsequent openings request compensated resume batches;
one changes the nested SXD logical resource `9:7→9:8`
during the menu and quarantines with `phase=6`;
both original Resumes were forwarded. Two later complete
stable compensated menus suppress exactly their two paired
original Resumes. The ninth open menu has no matching
script Resume in this log and must not be classified.

**Next static target:** the exact file
`Rayman2Fix/dll/WAVMWbvr.dll` from this owner's tested retail
CD installation. Hash first, then read-only PE imports/exports,
and static control flow around RVAs
`0xC7E0`, `0xC830`, `0xB920`, `0xC790`.
Inspect driver state transitions, refcounted pause semantics,
Stop/Resume ordering, and any WinMM waveOut delegation.
Do not replace or modify the original DLL, and do not import
offsets from a GOG, Steam or other CPA game copy without a
byte-identical hash comparison.

Physical pre-driver interception remains disabled, even for
early `9:12` controller-reference mismatches. The fact that
two callbacks have the same module/handle is not enough to
prove rollback from a withheld Pause.

## Immediate Pause-only counterexample model (17 November 2026)

The owner's `Rayman2Fix(20261117-034508).log` (standard build `942f09cb`)
reconfirms that the **first early source-specific ESC** has an original
pending controller (`ref_class=3`, `pending_target_matches_outer=1`,
outer `9:5`, inner `9:6`). Its original wrapper produces two native
slot-2 Pauses on one handle, both forwarded; native pause depth `1→2`,
sampled original ADPCM `paused=1`. Its original exit forwards two slot-3
Resumes, depth `2→1`, and sampled `paused=0`. A later ESC when the
current Theme has synchronized (`ref_class=1`, inner `9:7`) requests
the existing compensation; it changes to SXD `9:8` while the menu is
open, enters `phase=6` and forwards the two original Resumes. These
are **source/driver observations**, not proof of continuous voice lifetime.

The independently SHA-verified original 1999 CD binaries establish one
useful *conditional* algebraic fact for WAVMW type-4 → APMMX:
`APMMXbvr.dll` `0x1EF0` assigns `record+0x1C=1` on a validated
native Pause; `0x1F30` assigns `record+0x1C=0` on a validated
Resume. No pause-depth counter is read at those two writes. These
functions run under the original APMMX lock via `0x3380` and
`0x33C0`. The **game's** source-table `+0x1C` remains a different
depth field, and the source wrappers must never be bypassed.

The new **unconnected, pure C++** model
`include/rayman2fix/esc_pause_only_model.hpp`, exercised by
`tests/esc_pause_only_model_tests.cpp`, enumerates both Pause decisions
for one **initially playing, unique, unchanged physical record**, followed
by exactly the two **unmodified original** Resumes. It computes:

| First native Pause | Second native Pause | While ESC (bit) | After native Resumes (bit) |
| --- | --- | --- | --- |
| forwarded | forwarded | 1 | 0 |
| skipped | forwarded | 1 | 0 |
| forwarded | skipped | 1 | 0 |
| skipped | skipped | 0 | 0 |

**Narrow result:** in this hypothetical stable-voice case, skipping only
one Pause **does not keep music playing**: the other Pause still sets the
same bit. Yet the original Resume bit clears are idempotent and do
not require a previous Pause bit set on that same unchanged record.
Therefore an isolated *flag-write* model does **not** inherently create
a refcount debt for a half-skipped pair. This is **not** a general claim
about CPA lifetime or the effects of skipping original driver callbacks.

All materially difficult cases are expressly **outside the model**:
already paused voice, unmatched original Resume, focus/Alt+Tab, nested
menu, other original driver operations, new Theme part, retired or
reused native handle/record (ABA), source/driver interleaving,
cross-thread code, or alternative sound backends. Real logs do not
establish that the two observed slot-2 callbacks and subsequent slot-3
callbacks target the exact same physical **generation** throughout the
menu. A transition can advance SXD `9:7→9:8` without recorded global
Play/Stop, and the original APMMX mixer can inactivate a voice itself.
The model always returns
`runtime_interception_authorized=false`, even for its passing case.

**Decision: NO-GO for enabling immediate Pause skipping.** This
research narrows an over-strong assumption that *every* partially
omitted Pause must necessarily accumulate an APMMX counter debt; it
does **not** solve source identity, physical generation or independent
focus ownership. Future work should first trace the two original
sound-table **source indices and nested native objects** as separate
read-only facts, without treating a shared driver handle as a durable
voice epoch. Existing strict `ref_class=1` authorization and
`phase=6` passthrough remain unchanged. Do not activate this model
via `MusicInPauseMenu`; PR #10 stays draft.

## Post-menu source alias observer (17 November 2026)

The original retail-CD disassembly verifies that
`0x4580B0` iterates **64 source records** of 32 bytes beginning at
`0x4B8C98` (loaded RVA `0xB8C98`). For an eligible transition record,
`0x4584A0` selects the source type through `[[record+4]+4]`. The
type-1 route loads `[record+0]` and passes it to `0x4553A0`, whose
registered callback goes to the native SXD transition driver. In
contrast, the type-2 route can dispatch through another driver.
A matching `[record+0]` pointer alone therefore does **not** prove
which source entry reached WAVMW or that an entry was invoked at all.

A **diagnostic-only, bounded** observer now runs on the render-thread
menu tick, **after** both the unchanged original script Pause wrapper
and the existing native audio compensation. When the already captured
script source reported exactly two matching native slot-2 callbacks,
the observed WAVMW object type was 4, and the previous game pause
depth is known, it reads the exact original 64-source table **twice**
(2 KiB per read) via the existing Win32 memory reader. If both full
byte snapshots match, it compares each entry's `+0` reference to
the *previously observed* native transition callback argument. The
report contains ONLY counts, first four **source indices**,
source-table transition eligibility (`+0x14 != 0`, `+0x08 == 0`,
`+0x10 != 0`) and original game depth stamps (`+0x1c` equal to
the old depth). No pointer/address is logged or retained, and no
memory is written.

The new log marker is:

```text
RUNTIME-ESC-SOURCE-ALIAS: snapshot=0|1 same_ref=N transition_candidates=N depth_stamped=N indices=a,b,c,d depth_before=N (post-wrapper double-read; not callback or physical voice proof)
```

This is intentionally **not** a pre-dispatch predictor: the table
may have changed during the original source wrapper and again between
the original callbacks and the menu tick. Two equal render-thread
reads are **not atomic with the native calls**; depth stamps can
predate this ESC and do not identify physical ADPCM handles. It is
still useful to distinguish whether the original source table
currently has **zero, one, two or more distinct indices** that alias
the observed transition *object*. For example, a count of 2 would be
consistent with two separate source entries referencing one object,
not proof of the two exact originating callbacks.

The pure classifier `include/rayman2fix/esc_source_alias.hpp`
has synthetic C++ regression tests
`tests/esc_source_alias_tests.cpp` (alias multiplicity, excluded
source, old depth stamp, torn/incomplete observation, bounded indices).
Both this classifier and the earlier flag-only model always set
`authorize_audio=false`. Neither is linked to an active Pause
suppression decision, and there is **no new driver callback hook**.

**Next owner-log check** (only after Windows x86 CI succeeds): a
post-intro early ESC and a later stable ESC with `[Debug] Log=1`,
`[Features] MusicInPauseMenu=1`. Compare `same_ref`,
`transition_candidates` and `depth_stamped` against the existing
`RUNTIME-ESC-PREDRIVER-GATE` line. Do not infer original callback
identity, atomicity, or native voice generation from matching counts.
Leave physical Pause withholding NO-GO and PR #10 DRAFT.
