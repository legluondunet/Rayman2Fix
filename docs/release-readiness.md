# Rayman2Fix — release readiness

Updated 27 November 2026. CI artifacts are development builds, not a
public release.

## Completed engineering work

- GPL-3.0-only licensing, retaining GameFrame Apache 2.0 and SDL3 zlib notices.
- Audio crackling mitigation and ESC music: #39, #43, #47.
- Native Learn_10 F248 skip: #35, #3.
- Read-only VIDEO pages: #53, #49; merged as 9a2f7ab9.
- Single EXE, verified dependency deployment and initial log retention.
- SDL3 dependency audit and ABI contract tests: #58, merged as ea077278; CI #2006 passed.
- SDL3 integration and enchanted controls menu: #56, merged as 5ba2a4c4.
  CI #2001 passed Linux and Windows x86; owner approved build 4467b514
  under Wine with automatic controller handovers and physical indicators.

- GOG offline installation and verified launch under Wine: #61 / #62.
  Real installer manifest/extraction, edition-specific receipt and shared runtime
  preflight passed. No original setup is executed.
- Repeated Jail_20 skips and standard-build diagnostic cleanup: owner validated
  248332c4 (workflow 2102), four native transitions of 388 ms, three scoped
  replay resets, original F248 slot restored after the first Learn_10 visit.

## Remaining priorities

1. #45 — retain the validated queued-before-Playing guard and scoped replay
   repair. Investigate only reproducible residual failures; do not add
   speculative delays.
2. Targeted controller tests: jump/surface and target/dive in an actual
   swimming scene, and world-knowledge versus Murfy in eligible contexts.
   Keep the pending markers until the actual native effect is confirmed.
   Validate these behaviors under Wine with the available controllers.
3. Final cross-edition regression: fresh CD installation and verified launch,
   GOG installation/verification and updated embedded guide. Retain the approved
   CD executable requirement and original approved GOG executable. Check source
   availability and all dependency notices. No commercial assets are distributed.

## Platform validation policy

The maintainer has no native Windows computer available. Native Windows
hardware/gameplay testing is deferred to potential community testers and
is not a release prerequisite. Continue Windows x86 automated CI tests;
report native Windows gameplay coverage as incomplete, not validated.

No public release is authorized by these engineering milestones alone.
