> **ESC menu music (opt-in):** in `rayman2/rayman2fix/Rayman2Fix.ini`,
> set `[Features] MusicInPauseMenu=1`. This single option enables the
> guarded native Theme/SXD continuation, including verified early Theme
> handoffs. The default is `0`. `[Debug] Log` controls diagnostics only.
> Native game Pause bookkeeping, Stop, focus and stream-change safety
> checks remain active. The retired `EarlyThemeMusicExperimental` key
> is removed during INI migration; an existing MusicInPauseMenu choice
> is preserved.

## New nested layout and safe migration

The **retail game** is installed under `rayman2/`; the mod's
configuration, runtime DLL, GameFrame `glide2x.dll`, license,
backups are stored under `rayman2/rayman2fix/`; optional replacement images live in `rayman2/rayman2fix/textures/`.
The game retains `rayman2/` as its working directory. The launcher
prepends the nested mod directory to the **child-only** Windows PATH
for Glide DLL lookup and injects the runtime by its absolute path.

`winmm.dll` stays beside `rayman2.exe` so Windows locates the
game-facing audio bridge. Windows also requires the generated
`rayman2.exe.manifest` beside the EXE for per-monitor DPI behavior.
All three diagnostic logs (installer, runtime and optional CD inspection)
remain beside the outer `Rayman2Fix.exe`.

On verified startup a legacy `Rayman2Fix/` installation is migrated
to `rayman2/` by renaming its directory and moving only known mod
files into the nested `rayman2fix/`. No game media is re-extracted,
and existing INI values, backed-up executables, receipts and saves
remain unchanged. The old `Rayman2Fix_logo_hd.rgba` file is deliberately
not migrated or renamed; recreate the optional resource directly at
`rayman2/rayman2fix/textures/logo.rgba` with the converter. If **both** old and new directories
exist, migration refuses to overwrite either. The unchecked launch
mode does not migrate or repair files.

## Optional real-HD Rayman 2 opening logo (experimental; issue #16)

The 18 pieces of the retail CD's "Rayman 2 — The Great Escape" title
are stored as vertically inverted 32×32 Glide format-12 textures. The
owner's texture captures reconstruct the *entire* upright 192×96 logo
without gaps in a verified 6×3 arrangement:

```
34 31 28 25 22 19
33 30 27 24 21 18
32 29 26 23 20 17
```

The previous 4× bicubic interpolator (#18) was tested and made nearly
no visible improvement; **this implementation instead uses an optional
genuinely higher-resolution image supplied locally by the user**.
It is not merged with or dependent upon the bicubic prototype.

No third-party or Ubisoft picture is committed, embedded, or distributed
by Rayman2Fix. The user can get a cleaner 1582×676 transparent-logo
reference at
https://raymanpc.com/wiki/script-en/index.php?title=File:Rayman_2_Logo.png .
Copyright and terms for that artwork remain with their respective
owners: the user chooses and acquires the image separately. The included
open-source helper converts a transparent PNG to a fixed 768×384 raw
RGBA atlas that the GameFrame DLL recognizes. The HD atlas remains a
local optional file, not a game-resource rewrite.

On Linux/Manjaro (requires python-pillow), from the Rayman2Fix launcher
directory:

```bash
curl -fL -o prepare_intro_logo_hd.py \
  https://raw.githubusercontent.com/legluondunet/Rayman2Fix/feat/nested-game-and-fix-layout/tools/prepare_intro_logo_hd.py
python prepare_intro_logo_hd.py --download
```

RayWiki sometimes returns HTTP 403 to automated requests. Open the
[1582×676 original PNG](https://raymanpc.com/wiki/script-en/images/b/bb/Rayman_2_Logo.png)
in a normal browser. Save it as `Rayman_2_Logo.png` beside the launcher,
or in `~/Downloads/` or `~/Téléchargements/`. Re-run
`python prepare_intro_logo_hd.py --download`: the updated helper
detects the local file before attempting another network download.
Alternatively use `python prepare_intro_logo_hd.py --png /path/to/logo.png`.
Never silently fall back to a different, lower-quality logo; only
the user-provided original or a successfully downloaded original is used.
The browser-download fallback only needs Python and Pillow.
The helper creates `rayman2/rayman2fix/textures/logo.rgba` when run from the launcher directory.

The pinned GameFrame renderer loads that exact-size/header atlas on
first use, and changes **only** eighteen original 32×32 format-12
textures whose decoded RGBA FNV-1a64 fingerprints match the approved
CD source. Their original draw calls still control the placement,
geometry, alpha blends, timing, fades, and cinematic transitions. UV
scale remains the original 32×32 logical size; just the GPU texture
is 128×128. The formerly over-strict single-mipmap condition has been removed. All available source mip levels are validated first and replaced as a consistent 4x GPU pyramid (128, 64, 32, etc.), generated from the same 128×128 atlas tile with alpha-weighted area downsampling. The logical Glide dimensions/UVs and game's original mip count are unchanged. Other textures, unsupported games/editions, failed fingerprints, missing or malformed atlases pass through unchanged.
Deleting `rayman2/rayman2fix/textures/logo.rgba` restores the original logo with no
INI change. The diagnostic `RUNTIME-GLIDE: INTRO-LOGO-HD` in
`Rayman2Fix.log` reports loading and matching at most 18 sources.

The plugin is disabled unless the user supplies the separate atlas.
**This is an experimental draft pending a short Wine/Windows visual
test**, including seams, title placement and fade transitions. The
conversion deliberately maps the reference artwork to the exact
historic atlas bounds; it is not promised to be pixel-identical to
the retail title's low-res palette.

## Music in the Escape pause menu

Set `[Features] MusicInPauseMenu=1` in
`rayman2/rayman2fix/Rayman2Fix.ini` to keep the game's native
Theme/SXD music audible during the ESC menu. With `0` (default),
Rayman 2 retains its native pause behavior. Both the synchronized Theme
and the early pending-Theme handoff use this one option; `Log=0` does
not disable either route. The guarded early path requires original
script-sourced paired pause callbacks, the real ESC actor, unique Theme
and SXD resource provenance, original sound source agreement, and a
coherent active paused APMMX voice. It forwards the original native
pause operations, never changes script pause depth, and revalidates
resource ownership before compensating driver calls.

The owner validated early-pause audio continuity and subsequent return
into gameplay in multiple Wine sessions before PR #10 was merged.
Native Stop, focus and stream-change quarantine remain fail-closed.
The physical lifetime of a reused ADPCM voice cannot be proven from
source snapshots alone; avoid broadening the source gates without
independent runtime tests. The former `EarlyThemeMusicExperimental`
setting is obsolete and removed automatically during INI migration.

The separately labelled GitHub Actions artifact
`Rayman2Fix-Windows-x86-audio-trace` has a **research-only** DLL with
`RAYMAN2FIX_AUDIO_TRACE=1`; the normal
`Rayman2Fix-Windows-x86-bootstrap` build has **no audio tracer**.
On the one SHA-256-approved original CD executable, and only when
`[Debug] Log=1`, this tracer observes up to 16 calls per native
audio channel (14 independently bounded channels; 64 for the
common Sxd pause/resume channels so multiple menus fit), including Sxd
pause/resume/stop, Sxd transitions, sound volume, MIDI,
MIDI transitions and CD audio.
The 1999 PC CD assembly additionally confirms a 64-record global
sound table and one global audio pause-depth counter. Research-only
log lines now include the matching `sound_slot` number (0–63, when
available), native `sound_transport` (SXD=1 / MIDI=2 when readable),
transition and active flags, and `global_pause_depth`. Additional
`RUNTIME-AUDIO-STATE` lines record actual changes of the native
pause-depth counter every ~500 ms, including when no audio callback
fires. The `sound_matches` field reports when several game sound
records share the same driver handle; a non-unique `sound_slot`
must **not** be assumed to identify an individual song.
The first native source-classification test returned
`audio_origin=unknown` for every observed call. The updated
research build widens its stack read from 96 to at most 1,024
32-bit words (4 KiB) within the first committed region and,
on the first two events for each pause/resume channel, logs
`RUNTIME-AUDIO-CALLSTACK` with up to four *candidate* code
RVAs (no absolute pointers or data) plus `wrapper_returns`
and `paired_callers` counts. A provenance label now requires
**exactly one** adjacent pair from the approved CD's native
pause/resume wrapper and its verified outer script/Windows
caller; solitary stale return addresses never qualify.
Candidate RVAs by themselves are diagnostic evidence,
not validated return frames.
New `audio_origin` diagnostics attempt to distinguish actual
`game-script-pause` / `game-script-resume` call paths from
`windows-activation-pause` / `windows-activation-resume`
using the original CD's verified code-return addresses. If the
bounded read-only call-stack snapshot cannot classify safely,
it reports `unknown` or `ambiguous`; a pressed Esc key
alone must never authorize music to continue.
These are read-only observations, **not proof of a music stream**, and no
raw memory address, sound resource filename or pointer is logged.
The global audio pause routine is invoked both by the window
activation handler and by a native game script, so suppressing its
calls based on a key or focus alone would be unsafe. The verified original driver function is always called with
the same argument; no sound is silenced, played, skipped, replaced,
or looped. Installation of those diagnostic-only hooks fails closed
when driver functions or game-memory protections are unexpected.
The research build is **not a stable release**.

For a reproducible Wine test, play until music is audible, open
the Escape menu for a few seconds, return to the level,
then Alt-Tab away and back (to contrast focus-based audio pauses).
Exit the game and provide **both** `Rayman2Fix_installer.log` and
`Rayman2Fix.log` from beside `Rayman2Fix.exe`. The lines
`RUNTIME-AUDIO-TRACE` identify exactly which native music/sound
pause channel is invoked and whether the game had focus at
that moment; a key-down flag is only supporting evidence, not
a menu-state detector. F1 is out of scope for this improvement.
Until the exact music-only record is verified, do not enable
an audio bypass. See issue #4.

# Rayman2Fix

**A new, independent compatibility project for Rayman 2: The Great Escape (PC CD editions).**

Rayman2Fix is being developed **from scratch**. It is **not a fork** of
Ray2Fix and does not contain code copied from that project.

> **Development status: first real game-launch experiment.** The installer
> validates original CD assets and can launch the single known unprotected
> Rayman 2 x86 executable, **only after** validating the original backup,
> the patch receipt, and GameFrame's Glide DLL. Actual playability on
> Windows/Wine/Proton is still being tested; no broad game compatibility
> is claimed.

## Release diagnostics and research-only probes

Ordinary builds keep `[Debug] Log=1` for concise renderer/HUD/FOV,
installation and game errors, with separate
`Rayman2Fix_installer.log` and `Rayman2Fix.log`.
The exhaustive reverse-engineering probes for the original `Learn_10`
director and native superimposed-script dispatcher, plus frequent detailed
HUD snapshots, are disabled in release builds. They previously observed
the engine on the render thread and are not needed by the validated
widescreen or 4:3 fixes. For a local research build only, configure
CMake with `-DRAYMAN2FIX_RESEARCH_PROBES=ON`; this is a **build-time**
switch, not an INI setting. The unsafe `Learn_10` cinematic skip remains
disabled in both modes. The real `jail_20` opt-in skip is unaffected.

## Second introduction skip (opt-in, experimental)

Set `[Features] SkipCinematics=1` in `Rayman2Fix.ini` to enable the
existing second-introduction skip. It is disabled by default.

When enabled, on the approved retail game executable press **Space**,
**Enter** or **Escape** during the **second** opening cinematic
(`jail_20`) to request
the native transition to `learn_10`. The first cinematic (`jail_10`),
the menu and later cutscenes are not affected. Holding Enter while entering
the cutscene will not trigger a skip until the key is released and pressed again.
The game uses its own deferred level-change request, with no save operation;
full in-game transition still needs validation under Windows and Wine.
`RUNTIME-INTRO:` lines in the one-file log identify activation and requests.

## Learn_10 opening cinematic (skip disabled after visual regression)

The first playable level's original cutscene is **not skippable with
Rayman2Fix at present**. The experimental jump directly from the
director's native behaviour 3 to behaviour 1 was found unsafe during a
Wine gameplay test: story bit 1091 changed, but the graphics became
corrupted. That handover is **not** sufficient to restore the camera,
Rayman and all scene state. The previous `NOMOVIES` and stand-in
waypoint attempts were ineffective. Do not re-enable any of them without
a complete, gameplay-tested restoration path.

**Safety rollback:** The runtime never requests a native director
transition, edits the scene's story bits, moves Rayman, writes camera
matrices or changes saves to bypass this introduction. All original
`Learn_10` scripts run normally. Enter is not intercepted by this
feature.

With `[Debug] Log=1`, the runtime takes **read-only** samples at
the start, 0.25, 1, 2.5, 5, 10 and 20 seconds, then more densely
from 21 to 25.5 seconds (while the intro is still running),
and at the natural story bit 1091 handover and a second after.
Additional late checkpoints are kept at 27, 35 and 50 seconds
if the cinematic runs longer than usual. The first sample may be unavailable until Rayman is loaded.
The diagnostics capture Rayman's flags/position, camera position,
director behaviour index and stand-in position, if available.
Missed checkpoints are skipped instead of replayed in a burst when the
camera callback resumes after a gap. A bounded, read-only change detector
(maximum eight event snapshots) additionally records actual changes in
Rayman's flags and in the director's behaviour index. The callback is
only an observation opportunity, not a clock: a gap in the log is not
evidence of what happened inside the game during that gap. No game data
is written, no process addresses are logged and no new thread is made.
These observations provide a reference for a future safe skip.

**Compiled script inspection (read-only, bounded):** Once the genuine
`Learn_10` director enters behaviour 3, the runtime inspects its
compiled model behaviours 1 (exit), 3 (intro walk) and 5 (handoff),
including their schedules and up to eight rules each. Logs contain
only node indices, nesting depths and symbolic opcode numbers
(procedures, conditions, variables and local behaviour indices); game
pointers, asset bytes and executable instructions are never logged.
The parser checks node types and depths; ordinary scripts are capped at
1024 nodes. The previously truncated **behaviour 3, rule 2** is read
once with a higher limit of 8192 nodes, retaining a bounded last-96
opcode tail, last-64 procedure/meta-action/behaviour sites, and the
nested opcode context from nodes 870 through 1100 (the native
cleanup/behaviour-switch region). A scan
duration is recorded in milliseconds and raw pointer operands remain
redacted. Output remains bounded by a shared log-line budget.
During natural state snapshots, only the low byte of integer-typed
director variables 91, 47, 50 and 110 is observed (with validity/type
labels). The additional vars 50 and 110 are involved in separate branches
of the original behaviour-3 rule 2; its switch to behaviour 1 checks
var110==1. Static analysis now identifies a separate progression
counter at nodes 956–970: if var83 < (var14 - 1), the native script
increments var83; otherwise it sets var110=1 and enters its original
actor/Rayman restoration branch. This is a candidate *internal*
completion path, not a proven shortcut. The diagnostic now also
tracks var83 and var14 in read-only mode using their declared integer
widths (8/16/32-bit, signed as appropriate), and logs their values
at checkpoints and changes through the existing bounded event ring.
An automated C++ test guards the type decoding. No script values,
camera matrices or scene flags are altered. The first live trace showed var50=0→1→0 and the **low byte**
of var110=0→2→0 on four occasions, but the initial 16-event budget ran
out around 20.6s, before the actual ~25.4s exit. The probe now reads
var50 as its declared 8-bit integer and var110 as its declared 32-bit
integer, retaining the **last 48** changes in a bounded circular buffer
throughout the introduction. Only after the natural handover and a
one-second grace period does it log those events, the number omitted,
and whether any observed var110 transition entered value 1. Every
sample and event is passive; neither variable is ever written. Pointer
variables and process addresses are never logged. An event that occurs
entirely between camera callbacks may still be missed. Neither var110
nor any other script variable is used to bypass the cutscene. **It does not execute, fast-forward or
modify a script.** We will use the data to identify which native
cleanup operations a safe future skip must preserve.


**Native ending structure (observed on Wine, 10 November 2026):**
the complete gate history contained 30 events (none dropped). Director
variable 83 advanced 0→1→2→3→4→5→6 with variable 14 fixed at 7,
then the native director changed from behaviour 3 to 1 at ~25.397 s;
story bit 1091 was observed at ~25.414 s. Six 0→2→0 pulses of the
32-bit variable 110 were sampled, but no value 1 was observed. A
shorter pulse between callbacks remains possible.

Cross-referencing the numeric script IDs with the public raytools
`ACP_Ray2/src/AI/AI_Const.h` enumeration identifies native
`ChangeMyComport` (P69), `ChangeComport` (P67),
`ChangeComportReflex` (P68), `ActivateObject` (P84),
`KillPerso` (P139), and `ChangeOneCustomBit` (P174).
Near nodes 1022–1046, the original script includes actor
reactivation, behaviour/reflex transitions and clearing of Rayman's
custom bits 14 and 17. The normal ending therefore involves more than
a jump to behaviour 1.

The read-only `native-exit-shape` diagnostic now checks those exact
node/opcode landmarks against the live compiled script. It is a
**version-specific script fingerprint, not proof of safe shortcut
eligibility**. A mismatch is informative and does not change
gameplay. The previous early jump remains disabled until the original
cleanup can be exercised safely and validated visually.

The script shape check additionally verifies the **If/Then/Else nesting**
of the native exit branches and that `ChangeMyComport` targets behaviour
1 in the same verified director model. A correct opcode list with
incorrect nesting or an unrelated behaviour pointer is rejected. In the
original script, a `var83 >= (var14 - 1)` event assigns
`var110=1` within the **Else** branch, executes the engine's actor/Rayman
restoration, and is followed by the separate `var110==1` director-exit
gate. This explains why simply forcing behaviour 1 can bypass cleanup.
It does not demonstrate that manipulating `var83` is safe: the
cinematic's actor positions, camera and script phase would still differ.

**Failed counter-only experiment (10 November 2026, reverted):**
The disposable Wine test set director var83 from 0 to 6 after 1.0s;
the write was accepted but the engine stayed in behaviour 3, Rayman and
the camera remained stationary, and var50/var110 stayed at 0 with no
native progression pulses. It never reached story bit 1091 before the
user closed the game. Reaching var83=var14-1 is NOT a native completion
request. The optional environment variable and all code that wrote
var83 have been **removed**; do not re-enable this attempt.

To investigate the missing prerequisite without repeating a failed
write, the normal read-only snapshots now also report exact-width
integer scalar AI variables 16, 17, 95 and 96 (if they are declared
integer and readable). Arrays, floats, pointers and actor references
are not decoded or logged. This is diagnostic only. It may identify
the missing phase gate, but no causal interpretation is assumed.

**Native pulse/phase correlation (10 November 2026):**
The newest unmodified Wine 11.17 run again completes Learn_10 naturally
at 25.406 s and preserves all 30 gate transitions. During behaviour 3,
var16 was 1; var95 moved through 10, 2000, 4100, 1000, 1500 and
500 while var83 advanced at separate moments. Var96 remained zero at
coarse checkpoints, but a short intermediate value may have escaped
observation. The next diagnostic build records scalar var16/95/96 values
in the EXISTING native var50/110/83 gate-event ring only at the
events themselves; phase_valid marks successful readings (1=var16,
2=var95, 4=var96). These observations are read-only and do not
enable any shortcut. For var17, only the declared type is now exposed
when the value is not a scalar integer; its content remains untouched.

**Current targeted script investigation (10 November 2026):**
`NOMOVIES` requires a playable in-game pause menu, but the user's
Wine test confirms that ESC cannot open Pause during first-visit
`Learn_10`. The game therefore cannot accept the cheat there; this
does not prove that its native skip handler is ineffective. The
[PC fast-reset speedrun instructions](https://www.speedrun.com/r2/guides/xin1n)
also explicitly require watching the silver-Lum cinematic before
preparing their reusable save, so they are NOT a no-save workaround.

The earlier read-only trace identified exactly six `v96=110` pulses,
coincident with six native `var83` increments, while normal cleanup
resumes Rayman at 25.4 s. An additional bounded *read-only* script
locator now looks specifically for a native comparison of variable
96 with integer literal 110, with verified expression nesting.
Only if one is present does it print a short excerpt around its
first occurrence; if none is found it says so. The excerpt contains
sanitized opcodes and integer variable IDs, not pointers or asset
data. **Neither a matched predicate nor the value 110 authorizes a
skip.** The first-visit `Learn_10` cinematic remains untouched.

**Result of the first phase-110 probe (log 20261110-145401):**
the approved compiled script has exactly one matched `V96 == I110`
predicate (node 317, native Equal condition). Its Then branch
(nodes 320–326) assigns `var110 = 2` and `var50 = 1`.
This matches all six naturally observed gate pulses. **Code 110
means completion of an intermediate stage, not completion of the
cinematic.** The final exit/restore logic belongs to the separate
native branch at nodes ~926–1055. `var83` must not be forged to 6,
and `v96=110` must not be written blindly.

For the next one-time diagnostic, the validated behaviour-3 rule-2
probe additionally reports sanitized nodes in the previously unread
ranges 134–383, 384–639 and 640–869, alongside the already bounded
prefix and native exit context. These are three disjoint windows of
at most 256 nodes each. The full game remains unmodified, and no
pointer-type operands or copyrighted game assets are logged. This
should allow static reconstruction of what lies BETWEEN stage
completions and the engine's real exit, without another speculative
memory write.

**Full middle-branch trace (10 November 2026, log 150453):**
the behaviour-3 rule-2 script reveals an *independent, earlier*
condition `v96 == 104` near nodes 157–161. Its Then branch assigns
`var110 = 1` (nodes 172–174), deactivates the temporary script actor,
reactivates native actors, switches their behaviour and reflex, clears
Rayman's custom bits 14 and 17, and restores his animation/control
flag. This resembles the later terminal restoration at nodes
~966–1055, but is NOT identical in every instruction.

By contrast, `v96 == 110` near node 317 sets `var110 = 2` and
`var50 = 1`, signalling an ordinary stage progression pulse;
this is not an exit instruction. An exact nesting and cleanup-shape
probe now checks the `v96 == 104` branch separately and reports
`native-early-exit-shape`, without enabling it. The native rule also
checks `var110 == 1` at nodes 926–939 and switches the director
to behaviour 1, which suggests that the early `104` branch is a
plausible native completion mechanism.

**Original NOMOVIES gate discovered (10 November 2026):**
[OpenSpaceToolbox's progress-array flag map](https://github.com/raytools/OpenSpaceToolbox/blob/master/OpenSpaceToolbox/ViewModels/ProgressArrayWindowViewModel.cs)
labels bit **1188** as `NoMovies cheat`.
[ACP_Ray2's opcode enumeration](https://github.com/raytools/ACP_Ray2/blob/master/src/AI/AI_Const.h)
defines K1 as `IfNot` and F248 as `GetBooleanInArray`.
The actual Learn_10 behaviour-3 rule-1 starts with
`IfNot(GetBooleanInArray(actor.V42, 1188)) Then ...`.

This establishes a real director branch controlled by the game's
NOMOVIES bit. A new, *read-only* script shape check verifies these
first seven nodes and their nesting, reads flag 1188 at the start
and natural end of the cinematic, and prints bounded rule-1 nodes
100–265 to inspect the other branch. The existing story-array locator
already supports bit 1188; there is no new pointer traversal.

**Results of the first NOMOVIES test (log 20261110-152709):**
the real script gate matches perfectly, and the original cheat
bit 1188 is **0 both at level entry and at natural handover**.
The game plays its six ordinary stage transitions and returns control
normally at 25.413 seconds, with all 30 events retained. The exact
NOMOVIES alternate path starts with a **top-level Else at node 100**
followed by `IfNot(V119)` at nodes 101–103 and, later, a
`ChangeMyComport`-style native procedure at node 263.
This shows an alternate director path, but does not by itself
demonstrate an immediate movie skip. Independent Rayman 2 PC cheat
documentation explains that NOMOVIES **enables skipping with Enter**;
it is not an automatic fast-forward.

The same log exposed a **false-negative in our native early-exit
diagnostic**, not in the game: its `actors=0` was caused by expecting
two P318/P312 instructions at depth 10 when they are actually at
depth **11** (nodes 237 and 241). The updated read-only matcher now
uses the observed depths, with regression tests, and separately
fingerprints the NOMOVIES alternate Else arm.

**New native input clue:** the original AI enum identifies condition
45 as `JustPressedBut` and condition 48 as `IsTimeElapsed`.
Both branches of the NOMOVIES-controlled director rule 1 contain
`JustPressedBut`: at node 8 (cheat bit false) and node 106 (cheat
bit true, with a 1000-time-unit guard using `IsTimeElapsed`).
The old probe omitted the operands to these conditions because their
node type, `AI_E_ti_Button` (14), was not selected. The new
read-only diagnostic includes such nodes in sanitized script excerpts.
Only small IDs 0–255 are printed; pointer-like values are hidden.
This will reveal which original engine input each branch uses and
allow comparison to PC Enter without guessing a Windows virtual-key
mapping. It does NOT synthesize a key or toggle the cheat bit.

**Native button operands observed (Wine log 20261110-154233):**
both `JustPressedBut` conditions now have explicit type-14
`button` operands (nodes 9 and 107), but their raw 32-bit values
exceed 255 and are intentionally REDACTED. They must **not** be
presented as small button IDs or Windows VK codes. The same log
confirms all three exact read-only script fingerprints, including
the repaired early-exit actor cleanup. The original first-visit
scene still completes at ~25.416 s, with NOMOVIES bit 1188 OFF.

To resolve whether those two branches refer to the **same native
button operand**, the new static script check compares their full
values solely inside the game's process, logs only an equality
boolean and opaque-vs-small status, and never prints/reads a pointer
target. A one-time read-only snapshot also records the integer types
and values (when valid) of V11, V49, V93 and V119, the extra
conditions used by the native first-visit and cheat-enabled branches.
Neither observation maps a button to Enter or forces any game state.

**Unresolved:** switching the bit after Learn_10 has initialized
may be too late to alter native cutscene flow; setting it earlier
could affect a progress array that is later serialized in save data.
Therefore this work MUST NOT set the flag, even temporarily,
until its persistence and native cleanup effects can be tested
without touching the user's saves.

**Outstanding unknown:** `v96` is normally assigned from the
director's compiled `F158` function (cross-reference name
`DisplayText` in ACP_Ray2), which may recompute the signal every
game tick. Therefore forcibly writing `v96=104` has not been
validated as a safe or effective user shortcut. No script designer
variable is changed, no skip is enabled and no saving logic is
modified. Further experimentation must prove how to request that
signal coherently rather than jumping past cleanup.

The separately validated native `Jail_20 -> Learn_10` level change
remains enabled, as do GameFrame loading-image colours and 4:3 framing,
Hor+ 3D and the 4:3 HUD. This rollback does not merge the PR.


## Experimental loading-image colour diagnostics

The currently approved 3D/2D rendering is unchanged. While `[Debug] Log=1`,
the pinned GameFrame Glide wrapper emits a **small capped number** of
`RUNTIME-GLIDE:` lines to the existing `Rayman2Fix.log` (the dedicated game/runtime log):
framebuffer pixel mode and colour format, large CPU framebuffer transfers,
and early texture/palette uploads. These diagnostics let us distinguish a
16-bit R/B ordering error in LFB images from palette/texture upload errors.
No pixels are changed and there is no user-visible configuration setting.
This is investigative pending in-game comparison of the loading artwork.

## Ubisoft logo and other native superimposed actor matrices in widescreen

The approved retail CD engine writes one calculated value into **both**
the horizontal X and vertical Z columns of a superimposed actor
transformation matrix (at `0x471230` and `0x4718D6`). An older
widescreen fix reduced that common value by `g_hud_scale`, unintentionally
squeezing the opening Ubisoft logo/arc **vertically** along with its
horizontal centering. A distinct text-only correction had already fixed
« PRÉSENTE » while leaving the logo distorted.

The new patch keeps the existing X coefficient but, **only** in those
two SHA-locked native store sequences, restores Z to its original size
by multiplying by `1 / g_hud_scale`. It uses an x86 trampoline with
validated original bytes, preserves the original x87 stack depth and
registers, validates and installs both sites atomically with the
pre-existing superimposed source patch, and fails closed on unknown EXEs.
No global 3D transform, 2D/HUD callback, text coefficient or cinematic
viewport correction is changed. Unit tests cover native 4:3, 16:9,
21:9 and both x86 gateway encodings. Visual confirmation of the logo
and other superimposed actors remains necessary before merge; the
existing `RUNTIME-OVERLAY-ACTOR` log reports the applied coefficient.

## Cinematic titles: preserve 4:3 text proportions in widescreen

The approved retail game builds native `TextSuperImposed` titles
through the same screen-world UI path as its other 4:3 overlays.
The binary independently multiplies the native viewport width by
`1/640` and its height by `1/480` (CD RVAs `0x75BEF` and
`0x75BF7`). The widescreen runtime must apply its horizontal
centering factor to the width term **only**: applying it to the height
term made the opening credits and dialogue titles about 25% too short
on a 16:9 monitor. The fix restores the native height term while
retaining the existing 4:3 horizontal centering and world Hor+ FOV.
The correction requires no new INI setting and applies to all native
`TextSuperImposed` routes, not specific cutscene scripts.
It has regression tests; in-game visual verification is required
before merge.

## Original loading artwork: RGB565 colors and centered 4:3

The loading picture is a CPU-written 1024x768 RGB565 frame while Glide
reports ABGR. On this specific complete-frame upload, Rayman2Fix restores
the RGB565 red/blue interpretation, preserving all regular 3D/texture
formats and HUD rendering. The same image is projected to a centered 4:3
canvas, with native-height scaling and automatic black pillarboxes at
widescreen resolutions. Other LFB sizes/formats, 3D, FOV, 60 FPS, menus,
and gameplay remain untouched. `RUNTIME-GLIDE` logs one activation line
when debugging is enabled. The visual result must still be checked on
Wine and Windows before the PR is merged.

## Automatic launcher workflow

Run **`Rayman2Fix.exe` without arguments**. The obsolete `--init` command
has been removed.

- When `rayman2/rayman2.exe` already exists, the launcher checks the
  local CD installation receipt (original filenames, sizes and SHA-256) and audio assets
  without needing the source CD again. Missing or changed files are reported,
  not silently overwritten.
- Otherwise, it searches for **exactly one** adjacent `.iso` or `.cue` file.
  It refuses absent or ambiguous media, and requires an identifiable
  `RAYMAN2` volume with `rayman2.exe`, `dll/snd_cpa_mmx.ini`, game data,
  and no unrecognized invalid file extents.
- It extracts the supported CD files to a sibling staging directory, omitting
  the known installer-only material and dummy entries, preserving long Joliet
  names in lowercase and writable. It creates `dll/snd_cpa.ini` from
  `dll/snd_cpa_mmx.ini`, writes a local **SHA-256** CD file receipt,
  then publishes into `rayman2/` with fixes in `rayman2/rayman2fix/` transactionally. Existing
  unrelated files are preserved; an incomplete/unrecognized installation is
  never silently overwritten.
- The launcher embeds and deploys its own x86 `Rayman2Fix.dll`, creating
  the `rayman2/rayman2fix/` directory and minimal INI automatically. If the
  runtime DLL changes, its **immediately previous version** is kept as
  `Rayman2Fix.dll.bak`; the older backup is replaced, never accumulated
  as numbered history.

**Important:** a SHA-256 receipt generated from the same CD verifies
subsequent file integrity, but does not authenticate the retail edition.
**Game launch is now attempted only** for the approved protected/unprotected
executable SHA-256 pair, with `rayman2.exe.bak` and patch receipt verified.
An unsupported EXE or unverified GameFrame library still blocks launch.
No-CD/gameplay compatibility is not yet established by tests.
SDL3/gamepad integration is deferred to a later phase.

The single EXE now deploys an exact-edition-gated x86 `Rayman2Fix.dll`:
it can adjust the game's camera horizontal field of view (Hor+) **in
memory** for widescreen output, after checking the exact 32-bit executable
signature. It refuses all other game versions. GameFrame's Apache-2.0
OpenGL Glide 2 wrapper is embedded as `glide2x.dll`.
Window styling and FOV are implemented for the owner's first Wine/Windows
test; gameplay appearance, HUD alignment, video behavior and Alt+Tab are
**not yet validated**. SDL3/gamepad integration remains deferred.

## Installed-file layout

The workflow archive now contains **one executable only**:

```text
Rayman2Fix.exe
```

On first execution it generates (and subsequently preserves):

```text
Rayman2Fix.exe
Rayman2Fix_installer.log       (installer messages, when Log=1)
Rayman2Fix.log                 (runtime/GameFrame diagnostics, beside launcher)
rayman2/
    rayman2.exe                (game executable from the CD)
    rayman2.exe.bak            (protected original, if patched)
    rayman2.exe.manifest       (required game-facing DPI manifest)
    winmm.dll                  (game-facing audio bridge, if present)
    ubi.ini                    (managed game configuration)
    data/                      (retail game resources)
    dll/                       (retail audio configuration)
    rayman2fix/
        .rayman2fix-cd-files.tsv  (local CD asset receipt)
        .rayman2fix-exe-patch.tsv (executable-patch receipt)
        Rayman2Fix.ini         (user mod configuration)
        Rayman2Fix.dll         (x86 injected mod runtime)
        Rayman2Fix.dll.bak     (single runtime backup)
        glide2x.dll            (pinned GameFrame renderer)
        GameFrame-LICENSE-APACHE.txt
        textures/
            logo.rgba         (optional externally provided HD logo atlas)
```

The correct Glide 2 filename is **`glide2x.dll`**, not `glide2.dll`.
The launcher embeds the **pinned GameFrame x86 Glide DLL**, its Apache 2.0
license, and the project's own inert runtime DLL. It generates the INI and
log itself. There is still **only one file** in the Windows workflow artifact:
`Rayman2Fix.exe`. SDL3 and copyrighted game files are never bundled.

**`Rayman2Fix.ini` is the only user-editable configuration file.** Newly
generated installations contain:

```ini
[Debug]
Log=1

[Graphics]
Resolution=Desktop
; FPSLimit=0 disables the cap; accepted range: 15..240
FPSLimit=60
; Auto = monitor aspect ratio; 4:3 = centered classic picture
AspectRatio=Auto

[Features]
; 1 = allow Space/Enter/Esc to skip jail_20 only
; Learn_10 skip remains disabled (unsafe)
SkipCinematics=0
; 1 = hide native rectangles behind in-game dialogue
RemoveDialogueBackgrounds=0
; 1 = experimental removal of game-drawn cinematic letterbox bars
RemoveCinematicBars=0
; 1 = continue native Theme music during ESC (including early handoff)
MusicInPauseMenu=0
```

`Resolution=Desktop` automatically uses the primary desktop resolution on
Windows/Wine. You can alternatively select an explicit resolution such as
`Resolution=1280x720` or `Resolution=1920x1080`.
The format is validated and invalid values are refused, not silently ignored.
`FPSLimit=60` caps the **game's rendered frame rate** by pacing its
`grBufferSwap` calls; set `FPSLimit=30`, `120`, or another rate
between 15 and 240 if required. `FPSLimit=0` disables the cap.
`AspectRatio=4:3` in `[Graphics]` selects a centered, exact 4:3 output
image with black sidebars, while `AspectRatio=Auto` (default) retains
widescreen Hor+ 3D and centered 4:3 HUD. At 1920×1080 the game
renders at 1440×1080 within the unchanged borderless 1920×1080
monitor window. The pinned GameFrame wrapper already centers the
picture and clears unused sides black in its presentation path:
no additional rectangles, exclusive fullscreen, monitor mode change,
or per-level rules are required. Original 4:3 FOV is used in 4:3 mode,
and the native UI is not squeezed.

`SkipCinematics=1` in `[Features]` enables the existing
**deliberate keypress** skip of second introduction `jail_20` only.
It does **not** skip the first cinematic `jail_10`, the unsafe
`learn_10` sequence, or later game cutscenes. With the default
`SkipCinematics=0`, even the second intro behaves normally.

`RemoveDialogueBackgrounds=1` in `[Features]` removes the wide,
black native GLI frame rectangles behind in-level dialogue. This path
has been confirmed by the owner's Wine runtime log. It does not hide
the dialogue text or unrelated UI, and defaults to `0`.

`RemoveCinematicBars=1` independently enables an **experimental**
filter for top/bottom cinematic strips drawn by the same native
`GLI_vDisplayFrame` source. **This has not yet been demonstrated to
remove the black bars visible in cutscenes**, which may use a separate
video or image path. It does not change the sidebars generated by
GameFrame's `AspectRatio=4:3`, nor full-screen fades. It defaults to `0`.
The experimental `RemoveCinematicBars=1` implementation now
tries to **expand native GLI viewport bounds at their source**, before
the engine draws the world/image or submits black masks. This replaces
the previous strategy of merely suppressing `GLI_DRV_vAddBlackPolygon`
for top/bottom strips. The user's Wine tests proved that the older
strategy made the black bands BLUE in cinematics 1 and 3, while black
bands in another cinematic remained black: hiding masks is not sufficient.
The black polygon callback now only reports native rectangle geometry;
it does **not** suppress any masks.

The source-side hook uses the SHA-locked retail executable at native
VA `0x420660`, whose verified prologue is patched with a trampoline.
It recognizes only 1024×768 internal full-width viewports with moderate
top/bottom insets expressed as thousandths, temporarily passes 0..1000
to the original viewport calculator, and restores the source parameters
after the original recalculates its cached screen limits. Unknown
viewports, side panels and full-screen fades are preserved. The vertical
cinematic viewport fill is available in both Auto and centered 4:3 output
modes; GameFrame's 4:3 sidebars are not removed or stretched. A
`RUNTIME-CINEMATIC-VIEWPORT` log entry confirms that the native viewport
was actually expanded, whereas `RemoveCinematicBars=1` alone only confirms
the option was read. Bounded diagnostic entries record both the
original requested inset and derived full-screen bounds.

**This is still a test prototype, not a visual fix confirmed on game
hardware.** It may not affect scenes using a different renderer or
letterbox produced by the image itself, and it must be visually checked
before merging. The default remains `RemoveCinematicBars=0`.

An existing `RemoveBlackFrames=1` setting is migrated once to
`RemoveDialogueBackgrounds=1` and `RemoveCinematicBars=1`; any
already-explicit new setting wins over the legacy value. The obsolete
combined key is removed. Existing INIs retain unrelated settings,
comments, BOM and line-ending format. Both new options are independent
of `SkipCinematics`, aspect ratio, and one another.

For troubleshooting, `[Debug] Log=1` also prints
`RUNTIME-CONFIG: build=... RemoveDialogueBackgrounds=0/1 RemoveCinematicBars=0/1`
directly in `Rayman2Fix.log`. If `RemoveCinematicBars=1` is requested
but the runtime reports `0`, inspect the installed `Rayman2Fix.ini` or
the launcher/version being used. It must print the effective value before
any cinematic filtering starts.

With `RemoveCinematicBars=1` and `[Debug] Log=1`, the experimental
build also records **read-only, bounded** diagnostics in the existing
`Rayman2Fix.log`: `RUNTIME-CINEMATIC-SOURCE` for broad GLI edge-shaped
quads (including unverified 2D routes), and `RUNTIME-GLIDE:
CINEMATIC-CLIP`, `CINEMATIC-PASSTHRU` and
`CINEMATIC-CLIP-SUMMARY` for GameFrame's clip-window and 2D passthrough
transitions. These events do not identify a black fill or movie by
themselves. They help distinguish a missed native frame from a clipped
viewport or a separate video path. Only previously classified,
SHA-locked native GLI frame quads can be suppressed; the diagnostic
does not erase new geometry or modify Glide state.

These are maximum frame rates, not guaranteed measured FPS: actual performance
may be lower. Very high limits may affect classic-game timing and require
in-game testing; 60 is the default setting to validate on real hardware.
`Log=0` suppresses both installer and game/runtime diagnostics and removes
both stale log files. With `Log=1`, the installer log is reset at each
launcher invocation; the runtime log is reset only when a verified game
is about to launch. Launcher-only commands or a missing-CD error do not
erase the previous runtime trace. Both logs are beside `Rayman2Fix.exe`,
never rotate into `.bak` history, and share the single INI switch.

Rayman 2 still needs its legacy `ubi.ini` in the game directory, but **the
user must not maintain it**. The launcher preserves its unrelated CD settings
(e.g. `Language`) or creates it when absent, and automatically synchronizes
the required Glide settings (`GLI_DllFile=GliVd1`,
`GLI_Dll=Glide2` and the supported internal
`GLI_Mode=1 - 1024 x 768 x 16`) before each launch.
**This internal 4:3 mode is not the desktop/output resolution.**
The launcher derives native physical pixels from Windows/Wine's
primary monitor and DPI APIs, and supplies GameFrame's `GLIDE_SIZE`
environment variable (e.g. `1920x1080`) to the child game process.
It also generates the `rayman2.exe.manifest` PMv2 DPI compatibility
file without changing the SHA-256-approved game EXE.

During gameplay Rayman2Fix supervises the actual game window, requests
borderless fullscreen at the monitor's physical coordinates, and never
enables exclusive fullscreen, topmost or changes the desktop resolution.
Only the approved game executable may load the in-memory Hor+ camera
correction. The projection angle is calculated for the output aspect,
while the FOV reference stays 4:3. Glide, 16-bit internal mode,
borderless window and Hor+ are mandatory fixes, not redundant user options.
`ubi.ini` is mutable, so it is deliberately excluded from the immutable
CD SHA-256 receipt. Old receipts with this entry are migrated without the CD.

The game-facing configuration and Hor+ projection calculation are
unit-tested, but **actual Rayman 2 widescreen visuals and HUD still require
Wine/Windows testing**. Unknown or modified camera function bytes are
refused instead of patched.
No usernames, private home paths, serials or network identifiers are logged.

## Available now (PR #1)

The initial Windows x86 executable can inspect and safely extract **ISO 9660**
images (2048-byte sectors) and **CUE/BIN with one MODE1/2352 or MODE1/2048 data
track**. ISO 9660 boot descriptors are handled. Audio tracks are not extracted.

**Filename handling:** the inspector searches for a **Joliet supplementary
volume descriptor** and uses its real UCS-2 long filenames instead of the
8.3 names in the ISO 9660 primary tree. All extracted files **and directory
components are lowercased**, including accented Unicode letters, and are
marked writable (no Windows read-only attribute). The original CD has a single DLL directory, extracted as `dll/`.
The historical Lutris `DLL`/`Dll` merge belonged to the Ray2Fix/dgVoodoo2
installation and is **not required** here. Unexpected collisions after
lowercasing (files or directories) are refused rather than silently merged.

If the CD has **no Joliet tree**, the inspector clearly reports that it
only has primary ISO 9660 names and that original long names may be
unavailable. No invented name mapping is applied to necessary game files.

For the `RAYMAN2` CD, these **ten exact root-level paths** are
excluded from installation if present:

- `00000001.tmp`, `0000040c.016`, `0000040c.256` (SafeDisc-era auxiliaries).
- `secdrv.sys`, `rayman2.icd` (original protection-related components).
- `clcd32.dll`, `clokspl.exe`, `drvmgt.dll` (previously approved
  auxiliary exclusions).
- `loader.ini`, `dplayerx.dll` (additional owner-approved optional
  root-level files, not needed by the Rayman2Fix install/verify contract).

An earlier inspected retail CD contained `directx/dplayx.dll` under the
already-excluded DirectX installer directory, but no root `dplayerx.dll`.
The two names are different; the explicit root `dplayerx.dll` rule is
retained for editions or legacy installations where that file appears.
The unrelated root `dxplayerx.dll` is **not** excluded.

Exclusions match only these exact root files on the `RAYMAN2` volume:
nested namesakes, such as `data/loader.ini` and `data/dplayerx.dll`,
and other extensions are preserved. Existing installations are migrated
without needing the CD again: the launcher tolerates missing excluded
files during verification, then removes any obsolete root copies and
their entries from the local SHA-256 receipt. Unrelated game assets
and settings remain verified and preserved.

On a disc whose volume label is exactly `RAYMAN2`, we recognize **23 exact
`Data/World/Levels/*.DAT` paths** discarded by the author's original
[Lutris CD installation script](https://lutris.net/games/install/39141/view).
Those known exclusions are skipped during normal installation, including
unreadable or fictitiously large CD extents. Other unrecognized out-of-range
files still block installation safely, with a `SUSPECT` diagnostic in the
installer log. The log distinguishes declared total bytes, physical image
size, selected extraction bytes and each exclusion; there is no blanket
skip-by-size.

### Installer commands

```text
Rayman2Fix.exe                        automatic install / verify / launch
Rayman2Fix.exe --install-only         install / verify / update recognized fixes; no game launch
Rayman2Fix.exe --launch-no-verify     attempt direct game launch, no checks or repair
Rayman2Fix.exe --verify              verify local CD receipt, sizes and SHA-256
Rayman2Fix.exe --inspect image.iso    create Rayman2Fix_inspect.log
Rayman2Fix.exe --inspect image.cue [track.bin|track.img]
Rayman2Fix.exe --version             display version
Rayman2Fix.exe --help                display CLI help (also -h)
Rayman2Fix.exe --verbose             print detailed diagnostics to console
```

`--verbose` is an optional modifier, e.g. `Rayman2Fix.exe --install-only --verbose`;
it may appear before or after the primary command. If no command is given,
the launcher detects an existing installation or exactly one neighboring
`.iso`/`.cue` image, safely stages files under `rayman2/`, verifies
them, and attempts to launch the approved game. `--install-only` uses
the **same installation and verification logic** but finishes without
starting the game. `--inspect` is the one supported *read-only* disc
diagnostic: use it with an ISO/CUE (optionally a separate BIN/IMG track)
to generate `Rayman2Fix_inspect.log` beside the launcher for reporting
an unsupported edition. It does not install, run, or modify game files.
The old `--extract` command remains removed; no duplicate
`DiscContent/` extraction path is exposed. Images passed directly
without `--inspect` are refused.

The installer displays progress with ordinary flushed text lines, so it
remains readable in Windows cmd/PowerShell, Wine and redirected output.
It reports copied files/bytes, exclusions, audio and graphics settings,
executable patch and SHA-256 results. The ordinary `--verify` command checks
installed file presence, sizes and SHA-256 against a **local receipt**,
not a trusted retail-edition allowlist.

`--launch-no-verify` is for intentional troubleshooting: it does **not**
verify executable presence, hashes, backup, CD assets or GameFrame and it
does not extract, patch, repair or provision game DLLs. It attempts to start
`rayman2/rayman2.exe` using the existing display settings (desktop
defaults if the INI is absent); Win32 reports the error if the file is
missing. It can therefore attempt an unsupported executable and **does not**
imply that the installation was authenticated.

GameFrame integration means the correct open-source graphics DLL is installed
and SHA-256-verified; it does **not** prove that Rayman 2 can render with it.
The launcher now performs an initial game-startup experiment with strict
original/unprotected EXE SHA-256 and size checks. It starts `rayman2.exe`
through Win32 `CreateProcessW` with `rayman2/` as the working directory,
waits for the process to exit, and logs the full exit code and duration in
`Rayman2Fix_installer.log`; the injected DLL and GameFrame diagnostics
go to the separate `Rayman2Fix.log`. In the default verified path, unknown editions are never executed; the explicitly requested `--launch-no-verify` mode bypasses these safeguards for troubleshooting.
Testing is required on both Windows 11 and Wine/Proton before claiming support.

## Original executable and patch backup

The user **already supplied both x86 executables** in the conversation
(November 9, 2026). Both have identical PE timestamps and entry-point
RVA, and their source/target SHA-256 identities are:

- **Protected original**: `rayman2(1).exe`, **676,352 bytes**,
  `7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`.
- **Unprotected target**: `Rayman2.exe`, **1,468,928 bytes**,
  `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`.

A **46,841-byte R2FXP01 delta** was generated and independently
round-trip-checked against these two user-owned inputs. Approximately
89% of the target bytes are copied from the protected EXE; the remaining
bytes are stored in small compressed LZ4 payloads (42,039 encoded bytes).
The underlying game executables are **never distributed or committed**.

- `tools/build_exe_patch.py` now supports pure-Python LZ4 compression
  of unmatched spans, and refuses large encoded target payloads.
- The cross-platform C++ decoder supports the compressed `Z` operation
  with strict bounds, and validates the exact SHA-256 and sizes of the
  **approved original and target** before replacing either executable.
- **Preferred local workflow (no Python / no private delta needed):**
  place the user's own **decrypted `Rayman2.exe`** beside
  `Rayman2Fix.exe` (on Windows, `rayman2.exe` is equivalent).
  Both the protected installed executable and the local decrypted
  candidate must match the approved size and SHA-256 values above.
  On the next normal startup or `--install-only`, the installer copies
  the decrypted EXE to `rayman2/rayman2.exe`, backs up the original as
  `rayman2/rayman2.exe.bak`, and writes the patch receipt under
  `rayman2/rayman2fix/`. The supplied file next to the launcher remains
  untouched. An unsupported candidate aborts safely with a diagnostic.
- Alternatively, place a generated `Rayman2Fix-executable.r2patch`
  beside `Rayman2Fix.exe`. On a different edition, the source stays
  unchanged.
- The final one-EXE packaging remains available: local Windows builds
  may pass `-DRAYMAN2FIX_EXE_PATCH=<absolute delta path>` to embed the
  approved binary delta as RCDATA resource `#1102`. The standard public
  GitHub CI archive **does not contain the private delta or copyrighted
  decrypted game EXE**. It can nevertheless use the user-supplied local
  `Rayman2.exe` without any compiler or Python dependency.
- On an exactly recognized original SHA-256, the installer writes and
  verifies the new executable to a temporary file, **renames the untouched
  original to `rayman2.exe.bak`**, publishes the new `rayman2.exe`, and
  records both SHA-256 hashes in `.rayman2fix-exe-patch.tsv`.
- A subsequent `--verify` checks the CD's original hash against the
  **backup**, and the patch target hash against the installed EXE. An existing
  backup is *never* silently overwritten; a failed replacement rolls back.

**The EXE patch is already generated and verified**, but is not
present in the public CI build. When the owner supplies the approved
decrypted EXE or adds the matching local delta, Rayman2Fix creates
`rayman2.exe.bak` only after reconstructing and SHA-256-checking
the unprotected executable. Once the approved EXE and pinned GameFrame
are verified, the launcher **now attempts to start Rayman 2**. Actual
gameplay, CD-independent runtime behavior and Glide rendering remain to
be validated; SDL3 is out of scope for this stage.

## Build

The source uses C++17 and CMake. On Windows, build in 32-bit mode:

```powershell
cmake -S . -B build -A Win32 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CI checks out a fixed GameFrame upstream revision
(`f195a074a63cc150673c38becdec696163b0b3d1`). Before compiling the
32-bit Rust/MSVC `glide2x.dll`, Rayman2Fix applies a small, reproducible
`tools/gameframe/apply_fps_limit.py` adaptation **only in the temporary CI
checkout**. It adds a 15..240 FPS cap (or 0 for unlimited) to
`grBufferSwap` on the game's calling thread using a monotonic clock and
`sleep`, without adding a second DLL or modifying upstream. The patch fails
closed if upstream changes. The build audits all 156 canonical Glide exports.
CMake embeds it in `Rayman2Fix.exe` together with its Apache license,
and the executable verifies the SHA-256 of the installed DLL on every start.
If a different `glide2x.dll` exists, the launcher saves it as
`glide2x.dll.bak` (or the next unused numbered backup) before installing
the pinned DLL. No pre-existing backup is overwritten.

Linux and Windows CI run synthetic CD installation/verification tests.
GitHub Actions ships **only the Windows x86 EXE**, with **30-day** artifact
retention. Windows tests additionally compare the compiled DLL to the
unpacked copy byte-for-byte, and exercise the upgrade/backup path.

## Architecture and evidence

- [Design and roadmap](docs/architecture.md)
- [How to document retail disc editions](docs/disc-evidence.md)
- [CD installation steps from the author's Lutris installer](docs/lutris-cd-install.md)
- [Bootstrap test protocol](docs/testing.md)

No copyrighted Rayman 2 content, proprietary executables, disc images,
activation keys or protection-removal payloads are stored or distributed here.
Some CD executables use obsolete SafeDisc protection; compatibility must be
verified separately from ISO extraction. Support will be claimed only after
testing the corresponding edition on Windows and Wine/Proton.

Rayman2Fix's own project license is **to be selected before release**.
Third-party components will retain their respective notices and licenses.

Prepared by legluondunet, assisted by ChatGPT (OpenAI).
