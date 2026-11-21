# Research-only native video menu AI controller telemetry

This iteration replaces the earlier menu text resolver detour with a
purely read-only in-memory **model and active behavior** observer.
It is included ONLY in the separate Win32
`Rayman2Fix-Windows-x86-resolution-menu-trace` artifact;
main and the normal release are unchanged.

## What is verified

- The launcher continues to hash-authenticate the approved deprotected
  game. The observer independently checks the PE32 fingerprint
  (timestamp, architecture, six sections, entry, SizeOfImage).
- A bounded 160-MiB committed private-memory scan tries to locate a
  20-byte AIModel header with the **real script signatures** observed in
  the maintainer's hashed Menu.sna. No fixed scene VA is trusted; the
  previous 0x079209A8 is only a fast candidate checked against all
  signatures.
- Matching is strict: 27 normal and 7 reflex behaviors, three separate
  `VID_SelectResolution` calls (normal 5, script 3), `VID_SaveCurrentResolution`,
  the extra `VID_SelectResolution` (normal 19, script 2), and the
  independent `VID_ChangeBrightness` (reflex 5, script 2).
- The related Mind is accepted only if its normal and reflex
  Intelligence point back to the expected two model substructures.
  The current behavior pointer is resolved against the validated
  12-byte entry arrays. Normal 5 and reflex 5 are NEVER conflated.
- ReadProcessMemory and VirtualQuery are used only for observation;
  **no native code detours, memory writes, script patches, input
  injection, resolution changes, or drawing modifications** are added.
- At most 18 scans, 160 MiB per scan, every 5 seconds while a
  controller has not been found. Once found, snapshots are sampled
  every 200 ms; changes and bounded 5-s heartbeats logged for at
  most 180 seconds. No raw pointers or actor strings are logged.
- If more than one model matches or the Mind/Intelligence is not
  verified, the monitor fails closed with status and no inferred
  submenu.

## Diagnostic target

Expected log lines (Log=1):

    RUNTIME-MENU-AI: status=started read-only=1
    RUNTIME-MENU-AI: status=controller-found ... mind=validated
    RUNTIME-MENU-AI: t_ms=... normal=5 reflex=... level_hint=...
    RUNTIME-VIDEO-MENU-TRACE: t_ms=...

The current behavior identity does **not** automatically imply that
the Resolution submenu is open. The Wine test needs a correlation across
several transitions (Options > Resolution, back, visit brightness,
back), without deliberately applying or saving a new mode.

If no verified controller is found, the memory layout might diverge
from the static SNA or the matching level not have been loaded. Inspect
the scan diagnostics before adding more native hooks.

## First verified Wine result and next scalar probe (21 November 2026)

Session `Rayman2Fix(20261121-130111).log`, 27,225 bytes, SHA-256
`f618d721890efa56b5cc3e3457ff0dc1177b0cb0dc340db8a1bb4d896abd12f9`.
The controller was **found on scan 2**, uniquely, with verified
Mind and 27/7 behaviors. Normal 5 appeared twice (8.253, 14.858 s),
reflex 5 once (10.257 s), and normal 19 did not appear. Original mode
remained 1024x768x16. No game crash is proved or ruled out by the log.
The data proves live controller identity; the specific screen associated
with each behavior still needs confirmation.

Additional static validation of the owner's hash-locked Menu.sna reveals:
controller Mind `0x07920990` → DsgMem `0x07921430`, with a DsgVar
declaration of **90 entries**. `V17` is an integer type 5 at offset
`0x144` in its live value buffer; the normal-5 script tests V17 against
constants 1, 2, 3, 4 before selection actions. `V51` is a byte kind 0
at `0x4A5` and is read by the same script. This does **not** yet establish
whether V17 represents a highlighted row or an action parameter.

The next diagnostic checks the live DsgVar chain and each field's exact
type/offset before reading. It adds `v17_valid, v17, v51_valid, v51`
to the existing state-change log and triggers logging on their changes.
Invalid pointers, changed schemas and non-integer fields cause missing
values rather than speculative reads. No write, hook or video change.
