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

## Owner regression log 16:25, 23 November 2026

The owner confirms the startup VIDEO information panel and translucent
dark-blue background, but reports three recurring issues and one new
scene-reload failure: Down moves the original butterfly under the
panel, the site link does not launch the default browser, Pause → Options
→ VIDEO retains the native mutable options, and returning to main MENU
restores the original "Site Rayman2" caption.

The log identifies the shared 23/6 pause controller and actual in-game
VIDEO normal=9/reflex=2, but **549** repeated
`status=preflight-refused reason=original-script-signature` messages
mean **no** pause scripts were installed. The research code now reports
the exact failed behavior/script, expected and observed shape on the
first refusal, then stays fail-closed until a new MENU scene to avoid
noisy CPU/log loops. A second log is required to diagnose whether
the runtime changes the signed-off static script shape or one of its
guarded literals. Never weaken the source signature check blindly.

The visual butterfly movement was traced to the two source SetAGO
positioning commands (normal5/script4 nodes 3 and 15): the private
VIDEO script4 now ends before both commands, rather than preserving
their transforms under the info panel. Other menu scripts remain intact.
Native butterfly visibility and fixed position still need visual
validation.

The main-menu site IAT bridge is left forwarding for the lifetime of
the process and reauthenticated when a new MENU scene is loaded
(the old restore/reinstall sequence could fail to reattach it).
The site URL is still literal-redirected only in the script clone;
a small input monitor additionally requires an exact authenticated
main-menu 27/7 controller normal11/reflex0 and an explicit action:
Enter with verified V17=4, or a mouse click inside the site caption
band. It never launches on a hover, on another menu or while playing.
This fixes the dependency on the old original URL/browser action, which
did not reach the observed ShellExecuteA callback in the owner's tests.

All these changes are experimental on draft PR #52 and require
a single Windows manual validation with the next CI artifact.

## Owner log 16:49:31 — site action validated; paused VIDEO overlay regression

The maintainer visually validated the GitHub browser opening. The log
confirms `site-user-action=1` followed by
`browser-launch-dispatched=1`: **do not regress this**.

Three defects remain in this run:

1. In MENU VIDEO, pressing Down moves the animated butterfly behind the
   blue information panel, even though V17 is clamped to singleton 1.
   Original `Menu.sna` reflex0/script2 has the interpolated V70
   selection target (DsgVar scalar index70 offset0x22c). The new game-
   flip fallback checks the original 90-variable schema and clamps
   both V17 and V70 to 1, only while the verified MENU normal5/reflex0
   is active. Not yet visually validated.
2. Pause → Options → VIDEO shows the blue Glide page on top of the native
   VIDEO widgets (BASSE/RESOLUTION/HAUTE/SAUVER RESOLUTION/LUMINOSITE).
   The log confirms `RUNTIME-PAUSE-VIDEO-PAGE: status=installed`, so
   the former signature error was **not** the cause this time.
   Read-only original `Fix.sna` inspection revealed those widgets were
   already instantiated earlier by the distinct normal6/script2
   preparation branch, *before* normal9/reflex2 enters the page.
   The experimental branch now validates all 1303 nodes of this
   script (shape `0xd201f1c8bc668803`) and skips only the
   V16=0 VIDEO preparation sub-branch, preserving existing native
   normal9 behavior transition commands and all subsequent Options
   branches. The installation is atomic with the five prior script
   clones, with original bytes never modified. The manual test must
   check no old items remain and that ESC/return visits never show an
   empty VIDEO menu.
3. Returning to MENU after gameplay restores the original "Site Rayman2"
   caption. The log pinpoints `install-refused reason=site-script-validation`
   on every retry. The validated native row + system browser input
   monitor is already independent from legacy URL script cloning.
   The installer now treats the 1995-node old browser-script patch as
   optional, while installing the authenticated visible label and
   ShellExecuteA hook on each verified MENU reload. The old URL may
   still be intercepted and redirected by exact EXE callsite. Manual
   confirmation of returning to MENU and clicking the link is required.

All these source-only experiments stay in draft PR #52; production
`main` remains unchanged.

## Owner log 17:58:09 — confirmed fixes and native 2D overlay findings

On Wine (1920×1080, RX 6600M), the maintainer **validated** the
Rayman2Fix GitHub action and persistence of that entry after gameplay.
The latest log also confirms correct source-model discovery,
`RUNTIME-PAUSE-VIDEO-PAGE: status=installed`, and
`frame-rendered=1`, but the visual screenshot still shows **SON**
behind the blue panel and **CONTROLES** beneath it. These are
original **parent Options** captions, not just the abandoned resolution
choices. The earlier normal6/script2 cut bypassed native parent Options
construction and was associated with an empty return screen.

The next prototype **does not patch normal6/script2**, preserving
original ESC back/return navigation. Its five fail-closed VIDEO script
copies still neutralize native resolution/mode/brightness writes.
The approved JFFTXT glyph-render source hook now conditionally hides
parent Options text only while authenticated in-game normal9/reflex2
has rendered the blue Glide information panel at least once, retaining
the original VIDEO title and returning immediately to normal drawing
upon leaving this exact state. World sprites and the animated 3D
butterfly are not filtered. The source-based filter avoids deleting
text resources or unrelated menu branches.

Despite a logged V70 target clamp, Down still moves the native
MENU butterfly. Original Menu.sna source parsing confirmed that
reflex0/script2 uses DsgVar index **69**, type=5,
offset=0x228 as the interpolation **source**, then copies into V70
(index70/type5/offset0x22c) and V17. The prototype validates all
DsgVar index69 and index70 declarations, and clamps V69 alongside
V70 and V17 during verified startup VIDEO only. It does not intercept
global keyboard input, move any actor, or change other menus.
This visual fix remains unconfirmed until an owner screenshot/test.

All changes still require the experimental x86 CI and live validation;
do not merge into `main` on static tests alone.

## 23 November 18:16 Wine regression — in-game website and VIDEO back state

The owner's `Rayman2Fix(20261123-181651).log` (runtime
build `4910dba4`) confirms both a persistent MENU website caption
failure after returning from gameplay and a real in-game VIDEO transition
`normal7/reflex2 → normal6/reflex2 → normal9/reflex2` with
`frame-rendered=1`. The source V69/70 clamp logged but the maintainer
still observed one native butterfly movement from Down.

The next prototype independently authenticates **the original Fix.sna
normal7/script9**, 1282 nodes (source shape
`0xa318b1055ee76c9f`), with the native website action proc369 at
node1158 behind the verified DsgVar `V16=3` selector (86 declared
variables, V16 scalar offset `0x110`). Enter only on that exact
state and site row opens the Rayman2Fix GitHub homepage through
ShellExecuteA; no global input capture or unrelated menu site actions.
The original executable's unique authorized ShellExecuteA caller RVA
0x187267 also redirects from Pause, with a shared 1.5-second debounce
to avoid two browser tabs if the native action and source watcher both
dispatch. Mouse-click support for this distinct Pause menu is not
yet claimed.

The shared five-locales Fix.sna website caption at handle362 is now
replaced *in its verified live text buffer* as well as in the pointer
table. This affects only RAM, not commercial assets on disk, and
persists when returning from MENU into gameplay or into Pause. The
in-place replacement is attempted only when the original localized
string is authenticated and fits the smaller `/C:Rayman2Fix` buffer;
otherwise the previous safe pointer-only path remains available.

To avoid damaging the original ESC return logic, normal9/script1
initialization (including V16 and V17) is no longer modified.
Four separately source-signed mode/save/brightness action scripts
continue to prevent obsolete resolution changes in Pause.

The old Options labels in the paused VIDEO screen are partly rendered
as native **3D TextSuperImposed** and were not affected by the 2D
JFFTXT glyph filter. A second exact source filter is now applied only
when the blue Pause VIDEO page has actually drawn, only for the game's
approved UI viewport and original return RVAs 0x75C58 and 0x75C93,
and only below the preserved VIDEO title band. It shifts those
body-only native text vertices beyond the viewport without touching
game/world geometry, butterfly actors, normal Options navigation or
the new panel. This geometry and layout still need a real Wine test.

For startup VIDEO, the source-verified normal5/script1 JFFTXT text
focus argument has been changed to constant singleton index1 rather
than a live V17 read; this is an additional purely presentational
experiment and may still not suppress the butterfly movement until
the owner visually confirms it.

Everything remains opt-in to the draft research PR #52. The linked
artifact must be the Windows **native-graphics-menu-prototype**, not
the default bootstrap.
