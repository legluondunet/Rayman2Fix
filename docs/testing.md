# Bootstrap validation

Build the x86 Windows `Rayman2Fix.exe`, which **embeds** the project's
inert `Rayman2Fix.dll`. The workflow archive must contain **only one EXE**;
the INI, log and runtime directory must be created by that executable.
The same EXE must now also embed and deploy the pinned GameFrame
`glide2x.dll` and Apache 2.0 license with verified SHA-256.
Run `ctest` on Windows and Linux for synthetic ISO/CUE and automatic CD
installation, verification and refusal behavior.

Synthetic tests MUST never depend on distribution of Rayman 2 assets.

## Automatic startup checks

- Launch **`Rayman2Fix.exe` without arguments** and without any CD or
  `Rayman2Fix/` folder: it creates the folder, its own embedded runtime DLL,
  the minimal `[Debug] Log=1` and `[Graphics] Resolution=Desktop` INI
  and the single launcher-side log, then
  reports the missing CD cleanly. The former `--init` is unsupported.
- Place exactly one original `.iso` or `.cue` beside the launcher. The
  default command inspects it, verifies required file paths, extracts into
  a sibling `Rayman2Fix.installing/` staging folder, initializes
  `dll/snd_cpa.ini` from `snd_cpa_mmx.ini`, writes a size receipt and
  promotes the assets to `Rayman2Fix/`. The installed layout must not use
  `DiscContent/`.
- Remove or unmount the source ISO and run without arguments again.
  Existing game files must be **verified without reinstalling**. An
  unpatched, unsupported or modified game executable must **never** run.
  Only the exact approved unprotected executable with original `.bak`,
  patch marker, GameFrame and sound/ubi.ini assets can be launched.
- Modify/remove one installed game file (including a same-size modification):
  SHA-256 verification and default launch must report the mismatch and
  leave the user's files untouched.
- Fresh RAYMAN2 installs must omit exactly eight root-only entries:
  `00000001.tmp`, `0000040c.016`, `0000040c.256`, `secdrv.sys`,
  `rayman2.icd`, `clcd32.dll`, `clokspl.exe`, `drvmgt.dll`,
  including their SHA-256 receipt entries. Same-named files under `data/`
  are preserved.
- The misspelled root-level names `dxplayerx.dll` and `dplayerx.dll`
  must NOT be excluded if encountered in synthetic test media.
  The real `directx/dplayx.dll` belongs to the already excluded `directx/`
  installer tree, not to the root-only protection auxiliary list.
- Existing installs recorded by older builds must migrate without the
  original CD: remove those eight files and receipt entries, tolerate an
  already-deleted obsolete file, preserve remaining SHA-256 hashes and
  the user INI, then verify normally.

- With two images, no images, a malformed CD, or a foreign folder already
  occupying `Rayman2Fix/`, the launcher must fail without silently
  deleting any data.
- Generate `ubi.ini` on the first install from the CD's settings, preserving
  unrelated keys and sections, but force Glide 2 and user-selected
  `[Graphics] Resolution` (e.g. `1024x768`) as
  `GLI_Mode=1 - 1024 x 768 x 16`.
- Change `Resolution` to `1280x720` with the original CD removed.
  On the next launcher run, check that only the managed graphics values
  change, and the game-facing `ubi.ini` is updated. Removing `ubi.ini`
  should cause it to be regenerated. Invalid resolution must fail safely.
- Older manifests that recorded `ubi.ini` must migrate its entry out of
  the immutable SHA-256 receipt without deleting the game-facing file.
- Validate `--verify` on installed CD files. It checks **size and SHA-256** against
  a locally generated receipt, but source-edition authentication and
  no-CD/Glide compatibility are not yet certified.
- Check that the generated `Rayman2Fix.ini` contains
  `[Graphics] FPSLimit=60`. Older user INIs without this entry must still
  default to 60. Changing to `FPSLimit=75` must pass that value to
  GameFrame's child-only `RAYMAN2FIX_FPS_LIMIT` environment variable; the
  launcher environment must be restored immediately after process creation.
  `FPSLimit=0` disables pacing. Valid values are 0 or 15..240;
  invalid strings and out-of-range values must fail safely.
- Verify the temporary pinned GameFrame checkout applies
  `tools/gameframe/apply_fps_limit.py` before the Rust build and that the
  limiter is attached to `grBufferSwap` **on the producer/game thread**,
  not only the asynchronous GL swap thread.
- Test an exe-only copy of the workflow artifact: verify its own
  `Rayman2Fix.dll` and pinned GameFrame `glide2x.dll` SHA-256 match the
  original build outputs. Verify the extracted Apache license is identical
  to the pinned GameFrame source and no DLL/INI/log sidecars are shipped.
- Replace the installed `Rayman2Fix.dll` twice in succession, with
  a preexisting `Rayman2Fix.dll.bak` and old
  `Rayman2Fix.dll.backup.N` files: after each replacement, retain
  **one** `Rayman2Fix.dll.bak` with the immediately previous runtime.
  Delete earlier backup generations; a subsequent unchanged launch
  must not change the backup.
- Replace the installed `glide2x.dll` with an unknown file, rerun the
  launcher, verify the old file is preserved as `glide2x.dll.bak` and
  the pinned DLL is restored. A third launch must be idempotent.
- Verify SDL3 is NOT a prerequisite and does not produce a missing-runtime
  warning; gamepad input remains unimplemented until the next phase.
- Check that `Log=0` suppresses and removes stale logs, while preserving
  existing user settings.
- With untrusted/incorrect `rayman2.exe` and an invalid patch marker, the
  Win32 preflight **must refuse** to spawn any process. CI tests this
  refusal on both Windows and Linux.
- The Windows-specific CreateProcessW smoke test launches a harmless
  fixture with spaces in its path and an explicit working directory,
  waits for it to finish, verifies the working directory marker and
  records the exact nonzero exit code. No commercial executable is
  distributed or executed by CI.

## First approved-game Wine/Windows launch test

After installing the supported retail CD and applying the owner's
`Rayman2Fix-executable.r2patch` (sidecar next to the launcher), check that
`Rayman2Fix/rayman2.exe.bak` still hashes to
`7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`
and `rayman2.exe` hashes to
`9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`.

Run `WINEDEBUG=-all wine Rayman2Fix.exe` (or `Rayman2Fix.exe` on Windows).
This build should now attempt to spawn `rayman2.exe` in its own directory
and **wait until the game process exits**. Observe any window/dialog, intro,
menu, graphics (Glide), sound and keyboard navigation. To check the FPS
cap, run the game at `FPSLimit=60`, then `FPSLimit=30` and `FPSLimit=0`
for a brief comparison using an external FPS overlay. Confirm that the
gameplay speed does not change and that Wine Alt+Tab is still responsive. Record the console
output and `Rayman2Fix.log` (look for `LAUNCH-READY`,
`LAUNCH-START`, `LAUNCH-EXIT`, or `LAUNCH-ERROR`).
A nonzero exit code may indicate a game crash or a missing runtime component.
Do not claim playability from the launch event alone.

## Native borderless, DPI and first Hor+ in-game validation

With the owner's approved unprotected Rayman 2 EXE plus its original
`rayman2.exe.bak`, run the current Windows-x86 launcher under Wine:

```bash
WINEDEBUG=-all wine Rayman2Fix.exe
```

The **single** user-facing setting `Rayman2Fix/Rayman2Fix.ini` should remain
`[Graphics] Resolution=Desktop`. On the user's 1920×1080 monitor the
launcher must log:

- `CONFIG-UBI: ... internal=1024x768x16 output=1920x1080`; the internal
  Glide compatibility mode must **not** be forced to an unsupported 16:9
  value.
- `DISPLAY: primary_physical=1920x1080 dpi=... output=1920x1080
  GameFrame.GLIDE_SIZE=explicit window=borderless-not-topmost`.
- `RUNTIME-FOV: injection_attempted=1 dll_loaded=1 active=1 status=1`
  (or `status=2` for an actual 4:3 display). A failed load, missing export,
  unrecognized code signature or patch failure must be logged explicitly
  and requires investigation before claiming Hor+.
- `PRESENTATION: window_found=1 borderless_applied=1 changes=...`
  with `child_window_dpi` logged; `LAUNCH-EXIT` must report the real
  game exit code.

Inspect **actual gameplay**, not just a launch success message:

1. Window client rectangle exactly covers the physical primary monitor:
   no border/title bar/taskbar, no exclusive display mode switch. Alt+Tab
   and switching to another window must work and the game must not be topmost.
2. Horizontal FOV should show more landscape at 16:9 than 4:3, *not* zoom,
   vertical crop or a stretched 4:3 picture. Compare matching scene screenshots.
   Check that Rayman is not distorted; inspect HUD and all menu overlays.
3. Check native resolution and physical DPI at 100%, 125%, 150% Windows
   scaling and under Wine where possible. The launcher must not confuse
   1920 physical pixels with DPI-virtualized logical pixels. Check a second
   monitor or a nonzero virtual-desktop origin if available.
4. Verify cutscenes, menus, gameplay and the first post-video transition,
   plus quitting and a second launch. The external DPI manifest must not
   trigger an SxS failure or an exception.
5. Verify a 4:3 display leaves the original FOV untouched and unknown EXEs
   never receive executable hooks. Share a screenshot and the single
   `Rayman2Fix.log` for any discrepancy.

The initial Hor+ runtime uses an exact seven-byte checked detour in the
owner-approved Win32 EXE at image RVA `0x22AB0`, with alphaX/alphaY
camera fields at offsets `0x64`/`0x68`. No game exe bytes are modified
on disk. Unlike the main launcher, this game runtime has not yet been
validated by real visual screenshots; in particular **HUD overlays may
still require their own aspect correction**.

## Learn_10 native dialogue text layout (widescreen)

The owner supplied three reference screenshots (two PC 1920x1080, one
Dreamcast 4:3). The PC tutorial/dialogue glyphs overflow the already
properly centered black conversation panel. The panel, narrator icon
and arrow must NOT be widened, redrawn or repositioned to mask the bug.

On 16:9, Rayman2Fix now hooks the **owner-verified original**
`JFFTXT_vDrawString` (RVA 0x660B0, seven complete checked x86
instructions; exact original and patched EXE hash verified during launch).
For the bottom-of-screen text of `learn_10` only, it passes a
**temporary copy** of the original game text descriptor to the native
renderer. It maps X from the 1000-unit screen-space canvas into the
same centered 4:3 region as the panel and scales the native font size
by `(4/3) / monitor_aspect`. Native Y, dialogue wording, colors,
line breaks, texture data and other HUD sprites remain untouched.
On 4:3 and in all other levels the hook simply passes the
original descriptor through unchanged. Failure to match exact
original prologue refuses the hook and logs this safely.

**Required visual check** under Wine and Windows:
1. At 1920x1080, start a new game, skip `Jail_20` and watch `Learn_10`.
   Compare `SALUT RAYMAN ! CONTENT ...` and the tutorial
   `PRESSE Q POUR SAUTER ...` to the Dreamcast reference.
2. Verify that every line remains inside the original black box with
   a visible margin; the narrator portrait and continuation arrow
   must not be distorted. The font must keep the game's native color,
   outline, accents and punctuation.
3. Check after leaving the dialogue that all regular HUD sprites,
   pauses, menus and gameplay remain unchanged. Recheck 4:3 and one
   alternative wide aspect when possible.
4. The log should include `RUNTIME-DIALOGUE: verified native JFFTXT...`
   and, on an actual bottom dialogue, `RUNTIME-DIALOGUE: native Learn_10
   bottom text size/X corrected`. A successful CI **cannot** prove
   that the three screenshot compositions now match. Keep PR draft
   until the owner validates the visual result.

**Failed Wine visual check (2026-11-10, 162311.log and 162303.png):**
the black panel still overflows, and the log confirms the x86 font
detour **installed** but NEVER emitted `native Learn_10 bottom text
size/X corrected`. There is no evidence that the affected dialogue
font used JFFTXT_vDrawString or that the eligibility conditions matched.
This is *not* a reason to change the scale coefficient blindly.
The next read-only patch therefore logs `native JFFTXT draw callback
reached` once, a handful of `font-probe sample` geometry records
(x,y,size,alpha,flags, but NEVER text content or addresses), and
`learn_10 after 3.5s font_calls_total/learn/corrections` even if
none matched. If `font_calls_learn=0`, stop working on JFFTXT and
locate the actual dialogue renderer instead. If positive but
`match=0`, refine only the exact observed eligibility constraints.
The native game pixels and all dialogue sizes are unchanged by this
diagnostic-only follow-up.

## Manual checks

- `Rayman2Fix.exe --help` and `--version`.
- `--inspect disc.iso` and `--inspect disc.cue`, each containing one ISO9660
  MODE1 data track. Unsupported formats fail without side effects.
- `--extract disc.cue` writes to `Rayman2Fix/DiscContent`.
  Check the console shows image inspection, extraction progress in approximately
  5% steps, finalization, and a factual summary (files/bytes copied,
  exact skipped entries, destination, elapsed time). Ensure even one
  large file emits progress during its transfer rather than only at the end.
  The automatic no-argument installer should show its eight active stages,
  including SHA-256 receipt generation and verification, then a final summary.
  Both modes must flush line-oriented progress for Wine/PowerShell.
- Repeat the extraction: it must refuse to overwrite the prior directory
  without incorrectly reporting success. An interrupted/failed extraction
  must not leave a promoted output or a false success summary.
- Truncated / malicious image paths fail without writing outside the staging dir.
- Check UTF-16 Windows path support on filenames with French accents.
- When a Joliet supplementary descriptor is present, use its actual long names
  instead of the ISO9660 primary 8.3 aliases; show the source in `--inspect`.
- Export every directory and file component in lowercase, including accented
  characters. Confirm the output is writable and has no Windows read-only bit.
- Extract the CD's single original DLL directory to `dll/`, with no extra
  `DLL`/`Dll` merge. Reject unexpected case-colliding source entries.
- Without Joliet, clearly label the ISO9660 primary-name fallback instead of
  inventing long file names.

**The ability to spawn a separate Windows process is unit-tested; actual
Rayman 2 rendering, playable no-CD launch, SDL3 initialization or game
hooks are not established by CI.**
