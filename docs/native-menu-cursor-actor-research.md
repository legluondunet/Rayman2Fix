# Rayman2Fix — native menu cursor actor research (experimental)

Date: 21 November 2026. Branch: `research/native-resolution-menu-state`.
This document records **read-only evidence**, not a verified cursor-render
suppression technique. Do not merge it as an interactive feature.

## Last Wine experiment

The owner ran the isolated prototype #1510 (runtime `c0a7a16b`)
and supplied `Rayman2Fix(20261121-155701).log`, 222 lines.
The logger confirmed guarded native scripts and text installation,
three visits to the video behavior `normal=5,reflex=0`, the
`normal=18,reflex=5` brightness behavior, and the selected internal
Glide mode remaining `1024x768x16`.

The temporary `RUNTIME-GRAPHICS-CURSOR-RAW` probe saw only
`route=plain`, with approved game callsites `RVA 0x294D4` and
`0x29AF1`. Reported rectangles were outside the internal 1024×768
screen (e.g., around x=94..113/y=820..852, or negative X/y>1000),
and in some cases reciprocal depths were negative. None satisfied
the broad 2D-cursor screen-quad predicate; neither UV nor colored
route was observed by this scoped probe.

**Inference boundary:** these callbacks do not identify the visible
butterfly/luciole. The actor might render through world-model geometry
rather than one of the generic sprite callbacks. DO NOT suppress either
callsite, extrapolate a cursor texture hash, or hide all sprites in
a screen region. That might erase world objects and the brightness bar.
The generic cursor-geometry probe and its runtime callsites were retired.

The video-action counters were all zero, matching a navigation-only
test; `original_video_calls_forwarded=0`. A zero counter does not
prove the block mechanism independently. The engine mode stayed
`1024x768x16`; the output is separately 1920×1080 on the owner’s
screenshots. The menu still exposes native selection/color and cursor
behaviors: it is not yet a true read-only information page.

## SNA-derived actor name anchors

The maintainer-owned `menu.sna` was inventoried previously:
SHA256 `73e754887d85954ee611b6fbff118293fa9f264f5f165c3e9b2823176998ecbb`,
module 4 decoded payload begins at file offset 257.
Names inside that one payload (decoded file offsets):

| String | Offset | Relative to `YAM_CurseurMenu_I1` |
| --- | ---: | ---: |
| `YAM_CurseurMenu_I1` | 701 | 0 |
| `popolopoi_menu` | 749 | +48 |
| `ARG_CurseurMenu` | 793 | +92 |
| `YAM_GenMenu_I1` | 2961 | +2260 |

These names indicate a **specific cursor-related scene actor
candidate** and nearby menu structures. They do not prove that this
actor is the visible butterfly, or expose its renderer and visibility
control.

The research-only AI controller observer now performs a second,
one-shot, bounded `ReadProcessMemory` scan of committed writable
private memory after an independently verified controller is found.
It counts exact occurrences of `YAM_CurseurMenu_I1` and separately
four-name co-located matches with the above byte offsets. Only
aggregate counts and scan volume are logged:

`RUNTIME-MENU-CURSOR-ACTOR: exact-name-hits=... name-table-matches=... scanned_mib=... unreadable_chunks=... verified-actor-renderer=0 mutation=0`

A name-table match locates the *string table*, not necessarily the
actor record. Zero matches do **not** prove the actor absent: an
unreadable, read-only, packed, remapped or differently laid-out block
would not qualify. Multiple matches are ambiguous. No raw addresses,
object identities, proprietary asset bytes or complete decoded game
data are logged.

Next meaningful research step, **only after a unique name anchor is
observed**: resolve typed, relocated references back to its owning
superobject/character and validate its world transform and draw owner
against cursor movement in the menu. This requires structural proof,
not a global GLI quad suppression or interception of unrelated input.
Make any future per-actor visibility change separately switchable
and restore actor state on video-page exit; preserve brightness and
normal menu flow. If the anchor is not found, revisit the offline
SNA/RTB layout first rather than guessing runtime addresses.

## Validation

Pure asset-free C++17 test:
`graphics-menu-cursor-actor-name-fingerprint`. Validates required
name offsets, null terminators and fail-closed malformed layouts.
The normal release and `main` remain unchanged. The research
variant never changes actor state or rendering.


## Wine #1522: one verified co-located name table (21 November 2026)

The owner supplied `Rayman2Fix(20261121-161102).log` from runtime
`e847a59f`. At ~5 seconds after process start, the controller was
found with 27 normal and 7 reflex behavior entries, as on earlier runs.
The new one-shot scan returned:

```text
RUNTIME-MENU-CURSOR-ACTOR: exact-name-hits=3 name-table-matches=1 scanned_mib=110 unreadable_chunks=0 verified-actor-renderer=0 mutation=0
```

This is **positive evidence** that the expected four-name layout is
present in one contiguous region of readable/writable private game
memory. The three exact single-name hits by themselves are NOT three
cursor actors. We still have no evidence that the matched string region
owns the 3D mesh or screen-space visual known as the menu butterfly.

The next stage must remain read-only, with no cursor hiding, pointer
patching, input suppression or vertex modifications. Previous
`RUNTIME-GRAPHICS-CURSOR-RAW` captures did not identify the cursor.
The video guards remained installed in this navigation-only test;
`video_actions_blocked=0`, `saves_blocked=0`,
`original_video_calls_forwarded=0`, and the internal mode remained
`1024x768x16`. The log confirms repeated `normal=5/reflex=0`
information-page visits and `normal=18/reflex=5` brightness transitions
with original labels restored.

### Follow-up: candidate pointer values, NOT a linked actor

With exactly one matching name layout, the new research code rechecks
the same 2,300-byte name fingerprint before performing **one bounded**
scan of committed writable/private x86 memory. It counts 4-byte aligned
values exactly equal to one of four potential targets:

- Cursor name address: relative +0 from the identified anchor
- `ARG_CurseurMenu` address: relative +92
- `YAM_GenMenu_I1` address: relative +2260 (negative control/comparison)
- Tentative module-4 payload base: relative −444, derived from the
  SNA decoded payload offsets 701 and 257

The new `RUNTIME-MENU-CURSOR-REFS` log gives total occurrences and
counts with the *reference site* within ±64 KiB of the anchor. The
scan does not dereference any matched candidate or log memory
addresses. Exact 32-bit values are suggestive but do not determine
field types, whether they are valid pointers, or ownership by a
`SuperObject` / `Perso`. Zero references would not disprove an actor:
the engine may use inline names, nonwritable memory, transformed
handles, or typed relocations that never directly reference these
strings. Only evidence of a validated actor-to-renderer chain could
justify an eventual visibility change, with restoration outside the
information-only page.

The normal `main` release remains untouched. Test is
`graphics-menu-cursor-ptr-value-candidates`, independent of game
assets and active in Release builds.
