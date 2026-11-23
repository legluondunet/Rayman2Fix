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
- The native normal-5 script1 is intentionally copied **unchanged**:
  it also controls original Options labels and changing it caused
  VIDEO to disappear from the parent menu. The page instead resets
  only obsolete V17=1..3 selections after native AI updates on the
  original frame thread, after repeated validations of MENU, the
  verified current normal5/reflex0 controller and the DsgMem schema.
  ESC and other menus are never modified; the CD files are untouched.
  The old normal-5 script1's JFFTXT title drawing is also left
  unchanged; its percent-glyph title row is normalized to fixed 20..25
  Y bounds only during the standalone page, retaining original textures
  and colors without the selected-row bobbing. Data rows now use
  RGB(255,255,255) in both GDI and Glide RGB565 composition.
- The information page has no background fill: the game continues
  drawing its live 3D scene. A 2px transparent blue outline (sampled
  median RGB 145,206,255 from the native title) surrounds the white
  text; no colored or dark rectangular panel.
- The verified normal-5 selection range is constrained to V19=V20=0
  at the native render boundary, before the next key evaluation, in
  addition to the already validated V17 correction. This is limited
  to the retail VIDEO range 0..3; all unrelated Options behaviors
  remain untouched.
- The GameFrame LFB read/write ABI is based on Glide2's documented
  eight- and seven-parameter exports, and must be checked under Windows
  and Wine before a release. On unsupported exports the original flip
  is forwarded unchanged.
- Native UI scene actor and Escape independence require in-game tests.
  CI compiles code and validates preflight policy; it cannot validate
  actual rendering or input under Rayman 2.
