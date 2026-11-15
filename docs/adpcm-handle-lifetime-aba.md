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
now verifies the original `rGetPosSxdADPCM` export, **12 exact
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
