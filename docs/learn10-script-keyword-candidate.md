# Learn_10 script-keyword alternative — independent research candidate

**Draft only. NOT for installation or user testing.**

## What changed

The existing, user-validated PR #31 temporarily sets story bit 1188
(`NOMOVIES`) to select the original native ELSE branch in director
behaviour 3. Although the bit is restored at handover on Windows and
Wine, it resides in the persistable progression array, which may be
written automatically before a manual save slot exists.

The supported Rayman 2 director's behaviour-3 rule 1 has this exact
root structure (original node indices in parentheses):

```
(0) K1  IfNot                  // keyword value 1
(1) F248 GetBooleanInArray
(2) O12 actor.V42             // progress-bit array
(5) I1188                    // story bit 1188
(6) K8  Then                 // normal cinematic
...
(100) K9 Else                // native NOMOVIES alternative
(106) C45 JustPressedBut     // physical input, no injection
(109) C48 IsTimeElapsed
...
```

Rayman 2's own keyword enum in `raytools/ACP_Ray2/src/AI/AI_Const.h`
declares `AI_E_kw_If=0` and `AI_E_kw_IfNot=1`. Its node
structure `AI_tdstNodeInterpret` in `src/AI/AI_Scr.h` is an
8-byte x86 record with the 32-bit keyword at offset 0, depth at
offset 6 and type at offset 7.

**Hypothesis:** when original bit1188 is **0**, temporarily replace
that one root keyword `IfNot(1)` with `If(0)`. The same native
ELSE path should now be selected. Neither `Fix.V42` nor any other
progress bit, actor state, native input, timeout variable, or script
condition beyond the root keyword is modified.

## Safeguards in the isolated candidate

- Separate compile-time `RAYMAN2FIX_LEARN10_SCRIPT_GATE_PROTO=ON`;
  requires research probes; mutually exclusive with
  `RAYMAN2FIX_LEARN10_NOMOVIES_PROTO=ON`. The default build
  contains neither script mutation nor story-bit mutation.
- Separate runtime environment opt-in (exact value `1`):
  `RAYMAN2FIX_LEARN10_SCRIPT_GATE_PROTOTYPE`, logging and
  `SkipCinematics=1` must also be enabled.
- First visit only, original bit1188 must be known **off**, director
  behaviour must match original behaviour 3, and the two original
  behaviour-3 rules and native cleanup must match the strict approved
  script fingerprints.
- Check exact root keyword value `1`, depth `1`, type `0`;
  node must be aligned and already writable. No `VirtualProtect`
  and no modifications of game executable, asset files, INI or saves.
- Make **one** atomic 32-bit CAS from keyword `1` to `0`,
  never a blind write. Revalidate and restore with CAS `0` to
  `1` when native director leaves b3, when story1091 indicates
  handover, at an observed 5,000 ms timeout or on level change.
  Refuse stale/mismatched script-node pointers; no writes to an
  unrelated or changed node.
- Log `phase=armed`, `phase=restored`, `phase=restore-pending`
  and `phase=handover-verification`, including read-only bit1188.
  Never label a failed read as a successful restore.
- C++ policy tests check exact keyword, context, negative cases,
  read-only original progress flag requirement, timing and tick
  rollover. CI compiles the separate Windows x86 variant and
  verifies that the original bit-writing prototype is *absent*.
  No artifact is uploaded to avoid presenting this as user-ready.

## Why it is still a draft

**Script-node modification is not automatically safe.** Further
analysis must establish whether other actors share the rule, whether
the engine caches an evaluated branch and whether all lifecycle
paths leave the original script node intact. The 5,000 ms timeout is
callback-driven, not a watchdog. An interruption could leave a
transient IF keyword in memory until callbacks resume, although the
persistable progress array is never directly changed by this
candidate. It is not yet established that runtime script bytes can
never affect an internal save.

The candidate must remain separate from PR #31. No player test,
manual save test or merger is requested until these questions are
resolved through static analysis and independent reviewer checks.
