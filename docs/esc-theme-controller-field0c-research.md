# Rayman 2 retail CD: Theme-controller +0x0C investigation

**Status:** original field semantics unverified; research-only. The feature
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

## What is still missing

The attached `WAVMWbvr.dll` and `APMMXbvr.dll` are
byte-verified 1999 audio backends. Their exact ADPCM
state semantics are known, but neither contains the
**game executable's own Theme player/controller logic**.
No user-supplied unprotected retail game executable is
currently mounted in the working container. Third-party
GOG executables, leaked code or mismatching engine
versions are not substitutes for that exact implementation.

To identify `controller+0x0C` rigorously, the next
step is to discover, **in the actual loaded retail-CD
game code**, the machine instructions referencing it
and then distinguish **readers, writers, state updates,
and reset conditions** with static disassembly.
An address literal appearing in a machine-code byte
window is only a *candidate*, not proof of an
instruction boundary or of a write.

## Standard, one-time read-only source-location probe

The branch now includes a purely observational function
`proto_audit_controller_code_references()`. It runs
**once**, only if `MusicInPauseMenu=1`, logging is enabled,
and the existing camera hook has already positively
verified the original retail-CD image. It is *not*
a game hook or callback; it performs no writes, page
protection changes, or audio calls.

The helper validates the original PE32/x86 timestamp
and image size, reads only the approved original
`.text` RVA range `0x1000..0x9B1D0` once,
and searches for the relocated 32-bit address
literals for controller `+0x00`, `+0x0C`,
and `+0x14`. Up to **12** occurrences per
field are logged with a 10-byte left and right
machine-code context; the four address bytes are
masked as `????????` to avoid printing raw pointers.
The log prefixes are:

```text
RUNTIME-ESC-CONTROLLER-XREF: field=state_0c disp_rva=... context_masked=... status=literal-candidate-only
RUNTIME-ESC-CONTROLLER-XREF-SUMMARY: field=state_0c matches=N shown=M truncated=0|1
```

Actual output may have **zero matches** if the original
engine accesses the field through `[register+0x0C]`
rather than a direct absolute address. Conversely
a positive literal match might be code data,
an immediate operand, or an unaligned byte sequence.
Do **not** infer native field semantics, patch
the original controller, or loosen the strict Theme-owner
gate based on this scan alone.

A future offline study will decode candidate x86
instructions, identify the containing functions
and their call paths, then check original source
and lifecycle guards. The standalone xref trace
is not sufficient to choose a safe early-ESC
music intervention.

## Safety invariants

- Original native paired ESC Pauses and Resumes
  remain intact except for already validated existing
  post-verdict compensation of matched Theme ownership.
- Strict `ref_class=1` controller/Theme match remains
  necessary for the existing opt-in compensated path.
- `phase=6` quarantine and original native passthrough
  on inner SXD change remain mandatory.
- The same ADPCM pool ordinal across menus is **not**
  a durable music/voice identity; Stop invalidates
  and compacts records.
- No queued pre-driver Pause or late Resume replay
  may cross a Stop, focus transition or new
  Theme generation.
