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
