# Experimental in-process Options/Resolution text route trace

Only the separately built graphics-menu research launcher adds this trace.
The standard main branch does not compile it.

Approved deprotected game x86 PE, image size 0x001D7000 and 6 sections,
is checked before a 17-byte verification of the original text resolver
at VA 0x494C70. A seven-byte, two-instruction detour is installed only
after the game's already-authenticated camera hook is successfully loaded.
The original result is forwarded completely unchanged; the diagnostic
counts text handles 370-373 and 379-385, with a separate count for
AI text expressions returning via VA 0x4829EE. This is a native code
interception, so there remains experimental compatibility/crash risk.
No video change, scene data write or string replacement is performed.

The observer emits cumulative counts in Rayman2Fix.log at most every
500 ms (120 reports maximum / 300 seconds). Brightness label 385 is
kept independent from resolution labels 379-384.

Use Debug.Log=1. Idle in main menu; enter Options then Resolution and
wait five seconds without changing any option; leave and repeat.
Visit brightness separately. Supply the runtime log and look for
RUNTIME-MENU-TEXT: status=installed followed by t_ms samples.
These samples only prove text lookup paths, not yet specific submenu
identity. Native VID_SelectResolution and brightness calls remain
untouched. Work on the actual read-only in-game screen is still pending.

## Wine result 2026-11-21

See `docs/native-menu-text-wine-result.md`.
The original text resolver and AI lookup route are confirmed, **but all
resolution and brightness labels are queried as one synchronized group**.
Label counts are not a safe submenu predicate. Further text-only tracing
cannot identify the active page; prioritize the AI controller's live
behavior and preserve native resolution/brightness behavior unchanged.
