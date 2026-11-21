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
