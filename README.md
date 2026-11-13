## Sound while F1/Escape menus are open — research build

Requested future setting: `[Features] MusicInPauseMenu=0` (disabled by
default). **Not yet implemented in the user INI**, because a reliable
audio-only pause policy has not been identified. No placebo option is
shipped. The original music and the game's sound driver are unchanged.

The separately labelled GitHub Actions artifact
`Rayman2Fix-Windows-x86-audio-trace` has a **research-only** DLL with
`RAYMAN2FIX_AUDIO_TRACE=1`; the normal
`Rayman2Fix-Windows-x86-bootstrap` build has **no audio tracer**.
On the one SHA-256-approved original CD executable, and only when
`[Debug] Log=1`, this tracer observes up to 48 native calls across
pause/resume Sxd, Sxd transitions, MIDI, MIDI transitions and CD
audio. The verified original driver function is always called with
the same argument; no sound is silenced, played, skipped, replaced,
or looped. Installation of those diagnostic-only hooks fails closed
when driver functions or game-memory protections are unexpected.
The research build is **not a stable release**.

For a reproducible Wine test, play until music is audible, open
the Escape menu for a few seconds, return to the level, open
the F1 information screen for a few seconds and return, then
Alt-Tab away and back (to contrast focus-based audio pauses).
Exit the game and provide **both** `Rayman2Fix_installer.log` and
`Rayman2Fix.log` from beside `Rayman2Fix.exe`. The lines
`RUNTIME-AUDIO-TRACE` identify exactly which native music/sound
pause channel is invoked and whether the game had focus at
that moment; a key-down flag is only supporting evidence, not
a menu-state detector. Without an actual test trace, do not
enable an audio bypass. See issue #4.

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

- When `Rayman2Fix/rayman2.exe` already exists, the launcher checks the
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
  then publishes directly into `Rayman2Fix/` transactionally. Existing
  unrelated files are preserved; an incomplete/unrecognized installation is
  never silently overwritten.
- The launcher embeds and deploys its own x86 `Rayman2Fix.dll`, creating
  the `Rayman2Fix/` directory and minimal INI automatically. If the
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
Rayman2Fix_installer.log        (installer/launcher when Log=1)
Rayman2Fix.log                  (DLL, GameFrame and game, after launch)
Rayman2Fix/
    Rayman2Fix.dll             (unpacked from the EXE; inert placeholder)
    Rayman2Fix.dll.bak         (only if updated; one latest backup)
    Rayman2Fix.ini             (only user-editable configuration)
    ubi.ini                     (generated game-facing Glide config)
    rayman2.exe.manifest         (generated DPI compatibility manifest)
    rayman2.exe                (from original CD)
    dll/
        snd_cpa_mmx.ini         (from original CD)
        snd_cpa.ini             (initialized from MMX ini)
        ...
    data/ ...                  (original CD assets)
    .rayman2fix-cd-files.tsv   (local size + SHA-256 receipt)
    glide2x.dll                (GameFrame x86: deployed from launcher)
    GameFrame-LICENSE-APACHE.txt (Apache 2.0 license: deployed from launcher)
    SDL3.dll                   (future phase: not bundled or required)
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
; 1 = centered 4:3 picture with black sidebars
Force4x3=0

[Features]
; 1 = allow Space/Enter/Esc to skip jail_20 only
; Learn_10 skip remains disabled (unsafe)
SkipCinematics=0
```

`Resolution=Desktop` automatically uses the primary desktop resolution on
Windows/Wine. You can alternatively select an explicit resolution such as
`Resolution=1280x720` or `Resolution=1920x1080`.
The format is validated and invalid values are refused, not silently ignored.
`FPSLimit=60` caps the **game's rendered frame rate** by pacing its
`grBufferSwap` calls; set `FPSLimit=30`, `120`, or another rate
between 15 and 240 if required. `FPSLimit=0` disables the cap.
`Force4x3=1` in `[Graphics]` selects a centered, exact 4:3 output
image with black sidebars, while `Force4x3=0` (default) retains
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
Existing INIs receive only the missing disabled options, preserving
their custom values, unrelated settings and comments.

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

For the `RAYMAN2` CD, these **eight exact root-level paths** are
excluded from installation if present:

- `00000001.tmp`, `0000040c.016`, `0000040c.256` (SafeDisc-era auxiliaries).
- `secdrv.sys`, `rayman2.icd` (original protection-related components).
- `clcd32.dll`, `clokspl.exe`, `drvmgt.dll` (additional owner-approved
  SafeDisc-era auxiliary names).

A verified inspection of the retail CD found `directx/dplayx.dll`,
a DirectPlay library in the already-excluded DirectX installer folder.
Neither `dxplayerx.dll` nor `dplayerx.dll` was present: these were
mistyped names and have been removed from the exclusion list.

The exclusion never matches other files by extension or similarly named
files under `data/`. Existing installations are migrated without the CD:
after verifying all retained files by SHA-256, the launcher removes these
exact root files and their receipt entries. User settings and other game
assets are preserved.

On a disc whose volume label is exactly `RAYMAN2`, we recognize **23 exact
`Data/World/Levels/*.DAT` paths** discarded by the author's original
[Lutris CD installation script](https://lutris.net/games/install/39141/view).
Those entries stay visible in `--inspect`, but are skipped during
`--extract` (including any unreadable/fictitiously large extents).
**Other unrecognized out-of-range files no longer abort `--inspect`**: they
are shown with a `SUSPECT` label and their path, LBA and size are recorded
in the log. `--extract` still refuses to start if any such unknown entry
exists. No invalid data is silently copied. The log
distinguishes **declared total bytes**, **source image size**, **selected
extraction bytes** and each known exclusion. Unknown/unlisted oversized files
still cause a safe error; no blanket skip-by-size is performed.

```text
Rayman2Fix.exe                    (automatic install/verify workflow)
Rayman2Fix.exe --verify           (check local CD file-size receipt)
Rayman2Fix.exe --inspect "my-disc.iso"
Rayman2Fix.exe --inspect "my-disc.cue"
Rayman2Fix.exe --extract "my-disc.cue"
Rayman2Fix.exe --extract "my-disc.iso" "custom-output-folder"
Rayman2Fix.exe --help
```

The explicit diagnostic `--extract` command still targets
`Rayman2Fix/DiscContent/` by default. Both it and the automatic installer
display a readable command-line progress report: current operation, copied
bytes and files at 5% increments, then a success summary including the
actual files/bytes written, excluded files/bytes, destination, and duration.
The automatic installer also reports the audio, graphics configuration,
executable patch and final SHA-256 verification status. Progress uses
ordinary flushed lines (no ANSI or terminal-only carriage returns), so it
remains readable in Windows cmd/PowerShell, Wine and redirected output. **The no-argument installer does
not use DiscContent**: it publishes the staged assets directly under
`Rayman2Fix/`. Extraction never runs the CD's own installers or modifies
the retail executable. `--verify` checks installed file presence, sizes and SHA-256,
but is not a trusted retail-edition allowlist/profile check.

GameFrame integration means the correct open-source graphics DLL is installed
and SHA-256-verified; it does **not** prove that Rayman 2 can render with it.
The launcher now performs an initial game-startup experiment with strict
original/unprotected EXE SHA-256 and size checks. It starts `rayman2.exe`
through Win32 `CreateProcessW` with `Rayman2Fix/` as the working directory,
waits for the process to exit, and logs the full exit code and duration in
`Rayman2Fix_installer.log`; the injected DLL and GameFrame diagnostics
go to the separate `Rayman2Fix.log`. Invalid editions are never executed.
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
- To test an owner-provided delta without uploading game-derived patch
  material to GitHub, place `Rayman2Fix-executable.r2patch` beside
  `Rayman2Fix.exe`. An installed matching retail executable is then
  upgraded on the next no-argument launch. On a different edition, the
  source is preserved unchanged.
- The final one-EXE packaging remains available: local Windows builds
  may pass `-DRAYMAN2FIX_EXE_PATCH=<absolute delta path>` to embed the
  approved binary delta as RCDATA resource `#1102`. The standard public
  GitHub CI archive **does not yet contain the private delta**, and thus
  does not auto-patch unless the owner provides the sidecar.
- On an exactly recognized original SHA-256, the installer writes and
  verifies the new executable to a temporary file, **renames the untouched
  original to `rayman2.exe.bak`**, publishes the new `rayman2.exe`, and
  records both SHA-256 hashes in `.rayman2fix-exe-patch.tsv`.
- A subsequent `--verify` checks the CD's original hash against the
  **backup**, and the patch target hash against the installed EXE. An existing
  backup is *never* silently overwritten; a failed replacement rolls back.

**The EXE patch is already generated and verified**, but is not
present in the public CI build. When the owner adds the matching local
delta (or builds with it embedded), Rayman2Fix creates
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
