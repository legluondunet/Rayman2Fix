## Driver-space geometry trace paired with native 2D inputs

The same read-only diagnostic build also records up to three first
lower-screen GLI driver quads per native source route, including the
upstream **original** and **mapped** percent X coordinates and the
actual downstream sprite **vertex** X/Y coordinates. Native GLI
calls are synchronous and use a thread-local context that is restored
after each call; its existing 4:3 transformation and double-scaling
bypass are unchanged. The new `RUNTIME-HUD-DRIVER` lines help
determine whether a mismatch comes from the original game descriptor,
the universal percent mapping or the downstream driver.
Original source text, texture names, game pointers and save data
are never logged. No drawn geometry is changed.

## 12 November 2026: compare actual lower-screen geometry

The latest owner-supplied Wine-staging 11.17 log
`Rayman2Fix(20261112-161949).log` shows a successful 1920x1080
session, natural Learn_10 intro exit, and no crash. The three
verified callsite categories use **the SAME 1024x768 native
viewport**, and the common 2D percentage hook is definitely active.

At t60 seconds, original native source counts are:
- `glyph=38263` (JFFTXT font quads),
- `image=2277` (normal and clipped screen images),
- `frame=783` (GLI_vDisplayFrame rectangles).
The frame belonging to the original dialogue appears at native
`x=14.00..94.00, y=80.60..92.40`.
However the first sampled glyph/image entries after t25s were
at `y=7.0..11.5` and `y=8.5..11.5`, i.e. TOP-SCREEN HUD, **not
the dialogue or blue continuation arrow**. These samples cannot
tell us why the dialogue content still overflows the frame.

**Next bounded, read-only test:** sample up to four lower-screen
(`y_min>=72, y_max<=105`) original native rects per producer,
and at 40/60-second checkpoints aggregate their ORIGINAL X
extrema independently for font glyphs, images and panels.
The first items belong to general bottom-screen UI and are not
automatically identified as Murfy or arrow. No strings, fonts,
textures, pointers, personal data or save state are logged.
Most importantly, **this patch does not change any projection,
sprite/triangle geometry or runtime game state**. The owner already
confirmed no visual improvement in the last build and need not
send a duplicate screenshot. By comparing bottom text/image
positions against the actual native panel before scaling, we can
determine whether the layout discrepancy is present in original
input coordinates or is introduced downstream.

Only then choose a universal 4:3 UI architecture; do not
prematurely shrink all fonts or apply a special-case Learn_10
offset. Keep PR #2 draft/unmerged. NOMOVIES work stays paused.

## Additional screen-image branches verified in native disassembly

The original screen-image pass also calls the same GLI 2D percent
entry on three clipped-image paths (call VAs 0x4606DF, 0x460743,
0x46076D). The initial classifier counted only the direct
0x460653 path and mislabeled these as "other". All three additional
callsite return addresses are now included in the image counter.
No renderer or artwork changes.

## 2026-11-12: render-path diagnostics after user confirms unchanged image

Owner explicitly confirms **no visual improvement** under Wine
11.17 at 1920×1080 with the upstream native 2D transform (`02cc2e1`).
Do not ask for a second identical screenshot or claim visual success.
The latest Wine trace verifies the common hook is active and that
all 45,682 intercepted calls at the 60-second checkpoint invoke a
downstream colored (42,212) or plain (3,470) callback and bypass
double-scaling correctly. This *only* proves hook activity.

**Critical conceptual limitation:** the upstream entry is shared
by both native font glyphs and background panels. A shared affine
transform leaves any existing *relative* displacement between the
glyphs and panel invariant. It does not, by itself, prove a correct
layout. In addition, the same input coordinate percentages may map
to different native viewports. This must be measured, not guessed.

Direct original-binary disassembly (approved original SHA256
`7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`)
at `GLI_vDraw2DSpriteWithPercent` (VA 0x429670)
confirms it reads the first argument's native viewport HEIGHT at
`+0x08` and WIDTH at `+0x0C`, and receives x/y arguments in
percent coordinates. The same exact function has three **known,
independent** calling routes, identified by fixed call-return RVA:
- `0x664D9` from `JFFTXT_vDrawString` (font glyphs)
- `0x60658` from the screen-image renderer's direct path
- `0x606E4`, `0x60748`, `0x60772` from its three clipped-fragment
  drawing paths (the same native screen-image function); all four
  are classified as UI pictures
- `0x2987B` from `GLI_vDisplayFrame` (black/background frames)
Other native entry callers remain classified as "other".

A **read-only diagnostic follow-up** classifies at most these
return-address categories, counts their calls at the existing 5/40/60s
checkpoints and prints one bounded geometry-only sample per category
before and after t25s (eight samples total). Each sample includes
native viewport WIDTH/HEIGHT and the original incoming x/y percent
coordinates. No text, pointers, strings, textures, input or save
data are logged. Nothing is scaled differently in this follow-up.
The new trace should tell whether the SAME input viewport and space
are used for fonts, background frames and images; this is the crucial
missing evidence for an **engine-level universal** fix, not a special
case for one dialogue.

The former native 2D transform remains as before (visual failure is
known), but this diagnostic adds no further rendering changes.
PR #2 remains draft/unmerged; NOMOVIES exploration remains paused.

## 2026-11-12 native 2D geometry correction at the single shared entry

The latest user log `Rayman2Fix(20261112-103304).log` is from
Windows 11 1920×1200 (16:10), internal rendering 1024×768.
It proves the 4:3 screen-world viewport correction executes with
`native_width=1024 projected_width=853 projected_x=85 height=768`.
The 3D Hor+ camera and all three HUD sprite callbacks are installed.
The additional `GLI_DRV_vSendSingleTrianglesToClip_` diagnostic
was installed but saw **zero triangles** over 91 seconds at 5, 40
and 60 seconds. The first rejected UV quad had `invz=0`; this
does not identify the actual Murfy text/continuation arrow.
The user screenshot still shows text overflow on the left and the
blue continuation arrow outside the right of the properly centered
black text frame.

**Independent native-code verification from the exact SHA256-approved
original Rayman2.exe**:
- All screen font glyphs call `GLI_vDraw2DSpriteWithPercent`
  at VA **0x429670** from `JFFTXT_vDrawString` VA **0x4664D4**.
- `GLI_vDisplayFrame`, the native panel/frame renderer, also calls
  the same VA from **0x429876**.
- The screen-image rendering path, used for original UI pictures and
  potentially the continue arrow, calls the same VA from **0x460653**.
- The native function multiplies X/Y inputs by the float **0.01**
  constant at VA **0x49C618**, then by the native viewport dimensions.
  Its input is **percent space 0..100**, not 1000 text units or
  projected 1024×768 vertex coordinates.
- The verified 7-byte prologue at this VA is
  `83 EC 50 8B 44 24 54` (two complete instructions).
  Therefore a separate, authenticated x86 detour is possible without
  modifying game files.

The new code moves the global centered 4:3 transform **upstream to this
shared native 2D geometry entry**, before fonts, arrows, and frames can
diverge to separate driver paths. It transforms only horizontal
percent coordinates as
`x'=50+(x-50)*((4/3)/output_aspect)`, retaining Y, font size,
dialogue strings, texture coordinates, driver mode and game logic.
For 16:10, input X=[0,100] maps to [8.333,91.667]; for 16:9,
X=[0,100] maps to [12.5,87.5]. The existing GLI driver sprite
callbacks **must bypass their own 4:3 correction synchronously**
during a call from the native 2D entry, so each element is transformed
**exactly once**, never twice. Sprite callbacks reached from other
engine paths retain the current validated 4:3 behavior.

Removes the now-proven-unused triangle diagnostic and short-lived
lower-screen samples; retains only bounded counts of native 2D calls
and downstream passes at 5, 40 and 60 seconds to confirm execution.
This architecture does not special-case Learn_10, Murfy, any string,
position, language, font, resolution, or picture. It also does not
affect the panoramic gameplay camera.

**Status:** new candidate must be compiled and visually tested on
both Windows/Wine; do NOT conclude the text and blue arrow are fixed
until a new screenshot confirms them inside the original black
frame at 16:10 and another monitor aspect. Keep PR #2 draft and
NOMOVIES research paused.

## 2026-11-12 GLI viewport field order: corrected after Windows 11 test

The Windows 11 1920x1200 screenshot and log `Rayman2Fix(20261112-095637).log`
showed an incorrect `native_width=768 height=1024`. At VA 0x4206BC
the *real* original Rayman 2 binary stores viewport width at offset
+0x0C; at 0x4206BF it stores height at +0x08. The initial patch
incorrectly reversed these fields, so it compressed 768 vertical pixels
instead of 1024 horizontal pixels. The original source viewport is
not modified; only a temporary 0x70-byte viewport copy is changed
before the original projection routine. The corrected code has named
native offsets and byte-level tests guaranteeing only native WIDTH
(+0x0C) and horizontal origin (+0x58) change.

At 1920x1200 (16:10), expect native_width=1024 height=768,
projected_width=853 projected_x=85; at 1920x1080 (16:9), expect
projected_width=768 projected_x=128. No level-specific text, arrow,
font or HUD box changes were added. Visual verification is still needed.

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

## Screen-world interface camera versus panoramic world camera

Rayman 2 has TWO 3D rendering passes. The gameplay camera uses Hor+;
the original game's **screen-world overlay** draws UI actors, including
conversation glyphs, through its own camera and viewport.

Disassembly of the exact owner-approved patched executable shows:
- `0x4052C0` creates the original screen camera whose pointer is
  stored at **0x50054C** and sets alphaX=**1.5**, alphaY=**0.75**.
- `0x405180` configures the dedicated 100×100 / 1000-unit viewport
  at **0x5004D4** and renders through that same screen camera.
- The game's `0x422AB0` camera-adjustment path is called through
  `0x4228D0` for both overlay and ordinary gameplay cameras.

Earlier code erroneously applied the gameplay Hor+ formula to ALL
cameras. At 16:9 that changes the screen camera's alphaX from its
native 1.5 to ~1.259, plausibly magnifying the glyph geometry. The
user's Wine screenshots confirm that JFFTXT font-descriptor scaling
does **not** solve the overflowing speech text.

**Universal correction:** consult the ORIGINAL engine-owned camera
pointer slot (VA 0x50054C) during the SHA/PE-approved camera detour.
When the camera equals that original UI camera, preserve BOTH original
FOV angles; for other gameplay cameras continue Hor+ unchanged.
This is a renderer-level rule, not a level, language, font, string,
coordinate, or resolution exception. The 2D HUD/sprite callbacks
still use the centered 4:3 canvas. Remove the entire former
`JFFTXT_vDrawString` interception and all Learn_10-specific font
heuristics/tests, without touching original dialogue assets/saves.

**Screen-world viewport projection correction (17:37 Wine screenshot):**
The screen camera was correctly recognized and excluded from Hor+,
but the projected text and blue arrow still spill outside the already
centered 4:3 black dialogue box. Investigation of the exact approved
Rayman2.exe disassembly shows the viewport used by this pass still
covers 100% of the internal 1024-pixel horizontal image.

The native GLI functions `0x420660` and `0x422AB0` expose
viewport width/height in the 0x70-byte struct at +0x08/+0x0C and
origins at +0x58/+0x5C; `0x420A80` copies that structure onto the
stack for camera projection. The runtime now supplies the original
projection function with a second temporary copy **only for the
engine-owned screen-world UI camera**. It scales viewport width by
`(4/3) / output_aspect`, and offsets its left edge by half of the
removed width, preserving height, vertical position and native FOV.

For 1920x1080 with 1024x768 internal output, the expected viewport
projected geometry is width **768**, left **128**, height **768**.
This matches the same global horizontal 4:3 centering used by the
existing HUD sprite callbacks. Other cameras, 3D panoramas, text
assets, save games and the original viewport storage are untouched.
The correction applies to every level and aspect, never to named
dialogues, font sizes or individual arrow sprites.

**The next owner log must contain** `RUNTIME-VIEWPORT:
screen-world 4:3 centered native_width=1024 native_x=0
projected_width=768 projected_x=128 height=768`. If the engine
returns different values, the patch logs the actual viewport geometry
or safely retains the native projection; the visual result must
still be checked against the Dreamcast screenshot.

**Visual validation required on Windows and Wine:**
- Verify the log says `RUNTIME-FOV: native 4:3 screen-world camera
  preserved`, ideally alphaX≈1.500 and alphaY≈0.750.
- Verify `RUNTIME-FOV: Hor+ enabled for world cameras` and that the
  playable 3D world still uses monitor-wide FOV.
- Compare the original Murfy dialogue and tutorial panels in Learn_10
  with the supplied 4:3 Dreamcast reference.
- Also test other levels, UI text, menus and at least one other aspect
  ratio if practical. The layout must not rely on level-specific
  tweaks or grow any text box to cover overflow.
- There should be NO `RUNTIME-DIALOGUE:` log entries because the
  experimental JFFTXT detour is removed.

CI and pure policy unit tests cannot confirm on-screen correctness;
keep draft PR #2 unmerged until screenshots are validated.
The Learn_10 movie skip is still disabled; NOMOVIES work remains paused.

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
