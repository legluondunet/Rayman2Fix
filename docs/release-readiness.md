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

1. #45 — owner validated both replay cinematic fixes in build d88a8501
   (workflow 2116): six successful Jail_20 exits, five replay resets,
   six F248 handover passes with restored slot and bit1188=0, plus
   explicit visual/gameplay approval. Retain the validated runtime.
   Reopen only on concrete regression evidence.
2. Targeted controller tests: jump/surface and target/dive in an actual
   swimming scene, and world-knowledge versus Murfy in eligible contexts.
   Keep the pending markers until the actual native effect is confirmed.
   Validate these behaviors under Wine with the available controllers.
3. Cross-edition fresh-install regression passed (PR #62 merged as 51ec1665).
   CD log 20261127-010554: 2095 files verified, approved EXE and backup,
   successful game exit; GOG installation and repeat verification passed earlier.
   A separate CD process relaunch is not captured in the first-session logs.
   Check release source availability and dependency notices before publication.
   No commercial assets are distributed.

## Platform validation policy

The maintainer has no native Windows computer available. Native Windows
hardware/gameplay testing is deferred to potential community testers and
is not a release prerequisite. Continue Windows x86 automated CI tests;
report native Windows gameplay coverage as incomplete, not validated.

No public release is authorized by these engineering milestones alone.

## Targeted controller validation candidate

The existing mapping remains unchanged. South (A/Cross) supplies native IPT10/15/21, left trigger supplies IPT13/14, and D-pad Up/Down share the PC DIK_F1 route. A new bounded `RUNTIME-SDL3-TARGET` trace with Log=1 distinguishes physical edges, accepted actions and the first verified native route per accepted activation. The four actions have independent budgets; focus/device/scene boundaries clear route evidence. A native predicate result or keyboard-buffer merge is not proof of swimming movement, opening a screen, or hearing music. Validate those effects visually/audibly. No new setting or game memory write is introduced.

## Owner validation checkpoint 20261127-082021

The owner confirmed visual/gameplay integrity after six cinematic tests. The same runtime log records three D-pad Up and one D-pad Down activations reaching the verified DIK_F1 merge in Learn_10 (lines 1064–1091); original IPT22/23 positive states are observed on the first three activations. No generic menu opening or paired audio continuation attributable to those four presses is recorded. This establishes delivery, not the native screen effect. Jump reaches IPT10/15/21, but no swimming-specific movement is established and no target/dive press appears. Test F1 in an eligible gameplay situation (including sustained hold) and compare physical keyboard F1 to the two pad directions. Test LT/L2 and South in actual water. The validated d88a8501 artifact already contains the required input and audio traces; reuse it rather than rebuilding solely for documentation changes. Leave pending labels unchanged until the owner confirms the actual effects.
