# Isolated native Learn_10 NOMOVIES prototype (experimental)

This research variant is **not** the ordinary Rayman2Fix release and is
**not** the passive `learn10-trace` artifact. It is an explicitly opted-in
experiment to see whether the original game's **NOMOVIES** director branch
can end the 3D introduction using the **engine's own** scene transitions.

## Scope and risks

Only the supported, SHA-256-verified Rayman 2 executable is eligible.
The prototype requires all of the following simultaneously:

- The separate Windows x86 binary built with both
  `RAYMAN2FIX_RESEARCH_PROBES=ON` and
  `RAYMAN2FIX_LEARN10_NOMOVIES_PROTO=ON`.
- `[Debug] Log=1` and `[Features] SkipCinematics=1` in the existing INI.
- **Exact** environment variable
  `RAYMAN2FIX_LEARN10_NOMOVIES_PROTOTYPE=1` inherited by the game.
- First-visit `learn_10`, original story bit 1091 unset, original
  NOMOVIES bit **1188 unset**, native director in behaviour **3**, and
  both original script rules and their native cleanup fingerprints
  verified in process memory. Any mismatch refuses the experiment.

When all conditions hold, the experiment changes **only the NOMOVIES
bit 1188 in the game's in-memory progress array**, using an atomic
compare/exchange on its original 32-bit word. The other bits remain
untouched. It does **not** edit a script, call `F158`, change the
director behaviour, force `V96` or `var83`, move Rayman/camera,
inject keyboard events, or request a new level.

**Important:** the bit is temporarily in a progress array that the
game may save. The revised prototype exposes the flag for **at most
5 seconds of observed render callbacks**, restoring it as soon as the
native director leaves behaviour 3, at the 5-second deadline, at
handover (bit 1091) or on leaving the level. It only arms during the
first 3 seconds of the scene. If the game freezes and stops calling the
existing render callback, that deadline cannot execute until callbacks
resume. It also cannot guarantee
that an unexpected automatic save, crash, or level transition will
not persist experimental state. **Never run this on your normal
installation, real saves, or work profile.** Use a full, disposable
installation and separate saves (a dedicated Wine prefix/user profile
if needed). Do not save manually while the experiment is active.
If the picture corrupts, exit without saving and discard the
disposable copy.

## Test on Windows

1. Make a separate **copy** of an already-installed and verified
   Rayman2Fix folder, including `game/`, `state/`, `mods/`,
   `third_party/` and the INI. Use a separate test-only save
   location/profile. Keep the original installation untouched.
2. Download the CI artifact
   `Rayman2Fix-Windows-x86-learn10-nomovies-PROTOTYPE` from the
   experimental PR. Replace only the copied root `Rayman2Fix.exe`.
   The launcher will update its own `game/fix.dll` from the embedded
   resource; nothing is copied over the original installation.
3. In **PowerShell**, from the disposable copy:
   ```powershell
   $env:RAYMAN2FIX_LEARN10_NOMOVIES_PROTOTYPE = '1'
   try {
       .\Rayman2Fix.exe
   } finally {
       Remove-Item Env:RAYMAN2FIX_LEARN10_NOMOVIES_PROTOTYPE -ErrorAction SilentlyContinue
   }
   ```
4. Start a **new game** and skip the second cinematic (`jail_20`)
   as usual. Once the 3D introduction in `learn_10` has started,
   **release all keys**, wait approximately **2 seconds**, then
   **press and release Enter once** (do not mash keys). The original
   script's *NOMOVIES ELSE* route contains its **own**
   `JustPressedBut` and `IsTimeElapsed(V93,1000)` input/time gate,
   **distinct** from the button check in the `IfNot` route.
   The original button operand is opaque, so Enter is a **hypothesis**
   rather than a proven key mapping. No input is synthesized by the fix.
   The prototype activates the flag early (up to t=3 seconds) and
   attempts to clear it when the director leaves behaviour 3 or after
   five seconds, whichever happens first.
5. If Rayman gains control early, check the 3D world, camera,
   HUD, movement, pause and return to normal gameplay. **Do not
   save** during this initial test. Exit and provide the new
   `Rayman2Fix.log` and `Rayman2Fix_installer.log`. If the 3D
   scene completes normally instead, that is a valid negative
   experimental result.

For Wine, use the same single experimental EXE inside a disposable
installation and **separate Wine prefix**, setting the opt-in variable
only for the test command:

```sh
RAYMAN2FIX_LEARN10_NOMOVIES_PROTOTYPE=1 wine Rayman2Fix.exe
```

## First live Windows result (17 November 2026)

The 406-line test log `Rayman2Fix(20261117-192631).log`
demonstrated an accelerated engine-managed handover with the opt-in
build: NOMOVIES armed at **281 ms**, physical Enter detected at
**3,078 ms**, original director changed behaviour via index 7 toward 1,
NOMOVIES restored at **3,109 ms**, and story bit 1091 confirmed at
**3,125 ms**. At handover the temporary bit 1188 was **0**, compared
with about 25 seconds for the natural cinematic. No direct V96 write,
director-behaviour jump or artificial keyboard event was used.

The log also shows HUD callbacks and native ESC menu state changes
after handover, **but that does not prove the graphics, camera, music,
input or menu look and behave correctly**. Neither a persistently
clean game save nor Wine parity has been demonstrated. This
experimental build must remain separate from release until those
checks are complete.

The next revision clarifies the log wording: in the opt-in build,
`phase=handover` reports whether NOMOVIES was ever armed, whether
it was rolled back, whether it remained armed, and the number of
physical Enter edges. All of these are **observations of in-memory
state**, not proof that every saved file is uncontaminated.

## Offline automated audit (no save-file access)

A standalone log auditor is included in the experimental branch:
tools/learn10_trace_validator.py. It requires Python 3 (standard
library only), and can run on Linux or Windows. For Windows without
Python, simply upload the non-cumulative log for analysis instead.

For example:

    python3 tools/learn10_trace_validator.py "Rayman2Fix(20261117-192631).log"
    python3 tools/learn10_trace_validator.py --json "Rayman2Fix(20261117-192631).log"

Results:
- CANDIDATE, exit 0: fast native handover, Enter edge, bit 1188 verified
  clear on handover, director=1, Rayman not suspended and no rollback
  errors detected in the log
- INCOMPLETE, exit 3: essential input or handover evidence missing, or
  the level reached a normal-duration handover (not a fast skip)
- UNSAFE, exit 2: observed rollback error, bit still set at handover,
  incompatible event order, duplicated sessions or exposure >5 seconds
- NOT_APPLICABLE, exit 4: this is not an enabled NOMOVIES prototype log

The auditor works on logs from both the original experimental
prototype and the newer build with explicit phase=handover-verification.
For older logs, missing armed_duration_ms and handover-verification
are reported as warnings, not silently fabricated as passes.

Even CANDIDATE is **not a final safety certificate**: it cannot see
the game picture, feel controls, detect unlogged frame stalls, certify
Wine compatibility, or inspect whether bit 1188 was written to a save
file. Run the test only with truly isolated, disposable saves.

## Early-Enter diagnostic (18 November 2026)

Two **Windows logs from the same runtime build** establish a timing
sensitivity without yet proving the native cause:

| Test log | NOMOVIES armed | Physical Enter | Rollback | Gameplay |
| --- | ---: | ---: | --- | ---: |
| `Rayman2Fix(20261118-133619).log` | 640 ms | **843 ms** | after 5,000 ms exposure | 25,406 ms (natural) |
| `Rayman2Fix(20261118-133715).log` | 281 ms | **4,109 ms** | after 3,735 ms exposure; native director left b3 | 4,156 ms (fast) |

The native NOMOVIES ELSE rule checks
`JustPressedBut` **and** `IsTimeElapsed(V93,1000)`. The failed
press occurred only **203 ms after the experimental bit was armed**,
whereas the successful press occurred **3,828 ms after**. That
correlation suggests an early timer-gate rejection but does not prove
the engine's timer `V93` was reset when we armed the bit. The
`V119` guard might also contribute; neither variable was observed
after the first native script snapshot.

**Important:** the previous experiment observed physical Enter only
while bit1188 was armed. Its single observed early press does NOT
prove the user did not retry after the five-second rollback. The game
may also miss a very short input between sampling callbacks.

The new research-only diagnostic adds:
- `since_arm_ms` and `within_first_second_of_arm` to physical Enter
  logs, explicitly **only a timing proxy** (not native timer evidence)
- `phase=physical-enter-after-rollback` to record later physical
  keypresses **without reactivating bit1188**
- `phase=native-gate-snapshot` with bounded, read-only integer
  samples of `V93` and `V119` while the original director is in
  behaviour 3, to distinguish an elapsed-time guard from a persistent
  early-key latch

**No synthesized input, no longer NOMOVIES exposure, and no changed
gameplay behaviour** are introduced. The goal is to establish which
engine-native condition blocks an early press before implementing a
safe buffered input approach. Use disposable saves only.

## Evidence in the non-cumulative runtime log

- `RUNTIME-LEARN10-PROTOTYPE: enabled=1`: compile-time and environment
  opt-in both present, with logging enabled.
- `phase=armed`: original script identities verified and bit 1188
  temporarily enabled. **This does not prove the game used the skip.**
- `phase=refused`: no bit changed (unknown director/script,
  changed pointer/flags, or unsupported installation).
- `phase=physical-enter-edge`: physical Enter pressed while the
  experimental bit was active. This **does not prove** that the game's
  opaque `JustPressedBut` operand recognized it.
- `phase=restored reason=director-left-b3` or
  `reason=max-exposure-5000ms`: flag cleared without waiting for the
  end of the ~25-second cinematic.
- `phase=restored reason=story1091-handover`: fallback restoration if
  still armed, then `nomovies-flag at-handover enabled=0` confirms this.
- `phase=restore-pending` or `phase=error`: stop, do not save,
  discard test copy, provide logs.
- `natural=handover`, director behaviour 1, story bit 1091 and
  normal Rayman/camera/HUD/controls: compare visually to the
  unmodified ~25-second natural sequence.

The first prototype incorrectly treated the two native button gates as
interchangeable; the second attempt incorrectly assumed the ELSE route
needs no input. The actual compiled rule has **two separate
`JustPressedBut` conditions**, one in each branch, and the ELSE gate
also has a native 1-second timer condition. This test uses the ELSE
gate with real physical input and a bounded flag exposure window.

No conclusion about safety or effectiveness follows solely from
successful compilation or from the bit being armed. The experimental
PR must remain separate and unmerged until Windows and Wine gameplay
tests confirm correct cleanup **without save contamination**.

## Two Enter presses in one successful session (18 November 2026)

New Windows log: `Rayman2Fix(20261118-141030).log`, one experimental
runtime session. This older build **logs** NOMOVIES `phase=armed t=266ms`,
but the timestamp was sampled *before* the large compiled-script probe.
The later monotonic counter reports the first physical Enter at **500 ms**
as only **109 ms after arming** (effective activation near 391 ms,
inferred rather than explicitly timestamped). The updated prototype
logs the current monotonic arm time as `t` plus `sampled_intro_ms`
to expose the diagnostic delay. This does not change the gameplay.
Physical Enter #1 at **500 ms** did
not cause a transition. Enter #2 occurred at **3,531 ms** (3,140 ms
after arming). The original director changed behaviour **3 -> 7**
at **3,547 ms** (16 ms later), then **7 -> 1** at **3,562 ms**.
Engine handover followed at **3,578 ms**.

Bit1188 was restored when the director left behaviour 3, after
**3,171 ms** exposure; in-memory `handover-verification status=pass`
confirmed bit1188=0 and rollback_complete=1. This does **not**
verify graphics, controls or saved files.

Native V93 remained 66 and V119 remained 0 at 1,000 ms and 2,500 ms.
Those scalar snapshots do not reveal the engine's actual
`IsTimeElapsed(V93,1000)` evaluation. The timing correlation of the
second press and native exit does not prove causal input mapping.

The offline trace validator now lists each Enter edge, its time
since arming and any nearby behaviour-7 transition, explicitly as
**timing evidence only**. Next: establish the native input mapping and
timer outcome before experimenting with buffered early input. Do not
inject synthetic keys, change V93/V119, extend flag exposure or merge
the prototype on this log alone.
