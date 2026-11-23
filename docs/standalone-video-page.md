# Standalone VIDEO information page — experimental prototype

Research branch based on the original native graphics-info study; this
prototype is deliberately not enabled in the standard Rayman2Fix release.

## Architectural change

The original game-level Options menu is unmodified. Once a validated
Menu controller enters the native normal-5/reflex-0 VIDEO page, only its
verified script copies 3 and 4 are terminated before old choices or labels
can be constructed. Script 4 retains its first two verified scene-position
calls, pending visual confirmation that these preserve the butterfly.
Old native video selection/save dispatches remain independently guarded.

A distinct `standalone_video_page.cpp` module owns all information strings,
state, colors, geometry and rendering. It hooks the signed-off original
game's `GLI_DRV_vFlipDeviceWithSyncro` slot (image RVA `0x104538`) and
draws immediately before the native flip, via Glide2 `grLfbReadRegion`
and `grLfbWriteRegion`. The underlying 3D framebuffer is read and
alpha-composited locally. Win32 GDI only creates a **memory-only glyph
mask**; no popup or external window, downloadable text/font asset,
Direct3D9 call, injection of proprietary glyphs, or game file rewrite.

The page draws *only* while all guards pass: approved image, loaded MENU,
verified AI controller, swapped private scripts and normal5/reflex0.
The parent Options page, other menu behaviors, brightness and gameplay
pass unchanged. The original 1999 internal game mode remains 1024x768x16
and the user-configured output dimensions are displayed separately.

## First user validation

Use only the isolated `Rayman2Fix-Windows-x86-native-graphics-menu-prototype`
artifact from the draft PR #52, not the standard bootstrap EXE.

1. Open Options: check Video, Son, Contrôles and Langue are intact.
2. Enter Video: a **separate left-hand information panel** should appear
   over the live 3D menu background, without the old resolution list.
3. Check whether the original butterfly remains animated and Escape
   returns to Options (no permanent display-mode change).
4. Press Enter repeatedly: no visual selection refresh is intended.
5. Return to Options and visit Son/Contrôles/Langue, then return to
   Video and Alt-Tab.
6. If there is a blank page, incomplete transition, or missing
   butterfly, close the game and attach `Rayman2Fix.log`. No further
   interactive video option should be activated in an unstable state.

## In-game ESC Pause menu

The 23 November 15:28 owner log confirms an independently authenticated
shared Fix.sna controller (23 normal / 6 reflex behaviors). Its
normal=9/reflex=2 state is the script-identified in-game VIDEO page;
normal=14/reflex=3 is a distinct submenu state.

The latest **experimental** build now preflights five exact original
script shapes in this distinct controller, swaps private copies (never
rewrites commercial assets), guards the obsolete resolution and save
paths, and draws the same blue-tinted Glide2 information panel while
the authenticated normal=9/reflex=2 page is visible. The separate
`RUNTIME-PAUSE-VIDEO-PAGE: frame-rendered=1` marker confirms actual
in-game rendering. Visual behavior, Escape navigation, and the native
butterfly still require manual Windows/Wine validation. No production
parity is claimed yet.

## Main-menu Rayman website (research build)

- Handle **362** was independently verified from the owner's original
  `fix.sna` + `fix.rtb` for all five native languages, with the
  exact original `/C:` text strings. In the original MENU only,
  the one validated string-pointer slot is temporarily changed to
  `/C:Rayman2Fix` (no version/commit suffix).
- The original PC x86 executable imports ShellExecuteA from SHELL32 at
  RVA 0x9C220. An exact IAT callback remains as a fallback, but the
  15:28 owner log showed **no callback** when testing the label.
  The native main-menu normal-11/script-0 independently contains its
  original procedure **369** (node 1835) and original website string
  literal at node 1836 (`http://www.rayman2.com/`). The latest
  experimental code validates all 1995 script nodes and clones that
  script in private memory, replacing **only** the verified literal
  pointer with the Rayman2Fix GitHub URL. No original asset is edited.
  The original ShellExecute behavior is retained, and both pointer
  replacements are restored on MENU unload. A live click/Enter test
  is still required.
- The site handler was revised in commit cff151c8 to check the
  hash-verified EXE's exact ShellExecuteA **caller return RVA 0x187267**
  rather than reading a legacy website URL. The 15:01 owner log reports
  runtime build 9aab8139, which predates this revision; that log cannot
  validate the corrected action. Pressing Enter or clicking Rayman2Fix
  and observing the default-browser navigation is still required on the
  latest experimental artifact.

## Limitations and safeguards

- Only the approved retail PC menu-script shapes are supported.
- The native `normal-3` preparation stage is shared with unrelated
  menus. This build does not suppress its glyphs or infer ownership by
  their screen coordinates; a residual transition is still possible.
- Five data rows use a system font in a memory-only raster mask. The
  VIDEO title is the original animated native JFFTXT heading, retained
  in the verified title band only while the standalone page is visible.
  The 600x320 panel is centered in the 1024x768 canvas and has no
  Retour/Echap or automatic-settings row.
- GPU name comes from EnumDisplayDevicesA. On native Windows, SetupAPI
  supplies a more precise matched display-class device name and an
  installed DriverVersion only when there is one unique VEN/DEV match.
  Under Wine the host graphics driver version is reported as
  unavailable, never guessed from the Win32 emulation layer.
- On the main MENU normal-5 script-1 private clone, the original
  `[0..3]` selection becomes the native valid singleton `[1..1]`
  at authenticated V17/V19/V20 source nodes. The previous `[0..0]`
  attempt was not accepted by the native navigation helper (a Down
  key still caused a one-step movement). Original JFFTXT title
  parameters, V14 parent Options status, other menu branches and
  the animated butterfly are preserved. The game-thread cursor
  correction is only a defensive fallback. CI proves the patch
  preflight, not yet the visible elimination of the one-step movement.
- The information page now composites a subtle translucent midnight-blue
  background (alpha 100/255, 39%) over the live 3D scene using the
  approved Glide2 LFB read/write path. A crisp 2px title-blue border
  surrounds the white text; no asset files, butterfly actor or scene
  geometry are modified. The color and transparency are pending visual
  validation under Windows/Wine.
- The source-level normal-5 `[1..1]` singleton is independent from
  the late flip-time V17/V19/V20 fallback; it is validated from the
  original 324-node script before publication. Neither path changes
  the parent Options menu's selection or the original disk data.
- Glide RGB565 channel order is compensated (red/blue swapped).
  The blue outline and subtle dark-blue translucent background were
  approved visually by the maintainer on 23 November 2026.
- The GameFrame LFB read/write ABI is based on Glide2's documented
  eight- and seven-parameter exports, and must be checked under Windows
  and Wine before a release. On unsupported exports the original flip
  is forwarded unchanged.
- Native UI scene actor and Escape independence require in-game tests.
  CI compiles code and validates preflight policy; it cannot validate
  actual rendering or input under Rayman 2.

## In-game Pause graphics model discovery — 23 November 2026

The owner-supplied, SHA-verified original `Fix.sna` / `Fix.rtb` includes
a **distinct shared graphics controller**, with 23 normal and 6 reflex
behaviors, unlike the validated MENU controller with 27/7. Static
read-only inspection identifies the original normal-9 script-2 as
containing three `VID_SelectResolution` commands (procedure 356)
at nodes 40/50/60 plus video text handles 379 and 371 at nodes
71/101. Normal-15 script-2 has a separate guarded 356 call.

The experimental `pause_video_observer.cpp` now scans only committed
writable private process memory when the generic ESC menu flag is active
outside MENU. It accepts exactly one model only after matching normal
and reflex counts, each of these original script signatures, and
the reciprocal Mind/Intelligence pointers. It logs any verified
normal/reflex transitions, including a normal-9/reflex-0 candidate.
Its results are **observations, not proof of the actual on-screen
Pause → Options → VIDEO UI**. The probe never edits memory, intercepts
input, changes the display mode, or draws an information page.

A log from entering the actual in-game Pause → Options → VIDEO page
is required before a separate, script-verified read-only replacement
can be enabled. No in-game parity is claimed in the current build.

## Current acceptance checks (post 15:28, 23 November 2026)

- **MENU → Options → VIDEO**: Down/Up must not move the native focus
  even one row; Escape must return normally to Options.
- **MENU → Rayman2Fix**: clicking or Enter must open the default
  browser on the project home page. Script node 1836 is the primary
  URL redirect. IAT logging is only a fallback diagnostic.
- **Game → ESC → Options → VIDEO**: the standalone GPU/driver/output
  panel must appear without legacy resolution choices or brightness.
  Confirm the first-frame log `RUNTIME-PAUSE-VIDEO-PAGE: frame-rendered=1`,
  preserved pause music, original butterfly and Escape return.

These are **not** production release claims. The scripts and display
are opt-in on draft PR #52 only; main stays untouched.
