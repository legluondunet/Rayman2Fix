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
