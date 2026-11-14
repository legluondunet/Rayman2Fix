# Rayman2Fix — *experimental* ESC music continuation

This is **not a release**. The build has not yet been validated against the
owner's installed audio-driver DLL or by a real Wine gameplay session.
Only the separate GitHub Actions artifact
`Rayman2Fix-Windows-x86-esc-music-prototype` contains the executable with
this experimental code. The normal build is unaffected.

## Activation and safeguards

The prototype is compiled only when **both** build switches
`RAYMAN2FIX_AUDIO_TRACE=ON` and `RAYMAN2FIX_ESC_MUSIC_PROTO=ON` are set.
It additionally requires **at launch**:

```bash
RAYMAN2FIX_ESC_AUDIO_PROTOTYPE=1 WINEDEBUG=-all wine ./Rayman2Fix.exe
```

It also requires `[Debug] Log=1` in the existing
`Rayman2Fix/Rayman2Fix.ini`. This is intentionally a **test-only
environment variable**, not the planned `[Features] MusicInPauseMenu`
setting. An ordinary no-argument launch is required to deploy the bundled
experimental `Rayman2Fix.dll` before launching the existing game. Do **not**
use `--launch-no-verify`, which deliberately skips runtime DLL deployment.
The experimental build **does not contain the commercial game or soundtrack**.
It can launch only an already valid user installation with the approved
unprotected game executable, original backup and installation receipt.

The code checks exact known original-CD x86 return-site bytes at **four**
callers, for scripted sound pause/resume and Win32 focus pause/resume.
No caller is hooked if any expected code bytes differ. The same exact
real current Theme/SXD driver, current Theme resource-descriptor reference
from the native player and unique loaded-bank provenance must be found.
The previous comparison of that pointer to a synthesized packed bank/index
was wrong and is retained in diagnostic logs only, never as an enable gate. The original script and Win32
sound wrappers still run.

During a *candidate* ESC-induced script pause, exactly two identical
Theme/SXD `PauseTransitionSxd` calls must be observed. The game UI flag
typically rises **after** those driver calls; no synthetic resume occurs
until it is subsequently observed open with the game in foreground,
the same active Theme **and** the native `YAM_GenMenuMap_I1` actor present
in the active game world. The actor check is bounded and performed only at
that transition. The current prototype tentatively balances the
original double pause by issuing exactly two callbacks to the **original**
driver; the installed APM mixer's real callback counting/idempotence has
**not been authenticated**. On menu close it suppresses exactly two matching
native resumes; unrelated SFX, native Stops and other handles are never
selected. Focus transitions have a separate policy, including safety pause
and a fail-closed quarantine for unexpected event orders. F1 is excluded.
The native scene pause flags and per-sound markers are not rewritten.

**This is still a working hypothesis requiring actual listening.**
The observable key press and in-game menu flag are combined with distinct
native source paths, but do not on their own prove that no other script can
enter a similar state. If no eligible current Theme is found or a required
proof fails, the build should leave the original native audio calls unchanged.

## Controlled Wine test

Save any ongoing game first, then use the experimental EXE **from the
GitHub Actions artifact** in the same launcher directory as the existing
installation. The installer manages at most one previous
`Rayman2Fix.dll.bak` while replacing its own DLL. Keep a separate copy of
your working launcher/installation before experimenting.

1. Start without the environment variable and confirm the original behavior
   (the game music stops in ESC).
2. Quit. Start with `RAYMAN2FIX_ESC_AUDIO_PROTOTYPE=1` as above.
3. In a level with audible background music, open ESC for several seconds,
   return to gameplay, and listen for *continuous* playback with no restarted
   song, skips, double play or changed sound effects.
4. Repeat with ESC open then Alt+Tab away and back; the music must obey the
   original background-focus pause. Test one level change with music running.
5. Quit and inspect **both** separate logs beside the launcher:
   `Rayman2Fix_installer.log` and `Rayman2Fix.log`.

Useful `Rayman2Fix.log` markers:
- `RUNTIME-ESC-PROTOTYPE: enabled=1`
- `RUNTIME-ESC-PROTOTYPE: event=source-hooks-installed`
- `event=script-pause-observed`
- `event=driver-resume-batch` and `event=native-resume-suppressed`
- `event=driver-pause-batch` when compensating for focus loss or uncertainty

A successful CI build demonstrates executable compilation and embedding,
**not** that audio continued correctly in Wine. If sound behaves unexpectedly,
stop this experiment and relaunch with a **normal nonexperimental build** to
restore the normal DLL, without deleting music files or overwriting saves.
Do not merge this prototype or expose `MusicInPauseMenu=1` until a real
Windows/Wine test validates sound, focus and Stop behavior.


## Evidence from owner Wine test — 14 November 2026

The owner-provided `Rayman2Fix(20261114-102144).log` has 243 lines and
**`prototype=0`**: the x86 experimental DLL was deployed, but the opt-in
environment variable was **not enabled**. Therefore this run does not test
continuing music. All native sound calls were forwarded unchanged.

It provides new, independent research evidence: four original script-driven
Theme/SXD pause pairs (actual loaded outer bank/index **9:5**, inner
**9:6 → 9:7 → 9:8 → 9:9** as native levels evolve), three complete
script-driven resume pairs, and a legitimate Theme stop for the fourth.
When the native `in_game_menu` flag rises, the read-only actor probe
consistently observes `personal_type_found=1 dynamic_actor=1
inactive_actor=0` on the loaded level. Initial actor probing before
game loading yielded unknown, not proof of absence. During the last
observed menu exit, the actor is no longer in the active dynamic root.
The generic flag remains insufficient as independent ESC attribution,
but checking it with the actor plus the native script caller is stronger.

Every sampled old `native_theme_player_key_matches_outer` value is zero.
The previously assumed format `(loaded_BNM_bank<<16)|resource_index`
was **not** the native memory value at `0x4B95CC`. The original x86
`0x45997B` compares the incoming resource-descriptor **reference**
with `0x4B95CC`, while `0x457F21` delegates the Theme resource to
`0x459950`. The prototype now additionally compares that native
reference with its independently verified active outer Theme descriptor
pointer. This field is called `native_theme_player_ref_matches_outer`;
it must equal **1** before intervention. If it remains zero, no audio
suppression/replay is permitted. The old packed bank/index comparison is
preserved only to understand legacy trace logs. Neither comparison
alone proves audible music identity; real Wine listening and Alt+Tab
still need validation.

## Experimental Wine log — 14 November 2026, 10:37

`Rayman2Fix(20261114-103730).log` is the first confirmed
`prototype=1` / `source-hooks-installed` test. The original audio
calls still flowed through unchanged: **no** `driver-resume-batch`
or `native-resume-suppressed` was emitted, and the first
`script-pause-observed` was already `phase=0` (candidate discarded).
This is not a successful music-continuation test.

The log exposed two separate experimental gating errors:

1. The first ESC script pause at `t_ms=38040–38041` has a unique,
   bank-proven native Theme + SXD owner (outer 9:5, inner 9:6), but
   `native_theme_player_ref_matches_outer=0`. The old adapter demanded
   this **final** player match while merely collecting the two native
   Pause callbacks, discarding the candidate before the menu flag rose.
   The observed previous/next scenes can transiently carry a different
   native player reference. We now collect the strict unique outer/inner
   owner and bank identity **provisionally**, then revalidate
   `native_theme_player_ref_matches_outer=1` before any synthetic Resume.
   A mismatch at menu-open remains a hard opt-out, not a bypass.
2. A later ESC script pause at `t_ms=88176–88177` entered at native
   pause depth 2, not 1. Its current-player descriptor reference
   **did** match (1), but the prototype's earlier hard-coded
   `depth==1` candidate restriction silently rejected it. Native
   pause depth is cumulative, so candidate detection now accepts only
   a bounded depth in **1..8** while still requiring a real pressed ESC,
   foreground and initially closed menu, plus unchanged native
   script-wrapper provenance.

The return-address research probe printed `audio_origin=unknown`
after all four source call sites were diverted through the experimental
DLL. That is expected from a changed native call stack and should not be
treated as an unsourced audio event. The new trace labels its origin
using the **scoped thread-local original caller** from the already
verified source-specific detours. This changes diagnostics only, not
the selected physical handles or source of hook authorization.

A genuine `StopTransitionSxd` occurred before the first menu flag
fell (`t_ms=46425`), so no compensation may retain that retired
Theme. The regression remains important for the **second** and
**third** ESC sessions, which have a current-player pointer match
and a real native menu actor.

These changes are a candidate-provenance repair, **not** evidence
that the installed `WAVMWbvr.dll` pilot supports paired physical
resume or that the sound continues. Windows x86/CI and one further
live listening test are required.


## Owner Wine trace — 14 November 2026, 10:50 (r3 milestone)

`Rayman2Fix(20261114-105057).log` is the first real gameplay trace
demonstrating the **full experimental adapter path**, across fifteen
native ESC/menu-open events, with the tester responding *« enfin ! »*.
The trace includes exactly **15** `script-pause-observed`,
**15** `menu-verdict=paired-native-resume-requested`,
**15** `driver-resume-batch`, and **28**
`native-resume-suppressed` events. The live driver therefore received
the intended two compensation calls in all fifteen observed menu
openings; fourteen observed script exits had matching pairs of native
resumes suppressed. This is evidence of the intended control flow,
**not** an independent digital audio recording or a Windows native
compatibility test.

The last menu-open at ~173.6s is different: instead of a fifteenth
matched native-close pair, the machine enters
`driver-pause-batch phase=6` (`quarantined`) at ~176.1s and then the
native game emits a legitimate `StopTransitionSxd` at ~176.145s.
The experimental policy restored its fail-closed physical pause on
uncertain state, rather than erroneously leaving music artificially
playing after stop. Treat this as a **follow-up audit target** before
release, not as proof of a crash or regression. The next logged
scene transition completes, and the trace contains no explicit fatal
error.

The test does **not** demonstrate Alt+Tab: every sampled foreground
state was 1. Native Windows and alternate audio backends are untested.
Keep opt-in build-only functionality gated, PR #9 in draft, and the
normal release build unchanged until those compatibility checks pass.


## Owner Wine trace — 14 November 2026, 10:56 (r3 repeat test)

`Rayman2Fix(20261114-105615).log` records the same experimental
build with `prototype=1` and verified source hooks. Across **five**
script-driven ESC openings there are **five** `paired-theme-candidate-ready`
events, **five** `paired-native-resume-requested` menu verdicts,
and **five** `driver-resume-batch paired-calls=2` compensations.
The four ordinary menu exits emitted **eight**
`native-resume-suppressed` events, precisely two per completed exit.
No fatal error appears in the trace.

The final opened menu at about `t_ms=187.595s` has no ordinary native
resume pair in this recording; instead, the original game forwards a
legitimate `StopTransitionSxd` at `t_ms=190.365s` and the menu actor
subsequently leaves the active world. A later native Stop at 193.917s
belongs to the next loaded soundtrack. Neither native stop is suppressed.
This is a useful scene-transition/stop result, not a proof that every
level transition or focus reentry works.

Separately, the engine pause byte temporarily rises to 1 between
`t_ms=52.513s` and `65.079s` while `in_game_menu=1`, but all
sampled foreground flags still equal 1. This **does not**
establish a real Alt+Tab/focus-loss test, so do not mark that case
as covered or infer its audible behavior.

The expected r3 adapter path now repeats across two independent Wine
logs, but this source observes callbacks rather than sound output.
Music continuity is supported by the owner's qualitative feedback
and still requires native Windows and true focus-loss checks before
an enabled-by-default production release.
