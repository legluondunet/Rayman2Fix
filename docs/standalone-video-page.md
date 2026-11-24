# VIDEO information pages — standard Rayman2Fix build

The standard Windows x86 single-EXE installer includes the owner-validated
read-only VIDEO pages, without a separate research artifact. They work
with Debug.Log=0. The legacy passive resolution trace is restricted to
explicit research builds; authenticated controller discovery remains
required for the feature.

## Current behavior

- Startup Options → Video and ESC → Options → Video share a centered
  600×368 panel (X=212, Y=284 on the 1024×768 internal canvas).
- Six rows: output resolution/aspect, GPU, graphics driver, renderer,
  internal surface, and Rayman2Fix version-commit.
- Midnight-blue background, exactly 40% opacity (102/255), 2px blue
  border, 12px corner radius, 4× supersampled GDI text/edge coverage.
  The preferred 19px Segoe UI text adapts to the available width.
- Native VIDEO title and animated butterfly/luciole are retained.
  Obsolete resolution and brightness choices are removed only within
  authenticated VIDEO states. Escape returns to the original Options
  menu; unrelated controls and gameplay are preserved.
- The original website caption becomes Rayman2Fix during loading;
  authenticated Enter/click action opens the official GitHub repository.

## GPU and driver identity

Native Windows uses the live GameFrame GL_RENDERER name, with matching
PCI identities and SetupAPI display driver properties. For the generic
AMD Hawk Point renderer, the device and revision identify a known
marketing model (760M/780M), following AMD's libdrm amdgpu.ids entries.
Unknown or ambiguous identities are not guessed. PCI and chip diagnostics
stay in the log, not the panel. Under Wine, the existing matched host
Vulkan ICD path provides driver information when supported; unavailable
or unmatched versions are not invented.

## Implementation and safeguards

The verified native frame-flip hook draws a memory-only information
panel via Glide2 LFB read/write calls. It uses the live scene as its
background; no extra window or proprietary font is included.
The distinct MENU and Pause AI controllers are authenticated against
original script shapes; private script copies are published only after
preflight. Original commercial files are not modified by these runtime
menu changes. Obsolete native text, navigation and leaked brightness
images are scoped to the corresponding verified states. Unknown
scripts are left intact. No external GPU query command is launched.

The implementation preserves compile-time troubleshooting switches.
The old research-only resolution trace is not started in ordinary builds,
even with Debug.Log=1. Other diagnostic tools and historical research
are retained for troubleshooting rather than deleted.

## Validation

The maintainer approved the Windows test of build bab71107 on
24 November 2026. Its log confirms early handle-362 replacement,
AMD Radeon 780M (PCI 1002:1900, revision D0), driver match, 19px font,
six rows and the 40% rounded panel. CI run 45376070704 passed Windows
x86 build, GameFrame ABI audit, packaging and Linux/Windows tests.
The final review corrects the separate CF revision to Radeon 760M and
gates the passive resolution trace; this follow-up requires fresh CI.
Prior Wine validation covers the preceding menu implementation, not
every final Windows-specific adjustment. CI does not render the game.

## Historical research (23 November 2026)

The following notes document earlier experimental builds and test
results, not the current user-facing configuration.

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
