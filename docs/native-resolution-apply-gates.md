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
