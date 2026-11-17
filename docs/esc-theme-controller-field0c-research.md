# Rayman 2 retail CD: Theme-controller +0x0C investigation

**Status:** the original pending-Theme switch protocol is statically verified;
its relationship to the physical ADPCM voice still needs runtime confirmation.
Research-only. The feature
`[Features] MusicInPauseMenu` and all native Pause/Resume/Stop calls
are unchanged by this investigation. PR #10 remains draft.

## Evidence already observed (14–15 November 2026)

The approved retail-CD game has a native global Theme player starting
at original VA `0x4B95B8` (loaded PE base `0x400000` plus RVA
`0xB95B8`). The runtime's read-only 24-byte controller sample records:

| Offset from player | Original VA | Logged meaning |
| --- | --- | --- |
| `+0x00` | `0x4B95B8` | active flag, observed `1` for active player |
| `+0x0C` | `0x4B95C4` | **unknown** native controller field, observed `0` or `1` |
| `+0x14` | `0x4B95CC` | current native Theme **descriptor reference** (pointer; not a bank/index numeric key) |

The original direct/indirect game source and registered Theme descriptors
are independently verified by the existing runtime. We are **not**
claiming that the middle field is a transition counter, crossfade
duration, state enum, or other named property.

The most informative owner trace is
`Rayman2Fix(20261115-075536).log`:

1. `t=39496ms`: native player ref matches active Theme `9:12`,
   `controller_field0c=0`; inner SXD is in bank 239.
2. `t=40346ms`: two Theme record candidates overlap and
   `controller_field0c=1`; the unique active owner is temporarily
   ambiguous.
3. `t=40396ms`: one active outer Theme `9:5` / inner SXD `9:6`,
   **but native player pointer still designates the previously active,
   readable `9:12` Theme descriptor** (`ref_class=3`).
   The controller field remains `1`.
4. First three ESC openings at 41079, 52046, 57679ms:
   identical source-qualified pair of ADPCM type-4 native Pause
   calls. Each live APMMX record validates with
   `paused=1, other_gate=0` after the strict owner
   mismatch rejects synthetic compensation.
5. `t=65846ms`: `controller_field0c=0` and original
   Theme-player reference now matches active Theme `9:5`
   (`ref_class=1`), inner SXD `9:7`. Later accepted
   ESC compensation results in valid ADPCM voice
   `paused=0, other_gate=0` after native dispatch.

For a cross-session sanity check, all **10 locally available**
owner logs that contain the Theme watcher were inspected:
`20261114-172750`, `180950`, `184139`, `185515`,
`191013`, `192852`, `201946`, `211741`,
`213131`, and `20261115-075536`. Each has one
change-triggered sample `field0c=1, ref_class=-1`
during handoff ambiguity and one sample
`field0c=1, ref_class=3, ref_match=0`
during the alternative-Theme reference. No sample of
`field0c=0, ref_class=3` was found in this set.

This repeats **one owner/test scenario**, not ten
independent sound-engine conditions. Absence of a
change-triggered sample also does not prove the
absence of short-lived intermediate states.

## The exact original retail executable has been recovered

The owner's already-provided Library file `Rayman2(3).exe` was
materialized **read-only** and verified:

| Property | Exact observed value |
| --- | --- |
| Size | 676,352 bytes |
| SHA-256 | `7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3` |
| PE | Intel x86 PE32, image base `0x00400000` |
| PE timestamp | `0x3807B68B` |
| Image size | `0x117000` |
| Executable .text | VA `0x401000..0x49B1D0`, RVA `0x1000..0x9B1D0` |

The SHA-256 is **identical to the retail-CD executable already approved**
in `src/runtime/runtime.cpp`. Two other user Library files
(`Rayman2.exe` and `rayman2(4).exe`, each 1,468,928 bytes)
have an unrelated SHA-256
`9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`;
**their code and offsets were not used**. The exact approved
file was disassembled statically with `objdump -d -Mintel`.
No game's executable was run, changed, or committed to GitHub.

## Decisive native control-flow evidence

The pending-transition structure and the actual meaning of
`controller_field0c` can now be derived from original instructions,
without guessing from the GOG edition.

| CD native VA | Instruction/operation | Consequence |
| --- | --- | --- |
| `0x459670` | initial setup assigns `[controller+0x14]=incoming_theme` and zeros `+0x0C`, `+0x08`, `+0x10`, `+0x04` | First Theme becomes current immediately |
| `0x4595F6` | `mov DWORD PTR [ecx+0x0c],eax` where `eax=1` | Set **pending Theme switch** flag |
| `0x4595FC` | `mov DWORD PTR [ecx+0x44],edx` where `edx` is the incoming Theme descriptor | Save the **next Theme** pointer in a separate pending slot |
| `0x459467` | load `[ecx+0x0c]`; if nonzero, inspect pending target `[ecx+0x44]` | Detect a pending switch during the original player's progress/update |
| `0x459476` | `mov DWORD PTR [ecx+0x0c],ebx` where `ebx=0` | Clear pending flag |
| `0x45948E` | `mov DWORD PTR [ecx+0x14],eax` where `eax=[controller+0x44]` | **Commit the pending descriptor as the current Theme** |
| `0x4596E0 → 0x459701` | periodic/player callback invokes `0x459460` | One confirmed caller of the pending-switch completion path |
| `0x45996E` | load `[0x4B95C4]` into `ecx` | A Theme-update entry explicitly inspects the pending flag |
| `0x45997B` | compare incoming descriptor with current pointer `[0x4B95CC]`, with branches also reading the pending flag | The pending state influences whether another change is processed |
| `0x4597C2` | read `[0x4B95C4]` for native querying | The same state is externally readable |

The strong conclusion is that **`controller+0x0C` flags a pending
Theme switch**. A new Theme descriptor can be stored in
`controller+0x44` **before** the older descriptor in
`controller+0x14` is replaced, and `0x459460`
handles eventual promotion. This directly explains the
previously mysterious 9:12/9:5 reference discrepancy.
The exact duration/timing and the device-level semantics
of the two simultaneous Theme resources are **not**
established by the code alone.

The original code accesses `0x4B95C4` as a global
32-bit address literal at only two confirmed sites
(`RVA 0x597C3`, `0x59970` are displacement
byte positions), both **reads**. The writes
are indirect via the controller structure in
`0x4595D0` and `0x459460`. This is why
searching only for literal bytes cannot fully
trace the state lifecycle.

## Minimal next live proof: pending target actually equals active Theme

The opt-in standard runtime now **passively** compares the pending
descriptor at original `[controller+0x44]` against the **unique
already-validated active outer type-11 Theme descriptor**
from the game's 64-slot native sound table.

The new fields are:

- `pending_switch_state=0|1|-1`: original `+0x0C`
  zero, pending, or unavailable/unrecognised.
- `pending_target_matches_outer=0|1|-1`: meaningful
  **only when** the pending state is 1 and the target
  is a plausible pointer. It compares the pointer
  itself against the unique active type-11 outer
  descriptor. No bank-only or first-candidate guess.
- `pending_target_differs_current=0|1|-1`: compares
  the pending descriptor to the older `+0x14`
  current descriptor in the same read-only snapshot.

These fields are printed by the **existing**
`RUNTIME-ESC-THEME-WATCH` (on sampled state changes)
and `RUNTIME-ESC-OWNER-DIAG` (after a source-specific menu
decision). No new native hook or audio calls have
been introduced; no pointer or path is printed.
If there is no **unique type-11 active Theme**,
the pending-target conclusion remains unknown.
The samples are **not atomic** with the original
game's asynchronous Theme transition and must never
be treated as an audio authorization credential.

The next owner log can answer the specific remaining
question: while `ref_class=3` and the old current
Theme is 9:12, is the pending pointer **exactly**
the validated 9:5 descriptor (and different from
the current pointer)? The expected values in that
hypothesis are `pending_switch_state=1`,
`pending_target_matches_outer=1`,
`pending_target_differs_current=1`.
**These are predictions, not yet runtime observations.**

## Removed obsolete code scan

We temporarily prepared a startup read-only literal-xref
scanner while the original executable was unavailable.
Now that the exact SHA-256-matched CD executable was
found and disassembled directly, that scanner was
removed from the runtime, with its related source
guard replaced by a stricter pending-pointer test.
No 600 KiB code scan is shipped in the diagnostic
build.

## Safety invariants

- Original native paired ESC Pauses and Resumes
  remain intact except for already validated existing
  post-verdict compensation of matched Theme ownership.
- The verified `ref_class=1` live Theme pointer equality
  remains mandatory for current compensation in
  `proto_identity`; **pending-target matching does not
  relax this condition**.
- `phase=6` quarantine and original native passthrough
  on inner SXD change remain mandatory.
- The same ADPCM pool ordinal across menus is **not**
  a durable music/voice identity: Stop invalidates
  and compacts records.
- No queued pre-driver Pause or late Resume replay
  may cross a Stop, focus transition or new
  Theme generation.

## Owner follow-up, 15 November 2026 at 08:40 — pending descriptor hypothesis confirmed

The owner ran the **same original SHA-approved executable** with
the passive pending-Theme diagnostics; submitted
`Rayman2Fix(20261115-084027).log` (157 lines).
This is the *first direct original-engine runtime confirmation*
of the two-pointer disassembly, rather than a code-only hypothesis.

The exact source log reports:

| Event | Time (ms) | Original controller and sound evidence |
| --- | ---: | --- |
| Previous Theme active | 9,106–33,067 | current reference class 1, Theme `9:12`; `controller_field0c=0` |
| Overlapping transition | 33,867 | `controller_field0c=1`, two outer Theme candidates, so target still **unverified** |
| New Theme becomes uniquely active | 33,917 | current reference still the readable old `9:12` (`ref_class=3`); active Theme `9:5`, inner `9:6`; **`pending_switch_state=1 pending_target_matches_outer=1 pending_target_differs_current=1`** |
| First early ESC | 34,367 | two same-object native `PauseTransitionSxd` callbacks, ADPCM type 4, depth `1→2`; valid original ADPCM record `paused=1 other_gate=0`; no compensation |
| Owner diagnosis of this rejected opening | approximately 34,368 | original live pointer class 3, **pending state 1 and target pointer equal to active outer Theme**; native owner/driver and bank `9:5/9:6` verified |
| Second early ESC | 37,350 | same source-qualified native paired Pauses; ADPCM `paused=1`; rejected; **no new direct owner diagnosis was logged** |
| Third early ESC | 42,734 | same source-qualified native paired Pauses; ADPCM `paused=1`; rejected; **no new direct owner diagnosis was logged** |
| Pending transition completed | 47,017 | `controller_field0c=0`, current reference now exact active Theme `9:5`, inner `9:7` |
| Later ESC | 47,600 | two native Pauses, followed by existing two synthetic Resumes; ADPCM post-dispatch `paused=0 other_gate=0` |
| Safety fallback | 51,817 | active inner SXD changes to `9:8` while menu=1, triggering existing `phase=6`; no source-qualified close before this log ends |

**Closing pairs:** the first three source-qualified ESC
Resume wrappers occurred at **35,817**, **41,733**
and **46,067 ms**, each with exactly two native
Resume callbacks fully forwarded and an original
script depth `2→1`; all three lifetime reports have
`paired=1 overlap=0` with unchanged logical inner
SXD `9:6`. There is **no matching script Resume** for
the fourth, compensated menu. Do not count it
as a completed ESC cycle or conclude it was stuck.

**Precise conclusion:** the original controller's
`+0x44` pointer identifies the **already active, unique**
Theme `9:5` while `+0x14` still holds the old `9:12`.
This is proven both by hash-pinned retail-CD
disassembly and by two independent read-only
runtime checks near the first menu (the watcher
at 33,917 ms and the post-verdict diagnosis near
34,368 ms). It explains the strict
`native_theme_player_ref_matches_outer==1`
rejection at early ESC.

**Evidence boundary:** the source log has only **one
direct owner diagnosis for an early rejected ESC**;
subsequent rejections confirm the ADPCM pause gate
but are not independent direct observations of
`controller+0x44`. The previous change-triggered
watcher snapshot cannot prove that its pointer
remained stable in between. The ADPCM voice index
`0` is temporary and not a music-generation
identifier. Zero observed global Play/Stop counts
during the paired windows do not prove there
were no native internal stream transitions.

**Implementation/next gate:** update the offline
`tools/analyze_esc_adpcm_trace.py` to distinguish
`direct_pending_target_confirmed` from
`last_watcher_pending_target_match_at_ms`.
Never copy a one-time owner diagnosis across
subsequent ESC openings; reject contradictory
and duplicate reports. Add synthetic tests
and keep `audio_intervention_authorized=false`
by design.

**Next engineering question:** what exactly
happens to the **same ADPCM handle** when the
original player transitions from pending
`9:5` to current `9:5`, while the ESC menu
is still active? The engine may update or retire
a voice asynchronously (original APMMX Stop can
invalidate and compact its record pool).
A narrowly gated *experimental* early
compensation might be feasible, but this log
does not yet establish safe lifetime handling,
per-voice exclusivity, focus semantics and
matched Resume suppression. Do not relax the
production pointer-equality gate, or change
native calls, from this evidence alone.

## Further offline original-code/lifetime research — 15 November, 08:53–09:04 UTC

**All evidence derives from the exact owner's 676,352-byte
SHA-256-approved `Rayman2(3).exe`, SHA-256
`7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`,
and the previously supplied original sound-driver DLLs. No proprietary bytes
were committed to GitHub or executed outside the game.**

### Newly verified: a second path can REWRITE the pending target

The original `0x4595D0` function sets a pending new
Theme when the engine is explicitly asked to switch.
However, static x86 disassembly found **another
independent write path** inside original periodic
update `0x459460`:

```asm
0x459521  cmp dword ptr [esi+0x20], 0
0x459526  mov [ecx+0x10], eax      ; eax=1
0x459529  mov [ecx+0x0C], eax      ; queue another pending switch
0x45952C  mov edx, [esi+0x20]
0x45952F  mov edx, [edx]
0x459531  mov [ecx+0x3C], edx
0x459534  mov edx, [0x4B949C]     ; *separate next-Theme source*
0x45953A  mov [ecx+0x44], edx      ; REWRITE pending descriptor
```

This branch is reached from the original Theme
progression code after a threshold/repeat comparison
on the Theme's internal `+0x1C`, `+0x28`
and controller counters `+0x24`, `+0x38`.
The code is a **new-target queue on progress**,
not proof that this path was actually executed
during the user's specific ESC menu.

The earlier original promotion code establishes
another subtle ordering:

```asm
0x459467  mov eax, [ecx+0x0C]     ; pending flag
0x459473  mov eax, [ecx+0x44]     ; pending target
0x459476  mov [ecx+0x0C], ebx     ; ebx=0, clear flag FIRST
...
0x45948E  mov [ecx+0x14], eax     ; promote pending target LATER
```

These are **two separate writes**. Therefore
`pending_switch_state=0` seen by a read-only
observer does not atomically prove the current
`+0x14` pointer has already been promoted.
The existing diagnostic only observes; it does
not acquire original game-player synchronization,
and must not authorize a synthetic driver call.
Furthermore, a one-off `+0x44` match to the
active 9:5 Theme does not prove the target
cannot be replaced *during* a menu. These
are concrete lifetime/TOCTOU hazards, not
merely hypothetical unknowns.

15 original x86 instruction site fingerprints
(including both distinct writes to `+0x44`,
both writes to the `+0x0C` pending flag,
promotion, and the periodic updater's
call) are recorded in new, stdlib-only
`tools/audit_original_cd_theme_switch.py`.
It **refuses to interpret ANY of these RVAs**
until the full input SHA-256 and PE
timestamp/image-base/image-size match the owner's
retail-CD original, and then verifies the
actual original instruction bytes. Synthetic
tests `tests/test_audit_original_cd_theme_switch.py`
reject mismatched hashes, PE metadata, and opcodes.
No executable patches, new audio callbacks,
loaded DLLs or configuration changes are involved.

### New empirical finding: ~4 seconds of *outside-menu* handoff time

The locally available original Rayman2Fix
owner logs (14–15 November) were independently
reexamined, **accepting only an original source-
identified pair of ESC native Pauses, a matching
source-specific two-Resume wrapper, and a
`paired=1 overlap=0` lifetime record for
every interval lying between watcher samples
`9:5 ref_class=3 field0c=1` and
`9:5 ref_class=1 field0c=0`**.
Missing or overlapping closes exclude an entire
run. Only seven runs qualified:

| Owner log timestamp | Pending wall time (s) | Fully paired ESC script windows (s) | Outside ESC (s) |
| --- | ---: | ---: | ---: |
| 20261114-191013 | 27.156 | 23.032 | **4.124** |
| 20261114-192852 | 18.438 | 14.312 | **4.126** |
| 20261114-201946 | 28.250 | 24.187 | **4.063** |
| 20261114-211741 | 23.688 | 19.642 | **4.046** |
| 20261114-213131 | 4.015 | 0 | **4.015** |
| 20261115-075536 | 25.450 | 21.584 | **3.866** |
| 20261115-084027 | 13.100 | 9.166 | **3.934** |

Mean **4.025 seconds**, median **4.046 seconds**,
range **3.866–4.126 seconds**, sample standard
deviation ~**0.096 seconds**. These timings
were derived from original log event timestamps,
not a claimed audible listening measurement.
Other older runs were **excluded** because a
required complete source-specific Resume/lifetime
pair was absent or source pairing was ambiguous;
they must not influence the estimate.

The regularity across an approximately
**4–28 second wall-clock span** supports the
hypothesis that the native Theme transition
depends mainly on *progress that is suspended
while the Escape menu has paused ADPCM music*.
The already-verified original `APMMXbvr.dll`
mixer skips the actual paused ADPCM record when
its `+0x1C` flag is 1; this provides a
mechanistically consistent explanation.

**Limitations:** the native Theme watcher
samples periodically (typically 50ms), and
source-verified ESC script intervals are a
proxy for physical ADPCM pause time, not direct
sampling of the stream's decode clock. We
cannot yet claim the original code has a
wall-clock-free 4,000ms timer, nor rule out
other internal clock sources or overlapping
subresource changes. Most logs repeat the
same early-level scenario under the same
install, so this is not cross-game validation.

For repeatability, `tools/analyze_theme_handoff_timing.py`
now performs exactly this timing subtraction
from original source-paired log events and
**fails closed** on unmatched Pauses,
Resumes, overlaps, missing previous Theme,
or unclosed windows. It reports its uncertainty
and never authorizes a native command.
A synthetic unittest suite
`tests/test_theme_handoff_timing.py`
checks ordinary, long, overlapping and
incomplete scenarios. The project CI invokes
both this suite and the exact retail-CD
opcode audit suite.

### Consequence for proposed early-ESC bridge

An early post-native synthetic Resume on a
verified pending target would no longer just
keep the *existing* 9:6 ADPCM record audible;
it could permit the original Theme controller
to **progress, promote 9:5, replace its
pending descriptor again, and advance the
inner 9:6→9:7 resource while the ESC menu
remains open**. That exact sequence has a
precedent: the owner logs already observed
progression `9:7→9:8` and `phase=6` while
a compensated menu remained open.

The safe intervention would need to recognize
record retirement/reallocation and script
Resume/focus ownership *across* that promotion.
The original ADPCM Stop invalidates the
old handle and can compact the voice pool.
Native controller `+0x44` equality and
`voice_index=0` are demonstrably insufficient
as durable stream-generation credentials.

**Decision:** maintain the production
`ref_class=1` gate and `phase=6`
quarantine. No early synthetic Resume,
native Pause suppression, deferred Resume
replay or original DLL patch is justified
by this timing correlation alone.
