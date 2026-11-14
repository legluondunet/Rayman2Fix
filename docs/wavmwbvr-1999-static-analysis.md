# Rayman 2: static audit of the owner's original WAVMWbvr.dll

**Status:** research-only, **no native driver patch / no new intercepted audio calls**. 14 November 2026. This is static inspection of the **exact binary uploaded by the owner**, not a runtime trace, inference from `snd_cpa.ini`, or assumptions about another retail/GOG DLL.

## Binary identity

| Attribute | Observed |
| --- | --- |
| File | `wavmwbvr.dll` (matches `WAVMWbvr.dll` in owner's Windows log) |
| Size | **177,152 bytes** |
| SHA-256 | `cd0a0ad0a95880cd8e25e50844ec8e6f9d6725bbee7c09a8ab328cbbc953f016` |
| Format | PE32 DLL / Intel x86 / image base `0x10000000` |
| PE time stamp | `0x38076FB4` (15 October 1999, 18:17:24 UTC) |
| Code section | `.text`, RVA `0x1000`, virtual size `0x1B68C` |
| Debug info | CodeView NB10 string contains `winmm/mmx/WAVMWBVR.pdb` (path embedded in original DLL; does **not** mean the PDB is available) |

The PE import table includes `WINMM.dll` `waveOutOpen`, `waveOutWrite`, `waveOutReset`, `waveOutClose`, and `waveOutPrepareHeader`, plus WinMM mixer operations. An import alone does **not** prove that a particular Pause/Resume implementation directly invokes those APIs.

The original owner runtime log `Rayman2Fix(20261114-201946).log` reported `[DLL_Sxd] Default=WAVPW File0=WAVMW` but resolved all **four real original callbacks** into `WAVMWbvr.dll`. The PE export table independently confirms these exact entry points:

| Exported function (stdcall, one 32-bit argument) | RVA | Internal role |
| --- | --- | --- |
| `_SND_fn_vPauseTransitionSxd@4` | `0xC7E0` | Type-tag dispatcher |
| `_SND_fn_vResumeTransitionSxd@4` | `0xC830` | Type-tag dispatcher |
| `_SND_fn_vStopSxd@4` | `0xB920` | Normal sound Stop; reads tag from **argument + 8** |
| `_SND_fn_vStopTransitionSxd@4` | `0xC790` | Type-tag dispatcher; reads tag from **argument + 0** |

**Critical argument distinction:** `PauseTransitionSxd`, `ResumeTransitionSxd`, and `StopTransitionSxd` immediately dereference their argument as a pointer to a structure with a **type tag at offset 0 and a backend payload pointer at offset 4**. Thus the `DWORD arg` logged as a native *handle* by Rayman2Fix represents an opaque **transition-object pointer** at this exported layer; the same `arg` in two calls proves source-paired object equality, **not** an independently verified physical WinMM voice handle or permanent stream lifetime. `StopSxd` is **not** layout-identical: its dispatcher reads type at `arg + 8`. Never apply the Transition-layout assertion to all native sound APIs.

## Exact transition dispatcher branch map

In the reviewed file, `PauseTransitionSxd` at `0xC7E0` inspects `[arg]` and routes as follows. `ResumeTransitionSxd` at `0xC830` mirrors that dispatch:

| Type tag `[arg]` | Pause call path (RVAs) | Resume call path (RVAs) | Statical destination identity |
| --- | --- | --- | --- |
| **1** | `C7E0 → 6FF0 → C320` | `C830 → 7010 → C350` | Native sample/voice path, with nested wrapper/handle handling |
| **2** | `C7E0 → 2900 → [dynamic fnptr 2692C]` | `C830 → 2930 → [dynamic fnptr 26930]` | `_SND_fn_vPauseTransitionMPEG@4` / `_SND_fn_vResumeTransitionMPEG@4` |
| **4** | `C7E0 → 3C60 → [dynamic fnptr 26A9C]` | `C830 → 3C90 → [dynamic fnptr 26AA0]` | `_SND_fn_vPauseTransitionSxdADPCM@4` / `_SND_fn_vResumeTransitionSxdADPCM@4` |
| Other | Error/report branch; returns | Error/report branch; returns | Unsupported/unknown |

The other modules' export **names** are embedded in this original DLL and resolved by the helper at RVA `0x3FA0`. Pointer setup sites around RVAs `0x4F64–0x4FA7` (MPEG) and `0x5D43–0x5D86` (ADPCM) install an internal fallback if the target is absent. These dynamic function pointers were **not** resolved in the submitted disk image: we have not proved which secondary module/version is actually loaded at runtime or whether it uses reference counting.

For `StopTransitionSxd`, type 1 routes to `0x6FC0`, type 2 to `0x28D0` (dynamic `_SND_fn_vStopTransitionMPEG@4`), and type 4 to `0x3C30` (dynamic `_SND_fn_vStopTransitionSxdADPCM@4`). This function's correspondence with each Pause/Resume branch is exact at the exported dispatcher level.

## Lower sample/voice path: an explicit flag, not a proven reference count

For type 1, the path through `0x6FF0 → 0xC320` eventually reaches `0xD140 → 0xAB20` for a non-nested voice object. The mirrored Resume goes through `0x7010 → 0xC350 → 0xD150 → 0xAB40`. A nested intermediary (tag 2 inside the type-1 payload) can route through `0xCDA0/0xCDB0` before reaching those routines.

At `0xAB20`, the leaf writes `[voice+8] = 1` and dispatches its underlying pause helper. At `0xAB40`, it writes `[voice+8] = 0` and dispatches its underlying resume helper. Subsequent helpers at `0x9EE0` and `0x9F10` set and clear a per-voice flag in a 112-byte-stride table, then call another loaded function pointer via `0x31A0` / `0x31D0`. These are **observable boolean writes at this specific layer**, rather than increments/decrements of a nesting counter. This **does not establish** that the full WinMM mixer/device or MPEG/ADPCM branch lacks independent pause-depth or asynchronous buffering semantics.

Type 1 also calls lock/unlock helpers at `0x6860` and `0x6880`; this makes bypasses of the native dispatcher especially risky. No code was executed from the DLL during this investigation.

## Immediate implications for the ESC feature

1. The owner's Windows logs show two identical source-specific `PauseTransitionSxd` object pointers per ESC open and two matching `ResumeTransitionSxd` object pointers per exit. **They do not log the type at `[arg]`**. We therefore cannot yet say whether actual ESC music follows native sample, MPEG, or ADPCM branch. The `[DLL_Adpcm] Default=APMPW` setting is not a substitute for this observation.
2. Do **not** infer that two identical driver calls create a refcount of 2, or that one callback can safely be dropped. Some type-1 leaf state appears boolean, but branches 2 and 4 are dynamically delegated; callback pairing is part of the original engine contract and remains preserved.
3. A logical Theme/SXD resource update `9:7→9:8` during an ESC menu is not proof that this transition-object pointer changed or was freed. Nonetheless, until type and actual backend lifecycle are independently verified, the existing `phase=6` conservative native passthrough remains correct.
4. **Next minimal proof — implemented as passive instrumentation:** `RUNTIME-ESC-PREDRIVER-GATE transition_type_post_pause` now reads `[arg]` **after the unchanged original script Pause wrapper returns**, not in any audio-driver callback. It is restricted to the first 16 source-verified, same-object two-Pause episodes; `-1` means unreadable/not sampled, `0` means a read but unexpected tag, and `1/2/4` are the static tag values of this exact file. It reports no raw pointers and does not influence any audio decision. **This post-wrapper read is weaker than a verified pre-call snapshot** if the game changes/frees an object during Pause; `-1` must remain unknown and even a valid tag cannot authorize bypass. If type 2 or 4, next analyze the exact dynamically resolved MPEG/ADPCM callback modules before asserting pause semantics. Comparing a matching Resume tag at the original callback boundary is not implemented yet.
5. The earlier `pre_driver_pause::Policy` remains a disconnected research model. Do not enable it from static dispatch analysis alone; its unrecoverable deferred-Pause debt after a stream retirement is still unsolved.

## Reproduction notes

Static-only tools: `sha256sum`, `file`, `objdump -p`, and `objdump -d -Mintel --start-address=... --stop-address=...`. Disassembly image base `0x10000000`; to examine an RVA add the image base. Exact original binary SHA-256 above is a **per-version guard**. Do not reuse these RVAs for any other DLL without a byte-identical hash check.

No original file was modified, repackaged, or committed. This document contains only derived technical analysis.

## Source-confirmed ESC tag=4 and exact ADPCM indirection table (owner, 14 November 21:17)

Owner Windows runtime file `Rayman2Fix(20261114-211741).log`
reported **11/11** qualified ESC Pause wrappers with
`transition_type_post_pause=4`, including the first **six**
Theme controller reference mismatch cases and five later matched
menu openings. Ten matching script Resume wrappers completed;
all ten had original depth `2→1` and exact two native slot-3
callbacks, six fully forwarded and four fully suppressed after
the existing successful synthetic compensation. There was one
`phase=6` SXD resource quarantine `9:8→9:9` in the last,
unclosed menu. None of these figures is an audible result.

The type tag was read **after** the game script's original Pause
wrapper completed: the evidence consistently points toward
ADPCM dispatch (type 4), but is not an atomic proof of the
pre-Pause type or an independently validated physical voice.

Additional original `wavmwbvr.dll` instruction audit confirms
that the type-4 delegated operations read a contiguous
12-byte function-pointer table:

| Native dispatcher | Type-4 wrapper | Actual pointer slot RVA | Stub RVA if missing |
| --- | --- | --- | --- |
| `StopTransitionSxd 0xC790` | `0x3C30` | `0x26A98` | `0x3C50` |
| `PauseTransitionSxd 0xC7E0` | `0x3C60` | `0x26A9C` | `0x3C80` |
| `ResumeTransitionSxd 0xC830` | `0x3C90` | `0x26AA0` | `0x3CB0` |

The table values are populated with dynamically resolved
`_SND_fn_vStopTransitionSxdADPCM@4`,
`_SND_fn_vPauseTransitionSxdADPCM@4`, and
`_SND_fn_vResumeTransitionSxdADPCM@4` pointers.
A fallback stub is installed if a function is absent.
Neither `[DLL_Adpcm] Default=APMPW` nor the name
`WAVMWbvr.dll` establishes the identity of the actual
secondary module containing those function pointers.

The standard Windows x86 runtime now emits exactly one
`RUNTIME-AUDIO-ADPCM-DELEGATE` record per Stop/Pause/Resume
target, after the first source-confirmed `type=4` opening
and **after** the normal audio compensation action. It
checks loaded module owner `WAVMWbvr.dll`, original
PauseTransition function offset `0xC7E0`, original PE
timestamp `0x38076FB4`, image size `0x87000`, and the
verified `8B 4C 24 04 8B 01 48` entry prefix before
reading the three original indirection slots. These are
conservative **version fingerprints** rather than a full
SHA-256 image check.

Each target logs `status=resolved` with **module basename
and callback RVA**, `status=fallback` for its known stub,
or an explicit unknown/unresolved status. No raw address or
game path appears in the log, no secondary DLL is loaded,
and no function pointer is called. This is a diagnostic,
not permission to suppress physical native Pauses. If
runtime version checks fail, **no slot offset is read**.

The next high-confidence input is a normal owner log from
that build. The actual resolved ADPCM DLL can then be
identified and, if necessary, studied through its own
hash-locked original bytes. Keep PR #10 draft.

## Exact downstream ADPCM DLL identified in owner's 21:31 session (14 November)

A new Windows runtime log, `Rayman2Fix(20261114-213131).log`,
shows that the new original-DLL indirection-table observer
resolves **all three** delegated type-4 functions successfully:

| Native ADPCM delegate | Resolved original module | Callback RVA | Resolution |
| --- | --- | --- | --- |
| StopTransitionSxdADPCM | `APMMXbvr.dll` | `0x54C0` | loaded, executable address |
| PauseTransitionSxdADPCM | `APMMXbvr.dll` | `0x54D0` | loaded, executable address |
| ResumeTransitionSxdADPCM | `APMMXbvr.dll` | `0x54E0` | loaded, executable address |

The source of these three facts is the **actual original
WAVMWbvr function-pointer table at runtime**, not the
`[DLL_Adpcm] Default=APMPW` configuration string. This
establishes the full original module chain as
`rayman2.exe → WAVMWbvr.dll type 4 → APMMXbvr.dll`
for observed source-specific ESC transition objects. It
does **not** establish the per-stream implementation
inside APMMXbvr or how pause state is rolled back.

The session contains **10** source-verified script Pause
wrappers, all ten with post-native type `4`, and **8**
script Resume wrappers, each with two same-object native
slot-3 calls, no mixed pair: **six** early strict Theme
reference mismatches forwarded both, **two** stable
compensated menus suppressed both. The first observed menu
at 115.953 s compensated while inner SXD was `239:17`,
then progressed to `239:18` / `239:19` while menu=1
and entered the normal `phase=6` quarantine. This
first session has **no script Resume trace** before an
intro/level transition; its original native pause depth
is subsequently observed to start at **2** rather than 1.
The first following source Resume at 156.141 s correctly
reports `paired=0 overlap=1` because the earlier
recorded opening was not closed by a matched Resume.
**Its zero Play/Stop deltas are invalid**, not evidence
of an event-free session. Seven subsequent completed
lifetime reports are paired successfully; a last
compensated opening at 174.922 s also quarantines
without a matching close before the log ends.
The absence of a matching close in a log is not by
itself proof of a persistent stuck pause or a crash;
no owner listening outcome was supplied.

**Precise next static target:** the *owner's* actual
`Rayman2Fix/dll/APMMXbvr.dll` (case-insensitive Windows
name). Obtain that DLL from the same CD installation
without modification, hash it and verify the three
exported addresses/RVAs `0x54C0/0x54D0/0x54E0`.
Then statically trace the original type-4
Pause/Resume/Stop leaf paths, their per-channel state
and any callbacks, stop/replacement, refcount or queue
management. Do not guess that these three adjacent
entries are no-op stubs, and do not assume that the
sample/voice boolean state in WAVMWbvr applies to
the delegated ADPCM implementation.

Public project `raytools/ACP_Ray2` documents
multipart CPA Themes; their logical transition keys
do not establish a physical voice lifetime. No
additional gameplay test is required merely to
identify the module, and no original DLL is
modified or committed.

## Offline ADPCM DLL PE32 audit tool — awaiting owner's exact binary

The exact `Rayman2Fix/dll/APMMXbvr.dll` file has **not yet been
provided**. External sources such as
https://github.com/Synthesis/ray2get and public Steam file
manifests confirm that an APMmxBVR decoder is distributed with
some Rayman 2 PC editions, but **neither source verifies the
owner's DLL bytes or the implementation of native Pause/Resume**.

Research support added to PR #10:

- `tools/audit_apmmxbvr.py` is a standard-library-only,
  **read-only**, Windows/Linux-compatible PE32 x86 inspector.
  It never loads, runs, replaces, copies, or writes the input
  DLL. It prints SHA-256, size, PE timestamp, image base,
  and the first 24 on-disk bytes of the following named
  exports (if present), with strict RVA bounds.
- Required reference from the actual owner's native log:
  `_SND_fn_vStopTransitionSxdADPCM@4 = 0x54C0`,
  `_SND_fn_vPauseTransitionSxdADPCM@4 = 0x54D0`,
  `_SND_fn_vResumeTransitionSxdADPCM@4 = 0x54E0`.
  A different, absent, unreadable, or forwarded export
  explicitly fails validation rather than silently adopting
  addresses from another edition. This is *structural*
  export/RVA validation, not a proof of equivalent semantics.
- The optional `--expected-sha256` argument is a full-file
  hash guard for repeating analyses **after** a known owner's
  original file hash has been recorded. There is no guessed
  reference hash built into the script.
- Seven unit tests in `tests/test_audit_apmmxbvr.py` create
  synthetic PE32 bytes in temporary storage, validate
  correct/incorrect exports and bad headers, and verify
  JSON output and nonzero exit codes. No copyrighted
  game binary is committed or loaded by CI.

Run from the repository root:

```sh
python tools/audit_apmmxbvr.py "Rayman2Fix/dll/APMMXbvr.dll"
python tools/audit_apmmxbvr.py "Rayman2Fix/dll/APMMXbvr.dll" --json
```

Exit status: `0` means all **three exported RVAs** match the
owner's runtime log, `1` means the image is parsable but
does not establish that identity, and `2` means invalid
PE, I/O failure, or explicit SHA-256 mismatch. The output
does not establish pause counters, voice ownership, backend
queue state, stream-retirement behavior, or audio correctness.

Once the owner supplies this **exact** original DLL, hash it,
verify exported RVAs, then statically disassemble the three
confirmed functions and their callees. If the file does not
match, investigate that discrepancy rather than overriding
validation or importing a random online DLL. No runtime
patch or new audio hook is part of this tool.

## Owner supplied the exact downstream `APMMXbvr.dll`: static ADPCM Pause/Resume mixer analysis

The **original** `apmmxbvr.dll` attached on 14 November 2026 has
been inspected directly in read-only mode using the PE export table and
x86 disassembly, **without running it, changing it, or committing its
bytes**. It is the exact secondary module whose three native
callback RVAs the owner already observed with
`RUNTIME-AUDIO-ADPCM-DELEGATE`.

### Original binary identity

| Property | Observed value |
| --- | --- |
| File | `apmmxbvr.dll` |
| Size | **95,744 bytes** |
| SHA-256 | `ab2d7cc613fea6dd8b039e76f00a6f5d15c7ed37f2b93e0be7a86dbc6a0ee4cf` |
| Format | PE32, 32-bit Intel x86, five sections |
| PE timestamp | `0x38076F98`, **1999-10-15 18:16:56 UTC** |
| Image base | `0x10000000` |
| Mapped image size | `0x000EE000` |
| `.text` on disk | RVA `0x1000`, raw size `0xD800` |
| Export table | 59 named functions, including all three exact delegates |

All three exports from the actual file **exactly match the owner's
resolved addresses**, and each is an x86 stdcall one-argument wrapper:

| Original ADPCM export | RVA | Calls within this DLL |
| --- | --- | --- |
| `_SND_fn_vStopTransitionSxdADPCM@4` | **`0x54C0`** | `0x3340 → 0x1700` |
| `_SND_fn_vPauseTransitionSxdADPCM@4` | **`0x54D0`** | `0x3380 → 0x1ED0` |
| `_SND_fn_vResumeTransitionSxdADPCM@4` | **`0x54E0`** | `0x33C0 → 0x1F10` |

The internal wrappers at `0x3340`, `0x3380` and `0x33C0`
first check the same original initialization/global-enable value at
VA `0x100E82A8`. If enabled, they acquire a shared critical
section via `0x1020` with the original lock object at
VA `0x100163D0`, call the respective leaf function, release via
`0x1030`, and return. This is evidence of a common lock,
**not proof of a thread-safe interception outside that lock**.

### The actual Pause/Resume implementation

The two internal leaves inspect an opaque ADPCM handle and recover a
record pointer from `[handle + 0]`. They both call original
validator `0x2480` before writing any voice state. Validation
checks that the record lies in an original ADPCM record pool beginning
at VA `0x100E67D8`, that its offset is aligned to the original
**`0xE4`-byte record stride**, and that the supplied handle
matches `[record+0xD4]`. A null or invalid handle returns an
error instead of writing the pause flag.

For a *valid* record:

- `0x1ED0 + 0x20` (VA `0x10001EF0`) executes
  `C7 46 1C 01 00 00 00`, meaning **`[record+0x1C] = 1`**
  (pause).
- `0x1F10 + 0x20` (VA `0x10001F30`) executes
  `C7 46 1C 00 00 00 00`, meaning **`[record+0x1C] = 0`**
  (resume).

The functions **assign a Boolean-style field**, not `++`/`--`
on a reference count at this leaf. Calling Pause twice on the same
valid ADPCM record simply assigns 1 twice at this layer; calling
Resume twice assigns 0 twice. This does **not** prove that the game
scripts' global pause depth, the higher-level CPA Theme subsystem,
or another device/mixer callback has no independent state.

### Crucial: exactly how the ADPCM mixer makes paused voices silent

The original PCM mixing function at RVA **`0x18D0`** clears
its output accumulation buffer before scanning live ADPCM records.
The original record-loop cursor starts at VA `0x100E6804`,
which is **record-pool base + `0x2C`**, and advances by
`0xE4` per record.

At RVA `0x196C`:

```asm
mov eax, [esi-0x10]           ; record+0x1C, pause flag
test eax, eax
jne 0x10001B41               ; skip this voice's decoding/mixing
mov eax, [esi+0xB0]          ; record+0xDC, separate state
test eax, eax
jne 0x10001B41               ; also skip if other gate is set
```

The relation is exact: `esi=record+0x2C`, therefore
`esi-0x10=record+0x1C`. When Pause has stored 1,
the per-voice mixing/decoding path is skipped, leaving that
voice out of the newly produced PCM mix; Resume clears the
flag and allows it to participate again if still valid and
otherwise eligible. This proves the observed *per-record
mixer gate*, **not a Windows `waveOutPause` device call**.
The original module imports `KERNEL32.dll`, not `WINMM.dll`;
its actual output is delegated elsewhere.

### Stop irreversibly retires the current ADPCM object

The ADPCM Stop leaf `0x1700` validates the same handle and
clears a nested state field at `[record+0xC4]+0x60`
when that nested pointer is present. It then calls the
existing original `0x15D0` retirement function, which
validates the handle, releases associated objects, **sets
`[handle+0]=0` at RVA `0x1655`**, adjusts record
references, and removes/compacts the `0xE4`-stride record
array while decrementing the live record count. In particular,
the former handle does **not** provide a guaranteed way
to resume a physically retired sound after Stop.

Thus a Pause-debt protocol must not assume that delayed
Resume calls can always be replayed against the same
record, especially across game-level or logical Theme/SXD
part transitions. The original validator may reject the
old pointer after retirement, and its record storage may
have moved; **never store this raw record pointer in a
fix-level long-lived object**.

### Engineering conclusions for the ESC patch

1. We now have direct, original-binary evidence why the
   default native script Pause makes type-4 ADPCM music
   silent: a per-record Boolean pause flag causes the
   1999 mixer to **skip decoding/mixing that voice**.
   The previous MIDI/device/global-pause assumptions were
   too broad for the owner's confirmed active audio path.
2. The first six ESC openings have `type=4` but early
   Theme-player reference mismatch (alternate `9:12`
   vs active outer `9:5`, inner `9:6`). The missing
   compensation is due to the higher-level strong owner
   check, **not an ADPCM refcount at this leaf**.
3. A future pre-driver test could *in principle* avoid
   setting the ADPCM pause flag for a precisely
   source-verified transition owner. But selecting **the
   correct original ADPCM record across two identical
   callbacks, native script/global pause depth and a
   possible Stop or resource move** remains unresolved.
   It could also leave unintended nonmusic sounds
   playing during the menu. Do **not** enable physical
   Pause suppression based on the Boolean leaf alone.
4. A second safe research line is to compare, in a
   pure deterministic model, two original Pause/Resume
   writes on one valid record with an intervening
   Stop/reallocation, and prove that retirement forbids
   deferred writes/replay. The actual sound-driver DLL is
   unchanged and existing `phase=6` native passthrough
   remains mandatory.

The read-only inspector `tools/audit_apmmxbvr.py` verifies the
original exports and byte-address provenance separately; this
section records the manually verified static control flow
from this **exact SHA-256**. No runtime test of changed
audio behavior has been attempted.

## Passive source-paired ADPCM voice audit (14 November, next Windows run)

The two original owner-supplied DLLs now provide enough verified
structure for a *read-only snapshot* of the specific ADPCM
record referenced by a source-identified ESC Pause
**after the game's unmodified original wrapper and the existing native audio compensation are finished**.

The standard opt-in `MusicInPauseMenu` runtime adds
`RUNTIME-ESC-ADPCM-VOICE`, with four deliberately narrow
fields:

- `sampled=1` means **all** original version guards,
  pointer-chain checks and record-live-count checks passed
  in one bounded post-dispatch snapshot. `sampled=0`
  means **unknown**, not an absent sound or an unpaused
  sound.
- `voice_index` is the *temporary ordinal* of the
  verified `0xE4`-stride active record in the actual
  APMMX module, not a song ID, a stable music identity
  or a WinMM voice handle. Its value must not be cached
  or compared across a Stop/compaction.
- `paused=0|1|2` is the record's actual `+0x1C` state
  as observed after the current menu's original and compensated
  Pause/Resume decisions: unpaused,
  paused or unexpected other value. `-1` means unknown.
  It is measured after any existing synthetic Resume compensation,
  rather than delaying the audible recovery. The
  separately logged `transition_type_post_pause` still comes
  from immediately after the original wrapper.
- `other_gate=0|1` is the record's separate mixer
  exclusion field at `+0xDC`: zero or nonzero.
  A zero `paused` flag alone does not establish audible
  output when the other gate is set.

The snapshot is admitted **only** after precisely two
source-identified original slot-2 callbacks with an
identical transition-object argument and a post-wrapper
type `4`; the pre-existing first-16 ESC-source budget
applies. These reads happen in the menu tick AFTER
`proto_dispatch(action)` and `proto_deferred_safety()`,
never inside the original script wrapper or any
audio callback. This ordering is critical: the observer
must not extend the brief opening silence.

Strict positive checks:

1. Actual saved original Pause callback address is
   `WAVMWbvr.dll + 0xC7E0`.
2. The two loaded DLLs' PE headers are x86 with the
   owner-observed respective 1999 timestamps and image
   sizes, and both verified exported function entry
   byte prefixes match the attached originals. These
   are *runtime version fingerprints*, **not** a
   dynamic full-file SHA-256 proof.
3. The actual original WAVMW delegated function pointer
   at RVA `0x26A9C` equals
   `APMMXbvr.dll + 0x54D0`.
4. `[transition+0]=4`; `[transition+4]` supplies the
   ADPCM handle; `[handle]` references an original
   record with pool RVA `0xE67D8`, record stride
   `0xE4`, live record count RVA `0x24EC4`,
   active count `1..30`, and
   `[record+0xD4] == handle`.
5. Reads of the ADPCM handle target and live record count
   before and after the remaining observations must be
   identical. If they disagree, return `sampled=0`
   and never attribute the pause state to a stable record.

There is **no long-lived cache of the transition pointer,
ADPCM handle or record address**, no write to the original
game, no direct call into the ADPCM DLL, no added native
Pause/Resume/Stop and no changed compensation decision.
Source/Theme eligibility is unchanged. This snapshot
does not acquire the original ADPCM driver's critical
section, so even repeatable observations are still
**diagnostic only**, not an atomic guarantee of
stability across an asynchronous Stop or pool compaction.

This specifically tests the unanswered early-menu case:
when `RUNTIME-ESC-PREDRIVER-GATE native_ref_class=3`
(`9:12` mismatched with the active `9:5` Theme), does
the separately source-verified original transition
still have a valid live ADPCM record with `paused=1`
**after the normal menu decision** (which rejected
synthetic compensation)? If yes, the original Pause
left a particular live record in the mixer-muted state
despite the higher-level Theme pointer mismatch.
When the owner is verified and compensation is accepted,
the later post-dispatch snapshot may instead show
`paused=0`; this is an outcome snapshot and not a
before/after atomic trace.
It does **not**, by itself, prove that suppressing the
pause is safe (other sounds, Stop/compaction, focus,
script depth and Resume debt remain independent).

**Safety no-go:** the observed `voice_index` can be reused
after Stop/compaction. Even a stable ordinal in consecutive
menu logs is not a music-generation token. The
previous no-pre-driver-suppression policy continues until
a reliable exact owner/retirement contract is established.
