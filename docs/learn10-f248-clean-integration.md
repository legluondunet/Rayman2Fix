# Learn_10 native skip — clean F248 integration candidate

The **F248 temporary-result** route is the only experimental Learn_10
skip brought forward from PR #33 (Wine 11.19 validated by user). The
old `NOMOVIES` story-bit mutation and `IfNot` script-node rewrite
are **not copied into this branch**.

The original game function F248 `GetBooleanInArray` is first called
unchanged. Only the specifically matched director, behavior 3, root
`IfNot(F248(Fix.V42,1188))`, first visit, original integer-0 result
and the trusted game thread can alter the **temporary returned value**
0 to 1. Original engine input and native handover are preserved.

- Build default: **disabled**. Opt-in research x86 build uses CMake
  `-DRAYMAN2FIX_RESEARCH_PROBES=ON -DRAYMAN2FIX_LEARN10_F248_PROTO=ON`.
- Runtime explicit opt-in: environment
  `RAYMAN2FIX_LEARN10_F248_PROTOTYPE=1`, INI
  `[Features] SkipCinematics=1`, and `[Debug] Log=1`.
- No user-selectable new INI option and no extra launcher command flag.
- Do not modify story bit 1188 or shared AIModel script nodes.
- Once armed, the native function-table slot is disarmed on director
  exit, native handover, observed timeout or level change; if restoration
  fails, overrides remain disabled and the failure is logged.

## Verification

This is a carefully extracted version of the Wine-tested PR #33 code
on top of main, rather than a merge of the stacked research history.
An isolated CI executable embeds its matching F248 DLL and is intended
for Windows/Wine regression testing. The normal executable remains
unchanged unless research compile flags are explicitly provided.

Even with correct gameplay on Wine, **native Windows testing remains
pending**, and exceptional teardown/reentrancy conditions require
review before enabling this for all installations or merging to main.
No manual save/reload test is requested before a real save slot exists.
