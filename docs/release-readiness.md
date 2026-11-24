# Rayman2Fix — release readiness

Updated 24 November 2026. CI artifacts are development builds, not a
public release. The owner has requested licensing first, the targeted
Jail_20 regression test immediately afterward, and SDL3 thereafter.

## Completed engineering work

- Audio crackling mitigation and ESC music: #39, #43, #47.
- Native Learn_10 F248 skip: #35, #3.
- Read-only VIDEO pages: #53, #49; merged as 9a2f7ab9.
- Single EXE, verified GameFrame deployment, and initial log retention.

## Remaining priorities (in order)

1. Integrate GPL-3.0-only licensing, including the full root LICENSE and
   embedded docs/LICENSE. Preserve GameFrame's separate Apache 2.0
   license. Verify Windows x86 packaging and Linux/Windows tests.
2. #45 — confirm very early Jail_20 input on native Windows, including
   the queued-before-Playing case if reproducible. #46 is already merged;
   normal later skips have passed. Do not introduce a speculative retry
   or fixed delay solely to close the issue.
3. #40 / #54 — implement SDL3 gamepad support, verified x86 dependency and
   license, native keyboard coexistence, focus/hotplug, menu/gameplay
   mappings and real Windows/Wine validation.
4. Review supported CD editions, installation/launch, source availability,
   notices and release readiness. No commercial assets are distributed;
   the approved local executable requirement remains documented.

No public release is authorized by the license integration alone.
