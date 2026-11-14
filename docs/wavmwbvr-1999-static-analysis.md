# Rayman 2: static audit of the owner's original WAVMWbvr.dll

**Status:** research-only, **no native driver patch / no new intercepted audio calls**. 14 November 2026. This is static inspection of the **exact binary uploaded by the owner**, not a runtime trace, inference from `snd_cpa.ini`, or assumptions about another retail/GOG DLL.

## Binary identity

| Attribute | Observed |
| --- | --- |
| File | `wavmwbvr.dll` (matches `WAVMWbvr.dll` in owner's Windows log) |
| Size | **177,152 bytes** |
| SHA-256 | `cd0a0ad0a95880cd8e25e50844ec8e6f9d6725bbee7c09a8ab328cbbc953f016` |
| Format | PE32 DLL / Intel x86 / image base `0x10000000` |
| PE time stamp | `0x38076FB4` (15 October 1999, 18:17:24 UTC) |
| Code section | `.text`, RVA `0x1000`, virtual size `0x1B68C` |
| Debug info | CodeView NB10 string contains `winmm/mmx/WAVMWBVR.pdb` (path embedded in original DLL; does **not** mean the PDB is available) |

The PE import table includes `WINMM.dll` `waveOutOpen`, `waveOutWrite`, `waveOutReset`, `waveOutClose`, and `waveOutPrepareHeader`, plus WinMM mixer operations. An import alone does **not** prove that a particular Pause/Resume implementation directly invokes those APIs.

The original owner runtime log `Rayman2Fix(20261114-201946).log` reported `[DLL_Sxd] Default=WAVPW File0=WAVMW` but resolved all **four real original callbacks** into `WAVMWbvr.dll`. The PE export table independently confirms these exact entry points:

| Exported function (stdcall, one 32-bit argument) | RVA | Internal role |
| --- | --- | --- |
| `_SND_fn_vPauseTransitionSxd@4` | `0xC7E0` | Type-tag dispatcher |
| `_SND_fn_vResumeTransitionSxd@4` | `0xC830` | Type-tag dispatcher |
| `_SND_fn_vStopSxd@4` | `0xB920` | Normal sound Stop; reads tag from **argument + 8** |
| `_SND_fn_vStopTransitionSxd@4` | `0xC790` | Type-tag dispatcher; reads tag from **argument + 0** |

**Critical argument distinction:** `PauseTransitionSxd`, `ResumeTransitionSxd`, and `StopTransitionSxd` immediately dereference their argument as a pointer to a structure with a **type tag at offset 0 and a backend payload pointer at offset 4**. Thus the `DWORD arg` logged as a native *handle* by Rayman2Fix represents an opaque **transition-object pointer** at this exported layer; the same `arg` in two calls proves source-paired object equality, **not** an independently verified physical WinMM voice handle or permanent stream lifetime. `StopSxd` is **not** layout-identical: its dispatcher reads type at `arg + 8`. Never apply the Transition-layout assertion to all native sound APIs.

## Exact transition dispatcher branch map

In the reviewed file, `PauseTransitionSxd` at `0xC7E0` inspects `[arg]` and routes as follows. `ResumeTransitionSxd` at `0xC830` mirrors that dispatch:

| Type tag `[arg]` | Pause call path (RVAs) | Resume call path (RVAs) | Statical destination identity |
| --- | --- | --- | --- |
| **1** | `C7E0 → 6FF0 → C320` | `C830 → 7010 → C350` | Native sample/voice path, with nested wrapper/handle handling |
| **2** | `C7E0 → 2900 → [dynamic fnptr 2692C]` | `C830 → 2930 → [dynamic fnptr 26930]` | `_SND_fn_vPauseTransitionMPEG@4` / `_SND_fn_vResumeTransitionMPEG@4` |
| **4** | `C7E0 → 3C60 → [dynamic fnptr 26A9C]` | `C830 → 3C90 → [dynamic fnptr 26AA0]` | `_SND_fn_vPauseTransitionSxdADPCM@4` / `_SND_fn_vResumeTransitionSxdADPCM@4` |
| Other | Error/report branch; returns | Error/report branch; returns | Unsupported/unknown |

The other modules' export **names** are embedded in this original DLL and resolved by the helper at RVA `0x3FA0`. Pointer setup sites around RVAs `0x4F64–0x4FA7` (MPEG) and `0x5D43–0x5D86` (ADPCM) install an internal fallback if the target is absent. These dynamic function pointers were **not** resolved in the submitted disk image: we have not proved which secondary module/version is actually loaded at runtime or whether it uses reference counting.

For `StopTransitionSxd`, type 1 routes to `0x6FC0`, type 2 to `0x28D0` (dynamic `_SND_fn_vStopTransitionMPEG@4`), and type 4 to `0x3C30` (dynamic `_SND_fn_vStopTransitionSxdADPCM@4`). This function's correspondence with each Pause/Resume branch is exact at the exported dispatcher level.

## Lower sample/voice path: an explicit flag, not a proven reference count

For type 1, the path through `0x6FF0 → 0xC320` eventually reaches `0xD140 → 0xAB20` for a non-nested voice object. The mirrored Resume goes through `0x7010 → 0xC350 → 0xD150 → 0xAB40`. A nested intermediary (tag 2 inside the type-1 payload) can route through `0xCDA0/0xCDB0` before reaching those routines.

At `0xAB20`, the leaf writes `[voice+8] = 1` and dispatches its underlying pause helper. At `0xAB40`, it writes `[voice+8] = 0` and dispatches its underlying resume helper. Subsequent helpers at `0x9EE0` and `0x9F10` set and clear a per-voice flag in a 112-byte-stride table, then call another loaded function pointer via `0x31A0` / `0x31D0`. These are **observable boolean writes at this specific layer**, rather than increments/decrements of a nesting counter. This **does not establish** that the full WinMM mixer/device or MPEG/ADPCM branch lacks independent pause-depth or asynchronous buffering semantics.

Type 1 also calls lock/unlock helpers at `0x6860` and `0x6880`; this makes bypasses of the native dispatcher especially risky. No code was executed from the DLL during this investigation.

## Immediate implications for the ESC feature

1. The owner's Windows logs show two identical source-specific `PauseTransitionSxd` object pointers per ESC open and two matching `ResumeTransitionSxd` object pointers per exit. **They do not log the type at `[arg]`**. We therefore cannot yet say whether actual ESC music follows native sample, MPEG, or ADPCM branch. The `[DLL_Adpcm] Default=APMPW` setting is not a substitute for this observation.
2. Do **not** infer that two identical driver calls create a refcount of 2, or that one callback can safely be dropped. Some type-1 leaf state appears boolean, but branches 2 and 4 are dynamically delegated; callback pairing is part of the original engine contract and remains preserved.
3. A logical Theme/SXD resource update `9:7→9:8` during an ESC menu is not proof that this transition-object pointer changed or was freed. Nonetheless, until type and actual backend lifecycle are independently verified, the existing `phase=6` conservative native passthrough remains correct.
4. **Next minimal proof — implemented as passive instrumentation:** `RUNTIME-ESC-PREDRIVER-GATE transition_type_post_pause` now reads `[arg]` **after the unchanged original script Pause wrapper returns**, not in any audio-driver callback. It is restricted to the first 16 source-verified, same-object two-Pause episodes; `-1` means unreadable/not sampled, `0` means a read but unexpected tag, and `1/2/4` are the static tag values of this exact file. It reports no raw pointers and does not influence any audio decision. **This post-wrapper read is weaker than a verified pre-call snapshot** if the game changes/frees an object during Pause; `-1` must remain unknown and even a valid tag cannot authorize bypass. If type 2 or 4, next analyze the exact dynamically resolved MPEG/ADPCM callback modules before asserting pause semantics. Comparing a matching Resume tag at the original callback boundary is not implemented yet.
5. The earlier `pre_driver_pause::Policy` remains a disconnected research model. Do not enable it from static dispatch analysis alone; its unrecoverable deferred-Pause debt after a stream retirement is still unsolved.

## Reproduction notes

Static-only tools: `sha256sum`, `file`, `objdump -p`, and `objdump -d -Mintel --start-address=... --stop-address=...`. Disassembly image base `0x10000000`; to examine an RVA add the image base. Exact original binary SHA-256 above is a **per-version guard**. Do not reuse these RVAs for any other DLL without a byte-identical hash check.

No original file was modified, repackaged, or committed. This document contains only derived technical analysis.
