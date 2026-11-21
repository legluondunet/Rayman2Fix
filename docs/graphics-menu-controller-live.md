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
