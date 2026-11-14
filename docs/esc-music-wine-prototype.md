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
