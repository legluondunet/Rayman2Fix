# Rayman 2 PC — static map of ESC audio pause and the type-11 transition controller

Research note, **13 November 2026**. This document records the source of each binary-level claim and separates the *confirmed static mechanism* from hypotheses about the soundtrack. No licensed game executable or assets are redistributed.

## Scope and provenance

- Source: owner-supplied `rayman2(4).exe`, PE32 / x86, image base `0x00400000`, SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`, size **1,468,928 bytes**.
- This binary is **not** the approved original, protected 676,352-byte CD `Rayman2(3).exe` (SHA-256 starts `7927a106`). It is a **static reference only**. RVA matches in historical approved-CD runtime logs help constrain research but do **not** authorize binary patching.
- Tools: `objdump -d -M intel`, `objdump -p`, and Python's built-in `struct`, running locally. All addresses below are x86 image VAs, not file offsets. For this image's `.text` and `.data` the file offset equals `VA-0x400000`.
- The existing optional `RAYMAN2FIX_AUDIO_TRACE` build changes no audio arguments, return values, or playback behavior. The normal build does not include its hooks.

## Two different kinds of type

1. **Sound-event class**: `[event_descriptor+4]` selects a branch in `0x457E60`, with a table of eleven 32-bit destinations starting at `0x457FA0`. Branch **11** is `0x457F21`.
2. **Driver transport**: `[native_descriptor+4]` in `0x458220` chooses **1=SXD** versus **2=MIDI**. Type 11 is *not* a third native transport.

The jump table was decoded from the file, not guessed from contiguous disassembly:

```text
case 1..11 targets at 0x457FA0:
457E8F 457EAA 457EC5 457EE0 457F78 457F02
457F78 457F5D 457F78 457F42 457F21
```

## Confirmed transition creation chain

```text
0x457E60: dispatch event class 11
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

The type-11 controller does not merely store an opaque driver handle. Its
source data has a variable number of nested entries and a reference-fixup
stage. This is a promising **resource provenance** path that can be mapped
without launching Wine, provided the actual resource databases are present.

### Packed source identifiers: `0x453F90`

The original x86 instructions at `0x453F90–0x453FC9` resolve a **DWORD
resource identifier**, with a guard for `0xFFFFFFFF`:

```text
packed_id (DWORD)
  upper 16 bits -> runtime resource-group key
  lower 16 bits -> record index in the loaded group

0x4538C0: registry lookup in linked list at 0x514D50
           group key = [node+4], next node = [node+0x3C]
0x453F90: group_base = [[node+0x24]+0x0C]
           record_count = [[node+0x24]+0x10]
           if index >= record_count: return NULL
           record_ptr = group_base + 92 * index
```

The **92-byte record stride** is obtained from real instructions,
`lea ecx,[esi+esi*2]`, `shl ecx,3`, `sub ecx,esi`,
`lea eax,[edx+ecx*4]`: `4 × (24 - 1) = 92`. The group ID is a key
in a **runtime registry**, **not** automatically a disc index, archive number,
CD track or music ID. No concrete packed ID has yet been recovered from an
actual level's resources.

### Type-11 resource fixups: `0x459840`

`0x459840` translates an event descriptor into controller-owned memory.
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

Do not conflate event class, event descriptor conversion, and native driver
transport. The 11-byte conversion table at `0x4585A0` maps event
classes 1..11 to categories:

```text
event class :  1  2  3  4  5  6  7  8  9  10 11
category    :  0  0  0  0  4  4  4  1  4   2  3
```

Category **3** used by type **11** returns the input descriptor at
`0x458589` unchanged. Type **8** selects `0x458564 → 0x45AA00`,
and type **10** selects `0x458550 → 0x45A4A0`. These conversion functions
can resolve nested descriptors within an already selected type-11 event.
Transport class **1=SXD / 2=MIDI** is a *separate* selector inside
`0x458220`, unrelated to an event class number.

All static checks above have been added to the read-only JSON inspector
`tools/inspect_rayman2_audio.py` and its synthetic-only tests. A future
comparison with the approved retail-CD binary is needed before using the
addresses as patch locations.

## Native pause/resume dispatch — verified indirection

`0x4580B0` is the game's global **pause** loop over 64 sound-table records. It increments nested depth at `0x4A3CC8`. `0x458110` is the inverse **resume** loop. Records whose `+0x10` transition flag is nonzero are dispatched through `0x4584A0` / `0x458470`, where SXD uses `0x4553A0` / `0x4553C0` and their dynamic slots `0x4B8708` / `0x4B870C`.

Records without the transition flag use `0x458070` / `0x458090`, which use native tables `[0x4A3BA8 + 24*event_type]` and `[0x4A3BAC + 24*event_type]`. For **event type 11** these table entries resolve to:

| Operation | Function | Effect |
| --- | --- | --- |
| Pause | `0x459C40` | If controller state is active, reads nested record at **controller +0x30**, then calls `0x4584A0` |
| Resume | `0x459C60` | If active, reads controller +0x30, then calls `0x458470` |

**Important architectural finding:** when both an inner transition record and its outer type-11 controller record participate in a native pause, two separate code paths can reach the *same* driver `PauseTransitionSxd`/resume entry. This is a concrete explanation **candidate** for the repeated calls to slot 1 observed at the same millisecond in Wine. It is **not fully proven as the cause**: the controller pointer and the true executed call stacks still need to be verified.

**Diagnostic correction:** the earlier `inspect_audio_sequence_context()` returned an initially-zero `AudioSequenceContext` whenever the controller DWORD was **not exactly 1** (`active!=1u`). The native `0x459C40` callback instead checks only **nonzero** (`cmp [eax],0; je ...`). Therefore the prior `sequence_active=0` log field **did not establish** the native DWORD was zero: an alternate nonzero state would have been silently reported as inactive. The optional research build now categorizes the state as `sequence_state_class` (0=unreadable, 1=zero, 2=one, 3=other nonzero), with `sequence_active` determined by nonzero state and a validated table slot. Existing Wine traces cannot reconstruct which class applied retrospectively; do not use them to claim the controller was idle.

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
