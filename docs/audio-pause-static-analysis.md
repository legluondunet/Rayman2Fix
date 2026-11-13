# Rayman 2 PC — static map of ESC audio pause and the type-11 transition controller

Research note, **13 November 2026**. This document records the source of each binary-level claim and separates the *confirmed static mechanism* from hypotheses about the soundtrack. No licensed game executable or assets are redistributed.

## Scope and provenance

- Source: owner-supplied `rayman2(4).exe`, PE32 / x86, image base `0x00400000`, SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`, size **1,468,928 bytes**.
- This binary is **not** the approved original, protected 676,352-byte CD `Rayman2(3).exe` (SHA-256 starts `7927a106`). It is a **static reference only**. RVA matches in historical approved-CD runtime logs help constrain research but do **not** authorize binary patching.
- Tools: `objdump -d -M intel`, `objdump -p`, and Python's built-in `struct`, running locally. All addresses below are x86 image VAs, not file offsets. For this image's `.text` and `.data` the file offset equals `VA-0x400000`.
- The existing optional `RAYMAN2FIX_AUDIO_TRACE` build changes no audio arguments, return values, or playback behavior. The normal build does not include its hooks.

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
        -> 0x459AF4: store nested sound-table index at 0x4B95E8
     -> 0x457F92: save *outer* event/controller record (separate)
```

Critical distinctions:
- The game sound table begins `0x4B8C98` and has **64 entries of 0x20 bytes**.
- A table record stores the **driver handle at +0**, **game-native descriptor at +4**, **transition flag at +0x10**, and **active flag at +0x14** (see `0x4575D5–0x4575F0`). The handle is not a song ID, and equal handles do not entail equal source descriptors.
- `0x459950` maintains its type-11 controller at `0x4B95B8`. The nested sound-table entry index lives at controller **+0x30** (`0x4B95E8`). This is structurally separate from the *outer* type-11 sound-table record. It is inappropriate to treat one fixed table slot as “music.”
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

The loaded bank head `SND_tdstSndBankHead` contains its optimized **resource list first pointer at `+0x0C`** and **resource count at `+0x10`**. Native `0x453F90` reads those exact fields and selects one **92-byte** `SND_tdstBlockResourceMem` record. This resolves the previously unnamed “resource groups”: they are **sound banks**. The packed upper16 value is the loaded bank **ID**. In this **non-approved reference EXE**, the native filename formatter maps it to `bnk_<decimal bank ID>.BNM` (verified below); this correspondence is not independently verified against the original retail CD.

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

## Native pause/resume dispatch — verified indirection

`0x4580B0` is the game's global **pause** loop over 64 sound-table records. It increments nested depth at `0x4A3CC8`. `0x458110` is the inverse **resume** loop. Records whose `+0x10` transition flag is nonzero are dispatched through `0x4584A0` / `0x458470`, where SXD uses `0x4553A0` / `0x4553C0` and their dynamic slots `0x4B8708` / `0x4B870C`.

Records without the transition flag use `0x458070` / `0x458090`, which use native tables `[0x4A3BA8 + 24*event_type]` and `[0x4A3BAC + 24*event_type]`. For **event type 11** these table entries resolve to:

| Operation | Function | Effect |
| --- | --- | --- |
| Pause | `0x459C40` | If controller state is active, reads nested record at **controller +0x30**, then calls `0x4584A0` |
| Resume | `0x459C60` | If active, reads controller +0x30, then calls `0x458470` |

**Important architectural finding:** when both an inner transition record and its outer type-11 controller record participate in a native pause, two separate code paths can reach the *same* driver `PauseTransitionSxd`/resume entry. This is a concrete explanation **candidate** for the repeated calls to slot 1 observed at the same millisecond in Wine. It is **not fully proven as the cause**: the controller pointer and the true executed call stacks still need to be verified.

**Diagnostic correction:** the earlier `inspect_audio_sequence_context()` returned an initially-zero `AudioSequenceContext` whenever the controller DWORD was **not exactly 1** (`active!=1u`). The native `0x459C40` callback instead checks only **nonzero** (`cmp [eax],0; je ...`). Therefore the prior `sequence_active=0` log field **did not establish** the native DWORD was zero: an alternate nonzero state would have been silently reported as inactive. The optional research build now categorizes the state as `sequence_state_class` (0=unreadable, 1=zero, 2=one, 3=other nonzero), with `sequence_active` determined by nonzero state and a validated table slot. Existing Wine traces cannot reconstruct which class applied retrospectively; do not use them to claim the controller was idle.

## Exact nested ESC / focus-loss pause state machine

The code-level reason a naive "drop the Theme pause call" override is **not safe** is now independently reproduced without Wine. In the **non-approved reference EXE**:

- Both the game-script ESC pause (`0x471E70`) and Win32 window deactivation pause (`0x401C9C`) call the *same* `0x4536D0` wrapper, which calls global `0x4580B0` at `0x4536ED`.
- Both ESC resume (`0x471E67`) and Win32 reactivation resume (`0x401CE9`) call `0x453700`, which calls global `0x458110` at `0x45371D`.
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
| New inner stream | `0x459AF4` | Stores its sound-table index at `0x4B95E8` |
| Start success check | `0x459B06` | Compares the stored index with the `-1` sentinel |
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
