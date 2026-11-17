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
