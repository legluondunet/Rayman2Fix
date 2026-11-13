# Rayman 2 PC — static map of ESC audio pause and the type-11 transition controller

Research note, **13 November 2026**. This document records the source of each binary-level claim and separates the *confirmed static mechanism* from hypotheses about the soundtrack. No licensed game executable or assets are redistributed.

## Scope and provenance

- Source: owner-supplied `rayman2(4).exe`, PE32 / x86, image base `0x00400000`, SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`, size **1,468,928 bytes**.
- This binary is **not** the approved original, protected 676,352-byte CD `Rayman2(3).exe` (SHA-256 starts `7927a106`). It is a **static reference only**. RVA matches in historical approved-CD runtime logs help constrain research but do **not** authorize binary patching.
- Tools: `objdump -d -M intel`, `objdump -p`, and Python's built-in `struct`, running locally. All addresses below are x86 image VAs, not file offsets. For this image's `.text` and `.data` the file offset equals `VA-0x400000`.
- The existing optional `RAYMAN2FIX_AUDIO_TRACE` build changes no audio arguments, return values, or playback behavior. The normal build does not include its hooks.


## Independent retail-CD byte-for-byte parity audit (13 November 2026)

The **owner's authentic protected original CD** and modified GOG-derived reference were both recovered from the owner's private file collection **for local offline reading only**:

- Approved CD: `Rayman2(3).exe`, **676,352 bytes**, SHA-256 `7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`.
- Research reference (not approved for runtime): `rayman2(4).exe`, **1,468,928 bytes**, SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`.

A fresh independent checked **PE32 VA → raw offset** reader was used, because the CD `.text` starts at file offset `0x400` whereas the reference starts at `0x1000`; both map to virtual address `0x401000`. The **13 selected audio-code intervals totaling 11,520 bytes** are **identical byte-for-byte**. They cover Win32 focus pause/resume callers, VM script sound-pause/resume callers (not menu-specific), their shared wrappers, native 64-record pause/resume dispatcher, type-11 Theme lifecycle, resource-type dispatch, sound-record allocation, SXD transition playback/wrappers, bank loader/resource resolver, bank-registration command opcode handler, event creation and event dispatcher. The checked intervals are **selected**, not claimed to be exhaustive for all audio code.

In both binaries, **seven** exact `E8 rel32` call sites resolve to the same intended target:

| Call origin | VA | Target |
| --- | --- | --- |
| Win32 window deactivation | `0x401C9C` | `0x4536D0` |
| Win32 activation | `0x401CE9` | `0x453700` |
| Generic script opcode `0x1AA` (pause) | `0x471E70` | `0x4536D0` |
| Generic script opcode `0x1AB` (resume) | `0x471E67` | `0x453700` |
| Shared pause wrapper | `0x4536ED` | `0x4580B0` |
| Shared resume wrapper | `0x45371D` | `0x458110` |
| Bank filename formatter call | `0x453948` | `0x453870` |

Nine machine-code anchors for native pause-mark/depth ordering and the **type-11 jump-table entry** `0x457FC8 → 0x457F21` match independently in both CD and reference. Crucially, the CD bytes directly prove the order **write per-record pause mark, dispatch driver pause, increase global depth** and **decrease global depth, clear mark, dispatch driver resume**. This upgrades the earlier focus-loss counterexample from reference-only static reasoning to **original-CD code-verified reasoning**. It still does **not** show that an unsafe patch has ever run or that a particular soundtrack was audible.

**New retail-CD bank-name proof:** the original CD also contains exact NUL-terminated `bnk_%d.BNM` at VA `0x4A252C`. Its untouched native loader stores `lId` to `[esi+4]` at `0x453945` and passes the same value to the formatter at `0x453948`. Thus this code's numeric bank-ID to decimal filename formatting is **verified on the approved retail-CD executable**, not merely on the GOG-derived reference. For example, bank ID **73** formats to **`bnk_73.BNM`**. No genuine extracted `bnk_73.BNM` file has been inspected, so the file's actual existence, contents, Theme ID `73:48`, relationships to `.APM` assets and audible musical identity are still **unverified**.

`.rdata` also matches across the two files at **12,288 on-disk bytes**. But `.data` differs at **127 bytes in 16 runs**, `.text` differs elsewhere, and their PE sections/entrypoint-related material differ. **This is NOT whole-program equivalence**; unchanged code does not prove identical runtime game state or licensing/protection behavior.

The reproducible, **read-only SHA-pinned** tool is [`tools/check_rayman2_cd_audio_parity.py`](../tools/check_rayman2_cd_audio_parity.py); synthetic PE fixture tests are [`tests/test_audio_cd_parity.py`](../tests/test_audio_cd_parity.py), included in Linux CI. Example **local** use with personally owned EXEs:

```bash
python3 tools/check_rayman2_cd_audio_parity.py /path/to/Rayman2-original-CD.exe /path/to/Rayman2-reference.exe
```

The tool refuses unrecognized SHA pairs by default, prints **no protected code bytes** and does not need or embed either executable in CI. The normal game DLL and launcher remain **unchanged**.

**Impact on future ESC music feature:** core static native pause semantics and Theme ownership mechanism are now grounded in original-CD code. The remaining blockers are **real bank/Theme musical attribution** and an actual **origin-aware, stop-safe reconciliation** of original record pause marks and global depth with desired music state across ESC + Alt-Tab + level transitions. Never suppress driver calls in isolation; stop and resource replacement remain native. F1 stays excluded; `MusicInPauseMenu` stays disabled/unexposed.


## Retail-CD correction: a script audio pause is NOT proof of the ESC menu

**Critical origin classification correction (13 November 2026).** The native function `0x471E40` is **not** an ESC-key or pause-menu callback. Direct original-CD disassembly and the independently identical reference show:

```text
0x482690: lookup handler via [0x4A43E0 + opcode*8]
0x4A5130 (opcode 0x1AA): handler 0x471E40, metadata DWORD 0
0x4A5138 (opcode 0x1AB): handler 0x471E40, metadata DWORD 0
0x4A5140 (opcode 0x1AC): handler 0x471E40, metadata DWORD 0

0x471E40: ESI = second script parameter; EAX = [ESI-8]
0x471E48: compare/subtract 0x1AA
0x471E70: opcode 0x1AA -> global sound pause wrapper 0x4536D0
0x471E67: opcode 0x1AB -> global sound resume wrapper 0x453700
0x471E5B: opcode 0x1AC -> unrelated native sound call 0x453080
```

The three consecutive eight-byte dispatch entries share one opcode decoder; `0x482690` proves that the VM indexes the function-pointer table with the **opcode number**, not with an ESC event type. We do **not** know from these instructions which gameplay state, UI menu, cinematic script, engine event or command sequence emitted a given opcode. A user's observed ESC interaction during a diagnostic run is **external context**, not a property of opcode `0x1AA` or of the `game-script-pause` / `game-script-resume` log origin classifier.

**Safety implication:** the previously considered trigger `audio_origin == game-script-pause` is **insufficient to enable music in the ESC menu** and must never be treated as `esc_menu_verified`. It can identify a plausible VM origin, but the same generic opcode may be used in other scripted situations. Raw `GetAsyncKeyState(VK_ESCAPE)` is equally insufficient; holding or tapping ESC cannot prove that the in-game menu is actually open. The independent Win32 pause branch may also run while scripts are active. A future opt-in intervention requires a separately proven, read-only identification of the **real ESC menu lifetime**, independent of F1, other dialogs, cutscenes and Win32 activation. On ambiguity, preserve all native audio calls unchanged.

The exact VM opcode decoder instructions, three table entries, branch call targets and both pinned binary fingerprints are now verified in [`tools/check_rayman2_cd_audio_parity.py`](../tools/check_rayman2_cd_audio_parity.py). Synthetic [`tests/test_audio_cd_parity.py`](../tests/test_audio_cd_parity.py) deliberately asserts that generic script origin does **not** constitute menu evidence. **No actual game audio behavior has been modified.**


## Reproducible offline audit of the owner's corrected schema-2 Wine trace

A new read-only [`tools/analyze_rayman2_audio_trace.py`](../tools/analyze_rayman2_audio_trace.py) summarizes sampled driver events, attaches the corrected `RUNTIME-AUDIO-THEME-OWNER` lines to their matching **(channel, sample)** trace lines, and emits only aggregate evidence (no source filename, raw driver handle, stack address, full log lines or game assets). It is exercised in Linux CI by [`tests/test_audio_trace_evidence.py`](../tests/test_audio_trace_evidence.py) using wholly synthetic records. For a locally owned log:

```bash
python3 tools/analyze_rayman2_audio_trace.py "/path/to/Rayman2Fix.log"
```

**Independent offline hand-inspection of `Rayman2Fix(20261113-171247).log`:** 48 sampled `RUNTIME-AUDIO-TRACE: event=...` records, including 16 `StopSxd`, 14 `PauseSxd`, seven `ResumeSxd`, five `StopTransitionSxd`, four `PauseTransitionSxd` and two `ResumeTransitionSxd`. Eleven `RUNTIME-AUDIO-THEME-OWNER` lines accompany transition events. In the intervals already cross-checked with the real native CD code:

- **87,431–87,432 ms:** two `game-script-pause` transition driver calls, depth 1, one Theme outer `73:48` (slot 6), inner `73:50` (slot 1), correctly decoded native sound-record pointer.
- **106,431–106,432 ms:** two `game-script-resume` transition driver calls with the same Theme and inner resource, depth 1.
- **133,663–133,664 ms:** another pair of `game-script-pause` calls, outer Theme **still `73:48`**, but inner resource now **`73:51`**.
- **138,032 ms** and **141,667 ms:** distinct owned `StopTransitionSxd` events **at native global depth 2**, first on Theme `73:48` / inner `73:51`, then Theme `89:90` / inner `89:81`. Both must remain intact across any future pause feature.
- All reported `foreground` fields are **1**. There is **no sampled `foreground=0`** event and thus no independent Wine evidence for an Alt-Tab cycle. `esc_down=1` occurs during the script-origin pause and resume samples, but the **generic VM opcode alone plus key state is still not proof of in-game ESC-menu visibility**.

This report distinguishes **recorded driver calls** from **inferred Theme controller ownership**, **correlated same-ms duplicate candidates**, and **untested UI/focus conditions**. It never infers the specific song from an ID or treats a script-origin pause as a confirmed ESC menu. Log tracing is sample-capped; missing records cannot establish that a type of event never happens.

**No new Wine run or build of a patched runtime is required for this offline verification.** The original game DLL, launcher, INI, native audio semantics and the PR's draft state are unchanged.


## Corrected nomenclature — three different classifications

Cross-checked against the public [ACP_Ray2 `SND_Resource.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Resource.h) and [`SND_Event.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Event.h):

1. **Sound *resource* type** (`SND_tdeTypeResource`): `[resource+4]` in `0x457E60` chooses the 11-way resource dispatcher. **Type 11 = `SND_E_TR_Theme`**, **type 10 = `SND_E_TR_Random`**, **type 8 = `SND_E_TR_Switch`**, **type 4 = `SND_E_TR_Sequence`**, and **type 1 = `SND_E_TR_Sample`**. This previously had the misleading name "event class" in the research notes.
2. **Sound *event* type** (`SND_tdeTypeEvent`): a separate enum (`SND_EVT_Son_Play=1`, `SND_EVT_Son_ChangeVolume=11`, etc.). It is **not** the 11-way resource dispatch described above.
3. **Driver transport**: `[native_descriptor+4]` in `0x458220` chooses the actual **sample/SXD resource type 1 versus MIDI type 2** for the inner stream. A Theme resource is itself not a third driver transport.

Theme parts are described by [`SND_Theme.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Theme.h): per-part resource, loop start/end, number of loops, transition mode (crossfade, end-of-part or outro), and fade duration. This is a *positive static identification* of the type-11 Theme controller. It does **not** alone establish which audible song or which nested sample plays at a given moment.

The jump table was decoded from the file, not guessed from contiguous disassembly:

```text
case 1..11 targets at 0x457FA0:
457E8F 457EAA 457EC5 457EE0 457F78 457F02
457F78 457F5D 457F78 457F42 457F21
```

## Confirmed transition creation chain

```text
0x457E60: dispatch resource type 11 = SND_E_TR_Theme
  -> 0x457F21
     -> 0x459950: single type-11 controller, base 0x4B95B8
        -> 0x458530: select/copy nested native sound descriptors
        -> 0x458220: allocate 64 x 32-byte sound-table entry
           -> 0x455280: wrapper around the dynamic x86 5-argument
              _SND_fn_lPlayTransitionExSxd@20 at slot 0x4B86F0
           -> 0x457570: retain native driver handle and descriptor
        -> 0x459AF4: store inner sound-record pointer at 0x4B95E8
     -> 0x457F92: save *outer* event/controller record (separate)
```

Critical distinctions:
- The game sound table begins `0x4B8C98` and has **64 entries of 0x20 bytes**.
- A table record stores the **driver handle at +0**, **game-native descriptor at +4**, **transition flag at +0x10**, and **active flag at +0x14** (see `0x4575D5–0x4575F0`). The handle is not a song ID, and equal handles do not entail equal source descriptors.
- `0x459950` maintains its type-11 controller at `0x4B95B8`. The nested sound-record **pointer** lives at controller **+0x30** (`0x4B95E8`); its address must be decoded to an index after checking the native 64 × 0x20-byte table bounds and alignment. This is structurally separate from the *outer* type-11 sound-table record. It is inappropriate to treat one fixed table slot as “music.”
- In the reference binary, `0x4597E0` explicitly clears the controller's active state and is called during native audio initialization from `0x457E2D`. Other lifecycle paths use indirect entry points and pointer writes, so *initialization is not proof of all later state transitions*.
- Direct `call` instructions to `0x457E60` occur at `0x452E63`, `0x4579E2`, `0x45A26D`, `0x45A6BE`, `0x45A7D6`, `0x45ADD8`, `0x45AFD2`. These seven upstream sites are the next useful static provenance targets; their event arguments need to be linked to an actual resource category.

## Offline resource-ID tracing — nested type-11 events

The type-11 **Theme** controller does not merely store an opaque driver handle. Its
source data has a variable number of nested entries and a reference-fixup
stage. This is a promising **resource provenance** path that can be mapped
without launching Wine, provided the actual resource databases are present.

### Packed sound-bank resource identifiers: `0x453F90`

The original x86 instructions at `0x453F90–0x453FC9` resolve a **DWORD
resource identifier**, with a guard for `0xFFFFFFFF`:

```text
packed_id (DWORD)
  upper 16 bits -> runtime resource-group key
  lower 16 bits -> record index in the loaded group

0x4538C0: SND_tdstSndBankDescriptorList at 0x514D50
           bank ID = [descriptor+4], next = [descriptor+0x3C]
0x453F90: resource_base = [[bank+0x24]+0x0C]
           resource_count = [[bank+0x24]+0x10]
           if index >= record_count: return NULL
           record_ptr = group_base + 92 * index
```

The **92-byte record stride** is obtained from real instructions,
`lea ecx,[esi+esi*2]`, `shl ecx,3`, `sub ecx,esi`,
`lea eax,[edx+ecx*4]`: `4 × (24 - 1) = 92`. The group ID is a key
in the **runtime sound-bank list**, **not** automatically a disc index, archive number,
CD track or song ID. No concrete packed ID has yet been recovered from an
actual level's resources.

### Type-11 resource fixups: `0x459840`

`0x459840` converts a disk-format Theme resource into controller-owned memory.
It walks a **24-byte-stride** collection of nested records, invoking
`0x453F90` on the first DWORD of each element (`call` at `0x459869`).
An optional secondary pointer from descriptor `+0x28` is independently
resolved by `0x453F90` at `0x459890`. The resulting pointers become
input to event playback and must not be assumed to identify a particular
`.apm` file.

This is a genuine path from the controller to **loaded resource records**,
but it still does not label a record as music versus sound effect. The
next required provenance link is the group's archive registration/loading,
which likely requires corresponding game resource files for confirmation.

### Native command stream registers runtime resource groups

The resource group IDs are *not* hardcoded as disc indices in the path
just traced. At `0x4507D5`, a native command-stream interpreter
distinguishes numeric opcode **0x5D** from **0x5E** (subtract 0x5D,
then decrement):

- The **0x5E** branch obtains one group identifier from
  `0x44F8D0` and calls `0x454050` at site `0x4507E3`.
- The **0x5D** branch reads a count and a list of group IDs through
  `0x44F930`, and calls the same `0x454050` at
  `0x450811` for each entry.
- `0x454050` forwards to `0x454060`, which searches the runtime
  group registry (`0x4538C0`) and creates missing registry records
  (`0x4538E0`) before the later per-record resolution.

This confirms how the packed ID's **upper 16 bits** can refer to a
runtime group registered from loaded command data. It does **not**
establish which `MUSIC*.DAT`, `SOUNDS*.DAT` or `.apm` asset
corresponds to that group: an actual group command and resource database
would have to be decoded, and the non-approved reference EXE cannot
replace retail-CD evidence.

### A third, distinct dispatcher: `0x458530`

Do not conflate resource type, resource-descriptor conversion, sound-event type, and native driver transport. The 11-byte conversion table at `0x4585A0` maps **resource**
types 1..11 to categories:

```text
resource type:  1  2  3  4  5  6  7  8  9  10 11
category    :  0  0  0  0  4  4  4  1  4   2  3
```

Category **3** used by type **11** returns the input descriptor at
`0x458589` unchanged. Type **8** selects `0x458564 → 0x45AA00`,
and type **10** selects `0x458550 → 0x45A4A0`. These conversion functions
can resolve nested descriptors within an already selected type-11 event.
Transport class **1=SXD / 2=MIDI** is a *separate* selector inside
`0x458220`, unrelated to the *outer* Theme resource's type number.

All static checks above have been added to the read-only JSON inspector
`tools/inspect_rayman2_audio.py` and its synthetic-only tests. A future
comparison with the approved retail-CD binary is needed before using the
addresses as patch locations.

## Cross-checked sound-bank data structures (ACP_Ray2)

The open-source [ACP_Ray2 sound module](https://github.com/raytools/ACP_Ray2/tree/master/src/SND) supplies the original CPA structure names and matches **three exact addresses in the supplied EXE**: `SND_g_stBankDescriptionList = 0x514D50` (`SND.c`), `SND_fn_lSendRequestSound = 0x452D20`, and the bank-registry instructions at `0x4538C0`. The first address is exactly the global read in the native bank lookup; the second matches the native function prologue. This is strong evidence that the named layouts match this *reference* binary.

**Loaded bank descriptor** `SND_tdstSndBankDescriptorElement` (72 bytes, allocated at `0x4538E2`):

| Offset | Name | Native check |
| --- | --- | --- |
| `+0x04` | `lId` | `0x4538CD` compares bank ID |
| `+0x0C` | `szFileName[13]` | Named bank filename within the descriptor |
| `+0x24` | `p_stData` | `0x453FB2` loads bank head |
| `+0x3C` | `hNextBrotherDyn` | `0x4538D2` advances linked list |

The loaded bank head `SND_tdstSndBankHead` contains its optimized **resource list first pointer at `+0x0C`** and **resource count at `+0x10`**. Native `0x453F90` reads those exact fields and selects one **92-byte** `SND_tdstBlockResourceMem` record. This resolves the previously unnamed “resource groups”: they are **sound banks**. The packed upper16 value is the loaded bank **ID**. The non-approved reference and the **original retail-CD EXE independently** confirm the native bank-ID filename formatter `bnk_<decimal bank ID>.BNM` (see the pinned byte-parity section). The bank files and their contents themselves have not been inspected.

**Loaded resource record** `SND_tdstBlockResourceMem` (92 bytes):

| Offset | Actual field |
| --- | --- |
| `+0x00` | `Id`, a packed bank/resource ID, **not a pointer** |
| `+0x04` | `eType` (`SND_E_TR_Theme=11`, `Sample=1`, etc.) |
| `+0x08` | `eStorage` (external / megafile / bigfile) |
| `+0x0C` | `ucVolume`, one byte; next three bytes may be padding |
| `+0x10` | `bIsLoaded` |
| `+0x14` | `uRes`, union of in-memory resource definitions |

**Disk Theme descriptor**: [`SND_tdstBlockResourceDisk`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Resource.h) has its on-disk Theme union beginning at `+0x18`; the [`SND_tdstTypeThemeDisk`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Theme.h) has `ulNbParts` at union `+0x0C` (overall `+0x24`) and `lOffsetPartOutro` at union `+0x10` (overall `+0x28`). The native Theme fixup `0x459840` reads precisely these fields at `0x45985A` and `0x45987F`. Its collection elements are 24-byte `SND_tdstThemePart` entries.

### Corrected interpretation of the old diagnostic masks

The previous `desc_words_equal_mask` compares four words of **`SND_tdstBlockResourceMem`**, not four arbitrary pointers. A mask of `14` means **different resource IDs** but the same `eType`, `eStorage` and fourth DWORD. A mask of `6` means different **resource ID** and fourth DWORD; that last difference might be the volume byte or only another byte in that DWORD. **A matching SXD driver handle cannot establish the same resource identity.**

An older research probe incorrectly attempted to dereference the first DWORD of the resource record as a memory address. This interpretation was invalid because it is `Id`; the probe was bounded and read-only, but any output from it should be ignored. It has been **removed**. The research probe now only reports the decoded **bank group / resource index** and the actual volume byte for both compared records. No audio calls are altered.


### Original-CD dual-route proof for the same inner SXD handle

A further independent read of the pinned original CD PE, crosschecked byte-for-byte against the reference, verifies **both possible routes** from the native 64-slot audio dispatcher to the same inner transition SXD driver:

```text
0x4580B0 pause loop, eligible inner transition record (+0x10 != 0)
    0x4580E1 -> 0x4584A0 -> SXD PauseTransition

0x4580B0 pause loop, eligible outer type-11 Theme record (+0x10 == 0)
    0x4580EF -> 0x458070
       native resource-type pause table [0x4A3BA8 + 24*11] = 0x459C40
       0x459C4D -> 0x4584A0 -> SXD PauseTransition on Theme's inner record

0x458110 resume loop, eligible inner transition record
    0x458151 -> 0x458470 -> SXD ResumeTransition

0x458110 resume loop, eligible outer type-11 Theme record
    0x45815F -> 0x458090
       native resource-type resume table [0x4A3BAC + 24*11] = 0x459C60
       0x459C6D -> 0x458470 -> SXD ResumeTransition on Theme's inner record
```

All **six E8-relative call targets** and both **type-11 function-table pointers** were checked in the original retail-CD bytes and in the non-approved reference. They have been added to the SHA-pinned, read-only [CD parity inspector](../tools/check_rayman2_cd_audio_parity.py). The prior Wine schema-2 trace reports one active outer Theme at slot 6 pointing to the unique inner transition driver at slot 1; the driver sees two ESC pause and two ESC resume calls at the same global depth. The native code graph therefore supplies a **specific sufficient explanation for duplicate calls when both records are eligible**, without assuming the handle is a fixed music slot. It **does not prove** from the log alone that both pathways were taken at runtime; the exact live per-record pause eligibility and full caller stacks have not been captured.

A new [eight-case research-only synthetic dual-dispatch model](../tests/test_audio_dual_dispatch_model.py) exercises two eligible records, one ineligible record, inactive Theme, extra unrelated SFX, zero initial depth, and nested ESC/focus. In the straightforward `depth=1` state, both records have `pause_mark=0`, so ESC can pause the *same* driver twice, setting both native marks to 1 and global depth to 2. An additional Win32 focus pause at depth 2 dispatches **no** further driver pause; its matching resume from depth 3 to 2 can resume the same driver **twice while ESC remains open**. The following ESC script resume at depth 2 → 1 can then issue **two more native resumes**, since those mark values were cleared at focus gain. This is a structural demonstration of why dropping or compensating one low-level call alone is unsafe. It neither runs the original game nor makes audio playback changes.

**Implementation guard:** a future opt-in feature must reconcile the **two original participating records** (both of their native marks, logical Theme lifetime, and independent ESC/focus reasons), never just intercept one `PauseTransitionSxd` by driver handle. Even a post-dispatch `resume` is insufficient unless a subsequent focus loss can force an actual driver pause despite the game's already-set marks and focus gain cannot resume it inadvertently. Preserve `StopTransitionSxd` and level transitions unchanged; no F1 changes, INI key, DLL hook, or default behavior changes.



### Retail-CD Theme callback transaction guard and direct caller inventory

A new independent disassembly of the **approved CD** `Rayman2(3).exe` checks the mutable global `0x4B9618` (located in the zero-filled virtual data range, **not** file-backed at this VA). Exactly **six literal DWORD operands in the CD `.text`** reference it; the modified reference has the **same six operands at the same VAs**:

| Native VA | Operation | Source-backed interpretation |
| --- | --- | --- |
| `0x459694` | `mov [0x4B9618], edx` with `edx=0` from `0x459678` | Clear during initial controller data initialization for the no-parts branch |
| `0x4596E0` | `mov eax,[0x4B9618]; test eax,eax; jne 0x459751` | While the flag is nonzero, return before the callback's part-advance work |
| `0x45999D` | `mov dword [0x4B9618],1` | Mark beginning of a Theme start/replacement operation |
| `0x459B2C` | `mov dword [0x4B9618],0` | Clear at the end of that start operation |
| `0x459BE5` | `mov dword [0x4B9618],1` | Mark beginning of the Theme stop path |
| `0x459C23` | `mov dword [0x4B9618],0` | Clear after the stop has run and the controller first DWORD has been set to zero |

The address of the guarded callback `0x4596E0` is passed to `0x458220` at **`0x459AE5`**, whose creation path calls the SXD transition player. This strongly supports **a native callback-inhibition mechanism during Theme creation/stop**, not an arbitrary unused field. Importantly, **it is NOT a thread-safe lock, a guarantee of audio silence, or a reliable direct proxy for pause-menu state**. It may be accessed indirectly by code paths not found by literal operand scanning; the scan does not establish all possible callers or threads. The process's live value has not been observed.

Further, scanning every `E8 rel32` byte pattern in the CD `.text` and independently in the research reference returns precisely the following *direct-call candidates* for these six specific target functions:

| Target | Exact E8 site VAs in both images |
| --- | --- |
| Shared pause wrapper `0x4536D0` | `0x401C9C` (Win32), `0x471E70` (script) |
| Shared resume wrapper `0x453700` | `0x401CE9` (Win32), `0x471E67` (script) |
| Global pause loop `0x4580B0` | `0x4536ED` |
| Global resume loop `0x458110` | `0x45371D` |
| Native inner SXD transition pause `0x4584A0` | `0x4580E1` (inner) and `0x459C4D` (Theme delegated) |
| Native inner SXD transition resume `0x458470` | `0x458151` (inner) and `0x459C6D` (Theme delegated) |

These are **literal five-byte E8 call candidates**, cross-checked against disassembly at each known site. They are **not** a complete interprocedural call graph, and they **cannot exclude indirect calls** or custom return sites. They do establish that the two independently verified upstream callers to the shared wrapper are the script and Win32 origins, without relying on `GetAsyncKeyState(VK_ESCAPE)` as a menu detector.

The original CD's `.data` explicitly initializes native global pause depth `[0x4A3CC8]` to **1**, equal to the reference. That matches the runtime values at owner ESC events but is not sufficient to classify arbitrary pauses.

The original owner trace also shows the **same outer Theme ID (73:48) with different inner resources (73:50 → 73:51)**. An implementation that treats every inner sample change as a *new* soundtrack would incorrectly lose the logical Theme identity; an implementation that caches only the recycled SXD handle could accidentally resume a successor after stop. A future generation token must therefore distinguish *outer Theme creation/stop* from *inner part replacement*, and never compensate/forward a stale driver handle across either kind of lifecycle change.

The SHA-pinned [retail-CD audio parity tool](../tools/check_rayman2_cd_audio_parity.py) now has explicit checks for all six flag sites, callback registration, and the six sets of known E8 caller sites. Pure synthetic tests cover E8 scan, VA mapping, flag operands, and the important distinction between **selected-region equality** and **complete native-audio proof**. The first new synthetic CI run failed due to incorrectly requiring genuine CD transaction anchors even with `check_anchors=False`; this test-harness bug has been corrected without weakening real pinned-CD checks, and CI rerun. No actual game patch, audio hook, INI option or soundtrack file is introduced.


## Native pause/resume dispatch — verified indirection

`0x4580B0` is the game's global **pause** loop over 64 sound-table records. It increments nested depth at `0x4A3CC8`. `0x458110` is the inverse **resume** loop. Records whose `+0x10` transition flag is nonzero are dispatched through `0x4584A0` / `0x458470`, where SXD uses `0x4553A0` / `0x4553C0` and their dynamic slots `0x4B8708` / `0x4B870C`.

Records without the transition flag use `0x458070` / `0x458090`, which use native tables `[0x4A3BA8 + 24*event_type]` and `[0x4A3BAC + 24*event_type]`. For **event type 11** these table entries resolve to:

| Operation | Function | Effect |
| --- | --- | --- |
| Pause | `0x459C40` | If controller state is active, reads nested record at **controller +0x30**, then calls `0x4584A0` |
| Resume | `0x459C60` | If active, reads controller +0x30, then calls `0x458470` |

**Important architectural finding:** when both an inner transition record and its outer type-11 controller record participate in a native pause, two separate code paths can reach the *same* driver `PauseTransitionSxd`/resume entry. This is a concrete explanation **candidate** for the repeated calls to slot 1 observed at the same millisecond in Wine. It is **not fully proven as the cause**: the controller pointer and the true executed call stacks still need to be verified.

**Diagnostic correction:** the earlier `inspect_audio_sequence_context()` returned an initially-zero `AudioSequenceContext` whenever the controller DWORD was **not exactly 1** (`active!=1u`). The native `0x459C40` callback instead checks only **nonzero** (`cmp [eax],0; je ...`). Therefore the prior `sequence_active=0` log field **did not establish** the native DWORD was zero: an alternate nonzero state would have been silently reported as inactive. The optional research build now categorizes the state as `sequence_state_class` (0=unreadable, 1=zero, 2=one, 3=other nonzero), with `sequence_active` determined by nonzero state and a validated aligned record pointer. Existing Wine traces cannot reconstruct which class applied retrospectively; do not use them to claim the controller was idle.

## Exact nested script / focus-loss audio state machine

The code-level reason a naive "drop the Theme pause call" override is **not safe** is now independently reproduced without Wine. In the **non-approved reference EXE**:

- The **generic VM-script pause opcode 0x1AA** (`0x471E70`) and the Win32 window-event pause branch (`0x401C9C`) call the *same* `0x4536D0` wrapper, which calls global `0x4580B0` at `0x4536ED`. The VM opcode is **not in itself proof of ESC menu state**.
- The **generic VM-script resume opcode 0x1AB** (`0x471E67`) and the Win32 window-event resume branch (`0x401CE9`) call `0x453700`, which calls global `0x458110` at `0x45371D`.
- The global pause counter lives at **`0x4A3CC8`**. Each 32-byte sound-table record has `active` at **+0x14**, a separate skip/state field at **+0x08**, a transition flag at **+0x10**, and the **per-record pause mark at +0x1C**.
- `0x4580B0` loops over **64** slots. For every active record with `+0x08 == 0` and `pause_mark == 0`, it **stores the old global depth into +0x1C BEFORE dispatching** the native pause. It then increments global depth **after** walking all slots.
- `0x458110` first **decrements** global depth, then on active records whose `+0x08 == 0` and `pause_mark <= new_depth`, **clears +0x1C BEFORE dispatching** the native resume. This order is visible at `0x458116`, `0x45813C` and `0x458143`.

Narrow pseudocode for **one slot** (not a complete emulator of the game's audio subsystem):

```python
def native_pause(slot, depth):
    if slot.active and slot.other_state == 0 and slot.pause_mark == 0:
        slot.pause_mark = depth
        native_driver_pause(slot)  # nested Theme / SXD dispatches still apply
    return depth + 1

def native_resume(slot, depth):
    depth -= 1
    if slot.active and slot.other_state == 0 and slot.pause_mark <= depth:
        slot.pause_mark = 0
        native_driver_resume(slot)
    return depth
```

### Verified counterexample to a driver-only ESC bypass

Take a participating Theme-related record with **global depth = 1** and **pause mark = 0**, matching the *starting global depth* reported in the owner's older ESC traces:

| Operation | Depth before → after | Pause mark | Native driver pause request |
| --- | --- | --- | --- |
| ESC opens | 1 → 2 | 0 → 1 | **Yes**; a naive patch would drop **only this request** |
| Window loses focus | 2 → 3 | 1 → 1 | **No request**; record already marked |
| Window regains focus | 3 → 2 | 1 → 0 | **Native resume may be requested**, even though ESC remains open |

Thus a patch that merely swallows `PauseTransitionSxd` during ESC **can leave audio playing after Alt-Tab**, because the game records the pause as completed independently of the driver. It can also create a spurious resume at focus gain. There is an additional edge case at **depth = 0**: the first mark written is **0**, so the next nested pause can still invoke the driver a second time. Both cases are covered by a pure test model in [`tests/test_audio_pause_invariants.py`](../tests/test_audio_pause_invariants.py), with direct call-site, opcode and branch-order verification integrated into [`tools/inspect_rayman2_audio.py`](../tools/inspect_rayman2_audio.py). The model is a logical counterexample based on static code, **not a report that sound was actually heard after Alt-Tab**.

### Cross-check with existing Wine traces: two calls, one depth

The old owner logs, without any additional Wine test, corroborate the structural need to account for two possible pause paths. At the first ESC opening in `Rayman2Fix(20261113-091415).log`:

- `PauseTransitionSxd` at **45.605 s** and **45.606 s**, both `audio_origin=game-script-pause`, `sound_slot=1`, `sound_matches=1`, and `global_pause_depth=1`.
- The corresponding `ResumeTransitionSxd` calls at **55.988–55.989 s** both show `global_pause_depth=1`.
- Two later script-origin pause calls at **72.027–72.028 s** also have the same depth, slot and handle match; the native source-id comparison mask is `6` versus earlier `14`.

Because the global routine at `0x4580B0` increments depth **only after its full 64-record loop**, two identical driver calls observed at the same depth are **consistent with two routes being invoked by the same global operation**, not necessarily two separate calls to that global routine. The disassembly provides **two possible routes**: directly through the inner transition record and indirectly through the outer `SND_E_TR_Theme` record's controller callback `0x459C40`. The outer record has a distinct game-side handle, so `sound_matches=1` for the *driver* handle does not preclude two paths referring to it.

This remains a **constrained hypothesis, not proof of the executed callback stacks**, because the old trace sampled the driver interface, not both of its higher-level callers. The earlier `sequence_active=0` value is also ambiguous because its instrumentation only accepted a raw active state equal to exactly one. Do not turn this hypothesis into a bypass rule.

### Design requirements that follow

1. Establish the actual **Theme resource ID / bank ID** and current nested sound instance before any exclusion; an SXD transport, driver handle or sound-table index is insufficient to identify music.
2. Track **script pause** and **Win32 focus pause** as independent *causes*. Do not infer focus from ESC-key state; both invoke the same native wrappers but have distinct upstream return sites.
3. Preserve each legitimate **Theme stop, transition and level unload**, allowing driver handle recycling without stale overrides.
4. Preserve game sound-table and global depth consistency. If any Theme audio is deliberately kept running during a script pause, focus loss must explicitly suspend it and focus gain must restore it *only when the gameplay/menu state allows*.
5. Unknown resource identity or unverified focus-origin must **fall back to original audio behavior**. No experimental setting or driver-bypass is justified yet.
6. Verify the exact original retail-CD instructions independently before any hook design is merged, because these static VAs were observed in a non-approved reference EXE.

### Theme controller lifetime is explicitly gated

The direct x86 lifecycle of the **Theme singleton**, base `0x4B95B8`, is now verified in addition to its nested-slot `+0x30` dispatch:

| Operation | Native address | Result |
| --- | --- | --- |
| Initialization `0x4597E0` | `0x4597EC` | Sets `[0x4B95B8]=0` |
| New inner stream | `0x459AF4` | Stores its sound-record pointer at `0x4B95E8` |
| Start success check | `0x459B06` | Compares the stored record pointer with the `-1` sentinel |
| Successful Theme activation | `0x459B0E` | Sets `[0x4B95B8]=1` |
| Theme stop | `0x459C0D` | Sets first controller DWORD back to `0` |
| Theme pause / resume | `0x459C44`, `0x459C64` | Dispatches its nested sound only when controller first DWORD is **nonzero** |

The code therefore provides an ordinary zero/one lifecycle for these direct paths, but this does not prove indirect writes, alternate states or the value during an owner's historical ESC session. The previous diagnostic accepted only `state==1`, so its `sequence_active=0` result could not distinguish native zero from any other non-one value or a failed record read. The corrected research probe and static inspector leave that uncertainty explicit.

This *also* means it would be unsound to infer that **every** SXD transition pause is music or Theme-related. The actual active Theme resource/inner stream relation must be validated separately; an SXD driver handle alone is not an owner identity.

## Owner Wine observations already available

From `Rayman2Fix(20261113-091415).log`:

| Time (s) | Observed event | Transition handle | Description word equality |
| --- | --- | --- | --- |
| 6.357, 7.449 | `StopTransitionSxd` | Last played matches | `15`, all four words |
| 45.605–45.606 | Two `PauseTransitionSxd` calls, script-origin ESC | Last played matches | `14`: word 0 differs |
| 55.988–55.989 | Two `ResumeTransitionSxd` calls, script origin | Last played matches | `14` |
| 72.027–72.028 | Two further script-origin pause calls | Last played matches | `6`: words 0 and 3 differ |
| 75.529, 78.930 | Transition stop after second ESC | Last played matches | `6` |

The equal-descriptor mask is an *observational comparison of four 32-bit fields*, **not** a music classifier. No confirmed `windows-activation-pause` or loss-of-foreground audio event has been observed yet. Even successful correlation of the driver handle and source descriptor does not identify which audible track the instance represents.

## Archive table, not yet resource attribution

The static 24-pointer archive table at `0x4A2090` has three groups of eight. For discs 0, 1, 2, `SOUNDS*.DAT` occurs at indices **4, 12, 20** and `MUSIC*.DAT` at **5, 13, 21** (zero-based). These pointers establish archive *names*, not a direct connection to type-11 events. Indirect code reads of the base pointer at `0x44DE9D`, `0x44EB6C` and `0x44F13A` refer to the first archive entry, `LEVELS0.DAT`, rather than MUSIC archives. The reference EXE contains additional `.extra` sections that may be GOG-specific; do not interpret them as approved-CD filesystem behavior.

## Separating archive metadata from playable music

Public [Ray2Get documentation](https://github.com/Synthesis/ray2get) identifies the Rayman 2 PC **soundtrack itself** as sequences of `.apm` files that can be reconstructed/replaced per location (see its `file_sequences.txt`). A [community extraction discussion](https://raymanpc.com/forum/threads/does-anyone-know-how-to-rip-the-sound-files-from-the-pc-port-of-rayman.74978/) places these APM/BNM sound resources under `Data/World/Sound`. These are independent, external format descriptions, **not proof** that this binary's type-11 sound event was loaded from an APM, BNM or `MUSIC*.DAT` archive.

The three named `MUSIC*.DAT` entries in the *level archive directory* therefore should be treated as likely *metadata candidates*, not assumed to be the actual compressed waveform files. The next safe static question is how the event controller's descriptor is allocated and which resource identifiers it carries; do not substitute the filename-based music list for a runtime identity.

## File-path manifest corroboration (non-CD extra section only)

A direct bounded scan of the supplied `rayman2(4).exe` **`.extra2` section** finds **1,284 distinct `DATA\\WORLD\\SOUND\\*.APM` paths** and **405 distinct `*.BNM` paths**. All BNM entries are exactly `BNK_0.BNM` through `BNK_404.BNM`, with no gap. This extra section is **not present in the reference retail-CD assumptions**; its file-path entries establish filenames for a modified distribution, **not actual loaded audio bytes**, bank IDs, or corresponding original-CD files.

These path names independently overlap with the publicly documented [Ray2Get sequence list](https://github.com/Synthesis/ray2get/blob/master/file_sequences.txt) (2018): the main-menu music loops through `MM01` ... `MM10`, all of which appear in the extra2 inventory, and the introductory `JailMus` sequence corresponds to an available `JAILMUS.APM` filename. The inventory also contains `MAIN TITLE.APM`. This is a **filename cross-check**; it is not evidence that a specific Theme resource or driver handle plays a particular sequence during ESC.

A second inspection of the same section establishes its **complete binary record format**, not merely an ASCII filename search. It consists of repeating `<u32 declared_size><u32 path_length_including_NUL><ASCII filename><NUL>` entries, followed by a sentinel pair **`(0xFFFFFFFF, 0)`** and zero padding. A bounded sequential parser validates **3,716 entries**, **3,716 unique paths**, **1,284 APM** and **405 BNM** filenames. The remaining entries include unrelated game, driver and system assets. The sentinel appears at section-relative offset `0x22066`; the trailing `3,986` bytes are zero. These are all reproducible facts about **this modified reference EXE's** `.extra2` directory.

**Critical size caveat:** the sum of the *declared* sizes is **7,453,062,614 bytes (~7.45 GB)**. Several `MUSIC*.DAT`, `GRAPHICS*.DAT` and `SOUNDS*.DAT` entries claim hundreds of megabytes each, consistent with historically encountered artificially oversized CD directory records. Therefore these numeric fields are **unverified metadata, NOT actual byte counts and NOT file-integrity evidence**. Even plausible small declared sizes like `MM01.APM = 48,592` and `BNK_0.BNM = 6,642,012` are only declarations until checked against real extracted files. **Do not use this table for original-CD extraction, manifests, hash verification, or deciding which file length to trust**.

The offline PE inspector's optional `reference_extra2_file_manifest` section is SHA-gated to the **exact supplied non-approved executable** and now validates the complete record stream as well as aggregate path counts. Its new `declared_bytes_total_not_integrity`, `sound_declared_bytes_not_integrity`, `sizes_verified_against_files=false` and `approved_cd_metadata=false` fields make this limitation explicit. Its synthetic tests reject missing sentinels, duplicate records, truncated lengths, embedded NULs and nonzero padding. The tool neither exports content nor claims a stock CD contains these paths.

## Research-only BNM metadata reader and ESC focus policy model

**Scope and caveat:** the available research workspace has the owner-supplied non-approved PE32 reference binary and older Wine logs, **but not a byte-for-byte BNM bank from a verified original CD**. The experimental readers below must not be described as validated retail format support, executable patching, working menu music, or an install/integrity implementation.

### BNM metadata layout: native table offsets, not inferred placement

Independent reference sources: [Rayman2Lib `BNMFile.cs`](https://github.com/szymski/Rayman2Lib/blob/master/csharp_tools/Rayman2Lib/BNMFile.cs), [ACP_Ray2 `SND.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND.h), [`SND_Event.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Event.h), and [`SND_Resource.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Resource.h). The **non-approved** owner-supplied EXE's native loader `0x453990` reads precisely 44 bytes at `0x4539E7`.

**Important correction to the original research-only BNM inspector:** the two lists are **independently offset-addressed** within the 44-byte `SND_tdstSndBankHead`, **not guaranteed to be adjacent**. The original scanner incorrectly assumed `resource_start=44+event_count*32`; that could read an unrelated region and produce false Theme identifiers.

| Header byte offset | Native field | Verification |
| --- | --- | --- |
| `+0x00` | Bank format version | ACP_Ray2 |
| `+0x04` | `stEventList.hFirstElementOpt`, on-disk **relative offset** | Native `0x453BB8`: `mov ecx,[eax+4]`; `0x453BBB`: add header base |
| `+0x08` | Event count | `0x453C0C`; 32-byte event records |
| `+0x0C` | `stResList.hFirstElementOpt`, on-disk **relative offset** | Native `0x453BC3`: `mov ecx,[eax+0xC]`; `0x453BC6`: add header base |
| `+0x10` | Resource count | `0x453BCE` followed by 92-byte stride computation |
| `+0x14..+0x24` | Declared media offsets (MPEG, MIDI, sample, ADPCM, streaming) | ACP_Ray2; **origin/physical sizes not verified** |
| `+0x28` | `ulTotalSize`, a declared total | Not an independent file-integrity reference |

**Event-to-resource structural mapping:** `SND_tdstBlockEvent` contains `Id (+0)`, `eType (+4)`, `uParams (+8)`, in 32-byte records. For event type `SND_EVT_Son_Play=1`, `uParams.stPlay.uResource.Id` at `+8` is a packed resource reference: the native type-1 dispatch table entry at `0x453E90` selects `0x453DC9`, which loads that DWORD and calls resource resolver `0x453F90` at `0x453DD6`. Native `0x453F90` uses **upper 16 bits for the loaded bank ID and lower 16 bits as a *direct resource table index***; it does **not** search the resource entries by their `Id` fields. Therefore a `Play` → local `Theme` match requires that the indexed record's full packed `Id` equals the original reference **and** `eType=11`. Any discrepancy, a missing local bank, or a reference beyond the local table is reported as unresolved. Theme playback from another bank cannot be linked from one BNM alone.

The updated read-only [`tools/inspect_rayman2_bnm.py`](../tools/inspect_rayman2_bnm.py) validates independent event/resource ranges within the physical bank, rejects nonzero counts with nonexistent ranges and any overlap, reports `Theme=11` candidate metadata, and emits **only local candidate** `SND_EVT_Son_Play` → Theme references when the direct-index/full-ID test succeeds. It never decodes, extracts, modifies, or emits sound payloads. Use it locally on user-owned banks:

```bash
python3 tools/inspect_rayman2_bnm.py /path/to/DATA/WORLD/SOUND/BNK_0.BNM
```

The output includes `table_ranges_file_relative`, `play_event_references_candidate`, and `local_play_events_referring_to_themes_candidate`. These describe **candidate static structure**, not which song is heard in game, nor whether the file is an original retail bank. `verified_against_actual_retail_cd_bnm=false` and `audio_extracted=false` are mandatory. Media offsets, claimed data lengths and bank filename suffixes are **not authoritative** for CD installation or integrity. The [synthetic-only tests](../tests/test_audio_bnm_inspector.py) exercise contiguous, gapped, **reversed** lists, truncated records, overlapping or invalid offsets, null/other-bank event references, and deliberate resource-ID/index mismatch. No commercial bank/media assets are added to CI.

### Why raw Theme part data is not yet directly addressable

An additional native constraint was verified in the Theme fixup routine: at `0x459847` the loader reads the disk resource's `ulDataOffset` from `[ecx+0x10]`, at `0x45984A` obtains a **separately supplied data-segment base** and at `0x459853` adds the two before walking 24-byte Theme part records. The caller `0x457A10` receives that base from the bank resource converter, which selects different storage/media blocks. This means **`ulDataOffset` is not yet proven to be an absolute byte offset from the beginning of a BNM file**. Parsing `ThemePart` bytes at `file[ulDataOffset]` would be another unjustified shortcut.

Accordingly the metadata inspector reports `disk_data_offset_declared_unverified`, `number_of_parts_candidate` and `outro_offset_candidate`, but **does not dereference the Theme part list or claim it decoded a particular `.APM` track**. The necessary next offline task is to reconstruct each storage mode's data-segment base and validate on a genuine BNM. This protects us from misidentifying music or reading unrelated data.

### Filename mapping proven in the supplied reference EXE

A previously open gap is now closed **for this exact reference executable**. The native `SND_tdstSndBankDescriptorElement` allocator `0x4538E0` stores the numeric `lId` argument at **descriptor+4** (`0x453945`) and passes that **same argument** to `0x453870` at **`0x453948`**. The formatter pushes a literal format string from VA `0x4A252C`, whose bytes decode exactly to:

```text
bnk_%d.BNM
```

Its output is copied into descriptor `szFileName[13]` starting at **+0x0C** (`0x45392E`). Therefore for the supplied reference EXE, **bank ID 23 deterministically formats to `bnk_23.BNM`** (for a valid short numeric name). This is not inferred from the unrelated `.extra2` filename inventory; it is a direct functional mapping visible in the sound-bank loader.

The revised BNM inspector additionally reports `candidate_reference_bnm_filename` **only** if all on-disk resource IDs have the same upper16 bank ID and each low16 component equals its actual resource table index, precisely as `0x453F90` resolves references. Inconsistent indices, mixed bank IDs or empty resource lists yield no candidate filename. This still **does not** identify a particular audible song: a real BNM is required for the Theme resource identity, and independent retail-CD equivalence remains pending.

The standalone PE inspector verifies the exact format-string bytes, the `lId` assignment and the formatter call. No game binary, audio payload or game runtime is changed.

### Proposed independent-cause pause policy — specification only

The new [`tools/audio_pause_policy_model.py`](../tools/audio_pause_policy_model.py) expresses a **proposed desired audio state** for a future *proven-owned Theme stream*. It is **not connected** to the runtime or the native driver and does not fix the game's sound record pause marks (`+0x1C`) or global pause depth.

Conceptually, music may remain audible through **verified ESC-menu pauses only**, while all these causes require silence: focus loss, unknown focus state, any other/native game pause, unknown pause origin or actual Theme stop/level unload. The model also requires a resource ID and a generation token: an old stop event must never affect a newly started theme whose driver handle was recycled. Re-entering the foreground may restore the Theme only while still valid and otherwise eligible.

This is a deliberately narrow **fail-closed state specification**, not a simple `PauseTransitionSxd` interception. [Synthetic tests](../tests/test_audio_pause_policy_model.py) cover ESC→focus loss→focus gain, closing ESC while unfocused, unknown script causes, nested script causes, other audio pauses, level stops, stale generations and an exhaustive set of **4,096** short event sequences, with invariants that no owned Theme can be playing in the background or during an independently active pause cause. No F1 controls are introduced.

**Remaining integration work:** determine the actual Theme bank/resource ownership of the audible soundtrack and verify a way to reconcile this desired state with original `0x4580B0`/`0x458110` markers and driver calls. A model passing synthetic tests is **not** evidence the feature works or is safe under Wine/Windows; original-CD parity and one consolidated end-to-end test are still mandatory before release.

## Correction after owner Wine test — 13 November 2026, 14:40

A critical mismatch in our **research instrument**, not an absence of level music, was identified by comparing `Rayman2Fix(20261113-144018).log` with native x86 disassembly.

- Native allocator `0x4574E0` computes `lea eax,[ecx+0x4B8C98]` at **`0x45752C`** where `ecx=32*ordinal`: it returns a **pointer to an actual 32-byte sound-table record**, not the ordinal.
- Native Theme controller `0x459950` stores that pointer at **`0x4B95E8`** (`0x459AF4`).
- Native Theme pause `0x459C40` and resume `0x459C60` read `[controller+0x30]` and **pass this pointer directly** to `0x4584A0` / `0x458470`. They do *not* convert a slot index.
- Our previous probe wrongly tested the raw pointer with `slot>=64`, so it returned `sequence_slot=-1` and `inner_slot=-1` even with **controller state 1**. The resulting `theme_owned_driver=0` **cannot be used as evidence against Theme ownership**.
- The corrected probe takes the raw pointer as an **unsigned 32-bit address**, checks the pointer falls inside `[module+0xB8C98,module+0xB8C98+64*0x20)`, rejects misalignment and null/`-1` sentinels, and only then converts it to an index `(ptr-base)/0x20`. It never dereferences unvalidated addresses. The new `sequence_ref_class` and `inner_ref_class` distinguish the valid aligned-record-pointer class (3) from null (1), sentinel (2), unreadable (0), or invalid (4). Ordinary audio playback is unchanged.

**Test facts, not assumptions:** the new log has two ESC-triggered Theme-like transition pauses at **49.802–49.803 s**, two resumes at **59.502–59.503 s**, and a second pair of pauses at **63.735–63.736 s**. Each driver call is `game-script-pause` or `game-script-resume`, `sound_slot=1`, `sound_matches=1`, with native controller `sequence_state_class=2` meaning native active DWORD **exactly one**, and **one unique outer resource Theme** in native slot 8: **bank 73, resource 48**. The paused inner driver record reports resource **bank 73, resource 51**. This strongly motivates verifying their relationship with the corrected pointer decoder, but the old instrumentation failed to record it. The log contains no `foreground=0` event, so Alt-Tab has not been validated. A separate `StopTransitionSxd` at **67.387 s** while global depth 2 confirms the importance of forwarding legitimate stops.

This correction supersedes all earlier references in this report that described the Theme's `+0x30` value as a **numeric sound-table index**. Do not use the old `inner_slot=-1` or `sequence_slot=-1` observations to reject Theme attribution.

## Current next step: prove Theme ownership from one native pause event

The goal remains **level music audible during the in-game ESC pause**, while SFX obey their normal pause, the original Theme stop/change operations remain effective, and all audio pauses on loss of window focus. No F1 handling is in scope.

We now have a **concrete structural source-ID test**, independent of a guessed fixed sound-table slot:

1. The 64-entry native sound table `0x4B8C98` stores the **outer** Theme handle returned by `0x459950`/`0x459B3A`, which is the address of the Theme singleton `0x4B95B8`; its resource descriptor must have `SND_E_TR_Theme=11` at `+4`.
2. The same singleton's `+0x30` field at `0x4B95E8` contains the **inner** sound-record pointer, not a numeric index. The inner record must be active, transition-enabled, carry a native SXD resource type `1`, and own a **unique** driver handle.
3. Only if the paused driver handle is exactly this unique inner handle is `theme_owned_driver=1`. The outer Theme resource's packed ID identifies its **BNM bank** (upper16) and resource table index (lower16). The inner resource ID is logged separately. Any absent, inactive, shared or ambiguous record yields **no confirmed owner**.

This is implemented as a **read-only, bounded** `inspect_audio_theme_owner()` probe in the optional `RAYMAN2FIX_AUDIO_TRACE` build only. It reports a separate `RUNTIME-AUDIO-THEME-OWNER` line for the first eight native `PauseTransitionSxd`, `ResumeTransitionSxd` and `StopTransitionSxd` events, with `outer_candidates`, `outer_slot`, `theme_bank`, `theme_resource`, `inner_slot`, `inner_bank`, `inner_resource`, `inner_active`, `inner_transition`, `inner_handle_matches`, and `theme_owned_driver`. No raw pointers or resource-file contents are logged; no driver argument, call order or return value is changed. The ordinary release build compiles the probe out entirely.

The previous `Rayman2Fix(20261113-091415).log` did **not** measure these outer/inner relationships. Although a driver handle matched one native transition record at ESC, **there is no proof yet that it was owned by the Theme controller**. Once that is established, the next safety gate remains reconciling native per-record pause marks and nested script/focus pause depths; naively suppressing one driver call is unsafe. Do **not** enable an experimental INI setting before that gate is resolved. A single consolidated Wine/Windows test of the research-only build (ESC open/close, Alt-Tab while ESC is open, real level transition) can later check the remaining runtime assumptions.

## Safety requirements before a functional `MusicInPauseMenu` option

- Prove a **music resource identity** independently of driver pointer, table index, SXD transport, default volume or descriptor equality.
- Determine the true active type-11 controller state at ESC and the origin of **each** pause/resume call (including nested controller forwarding).
- Preserve ordinary `StopTransitionSxd` at menu/level transitions. Never prevent a legitimate stop.
- Analyze nested `0x4A3CC8` pause-depth behavior carefully: suppressing an ESC driver pause might leave the record marked as already paused, **potentially preventing a later Alt-Tab focus loss from issuing another pause call**. This is a risk deduced from the static global-pause loop, **not yet confirmed in Wine**. A safe implementation will need a deliberately designed state machine for focus loss and correct resume.
- Preserve SFX, ordinary MIDI/CD audio calls, shared Windows/Wine behavior; ignore F1 entirely.
- Keep the normal build free of instrumentation. Do not expose a nonfunctional INI setting or patch the non-approved reference EXE.

## Reproduce offline

```bash
sha256sum 'rayman2(4).exe'
objdump -d -M intel --start-address=0x457E60 --stop-address=0x457F9E 'rayman2(4).exe'
objdump -d -M intel --start-address=0x459950 --stop-address=0x459B46 'rayman2(4).exe'
objdump -d -M intel --start-address=0x4580B0 --stop-address=0x45816D 'rayman2(4).exe'
objdump -d -M intel --start-address=0x459C40 --stop-address=0x459C75 'rayman2(4).exe'
```

### Next offline work

Trace the resource origin of event class 11 via the seven upstream callers; check where their event descriptors are allocated/loaded and connect them to an actual `MUSIC*.DAT` entry if the code proves such a link. Verify controller lifecycle and one-versus-two-pause provenance against the approved CD image when available. Only request a consolidated Wine test for the specific ambiguities remaining after this static reconstruction.


## Owner Wine trace 2026-11-13 17:12 — Theme ownership proven at the driver

**Source/provenance:** owner-supplied \`Rayman2Fix(20261113-171247).log\` (168 lines, 45,702 bytes, SHA-256 \`068adc77c094b0729d10e1b7b62264aecd55e7b1e9e474b00ae0d9bf598dcdf4\`). This is the **first** observed log with the corrected \`RUNTIME-AUDIO-TRACE: enabled=1 schema=2 inner_record_ref=checked-pointer\` format and actual \`inner_ref_class=3\` / \`sequence_ref_class=3\`. This supersedes the prior stale-instrumentation evidence, not the native code audit. Instrumentation remains read-only, original driver calls forwarded unchanged.

**Observed ESC audio cycles** (runtime milliseconds are elapsed time, not wall clock):

| Event | Time | Outer Theme resource | Inner SXD resource | Native depth | Evidence |
| --- | --- | --- | --- | --- | --- |
| ESC script pause, transition call twice | 87.431–87.432 s | bank 73, resource 48, outer slot 6 | bank 73, resource 50, inner slot 1 | 1 at call, then 2 at 87.748 s | both \`PauseTransitionSxd\` events: \`theme_owned_driver=1\`, \`outer_candidates=1\`, \`inner_handle_matches=1\`, \`sequence_state_class=2\` (native state exactly 1), \`inner_ref_class=3\` (valid aligned record pointer); several other \`PauseSxd\` sounds also paused |
| ESC script resume, transition call twice | 106.431–106.432 s | bank 73, resource 48, outer slot 6 | bank 73, resource 50, inner slot 1 | 1 at call, remains 1 after 106.748 s | both \`ResumeTransitionSxd\` events also \`theme_owned_driver=1\`; \`ResumeSxd\` operates on other sounds |
| Second ESC script pause, transition call twice | 133.663–133.664 s | bank 73, resource 48, outer slot 6 | **bank 73, resource 51**, inner slot 1 | 1 at call, then 2 at 133.696 s | both \`PauseTransitionSxd\` events \`theme_owned_driver=1\`; native inner resource changed while outer Theme ID stayed the same |
| Transition stop **while ESC remains paused** | 138.032 s | bank 73, resource 48, outer slot 6 | bank 73, resource 51 | **2** | \`StopTransitionSxd\` still \`theme_owned_driver=1\`; the stopped Theme is part of a legitimate native transition and must not be frozen |
| Subsequent transition stop | 141.667 s | bank 89, resource 90, outer slot 0 | bank 89, resource 81 | **2** | distinct Theme identity, \`StopTransitionSxd\` with \`theme_owned_driver=1\`, reinforcing that stop and Theme replacement can occur while ESC pause depth remains elevated |

**Interpretation:** the corrected pointer decoder establishes the *structural Theme owner → inner SXD transition → exact paused driver handle* association for ESC and resume, rather than guessing from mutable slot 1 or recycled handles. The same outer Theme (bank 73/resource 48) now corresponds to inner resource 50 at the first pause and 51 at the second: **inner resource identity is not a stable proxy for the logical Theme owner**. It is still **not independently confirmed which Theme corresponds to the audible level soundtrack** or whether bank 73 is present and structurally valid on the original retail CD. The reference-only filename formatter would produce \`bnk_73.BNM\`, but the actual owner CD bank file has not been examined here.

Both ESC openings are classified \`audio_origin=game-script-pause\`, the first closing as \`game-script-resume\`; two transition calls per opening/closing have distinct observed stack candidate sequences. This supports multiple native dispatch layers, **not** a license to drop either call. Other SXD sound records have separate pause/resume events; no global bypass is acceptable. The log contains only \`foreground=1\` and **no** independently observed Win32 focus-out/in sequence, so Alt-Tab nested pause safety is still untested.

**Decision/gates:** do **not** implement a driver-only \`PauseTransitionSxd\` skip, which would leave original per-record native pause marks and global pause depth unreconciled; a subsequent focus-loss pause could then be suppressed or a focus return could resume audio while ESC remains open. Preserve **all** \`StopTransitionSxd\` calls, including at native pause depth 2. Keep the current default runtime unchanged, no \`MusicInPauseMenu\` INI key, no F1. Continue **offline** by verifying original-CD Theme resource metadata and designing an origin-aware, generation-safe synchronization between native pause marks and driver state; only then consider a consolidated Wine/Windows ESC+focus+level-transition test. A \`theme_owned_driver=1\` trace proves ownership, **not** working music-in-pause playback.
