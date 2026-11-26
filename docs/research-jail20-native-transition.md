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


## Actor ownership follow-up

The local pointer walk confirms the transition's owner: module 4/1 offset 0x1198 (actor), 0x12f0 (standard-game properties), 0x1378 (brain); module 5/2 offset 0x3d8c4 (superobject, with a matching actor backlink). Its normal AI behaviour table is module 13/1 offset 0xee8 (three behaviours). The actor has one designer variable, V0, and on-disk max HP is 0. Object-type IDs are (2,12,12); the nearby object-type name lists suggest World / Leveljail_20, but the complete cross-block name-table resolution is not established without the level relocation table. Do not rely on the guessed name for runtime targeting.

The two map calls are in normal behaviour 1, rule 0, nodes 33 and 41. The same rule's node 9 calls SetHitPointsMax(Me, 0) before the V0 branches. In particular V0=1 clears (not sets) bit 17 on two actors. The next behaviour's effect branch can return to behaviour 1; it is not itself a map change.

Reproduction: python3 tools/inspect_jail20_transition.py /path/to/jail_20.sna

With the supplied GOG file, the local actor/backlink scan identifies 25 actors with a valid local AI model, 78 distinct normal/reflex scripts and no script traversal failures. Exactly two literal Learn_10 change-map calls are found, both in the same actor. Of 38 candidate max-HP writer sites (per actor, including shared models), only the self-reset above resolves to this actor using the stored local references; eight have unresolved/null targets. Dynamic variable assignments, macros, relocation records and engine-side writes are outside this audit. In particular, this is not evidence that no other writer can target the actor at runtime.

Negative comparisons using the supplied GOG files found zero literal Learn_10 change-map calls in Jail_10 (7 actors / 91 scripts), Learn_10 (77 / 280) and Menu (26 / 189), with no traversal failures. The offline tool was exercised with the equivalent PC SNA byte-mask decoder and block layout; no Windows runtime test or compilation was involved.

Next: resolve cross-block references with the original Jail_20 RTB if available, or observe the verified controller's max-HP byte and current normal behaviour during natural playback versus a replay. Do not change the byte, reset bit 1091 or call native scene functions until the producer and required cleanup are understood.


## Text-command producers (shared Fix + Jail_20)

Decoded the owner-supplied GOG Fix SNA (SHA-256 `62c3f44504731808745b03ee69c1d6bef442b34a30996a0db300005e3197308d`) together with the previously identified Jail_20 SNA. A bounded local/shared pointer walk found 30 actors (25 level-local, 5 shared), 301 distinct normal/reflex scripts and zero script traversal failures. There were 41 candidate maximum-HP writer sites, counting shared models per actor. No relocation records or live execution were used.

Two local actors at module 4/1 offsets 0x3a20 and 0x48d0 share the same AI model (13/1:0x2960; type IDs 13,14,14 and 13,14,17). Their designer variable V0 is a PersoArray (element type 9, header 8 bytes, capacity 20), with the transition controller at index 3 and index 2 respectively. V65 is a temporary Perso reference, initially null. V84 is a byte index and V96 a signed integer. The references were verified against the controller's superobject and actor backlink. The previously unresolved V65 writers are therefore explained by array lookup rather than a fixed stored pointer.

The common model's normal behaviour 3/rule 2 and behaviour 7/rule 1 contain the same command dispatcher:

- F158 (TEXT_DisplayText) supplies V96 (nodes 23 and 36 in B3/R2, node 8 in B7/R1 are the assignments).
- For 1300 < V96 < 1400, V84 = V96 - 1301; after a valid-object check, V65 = V0[V84] (B3/R2 nodes 834–838; B7/R1 nodes 806–810). If necessary the actor is activated and custom bit 17 cleared.
- For 1399 < V96 < 1500, with a valid V65, subtract 1400 from V96 and call P6 SetHitPointsMax(V65,V96) (B3/R2 nodes 864–869; B7/R1 nodes 836–841).
- Therefore selection events 1304 and 1303 would target the level controller on the first and second actor respectively. Subsequent 1402 or 1499 would deliver state 2 or 99. These are conditional static interpretations, not a measured runtime sequence.

The shared Fix SNA text block (14/0) contains /E1303:, /E1304:, /E1402: and /E1499: control markers (and lowercase variants), consistent with the dispatcher. Exact text-handle-to-record mapping and actual active script instance on each visit remain unverified. F158 may have other side effects; overriding its return or injecting a command is not yet justified. The presence of these markers in the shared resource does not establish that the specific Jail_20 scene executes them on every visit.

The correct next runtime observation is a bounded read-only sample of the two actors' current behaviour, V84, V96, V65 target match, and the controller max-HP byte, correlated with the existing skip request and engine transition log. No game state, script or text may be modified. Do not treat story bit 1091 as the established root cause. The earlier conclusion that no producer was statically identifiable is superseded by this conditional array-dispatch path.
