# 12 November 2026 — offline SHA-locked original CD executable analysis

The **complete user-supplied retail unprotected CD EXE** (SHA-256
`7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`)
was recovered from the user's previously shared files and analysed
OFFLINE, without relying on a different GOG build or requesting
another user test. Original PE image base is `0x400000`.

The 8-byte native script-procedure dispatch records identify
**443 -> VA 0x470270**, `AI_E_proc_TextSuperImposed`.
That function evaluates its script parameter and writes a mode byte
at VA `0x4A40B4`. The original text superposition routine at
`0x475BB9` reads that mode byte to select the native 3D text
conversion path. This establishes a **direct implementation link**
between procedure 443 and native projected text, but not the
identity of the visible blue arrow.

All FOUR original call sites to
`GLI_vGet3DVertexFromScreenPos` at VA **0x422940** were enumerated:
- `0x4711F6`: procedure 409 actor placement.
- `0x47189B`: independent native superimposed actor setup.
- `0x475C53` and `0x475C8E`: both native text superposition
  conversion branches.
All pass the *original* dedicated UI viewport at VA `0x5004D4`.
The routine performs an inverse projection using viewport dimensions
and the screen camera: narrowing only the camera viewport can be
compensated by this conversion, leaving source objects misplaced.

The same approved EXE contains four native actor/text matrix extent
operations, all independently byte-verified:
- `0x47122A`, `0x4718CF`: `fmul [0x49CC58]` (1/640),
  actor X/Z extents.
- `0x475BEF`: `fmul [0x49CCA4]` (1/640), projected text X extent.
- `0x475BF7`: `fmul [0x49CCA8]` (1/480), projected text Z extent.

The original constants (1/640, 1/480) were read directly from the
PE .rdata section and are **not** globally overwritten.

## One guarded, experimental universal source-stage correction

New runtime prototype, in this draft PR:
1. Match the original CD's exact six-byte source-function prologue
   and ALL four original `fmul` instruction signatures, after
   the already-approved camera hook verified the PE.
2. Install a single source-entry trampoline for native
   `GLI_vGet3DVertexFromScreenPos` and map normalized
   `X -> 0.5 + (X-0.5) * hud_scale` exactly once, **only**
   for viewport VA `0x5004D4`. Keep Y/Z unchanged.
3. Redirect each individual 32-bit floating-point operand of
   the four matrix-size operations to a private, scaled constant,
   without changing game-shared .rdata constants.
4. ONLY after this whole-stage patch succeeds, leave the native
   screen-world viewport unchanged in the camera hook. If preflight
   fails, retain the previous viewport path.
5. Native 2D percent fonts/frames stay at their existing common
   4:3 transform; world-camera Hor+ stays active and unaffected.

This is a **candidate fix**, not a visual validation. It is
deliberately not tied to Murfy, Learn_10, languages or individual
textures. Procedure 410 is an independent 3D-world actor placement
path without a native inverse-screen conversion in its observed case;
its complete matrix semantics remain to be audited. A user Wine
visual trial is necessary before declaring the dialogue and arrow
resolved. PR #2 stays DRAFT and must NOT be merged based on CI alone.

---

# 2026-11-12 — classify the exact script IDs and the latest Wine trace

The owner's `Rayman2Fix(20261112-204416).log` (Wine-staging 11.17,
1920×1080, clean exit `0x00000000` after **235.233 s**) confirms
the executable's native overlay dispatcher jump table and all five
case branches. The public MIT-licensed
[ACP_Ray2 `AI_Const.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/AI/AI_Const.h)
names these sequential *native script procedure IDs*:

| ID | Defined procedure name | Meaning from symbol |
| --- | --- | --- |
| 409 | `AI_E_proc_SpoSetSuperimposed` | set an actor superimposed |
| 410 | `AI_E_proc_SpoSetSuperimposed2` | alternate set |
| 411 | `AI_E_proc_SpoReleaseSuperimposed` | release |
| 412 | `AI_E_proc_SpoSetCoordinates` | set coordinates |
| 413 | `AI_E_proc_SpoSwitchSuperimposedTab` | switch superimposed tab |

The independent `AI_E_proc_TextSuperImposed` is procedure **443**.
It is **not** covered by the existing 409–413 dispatcher observer.
Its name is useful engine terminology, *not evidence* that it owns
Murfy's visible letters or the blue continuation arrow.

At +2,+5,+10,+20 seconds **after the first lower frame**,
`RUNTIME-OVERLAY-ACTOR` reported **260,0,1,0,0** lifetime calls,
and **0** calls of every ID after that frame. A single **413**
call appeared *later* (after +20 seconds), just before the font
submissions started in the trace. Its independently captured
case body at CD VA `0x47131E` delegates to `0x480B90` and
`0x45BA60` before returning; it contains no *direct* matrix
column writes. Combined with the exact symbol
`SpoSwitchSuperimposedTab`, the timing is **not evidence of
text/arrow geometry modification**. Do not target 413 for resizing.

Procedure 409 was used **260 times before the lower frame**,
suggesting initial actor placement; 410 and 412 were unused during
this test. The two known independent native superimposed-position
and extent code sites `0x475C7F` / `0x475C96` remain much more
relevant than procedure 413, but are not yet positively linked to
the observed off-frame content.

Read-only native 2D geometry confirms again:
first frame X **14–94%** (downstream driver X **235.5–849.9px**
at internal 1024×768); over **4,096** vertically overlapping
font quads have original X **19.3–80.7%**, outside count **zero**.
These are GLI draw *submissions*, not unique letters or a
proven identification of every observed text object.
**Do not apply further general 2D font contraction.**

**Next engineering step:** identify the owning renderer/camera and
matrix/position stage for the visually misplaced overlays. Compare
actor-level 409/410 normalization with the existing centered
screen-world viewport; enable one consistent stage only, not both.
A dedicated 443 *script* procedure may be worth a separate source
review, but the symbol alone does not justify an active hook.
No new gameplay regression test is requested merely to reconfirm
these already-settled call counts. The visual problem remains open,
PR #2 remains draft.

---

## Wine log 2026-11-12 21:29:29: phase-specific counts

`Rayman2Fix(20261112-203205).log` confirms the approved CD
dispatcher observer installed and the game exited normally with
code 0 after 137.217 s on Wine-staging 11.17. At 40,60,75,85,90s
the script counts were unchanged: procedure 409=217, 410=0,
411=1, 412=0, 413=0 (and no other IDs), all before the
first lower frame. The frame finally appeared **AFTER** the
fixed 90s checkpoint. A single sample reports procedure 413 after
the frame; the test did not have a panel-relative actor count
checkpoint. Causation/visual identity of that actor is unknown.

The 2D overlap probe again recorded x_min=19.3, x_max=80.7
with **zero** native font quad submissions outside first lower
frame x=14..94 (milestone 4,096); do not squeeze 2D again.

The next strictly diagnostic CD build adds:
- A read-only 20-byte snapshot of the exact 409..413 jump table
  at CD VA `0x47135C`, plus at most 64 original code bytes from
  each of its five executable, in-image branch destinations.
  The original function already verifies its script selector as
  `procedure-409 <= 4`; logged targets are independently
  checked against PE executable sections. No branch is invoked
  outside game normal flow.
- Separate **2/5/10/20s checkpoints relative to the first lower
  frame**, covering both 2D glyph extents and each procedure's
  total / after-frame count. No absolute 90s cutoff.
- No actor geometry, font, screen camera, FOV, viewport, scene
  script or language-specific modifications.

A procedure 413 call appearing after the lower frame is *temporal
correlation*, not evidence that it draws the blue arrow.
Never enable the GOG actor position formula on top of our already
centered 4:3 viewport. PR remains draft.

# Rayman 2: superimposed actor placement — research, not an active fix

## Approved-CD byte evidence (2026-11-12)

The owner's `Rayman2Fix(20261112-191814).log`, Wine-staging 11.17,
verified CD game, exits normally after 37.563 s. The passive executable
probe fetched these actual approved-CD machine bytes:

- CD VA `0x4710C0` begins `83 EC 28 8D 44 24 30` (two whole x86
  instructions; function prologue) and at `0x4710E7` branches on
  `EDI - 0x199`, unsigned bounded by 4: script procedures **409–413**.
  The caller's instruction `mov edi,[ebx-8]` supports the procedure
  integer convention described in the Ray2Fix source.
- CD VA `0x475C7F`: `fmul dword ptr [0x49CCAC]`; at `0x475C88`
  the code passes viewport `0x5004D4` to original `0x422940`
  (screen-position translation). The original 0.001 scaling is the
  candidate; the pointer data are NOT yet verified here.
- CD VA `0x475C96`: `mov eax,[esp+18h]`,
  `mov ecx,[esp+4Ch]`; downstream original instructions write
  `[edi+34h]` and `[edi+54h]`, consistent with projected-object
  X/Z matrix columns. The exact semantic contents of those stack
  values require further testing.

These code locations are now **confirmed within this CD executable**,
not merely addresses copied from GOG. Their overall screen-role and
actual connection to the visible arrow remain to be demonstrated.

A new **observation-only** 7-byte trampoline is available at the
approved procedure function entry `0x4710C0`, with exact initial
instruction signature, PE guards and original cdecl/return ABI.
It records a few first procedure IDs and bounded wall-clock counters
(`RUNTIME-OVERLAY-ACTOR`) including procedure 409/410 occurrences
following the first lower frame. It does not modify actor positions,
matrices, viewport, FOV, game logic or save data. Its only installed
binary redirection returns to the original engine unchanged.

The user can reach the Murfy overlay to show which procedures execute
concurrently with the dialogue, but call counts are correlations,
not a positive identification of the arrow's object identity.



## What the owner's CD build demonstrates

The owner's Wine trace `Rayman2Fix(20261112-183520).log` reaches
4,096 GLI-native glyph quad submissions overlapping the first lower
panel (X=14..94%, Y=80.6..92.4%). Their combined original X span is
19.3..80.7%, with **zero** native 2D glyphs horizontally outside
the panel. The blue arrow and any screen-world 3D text have **not**
been positively matched to a draw call. Do not shrink these verified
2D glyphs again and do not claim the visual bug is solved.

## Independent MIT-licensed implementation evidence

The public [Ray2Fix `fix.c`](https://github.com/spitfirex86/Ray2Fix/blob/master/GliFixVf/fix.c)
includes `FIX_fn_p_stSPOSuperimpoed` (around lines 197–289):
- Procedure 409 processes actor coordinates in screen thousandths,
  converts original X to `0.5 + (X/1000 - 0.5) * squeeze`, and leaves
  Y and Z in the original normalized coordinate system.
- Procedure 410 processes original world coordinates and does NOT
  perform that screen-space X remap.
- Both change the superimposed actor's X and Z matrix extents to
  `(viewport_height / 480) * squeeze`.
- Separate code locations implement analogous superimposed
  position/size corrections:
  [`patch.asm`](https://github.com/spitfirex86/Ray2Fix/blob/master/GliFixVf/patch.asm).

At 1920x1080 output and 1024x768 native drawing, squeeze=0.75:
an actor origin X=0 maps to 0.125, X=0.5 remains 0.5, and
X=1 maps to 0.875. The candidate matrix extent is
`768/480*0.75 = 1.2`. These are evidence for **a different
engine rendering subsystem**, not proof that the user's mismatched
blue arrow uses the same procedure.

## Why this is not yet enabled

Our current runtime already narrows the screen-world camera viewport
from width=1024 at X=0 to width=768 at X=128. That produces
exactly the same screen-space horizontal position as the native
actor-level mapping above. **Applying both is double-scaling** and
would alter UI placement and possibly introduce new regressions.
The actor's matrix extent is also a distinct variable, not
necessarily fixed by changing the viewport.

Ray2Fix originally targeted a **different GOG executable**.
Subsequent Wine byte fingerprints of our approved CD executable
(`SHA256=7927a106...`) **independently verified** the native
code entry at game VA `0x4710C0` (RVA `0x710C0`) and
the code bytes at VA `0x475C7F`, `0x475C96`. The 409–413
native jump table at VA `0x47135C` was also read and followed
to the five executable code branches. These verified locations
establish the subsystem's presence in our edition but **do not**
establish the ownership of the visibly misplaced arrow/text.
Do not activate a write patch based on proximity or on foreign
versions' entry points without verifying the exact instructions
and avoiding double application with our narrowed viewport.

The new header `superimposed_math.hpp` and unit tests exercise
the candidate equations and the equivalence of actor-level origin
mapping with viewport-level origin mapping at 4:3,16:9,21:9.
**No runtime drawing hook or binary patch is introduced.**

To avoid requiring another copy of the retail EXE, a startup-only,
**read-only** probe now reports bounded hex fingerprints at three
public-GOG-reference RVAs, but ONLY when inside an executable section
of the already timestamp-verified CD PE image (and only after the
original camera patch byte pattern has matched). The log prefix is
`RUNTIME-OVERLAY-CODE`. It reads at most 96+32+32 bytes with
`ReadProcessMemory`, does not log process pointers, and does not
patch those candidate locations. A readable byte range **does not**
establish equivalence of the game versions; disassemble and verify
actual CD instructions/calling conventions before using any detour.
The startup-only fingerprints were obtained successfully in the
2026-11-12 Wine logs; no further startup-only test is necessary.

Next: identify which call actually draws the
blue continuation arrow or other out-of-frame projected text, and
select **one** horizontal placement stage. Keep a single Windows/Wine
implementation, retain world camera Hor+, and do not touch game
script variables or language-specific text. PR #2 stays draft.
