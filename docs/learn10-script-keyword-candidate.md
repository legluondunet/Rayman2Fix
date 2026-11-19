# Learn_10 script-keyword candidate — read-only ownership research

**Status: DRAFT, LIVE SCRIPT WRITES BLOCKED. Not for installation or user testing.**
Never replace the validated PR #31 prototype based on this branch alone.

## Motivation

PR #31 has a user-validated native Learn_10 skip on Windows and Wine.
It temporarily sets the original NOMOVIES story flag **1188** in the
Fix actor's progress array, then clears it at native handover. In-memory
restoration was verified, but automatic writes under the game's
`HiddenSlot` directory mean persistent save safety is not established.
The game does not offer a normal user-loadable save before level 2;
no manual save/reload test is requested for the intro.

The original behaviour-3 rule 1, observed in the approved CD binary,
begins:

```
(0) K1  IfNot                 // keyword 1
(1) F248 GetBooleanInArray
(2) O12 actor.V42             // persistable story bits
(5) I1188
(6) K8  Then                 // normal cinematic
...
(100) K9 Else                // native NOMOVIES alternative
(106) C45 JustPressedBut     // real game input
(109) C48 IsTimeElapsed      // original timing guard
...
```

The original Rayman 2 keyword enum
[`AI_Const.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/AI/AI_Const.h)
defines `AI_E_kw_If=0` and `AI_E_kw_IfNot=1`. The corresponding
[`AI_tdstNodeInterpret`](https://github.com/raytools/ACP_Ray2/blob/master/src/AI/AI_Scr.h)
is an 8-byte x86 script record with parameter at offset 0,
indent at offset 6, and type at offset 7.

**Research hypothesis:** when bit1188 is 0, temporarily evaluating
that root node as `If` instead of `IfNot` would choose the same
native ELSE branch as the validated NOMOVIES route, without changing
the progress bit. This is still hypothetical.

## Critical ownership finding

The upstream [`AI_tdstMind` and `AI_tdstAIModel`
structures](https://github.com/raytools/ACP_Ray2/blob/master/src/AI/AI.h)
show `Mind -> AIModel -> ScriptAI -> Comport -> Rules -> Nodes`.
`AIModel` is reached by a pointer from the actor's mind; it is
not proven to be privately allocated per actor. Thus **patching the
original node array may change a SHARED model** also used by another
actor, including future actors.

A snapshot with one current user of the model does NOT establish
exclusive ownership across all loaded/unloaded resources or future
spawns. Atomically writing a shared node still changes it globally,
and restoring after 5 seconds of renderer callbacks is NOT an
independent watchdog.

**Safety decision:** `kLearn10SharedScriptWritePermitted=false`
is a hard, compile-time guard. The script-gate opt-in build can
read the live script/AI model, but refuses before any write with
`phase=blocked reason=shared-AIModel-ownership-unproven`.
The original `IfNot -> If` and restore code remains a *disabled
research hypothesis* only. CI rejects any Windows x86 candidate
binary containing its `phase=armed` marker.

## Read-only AI model alias survey

The separate header `learn10_model_alias.hpp` traverses two known
runtime roots (dynamic/inactive worlds) with bounded graph traversal.
It follows only checked pointer chains through:
`SuperObject -> EngineObject -> Brain -> Mind -> AIModel`.

- Detects multiple current actors referencing the same AIModel.
- Refuses a complete result on cycles, duplicate nodes, broken parent
  links, unreadable actors or a missing director.
- Reports actor counts, alias counts and whether one current actor
  is observed; **the result never grants permission to write**.
- Does not print or transmit raw addresses, model identity or save
  contents. No writing to game memory or files.
- Pure C++17 synthetic fixtures cover unique-now, shared,
  incomplete, cycle, invalid parent, bad target and bounds.
- Optional research runtime opt-in:
  `RAYMAN2FIX_LEARN10_SCRIPT_GATE_PROTOTYPE=1`, with
  `[Debug] Log=1` and `[Features] SkipCinematics=1`.
  This only logs the census and `phase=blocked`, not a skip.

## Next independent approaches

Two possibilities merit investigation **without modifying the
shared original script buffer**:

1. **Interpreter-scoped conditional override:** locate the verified
   approved-CD interpreter callsite and let only the identified
   director's root `IfNot` condition select its native ELSE branch.
   Validate function signature, timing, actor identity, and safe
   restore. The upstream ACP_Ray2 API exposes
   `AI_fn_p_stEvalKeyWord`, `AI_fn_p_stEvalCondition` and
   `AI_fn_p_stEvalTree`, but its offsets are from another reference
   executable and must **not** be used for the CD without a signature
   match. No interpreter hook exists in this branch yet.
2. **Per-actor copy-on-write AI model:** clone only the required
   script/behaviour structure and redirect the identified actor's
   private reference. Requires proving engine allocation/lifetime,
   safe restoration of pointers and freedom from shared aliases.
   Copying only one pointer or using a shallow clone is insufficient.

The existing script opcode shape, native physical Enter handling and
native one-second timing guard should be preserved in either approach.
Avoid synthetic keyboard input, direct V96/var110 changes and forced
director behaviour transitions: earlier forced exits corrupted the scene.

## CI and release boundaries

`RAYMAN2FIX_LEARN10_SCRIPT_GATE_PROTO` is an isolated research
compile-time option, mutually exclusive with
`RAYMAN2FIX_LEARN10_NOMOVIES_PROTO`, and requires
`RAYMAN2FIX_RESEARCH_PROBES=ON`.

CTest runs the root-keyword policy tests plus the new ownership
census fixtures on Linux and Windows. The separate x86 Windows
candidate is compiled **without uploading an artifact** and must
contain the read-only survey + blocked markers but not the live
script-arm or prior story-bit mutation markers. Normal build outputs
remain unaffected.

This branch provides a safer **research instrument**, not a verified
new skip. PR #31 remains the only user-validated native skip, still
opt-in and isolated. No user save test or further early-Enter tests
are requested.


## Exact-hash offline preflight for native evaluator addresses (19 Nov 2026)

A new **read-only** tool,
[tools/learn10_interpreter_preflight.py](../tools/learn10_interpreter_preflight.py),
can inspect the independently approved, owner-supplied unprotected
`rayman2.exe` **without launching the game** or writing to the EXE.
It uses only Python's standard library and writes a JSON report to
stdout. It refuses unknown files, links, non-x86 or non-PE32 images,
bad section bounds, wrong PE timestamp and mismatched known code anchors.

The only admitted executable is the project's existing **approved** PC
unprotected build (1,468,928 bytes; exact SHA-256
`9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`),
already hash-pinned in `src/launcher/auto_flow.cpp` and
`src/launcher/game_launch.cpp`. PE timestamp `0x3807B68B` comes
from the already approved runtime.

Source comparison yielded two exact, independently grounded reference
matches with [raytools/ACP_Ray2](https://github.com/raytools/ACP_Ray2):

- `GAM_fn_vAskToChangeLevel = VA 0x4054D0`, same as Rayman2Fix's
  approved `kRequestLevelRva = 0x54D0`.
- Dynamic/inactive world roots `VA 0x500FD0`/`0x500FC4`
  match the approved world-census addresses.

The same upstream [`AI.c`](https://github.com/raytools/ACP_Ray2/blob/master/src/AI/AI.c)
provides **candidate only** (not CD-validated) evaluator VAs:

| Reference symbol | VA | RVA |
| --- | --- | --- |
| `AI_fn_p_stEvalKeyWord` | `0x480800` | `0x80800` |
| `AI_fn_p_stEvalCondition` | `0x480830` | `0x80830` |
| `AI_fn_p_stEvalTree` | `0x480B90` | `0x80B90` |

The preflight confirms two **separately verified CD anchors** before
sampling these candidate VAs from **executable PE sections only**:
`kNativeScreenToVertexRva=0x22940` and
`kSuperimposedProcRva=0x710C0`. Output includes only the
SHA-256 of each candidate's first **32 bytes** (shorter than the 0x30-byte separation between two reference entrypoints), never their content.
It deliberately returns **exit code 3**
(`CANDIDATE_NOT_VERIFIED`) even if the approved EXE and all
known anchors match: neither the evaluator's **machine-code semantics**
nor its **calling convention** has been inspected in that exact binary.

Optional researcher command (only when a *local* owner-approved EXE
is already available; **do not ask the user for a test now**):

```sh
python3 tools/learn10_interpreter_preflight.py /path/to/approved/unprotected/rayman2.exe
```

Synthetic PE32 fixtures and rejection tests run under GitHub Actions:
`python3 -m unittest discover -s tests -p 'test_learn10_interpreter_preflight.py'`.
No commercial binary enters CI or the repository, no hook has been
added, and `kLearn10SharedScriptWritePermitted=false` remains
unchanged. This diagnostic cannot authorize an interpreter detour,
even if a candidate VA maps to executable bytes.

**Next research requirement:** inspect the disassembly of the
approved evaluator candidate at the exact VA/RVA, validate the
`__cdecl` signature with three parameters, identify which branch is
evaluated and whether a strictly actor-scoped non-mutating override is
possible without disrupting engine recursion, reentrancy, timing,
other script nodes, and original return semantics. Do not substitute
the address taken from ACP_Ray2 for this verification.


### Read-only E8/rel32 call-pattern inventory

The preflight also scans **only executable PE section bytes** for the
raw 5-byte byte-pattern `E8 <signed-rel32>` whose computed destination
equals a reference evaluator VA. Each candidate gets
`raw_e8_rel32_pattern_count` and up to 20 source RVAs. These outputs
are **heuristic matches, NOT verified calls**: an `E8` byte may be
part of another x86 instruction, a constant, or embedded data. A
missing pattern also does not rule out indirect calls, inlined code,
or a different implementation. In particular, the preflight
**cannot infer calling convention or correct interception semantics**
from byte patterns, and `allows_hook` remains unconditionally false.

The synthetic tests inject two isolated relative-CALL encodings targeting
the two close-together reference evaluator VAs and validate the
reported counts and source RVAs. No game image or byte payload is
included in the repository or the CI workflow.

## Direct executable disassembly — confirmed (19 November 2026)

Two copies of the owner's **already-approved unprotected** `Rayman2.exe`
were found in the private file library. Both are 1,468,928 bytes and
match SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`.
The exact PE32 binary was examined read-only with GNU objdump, without
copying commercial game bytes into GitHub.

| Function / native structure | Verified VA | Static findings |
| --- | --- | --- |
| `AI_fn_p_stEvalKeyWord` | `0x480800` | Three stack arguments; reads the keyword from the root script node, resolves a handler through `0x487D50`, passes actor, following node and result pointer, then cleans 12 argument bytes |
| `AI_fn_p_stEvalCondition` | `0x480830` | Separate condition dispatcher; normalizes boolean result in output parameter |
| `AI_fn_p_stEvalTree` | `0x480B90` | Dispatches by node type, with three stack arguments |
| Keyword resolver | `0x487D50` | Retrieves a handler from the keyword table at `0x4A5E80` |
| **If / IfNot handler** | **`0x489250`** | **Keyword 0 (If) and keyword 1 (IfNot) BOTH use this SAME handler**; the handler branches by original keyword and negates the condition on the IfNot branch around `0x4892DF` |
| Script skip cache | `0x489190` | Can write a computed skip offset into the original script node; native traversal is not purely read-only |

**Consequences:** Swapping pointers in the global keyword dispatch table
would do nothing for If versus IfNot: their handler pointers are identical.
Patching the root node would still affect a potentially shared AI model.
A safe solution, if possible, requires an **actor- and exact-node-scoped
interpretation of one condition**, with no shared script modification,
progress-bit toggle, or global handler change. No such interception has
been implemented.

The offline PE32 preflight now checks six SHA256 machine-code windows,
the distinct 16-byte IfNot inversion sequence and both shared-handler
table entries **only for the exact approved full-file SHA**. A recognized
binary yields `DISPATCHER_STATICALLY_IDENTIFIED_NO_HOOK`; exit code 3
and `allows_hook=false` are intentionally unchanged. The source's
three-argument stack shape is observed, but no detour, trampoline,
reentrancy, stack ownership, hook lifetime, or per-actor context has
been validated in-game.

**No original EXE, dump, raw game code or executable artifact was
published.** CI tests remain synthetic. PR #31 remains the only
user-validated native skip; PR #32 remains research-only.
