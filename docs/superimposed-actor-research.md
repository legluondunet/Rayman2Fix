# Rayman 2: superimposed actor placement — research, not an active fix

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

Ray2Fix targets a **different GOG executable**. Its procedure IDs
(409,410) and hook locations (game VA `0x4710C0`, `0x475C7F`,
`0x475C96`) are research leads, **not verified entry points or
bytes** for our owner's SHA-locked CD game executable
(`SHA256=7927a106...`). Do not reuse them as CD RVA values.

The new header `superimposed_math.hpp` and unit tests exercise
the candidate equations and the equivalence of actor-level origin
mapping with viewport-level origin mapping at 4:3,16:9,21:9.
**No runtime drawing hook or binary patch is introduced.**

Next: map the approved CD executable's superimposed actor procedure
and its byte signatures, identify which call actually draws the
blue continuation arrow or other out-of-frame projected text, and
select **one** horizontal placement stage. Keep a single Windows/Wine
implementation, retain world camera Hor+, and do not touch game
script variables or language-specific text. PR #2 stays draft.
