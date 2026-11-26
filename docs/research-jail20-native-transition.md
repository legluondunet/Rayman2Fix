# Jail_20 native transition — read-only GOG investigation

2026-11-26. Owner-supplied original GOG data; no decoded resource is included or modified.

## Runtime evidence

In the owner's 21:54:39 log, first visit has story bit 1091 clear. The existing guarded native map request reaches Learn_10 in 389 ms. On replay the bit is set and the direct request is deliberately refused. The experimental IPT20 route recorded 23 consumer samples returning true, without an immediate exit. It was subsequently removed from the runtime. Do not equate an input consumer returning true with a native scene-transition acknowledgement.

## Offline evidence

The existing `tools/inspect_rayman2_sna.py` decoder and bounded block inventory accept all five supplied SNA files. Jail_20 SHA-256: `78fb4cade58406184f7f0dcc0ca3a705db0c87c65cc7fb0f10afed7d2154f6da`. It has three nonempty local blocks (4/1: 89860, 5/2: 439048, 13/1: 117512 bytes). The following offsets are relative to the data of module 13, block 1, not file offsets or live addresses.

* At 0x10fc a two-entry script header array points to the node arrays at 0x1108 and 0x128c; the behaviour-table entry at 0xef4 references that header array.
* The first array has 43 nonterminal 8-byte nodes and an EndTree at 0x1260. Two resolved string references at 0x1218 and 0x1258 point to separate Learn_10 names in the same block. Both follow procedure index 86 (ChangeMap). Their respective local-variable predicates compare V0 to 2 and 99. At the start of this rule V0 is assigned function index 46 (GetHitPointsMax) on Me. A separate path compares V0 to 1 and changes custom bit 17 on two actors. These are native script predicates, not authorization to force any value.
* The adjacent rule at 0x128c includes condition index 45 (JustPressedBut) combined with an actor custom-bit check (condition 75, bit 17); its action is procedure 115 (SendSoundRequest) on another actor, not ChangeMap. Its opaque button operand has not been resolved to an IPT index.
* A literal 1091 with the expected integer-node shape is present in Learn_10 module 13/1 at 0x7138, in an F248 progress-array predicate. No such literal node was found in the supplied Jail_20 local blocks. This is a bounded literal search, not proof that Jail_20 has no indirect dependency on the bit.

Opcode names cross-checked against [Raymap R2 AI tables](https://github.com/byvar/raymap/blob/master/Assets/Scripts/OpenSpace/AI/AITypes/AITypes_R2.cs). Script node layout matches the existing read-only `scene_script_probe.hpp` reader.

## Next validation

Identify the owning actor and all writers of its maximum-hit-points state; follow the native scene's control and cleanup paths. Resolve the actual native button operand rather than assuming IPT20. Preserve first-visit behavior, do not clear story bit 1091, write actor state, force a behaviour, or reintroduce a global input override based on these static findings alone. No new runtime change is proposed by this note. The owner need not retest the same build.
