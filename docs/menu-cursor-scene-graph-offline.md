# Rayman 2 PC CD — cursor scene graph reconstructed offline

Research date: 21 November 2026. **No game patch, new rendering hook or
commercial asset in this repository.** The owner-provided original files
were accessed read-only; only structural metadata is recorded here.

## Validated source and relocation boundaries

* `menu.sna`: 638,463 bytes, SHA-256
  `73e754887d85954ee611b6fbff118293fa9f264f5f165c3e9b2823176998ecbb`.
* `levels0.dat`: 150,775,808 bytes, SHA-256
  `8a9a1dc7118b31c9d63fdb901889a073f23641a5d0933d3af839e0df71e82b17`.
* PC Menu relocation table: encoded DAT offset `0x00E65000`,
  decoded length 68,203 bytes, exactly 8,522 pointer records
  (4/1: 1600; 5/2: 5325; 13/1: 1597). Each entry has a source
  address and a typed module/block destination. The verifier checks
  the type **and** that the target lies in that destination module.
* Three nonempty SNA modules: (4,1) data 62,328 bytes at base
  `0x07E70020`; (5,2) data 268,020 bytes at `0x07A90020`;
  (13,1) data 307,812 bytes at `0x07920020`.
  These bases are **static addresses in the decoded CD resource**,
  not runtime locations guaranteed under Wine ASLR.

## Three distinct, relocation-checked Perso chains

The offline analyzer reconstructs **26 bidirectionally verified**
`SuperObject(type=Perso) → Perso → StandardGame → SuperObject`
chains. It also verifies the Perso's brain and visual-data references.

| Role (qualification) | SuperObject | Perso | StandardGame | Object type indices |
| --- | --- | --- | --- | --- |
| Cursor-*candidate* | `0x07AB2174` | `0x07E703F8` | `0x07E70550` | `(2,11,11)` |
| Video AI controller (known negative control) | `0x07AB2250` | `0x07E70908` | `0x07E70A60` | `(3,12,12)` |
| `YAM_GenMenu_I1`-related actor (negative control) | `0x07AB230C` | `0x07E70B14` | `0x07E70C6C` | `(3,13,13)` |

Cursor candidate:
`Brain=0x07E705D4`,
`Perso3dData=0x07E70434`, native
`SuperObjectFlags=0x00000008`,
stored draw-flags word `0xFFFFFFFF`. The latter flags cannot
independently identify the visible butterfly. The controller and
GenMenu comparison chains use distinct records, thereby ruling out
mistakenly treating the controller as the cursor actor.

The module-4 name table contains unique fingerprinted strings
`YAM_CurseurMenu_I1`, `ARG_CurseurMenu`, `YAM_GenMenu_I1`
at relative offsets `444`, `536`, `2704`, each with an
exact self-pointer 12 bytes before its own string. The latest Wine
log measured these same `-12` pointers at runtime.

### What is proven versus not proven

**Proven** on this approved CD edition:

- All three actor candidates have valid typed,
  bidirectional `SuperObject/Perso/StandardGame` links.
- The cursor candidate type tuple `(2,11,11)` is unique among these
  26 validated Persos.
- The named cursor string table and its header self-references exist.
- The other two actor chains do not overlap this cursor candidate.

**Not proven**: the object-type index `11` binding to the
`YAM_CurseurMenu_I1` name in the separately maintained object-type
catalog; the cursor candidate owning the *visible* menu luciole; the
effective draw flag controlling its visibility; whether hiding it
preserves menu inputs and brightness; or the live location of these
static scene addresses. Do **not** patch `SuperObjectFlags` from this
report alone, and do not interpolate these static addresses into a
running game.

## Reproduction without introducing game resources into the repository

Run with the owner's locally installed retail files:

```sh
python3 tools/research_menu_cursor_actor.py \
  /path/to/Menu.sna /path/to/levels0.dat
```

The analyzer enforces both SHA-256 hashes, exact module/relocation
counts, name fingerprints, unique type-index triplets and complete
bidirectional pointers. It emits only JSON metadata. Other editions
fail closed until independently studied. The synthetic unit tests
`tests/test_menu_cursor_offline.py` do not require game files.

Raymap's public `SuperObject.cs`, `Perso.cs`,
`StandardGame.cs` and `SuperObjectFlags.cs` were used as structural
cross-checks, not as proof of runtime cursor behavior.

## Implications for the UI work

This new actor candidate narrows the offline research considerably,
but we must not accumulate more one-line diagnostics or ask the user
to test each static hypothesis. Work in the PR #51 research branch
only, keep original video mode protection, leave luminosity and the
other menu actors untouched. Next, prove object-type-name indexing
and identify the actor's native draw/selection route using structural
evidence. Only then propose one grouped Wine/Windows validation build
for the completed information-only menu.
