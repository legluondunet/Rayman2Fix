# Rayman2Fix — release readiness

Updated 25 November 2026. CI artifacts are development builds, not a
public release.

## Completed engineering work

- GPL-3.0-only licensing, retaining GameFrame Apache 2.0 and SDL3 zlib notices.
- Audio crackling mitigation and ESC music: #39, #43, #47.
- Native Learn_10 F248 skip: #35, #3.
- Read-only VIDEO pages: #53, #49; merged as 9a2f7ab9.
- Single EXE, verified dependency deployment and initial log retention.
- SDL3 integration and enchanted controls menu: #56, merged as 5ba2a4c4.
  CI #2001 passed Linux and Windows x86; owner approved build 4467b514
  under Wine with automatic controller handovers and physical indicators.

## Remaining priorities

1. #45 — investigate very early Jail_20 input under Wine if reproducible,
   including the queued-before-Playing case. #46 is already merged;
   normal later skips have passed. Do not add speculative delays.
2. Targeted controller tests: jump/surface and target/dive in an actual
   swimming scene, and world-knowledge versus Murfy in eligible contexts.
   Keep the pending markers until the actual native effect is confirmed.
   Validate these behaviors under Wine with the available controllers.
3. Review supported CD editions, installation/launch, source availability,
   notices and release readiness. No commercial assets are distributed;
   the approved local executable requirement remains documented.

## Platform validation policy

The maintainer has no native Windows computer available. Native Windows
hardware/gameplay testing is deferred to potential community testers and
is not a release prerequisite. Continue Windows x86 automated CI tests;
report native Windows gameplay coverage as incomplete, not validated.

No public release is authorized by these engineering milestones alone.
