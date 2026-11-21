# Rayman2Fix — native menu cursor actor research (experimental)

Date: 21 November 2026. Branch: `research/native-resolution-menu-state`.
This document records **read-only evidence**, not a verified cursor-render
suppression technique. Do not merge it as an interactive feature.

**Current interpretation:** the module 13/0 roots earlier described as
"graphics" have been reclassified as AI `Mind` records. See the
correction section at the end; older diary entries are historical.

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


## Follow-up: successful #1527 deployment and pointer-value evidence

The owner provided the two 21 November 2026 logs
`Rayman2Fix(20261121-163047).log` and
`Rayman2Fix_installer(20261121-163055).log`.

**Build identity resolved:** the installer reports
`Rayman2Fix 0.1.0-bootstrap commit=7c089bd2`, and the game runtime
reports `RUNTIME-CONFIG: build=7c089bd2`. GitHub commit
`7c089bd243dd1bc5abba2197512ccfbd38636463` is the CI-generated
merge commit combining PR branch head `b8dc77872b0e06e342ca6f5d81b34741090bc7b1`
with its base. It contains the #1527 changes. The installer reports
`RUNTIME-DEPLOY: status=replaced`. The earlier stale-DLL concern is
resolved for this session; **the two hashes agree**, although they
are the CI merge commit rather than the branch's eight-character SHA.

The runtime successfully logs:

```text
RUNTIME-MENU-CURSOR-ACTOR: exact-name-hits=3 name-table-matches=1 scanned_mib=111 unreadable_chunks=0 verified-actor-renderer=0 mutation=0
RUNTIME-MENU-CURSOR-REFS: anchor-revalidated=1 direct-name=4 arg-name=2 menu-name=2 candidate-block-base=2 nearby=1,1,1,1 scanned_mib=110 unreadable_chunks=0 actor-links-unverified=1 mutation=0
```

**Interpretation:** 10 aligned 32-bit values match one of four
candidate string or block addresses; exactly one site per category is
within ±64 KiB of the verified four-name block. This is not 10 cursor
actors; it does not establish ownership, typed references or the
rendering function. A value can happen to equal an address without
being an actual pointer.

The protected Resolution page enters/exits multiple times. The
native brightness behavior is still reported as `normal=18,
reflex=5` and the original labels are restored on exit.
`original_video_calls_forwarded=0`; the guarded selection/save
counters are zero in this navigation-only run. The game ends with
`LAUNCH-EXIT: child_exit_code=0x00000000`; internal resolution
`1024x768x16` unchanged. None of this proves the UI has become
noninteractive or that the cursor renderer is identified.

### Next bounded diagnostic: local reference layout

Because **each of the four kinds has exactly one local match**, the
new research code records its signed location relative to the
verified name-table anchor, not the absolute address. It prints one
additional marker, only if the anchor is still valid and each
local count is exactly 1:

`RUNTIME-MENU-CURSOR-REF-LAYOUT: relative-sites=d0,d1,d2,d3 span_bytes=N within_256_bytes=0|1 identity=unverified actor-renderer=unverified mutation=0`

The four values are offsets for the cursor name, ARG name, controller
name, and hypothetical module base, in that order. A small span may
suggest an adjacent relocation/pointer table; a large span makes one
tight record less plausible. Both are hypotheses, **not actor or
renderer identity**. Ambiguous or missing singleton values produce
`status=not-enough-singleton-references`. The scan remains
read-only, performed once; no extra memory traversals, raw pointer
addresses, actor-state or rendering mutations have been introduced.
A pure C++ test verifies signed offsets and clustering, and the
experimental Windows artifact's CI check now demands the marker
be present in the DLL.

When the next log establishes the offsets, inspect offline against
the known SNA/RTB field layout before selecting any live actor
hook. In particular, never hide objects based only on exact names
or this proximity statistic. The PR remains unmerged.


## Wine #1532: the four local sites are INLINE self-references (21 November 2026)

The owner supplied `Rayman2Fix(20261121-163931).log`, runtime
`d50e68ca`, compiled from the PR merge, with the following result:

```text
RUNTIME-MENU-CURSOR-REF-LAYOUT: relative-sites=-12,80,2248,-456 span_bytes=2704 within_256_bytes=0 identity=unverified actor-renderer=unverified mutation=0
```

The four candidate *targets*, relative to the cursor-name anchor,
are `0,+92,+2260,-444` (cursor name, ARG name, menu controller name,
tentative module base). Subtracting the targets from the four *sites*
gives `-12,-12,-12,-12` exactly. Therefore each of the four
nearby pointer-like values sits **12 bytes before the data it points
to**. These are **inline self-references**, not one four-member
actor/renderer pointer table. The regular 12-byte prefix resembles a
per-entry serialized or relocated header, but no struct-field type is
yet verified; `target=-444` is especially tentative.

The previous nearest-source clustering measure
`within_256_bytes=0` was true but misleading as an actor-research
signal: this is a fixed source/target displacement, not an absence of
structure. **Do not treat these four local hits as independent
actor links or hide the cursor using them.**

The already-existing single bounded pointer scan also found
`direct-name=4 arg-name=2 menu-name=2 candidate-block-base=2`.
Excluding the four local self-references leaves **at most** the
following external candidates: `3,1,1,1`. Their identities remain
unknown; they may be duplicates or relocation/loader bookkeeping,
not actor references.

### One follow-up without additional memory scans

The research code now classifies every hit in the existing bounded
scan as either an inline `site == target-12` reference or an
external candidate, and retains only up to 16 **source-site addresses**
per kind transiently to compute the distance between the nearest
external cursor-name and ARG-name reference sites. It never logs raw
addresses or dereferences them, and it does not change gameplay or
graphics. The new diagnostic is:

`RUNTIME-MENU-CURSOR-REF-CLASS: inline_minus12=a,b,c,d external_candidates=w,x,y,z closest_external_cursor_arg_bytes=N external_pair_within_256=0|1 typed_actor_links=0 mutation=0`

The `external_pair_within_256` flag merely compares pointer-site
proximity; even a small distance is not enough to identify a
`SuperObject`, `Perso`, animation owner, or draw path. A pure test
now checks all four exact -12 offsets and their fail-closed behavior.
The research-specific CI checks that the compiled fix DLL contains
this diagnostic. Keep `main` untouched and PR #51 unmerged.

The old menu video behavior continues to select original entries,
while script-level resolution changes are guarded. The new logging
does not remove the luciole or produce a non-interactive settings page.


## Autonomy phase: directly typed cursor-name ownership and shared visuals

The maintainer asked for a sustained autonomous phase without additional
Wine screenshots/log cycles. We re-used the **owner-provided** original
`menu.sna`, `levels0.dat`, `fix.sna` and `fix.rtb` in read-only
analysis. Their verified SHA256 values are pinned by the two scripts:

- `tools/research_menu_cursor_actor.py`: approved Menu.sna + Levels DAT
- `tools/research_menu_cursor_fix_links.py`: approved Fix.sna + Fix.rtb,
  plus the Menu/Levels hashes

### Directly proven name → StandardGame → Perso/SuperObject chain

`YAM_CurseurMenu_I1` resides at scene virtual `0x07E701DC`
within the Menu module 4/1. Its name-record pointer at `name-0x18`
is *typed by RTB* and targets `0x07E70588`, which equals the cursor
actor's StandardGame `0x07E70550 + 0x38`. Conversely, the
StandardGame field `+0x3C` holds a **typed** pointer back to the
name record `0x07E701C4`.

The **independent** `ARG_CurseurMenu` name record at
`0x07E70238 - 0x18` points (typed) to
`0x07E70550 + 0x5C`; the StandardGame's `+0x60` field
points back to that name record. Both links identify the **same
StandardGame**, without relying on a numerical object-type tuple
as the primary identifier.

The already-verified cursor actor chain is:
`SuperObject 0x07AB2174` → `Perso 0x07E703F8` →
`StandardGame 0x07E70550`, with a typed back-link from
StandardGame `+0x0C` to the same SuperObject. The brain is
`0x07E705D4`, and visual-data block is `0x07E70434`.
The name-linked actor's object-type tuple `(2,11,11)` now acts
only as a cross-check. A separately proven `YAM_GenMenu_I1`
name record resolves to the *different* GenMenu actor, preventing
accidental conflation with the cursor and the video-controller actor.

This is a substantive improvement: the named cursor actor's
**identity within the original scene is now supported by reciprocal
typed relocations, not merely co-located labels or pointer counts**.
It is not proof that the actor's visible 3D model is exactly the
butterfly/luciole in the user's screenshots.

### Verified link to common Fix visual state

Raymap's `Perso3dData` layout identifies pointers at offsets
`+0,+4,+8` as the actor's initial/current/secondary visual states
and `+0x14` as its family. The hash-locked original CD files provide
typed SNA/RTB links:

- Cursor visual-data `0x07E70434 +0,+4,+8` →
  common Fix state `0x001B5D14`, all identical
- Cursor visual-data `+0x14` →
  common Fix family `0x001B5CD8`
- Family index at `+0x0C` = `2`, consistent with cursor
  StandardGame family index `2`
- The family's two state-list head pointers at `+0x10,+0x14`
  both point (typed) to `0x001B5D14`, and its state count is `1`

For the **GenMenu negative control**, the distinct visual-data block
`0x07E70B50` resolves to family `0x001B6274`
(index `3`) and state `0x001B62B0`, also typed and internally
consistent. The cursor and GenMenu do not share a family/state.

These links demonstrate that the cursor instance has a separate
native visual state/family. They still do **not** establish the
associated mesh, texture, renderer callback, or whether making that
SuperObject invisible will remove the butterfly without affecting
unrelated menu behavior.

### Test and privacy policy

The script emits **metadata only**, no proprietary SNA contents.
It refuses unrecognized file hashes, missing typed relocations,
ambiguous ownership, wrong family indices and inconsistent state
pointers. Pure synthetic unit tests cover forged or missing back-links
and malformed cross-file visual state. The CI runs these tests under
Linux without requiring bundled commercial game assets.

Until renderer ownership is validated, leave the original cursor
visible and preserve the existing guarded video-selection calls,
brightness control, text localization and other native menus. No
actor flags, matrix, input routing or draw calls have been changed.

**Next engineering milestone:** use the now-established
named-actor → family/state chain to identify the actual animation
or geometric object owner, then isolate the resolution information
presentation from native highlight and selection UI. Do not ship
a visibility toggle based solely on name ownership.


## 21 November correction: shared block 13/0 is AI Mind, NOT a graphics renderer

**This section supersedes the earlier interpretation of the six-hop
"graphics" link.** Cross-checking the actual data fields against the
Rayman 2 OpenSpace/Raymap `Mind`, `AIModel` and `Intelligence`
layouts proves that the two previously observed shared roots encode
**AI behavioral state**, not identified graphical/mesh resources.
The older pointer graph was genuine but its semantic label was wrong.
The code now uses `linked_shared_ai_mind` and explicitly tests
AIModel and Intelligence reciprocal typed relocations.

| Known scene instance | Fix 13/0 Mind | AIModel at Mind+0 | Normal Intelligence at +4 | Reflex |
|---|---|---|---|---|
| Named menu cursor | `0x0229FE34` | `0x0229FE4C` | `0x022A1D90` | Absent |
| GenMenu control | `0x022A21FC` | `0x022A2214` | `0x022C89E8` | `0x022C8B4C` |

For both roots, `Mind+0` resolves to `Mind+0x18` (AIModel),
`Mind+4` resolves to a normal Intelligence whose first pointer
returns to that AIModel, and `Mind+0x0C` resolves to an optional
DsgMem (present in both reviewed instances). The GenMenu reflex
Intelligence's first pointer resolves to `AIModel+4`.
The AIModel normal behavior-table headers contain respectively
**2** and **23** entries; the cursor reflex pointer is null without
a relocation, while GenMenu has a typed reflex pointer. These
independent field checks are more informative than raw pointer
chaining and rule out treating block 13/0 as a known renderer.
No graphics callbacks or actor flags have been modified.

### Cursor instance AI script traced using menu module 13/1

The approved scene has the following separately verified path:
`SuperObject 0x07AB2174` → `Perso 0x07E703F8` →
`Brain 0x07E705D4` → `Mind 0x079206F0` →
`AIModel 0x07920708` → normal behavior list
`0x07920720` (2 behaviors) → selected behavior
`0x07920734` → script wrapper `0x07920750` →
script nodes at `0x07920758`.

The one observed script contains **26** PC ScriptNodes
(`param:u32, padding:u16, indent:u8, type:u8`) and ends with an
indent-zero type-8 terminator. Four type-23 **ActionRef** nodes
(all RTB-typed into shared Fix 4/0) reference three distinct
action records:

- `0x001B631C` once;
- `0x001A22D0` once;
- `0x001B5AD0` twice.

Crucially, the last record has **three independent typed links**
at offsets `+0x3C,+0x40,+0x44` back to the cursor's already
verified visual state `0x001B5D14`, and one at `+0x50` to its
family `0x001B5CD8`. The two other action records consistently
reference **different** state/family pairs. This is positive evidence
that the cursor AI script controls or selects an action attached to
its own state/family; it does **not** identify a rasterizer, textured
butterfly, or safe per-page visibility control.

The new read-only analyzer `cursor_normal_script_evidence` verifies
the complete typed path, bounded node count, signature and action
ownership. Its synthetic-only regression tests refuse missing
relocations, forged cross-links, contradictory owner records and
invalid terminators. Proprietary script bytes are never emitted or
redistributed; the only output is aggregate metadata.

### Instance object lists and remaining uncertainty

Among the 26 RTB-verified Menu Persos, **9** have two equal typed,
non-null object-list pointers at Perso3dData `+0x0C,+0x10`,
while **17** have both slots null with no relocations. The named
Cursor and GenMenu instances have **no direct instance object list**.
They might still drive separate drawing objects or scripted effects.
The AI findings above narrow the behavior path; they do not supply
a mesh or texture identity.

**Engineering decision:** no live SuperObject `Invisible` change,
sprite quad filtering, input interception, or brightness changes.
The native video page still uses the game's selection behavior
for layout/color/cursor. To achieve a truly read-only page, isolate
its graphics presentation from the interactive resolution rows
rather than assuming hiding the named AI actor is sufficient.
Only a consolidated functional change should be offered for Wine
testing. `main` remains intact; PR #51 is still experimental.
