# Learn_10: actor-scoped F248 temporary result — research only

**Independent source:** the owner's SHA-pinned, decrypted Rayman2.exe
(1,468,928 bytes; SHA256
`9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`)
was disassembled locally in read-only mode.

## New native evidence

The original first rule in director b3 is:
`K1 IfNot( F248 GetBooleanInArray( Fix.V42, 1188 ) )`.
**F248 is a FUNCTION node (type 3), not a CONDITION node.**
Therefore a hook on EvalCondition (`0x480830`) would *miss* this call.

The PE32 tree node-type dispatcher at VA `0x480B90` uses
table VA `0x4A4180`, where type 3 resolves to the FUNCTION
dispatcher at `0x480880`. That dispatcher obtains the F248 native
handler through resolver `0x487820`, whose 8-byte-stride table
starts at `0x4A5460`. Entry 248 (table VA `0x4A5C20`)
points to **`0x47A8E0`**, the approved-CD function handler.
Its 32-byte SHA256 is
`19d2d8deee6fc7a9e98b7740d92fe9ebb280852e2188b66e5a0afc9a94023239`.
The 32-byte entry SHA256 for `0x480880` is
`ec60b75b3b555dff687987f1cdb658320e1011ef33e009eb91f6002896979433`.

The F248 handler reads the indexed bit in the array and writes
a 0/1 integer into **the ephemeral output parameter**
(`AI_tdstGetSetParam`): value at offset 0, type **6** at offset 12.
It also computes the next-node pointer. The parent `IfNot` handler
then negates that result to choose the original `Then` or `Else` path.

**Candidate mechanism, not implemented:** after normal F248 execution,
for the **exact** director superobject, active intelligence b3,
verified root `IfNot`, exact function node `root+8`, first visit and
bit1188=0, a scoped detour could hypothetically change ONLY the
temporary returned integer value from 0 to 1. The parent `IfNot`
would then evaluate false, selecting the native ELSE path with its
original real-Enter and one-second timing guards intact.

This would not modify `Fix.V42`, the original script bytes, global
function tables, or shared AI models. HOWEVER, interception of
`EvalFunction` has not been installed, tested or shown safe for
runtime reentrancy, output-parameter lifetime, concurrent actor calls,
recursion and exact detour restoration. A callback that changes the
value outside the exact context would still be unsafe.

The pure C++17 policy in
`include/rayman2fix/learn10_f248_result_scope.hpp` and its synthetic
CTest fixture check all the negative cases. **The live hook remains
unconditionally disabled** (`kLearn10F248RuntimeHookAllowed=false`).
This is not a playable implementation, and no user test is requested.

Research next: independently inspect callsite/return semantics of
the exact F248 node in the approved binary, prove actor-scoped
matching at the function dispatcher, and design a reentrant-safe
trampoline only if supported by the evidence. Do not modify the
existing functional PR #31.
