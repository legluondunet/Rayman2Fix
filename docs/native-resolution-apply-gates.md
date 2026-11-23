# From menu selection diagnostics to a safe read-only Resolution page

Research status on 21 November 2026. This document contains only the
small native AI command IDs, scene addresses and technical conditions
from the owner-supplied, hash-locked Menu.sna. **No game resources**,
decrypted scripts or code bytes are stored in this repository.

## Repeat Wine result: classifying active menu choices

Owner-provided `Rayman2Fix(20261121-132219).log`:
- 37,694 bytes, 215 lines, SHA-256
  `6fe1342d83cb9352c90f868b9e746a90c3cf34cb9a95dcca1391b4d0cc7bd00b`;
- verified AI controller found on scan 2, exactly one model, valid Mind;
- 39 `t_ms` behavior snapshots, of which **18** are
  `video-choice-candidate` (normal 5/reflex 0; V17=1/2/3),
  **6** are `brightness-candidate` (normal 18/reflex 5; V17=5),
  **9** are `other-menu-candidate` (normal 11/reflex 0),
  **6** are `unknown` (normal 3/reflex 0; normal 18/reflex 1;
  normal 9/reflex 3);
- all 39 V17/V51 fields validated, V51=0 at each sample (a 200-ms
  periodic observer cannot rule out faster pulses);
- native GLI output: 45 snapshots, all `1024x768x16`, selected mode 5
  of 8. **This is the internal game video mode, not the desktop
  resolution; it must not be displayed as the physical monitor mode.**
- no runtime observer `status=finished` line; this log alone cannot
  establish a clean process exit.

**Decision:** the advisory classifier from `c3e6db03` is useful for
observing the menu, with `unknown` correctly retaining transient
states. It is not, by itself, a safe interception of native game
procedures. Repeating the same navigation-only diagnostic is unnecessary.

## Native video-selection expressions in PC Menu.sna

The script `normal[5].script[3]` starts at scene VA `0x0792F210`,
and contains three branches with the *same AST structure*:

| V17 equals | V51 equals | Procedure node | Argument | Label |
| --- | --- | --- | --- | --- |
| 1 | 1 | `0x0792F350`, proc ID 356 | 0 | `VID_SelectResolution` |
| 2 | 1 | `0x0792F3A0`, proc ID 356 | 1 | `VID_SelectResolution` |
| 3 | 1 | `0x0792F3F0`, proc ID 356 | 2 | `VID_SelectResolution` |

The three equality comparisons against V51 use script nodes
`0x0792F340`, `0x0792F390`, `0x0792F3E0` (var ID 51, kind 11).
Their sibling constant nodes immediately before the video procedure
have kind 12, value 1, scene VAs `0x0792F348`, `0x0792F398`,
`0x0792F3E8`.

Other video routes need separate treatment:
- `VID_SelectResolution` is also referenced by normal[19].script[2]
  at scene VA `0x07950794`. **Normal 19 has not been observed**
  in the known three Wine sessions. Do not assume it is unreachable.
- `VID_SaveCurrentResolution` (function ID 261, kind 3) occurs
  at script node `0x0792FF70`. Its full execution condition and
  side-effects are **not yet established**. It must not be considered
  protected merely because the three selection calls are guarded.
- Brightness is independent: reflex[5].script[2] includes
  `VID_ChangeBrightness` (procedure ID 357) at `0x0795AF30`.
  This feature and all unrelated menu controls must remain functional.

The scene VAs are **NOT** stable runtime pointers. Any implementation
must use the already-verified loaded AIModel -> behavior -> script
reference chain and validate all node types, IDs, arguments and
surrounding tree shapes in memory. Failure to validate must leave the
game unchanged.

## Implementation acceptance criteria

A first *functional* research build should:

1. Display genuine detected output information (adapter, reported
   driver, desktop resolution, desktop color depth, and 4:3/16:9
   aspect) in the original graphics Options screen, using the game's
   existing text/geometry presentation rather than an unrelated popup.
   Clearly separate physical output from the game's 1024x768x16
   internal Glide mode. Handle the distinction between Wine's
   emulated D3D9 adapter/driver and the display adapter reported
   by the OS, without inventing Linux/Mesa driver versions.
2. Preserve original navigation and the brightness behavior.
3. Prevent only the original **menu-originating** selection and
   save/apply operations. No global hook on
   `GLI_DRV_hChangeMode`, since initialization still needs it.
4. Fail closed for unknown EXEs, model signatures, menu states,
   invalid pointers and unsupported translations. Do not modify
   copyrighted CD files on disk or bundle them.
5. Confirm in Wine **and** native Windows: resolution remains
   unchanged even if the user tries to activate the old controls;
   all other settings and return-navigation work. UI visibility,
   text fit and audio correctness require observation beyond CI.

**Nothing in the current PR implements this user-facing screen or
blocks these procedures yet.** The current implementation consists of
offline structural research and read-only diagnostic prototypes only.

## First real UI prototype under Wine: safe refusal, 21 November 2026

The owner tested GitHub Actions artifact `14709068179` from commit
`70c59546ff5b2cde9e1ce863598a1414e5abea7f` and supplied
`Rayman2Fix(20261121-134743).log` (38,793 bytes / 226 lines,
SHA-256 `1608f675df84355048c4dbf87c55ef57362bd81106628b3b1ed734e379d0a2f3`).

The research module started, the script-verified native Menu controller
was found on scan 2 (normal 5/reflex 0 V17=1/2/3, reflex 5 brightness),
but the hook did **not** install: there are six
`status=callbacks-unavailable original-menu-retained` lines,
no `status=commands-guarded` and no `status=native-text-installed`.
Engine mode stayed 1024x768x16. The runtime log lacks the launcher's
exit code and cannot prove clean process termination.

**Exact static executable verification:**
the previously owner-supplied approved PE32 executable `Rayman2.exe`
(size 1,468,928; SHA-256
`9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`)
contains selector slot RVA `0xA4F00` → VA `0x46FE00`, save-function
slot RVA `0xA5C88` → VA `0x47AC70`. The first 15 bytes at the
two functions and the evaluator at VA `0x480B90` match the prototype
byte checks exactly. These facts do **not** prove the same pointers and
memory protections in a running Wine process.

The first prototype incorrectly allowed only PAGE_READWRITE and
PAGE_EXECUTE_READWRITE in `writable_slot`; the project's tested
F248 slot policy also permits PAGE_WRITECOPY and
PAGE_EXECUTE_WRITECOPY. This is a likely but **unconfirmed** reason
for refusing the callback under Wine.

The follow-up prototype accepts the same four writable memory protection
classes, verifies the 4-byte slot fits fully in the queried committed
memory region, and logs a **specific reason** for failed preflight
(e.g. `select-slot-protection`, `normal5-save-script-mismatch`,
`save-slot-target`). Intercepts additionally revalidate the current
model and active behavior on demand. If validation still fails,
both original dispatch entries and original native text remain unchanged.

**Do not ask the user to activate any resolution entry yet.**
First require the specific `commands-guarded` and
`native-text-installed` markers and a screenshot confirming the
native presentation. The only allowed first test is navigation.


## Follow-up experimental glyph layout (21 November 2026)

The owner tested research build `30d98abd` (log
`Rayman2Fix(20261121-140005).log`) and provided a 16:9 Wine screenshot
of the video options. The native dispatcher guards and text replacement
installed successfully, but the unmodified 1999 UI widget geometry
showed enormous intersecting and offscreen information strings.
The counters `video_actions_blocked=0`, `saves_blocked=0` **do not
prove video mode protection**; they only show no captured action.

The follow-up branch remains **experimental and unmerged**:

- `native_graphics_info_proto.cpp` protects exact video-script calls
  independently of whether its text UI can render; the original
  brightness function 357 remains forwarded.
- Localization handles 379–384 are now temporarily substituted **only**
  while a validated native Menu controller reports normal behavior 5,
  reflex 0, V17=1..3 and V51=0; original language pointers are
  restored when leaving that state. This is a verified *video-choice
  candidate*, not a complete proof of every original submenu edge.
- The already-installed native glyph hook optionally transforms the
  existing game's JFFTXT character quads into a five-row 4:3-safe grid.
  The source percent-coordinate bands come from the screenshot and
  the native glyph diagnostics. **Position bands are provisional**:
  they are only applied for the validated state and must be checked
  visually on Wine and Windows. No game binaries, scripts, textures,
  fonts or driver callbacks are modified on disk.
- `Rayman2Fix_Initialize` supplies the actual launcher output surface
  dimensions to the information module. Internal Glide remains
  1024×768×16 and desktop depth is explicitly labeled separately.
  Output ratio 16:10 retains its conventional label.
- Strings have a strict 35-byte native-line cap. This may abbreviate
  long reported GPU/driver names until a genuinely resizable
  native-font panel is established; it is *not* a finished information UI.
- Native brightness glyphs/slider below the original 80%-Y boundary
  are never reflowed, and other menu states disable text substitution
  and layout transformations.
- Pure Windows/Linux tests cover row mapping, screen bounds, brightness
  exclusion, unknown Y bands and invalid quads. The runtime logs
  `reflowed_glyphs` so an unchanged UI is distinguishable from
  a successful glyph-hook invocation.

**Next test (navigation only, before any video-control activation):**
Install the special `Rayman2Fix-Windows-x86-native-graphics-menu-prototype`
artifact after CI passes. With `[Debug] Log=1`, visit Options →
Résolution and take a screenshot; check line order, spacing, aspect
and the persistent brightness control. Then back out, visit brightness,
return to Resolution, and exit. Do not select/save a video resolution.
Inspect `RUNTIME-GRAPHICS-INFO` markers for an installed menu guard,
scoped text replacement and nonzero `reflowed_glyphs`. Failure to
see these markers means the displayed result is not evidence of
the new position transform. Follow with native Windows tests and only
then a separate guarded no-mode-change behavior test.

The screenshots and runtime traces are the authority for actual menu
layout; CI alone cannot establish it.


## Wine feedback — V51 latch and premature restoration (21 November 2026, 14:56)

Owner log `Rayman2Fix(20261121-145638).log` from the preceding native-graphics prototype (runtime build `ffc3cf77`) and direct visual observation:

- `status=native-text-installed ... scope=video-choice-only` at ~6s, when verified Menu normal=5/reflex=0, V17=1, **V51=0**.
- At ~9.6s, after a navigation Enter edge, the same verified normal=5/reflex=0/V17=1 has **V51=1**. Previous code incorrectly required `V51==0`, removed the substituted labels, and disabled its JFFTXT glyph layout.
- First status at 10s: `native_labels=0 video_actions_blocked=1 saves_blocked=0 original_video_calls_forwarded=0 reflowed_glyphs=19378`. The intercepted video-selection command **was blocked**, but the labels disappeared and the glyph count stopped increasing.
- Later returns to normal=5/reflex=0 at ~16.9, ~23.9, ~40.9s still have `V51=1`, so text substitution never recovered. This **proves the previous predicate was not stable across normal menu navigation**.
- V17=4 is reached in normal=5/reflex=0 (at ~27.7s); brightness remains a separate normal=18/reflex=5 state (~28.5s). Internal GLI remains 1024x768x16, index 5, throughout this session. That is evidence of no mode change observed in this run, not a universal guarantee.

### Changes in the research branch

- `graphics_info_video_page` now requires a verified PE, the MENU level and validated AI controller, normal=5/reflex=0, V17=1..4 and a **validated V51 in either {0,1}**. It does not assume V51=0. Any unknown V51 or unrelated normal/reflex state fails closed. Separate passive scope classifier updated consistently.
- The original localization strings are still restored on a *true* exit from the normal-5 video behavior (including brightness and other menus), and on unload. They are no longer restored just because the in-menu action latch became 1.
- Diagnostic events `status=video-page-visible row=... latch=... return_visit=...` and `status=video-page-exit` make it possible to verify continuous display after Enter and correct restoration on true exit.
- New policy/controller tests include V51=0/1, V17=1..4, re-entry with sticky latch=1, and rejection of brightness/other menus and unknown inputs.

**Caveat:** This is still the original interactive native menu underneath the informational labels. Only the exact script-level resolution-selection and save hooks are guarded; avoiding unintended menu navigation requires a separate, carefully validated native behavioral change. Do not describe the page as a finished non-interactive UI or merge to `main` based on synthetic tests alone.

**Next Wine test:** open Options → Résolution, verify the information screen and its layout, press Enter once on one original selection **only after confirming the research guard is installed**, and check that the information remains visible and `video_actions_blocked` increments without a mode change. Exit to Options, visit Brightness, reopen Resolution; verify information returns even with V51=1. Supply `Rayman2Fix.log` and two screenshots. Check actual behavior and 4:3 geometry on Windows after Wine.


## Offline PE disassembly and native material scope (23 November 2026)

**Approved user-provided executable**: `Rayman2.exe`, 1,468,928 bytes,
SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`.
It matches the established PE fingerprint (timestamp `0x3807B68B`,
six sections, entry RVA `0x5FCC0`, image size `0x1D7000`).
Disassembled in memory-independent, read-only mode using `objdump -d -M intel`.
No game executable or original code bytes are included in the repository.

At approved EXE VA **0x429670** the native
`GLI_vDraw2DSpriteWithPercent` source function constructs screen-space
vertices and, for its *colored* path, constructs a separate color argument
before calling the stored callback at **0x504838** (VA **0x4297E6**).
The path reads native color channels from globals near
`0x503748..0x50374A`; alpha/other values come from the current
render state. This explains why a previous *vertex-only* opaque-white
experiment did not remove the screenshot's colored native option labels.
**A pointer to the colored callback's second argument must not be
treated as a proven 4×ARGB array yet:** the layout and draw semantics
need direct runtime evidence. Blindly overwriting it could corrupt
geometry or draw state.

**Next controlled research build:** the already-verified native GLI font
callback reads the first 16 bytes of that otherwise opaque secondary
color argument using a bounded, protected process-memory read. Each
sample is explicitly labeled with (1) the original native glyph source
band (four known information rows), and (2) the script-validated
native V17 focus row (1..4), to a maximum of **16 log lines total**.
Markers are `RUNTIME-GRAPHICS-MATERIAL: focus=N row=N raw_dwords=...`.
No pointer/address is logged, no color is changed, and none of the
original navigation, brightness, butterfly, text restoration or
resolution-action guards are touched.

These observations will establish whether color data tracks focus and
whether it can be safely cloned at the original renderer boundary.
Even matching material-color values **would not by itself** disable
native selection: the final informational screen must also avoid
iterating active buttons while preserving the original brightness
and return behaviors. Do not call the current prototype
non-selectable until that property has been observed in-game.


## Experimental static VIDEO information scripts — 23 November 2026

Using the owner's validated `Menu.sna` and `levels0.dat`, the
verified normal behavior 5 has five native scripts. Two presentation
scripts end in `fn_p_stJFFTXTProcedure` (PC Raymap function/procedure
index 506) with first parameter `V17`, the original keyboard-driven
selection scalar. In the decoded approved resource, they have:

| Normal 5 script | Full node count | Final JFFTXT call node | Normalized script FNV64 |
| --- | ---: | ---: | --- |
| 1 | 324 | 313 | `b363803e1ef0a4df` |
| 4 | 517 | 512 | `e3f194a21d7d5471` |

The fingerprints cover EVERY 8-byte node (parameter, padding, depth,
type), zeroing parameters only in Raymap pointer-bearing node types
14,23,28,30,32 that the CD's scene loader relocates. The four
JFFTXT argument nodes, terminal type-8 node and two full fingerprints
must all match before any replacement.

**Opt-in prototype on PR #51, not a release change:** allocate two
private memory-only COPIES of those two scripts and replace only the
first JFFTXT argument's typed `V17` node with a literal zero. Then
atomically swap the two corresponding normal-5 script list slots
while the approved executable, exact Menu model and native VIDEO
behavior have been verified. At the first transition out of VIDEO,
restore the exact original pointers using compare-and-swap; the
original game SNA and executable remain unchanged on disk.
Never free a once-swapped script copy while game threads may still
be referring to it. The whole operation refuses if signatures,
pointer validity or script-slot protection differ; rollback retains
ownership of any partial swap. The existing video-selection and save
guards remain installed; brightness's separate reflex-5 script and
the decorative butterfly are never modified.

**Important limitation:** `V17` and original up/down navigation
remain native. This is an *isolated highlight/presentation
experiment*, not proof that the information rows have ceased to be
selectable. Literal zero may blank a native JFFTXT element or produce
a different color. An in-game Wine screenshot/log is essential
to validate that the four info lines remain visible, fixed and
unhighlighted, that brightness/return work, and that exit/reentry
restores the exact original menu behavior. If anything is abnormal,
revert this experiment rather than manipulating V17 or a cursor actor.


## User Wine #1634 results and safer palette-only follow-up (23 November 2026)

User screenshots `image(20261123-055759).png` and
`image(20261123-060026).png`, runtime log
`Rayman2Fix(20261123-060109).log` (build `77fe3ac1`):

- VIDEO shows the correct OUTPUT/GPU/D3D9/GLIDE information grid, but
  the original yellow/blue/pink choice colors still change with
  up/down selection. The butterfly is visible.
- LUMINOSITE is clean, without giant cached VIDEO text. The slider and
  butterfly are present, and the internal mode stays `1024x768x16`.
- The script-clone path **refused preflight** once:
  `status=static-information-script-preflight-refused`. Therefore
  `static-information-script-copies-installed` was never emitted;
  no visual outcome can be attributed to that experiment. The exact
  preflight subcondition remains unobserved; do NOT weaken guards
  or assume the cause. No video mode changes were attempted (blocked
  selection/save counters remain zero).
- The material trace confirms the separately transmitted colored-GLI
  palette: for focus 1..3, a glyph's unselected edge color is typically
  `0xFF6EBEFF`, while the native focused row uses `0xFFFFFF78`.
  The native red row has `0xFFFA4646`; the OUTPUT row uses
  `0x5A` source alpha. The opaque-white *vertex-only* tint was
  ineffective because the downstream color pointer supplies these
  colors independently.

**Active follow-up in the experimental special artifact**:
the script-clone installation was **reverted from the runtime**.
The source-level script fingerprint policy stays as offline research
with synthetic tests but is not called in production or experimental
runtime. Instead the approved GLI colored-glyph hook copies the
observed four-element secondary palette into a local stack array
and replaces the four colors with one uniform white source-edge
color, preserving its original alpha. Scope is verified MENU VIDEO
information glyphs, one of four source rows, a validated native
V17 focus 1..4, the existing reflowed-glyph TLS provenance, and
an observed identical-pair color structure. Anything else is passed
through untouched. First successful marker is
`RUNTIME-GRAPHICS-STYLE: status=static-information-palette-active`.

This color transformation does **not** suppress the game's underlying
V17 navigation. The prior `VID_SelectResolution` / save guards
remain, so selecting a fake option should not apply a resolution,
but full keyboard non-selection cannot be claimed solely from
the rendered static palette. The next Wine test should check:
constant text tint across up/down, native brightness and return
unchanged, butterfly present, unchanged internal resolution. Do
not merge PR #51 without observing these properties and deciding
whether the remaining internal V17 movements are acceptable or
must be eliminated by a separate, verified input-state mechanism.


## Experimental VIDEO information persistence during brightness (23 November 2026)

Owner observation from `Rayman2Fix(20261123-062939).log` and the two
screenshots taken at 06:28/06:29: the four rows are white and aligned
in VIDEO, but keyboard up/down still changes the native V17 focus; all
four information rows disappear on selection of LUMINOSITE.

The disappearance was explicitly caused by the worker restoring
localization handles 379..384 and disabling glyph reflow when the native
intelligence switched from normal=5/reflex=0 to normal=18/reflex=5. This
behavior was deliberately introduced after an earlier giant-glyph
regression during brightness; the scoped stale-video-glyph filter
subsequently prevented that regression.

**Next guarded visual experiment:** if and only if VIDEO has already
installed the six native information text pointers and the approved
MENU controller validates normal=18/reflex=5/V17=5, leave those text
pointers installed and keep the existing information glyph reflow and
fixed-color palette enabled. Do not suppress these verified glyphs on
this brightness behavior. The native brightness string handle 385,
slider, input, actor/butterfly, and script logic are unchanged. The last
validated VIDEO V17 (1..4) remains solely a *snapshot* for the font
color path, not a game-state write. On any other menu state or level
unload, restore the original text pointers and disable the panel. If
the game fails to draw cached information glyphs during brightness,
this change will not synthesize them and the panel remains absent.

Diagnostic marker:
`RUNTIME-GRAPHICS-INFO: status=brightness-information-panel-retained`.
CTest covers all nine disqualifying conditions. The new experiment
must not be described as achieving non-selectability: V17 still changes
with up/down on the native VIDEO page. A genuine static page requires
separating the data from native selection widgets. Do not merge PR #51
without visual verification under Wine and Windows.


## Read-only script-preflight evidence (23 November 2026)

The earlier two-script `JFFTXTProcedure` clone was rejected by a
combined preflight. The exact predicate was never logged, and matching
offline SNA fingerprints does not prove that all live pointers and
memory protections are compatible. A new **read-only** diagnostic runs
once per verified VIDEO visit, logs no process addresses, copies at most
517 script nodes into temporary memory, and checks independently for
the two original normal-5 scripts:

- native script-list count and pointer validity;
- slot writable protection (observed only);
- 8-byte node array readability;
- relocation-normalized complete script fingerprint;
- exact terminal JFFTXT call shape.

The `RUNTIME-STATIC-SCRIPT-DIAG` markers report
`slot_write`, `ptr_valid`, `bytes`, `hash`, and `shape`.
There are no game-state writes or swaps, and the old script-clone
installation remains disabled. A future targeted correction must be
based on observed individual failure predicates, not on weakening
preflight as a whole.


## User regression #1643: late installation, dim output, focus still active (23 Nov 2026)

Attached screenshot `image(20261123-071228).png` and log
`Rayman2Fix(20261123-071343).log` (runtime build `235354b3`):

- The original resolution menu is visibly shown for at least about
  one second before the VIDEO information text takes its place.
  The runtime log records `RUNTIME-MENU-AI: scan=1` without a match
  and `controller-found scan=2` at `t_ms=7937`; only then do
  `native-text-installed` and `video-page-visible` appear. The
  former 5-second inter-scan delay explains a substantial part of
  this delay (it does not yet prove flash-free startup).
- The first OUTPUT row remains grey while GPU, D3D9 and GLIDE are
  solid white: the source font palette for its old native disabled
  option was `0x5AFFFFFF` (alpha 0x5A), and the earlier invariant
  palette intentionally preserved that alpha. Fixed text is not a
  disabled choice, so all four rows now use the same opaque white
  copy `0xFFFFFFFF`, never modifying the game's own palette.
- The native input focus still traverses `V17=1,2,3`, then enters
  brightness via `normal=18/reflex=5/V17=5`. All four rows can
  therefore still be targeted invisibly. The white palette removes
  only their highlighted colors, NOT their selectable behavior.
- `brightness-information-panel-retained` confirms the code keeps
  information localization pointers during brightness, and the
  screenshot shows the four lines remain visible above the native
  slider. The butterfly remains present.
- `RUNTIME-STATIC-SCRIPT-DIAG` proves both script slots writable,
  node array pointers valid and all bytes readable, but reports
  `hash=0 shape=0` for the two original scripts. The old
  `shape` field conflated signature and tail validation: the
  diagnostic now prints a separately computed terminal JFFTXT
  `tail` flag, the normalized `observed_fingerprint`, and the
  terminal opcode/typed argument, with no in-game writes and no
  raw process addresses.
- The internal renderer mode remains `1024x768x16`; the
  intercept counters are zero, so no user-initiated mode changes
  were attempted during this run.

### Limited next prototype

Only two behavior adjustments are applied: the first three
bounded model-scan retries run at 750 ms rather than 5000 ms,
and all four information rows render opaque white in copied
GLI materials. The memory search budget (160 MiB), full script
validation, native brightness and butterfly, and
`VID_SelectResolution`/`VID_SaveCurrentResolution` protections
are unchanged. CTests cover scan cadence and opaque palette.

This reduces flash time and fixes the visually disabled first line,
but neither change can remove native keyboard focus. Any future
script/input modifications must await the expanded live preflight
evidence, and must not be presented as a nonselectable UI before a
real in-game validation.


## Native VIDEO disabled-choice experiment — 23 November 2026

The maintainer's #1652 screenshot (07:55) confirms the four
information rows render uniformly white and the 750-ms scan retries
locate MENU AI at ~3.84 s instead of ~7.94 s. The original menu still
appears briefly before the information pointers are installed; do
not assert flash-free startup. `V17` still walks 1→2→3: the screen
is not nonselectable.

The approved `menu.sna` defines normal-5 script #3 (430 ScriptNodes)
and script #4 (517 ScriptNodes). Both initialize the native
`V14[1..3]` resolution choice states to value **2** (enabled) and
later assign **3** (highlighted) to the current resolution entry.
The native Save resolution choice has been observed in state **1**
(disabled, drawn grey and skipped in actual user input). A new
memory-only, **shape-locked experiment** replaces exactly the six
2/3 state literals for V14[1..3] in each of the two script COPIES
with state **1**. The native Luminosity entry V14[5] is left in
its original state, all `V17` inputs remain native, and no text
buffer or butterfly instruction is rewritten.

The two full script structure fingerprints (each node's 16-bit
padding, nesting depth and kind; excluding pointer/scalar values
that relocate in memory) must match the source-approved originals:
script3 `f4cc82cd7d6a21a4`, script4 `d6fbfd33f17ec60b`.
Each target also validates its exact `Operator_Affect ->
Operator_Array V14[index] -> Int(2 or 3)` typed AST, index and
indent, and terminator, *before any copy is changed*. If any
check fails, the original game runs unchanged. The worker attempts
script-list CAS only when the verified native controller is **outside
normal-5** execution. The copied allocations stay alive to avoid
in-flight interpreter use-after-free. If a swap partially fails,
the old script-list pointer is conditionally restored and broader
VIDEO 356/F261 dispatcher guards remain until safe completion.

**Critical video-write safety:** script #3's `VID_SelectResolution`
and `VID_SaveCurrentResolution` child pointers move when cloned.
The hook's exact child references are updated to the cloned
addresses before removing the temporary broad MENU dispatch guard;
the original normal-19 resolution mode hook is untouched. Without
this retargeting, cloning the script would have created a dangerous
mode-write bypass.

**This is a hypothesis-driven test, not yet a validated fix.**
It assumes that state 1 is used by the engine to exclude unavailable
options from keyboard selection (as seen on the original grayed
Save item). Whether all three choices are skipped in the native
menu, whether Luminosity remains reachable and whether the initial
text replacement transition stays clean require a Wine game test.
The preceding `JFFTXTProcedure` full-script hash mismatch is
not treated as proof that this different six-literal patch works.
Expected markers are `disabled-options-copies-installed` or the
explicit `disabled-patch-refused` / `script-shape-mismatch`.
Do not merge PR #51 before validated screenshots, key traversal,
brightness and return, and a log confirming that the game mode
remains unchanged.


## User validation of #1667 and one-entry VIDEO navigation follow-up (23 November 2026, 08:43)

**User log:** `Rayman2Fix(20261123-084317).log`, build
`73ed4850` (experimental native-menu CI #1667). There was no
accompanying image or written visual verdict for this run.

Observed evidence:

- `controller-found scan=2` around t=3359ms; patch installation
  succeeded at the verified native MENU controller in normal=6:
  `disabled-options-copies-installed scripts=1,3,4`.
  Therefore a missing or rejected script patch is **not** the cause
  of the remaining navigation.
- The replacement text pointers were installed at the true
  normal=3/reflex=0/V17=0 preparation state (t=8406ms), before the
  normal=5 VIDEO state (t=9406ms). This is a successful *code-path
  observation*, not proof the transient original menu disappeared.
- The native UI still traverses V17=1 and V17=3 (as well as
  V17=5 in brightness normal=18/reflex=5). Disabling V14[1..3]
  to state 1 in scripts 1,3,4 therefore does **not** make those
  choices unreachable. The original brightness and return
  transitions remain visible in the logs, and the video driver
  stays 1024x768x16. Blocked mode/save counters stay zero:
  no actual mode change was attempted, so guard interception
  was not stress-tested.
- The full script fingerprint diagnostic continues to mismatch in
  **copied** scripts 1 and 4, while their terminal JFFTXT call
  still validates. The full hashes are dynamic/relocated and are
  not used to authorize this independently verified shape patch.

**Offline SNA research:** In the user's original validated MENU scripts
3 and 4, the assignments `V19=1`, `V20=5` bracket the
native list of selectable entries. A separate original MENU script
uses a valid `V19=V20=1` one-item range, showing equal bounds
are an established native pattern. Both VIDEO scripts also assign
`V17` to each detected/current resolution index 1,2,3, and
script 4 restores a previous focus from `V73` upon re-entry.
These are distinct from the V14 rendering/disabled-state bytes.

**New experimental memory-copy policy:** on top of the existing
V14 state-1 changes, apply strict AST-validated adjustments to
scripts 3 and 4 only:

- `V19=1` becomes `V19=5`; `V20=5` is checked and unchanged;
- each of the six verified initial/current-resolution `V17=1/2/3`
  assignments per script becomes `V17=5`;
- script 4's verified `V17=V73` restore expression becomes the
  constant `V17=5`, eliminating a stored old resolution choice.

This does **not** run a background loop that forces V17; the native
one-entry range and initial focus are authored entirely in private
script copies. Their complete type/depth/padding shape hashes,
all typed assignment opcodes, destinations, literal sources and
nesting levels are validated before the first local copy mutation.
The changes fail closed; original scripts remain on disk and in
their original process memory unchanged. The brightness slider's
native reflex behavior, the butterfly, return controls and the
protected mode-selection dispatcher remain untouched.

**Limitations:** We have not observed how the native Rayman 2 AI
interpreter processes a `V19=V20=5` VIDEO state. CI only validates
the script-copy transformation, *not* actual keyboard non-selection.
The next single Wine test should verify no V17 transitions to 1..4
after entering VIDEO, immediate Luminosity accessibility, proper
return, and unchanged video mode. An unresponsive/hanging menu is
a failure: revert the experiment and retain the #1652 visual
baseline. PR #51 must not merge before this live validation.


## User scope change and #1672 regression (23 November 2026, 09:16)

User explicitly decided **not to retain the brightness adjustment** in the
Rayman2Fix VIDEO screen. The new target is a **read-only graphics information
page** (OUTPUT, GPU, D3D9, GLIDE), retaining the original animated yellow
butterfly and ordinary return-to-Options handling. Arrows/confirmation must
not select or alter graphics settings; the old slider, `LUMINOSITÉ` label
and legacy mode choices need not appear. No new INI toggle is requested.

The provided screenshots `image(20261123-091443).png` and
`image(20261123-091458).png` reveal the regression: an old
`RÉSOLU...` / `SAUVE...` overlay appears on one state; the other
shows only `LUMINOSITÉ` with its slider. The butterfly is present in
both. The log `Rayman2Fix(20261123-091616).log`
(`build=33dd635f`, prototype #1672) establishes:

- `disabled-options-copies-installed ... navigation-range=5..5`
  succeeded at ~3.39s; `pre-video-information-text-installed` at
  ~4.64s, then native VIDEO `normal=5` at ~7.64s.
- Native selection still intermittently sets `V17=3` in `normal=5`
  and `V17=5` in the actual brightness state
  `normal=18/reflex=5`. Each transition can call
  `video-page-exit original-label-pointers-restored` and reinstall
  localization pointers, explaining the changing menu appearance.
- Native video mode remains `1024x768x16`. The video-mode change
  and save counters are zero: no attempted mode write was observed.
- This is **not** fixed by changing V14 entry states or by narrowing
  V19/V20 alone. Do not claim keyboard navigation is disabled.

### Exact native transition discovery

The approved original `menu.sna` reveals the small **normal-5
script #2**: 33 typed eight-byte nodes. Its leading condition checks
`Cond_Equal(V17,5)` at nodes 1..3, and its trailing instruction
calls `Proc_ChangeMyComportAndMyReflex` (procedure index 71)
at node 29 with target behavior references to
**normal 18** and **reflex 5** at nodes 30..31. These are the precise
native state transitions causing the obsolete brightness slider to
replace our read-only data. The complete type/depth/padding structural
fingerprint of script 2 is `9902892c1e0da3c5`.

### Next experimental fix: no brightness transition, butterfly preserved

- Extend the earlier fail-closed runtime script copies from normal-5
  scripts 1,3,4 to **1,2,3,4**. Before publishing any pointer, verify
  the whole script-2 structural fingerprint, the exact
  `Cond_Equal(V17,5)` AST and the procedure-71 normal18/reflex5
  live target addresses. In the private script copy change **only**
  the condition literal `5` to an unreachable unsigned index
  `0xFFFFFFFF`. Original asset bytes and all butterfly actor
  instructions remain unchanged; no background V17 forcing.
- Keep the existing mode-write guards during CAS of all four script
  pointers and retarget script-3 child hooks to the new copy's address.
  Retain a once-published clone to prevent possible use-after-free.
- Hide text handle **385** (native obsolete brightness label)
  by substituting a single blank-space string in the VIDEO-localized
  pointer table, with exact compare-exchange restoration on exit.
  **Only hide it if the script-2 brightness transition was
  successfully suppressed.** If preflight fails, leave the
  original brightness option visible rather than having a hidden
  still-clickable setting; earlier information labels still use
  handles 379..384.
- Leave the original 3D scene, native butterfly and return mechanisms
  alone. The brightness-specific reflex procedure 357 is not patched
  because its entrypoint should no longer be reachable from this page.
- Tests include complete 33-node structural shape verification,
  typed operands, live behavior target references, preservation of
  every untouched node, and atomic refusal for altered conditions,
  targets, or node layout. These are synthetic fixtures, not bundled
  copyrighted game scripts.

**Important limitation:** suppressing this *known* transition should
stabilize display across arrow presses, but this does not by itself
prove the original menu's keyboard focus scalar V17 stops changing
internally. A real-game screenshot/log must establish that the panel
remains visually constant, that Enter never opens brightness, the
butterfly is animated, and Escape returns normally. No merge of PR
#51 until this validation. If another path still enters normal18,
investigate it separately; do not hide all UI or suppress unknown
GLI quads by generic geometry.
