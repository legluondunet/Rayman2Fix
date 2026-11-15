# Original APMMXbvr.dll: handle lifetime, slot rebasing and ABA reuse

**Scope:** research-only, no change to Rayman2Fix runtime audio behavior,
no patch to the original game or original DLLs. Evidence derives from the
owner-supplied original 1999 `apmmxbvr.dll`:

- 95,744 bytes, PE32 x86, 15 October 1999;
- SHA-256: `ab2d7cc613fea6dd8b039e76f00a6f5d15c7ed37f2b93e0be7a86dbc6a0ee4cf`;
- original image base `0x10000000`.

All addresses below are **original virtual addresses** in this *exact*
binary. Use RVA = VA minus `0x10000000` for static analysis, and
**do not** reuse these constants on a different DLL without matching
the full-file digest. Verified with read-only `objdump -d -M intel`
plus on-disk fingerprint comparisons.

## 1. The driver has TWO distinct pools

| Data | Original address | Evidence |
| --- | --- | --- |
| Handle registry | `0x10024E48..0x10024EC0` (exclusive end) | 30 x 4-byte entries |
| Live record count | `0x10024EC4` | max `0x1E` (=30) |
| Record array | `0x100E67D8` | stride `0xE4` (=228 bytes) |
| Record → handle backlink | `[record+0xD4]` | set at `0x1000200E` |
| Handle → record pointer | `[handle]` | set at `0x100014C4` |
| Record active/recyclable indicator | `[record+0x28]` | active set 1 at `0x10002007`; can become 0 at `0x100017CA` |
| Record mixer Pause flag | `[record+0x1C]` | Pause=1, Resume=0 |

A **handle** is the address of an entry in a persistent 30-slot
registry. A **record** is a movable `0xE4`-byte object in
the compactable live record array. The record's `+0xD4`
points back to its handle slot.

These identities are not interchangeable: the same voice can move to
another record array index without changing its handle's address.
Conversely, a recycled handle address can denote **different audio
resources at different times**.

## 2. Original Stop explicitly rebases survivors

The original `StopTransitionSxdADPCM` entry
`0x100054C0` calls `0x10003340` → `0x10001700` →
retirement at `0x100015D0`.

On a valid handle, original native code does:

```asm
0x10001655  mov [esi],0                  ; the stopped handle is invalidated
0x1000165B  mov eax,0x10024E48          ; start 30-slot handle table
0x10001660  mov ecx,[eax]               ; handle points to some record
0x10001662  cmp ecx,edi                 ; edi = removed record
0x10001664  jb  0x1000166E             ; records below it stay put
0x10001666  add ecx,0xFFFFFF1C          ; subtract 0xE4
0x1000166C  mov [eax],ecx               ; REBASE surviving handle
0x1000166E  add eax,4
0x10001671  cmp eax,0x10024EC0
0x10001676  jb  0x10001660
...
0x100016B0  sub eax,edi
0x100016B2  sub eax,0xE4
0x100016B7  push eax                   ; bytes to move
0x100016B8  lea eax,[edi+0xE4]         ; source after retired record
0x100016BE  push eax
0x100016BF  push edi                   ; destination removed record
0x100016C0  call 0x10006060            ; original overlap-aware memory mover
```

When deleting a record that is *not* the last one, the driver
moves subsequent records left by one element. **The old handle for
a surviving record is adjusted to point to its new record
address.** The same pool ordinal can therefore identify a completely
different surviving voice after compaction. The native
`record+0xD4` backlink moves with the record contents.

**Important nuance:** simply observing an unchanged handle before
and after an event does NOT mean the record stayed at the same
address, and observing a changed record address does NOT mean the
song changed. This direction of compaction is accounted for
by the original 30-entry registry update.

## 3. First-free handle slots are REUSED after Stop

At `0x100014A1–0x100014BA`, the normal new-record
allocation path scans the registry for the first entry whose
stored record pointer is zero, then uses **the address of
that slot** as its new handle. `0x100014C4` stores
the new record address there.

After a Stop has set `[old_handle]=0`, a new sound
can receive that **same handle address** (including
its former index, if the record pool had become empty).

It is possible for a saved observer tuple
`(handle_address, record_ordinal, live_record_count, valid_backlink)`
to look identical **before A / after B**, despite the old
sound having been destroyed and a new sound allocated.

This is an **ABA identity hazard**: pointer equality and valid
record backlink prove *current structural validity*, not that
the current record is the exact original music generation.

## 4. More serious: in-place reuse WITHOUT any original Stop call

Original allocation `0x100013A0` has a second path:

```asm
0x10001460  mov edx,[esi+0x28]        ; inspect existing record state
0x10001463  test edx,edx
0x10001465  jne 0x1000148F           ; still active → not reusable here
...                                    ; additional +0xD8 eligibility tests
0x1000148F  test ecx,ecx             ; eligible inactive record?
0x10001491  jne 0x100014BE
0x100014BE  mov ebp,[esi+0xD4]       ; reuse ORIGINAL handle slot
0x100014C4  mov [ebp],esi            ; same handle → same record index
0x100014CF  mov ecx,0x39
0x100014D4  xor eax,eax
0x100014D6  mov edi,esi
0x100014D8  rep stosd                 ; REINITIALIZE record (0xE4 bytes)
...
0x100014F9  call 0x10001FC0           ; initialize a DIFFERENT resource
...
0x100015A3  test eax,eax             ; skip count increment if reused
0x100015A5  jne 0x100015AD
```

The original playback/progression code can mark a record inactive
(`[record+0x28]=0` at `0x100017CA`). **Subject to additional
original eligibility conditions at `+0xD8` and mode parameters**,
a later allocation can reuse that same slot and handle.

The initializing function at `0x10001FC0` sets
`[record+0x28]=1`, `[record+0x1C]=0`,
`[record+0xD4]=handle` and
`[record+0xD0]=resource_descriptor`.

This path **does not call Stop**, does **not invalidate
the handle first**, and **does not increase the live count**.
Yet it can start a different audio resource with the same
handle, record ordinal, backlink and count.

Therefore even a hypothetical perfect native Stop observer
would be **insufficient** to certify music generation.
It must also see successful original creation/reinitialization
events, including this in-place allocator route.

## 5. Consequence for early ESC music compensation

The current read-only `RUNTIME-ESC-ADPCM-VOICE` snapshot validates
a **single instant** of `[handle] ↔ [record+0xD4]`,
the active record count, owner DLL version and pause flags.
The source-qualified two original Pause callbacks and the pending
Theme descriptor `9:5` are independently proven.

**None of these establish an immutable generation** across
the original driver's reuse routes. A newly recycled record can
pass all the same structural checks. This explains why the
proposed early-ESC "resume now, suppress native exit resumes
later" cannot yet be safely authorized across a native Theme
promotion or an inner SXD handoff.

The separate preliminary `pre_driver_pause::Policy` has
another unresolved safety debt: withholding an original
Pause that can no longer be safely replayed after retirement.
It stays disconnected from the shipping runtime.

A safe bridge would need, at minimum:

1. Complete original ADPCM **record creation/reinitialization
   coverage**, not just Stop, and a monotonic identity/generation
   token maintained *outside* the original handle slot.
2. Observation of every original retirement, transition and
   focus/unknown source capable of invalidating compensation.
3. Correct original paired ESC script Resume handling under
   reused handles, nested native pause depths and scene changes.
4. Fail-closed operation on missing, incomplete or ambiguous
   lifecycle events, without replaying a deferred driver
   command on a recycled handle.

Until those requirements are established, retain
strict `ref_class=1` ownership and `phase=6`
native passthrough. **No new native Pause/Resume/Stop
interception has been enabled**.

### Reproduction (read-only)

```sh
python3 tools/audit_adpcm_handle_lifetime.py "Rayman2Fix/dll/APMMXbvr.dll"
python3 tools/audit_adpcm_handle_lifetime.py "Rayman2Fix/dll/APMMXbvr.dll" --json
python3 -m unittest discover -s tests -p 'test_adpcm_handle_lifetime.py'
```

The SHA-pinned tool checks **22 original instruction fingerprints**,
demonstrates the two ABA pathways using explicitly **synthetic**
track identities, and never loads the original DLL as executable code.
These are proofs of *possible* aliases from the original code, **not
claims that a given alias occurred in the user's latest runtime log**.

## 6. Other internal retirement and reinitialization paths

A further exact-original x86 direct-call audit shows that the
retirement function `0x100015D0` can be reached from **multiple
independent code paths**, not just the exported
`StopTransitionSxdADPCM`:

| Direct call address | Called address | Observed context |
| --- | --- | --- |
| `0x1000172C` | `0x100015D0` | StopTransition's validated-stop leaf |
| `0x10001C8D` | `0x100015D0` | transition/fade-related path when the record becomes inactive |
| `0x10001F68` | `0x100015D0` | inactive-record cleanup when `[record+0x28]=0` |
| `0x10002950` | `0x100015D0` | another original Stop path, under the original driver critical section |

There are also **two additional calls** to the common
original `0x10001FC0` record initialization helper:

- `0x100021F3 → 0x10001FC0`
- `0x100022A3 → 0x10001FC0`

These are associated with nested/secondary record handling,
including fade/transition operations. They do **not**
necessarily mean a whole top-level voice was newly created,
and cannot be treated as such without examining the context.
However, this confirms that **monitoring only the exported
PlayTransition and StopTransition functions would not be
an exhaustive lifecycle observer**.

All six additional call instructions were verified
byte-for-byte on the owner's exact SHA-pinned DLL.
The offline tool now verifies **22** original instruction
sites (rather than 16); the synthetic test suite checks
the additional fingerprints. Only read-only source
inspection was performed.

**Strengthened NO-GO:** a future per-voice generation
observer must cover internal retirement and record
initialization paths or deliberately refuse to act
when coverage is incomplete. It must never assume that
a matching source-scoped ESC pair, Theme target and
valid ADPCM backlink constitute an unchanging generation.


## 7. Independent direct-call scan and auxiliary record initializers (15 November)

A second SHA-pinned, read-only check of the owner's exact original
1999 APMMXbvr.dll traversed every raw byte of its .text section
(RVA 0x1000, 0xD800 on-disk bytes). For each candidate E8 disp32
opcode, it computed the signed relative target. The scan found exactly
these raw *direct-call candidates* to two original helpers:

| Target RVA | Raw E8/rel32 caller RVAs | Role |
| --- | --- | --- |
| 0x15D0 | 0x172C, 0x1C8D, 0x1F68, 0x2950 | Record retirement |
| 0x1FC0 | 0x14F9, 0x21F3, 0x22A3 | Record initialization |

The seven candidate locations are actual calls in an independent
`objdump -d -M intel` disassembly. No other raw E8/rel32 candidate
inside this exact original .text section targets those two helpers.
This is **not** complete interception coverage: indirect calls,
alternate branches, function-pointer dispatch, and concurrency remain
outside the byte scan.

Further exact original x86 inspection distinguishes auxiliary
initialization from creation of a physical top-level music voice:

- At 0x2180..0x21F3, a parent handle is validated at 0x2480;
  the code allocates or reuses a subsidiary block under
  [primary_record+0xC4], clears its 0xE4 bytes, then calls 0x1FC0
  with the **existing parent's handle**.
- At 0x2210..0x22A3, the validated parent receives
  [primary_record+0x2C]=1; its subsidiary +0xC4 points to the
  fixed object 0x100E65A0, that object is reset and global flag
  0x100E6678=1 is set before calling 0x1FC0 with the **same
  primary handle**.

Consequently, **incrementing a generation counter on every 0x1FC0
call would be incorrect**: these nested helpers can reset auxiliary
records without creating a new top-level handle. Equally, observing
only exported Stop callbacks misses internal retirement and primary
in-place reuse. A future observer needs a verified distinction
between primary and auxiliary records and all relevant mutation
routes under the original driver's lock. That is not proven yet.

Reproducibility (the original proprietary DLL remains local):

```sh
python3 tools/audit_adpcm_rel32_calls.py Rayman2Fix/dll/APMMXbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_rel32_calls.py'
```

The auditor checks the exact owner SHA-256, PE layout and expected
call-site candidates; the six tests use only synthetic data. An E8
byte scan can also find undecoded byte patterns, so it must not be
described as exhaustive x86 control-flow proof.

**Status: NO-GO on activating a new first-ESC bypass.** Native
Theme/SXD ownership checks and passthrough stay unchanged. No
proprietary DLL bytes or game executable are committed.

## 8. Primary allocator commit is distinct from a record initializer call (15 November)

A further SHA-256-pinned static audit on the owner's exact original
`APMMXbvr.dll` inspected **24 opcode anchors and six linked relative
control-flow edges**. The results refine, but do not lift, the NO-GO
in section 7:

| Original RVA | Verified effect | Implication for a hypothetical epoch observer |
| --- | --- | --- |
| `0x13A0` | Primary allocator entry, consults live count | Do not assume arbitrary nested initialization is a new voice |
| `0x14A1` / `0x14BE` | First-free handle or inactive-record reuse | The address of a handle alone cannot identify an epoch |
| `0x14C4` / `0x14CF` | Binds handle, clears the 0xE4-byte target record | Record memory is mutated *before* success is known |
| `0x14F9` | Calls shared record initializer `0x1FC0` | The initializer itself is **not** a per-voice creation event |
| `0x1557` / `0x1569` / `0x1572` / `0x1585` | Can allocate/attach a child, switch the record argument and **loop back to 0x14CF** | Even the `0x14F9` call site can execute twice during one allocator invocation |
| `0x158A` | Error path clears the bound handle | Failed destructive reinitialization must invalidate any previously assumed epoch |
| `0x1599` / `0x15AD` | Writes output handle and returns zero on success | Better *logical* primary-publication boundary than an initializer call, but not an atomic physical-generation proof |
| `0x159F` / `0x15A7` | Increments live count only if a primary record was not reused | Successful in-place reuse can change a voice without changing count |
| `0x21F3` / `0x22A3` | Two additional initializer callers use a validated primary's handle for auxiliary storage | Do not miscount those as new primary voice epochs |

The original `0x1585` jump is a **backward jump** to `0x14CF`.
The original `0x1503` failure branch targets `0x158A`. Together
with the child pointer switch and the final handle-output assignment,
this rules out a naïve `generation++` on any common initializer entry,
including calls originating at `0x14F9`. It does **not** show a safe
thread-atomic event stream or prove the game will enter any particular
path during an early ESC.

A new *synthetic-only* `SyntheticEpochModel` records a conceptual
generation at a successful primary publication, not on child setup.
It invalidates on retirement, destructive init failure, or unknown
lifecycle events, and makes same-address primary reuse replace the
conceptual generation. No native record has been shown to store this
counter, and the model cannot authorize a real Pause/Resume/Stop call.

Reproduce on the owner's original DLL **locally**, without distributing
it or executing it:

```sh
python3 tools/audit_adpcm_primary_epoch.py Rayman2Fix/dll/APMMXbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_primary_epoch.py'
```

**Current decision:** PR #10 remains draft. Keep the existing strict
`ref_class=1` and `phase=6` native pass-through protection. Before
any audio interception, native code must establish complete primary
creation, retirement, failed/reused creation and synchronization
coverage **including indirect paths**, plus exact Theme/SXD ownership
throughout the ESC pair. The tool is read-only and makes no
runtime-policy change.

## 9. Original TestIsPlaying can retire without an exported Stop (15 November)

An independently verified read-only static inspection uncovered a more
specific source of the remaining lifetime ambiguity. The owner's exact
1999 APMMXbvr.dll exports `_SND_fn_bTestIsPlayingSxdADPCM@4` at
**RVA 0x5150**. The following actual original call sequence is possible:

```text
0x5150  TestIsPlayingSxdADPCM (export)
  0x5155 -> 0x2900  wrapper checks initialization
    0x290E -> 0x1F50  checks [record+0x28] active state
      0x1F68 -> 0x15D0  retires inactive record and rebases handle pool
```

The final call is **conditional**: the cleanup must receive a non-null handle
whose record's active flag is zero. A mere
TestIsPlaying invocation does not necessarily retire the record.

The same exact original DLL has normal explicit Stop entry points
with visibly bracketing calls to the known shared critical-section
helpers (`0x1020` enter, `0x1030` leave; lock object at VA
`0x100163D0`). Its two Play wrappers also bracket the shared primary
allocator `0x13A0` with the same lock:

| Native route | Known lock acquire | Relevant operation | Known release |
| --- | --- | --- | --- |
| transition Play `0x2780` | `0x2795` | `0x27BE -> 0x13A0` | `0x281D` |
| regular Play `0x31E0` | `0x31F7` | `0x3223 -> 0x13A0` | `0x323C` |
| direct Stop `0x2930` | `0x2947` | `0x2950 -> 0x15D0` | `0x295D` |
| transition Stop `0x3340` | `0x334E` | `0x335B -> 0x1700 -> 0x15D0` | `0x3368` |
| TestIsPlaying `0x5150` | **none directly** | inactive cleanup `0x1F68 -> 0x15D0` | **none directly** |

A bounded raw E8/rel32 call scan on the exact original `.text`
found no **direct** calls to `0x1020` or `0x1030` in the four
verified function ranges `0x5150..0x515E`, `0x2900..0x2922`,
`0x1F50..0x1F72`, or `0x15D0..0x16F0`. Thus **this
exported path does not locally acquire the driver's known lock**, unlike
the documented Play/Stop wrappers. This is *not* a proof that external
callers, device callbacks or other locks never synchronize it, and is
not permission to hook any original function unsafely.

**Important consequence for real ESC logs:** the owner's
`Rayman2Fix(20261115-084027).log` contains three early ESC
sessions whose matched source/Theme pairs report
`play_between=0 stop_between=0` and stable *sampled* `9:6`
SXD provenance. Those zero counts only cover the instrumented
export-facing callback routes, **not the internal inactive-cleanup
retirement above**. They cannot validate a physical record epoch,
and do not refute an internal retire/reallocation between snapshots.
Conversely, the audit does not prove such an internal retire actually
occurred in any of those three sessions.

**Reproduce offline:**

```sh
python3 tools/audit_adpcm_lock_routes.py Rayman2Fix/dll/APMMXbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_lock_routes.py'
```

The auditor requires the owner's exact original SHA-256 and PE32
fingerprint, verifies 21 byte anchors and 15 relative direct-call edges,
checks four original bounded ranges for known direct critical-section
acquires/releases, and independently identifies the four bracketing
lock envelopes. Nine synthetic regression tests verify fail-closed
behaviour with changed instructions, metadata, missing/extra calls
and unknown DLL versions. Neither auditor nor test launches DLLs,
injects hooks, or writes to game memory.

**Decision remains NO-GO:** an observer of only exported Stop/Play
callbacks and a borrowed lock is *not* a sufficient per-voice
lifecycle monitor. Prove the full chain of original internal cleanup,
primary recreation, and asynchronous callback synchronization before
attempting early-ESC audio compensation.

## 10. WAVMW type-4 TestIsPlaying dispatch reaches the ADPCM callback table (15 November)

The owner provided **both** original 1999 modules; their file hashes,
native exports and intermediate function bodies were audited directly,
read-only and without executing either DLL:

| Original | SHA-256 | Relevant export |
| --- | --- | --- |
| WAVMWbvr.dll | `cd0a0ad0a95880cd8e25e50844ec8e6f9d6725bbee7c09a8ab328cbbc953f016` | `_SND_fn_bTestIsPlayingSxd@4` at RVA `0xB8A0` |
| APMMXbvr.dll | `ab2d7cc613fea6dd8b039e76f00a6f5d15c7ed37f2b93e0be7a86dbc6a0ee4cf` | `_SND_fn_bTestIsPlayingSxdADPCM@4` at RVA `0x5150` |

Verified **static** dispatch chain in the WAVMW caller:

1. `0xB8A0` branches on the original typed sound object.
   The type-4 branch `0xB8B2 -> 0xB8C4` calls `0x7B90`.
2. `0x7B90` uses callback slot `[ebx+0x4C]`. Its nine-entry
   callback table is copied at `0x7DB4` to the object base `+0x2C`.
   The ninth function pointer, constructed at `0xBFA7`,
   is the existing WAVMW trampoline `0x3420`.
3. `0x3420` dispatches via dynamic global pointer
   `0x10026A00`. During loader setup, `0x57EA` supplies the
   **exact embedded name** `_SND_fn_bTestIsPlayingSxdADPCM@4`
   from RVA `0x21560`; the resolver at `0x3FA0` imports
   `GetProcAddress`, and the returned function pointer is published
   at `0x57F8`.
   If this delegated TestIsPlaying result is zero, the WAVMW
   helper at `0x7BBC` may invalidate its inner sound wrapper at
   `0x7BED` and remove it through `0x7BF3 -> 0x7550`. The
   caller at `0xB904` can then unlink the outer SXD list entry
   through `0xB909 -> 0xB630`. Thus a *query* can trigger
   additional WAVMW object cleanup without an exported Stop.
4. On the owner's exact APMMX, that symbol exports RVA `0x5150`.
   As proved in section 9, this reaches the original
   `0x2900 -> 0x1F50 -> 0x15D0` *conditional* inactive cleanup
   path that does **not** call exported Stop.

This significantly narrows the missing-lifetime issue: **the type-4
interface used for the actual owner-observed ESC ADPCM path can reach
internal record retirement while public Stop-call counters stay zero**.
It remains possible that a given invocation returns earlier, that
another source is responsible for a particular call, or that a
different function pointer is installed at runtime. These static
facts do **not** prove that such retirement actually happened during
any logged ESC session. No claims are made about an invariant
runtime function-pointer address or lock ownership across threads.

Reproduce the two-DLL cross-check locally:

```sh
python3 tools/audit_adpcm_lock_routes.py Rayman2Fix/dll/APMMXbvr.dll \
  --wavmwbvr Rayman2Fix/dll/WAVMWbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_lock_routes.py'
```

The extended auditor validates **23 additional WAVMW opcode windows,
five direct-call edges, both exact PE versions and export names,
the embedded ADPCM symbol, and the original type-4 branch**. The
synthetic test suite now has **14 tests**. It does not capture
runtime function pointers or add any process hook; both DLLs remain
owner-local and no game binaries are committed.

**Next evidence gate:** a fully read-only, bounded test of **whether
and when** the WAVMW type-4 TestIsPlaying delegate is invoked relative
to early ESC and whether it triggers internal APMMX retirement.
That would require a carefully version-pinned runtime research build
with race-safe observation, *not* activating the proposed delayed
Pause/Resume bridge. The existing native owner gate and quarantine
passthrough remain in place.


## 11. A false TestIsPlaying result is not a retirement witness (15 November)

Further read-only, **exact SHA-256 verified** disassembly of the two
owner-supplied 1999 DLLs has resolved the possible outcomes of the
WAVMW type-4 `TestIsPlayingSxd` path. The original WAVMW
`0xB8A0 -> 0x7B90` helper contains *two decision gates before*
its indirect `0x7BB0` TestIsPlaying callback:

| Original path | WAVMW result | APMMX Test callback? | ADPCM retirement call? |
| --- | --- | --- | --- |
| Inner record `[+0x34]` nonzero (`0x7B9B..0x7BA4`) | 0 (false) | **No** | **No** |
| `[+0x34]=0`, `[+0x30]=0` (`0x7BA6..0x7BB7`) | 1 (true) | **No** | **No** |
| APMMX resolver missing, original WAVMW fallback at `0x5801 -> 0x3440` | 0 | Stub only | **No** |
| Native APMMX module disabled (`0x2900..0x2920`) | 0 | Yes | **No** |
| Native APMMX handle null (`0x1F50..0x1F70`) | 0 | Yes | **No** |
| Native record active, `[record+0x28]!=0` (`0x1F58..0x1F66`) | 1 | Yes | **No** |
| Native record inactive, `[record+0x28]=0` (`0x1F5F -> 0x1F67`) | 0 | Yes | `0x15D0` called, **not proven successful** |

A false result also triggers WAVMW's conditional cleanup of the inner
wrapper and outer SXD list entry, as shown in section 10.
**Neither WAVMW's cleanup nor a false result establishes that the
physical ADPCM record was retired.** Even on the last row the original
`0x15D0` retirement validates the supplied handle and can return
without freeing anything. In particular, neither return-value logging
nor an exported Stop-call count can serve as a trustworthy monotonic
physical voice generation.

The original WAVMW resolver can point to the local always-false
trampoline `0x3440` when the native symbol is absent. For the
owner's observed runtime the APMMX delegates seen in earlier logs
were resolved for Stop/Pause/Resume; that alone is **not evidence**
that the separate `TestIsPlaying` delegate is currently installed
or has actually executed. Do not infer a fallback occurred merely
from static code.

**New offline-only test equipment:**
`tools/audit_adpcm_query_outcomes.py` requires the exact original
pair, independently repeats the two-DLL bridge audit, then verifies
10 WAVMW and 9 APMMX instruction windows and 7 conditional branch
targets. Its synthetic `model()` intentionally never reports a
completed physical retirement; unknown state is rejected rather than
coerced to zero. `tests/test_adpcm_query_outcomes.py` adds
14 synthetic branch regression cases (including no delegate,
inactive record, null handle, missing data and version rejection).
CI runs these separately from the 14 prior lock-route tests.

```sh
python3 tools/audit_adpcm_query_outcomes.py \
  Rayman2Fix/dll/WAVMWbvr.dll Rayman2Fix/dll/APMMXbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_query_outcomes.py'
```

**Limits of existing owner Wine evidence:** the user-supplied
`Rayman2Fix(20261115-084027).log` has three early ESC openings
whose two native Pause callbacks are source-paired and whose
post-dispatch ADPCM sample is `paused=1`. Their matching
source-paired Resume lifetimes say `play_between=0 stop_between=0`.
Those counters did *not* trace WAVMW TestIsPlaying or original
internal ADPCM retire, so the log cannot distinguish the above
branches and does not establish that hidden retirement happened.

**Decision unchanged:** no synthetic native Resume, no suppression
of original Pause, no direct call to native TestIsPlaying for
observation, no relaxation of strict Theme/SXD ownership. A future
research probe would have to observe existing call/return boundaries
*passively*, with exact original DLL provenance, generation-safe
record retirement/creation coverage, concurrency handling and bounded
event buffering. Any missing coverage remains fail-closed.

## 12. Bounded read-only ADPCM active-state samples at ESC open and close

The current opt-in runtime observer already reads the exact
owner-identified APMMX post-dispatch Pause flag (record `+0x1C`)
after the original ESC script-pause wrapper. It is not an immutable
physical music record identifier.

To close a diagnostic blind spot, the existing snapshot helper now also
reads the *original* active flag (record `+0x28`), **twice** together
with the Pause flag. It discards a sample if those two values change
between reads or if the original handle backlink / pool count changes.
The bounded `RUNTIME-ESC-ADPCM-VOICE` message adds
`active=0|1|2|-1` (`-1` means unknown/unverified,
`2` means an unexpected nonzero flag).

A companion `RUNTIME-ESC-ADPCM-RESUME` line is written to the
**render thread only**, after the original script-resume wrapper has
finished and its bounded report has been flushed. To read the native
state, it requires **all** of:

- a source-matched, same-thread, nonoverlapping ESC Pause/Resume pair;
- exactly two original native Resume callbacks;
- no unrelated native audio slots and no second callback handle;
- a freshly observed native WAVMW type-4 transition object;
- the exact original WAVMW/APMMX PE versions, callback binding,
  record/backlink/slot checks and repeated stable samples.

It then reports `sampled`, `voice_index`, `paused`,
`active`, `other_gate`, and `transition_type`. The observer
does **not** persist raw pointers between opening and closing,
call `TestIsPlaying` (which can itself retire native records),
install new driver hooks, change the INI format, dispatch a new
Pause/Resume/Stop, or use any sample to authorize audio compensation.
The existing 16-opening / 16-resume budgets and
`[Features] MusicInPauseMenu=1` plus `[Debug] Log=1`
gates are unchanged. This code remains in the already separately
opt-in experimental ESC music feature.

**Interpretation:**
`active=0` observed in a verified original record is useful
evidence of inactivity at one instant. A changed verified record
slot/index or unexpected Pause flag is diagnostic evidence requiring
inspection. But `active=1` at both endpoints or equal
`voice_index` values **do not prove continuity**: a retired record
may be replaced at the same address between reads (native ABA).
A false or missing snapshot (`sampled=0`) is *unknown*, never
evidence of continued physical music. The scanner is not
synchronized with the original audio mixer; agreement between two
reads narrows, but cannot eliminate, a race or a hidden lifetime
change. All cases remain NO-GO for the proposed early-ESC
pre-driver Pause suppression.

Static guards in `tests/test_audio_trace_guards.py` verify the
original Resume always executes before observation, that diagnostics
are flushed after the menu tick's normal audio dispatch, and that
neither sampler invokes native TestIsPlaying or authorizes audio
commands. The Windows x86 build additionally checks compilation of
the separately gated opt-in ESC research runtime.

A future owner Wine log generated by the updated opt-in binary
can show early-ESC opening/closing states without a new intrusive
test hook. The old 15 November log predates the closing observer
and must **not** be described as having such evidence.

## 13. An original GetPos query can mark an ADPCM record inactive (15 November)

Exact-owner x86 disassembly revealed a **second query-origin state
transition**, which connects the previous WAVMW/ADPCM TestIsPlaying
cleanup to the voice's earlier *inactivation*. On the same original
SHA-256-pinned 1999 `APMMXbvr.dll`:

```text
0x51A0  export _SND_fn_rGetPosSxdADPCM@4
  0x51A5 -> 0x2A00  original position-query wrapper
    0x2A20 -> 0x2320  reads/progresses a validated voice record
      0x2355 -> 0x2480  native handle-to-record validation
      0x238C            EBX = 0 (zero register)
      0x2405            compare [record+0xDC] with zero
      0x240B -> 0x2449  skip completion branch if +0xDC == 0
      0x243C/0x2444    floating-point status condition
      0x2446            mov [record+0x28], ebx ; write 0
```

The state write at `0x2446` is **conditional**. The caller must
have passed the original handle validation, entered the branch where
`[record+0xDC]` is nonzero, and taken the floating-point comparison
outcome that does not skip the write. The instruction is a **zero
write to the same active flag** checked by TestIsPlaying's
`0x1F5A` routine, *not a physical retirement* or an exported Stop.

Another original playback/progression path can write zero to the
same active flag at `0x17CA`; the shared record initializer writes
one to it at `0x2007`. These four exact machine instructions are
separate paths in the original driver. In particular, it is wrong
to assume that `[record+0x28]=0` can only arise from exported
Play/Stop or from `TestIsPlaying` itself.

**Concrete possible two-step sequence with zero exported Stop calls:**

1. The game calls its existing original `rGetPosSxdADPCM` export,
   and the conditional completion path marks the live record inactive.
2. Later, an existing original `bTestIsPlayingSxdADPCM` call
   encounters that inactive record. It can attempt internal
   `0x15D0` retirement and clear the WAVMW sound wrappers.

No explicit exported Stop is required in either step. This is an
original **possible code path**, **not evidence** that either getter
or TestIsPlaying was invoked in the owner's early-ESC Wine log.
Neither step by itself proves completed native record retirement
or a stable generation of music.

**New regression coverage:** `tools/audit_adpcm_query_outcomes.py`
now verifies the original `rGetPosSxdADPCM` export, **17 exact
instruction windows, three signed direct-call edges and two
conditional branch targets**. The synthetic, fail-closed
`model_position()` expresses only this conditional active-flag
write and explicitly refuses to infer native retirement. Ten
additional synthetic tests check guard predicates, malformed branch
and opcode bytes, export identity and unknown branch state. The
original DLL itself is never committed or executed.

```sh
python3 tools/audit_adpcm_query_outcomes.py \
  Rayman2Fix/dll/WAVMWbvr.dll Rayman2Fix/dll/APMMXbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_query_outcomes.py'
```

**Diagnostic design restriction:** a runtime probe must not invoke
native GetPos to retrieve a convenient timestamp/position: GetPos
is **not guaranteed read-only**. Native TestIsPlaying is also not
guaranteed read-only. Our experimental ESC observer therefore uses
only bounded `ReadProcessMemory` snapshots after original actions.
Those snapshots remain non-atomic and cannot exclude an ABA lifetime
between samples. The feature's strict ref-class gate and original
script-pause fallback remain unchanged.

### WAVMW game-facing GetPos bridge (cross-DLL confirmation)

The actual owner-provided `WAVMWbvr.dll` also exposes
`_SND_fn_rGetPosSxd@4` (RVA `0xBD20`). Its ADPCM
**type-4 branch** is a statically verified route to the same
side-effecting APMMX getter:

```text
WAVMWbvr.dll (exact owner original)
  0xBD20  _SND_fn_rGetPosSxd@4
  0xBD30  type-4 branch -> 0xBD44
  0xBD4F  direct call -> 0x7CD0
  0x7CD0  checks inner [+0x30] state (can return early)
  0x7CE2  indirect callback call [table+0x40]
  0xBF8F  callback-table slot +0x40 built as trampoline 0x3510
  0x3510  dynamic trampoline -> pointer [0x10026A14]
  0x587E  loader resolves the exact symbol name
          _SND_fn_rGetPosSxdADPCM@4 (stored at RVA 0x21608)
  0x588C  publishes resolved pointer at [0x10026A14]
APMMXbvr.dll (exact owner original)
  0x51A0  _SND_fn_rGetPosSxdADPCM@4
  0x51A5  -> 0x2A00 -> 0x2320 -> conditional [record+0x28]=0
```

This closes the **static call-graph bridge** between the game-facing
WAVMW interface and the original ADPCM position getter. It does
**not** prove that Rayman 2 called that interface during the
owner's early-ESC session or that the runtime-installed callback
pointer was the expected original address. WAVMW can also return
before calling the delegate if its inner state is uninitialized.

The exact-hash offline audit now checks 13 WAVMW GetPos instruction
windows, two additional direct-call edges, the type-4 conditional
branch, both export names, dynamic symbol at RVA `0x21608`, and
the nine-entry callback-table routing. Six additional
synthetic tests refuse stale DLL export names, modified callback
slots, unexpected dynamic symbols and divergent branch opcodes.
Combined with the ten new APMMX GetPos tests, the
`test_adpcm_query_outcomes.py` suite now has **33 synthetic tests**.
No native query is invoked by the auditor or by the experimental
runtime monitor. Neither module is bundled in this repository.

**Consequence for the first ESC:** the *possibility* of
`WAVMW GetPos -> ADPCM active=0 -> WAVMW TestIsPlaying ->
internal ADPCM retirement` with no public Stop is established
across the original binary implementations. Actual ordering and
occurrence in the owner runtime remain unobserved, so no new
native Pause suppression or synthetic Resume is permitted.

### Original GetPos completion sentinel is not Stop

Further exact byte-by-byte inspection of the same owner's original
1999 APMMX DLL found a second distinction in the return path:

- The internal position reader checks `[record+0x4C]` at RVA
  `0x2449`, then checks `[record+0x28]` at `0x244E`.
  When both the terminal position and inactive condition are met,
  it returns the internal status **15** via `0x2453`.
- The GetPos public core at `0x2A2C` maps that internal status
  **15** to the signed return value **-2** (`0xFFFFFFFE`).
  Other nonzero reader failures return **-1**, while status zero
  follows the position-computation path.
- Neither internal status 15 nor the exported -2 return is a
  *successful ADPCM retirement event*. The record may still have a
  valid handle until some later native cleanup.

These are additional **original, hash-pinned opcode facts**;
no claim is made that the owner saw the -2 sentinel during the
early ESC session. A synthetic status classifier and three regression
tests now explicitly prevent treating terminal position as Stop,
reject malformed status values, and reject any change to the
original status-translation instructions.

## 14. Query-delegate provenance before and after Theme sync (15 November)

The owner-original `WAVMWbvr.dll` contains **five separately
resolved** type-4 APMMX driver callbacks relevant to this experiment,
not only the earlier Stop/Pause/Resume trio:

| Original WAVMW pointer RVA | Operation | Original WAVMW fallback RVA |
| --- | --- | --- |
| `0x26A98` | StopTransition | `0x3C50` |
| `0x26A9C` | PauseTransition | `0x3C80` |
| `0x26AA0` | ResumeTransition | `0x3CB0` |
| `0x26A00` | **TestIsPlaying** | `0x3440` |
| `0x26A14` | **GetPos** | `0x3530` |

Exact SHA-256-identified original disassembly confirms both new
fallback functions and their indirection tables. **GetPos and
TestIsPlaying are *queries with potential side effects***, so the
runtime **never calls them** for monitoring.

The existing, singly bounded function
`log_adpcm_delegate_origins_readonly()` now reads all five pointer
slots using the same verified original WAVMW PE timestamp, image
size, Pause entry opcode and read-only address-to-module classifier.
It does not load modules, invoke delegates, install hooks, publish raw
addresses, or modify driver memory.

Instead of emitting the pointers only once in an entire session,
the observation happens at most **twice**, once for a
source-confirmed, original pending Theme reference
(`native_ref_class=3`) and once for a synchronized original
Theme reference (`native_ref_class=1`). It executes in the
existing render-thread `proto_menu_tick`, *after* the original
native audio script and existing compensation dispatch. A new line
marks each stage:

```text
RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT: ref_class=3 stage=pending ...
RUNTIME-AUDIO-ADPCM-DELEGATE: op=test-is-playing status=resolved module=... callback_rva=...
RUNTIME-AUDIO-ADPCM-DELEGATE: op=get-pos status=resolved module=... callback_rva=...
RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT: ref_class=1 stage=synchronized ...
```

The original log
`Rayman2Fix(20261115-084027).log` **predates** this
five-pointer, dual-stage diagnostic. It contains only **three
resolved legacy callbacks**, four opening samples, three
paired `Play/Stop=0` menu lifetimes, and **zero** valid
post-resume active-flag samples. It cannot prove that either
query executed, that a hidden retirement happened, or that
the physical music voice was unchanged between observations.

A new **offline, read-only log classifier**,
`tools/audit_esc_adpcm_evidence.py`, keeps these limits explicit.
It accepts legacy and new logs, groups the five callbacks by
source-confirmed stage, refuses to compare incomplete/duplicated/
unrecognized stage reports, and classifies the bounded
`RUNTIME-ESC-ADPCM-VOICE` and
`RUNTIME-ESC-ADPCM-RESUME` snapshots without treating
matching record slots as an immutable voice generation. The
twelve synthetic tests in
`tests/test_esc_adpcm_evidence.py` cover old three-callback logs,
missing pointers, fallback changes, bad stage classes, duplicates,
unpaired closes, activity markers and unknown values.

```sh
python3 tools/audit_esc_adpcm_evidence.py \
  'Rayman2Fix(20261115-084027).log' --json
python3 -m unittest discover -s tests -p 'test_esc_adpcm_evidence.py'
```

**Interpretation:** a query callback that is bound to the original
APMMX indicates an available route, **not** execution of that route.
A change in pointer identity across phases can invalidate an
assumed fixed delegate. An *unchanged* pointer, and even
`active=1` sampled at both endpoints, still cannot rule out
internal inactivation, reclamation or ABA reuse. All unresolved
observations remain **NO-GO** for a synthetic early-ESC Resume
or native Pause suppression. No release behavior was changed.

## 15. Complete raw direct-call candidates for native query and primary helpers (15 November)

I independently re-read the owner's exact **95,744-byte,
SHA-256-pinned original** `APMMXbvr.dll` and disassembled its
original `.text` section (RVA `0x1000`,
`0xD800` raw bytes). The expanded read-only
`tools/audit_adpcm_rel32_calls.py` now verifies the **entire raw**
`E8+rel32` direct-call candidate set for eight internal targets:

| Target RVA | Original raw direct-call sites | Meaning |
| --- | --- | --- |
| `0x13A0` | `0x27BE`, `0x3223` | Primary allocator used by two Play routes |
| `0x15D0` | `0x172C`, `0x1C8D`, `0x1F68`, `0x2950` | Native record retirement, including internal cleanup |
| `0x1FC0` | `0x14F9`, `0x21F3`, `0x22A3` | Shared primary/**auxiliary** record initializer |
| `0x1F50` | **`0x290E`** | Conditional inactive cleanup inside TestIsPlaying |
| `0x2320` | **`0x2A20`** | Position/progression reader inside GetPos |
| `0x2480` | `0x15EB`, `0x170E`, `0x1C3F`, `0x1EDD`, `0x1F1D`, `0x1F8D`, `0x218E`, `0x2222`, `0x22DE`, `0x2355` | Shared handle validator |
| `0x2900` | **`0x5155`** | Public APMMX TestIsPlaying entry |
| `0x2A00` | **`0x51A5`** | Public APMMX GetPos entry |

All **23** candidate instructions were verified in an independent
`objdump -d -M intel` listing of the same SHA-pinned original.
This provides a useful *static* bound: each of the two internal
query helper functions `0x1F50` and `0x2320` has exactly **one
raw direct-call candidate** in this original `.text` section.
The existing original export stubs, in turn, are their sole
raw direct-call candidates at the next level. This justifies
targeting future external diagnostics at the existing query
boundary, rather than invoking the side-effecting helpers.

**Do not turn a single caller into a false runtime proof.**
The byte scan cannot exclude indirect calls, tail jumps,
nonstandard control transfer, alternate entries, concurrency or
observational side effects from a debugger. It cannot show which
calls executed in the owner's early-ESC session. The test suite
uses only synthetic bytes, refuses extra/missing GetPos or
TestIsPlaying caller sites, and preserves
`native_generation_proven=False` and
`permission_to_modify_audio=False`.

### Fingerprint a resolved query delegate, but never call it

A separate review of the exact APMMX export table and first
16 machine-code bytes establishes these original callback entries:

| Operation | Exact original APMMX export RVA |
| --- | --- |
| StopTransitionSxdADPCM | `0x54C0` |
| PauseTransitionSxdADPCM | `0x54D0` |
| ResumeTransitionSxdADPCM | `0x54E0` |
| TestIsPlayingSxdADPCM | `0x5150` |
| GetPosSxdADPCM | `0x51A0` |

The existing **bounded and passive** render-thread delegate
snapshot now reports
`original_entry_fingerprint=0|1` for each of these
five dynamic pointers. A positive value requires the owner
`APMMXbvr.dll` basename, expected RVA, exact 1999 PE32
timestamp and image size, and byte-for-byte matching first 16
export-entry instructions. This is **not** a full-memory or
SHA-256 check of the running DLL, nor proof that the game
called any callback. An unrelated module or unexpected code
gets fingerprint zero, never a positive provenance claim.

`tools/audit_esc_adpcm_evidence.py` now distinguishes a
**complete early/late pointer report** from a report in which
**all five original entry fingerprints match** at both stages.
A false, absent or mismatched provenance flag must never
be promoted into a physical audio voice identity. Runtime
behavior and audio forwarding are unchanged.

```sh
python3 tools/audit_adpcm_rel32_calls.py Rayman2Fix/dll/APMMXbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_rel32_calls.py'
python3 -m unittest discover -s tests -p 'test_esc_adpcm_evidence.py'
```

A future log may show that five exact original query delegates
were **available** before and after Theme synchronization, but
**only an actual non-intrusive runtime call/retirement observer**
could establish the timing and physical voice lifecycle.
**No new Pause suppression is authorized; PR #10 remains draft.**

## 16. Explicit NO-GO on hidden lifetime events in the unconnected ESC escrow model

A consequence of sections 9–15 is that a possible deferred pre-driver
Pause cannot be safely managed using **exported Stop counts alone**.
The original `rGetPosSxd` path may mark a record inactive, a subsequent
`bTestIsPlayingSxd` may retire it internally, and the primary allocator
may reinitialize the same handle/record without an exported Stop. The
original pre-driver research model previously covered explicit
`native_stop()` and changed script identities, but lacked a
corresponding **unclassified native lifetime event**.

The **pure, unconnected** C++ research policy
`include/rayman2fix/esc_pre_driver_pause_policy.hpp` now exposes
`unknown_native_lifecycle_change()`. It is not invoked by the game
and does not install a driver callback. Its sole effect on the
synthetic ESC escrow is to fail closed:

- When an original Pause is being held, clear the pending ledger,
  refuse to replay it and explicitly report
  `impossible_replay=true`.
- When a menu has already been committed, invalidate all future
  suppression of the original script Resume, **even if the new voice
  reused the identical handle and Theme/SXD identity**.
- Do nothing outside an active escrow; no future session is
  silently armed or modified.

Four C++ regressions now exercise hidden retirement with one Pause,
hidden retirement with both original Pauses held, same-handle
in-place reinitialization after menu confirmation, and unknown
lifecycle signal outside ESC.

**Crucial boundary:** having a safe invalidation method in an
offline model is not proof that a running observer can *detect
every* hidden lifetime transition at the right instant.
The method is deliberately **not connected to**
`src/runtime/runtime.cpp`, does not add a native hook, and
does not authorize a first-ESC music bypass. Actual detection
and synchronization remain the unresolved gate.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target esc_pre_driver_pause_tests
ctest --test-dir build --output-on-failure -R esc-pre-driver-pause-research-model
```

## 17. Original GetPos active-flag writer is not locally lock-bracketed

Direct, read-only inspection of the same exact original 1999
`APMMXbvr.dll` reveals that **all three original functions in the
game-facing GetPos ADPCM chain** have no raw direct `E8` call
to the known native critical-section helpers
`0x1020` (acquire) or `0x1030` (release) within
their verified function bodies:

| Function | Exact original RVA range inspected | Potential action |
| --- | --- | --- |
| GetPos public export | `0x51A0..0x51AF` | Forwards original argument |
| GetPos core | `0x2A00..0x2A54` | Calls `0x2320`, processes return status |
| Position/progression reader | `0x2320..0x247D` | **May write `[record+0x28]=0`** at `0x2446` |

This extends the existing TestIsPlaying/internal-retirement
no-local-lock finding. The expanded
`tools/audit_adpcm_lock_routes.py` verifies **seven**
bounded non-lock-calling code ranges and the original four
Play/Stop lock envelopes. Two additional synthetic regressions
reject a new direct native-lock call inside the GetPos reader
and retain the **not-proven** external/indirect synchronization
status.

**What is and is not proved:** neither original GetPos nor
TestIsPlaying visibly calls the known internal lock helpers
in its own bounded function chain, whereas the known
Play/Stop wrappers do. That does **not** prove they execute
without a lock acquired by a *caller*, nor does it identify
the exact thread that owns each audio operation. The original
read/write lifetime may still race with a mixer or asynchronous
original callback. Therefore even two identical coherent
`ReadProcessMemory` endpoint samples cannot certify no
retirement or ABA between them.

### External tracing is a different, intrusive experiment

The independently documented Wine `+snoop` facility can
instrument **native DLL export calls**, whereas ordinary
`+relay` predominantly observes Wine's builtin DLL interfaces.
Wine's implementation uses trampolines, and community
documentation explicitly warns `+snoop` may destabilize
an application:
<https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/relay.c>.
Likewise, attaching a debugger and placing execution
breakpoints can stop/resume game threads and alter timing.
**None of these instruments has been activated for Rayman2Fix,
and they should not be described as non-perturbing or as
proof of native lifetime coverage.** A controlled,
separately gated investigation would need to establish
its own no-regression and threading criteria.

The new passive five-delegate fingerprint + ESC endpoint
diagnostic is safer for gathering preliminary evidence,
but remains a **provenance and endpoint** observation only.
The pre-driver Pause escrow stays disconnected from
the game and native driver. PR #10 remains a draft.

## 18. Identical observations, different physical ADPCM epochs — synthetic counterexample

The new, stricter five-delegate original-entry fingerprints
still cannot establish a native musical epoch. A regression in
`tests/test_esc_adpcm_evidence.py` explicitly constructs
**two different physical histories producing exactly the same
observable ESC log**:

| Observation | Continuous voice | Native in-place ABA reuse |
| --- | --- | --- |
| Five query/Play/Stop/Pause/Resume callbacks fingerprint the original APMMX | Yes | Yes |
| Original native driver handle at opening and closing | Same | Same |
| Active voice slot `0` at both endpoints | Yes | Yes |
| Original driver active flag `1` at both endpoints | Yes | Yes |
| Native exported Play/Stop counts during ESC | 0 / 0 | 0 / 0 |
| Physical voice generation changed internally | **No** | **Yes** |

The second history represents the original allocator's proven
in-place record reuse path. A successful internal allocation can
reuse the native handle and record without calling exported Stop.
The first history contains no such internal change. Both are
compatible with **one identical set of endpoint measurements**.
The log classifier correctly leaves
`can_prove_continuous_physical_voice_generation=False` in
both cases even when all five DLL callback entry fingerprints
match. A newer source/richer sample cannot change that logical
limitation without directly observing original creation/retirement
events.

The source guard suites now include:
- **10** synthetic original direct-call candidate tests;
- **16** original WAVMW/APMMX lock-route synthetic tests;
- **17** offline ESC pointer/log-evidence tests, including this
  indistinguishability witness;
- C++ policy cases for unknown native lifetime invalidation.

No original DLL bytes or executable were committed to these fixtures.
The original WAVMW/APMMX audio functions remain uncalled by the
diagnostics; the experimental early-ESC physical driver bypass
remains **NO-GO**.

## 19. Parameter updates are another hidden native retirement route (15 November)

Further exact-original APMMX disassembly found a third native
lifecycle route not covered by the existing exported Stop,
TestIsPlaying or GetPos counters. Both exported parameter setters
can reach the **same private record-retirement function**:

```text
APMMX: _SND_fn_bSetParamSxdADPCM@8
    0x5120 -> 0x513C -> 0x28B0
    0x28C1 -> 0x1020  [driver lock acquire]
    0x28D3 -> 0x1C30  [internal parameter operation]
    0x28E9 -> 0x1030  [driver lock release]

APMMX: _SND_fn_bSetParamTransitionSxdADPCM@8
    0x5430 -> 0x544C -> 0x3250
    0x326A -> 0x1020  [driver lock acquire]
    0x3278 -> 0x1C30  [same internal parameter operation]
    0x328C -> 0x1030  [driver lock release]

Internal:
    0x1C54   test original record [+0x28] active flag
    0x1C59   if already inactive -> 0x1C8C
    0x1C8D -> 0x15D0  [internal record-retirement call]
```

The internal operation updates parameters of active records
through `0x1DC0`, and invokes `0x15D0` instead if its
validated native record is already inactive. This means a
**game-issued parameter update may retire a prior ADPCM record**
without any exported Stop call. Unlike the GetPos and
TestIsPlaying paths, **both original SetParam wrappers use the
known driver lock**; acquiring that lock does not make their
side effect visible to our Stop counter.

A complete SHA-256-pinned `.text` raw `E8+rel32` scan
finds precisely the two `0x1C30` callers `0x28D3` and
`0x3278`, reached by the two exported stubs `0x513C` and
`0x544C`. Thus the verified target set in
`tools/audit_adpcm_rel32_calls.py` expands to **11 original
internal targets and 27 direct call candidates**. The
`tools/audit_adpcm_lock_routes.py` now verifies **33 exact
opcode windows, 24 direct call edges and six lock-bracketed
routes**, including both SetParam wrappers. Its synthetic
tests fail closed on modified conditional instructions, extra
SetParam call sites and missing lock release. These counts
describe only the listed target set, not every x86 call
throughout the DLL.

### Seven dynamically resolved native ADPCM delegates

The original owner-provided WAVMW module loads the two
parameter callbacks by name just like the previous five:

| Callback | WAVMW pointer RVA | WAVMW original fallback RVA | APMMX original entry RVA |
| --- | --- | --- | --- |
| Stop | `0x26A98` | `0x3C50` | `0x54C0` |
| Pause | `0x26A9C` | `0x3C80` | `0x54D0` |
| Resume | `0x26AA0` | `0x3CB0` | `0x54E0` |
| TestIsPlaying | `0x26A00` | `0x3440` | `0x5150` |
| GetPos | `0x26A14` | `0x3530` | `0x51A0` |
| **SetParam** | **`0x269FC`** | **`0x3410`** | **`0x5120`** |
| **SetParamTransition** | **`0x26A88`** | **`0x3B60`** | **`0x5430`** |

The exact original WAVMW resolver at `0x57C5` reads
`_SND_fn_bSetParamSxdADPCM@8` from original RVA
`0x21540`, publishes the callback to `0x269FC`
at `0x57D3`, or installs fallback `0x3410`.
The resolver at `0x5C8A` loads
`_SND_fn_bSetParamTransitionSxdADPCM@8` from
original RVA `0x21A20`, publishes to `0x26A88`
at `0x5C98`, or installs fallback `0x3B60`.

The existing **opt-in render-thread read-only pointer observer**
now reports **seven instead of five** original delegate bindings.
The last two export entries have **two-argument stdcall signatures**,
but no delegate is called by the observer; their first 16
native machine bytes and expected export addresses are checked
alongside the previous five. Nothing changes in production audio,
or in the experimental Pause/Resume/Stop forwarding.

The offline log classifier retains the original **five-callback
core** comparison so logs from earlier builds remain readable.
It separately reports `complete_seven`,
`changed_parameter_operations`, and
`all_seven_original_entry_fingerprints_both_stages`
only when both pending and synchronized Theme stages have
unambiguous complete seven-pointer evidence. Missing parameter
pointers remain **unknown**, never unchanged. Even all seven
resolved fingerprints prove only callback *availability*, not
the execution or outcome of a parameter operation.

**Crucial limit:** nobody has yet observed a SetParam-triggered
retirement, a GetPos-triggered inactivation or a
TestIsPlaying-triggered retirement during an actual early
ESC session. The newly discovered paths show why the
original Stop/Play counters and five-pointer snapshots are
insufficient, not that the missing retirement actually
occurred. PR #10 remains draft; no physical driver
compensation bypass is authorized.

### Exact WAVMW type-4 SetParam dispatch and outer SXD cleanup

The SHA-256-pinned owner-provided `WAVMWbvr.dll` does not
merely resolve these two APMMX names: the real type-4
game-facing exports enter the matching dynamic callbacks.

For ordinary `_SND_fn_bSetParamSxd@8` at WAVMW
RVA `0xB7F0`, a type-4 sound object takes the conditional
branch `0xB805 -> 0xB817`, calls the helper
`0xB827 -> 0x7AF0`, and dispatches via callback
table slot `[ebp+0x34]` at `0x7B16`.
The original nine-function table has exactly the
`0x33E0` thunk in that slot (`0xBF77`).
This thunk invokes the dynamically resolved original
APMMX SetParam callback through `[0x100269FC]`
at `0x33F9`. If the WAVMW call returns **false**,
the caller can additionally unlink the **outer WAVMW SXD**
node via `0xB87F -> 0xB630`, without an exported Stop.

For `_SND_fn_bSetParamTransitionSxd@8` at
WAVMW RVA `0xC5D0`, the type-4 branch
`0xC5E3 -> 0xC5FA` enters the
`0xC603 -> 0x3B30` dynamic trampoline.
The thunk at `0x3B49` invokes the original
`[0x10026A88]` APMMX SetParamTransition callback.
This type-4 branch does **not** have the
same WAVMW outer-list cleanup evidenced in the
ordinary SetParam caller; do not generalize it.

Both resolver symbols, pointer publications, fallback
functions, both public exports, type-4 branches,
indirect callback sites, and the conditional outer
cleanup are now checked by
`audit_wavmw_setparam_bridge()` inside
`tools/audit_adpcm_query_outcomes.py`. This adds
**21 WAVMW exact opcode windows, five rel32 call
edges, two conditional branches** and six synthetic
negative/positive tests. The larger
`test_adpcm_query_outcomes.py` suite now contains
**39** synthetic cases.

This establishes a complete *possible* game-facing
path `WAVMW SetParam -> APMMX SetParam -> internal
inactive record retire`, with possible secondary
WAVMW outer SXD unlink. It does **not** show that
the game issued such a parameter operation during
the first Escape, nor identify the retired voice.
The experimental seven-pointer log probe observes
bindings only, never calls setters itself.

## 20. Native output-buffer fill can inactivate an ADPCM voice without game-issued queries (15 November)

The owner-provided, SHA-256-pinned original `APMMXbvr.dll` has
**another distinct route** to the native record active flag
`[record+0x28]`. This route is the **ordinary audio
output-buffer fill**, independent of any *game-issued* GetPos,
TestIsPlaying, SetParam or exported Stop call:

```text
APMMX original output / mixer buffer-fill wrappers
  0x3400:
    0x342C -> 0x1020       acquire original driver lock
    0x344C -> 0x18D0       fill buffer (mode=0)
    0x3498 -> 0x1030       release original lock

  0x34B0:
    0x34CE -> 0x1020       acquire original driver lock
    0x34F2 -> 0x18D0       fill buffer (mode=1)
    0x353E -> 0x1030       release original lock

APMMX common output-buffer fill
  0x18D0                   fill/advance native ADPCM buffer
    0x1AD1 -> 0x1740       progress an internal native record
      0x174C               EDX = 0
      0x1753 / 0x175F      inspect record +0x24 state
      0x17A5 / 0x17A7      inspect record +0xD8 state
      0x17BF / 0x17C4      inspect record +0x20 state
      0x17CA               mov [record+0x28], EDX   (= 0)
```

The final write is **conditional**. In the verified original
branches, the zero-write path is taken when the earlier
`+0x24`, `+0xD8`, and `+0x20` gates are zero;
other cases branch around it or handle a different form of
record progression. This is **inactivation**, not a confirmed
record retirement or handle generation change.

### Where the buffer-fill callbacks come from

The same exact original APMMX initialization routine
`0x2AE0` registers the two callback function addresses
with an **external output-interface registration function**:

```text
0x2B39:  mov [esp+0x1C], 0x10003400    ; output callback A
0x2B41:  call 0x3A50                  ; register callback
0x2B68:  mov [esp+0x1C], 0x100034B0    ; output callback B
0x2B74:  call 0x3A50                  ; register callback

0x3A50:  original registration wrapper
0x3A73:  call dword [0x100E849C]     ; resolved output provider
```

Unlike the public game-facing queries, these functions are
passed as **function pointers** to another original audio
component. Therefore a raw E8-only caller scan cannot
account for each invocation of the mixer wrappers: the
registration wrapper hands their pointers to an
indirectly dispatched provider. The static code establishes
the **callback registration**, not how often or on which
thread those callbacks actually run.

The output provider is not simply an arbitrary inferred
function name. Original APMMX RVA `0x4B7D` supplies the
**exact embedded string**
`_SND_fn_lCreateNewBufferExSxd@16` (RVA
`0x12010`) to its dynamic resolver at `0x4B83`.
The function pointer is stored at native VA
`0x100E849C` by `0x4B8B`, and falls back
to `0x3A80` (returns `-1`) if unresolved.
At `0x3A73`, the callback registration thunk
calls this **resolved** output interface. The
original `WAVMWbvr.dll` exports that named
`CreateNewBufferExSxd` function, independently
confirmed from its export table. This establishes
a concrete *possible* cross-module callback
registration route from APMMX into WAVMW rather
than direct game-side invocation.

The cross-DLL audit can additionally read the original
SHA-256-pinned `WAVMWbvr.dll`. Its exported
`_SND_fn_lCreateNewBufferExSxd@16` entry
is RVA `0xC3F0`. The original WAVMW code checks
the buffer object's first type field at `0xC3FA`
and routes the creation through one of two
internal backends, `0xCB00` or `0xD180`.
This call-graph link is verified by **eleven WAVMW
opcode windows and three direct-call edges**.
It confirms that a genuinely exported original
WAVMW buffer-creation implementation corresponds
to the name dynamically resolved in APMMX.
Four additional synthetic regressions guard the
module SHA, export RVA and backend branches.
It still does not establish which backend executes
in a particular owner session or the eventual
thread that invokes the registered callback.

The type-2 backend `0xCB00` has another
**verified, indirect callback layer**. At
`0xCB88` it supplies original WAVMW function
`0xC980` as a lower-layer callback, then
`0xCB94` calls `0xC3F0` again to create
that lower-layer buffer. The WAVMW function at
`0xC980` contains an indirect call through
`[ebp+0x18]` at `0xCA27`. This confirms
that **a direct-call-only static graph is
insufficient** for callback scheduling, and
that the wrapper can delegate further through
a stored function pointer. The audit checks
both callback constants and the indirect opcode.

This does **not** establish that `[ebp+0x18]`
held one of the two APMMX callbacks in the
owner's actual ESC session, nor when and on
which thread the lower callback executed.
The possible player-voice write remains a
static native path, not a witnessed live event.

The audit verifies the resolver name and exact
fallback, but does not read the **runtime**
resolved module pointer. It cannot assert whether
the provider actually accepted the callback, ran
it on a separate thread, or invoked it during
the first Escape; those are independent live
questions. The identity,
scheduling policy and concurrency of the provider remain
unverified. The auditor verifies these two exact pointer
constants, both registration calls and the provider's
indirect dispatch, and the synthetic tests detect altered
registration instructions.

This is significant because a hypothetical perfect trace
of the *public* `GetPos`, `TestIsPlaying`, `SetParam`,
`Play` and `Stop` APIs can still miss a native
`active=1 -> active=0` transition originating from
the buffer-fill path. Thus even finding that **no game query
executed** in an early ESC window would not prove that
the physical ADPCM voice remained continuously alive.
The output-buffer fill is reached through native
audio processing and may occur while gameplay rendering or
script execution is paused. Its *actual occurrence during
this owner's specific first ESC* remains **unobserved**.

The two original buffer-fill wrappers **do use the known
driver critical-section lock** (unlike the previously found
GetPos and TestIsPlaying local chains). Hence a future
lifetime observer must take into account **all** original
active-flag writes and retirement/allocator routes, whether
under the lock or from a different context. Interposing on
the original mixer or acquiring its lock at an unsafe time
could itself alter audio timing.

### Reproducible SHA-pinned audit

`tools/audit_adpcm_buffer_fill.py` verifies **27 native
opcode windows, ten direct E8 call edges, three
conditional branch targets**, and the exact original DLL
SHA-256 and PE metadata, read-only without executing the DLL.
The separate `tests/test_adpcm_buffer_fill.py` provides
**22 synthetic regression tests**, covering all zero-flag
gates, alternate branches, missing lock release and
changed active-flag writer.

`tools/audit_adpcm_rel32_calls.py` now also scans the **complete
original raw `.text`** for direct calls to `0x18D0`
(two sites `0x344C`, `0x34F2`) and `0x1740`
(one site `0x1AD1`), raising the verified target list
from 11 to **13** and the known raw direct-call candidates
from 27 to **30**. Neither count is an exhaustive x86
call graph.

```sh
python3 tools/audit_adpcm_buffer_fill.py Rayman2Fix/dll/APMMXbvr.dll \
  --wavmwbvr Rayman2Fix/dll/WAVMWbvr.dll --json
python3 -m unittest discover -s tests -p 'test_adpcm_buffer_fill.py'
python3 -m unittest discover -s tests -p 'test_adpcm_rel32_calls.py'
```

**Decision: NO-GO for pre-driver Pause suppression remains.**
The ongoing research must distinguish normal ADPCM buffer
progression, conditional inactivation, record retirement
and in-place primary reuse; endpoint voice flags and public
export counters do not uniquely identify those events.
No live-driver query, dynamic hook, or runtime audio change
was introduced by this audit.

## 21. Passive global output-buffer positions across original ESC audio handoff

The exact original APMMX output-buffer wrappers independently
maintain two **32-bit global output-position counters**:

| Native output wrapper | Original read | Original write | Buffer-fill call |
| --- | --- | --- | --- |
| Mode 0 `0x3400` | `0x3431` reads RVA `0xE82A0` | `0x3465` writes RVA `0xE82A0` | `0x344C -> 0x18D0` |
| Mode 1 `0x34B0` | `0x34D3` reads RVA `0x24ED0` | `0x350B` writes RVA `0x24ED0` | `0x34F2 -> 0x18D0` |

These values change as the original native output
functions advance their buffer positions; they are **not**
song identifiers, per-voice generations, or guaranteed
monotonic counters across reinitialization.

The existing, separately opted-in ESC diagnostic
`proto_sample_adpcm_voice_after_dispatch()` now also
reads **these two global DWORD values** using the
already established, original-PE-verified APMMX module,
after all original audio dispatch decisions. The current
`RUNTIME-ESC-ADPCM-VOICE` and
`RUNTIME-ESC-ADPCM-RESUME` lines each add:

```text
mode0_output=<unsigned-32-bit> mode1_output=<unsigned-32-bit>
```

The same 16-opening and 16-closing diagnostic budgets
apply. No original function is invoked and no additional
original callback is hooked. The values are captured
only with an accepted original ADPCM voice snapshot;
`sampled=0` must remain **unknown**, not a zero
position. Output counters are independently updated
and the pair is **not an atomic mixer-state snapshot**.

The offline `tools/audit_esc_adpcm_evidence.py` parser
now compares such pairs only when a single opening is
followed by a source-paired, sampled closing, with
no intervening overlapping opening. A counter increase
is **evidence only that the sampled global output
position changed** during the interval, not that
the specific paused song advanced or that the
original callback ran on a particular thread.
A decrease may be wraparound or reinitialization;
the parser does not interpret it as negative
progress. Equal counters do not prove that no
buffer-fill callback ran.

Six synthetic regressions reject missing or corrupt
values, 32-bit overflow, non-unique opening pairs,
unpaired closing and counter wrap ambiguities.
A separate runtime source guard checks that the
new reads stay outside all native callbacks and
cannot authorize new driver commands.

**How this helps with the early Escape:** a new
Wine log from the latest experimental binary
can differentiate visibly advancing global ADPCM
output positions from stable sampled values
between the original script pause and script resume.
This is a **diagnostic step**, not a proof of physical
record continuity or an invitation to hook the
mixer. Existing owner logs from 15 November
predate these counters, so no claim about their
values in those sessions is made.

**No change to early-ESC music forwarding or any
production path.** The original
`ref_class=3` pending handoff remains the
directly observed reason the current coordinator
rejects compensation; hidden native lifetime
changes remain only *possible* alternate hazards,
not proven causes in that log.

## 22. Live owner log exposes overstrict closing observer gate (15 November, 15:13)

The owner activated the explicit experimental ESC launcher correctly:
`RUNTIME-ESC-PROTOTYPE: enabled=1` and
`RUNTIME-AUDIO-TRACE: enabled=1 schema=2 prototype=1`.
The uploaded 571-line log
`Rayman2Fix(20261115-151318).log` contains **12 source-confirmed
menu openings and 11 matching script-resume intervals**, one
unfinished opening when the log stops. Every opening produces a valid
original ADPCM voice snapshot (`sampled=1`, record slot 0,
`active=1`), but **all 11 original closing snapshots are
`sampled=0`, `transition_type=-1`, counters zero/UNKNOWN**.

### The missing closing measurements were caused by our diagnostic gate

The old closing observer (not the actual music policy) demanded
`resume_count==2 && unrelated_slots==0 && other_handle_count==0`.
The native script Resume normally performs **two
`ResumeTransitionSxd` callbacks for the same music transition**
*plus* **5–20 ordinary Sxd effect Resume callbacks**. Thus every
real 15:13 interval has `resume_count=2`,
`other_handle_count=0`, but **`unrelated_slots>0`**.
The source check rejected these otherwise qualified observations
without even reading the native transition object.

The **diagnostic-only** gate now permits unrelated ordinary
effect callbacks but still requires all of:

- matched same-thread, same-generation, non-overlapping original ESC
  Pause/Resume lifetime;
- a source-verified, stable sampled SXD resource: valid baseline,
  `sxd_changes=0` and `sxd_unknown=0`;
- **exactly two** original transition Resume callbacks with the
  **same** valid non-null handle and consistent callback counts;
- both transition Resumes correctly accounted for as forwarded or
  experimentally suppressed;
- zero exported Play/Stop deltas both between wrappers and within the
  original script Resume wrapper.

The snapshot remains a **render-thread-only, after-native-dispatch,
read-only** memory observation: no extra GetPos, TestIsPlaying,
Pause, Resume, Stop, DLL hook or actual music decision is introduced.
The original compensation policy is **unchanged**, and its strict
current-Theme and physical-generation precautions remain active.
The new diagnostic prints `gate=0|1` on
`RUNTIME-ESC-ADPCM-RESUME`:
`gate=0` means conservative observation filter refused;
`gate=1 sampled=0` means filter passed but original native
memory snapshot could not be verified. In both cases the
physical voice state remains **unknown**.

### What the 15:13 test actually proves

| Menu opening | Original Theme reference | ADPCM flag after original Pause/prototype dispatch | Source-paired close |
| --- | --- | --- | --- |
| 1–4 | class 3, not matching current player | `paused=1, active=1`, compensation rejected | 4 native Resume pairs, no compensation |
| 5 | class 1, matching | `paused=0, active=1`, compensation initially accepted | Native fallback after SXD 9:7→9:8 change |
| 6–7 | class 1, matching | `paused=0, active=1` | Two verified Resume suppressions each |
| 8 | class 1, matching | `paused=0, active=1` | Native fallback after SXD 9:8→9:9 change |
| 9–11 | class 1, matching | `paused=0, active=1` | Two verified Resume suppressions each |
| 12 | class 1, matching | `paused=0, active=1` | No matching close before log end; changing SXD triggered safety fallback |

The **seven** resolved WAVMW→APMMX callback pointers
(Stop/Pause/Resume/TestIsPlaying/GetPos/SetParam/SetParamTransition)
have the owner's original PE/export-entry fingerprints before and
after Theme synchronization, but their existence **does not prove**
the game invoked a particular getter or driver retirement function.

At each opening `mode0_output == mode1_output`, and the two
global output-position counters increase **between menu openings**.
Since the old close snapshots were not taken, these values
**cannot establish progress during an open menu**. They
are not song-specific or physical voice epoch counters.

Eleven source-paired closing lifetimes show zero exported
Play/Stop deltas, but internal GetPos, TestIsPlaying, output-buffer
progression and reinitialization can operate without exported Stop.
Consequently, the log neither proves native driver retirement
nor continuous physical voice generation.

The replacement observer should now be eligible on **nine**
of the eleven completed intervals from this exact log's counters
and SXD stability conditions, if the updated binary is rerun;
the two stream-switch intervals `9:7→9:8` and
`9:8→9:9` remain correctly filtered. This is a prediction
based on source and recorded flags, **not a measured post-update
success**. No actual closed-interval buffer-counter deltas
can be asserted until a new Wine run of the updated prototype.

Static source guards in `tests/test_audio_trace_guards.py`
and four synthetic classifier regressions in
`tests/test_esc_adpcm_evidence.py` confirm that incidental
effect resumes no longer veto a legitimate observation and
that a failed/blocked snapshot is never mistaken for missing
music or driver retirement. PR #10 remains **DRAFT**.

## 23. Owner 15:34 Wine verification: original ADPCM output progression across ESC (2026-11-15)

The owner ran the **corrected, explicitly enabled**
experimental launcher and supplied
`Rayman2Fix(20261115-153430).log` (417 lines).
`RUNTIME-ESC-PROTOTYPE: enabled=1`,
`RUNTIME-AUDIO-TRACE: enabled=1 schema=2 prototype=1`.
This is a real Wine run, **not** synthetic CI evidence.

**The 15:13 instrumentation defect is fixed in practice:**
there are six original source-qualified ESC Pause openings,
five same-thread/generation-paired script Resume closings, and
three closing samples with **`gate=1 sampled=1`**.
The two other closings are deliberately excluded with
`gate=0 sampled=0` after detected original SXD resource
changes (`9:7→9:8` and `9:8→9:9`).
The sixth opening had no matching original script Resume
before the journal ended and must **not** be treated as a
completed menu interval.

The first three openings are the known
**pending Theme reference** (`native_ref_class=3`,
`native_ref_match=0`), and the original compensation
is rejected. Each opening reads native APMMX
`voice_index=0, paused=1, active=1` after the
original Pause. Each paired closing reads the
original music voice as `voice_index=0,
paused=0, active=1` **after** the original
native Resume; no synthetic music resume was
issued, and the three reported intervals have
zero **exported** Play/Stop calls. The recorded
inner resource `9:6` is unchanged in these
bounded paired observations.

Most importantly, the two original **global**
APMMX buffer-output positions now have valid
measurements **within** the same menus:

| ESC interval (ms) | Elapsed | Mode 0 open → close | Mode 1 open → close | Signed observed change |
| --- | ---: | ---: | ---: | ---: |
| 36718–43051 | 6333 ms | 762740 → 902660 | Same | +139920 |
| 44284–52501 | 8217 ms | 929940 → 1111440 | Same | +181500 |
| 53535–62868 | 9333 ms | 1134320 → 1340460 | Same | +206140 |

Across these intervals, the average rise is **≈22089
counter units/second** (individual intervals ≈22094,
22088, 22087). Both global counters were exactly equal
at each measured endpoint and increased by equal
amounts. This is **real observed native output-position
progression across each early ESC interval**, even though
the voice was paused at opening and the game's Theme
handoff was unresolved.

**Crucial interpretation:** The counters refer to
the **global output-buffer mixer**, not the
particular music stream. These results
show that original native audio output positions
continue advancing across the early ESC menus;
they do **not** show that the music played or that
the same physical musical voice remained
continuously allocated. The opening and closing
flags are post-wrapper endpoint samples, **not**
continuous observation of paused/active state
between them. No WAVMW query invocation,
internal APMMX record retire, audio thread identity,
or physical voice epoch is proved. The measured
rate is a counter rate, **not independently
proven to be a 22050 Hz music sample rate**.

The later matching original Theme class-1 openings
have a post-compensation `paused=0,active=1`
snapshot, but their native SXD changes trigger
the existing **fail-closed** compensation policy
(`driver-command-rejected` phase 6), so native
script Resume remains forwarded. The final
incomplete opening similarly triggers
original-native fallback; do not infer it
closed from the session ending.

A new **synthetic** regression in
`tests/test_esc_adpcm_evidence.py`
reconstructs six abstract opening intervals,
three valid counter deltas, two correctly
refused stream-switch closings, and one
unpaired opening. It asserts that the
log classifier never promotes global counter
movement into `proves_music_voice_progressed`
or `can_prove_continuous_physical_voice_generation`.

**Next experiment:** Establish a reliable
original Theme handoff/generation identification
during the first ESC, rather than suppressing
two original native Pauses solely because the
mixer output counters advanced. Until all
native record retire/reinitialize routes
and the pending player reference are
safely accounted for, the original
`native_ref_class=3` rejection remains
correct. No changes to driver forwarding,
music compensation authorization, or
runtime hooks are justified by this log.
PR #10 remains **draft**.
