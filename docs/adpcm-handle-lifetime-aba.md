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

The SHA-pinned tool checks **16 original instruction fingerprints**,
demonstrates the two ABA pathways using explicitly **synthetic**
track identities, and never loads the original DLL as executable code.
These are proofs of *possible* aliases from the original code, **not
claims that a given alias occurred in the user's latest runtime log**.
