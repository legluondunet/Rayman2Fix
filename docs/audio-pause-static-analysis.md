# Rayman 2 PC — static map of ESC audio pause and the type-11 transition controller

## Current target behaviour — clarified 14 November 2026

The owner has confirmed the reference behaviour on **Rayman 2 Dreamcast**: **when the in-game menu opened with ESC is visible, the current level music continues uninterrupted**. Rayman2Fix should reproduce that behaviour on Windows and Wine, with the same level music, not generic menu music or a restarted track. On normal ESC close, the existing soundtrack continues seamlessly. SFX/gameplay pauses retain their native behaviour; genuine `StopTransitionSxd`, level changes, replacement of a Theme/SXD resource and focus loss/Alt-Tab must remain authoritative. **F1 is explicitly out of scope**: do not instrument it, compare against it, alter its input or require a F1 classification before implementing the ESC feature.

This decision **supersedes the historical ESC/F1 research discussion below**. Older notes and trace warnings remain as an audit of hypotheses considered, *not* as active acceptance criteria. The current research trace samples the genuine ESC-menu actor candidate and native UI state transitions without logging F1. No runtime music behaviour has yet been changed. A reliable ESC menu lifetime / correct stream identity / generation-safe pause policy is still required before an audio override.

Research note, **13 November 2026**. This document records the source of each binary-level claim and separates the *confirmed static mechanism* from hypotheses about the soundtrack. No licensed game executable or assets are redistributed.

## Native C++17 ESC music adapter foundation — current work

To move beyond synthetic Python models, the PR now contains a **portable, implementation-ready C++ state machine** in [`include/rayman2fix/esc_music_shadow.hpp`](../include/rayman2fix/esc_music_shadow.hpp). It models one *generation-tokenized* current Theme→SXD owner, separate causes **ESC / lost focus / other native pause / unknown**, and a nonrecoverable quarantine until the owner is stopped and a **new** generation is explicitly proved. It also tracks two native Theme/SXD callback routes, without interpreting them as separate user pauses. It exposes `Action::pause`/`resume` logical requests; these do not call any Rayman DLL or modify any music.

The [native C++ test harness](../tests/esc_music_shadow_cpp_tests.cpp) is registered with CTest as `esc-music-shadow-state-machine` on Linux and Windows x86. It checks **32,768** five-event sequences, known/unknown focus states, nested independent pauses, duplicate Theme+SXD callbacks, stale generations, and Stop precedence. Assertions remain active in Release/NDEBUG (explicit `CHECK` macro); this is important because the Windows packaging CI builds Release.

A second isolated [`esc_music_mixer_bridge.hpp`](../include/rayman2fix/esc_music_mixer_bridge.hpp) declares a *future physical adapter contract*. Before attaching any driver callback, it requires independent proof of the **exact original CD**, the **active level-music stream**, the **actual transport handle** and the **pause/resume callback cardinality of the APM DLL**, plus an unchanged nonzero music generation token. There is **no default cardinality**; neither `1` nor `2` calls are assumed just because the original game emits paired Theme/SXD transitions. Once explicitly armed by a future validated adapter, this testable component permits only a bounded pause/resume batch, rejects stale generation callbacks, never synthesizes a Stop, and becomes permanently uncertain if a batch partially fails. [Mock mixer tests](../tests/esc_music_mixer_bridge_tests.cpp), registered with CTest as `esc-music-mixer-adapter-contract`, verify all these conditions on both platforms **without loading an audio DLL**. Merely compiling this contract does **not** activate physical audio interception.

**To identify the final real driver prerequisite without new blind guesses**, the existing opt-in `RAYMAN2FIX_AUDIO_TRACE` build now logs `RUNTIME-AUDIO-DRIVER: channel=... module=... callback_rva=...` once per successfully installed original callback slot. It obtains the **owning loaded module** using `GetModuleHandleExW(...FROM_ADDRESS|UNCHANGED_REFCOUNT)` and logs **only the sanitized module basename and module-relative RVA**. No personal path, raw address, game data or DLL code bytes are logged; no other module is loaded and the original callback continues unchanged. This distinguishes e.g. `APMmxSVR.dll` and `APMP5SVR.dll`, selected by the retail `Snd_Cpa.ini`, and identifies the actual entry points to investigate for idempotence vs refcounted pause. These module names are supported by the [game's public Steam depot inventory](https://steamdb.info/depot/15061/manifests/) and [independent OpenRayman sound research](https://github.com/huntyh/OpenRayman/blob/master/docs/formats/sound.md), but **the owner's actual original-CD mixer DLL has not yet been analyzed**. No user CD asset has been committed to GitHub.

**Current integration status:** C++ shadow and mixer bridge are available for unit-testing and have no connection to the normal runtime or the commercial driver. A future Windows x86 adapter must first prove which native driver functions are used **and how many physical callbacks restore a paused Theme**, then demonstrate ESC-only origin, current Theme equality and correct Stop/focus/generation behavior in a real game session. No `MusicInPauseMenu` INI feature, release, PR merge or altered soundtrack behavior yet.

## Engineering decision — implement a controlled native ESC-music prototype next

**Decision on 14 November 2026:** stop accumulating broad exploratory UI/music logs as the main deliverable. The **retail-CD code is sufficiently mapped to begin a separate, disabled-by-default prototype**. The relevant upstream paths are independently known: generic VM script pause opcode `0x1AA` at `0x471E70` and Win32 focus pause at `0x401C9C` both reach wrapper `0x4536D0` / global native `0x4580B0`. Resumes arrive through `0x471E67` and `0x401CE9` into `0x453700` / `0x458110`. These addresses are **distinct origins**, not an ESC-specific identity. The two participating original sound-table entries (outer `Theme`, inner SXD) both reach the **same physical driver**. The game controls each entry's `+0x1C` mark and the nested depth `0x4A3CC8`, independently of the external driver.

**First candidate:** intercept at the native **per-record dispatch** level, not at a single SXD pause callback. A verified ESC-menu-script pause should leave *only a confirmed live music Theme and its inner SXD* audible, while all other sound records still follow their native pause. Native CD and Win32 focus operations must remain distinct; all Stops and level replacement have priority. A very narrow [pure reference experiment](../tools/esc_music_record_experiment.py) and [synthetic tests](../tests/test_esc_music_record_experiment.py) now model original mark/depth ordering and show how this can work for ESC enter/exit, focus-loss/gain and even closing ESC before focus returns **without modifying the game runtime**.

**Critical counterexample discovered by the prototype:** a minimal intervention that skips ESC music marks/callbacks and re-bases the native mark at ESC close **is still not safe** if more than one independent external pause cause overlaps. For example ESC→focus loss→ESC close→an additional native script pause→focus gain can resume the music while the second pause remains active. This negative test **is intentional evidence against shipping the naive mark patch**, not a pass of the feature. Therefore an actual implementation must either **virtualize driver pause/resume state for the verified live Theme generation using separate ESC, focus and other-native pause causes**, or redesign the native record bookkeeping with a rigorously tested per-cause shadow state. A raw keyboard state, generic VM opcode, single record mark, or low-level callback count is insufficient.

**Practical next milestone:** produce *one* experimental Windows x86/Wine build, enabled **only for explicit test** and with fail-closed fallback, so the owner can A/B listen to the same level with the ESC menu open. Require (a) owner-identified live Theme/SXD (the previous logs give real banks `9:5→9:7` and `9:5→9:10`, but not the audible song identity), (b) a confirmed menu opening and independently identified upstream script origin, (c) explicit foreground/focus treatment, and (d) no resurrected driver generation after Stop. Each of the two original Theme/SXD driver routes must be accounted for. Always preserve default runtime until actual tests pass.

**Additional cross-check for soundtrack identity:** the independently published [Ray2Get](https://github.com/Synthesis/ray2get) documents that PC level background music is sequenced from `.apm` audio files and supplies a [per-level music-sequence map](https://github.com/Synthesis/ray2get/blob/master/file_sequences.txt). This can assist identification/subjective A/B validation, **but does not by itself map the owner's original CD Theme bank/index to a particular live `.apm` file**, nor does it prove that Dreamcast uses identical music content. We want the *existing* PC level music to continue at its current position, not to substitute console soundtrack files.

## Next prototype gate: confirm *current* native Theme before audio intervention

Independent examination of the **owner's original protected CD executable**, not only mod references, exposed a stronger evidence path for identifying level music. At **`0x459760`**, the native function called `dbgSND_fn_bGetThemePosition` in independent [OpenRayman research](https://github.com/huntyh/OpenRayman/blob/master/docs/formats/sound.md) first reads the active Theme-player flag **VA `0x4B95B8`**; if active, it returns the player's **current Theme lookup reference VA `0x4B95CC`** (plus index/repeat fields). At **`0x45997B`** the original code compares the incoming requested Theme reference with this same current reference to decide whether to reuse a live Theme or transition to a new one. **Three exact machine-code sequences were manually verified byte-for-byte in both the original CD and the modified reference**, then added to [SHA-pinned source parity checks](../tools/check_rayman2_cd_audio_parity.py) and [synthetic anchor tests](../tests/test_audio_cd_parity.py).

The optional research-only `RUNTIME-AUDIO-THEME-OWNER` trace now adds **`native_theme_player_key_matches_outer=-1|0|1`**. When the native player is active and the containing BNM bank and 92-byte record index are both uniquely established, the trace compares `[0x4B95CC]` against **`(actual_loaded_bank_id << 16) | native_resource_index`**. Equality provides **strong independent evidence that the Theme associated with the paused SXD is the player's *current* requested Theme**, rather than a background/UI resource merely owning an audio handle. An unavailable value remains `-1` (unknown), and a mismatch is `0`; the live values are **not yet measured in Wine**. The [offline trace analyzer](../tools/analyze_rayman2_audio_trace.py) separately reports `current_native_theme_player_matches_loaded_owner_events` only for observed equality, never infers the result for old logs. This still does **not** prove the decoder produces audible output or that the user hears the currently requested track; those are independent final integration checks.

The separate [ESC music shadow reconciler](../tools/esc_music_shadow_reconciler.py) models a **logical** pause/resume state per Theme+SXD stream generation, with independent ESC, focus and other-native pause reasons, quarantine on unclassified causes, and unconditional native Stop forwarding. It collapses two Theme/SXD callbacks for a single physical stream into one logical desired state change. [Its tests](../tests/test_esc_music_shadow_reconciler.py) include a failure case from the earlier per-record mark experiment and enumerate **4,096** short mixes of ESC/focus/other pause/Stop to check safety invariants. **These are behavioral model tests, not a functioning audio adapter.** Specifically, real `PauseTransitionSxd`/`ResumeTransitionSxd` call-count semantics in the installed APM mixer DLL are not yet verified; do not guess whether one pause or two physical driver calls are needed. The current model explicitly requires `verified_driver_adapter=True` before adopting a music stream. The normal launcher/runtime and physical game playback remain unchanged.

**Immediate work remaining for an audible experimental build:** (1) verify a live Theme-player match for the known loaded Theme controller; (2) anchor the menu-script event upstream, since the generic opcode `0x1AA` is not an ESC-only signal; (3) confirm the audio mixer pause/resume counting semantics, particularly double delivery via outer Theme and inner SXD; (4) wire the shadow policy to one *test-only* x86 adapter, retaining native SFX, focus/Stop, crash safety and the original default. Do not publish an audio-enabled build until these three validations and a real Wine/Windows gameplay test are passed.

## Scope and provenance

- Source: owner-supplied `rayman2(4).exe`, PE32 / x86, image base `0x00400000`, SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`, size **1,468,928 bytes**.
- This binary is **not** the approved original, protected 676,352-byte CD `Rayman2(3).exe` (SHA-256 starts `7927a106`). It is a **static reference only**. RVA matches in historical approved-CD runtime logs help constrain research but do **not** authorize binary patching.
- Tools: `objdump -d -M intel`, `objdump -p`, and Python's built-in `struct`, running locally. All addresses below are x86 image VAs, not file offsets. For this image's `.text` and `.data` the file offset equals `VA-0x400000`.
- The existing optional `RAYMAN2FIX_AUDIO_TRACE` build changes no audio arguments, return values, or playback behavior. The normal build does not include its hooks.


## Independent retail-CD byte-for-byte parity audit (13 November 2026)

The **owner's authentic protected original CD** and modified GOG-derived reference were both recovered from the owner's private file collection **for local offline reading only**:

- Approved CD: `Rayman2(3).exe`, **676,352 bytes**, SHA-256 `7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`.
- Research reference (not approved for runtime): `rayman2(4).exe`, **1,468,928 bytes**, SHA-256 `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`.

A fresh independent checked **PE32 VA → raw offset** reader was used, because the CD `.text` starts at file offset `0x400` whereas the reference starts at `0x1000`; both map to virtual address `0x401000`. The **13 selected audio-code intervals totaling 11,520 bytes** are **identical byte-for-byte**. They cover Win32 focus pause/resume callers, VM script sound-pause/resume callers (not menu-specific), their shared wrappers, native 64-record pause/resume dispatcher, type-11 Theme lifecycle, resource-type dispatch, sound-record allocation, SXD transition playback/wrappers, bank loader/resource resolver, bank-registration command opcode handler, event creation and event dispatcher. The checked intervals are **selected**, not claimed to be exhaustive for all audio code.

In both binaries, **seven** exact `E8 rel32` call sites resolve to the same intended target:

| Call origin | VA | Target |
| --- | --- | --- |
| Win32 window deactivation | `0x401C9C` | `0x4536D0` |
| Win32 activation | `0x401CE9` | `0x453700` |
| Generic script opcode `0x1AA` (pause) | `0x471E70` | `0x4536D0` |
| Generic script opcode `0x1AB` (resume) | `0x471E67` | `0x453700` |
| Shared pause wrapper | `0x4536ED` | `0x4580B0` |
| Shared resume wrapper | `0x45371D` | `0x458110` |
| Bank filename formatter call | `0x453948` | `0x453870` |

Nine machine-code anchors for native pause-mark/depth ordering and the **type-11 jump-table entry** `0x457FC8 → 0x457F21` match independently in both CD and reference. Crucially, the CD bytes directly prove the order **write per-record pause mark, dispatch driver pause, increase global depth** and **decrease global depth, clear mark, dispatch driver resume**. This upgrades the earlier focus-loss counterexample from reference-only static reasoning to **original-CD code-verified reasoning**. It still does **not** show that an unsafe patch has ever run or that a particular soundtrack was audible.

**New retail-CD bank-name proof:** the original CD also contains exact NUL-terminated `bnk_%d.BNM` at VA `0x4A252C`. Its untouched native loader stores `lId` to `[esi+4]` at `0x453945` and passes the same value to the formatter at `0x453948`. Thus this code's numeric bank-ID to decimal filename formatting is **verified on the approved retail-CD executable**, not merely on the GOG-derived reference. For example, bank ID **73** formats to **`bnk_73.BNM`**. No genuine extracted `bnk_73.BNM` file has been inspected, so the file's actual existence, contents, Theme ID `73:48`, relationships to `.APM` assets and audible musical identity are still **unverified**.

`.rdata` also matches across the two files at **12,288 on-disk bytes**. But `.data` differs at **127 bytes in 16 runs**, `.text` differs elsewhere, and their PE sections/entrypoint-related material differ. **This is NOT whole-program equivalence**; unchanged code does not prove identical runtime game state or licensing/protection behavior.

The reproducible, **read-only SHA-pinned** tool is [`tools/check_rayman2_cd_audio_parity.py`](../tools/check_rayman2_cd_audio_parity.py); synthetic PE fixture tests are [`tests/test_audio_cd_parity.py`](../tests/test_audio_cd_parity.py), included in Linux CI. Example **local** use with personally owned EXEs:

```bash
python3 tools/check_rayman2_cd_audio_parity.py /path/to/Rayman2-original-CD.exe /path/to/Rayman2-reference.exe
```

The tool refuses unrecognized SHA pairs by default, prints **no protected code bytes** and does not need or embed either executable in CI. The normal game DLL and launcher remain **unchanged**.

**Impact on future ESC music feature:** core static native pause semantics and Theme ownership mechanism are now grounded in original-CD code. The remaining blockers are **real bank/Theme musical attribution** and an actual **origin-aware, stop-safe reconciliation** of original record pause marks and global depth with desired music state across ESC + Alt-Tab + level transitions. Never suppress driver calls in isolation; stop and resource replacement remain native. F1 stays excluded; `MusicInPauseMenu` stays disabled/unexposed.


## Retail-CD correction: a script audio pause is NOT proof of the ESC menu

**Critical origin classification correction (13 November 2026).** The native function `0x471E40` is **not** an ESC-key or pause-menu callback. Direct original-CD disassembly and the independently identical reference show:

```text
0x482690: lookup handler via [0x4A43E0 + opcode*8]
0x4A5130 (opcode 0x1AA): handler 0x471E40, metadata DWORD 0
0x4A5138 (opcode 0x1AB): handler 0x471E40, metadata DWORD 0
0x4A5140 (opcode 0x1AC): handler 0x471E40, metadata DWORD 0

0x471E40: ESI = second script parameter; EAX = [ESI-8]
0x471E48: compare/subtract 0x1AA
0x471E70: opcode 0x1AA -> global sound pause wrapper 0x4536D0
0x471E67: opcode 0x1AB -> global sound resume wrapper 0x453700
0x471E5B: opcode 0x1AC -> unrelated native sound call 0x453080
```

The three consecutive eight-byte dispatch entries share one opcode decoder; `0x482690` proves that the VM indexes the function-pointer table with the **opcode number**, not with an ESC event type. We do **not** know from these instructions which gameplay state, UI menu, cinematic script, engine event or command sequence emitted a given opcode. A user's observed ESC interaction during a diagnostic run is **external context**, not a property of opcode `0x1AA` or of the `game-script-pause` / `game-script-resume` log origin classifier.

**Safety implication:** the previously considered trigger `audio_origin == game-script-pause` is **insufficient to enable music in the ESC menu** and must never be treated as `esc_menu_verified`. It can identify a plausible VM origin, but the same generic opcode may be used in other scripted situations. Raw `GetAsyncKeyState(VK_ESCAPE)` is equally insufficient; holding or tapping ESC cannot prove that the in-game menu is actually open. The independent Win32 pause branch may also run while scripts are active. A future opt-in intervention requires a separately proven, read-only identification of the **real ESC menu lifetime**, independent of F1, other dialogs, cutscenes and Win32 activation. On ambiguity, preserve all native audio calls unchanged.

The exact VM opcode decoder instructions, three table entries, branch call targets and both pinned binary fingerprints are now verified in [`tools/check_rayman2_cd_audio_parity.py`](../tools/check_rayman2_cd_audio_parity.py). Synthetic [`tests/test_audio_cd_parity.py`](../tests/test_audio_cd_parity.py) deliberately asserts that generic script origin does **not** constitute menu evidence. **No actual game audio behavior has been modified.**


### Rejected ESC-menu detector candidate: `ISPAUSEDEVT`

A possible-looking original-CD string, **`ISPAUSEDEVT`** at **`0x49F020`**, is **not** evidence of an in-game menu/UI boolean. Its only observed static literal references occur at `0x40129B` and `0x401480` (in both files), respectively in two Win32 event-handle setup paths:

- At `0x40129B`, the caller supplies the `ISPAUSEDEVT` name to imported **`OpenEventA`** through IAT `0x49C0E4` (`EVENT_ALL_ACCESS=0x1F0003`, inherit flag 1). The return value is stored as a handle, not a menu state.
- At `0x401480`, the caller supplies the same name to imported **`CreateEventA`** through IAT `0x49C01C` (loaded into ESI at `0x401432`), with standard security/manual-reset/initial-state parameters. The return value is also stored as a handle.

This identifies a **named Windows kernel synchronization event**, not whether the ESC menu is currently visible. The spelling alone would have been a dangerous false-positive basis for an audio feature. No safe native ESC-menu visibility field was established from this string. A separate UI-lifetime proof remains necessary; the desktop window title `Rayman2 - pause` is similarly not sufficient without proof of which application states set it.

## Reproducible offline audit of the owner's corrected schema-2 Wine trace

A new read-only [`tools/analyze_rayman2_audio_trace.py`](../tools/analyze_rayman2_audio_trace.py) summarizes sampled driver events, attaches the corrected `RUNTIME-AUDIO-THEME-OWNER` lines to their matching **(channel, sample)** trace lines, and emits only aggregate evidence (no source filename, raw driver handle, stack address, full log lines or game assets). It is exercised in Linux CI by [`tests/test_audio_trace_evidence.py`](../tests/test_audio_trace_evidence.py) using wholly synthetic records. For a locally owned log:

```bash
python3 tools/analyze_rayman2_audio_trace.py "/path/to/Rayman2Fix.log"
```

**Independent offline hand-inspection of `Rayman2Fix(20261113-171247).log`:** 48 sampled `RUNTIME-AUDIO-TRACE: event=...` records, including 16 `StopSxd`, 14 `PauseSxd`, seven `ResumeSxd`, five `StopTransitionSxd`, four `PauseTransitionSxd` and two `ResumeTransitionSxd`. Eleven `RUNTIME-AUDIO-THEME-OWNER` lines accompany transition events. In the intervals already cross-checked with the real native CD code:

- **87,431–87,432 ms:** two `game-script-pause` transition driver calls, depth 1, one Theme outer `73:48` (slot 6), inner `73:50` (slot 1), correctly decoded native sound-record pointer.
- **106,431–106,432 ms:** two `game-script-resume` transition driver calls with the same Theme and inner resource, depth 1.
- **133,663–133,664 ms:** another pair of `game-script-pause` calls, outer Theme **still `73:48`**, but inner resource now **`73:51`**.
- **138,032 ms** and **141,667 ms:** distinct owned `StopTransitionSxd` events **at native global depth 2**, first on Theme `73:48` / inner `73:51`, then Theme `89:90` / inner `89:81`. Both must remain intact across any future pause feature.
- All reported `foreground` fields are **1**. There is **no sampled `foreground=0`** event and thus no independent Wine evidence for an Alt-Tab cycle. `esc_down=1` occurs during the script-origin pause and resume samples, but the **generic VM opcode alone plus key state is still not proof of in-game ESC-menu visibility**.

This report distinguishes **recorded driver calls** from **inferred Theme controller ownership**, **correlated same-ms duplicate candidates**, and **untested UI/focus conditions**. It never infers the specific song from an ID or treats a script-origin pause as a confirmed ESC menu. Log tracing is sample-capped; missing records cannot establish that a type of event never happens.

**No new Wine run or build of a patched runtime is required for this offline verification.** The original game DLL, launcher, INI, native audio semantics and the PR's draft state are unchanged.

**Append-only trace session safety:** the runtime writes its `Rayman2Fix.log` using `FILE_APPEND_DATA`; a log may contain multiple game launches, with the same `sample=1` numbering repeated per audio channel. The offline analyzer now creates a new **session ID** at each genuine `RUNTIME-AUDIO-TRACE: enabled=1 schema=2` header. Theme owner lines are attached only within that session, duplicate driver-event pairing is restricted to one session, and even observations of inner-resource changes are scoped per session. Events that precede the first header belong to an explicitly unlabeled session `0`, not silently to the next launch. This avoids manufacturing cross-session pause pairs, owner matches or false resource changes. New synthetic tests cover two appended launches with matching sample IDs/timestamps, a missing owner, and a headerless prefix. The owner's historical 17:12 log had one schema-2 header, so this is primarily protection for later appended logs. More detail on how a single paused Theme resumes across focus and script events still requires independent live evidence.


## Original-CD in-game menu flag candidate (read-only, ESC and F1 still unresolved)

A more relevant native state candidate has been found in the public, third-party reverse-engineered [ACP_Ray2 `src/AI/AI.c`](https://github.com/raytools/ACP_Ray2/blob/master/src/AI/AI.c): the byte pointer **`AI_g_bInGameMenu = 0x4B9BC8`**. Its original-CD native code was compared with the owner-supplied static reference. These are **real code relationships on both the owner's original protected CD and the non-approved reference**, not guesses from naming.

- **10 identical little-endian pointer references within `.text`**, all with verified native instructions. The only two direct stores are **initialisation to zero** at `0x40376A` (`mov byte [0x4B9BC8],0`) and **a script-controlled boolean store** at `0x4730CC` (`mov byte [0x4B9BC8],cl`). There are **eight direct native reads**: `0x40B003`, `0x44B1C4`, `0x44B25D`, `0x44B319`, `0x44B336`, `0x44B3B6`, `0x46082D`, and `0x460EF2`.
- The script/VM dispatcher at **`0x4730A0`** checks opcode **`0x1F8`**; the eight-byte VM table entry at **`0x4A53A0`** points to that handler in both binaries. The native code calls `0x480B90` to evaluate the command's parameter, normalizes it to a boolean with `test ecx,ecx; setne cl` and stores it to `0x4B9BC8`. Therefore the flag is **actively set by game scripts**, not a mere input-key latch or HWND.
- The separate **engine-timer pause byte** is at **`0x500640`**, set to 1 at **`0x405CC5`** and cleared at **`0x405CE5`**, with getter `0x405D00`. It belongs to ACP_Ray2's named `GAM_fn_vSetEngineInPaused`/`GAM_fn_vResetEngineFromPaused`, **not** `AI_g_bInGameMenu`; the engine pause flag can be modified by an independent Win32 path. Never treat it as the ESC menu flag.
- The UI-looking literal **`Rayman2 - pause`**, at **`0x49F0A8`**, is passed to imported `SetWindowTextA` at **`0x401766`** and **`0x401A18`**, which operate on the **Windows window title**, not the game's displayed menu. These are the only literal title xrefs found in `.text`. It is another **rejected** ESC-menu state proxy.

**Additional original-CD rendering proof (independently matched in both PE files):** the **52 bytes from `0x40AFFE` through `0x40B031`** match byte-for-byte. At `0x40B003`, the engine loads the same native `AI_g_bInGameMenu` flag. When it is nonzero, it takes the branch at `0x40B00F`, pushes the separate `0x5004D4` viewport and calls `0x424440`. When it is zero, the `0x40B022` branch pushes `0x500458` and calls `0x429F70`. This is positive retail-CD **render-pipeline behavior** associated with the flag, rather than an unused variable or a pure audio switch. The read-only parity inspector verifies **all 47 bytes from `0x40B003`**, both direct call targets and their identical behavior in the reference. It is **not** evidence that the ESC screen is actually visible, and says nothing about F1. The native viewport switch is separate from generic script pause opcode `0x1AA`.

**Rejected second menu-state proxy (original CD + research reference):** the independent [Rayman2ModLoader variable map](https://github.com/kellsnc/Rayman2ModLoader/blob/master/Rayman2ModLoader/include/Rayman2Variables.h) names `GLI_g_hMenuBackgroundObject` at **`0x4A7250`**. In the original CD `0x40406A–0x404074` reads a graphics resource and **initializes** this handle, and `0x4245C8–0x4245DB` later **loads it and submits it to a graphics callback**. The same instructions appear byte-for-byte in the reference, and their only **two direct literal xrefs** (`0x404071`, `0x4245D0`) match. Consequently this is a **resource handle**, not evidence of the menu's current lifetime. In particular, nonzero `GLI_g_hMenuBackgroundObject` does **not** establish that the ESC menu is presently open. We do not add it to the runtime diagnostic or use it to trigger audio; the read-only CD inspector records it as a **negative candidate**.

**Independent modding corroboration, not native proof of an ESC-only flag:**

- [Original Ray2Fix `GliFixVf/fix.c`](https://github.com/spitfirex86/Ray2Fix/blob/master/GliFixVf/fix.c) draws its version text when `*AI_g_bInGameMenu` is true; another comment calls that version text the "pause menu version display". This corroborates the variable's practical association with an in-game menu.
- [Rayman2AP `src/mod.c`](https://github.com/Aeltumn/Rayman2AP/blob/main/src/mod.c) refers explicitly to the **escape menu** and, when the same variable is set, looks for the gameplay object named **`YAM_GenMenuMap_I1`**. It inspects that object's AI/behaviour state and menu entry arrays. This suggests a **second independent object-lifetime check** may separate a visible gameplay menu from generic scripted pause, but the object name's presence and lifetime **have not been independently verified on the owner's CD files**, and we do not know whether F1 shares this flag or object.
- [Twofold `Twofold/hooks.c`](https://github.com/spitfirex86/Twofold/blob/main/Twofold/hooks.c) independently shows the version string when either `*AI_g_bInGameMenu` or a main-menu-level flag is true, supporting the distinction between **menu in a level** and the separately loaded **main-menu level**.

**Original control documentation, independent of the PE and mod sources:** the [1999 French PC manual transcription](https://doczz.net/doc/5089030/01988-ray-2-fre-manual) assigns **ESC/Echap** to "Accéder au menu pendant le jeu" and **F1** to "Accéder au Savoir du Monde et rappeler Murphy"; the [Windows control reference](https://strategywiki.org/wiki/Rayman_2:_The_Great_Escape/Controls) independently makes the same distinction. **These are distinct documented gameplay functions**, supporting the decision to exclude F1 from this feature entirely. But manuals cannot prove their *internal* `AI_g_bInGameMenu` values or the `YAM_GenMenuMap_I1` lifetime. Further, the literal bytes `YAM_GenMenuMap_I1` and `GenMenuMap` are **absent from both supplied PE images**; game-level data would be needed to substantiate the exact actor name locally. This is not a failure of the menu-flag xref proof.

**Safety gate:** the known `AI_g_bInGameMenu` byte is **the first defensible native candidate**, much stronger than the generic script audio opcode `0x1AA`, raw Escape keyboard state, `ISPAUSEDEVT` or the Windows `Rayman2 - pause` caption. It still cannot yet authorize a music override: *ESC vs F1*, menu visibility timing, other gameplay UI, and the native two-record pause/stop/focus policy must be independently proven or treated as unknown. The [SHA-pinned parity inspector](../tools/check_rayman2_cd_audio_parity.py) now verifies all native xrefs and code anchors and explicitly reports **`ai_ingame_menu_does_not_prove_ESC_specific: true`**. Synthetic tests do not embed either game binary.

**Implemented optional read-only probe (no runtime audio changes):** the build-gated `RAYMAN2FIX_AUDIO_TRACE` variant now samples `module+0xB9BC8` (the native in-game-menu byte) and `module+0x100640` (the distinct engine-pause byte) in its existing approximately 500 ms poll. It checks the PE size, exact read lengths and boolean range, and logs `RUNTIME-AUDIO-UI-STATE` only when either value changes. These UI samples are **not a verified ESC vs F1 classifier** and polling can miss brief transitions. The dedicated [offline trace analyzer](../tools/analyze_rayman2_audio_trace.py) can aggregate these state changes; pure synthetic tests cover it. The production build does not enable this probe, and no sound arguments, stops, music, SFX, INI settings or engine state are modified. This provides a narrowly scoped research instrument for a future combined ESC/F1 + focus observation **only after the CI has passed**. The `YAM_GenMenuMap_I1` actor remains an externally supported *candidate* for stronger UI identity, not yet a native proof. **Additional same-build research-only evidence** now reads these two byte flags once **before every sampled original audio driver call**, preserves missing/invalid values as `-1`, and adds `ui_in_game_menu_at_call` / `ui_engine_paused_at_call` to that sampled `RUNTIME-AUDIO-TRACE` line. This is especially useful when a script's audio pause occurs before the VM changes the menu flag. The read-only offline analyzer emits call-time flag pairs for same-driver transition events and reports `-1` (unknown) for all older schema-2 logs that lacked these additive fields. No driver call is skipped, delayed, reordered or modified; the normal build and F1 behavior are untouched.


## Corrected nomenclature — three different classifications

Cross-checked against the public [ACP_Ray2 `SND_Resource.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Resource.h) and [`SND_Event.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Event.h):

1. **Sound *resource* type** (`SND_tdeTypeResource`): `[resource+4]` in `0x457E60` chooses the 11-way resource dispatcher. **Type 11 = `SND_E_TR_Theme`**, **type 10 = `SND_E_TR_Random`**, **type 8 = `SND_E_TR_Switch`**, **type 4 = `SND_E_TR_Sequence`**, and **type 1 = `SND_E_TR_Sample`**. This previously had the misleading name "event class" in the research notes.
2. **Sound *event* type** (`SND_tdeTypeEvent`): a separate enum (`SND_EVT_Son_Play=1`, `SND_EVT_Son_ChangeVolume=11`, etc.). It is **not** the 11-way resource dispatch described above.
3. **Driver transport**: `[native_descriptor+4]` in `0x458220` chooses the actual **sample/SXD resource type 1 versus MIDI type 2** for the inner stream. A Theme resource is itself not a third driver transport.

Theme parts are described by [`SND_Theme.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Theme.h): per-part resource, loop start/end, number of loops, transition mode (crossfade, end-of-part or outro), and fade duration. This is a *positive static identification* of the type-11 Theme controller. It does **not** alone establish which audible song or which nested sample plays at a given moment.

The jump table was decoded from the file, not guessed from contiguous disassembly:

```text
case 1..11 targets at 0x457FA0:
457E8F 457EAA 457EC5 457EE0 457F78 457F02
457F78 457F5D 457F78 457F42 457F21
```

## Confirmed transition creation chain

```text
0x457E60: dispatch resource type 11 = SND_E_TR_Theme
  -> 0x457F21
     -> 0x459950: single type-11 controller, base 0x4B95B8
        -> 0x458530: select/copy nested native sound descriptors
        -> 0x458220: allocate 64 x 32-byte sound-table entry
           -> 0x455280: wrapper around the dynamic x86 5-argument
              _SND_fn_lPlayTransitionExSxd@20 at slot 0x4B86F0
           -> 0x457570: retain native driver handle and descriptor
        -> 0x459AF4: store inner sound-record pointer at 0x4B95E8
     -> 0x457F92: save *outer* event/controller record (separate)
```

Critical distinctions:
- The game sound table begins `0x4B8C98` and has **64 entries of 0x20 bytes**.
- A table record stores the **driver handle at +0**, **game-native descriptor at +4**, **transition flag at +0x10**, and **active flag at +0x14** (see `0x4575D5–0x4575F0`). The handle is not a song ID, and equal handles do not entail equal source descriptors.
- `0x459950` maintains its type-11 controller at `0x4B95B8`. The nested sound-record **pointer** lives at controller **+0x30** (`0x4B95E8`); its address must be decoded to an index after checking the native 64 × 0x20-byte table bounds and alignment. This is structurally separate from the *outer* type-11 sound-table record. It is inappropriate to treat one fixed table slot as “music.”
- In the reference binary, `0x4597E0` explicitly clears the controller's active state and is called during native audio initialization from `0x457E2D`. Other lifecycle paths use indirect entry points and pointer writes, so *initialization is not proof of all later state transitions*.
- Direct `call` instructions to `0x457E60` occur at `0x452E63`, `0x4579E2`, `0x45A26D`, `0x45A6BE`, `0x45A7D6`, `0x45ADD8`, `0x45AFD2`. These seven upstream sites are the next useful static provenance targets; their event arguments need to be linked to an actual resource category.

## Offline resource-ID tracing — nested type-11 events

The type-11 **Theme** controller does not merely store an opaque driver handle. Its
source data has a variable number of nested entries and a reference-fixup
stage. This is a promising **resource provenance** path that can be mapped
without launching Wine, provided the actual resource databases are present.

### Packed sound-bank resource identifiers: `0x453F90`

The original x86 instructions at `0x453F90–0x453FC9` resolve a **DWORD
resource identifier**, with a guard for `0xFFFFFFFF`:

```text
packed_id (DWORD)
  upper 16 bits -> runtime resource-group key
  lower 16 bits -> record index in the loaded group

0x4538C0: SND_tdstSndBankDescriptorList at 0x514D50
           bank ID = [descriptor+4], next = [descriptor+0x3C]
0x453F90: resource_base = [[bank+0x24]+0x0C]
           resource_count = [[bank+0x24]+0x10]
           if index >= record_count: return NULL
           record_ptr = group_base + 92 * index
```

The **92-byte record stride** is obtained from real instructions,
`lea ecx,[esi+esi*2]`, `shl ecx,3`, `sub ecx,esi`,
`lea eax,[edx+ecx*4]`: `4 × (24 - 1) = 92`. The group ID is a key
in the **runtime sound-bank list**, **not** automatically a disc index, archive number,
CD track or song ID. No concrete packed ID has yet been recovered from an
actual level's resources.

### Type-11 resource fixups: `0x459840`

`0x459840` converts a disk-format Theme resource into controller-owned memory.
It walks a **24-byte-stride** collection of nested records, invoking
`0x453F90` on the first DWORD of each element (`call` at `0x459869`).
An optional secondary pointer from descriptor `+0x28` is independently
resolved by `0x453F90` at `0x459890`. The resulting pointers become
input to event playback and must not be assumed to identify a particular
`.apm` file.

This is a genuine path from the controller to **loaded resource records**,
but it still does not label a record as music versus sound effect. The
next required provenance link is the group's archive registration/loading,
which likely requires corresponding game resource files for confirmation.

### Native command stream registers runtime resource groups

The resource group IDs are *not* hardcoded as disc indices in the path
just traced. At `0x4507D5`, a native command-stream interpreter
distinguishes numeric opcode **0x5D** from **0x5E** (subtract 0x5D,
then decrement):

- The **0x5E** branch obtains one group identifier from
  `0x44F8D0` and calls `0x454050` at site `0x4507E3`.
- The **0x5D** branch reads a count and a list of group IDs through
  `0x44F930`, and calls the same `0x454050` at
  `0x450811` for each entry.
- `0x454050` forwards to `0x454060`, which searches the runtime
  group registry (`0x4538C0`) and creates missing registry records
  (`0x4538E0`) before the later per-record resolution.

This confirms how the packed ID's **upper 16 bits** can refer to a
runtime group registered from loaded command data. It does **not**
establish which `MUSIC*.DAT`, `SOUNDS*.DAT` or `.apm` asset
corresponds to that group: an actual group command and resource database
would have to be decoded, and the non-approved reference EXE cannot
replace retail-CD evidence.

### A third, distinct dispatcher: `0x458530`

Do not conflate resource type, resource-descriptor conversion, sound-event type, and native driver transport. The 11-byte conversion table at `0x4585A0` maps **resource**
types 1..11 to categories:

```text
resource type:  1  2  3  4  5  6  7  8  9  10 11
category    :  0  0  0  0  4  4  4  1  4   2  3
```

Category **3** used by type **11** returns the input descriptor at
`0x458589` unchanged. Type **8** selects `0x458564 → 0x45AA00`,
and type **10** selects `0x458550 → 0x45A4A0`. These conversion functions
can resolve nested descriptors within an already selected type-11 event.
Transport class **1=SXD / 2=MIDI** is a *separate* selector inside
`0x458220`, unrelated to the *outer* Theme resource's type number.

All static checks above have been added to the read-only JSON inspector
`tools/inspect_rayman2_audio.py` and its synthetic-only tests. A future
comparison with the approved retail-CD binary is needed before using the
addresses as patch locations.

## Cross-checked sound-bank data structures (ACP_Ray2)

The open-source [ACP_Ray2 sound module](https://github.com/raytools/ACP_Ray2/tree/master/src/SND) supplies the original CPA structure names and matches **three exact addresses in the supplied EXE**: `SND_g_stBankDescriptionList = 0x514D50` (`SND.c`), `SND_fn_lSendRequestSound = 0x452D20`, and the bank-registry instructions at `0x4538C0`. The first address is exactly the global read in the native bank lookup; the second matches the native function prologue. This is strong evidence that the named layouts match this *reference* binary.

**Loaded bank descriptor** `SND_tdstSndBankDescriptorElement` (72 bytes, allocated at `0x4538E2`):

| Offset | Name | Native check |
| --- | --- | --- |
| `+0x04` | `lId` | `0x4538CD` compares bank ID |
| `+0x0C` | `szFileName[13]` | Named bank filename within the descriptor |
| `+0x24` | `p_stData` | `0x453FB2` loads bank head |
| `+0x3C` | `hNextBrotherDyn` | `0x4538D2` advances linked list |

The loaded bank head `SND_tdstSndBankHead` contains its optimized **resource list first pointer at `+0x0C`** and **resource count at `+0x10`**. Native `0x453F90` reads those exact fields and selects one **92-byte** `SND_tdstBlockResourceMem` record. This resolves the previously unnamed “resource groups”: they are **sound banks**. The packed upper16 value is the loaded bank **ID**. The non-approved reference and the **original retail-CD EXE independently** confirm the native bank-ID filename formatter `bnk_<decimal bank ID>.BNM` (see the pinned byte-parity section). The bank files and their contents themselves have not been inspected.

**Loaded resource record** `SND_tdstBlockResourceMem` (92 bytes):

| Offset | Actual field |
| --- | --- |
| `+0x00` | `Id`, a 32-bit native record identifier, **not a pointer**; its upper/lower halves are **not independently verified** as that record’s BNM bank and table index |
| `+0x04` | `eType` (`SND_E_TR_Theme=11`, `Sample=1`, etc.) |
| `+0x08` | `eStorage` (external / megafile / bigfile) |
| `+0x0C` | `ucVolume`, one byte; next three bytes may be padding |
| `+0x10` | `bIsLoaded` |
| `+0x14` | `uRes`, union of in-memory resource definitions |

**Disk Theme descriptor**: [`SND_tdstBlockResourceDisk`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Resource.h) has its on-disk Theme union beginning at `+0x18`; the [`SND_tdstTypeThemeDisk`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Theme.h) has `ulNbParts` at union `+0x0C` (overall `+0x24`) and `lOffsetPartOutro` at union `+0x10` (overall `+0x28`). The native Theme fixup `0x459840` reads precisely these fields at `0x45985A` and `0x45987F`. Its collection elements are 24-byte `SND_tdstThemePart` entries.

### Corrected interpretation of the old diagnostic masks

The previous `desc_words_equal_mask` compares four words of **`SND_tdstBlockResourceMem`**, not four arbitrary pointers. A mask of `14` means **different native `Id` fields** but the same `eType`, `eStorage` and fourth DWORD. A mask of `6` means a different native **`Id` field** and fourth DWORD; that last difference might be the volume byte or only another byte in that DWORD. **A matching SXD driver handle cannot establish the same resource identity.**

An older research probe incorrectly attempted to dereference the first DWORD of the resource record as a memory address. This interpretation was invalid because it is `Id`; the probe was bounded and read-only, but any output from it should be ignored. It has been **removed**. The research probe now reports the **upper16/lower16 components of the record `Id` field** and the actual volume byte; those numeric components must **not** be promoted to a verified BNM filename or resource table index without the originating bank file. No audio calls are altered.


### Original-CD dual-route proof for the same inner SXD handle

A further independent read of the pinned original CD PE, crosschecked byte-for-byte against the reference, verifies **both possible routes** from the native 64-slot audio dispatcher to the same inner transition SXD driver:

```text
0x4580B0 pause loop, eligible inner transition record (+0x10 != 0)
    0x4580E1 -> 0x4584A0 -> SXD PauseTransition

0x4580B0 pause loop, eligible outer type-11 Theme record (+0x10 == 0)
    0x4580EF -> 0x458070
       native resource-type pause table [0x4A3BA8 + 24*11] = 0x459C40
       0x459C4D -> 0x4584A0 -> SXD PauseTransition on Theme's inner record

0x458110 resume loop, eligible inner transition record
    0x458151 -> 0x458470 -> SXD ResumeTransition

0x458110 resume loop, eligible outer type-11 Theme record
    0x45815F -> 0x458090
       native resource-type resume table [0x4A3BAC + 24*11] = 0x459C60
       0x459C6D -> 0x458470 -> SXD ResumeTransition on Theme's inner record
```

All **six E8-relative call targets** and both **type-11 function-table pointers** were checked in the original retail-CD bytes and in the non-approved reference. They have been added to the SHA-pinned, read-only [CD parity inspector](../tools/check_rayman2_cd_audio_parity.py). The prior Wine schema-2 trace reports one active outer Theme at slot 6 pointing to the unique inner transition driver at slot 1; the driver sees two ESC pause and two ESC resume calls at the same global depth. The native code graph therefore supplies a **specific sufficient explanation for duplicate calls when both records are eligible**, without assuming the handle is a fixed music slot. It **does not prove** from the log alone that both pathways were taken at runtime; the exact live per-record pause eligibility and full caller stacks have not been captured.

A new [eight-case research-only synthetic dual-dispatch model](../tests/test_audio_dual_dispatch_model.py) exercises two eligible records, one ineligible record, inactive Theme, extra unrelated SFX, zero initial depth, and nested ESC/focus. In the straightforward `depth=1` state, both records have `pause_mark=0`, so ESC can pause the *same* driver twice, setting both native marks to 1 and global depth to 2. An additional Win32 focus pause at depth 2 dispatches **no** further driver pause; its matching resume from depth 3 to 2 can resume the same driver **twice while ESC remains open**. The following ESC script resume at depth 2 → 1 can then issue **two more native resumes**, since those mark values were cleared at focus gain. This is a structural demonstration of why dropping or compensating one low-level call alone is unsafe. It neither runs the original game nor makes audio playback changes.

**Implementation guard:** a future opt-in feature must reconcile the **two original participating records** (both of their native marks, logical Theme lifetime, and independent ESC/focus reasons), never just intercept one `PauseTransitionSxd` by driver handle. Even a post-dispatch `resume` is insufficient unless a subsequent focus loss can force an actual driver pause despite the game's already-set marks and focus gain cannot resume it inadvertently. Preserve `StopTransitionSxd` and level transitions unchanged; no F1 changes, INI key, DLL hook, or default behavior changes.



### Retail-CD Theme callback transaction guard and direct caller inventory

A new independent disassembly of the **approved CD** `Rayman2(3).exe` checks the mutable global `0x4B9618` (located in the zero-filled virtual data range, **not** file-backed at this VA). Exactly **six literal DWORD operands in the CD `.text`** reference it; the modified reference has the **same six operands at the same VAs**:

| Native VA | Operation | Source-backed interpretation |
| --- | --- | --- |
| `0x459694` | `mov [0x4B9618], edx` with `edx=0` from `0x459678` | Clear during initial controller data initialization for the no-parts branch |
| `0x4596E0` | `mov eax,[0x4B9618]; test eax,eax; jne 0x459751` | While the flag is nonzero, return before the callback's part-advance work |
| `0x45999D` | `mov dword [0x4B9618],1` | Mark beginning of a Theme start/replacement operation |
| `0x459B2C` | `mov dword [0x4B9618],0` | Clear at the end of that start operation |
| `0x459BE5` | `mov dword [0x4B9618],1` | Mark beginning of the Theme stop path |
| `0x459C23` | `mov dword [0x4B9618],0` | Clear after the stop has run and the controller first DWORD has been set to zero |

The address of the guarded callback `0x4596E0` is passed to `0x458220` at **`0x459AE5`**, whose creation path calls the SXD transition player. This strongly supports **a native callback-inhibition mechanism during Theme creation/stop**, not an arbitrary unused field. Importantly, **it is NOT a thread-safe lock, a guarantee of audio silence, or a reliable direct proxy for pause-menu state**. It may be accessed indirectly by code paths not found by literal operand scanning; the scan does not establish all possible callers or threads. The process's live value has not been observed.

Further, scanning every `E8 rel32` byte pattern in the CD `.text` and independently in the research reference returns precisely the following *direct-call candidates* for these six specific target functions:

| Target | Exact E8 site VAs in both images |
| --- | --- |
| Shared pause wrapper `0x4536D0` | `0x401C9C` (Win32), `0x471E70` (script) |
| Shared resume wrapper `0x453700` | `0x401CE9` (Win32), `0x471E67` (script) |
| Global pause loop `0x4580B0` | `0x4536ED` |
| Global resume loop `0x458110` | `0x45371D` |
| Native inner SXD transition pause `0x4584A0` | `0x4580E1` (inner) and `0x459C4D` (Theme delegated) |
| Native inner SXD transition resume `0x458470` | `0x458151` (inner) and `0x459C6D` (Theme delegated) |

These are **literal five-byte E8 call candidates**, cross-checked against disassembly at each known site. They are **not** a complete interprocedural call graph, and they **cannot exclude indirect calls** or custom return sites. They do establish that the two independently verified upstream callers to the shared wrapper are the script and Win32 origins, without relying on `GetAsyncKeyState(VK_ESCAPE)` as a menu detector.

The original CD's `.data` explicitly initializes native global pause depth `[0x4A3CC8]` to **1**, equal to the reference. That matches the runtime values at owner ESC events but is not sufficient to classify arbitrary pauses.

The original owner trace also shows the **same outer Theme ID (73:48) with different inner resources (73:50 → 73:51)**. An implementation that treats every inner sample change as a *new* soundtrack would incorrectly lose the logical Theme identity; an implementation that caches only the recycled SXD handle could accidentally resume a successor after stop. A future generation token must therefore distinguish *outer Theme creation/stop* from *inner part replacement*, and never compensate/forward a stale driver handle across either kind of lifecycle change.

The SHA-pinned [retail-CD audio parity tool](../tools/check_rayman2_cd_audio_parity.py) now has explicit checks for all six flag sites, callback registration, and the six sets of known E8 caller sites. Pure synthetic tests cover E8 scan, VA mapping, flag operands, and the important distinction between **selected-region equality** and **complete native-audio proof**. The first new synthetic CI run failed due to incorrectly requiring genuine CD transaction anchors even with `check_anchors=False`; this test-harness bug has been corrected without weakening real pinned-CD checks, and CI rerun. No actual game patch, audio hook, INI option or soundtrack file is introduced.


## Native pause/resume dispatch — verified indirection

`0x4580B0` is the game's global **pause** loop over 64 sound-table records. It increments nested depth at `0x4A3CC8`. `0x458110` is the inverse **resume** loop. Records whose `+0x10` transition flag is nonzero are dispatched through `0x4584A0` / `0x458470`, where SXD uses `0x4553A0` / `0x4553C0` and their dynamic slots `0x4B8708` / `0x4B870C`.

Records without the transition flag use `0x458070` / `0x458090`, which use native tables `[0x4A3BA8 + 24*event_type]` and `[0x4A3BAC + 24*event_type]`. For **event type 11** these table entries resolve to:

| Operation | Function | Effect |
| --- | --- | --- |
| Pause | `0x459C40` | If controller state is active, reads nested record at **controller +0x30**, then calls `0x4584A0` |
| Resume | `0x459C60` | If active, reads controller +0x30, then calls `0x458470` |

**Important architectural finding:** when both an inner transition record and its outer type-11 controller record participate in a native pause, two separate code paths can reach the *same* driver `PauseTransitionSxd`/resume entry. This is a concrete explanation **candidate** for the repeated calls to slot 1 observed at the same millisecond in Wine. It is **not fully proven as the cause**: the controller pointer and the true executed call stacks still need to be verified.

**Diagnostic correction:** the earlier `inspect_audio_sequence_context()` returned an initially-zero `AudioSequenceContext` whenever the controller DWORD was **not exactly 1** (`active!=1u`). The native `0x459C40` callback instead checks only **nonzero** (`cmp [eax],0; je ...`). Therefore the prior `sequence_active=0` log field **did not establish** the native DWORD was zero: an alternate nonzero state would have been silently reported as inactive. The optional research build now categorizes the state as `sequence_state_class` (0=unreadable, 1=zero, 2=one, 3=other nonzero), with `sequence_active` determined by nonzero state and a validated aligned record pointer. Existing Wine traces cannot reconstruct which class applied retrospectively; do not use them to claim the controller was idle.

## Exact nested script / focus-loss audio state machine

The code-level reason a naive "drop the Theme pause call" override is **not safe** is now independently reproduced without Wine. In the **non-approved reference EXE**:

- The **generic VM-script pause opcode 0x1AA** (`0x471E70`) and the Win32 window-event pause branch (`0x401C9C`) call the *same* `0x4536D0` wrapper, which calls global `0x4580B0` at `0x4536ED`. The VM opcode is **not in itself proof of ESC menu state**.
- The **generic VM-script resume opcode 0x1AB** (`0x471E67`) and the Win32 window-event resume branch (`0x401CE9`) call `0x453700`, which calls global `0x458110` at `0x45371D`.
- The global pause counter lives at **`0x4A3CC8`**. Each 32-byte sound-table record has `active` at **+0x14**, a separate skip/state field at **+0x08**, a transition flag at **+0x10**, and the **per-record pause mark at +0x1C**.
- `0x4580B0` loops over **64** slots. For every active record with `+0x08 == 0` and `pause_mark == 0`, it **stores the old global depth into +0x1C BEFORE dispatching** the native pause. It then increments global depth **after** walking all slots.
- `0x458110` first **decrements** global depth, then on active records whose `+0x08 == 0` and `pause_mark <= new_depth`, **clears +0x1C BEFORE dispatching** the native resume. This order is visible at `0x458116`, `0x45813C` and `0x458143`.

Narrow pseudocode for **one slot** (not a complete emulator of the game's audio subsystem):

```python
def native_pause(slot, depth):
    if slot.active and slot.other_state == 0 and slot.pause_mark == 0:
        slot.pause_mark = depth
        native_driver_pause(slot)  # nested Theme / SXD dispatches still apply
    return depth + 1

def native_resume(slot, depth):
    depth -= 1
    if slot.active and slot.other_state == 0 and slot.pause_mark <= depth:
        slot.pause_mark = 0
        native_driver_resume(slot)
    return depth
```

### Verified counterexample to a driver-only ESC bypass

Take a participating Theme-related record with **global depth = 1** and **pause mark = 0**, matching the *starting global depth* reported in the owner's older ESC traces:

| Operation | Depth before → after | Pause mark | Native driver pause request |
| --- | --- | --- | --- |
| ESC opens | 1 → 2 | 0 → 1 | **Yes**; a naive patch would drop **only this request** |
| Window loses focus | 2 → 3 | 1 → 1 | **No request**; record already marked |
| Window regains focus | 3 → 2 | 1 → 0 | **Native resume may be requested**, even though ESC remains open |

Thus a patch that merely swallows `PauseTransitionSxd` during ESC **can leave audio playing after Alt-Tab**, because the game records the pause as completed independently of the driver. It can also create a spurious resume at focus gain. There is an additional edge case at **depth = 0**: the first mark written is **0**, so the next nested pause can still invoke the driver a second time. Both cases are covered by a pure test model in [`tests/test_audio_pause_invariants.py`](../tests/test_audio_pause_invariants.py), with direct call-site, opcode and branch-order verification integrated into [`tools/inspect_rayman2_audio.py`](../tools/inspect_rayman2_audio.py). The model is a logical counterexample based on static code, **not a report that sound was actually heard after Alt-Tab**.

### Cross-check with existing Wine traces: two calls, one depth

The old owner logs, without any additional Wine test, corroborate the structural need to account for two possible pause paths. At the first ESC opening in `Rayman2Fix(20261113-091415).log`:

- `PauseTransitionSxd` at **45.605 s** and **45.606 s**, both `audio_origin=game-script-pause`, `sound_slot=1`, `sound_matches=1`, and `global_pause_depth=1`.
- The corresponding `ResumeTransitionSxd` calls at **55.988–55.989 s** both show `global_pause_depth=1`.
- Two later script-origin pause calls at **72.027–72.028 s** also have the same depth, slot and handle match; the native source-id comparison mask is `6` versus earlier `14`.

Because the global routine at `0x4580B0` increments depth **only after its full 64-record loop**, two identical driver calls observed at the same depth are **consistent with two routes being invoked by the same global operation**, not necessarily two separate calls to that global routine. The disassembly provides **two possible routes**: directly through the inner transition record and indirectly through the outer `SND_E_TR_Theme` record's controller callback `0x459C40`. The outer record has a distinct game-side handle, so `sound_matches=1` for the *driver* handle does not preclude two paths referring to it.

This remains a **constrained hypothesis, not proof of the executed callback stacks**, because the old trace sampled the driver interface, not both of its higher-level callers. The earlier `sequence_active=0` value is also ambiguous because its instrumentation only accepted a raw active state equal to exactly one. Do not turn this hypothesis into a bypass rule.

### Design requirements that follow

1. Establish the actual **Theme resource ID / bank ID** and current nested sound instance before any exclusion; an SXD transport, driver handle or sound-table index is insufficient to identify music.
2. Track **script pause** and **Win32 focus pause** as independent *causes*. Do not infer focus from ESC-key state; both invoke the same native wrappers but have distinct upstream return sites.
3. Preserve each legitimate **Theme stop, transition and level unload**, allowing driver handle recycling without stale overrides.
4. Preserve game sound-table and global depth consistency. If any Theme audio is deliberately kept running during a script pause, focus loss must explicitly suspend it and focus gain must restore it *only when the gameplay/menu state allows*.
5. Unknown resource identity or unverified focus-origin must **fall back to original audio behavior**. No experimental setting or driver-bypass is justified yet.
6. Verify the exact original retail-CD instructions independently before any hook design is merged, because these static VAs were observed in a non-approved reference EXE.

### Theme controller lifetime is explicitly gated

The direct x86 lifecycle of the **Theme singleton**, base `0x4B95B8`, is now verified in addition to its nested-slot `+0x30` dispatch:

| Operation | Native address | Result |
| --- | --- | --- |
| Initialization `0x4597E0` | `0x4597EC` | Sets `[0x4B95B8]=0` |
| New inner stream | `0x459AF4` | Stores its sound-record pointer at `0x4B95E8` |
| Start success check | `0x459B06` | Compares the stored record pointer with the `-1` sentinel |
| Successful Theme activation | `0x459B0E` | Sets `[0x4B95B8]=1` |
| Theme stop | `0x459C0D` | Sets first controller DWORD back to `0` |
| Theme pause / resume | `0x459C44`, `0x459C64` | Dispatches its nested sound only when controller first DWORD is **nonzero** |

The code therefore provides an ordinary zero/one lifecycle for these direct paths, but this does not prove indirect writes, alternate states or the value during an owner's historical ESC session. The previous diagnostic accepted only `state==1`, so its `sequence_active=0` result could not distinguish native zero from any other non-one value or a failed record read. The corrected research probe and static inspector leave that uncertainty explicit.

This *also* means it would be unsound to infer that **every** SXD transition pause is music or Theme-related. The actual active Theme resource/inner stream relation must be validated separately; an SXD driver handle alone is not an owner identity.

## Owner Wine observations already available

From `Rayman2Fix(20261113-091415).log`:

| Time (s) | Observed event | Transition handle | Description word equality |
| --- | --- | --- | --- |
| 6.357, 7.449 | `StopTransitionSxd` | Last played matches | `15`, all four words |
| 45.605–45.606 | Two `PauseTransitionSxd` calls, script-origin ESC | Last played matches | `14`: word 0 differs |
| 55.988–55.989 | Two `ResumeTransitionSxd` calls, script origin | Last played matches | `14` |
| 72.027–72.028 | Two further script-origin pause calls | Last played matches | `6`: words 0 and 3 differ |
| 75.529, 78.930 | Transition stop after second ESC | Last played matches | `6` |

The equal-descriptor mask is an *observational comparison of four 32-bit fields*, **not** a music classifier. No confirmed `windows-activation-pause` or loss-of-foreground audio event has been observed yet. Even successful correlation of the driver handle and source descriptor does not identify which audible track the instance represents.

## Archive table, not yet resource attribution

The static 24-pointer archive table at `0x4A2090` has three groups of eight. For discs 0, 1, 2, `SOUNDS*.DAT` occurs at indices **4, 12, 20** and `MUSIC*.DAT` at **5, 13, 21** (zero-based). These pointers establish archive *names*, not a direct connection to type-11 events. Indirect code reads of the base pointer at `0x44DE9D`, `0x44EB6C` and `0x44F13A` refer to the first archive entry, `LEVELS0.DAT`, rather than MUSIC archives. The reference EXE contains additional `.extra` sections that may be GOG-specific; do not interpret them as approved-CD filesystem behavior.

## Separating archive metadata from playable music

Public [Ray2Get documentation](https://github.com/Synthesis/ray2get) identifies the Rayman 2 PC **soundtrack itself** as sequences of `.apm` files that can be reconstructed/replaced per location (see its `file_sequences.txt`). A [community extraction discussion](https://raymanpc.com/forum/threads/does-anyone-know-how-to-rip-the-sound-files-from-the-pc-port-of-rayman.74978/) places these APM/BNM sound resources under `Data/World/Sound`. These are independent, external format descriptions, **not proof** that this binary's type-11 sound event was loaded from an APM, BNM or `MUSIC*.DAT` archive.

The three named `MUSIC*.DAT` entries in the *level archive directory* therefore should be treated as likely *metadata candidates*, not assumed to be the actual compressed waveform files. The next safe static question is how the event controller's descriptor is allocated and which resource identifiers it carries; do not substitute the filename-based music list for a runtime identity.

## File-path manifest corroboration (non-CD extra section only)

A direct bounded scan of the supplied `rayman2(4).exe` **`.extra2` section** finds **1,284 distinct `DATA\\WORLD\\SOUND\\*.APM` paths** and **405 distinct `*.BNM` paths**. All BNM entries are exactly `BNK_0.BNM` through `BNK_404.BNM`, with no gap. This extra section is **not present in the reference retail-CD assumptions**; its file-path entries establish filenames for a modified distribution, **not actual loaded audio bytes**, bank IDs, or corresponding original-CD files.

These path names independently overlap with the publicly documented [Ray2Get sequence list](https://github.com/Synthesis/ray2get/blob/master/file_sequences.txt) (2018): the main-menu music loops through `MM01` ... `MM10`, all of which appear in the extra2 inventory, and the introductory `JailMus` sequence corresponds to an available `JAILMUS.APM` filename. The inventory also contains `MAIN TITLE.APM`. This is a **filename cross-check**; it is not evidence that a specific Theme resource or driver handle plays a particular sequence during ESC.

A second inspection of the same section establishes its **complete binary record format**, not merely an ASCII filename search. It consists of repeating `<u32 declared_size><u32 path_length_including_NUL><ASCII filename><NUL>` entries, followed by a sentinel pair **`(0xFFFFFFFF, 0)`** and zero padding. A bounded sequential parser validates **3,716 entries**, **3,716 unique paths**, **1,284 APM** and **405 BNM** filenames. The remaining entries include unrelated game, driver and system assets. The sentinel appears at section-relative offset `0x22066`; the trailing `3,986` bytes are zero. These are all reproducible facts about **this modified reference EXE's** `.extra2` directory.

**Critical size caveat:** the sum of the *declared* sizes is **7,453,062,614 bytes (~7.45 GB)**. Several `MUSIC*.DAT`, `GRAPHICS*.DAT` and `SOUNDS*.DAT` entries claim hundreds of megabytes each, consistent with historically encountered artificially oversized CD directory records. Therefore these numeric fields are **unverified metadata, NOT actual byte counts and NOT file-integrity evidence**. Even plausible small declared sizes like `MM01.APM = 48,592` and `BNK_0.BNM = 6,642,012` are only declarations until checked against real extracted files. **Do not use this table for original-CD extraction, manifests, hash verification, or deciding which file length to trust**.

The offline PE inspector's optional `reference_extra2_file_manifest` section is SHA-gated to the **exact supplied non-approved executable** and now validates the complete record stream as well as aggregate path counts. Its new `declared_bytes_total_not_integrity`, `sound_declared_bytes_not_integrity`, `sizes_verified_against_files=false` and `approved_cd_metadata=false` fields make this limitation explicit. Its synthetic tests reject missing sentinels, duplicate records, truncated lengths, embedded NULs and nonzero padding. The tool neither exports content nor claims a stock CD contains these paths.

## Research-only BNM metadata reader and ESC focus policy model

**Scope and caveat:** the available research workspace has the owner-supplied non-approved PE32 reference binary and older Wine logs, **but not a byte-for-byte BNM bank from a verified original CD**. The experimental readers below must not be described as validated retail format support, executable patching, working menu music, or an install/integrity implementation.

### Correction: BNM lookup key vs the resource record's own `Id` (original-CD verified)

This removes a previously **unsafe inference in the experimental BNM reader**. A direct disassembly of **the owner-approved retail CD**, independently identical to the reference for the **entire 64 bytes of native resolver `0x453F90`** (SHA-256 `1f4e185a18948dc27b9fe2af926984dbe0f4e4cbc846d66a9b2ff03b11b57c17`), proves the lookup algorithm:

```text
Input: packed reference from Play event [+8]
bank_id  = reference >> 16
index    = reference & 0xFFFF
bank     = find_loaded_bank(bank_id)       ; 0x4538C0
if not bank or index >= bank.resource_count: return null
return bank.resource_first + index * 92    ; no comparison to record[+0].Id
```

The **reference** supplied to the resolver is a packed bank/index key; the **`Id` stored inside the returned record** is a separate field which the resolver never verifies against that key. The earlier prototype erroneously required equality, and even guessed the filename `bnk_73.BNM` from such stored IDs. This was **not a valid retail-CD proof**. The updated [BNM metadata inspector](../tools/inspect_rayman2_bnm.py) now rejects this equivalence assumption. External [OpenRayman sound format research](https://github.com/huntyh/OpenRayman/blob/master/docs/formats/sound.md) describes the on-disk record `+0` as an *editor ID* in its surveyed distribution; **the owner's original-CD BNM is still unavailable**, so do not claim that description is verified for the owner's edition.

**Independent cross-distribution warning:** the publicly indexed [SteamDB depot 15061 sound manifest](https://steamdb.info/depot/15061/manifests/) lists `Data/World/Sound/Bnk_73.bnm` as **76 bytes**. Under the 44-byte BNM header + 92-byte resource-record layout, a file that small cannot contain a resource at **table index 48**. Meanwhile older runtime instrumentation printed `theme_bank=73 theme_resource=48` by splitting a native record's `Id` into two 16-bit words. These observations **must not be equated**. They concern different distributions, and the owner's actual `Bnk_73.bnm` has not been inspected; it is not proven whether the source of the discrepancy is edition differences or the older field interpretation. Thus `73:48` remains a **trace label**, not an authenticated filename, table position or audible soundtrack. The duplicate native Theme/inner-driver ownership and original driver pause/stop observations remain meaningful without such a filename claim.

The retail-CD resolver digest check now lives in the SHA-pinned [CD parity tool](../tools/check_rayman2_cd_audio_parity.py) with no executable bytes redistributed. The BNM reader and its synthetic fixtures have been corrected; no production audio calls or INI options have been changed.

### Inverting the native loaded-bank registry — no BNM file needed for live provenance

An important new read-only research path is now implemented **only in the opt-in `RAYMAN2FIX_AUDIO_TRACE` build**, not in the normal game. Instead of guessing the BNM bank from the first DWORD of `SND_tdstBlockResourceMem`, it performs the exact *inverse* of the original-CD resource resolver `0x453F90`:

1. Read `SND_g_stBankDescriptionList` first pointer at **`VA 0x514D50`**, using module RVA `0x114D50`. The native original-CD `0x4538C0` has `mov eax,[0x514D50]`.
2. Traverse at most **512** `0x48`-byte loaded-bank descriptors. Their **real bank ID** is at `+4`, next pointer at `+0x3C`, native state at `+0x20` (accept only `SND_BANK_Loaded=1`), and `p_stData` at `+0x24`. The native CD lookup independently reads the ID at `0x4538CD` and follows `+0x3C` at `0x4538D2`.
3. From each loaded bank head, read its **resource-table pointer** at `+0x0C` and **resource count** at `+0x10` (direct native reads at `0x453FBA` and `0x453FB5`). For a pointer to a live Theme or nested SXD resource descriptor, accept a candidate only when the address is inside that precise table, **aligned to 92 bytes**, and the calculated index is less than the count.
4. Require a fully traversed, **acyclic**, readable bank list with **exactly one match** before reporting `(native_bank_id, native_resource_table_index)`. Any missing bank, invalid loaded-bank header, cyclic links, 512-node limit, duplicate/overlapping tables, out-of-range pointers, or nonunique owner returns **-1/unknown**. The scan uses `ReadProcessMemory` only and is limited to the already-sampled initial transition driver events, never the normal build.

The trace keeps the **old** `theme_bank/theme_resource` and `inner_bank/inner_resource` fields solely for backwards compatibility: these are the **high/low 16-bit halves of the resource record's own `Id`**, *not* native loaded-bank provenance. **New distinct fields** in `RUNTIME-AUDIO-THEME-OWNER` are:

```text
outer_native_bank, outer_native_index,
outer_bank_candidates, outer_bank_list_complete, outer_bank_nodes,
inner_native_bank, inner_native_index,
inner_bank_candidates, inner_bank_list_complete, inner_bank_nodes
```

The [offline trace analyzer](../tools/analyze_rayman2_audio_trace.py) reports `native_loaded_bank_provenance` **only when both Theme and inner SXD have one unique, complete registry match**, and it refuses to pair otherwise similar samples if their newly verified native bank/index differs. Old logs get `None` for bank provenance, never a retrospectively invented `BNK_73.BNM` claim. Importantly this diagnostic is **not proof of audible music** or of ESC-menu visibility: it proves where a currently loaded *native record address* belongs in the bank registry, provided the memory snapshot is consistent.

The exact original-CD x86 instruction bytes supporting all seven bank-registry offsets (list head, bank ID, next, 72-byte descriptor allocation, bank-head pointer, resource count and resource first pointer) were added to the [SHA-pinned retail-CD parity audit](../tools/check_rayman2_cd_audio_parity.py); all are also present in the original signed CD PE's byte-identical region in the independent reference. The [research C++ guard tests](../tests/test_audio_trace_guards.py) forbid writes and unbounded traversal, while [synthetic trace evidence tests](../tests/test_audio_trace_evidence.py) prove `Id` halves are separate from native provenance and that ambiguous/incomplete lists cannot be treated as verified.

**Current validation boundary:** these are code-reviewed/static assertions and Windows/Linux CI tests, **not a live Wine observation of the new registry inverse**. To establish the original music's true bank, we still need a future diagnostic run or a separately authenticated CD BNM/level sound resource. No audio hook behavior is changed.


### Real Wine evidence: native Theme bank recovered (13 November 2026, 21:54 log)

The user supplied the **original diagnostic trace** `Rayman2Fix(20261113-215423).log` (142 display lines, **141 physical lines**; no game or CD assets were added to the repository). The read-only `RUNTIME-AUDIO-THEME-OWNER` snapshots finally provide **actual loaded-bank registry membership**, rather than speculative high/low halves of the resource record `Id` field:

| Observed native call / timestamp | Historical record `Id` halves (NOT bank keys) | True loaded Theme record bank/index | True inner SXD bank/index | Unique / registry fully traversed |
| --- | --- | --- | --- | --- |
| `StopTransitionSxd` 5,693 ms | outer `89:90`, inner `89:81` | **278:0** | **278:3** | 1 / yes, 31 nodes |
| `StopTransitionSxd` 8,238 ms | outer `89:96`, inner `89:91` | **282:0** | **282:1** | 1 / yes, 15 nodes |
| `StopTransitionSxd` 10,262 ms | outer `89:94`, inner `89:93` | **222:1** | **222:2** | 1 / yes, 49 nodes |
| First `PauseTransitionSxd` 38,779 ms | outer `73:48`, inner `73:49` | **9:5** | **9:6** | 1 / yes, 72 nodes |
| Second `PauseTransitionSxd` 38,780 ms | same as above | **9:5** | **9:6** | 1 / yes, 72 nodes |
| `StopTransitionSxd` 50,497 ms | same as above | **9:5** | **9:6** | 1 / yes, 72 nodes |
| `StopTransitionSxd` 55,999 ms | outer `89:90`, inner `89:82` | **278:0** | **278:4** | 1 / yes, 31 nodes |

This is a particularly strong validation of the inverse-lookup probe: **all seven ownership snapshots are unique and fully traversed**, and the exact *same native Theme 9:5 / inner SXD 9:6* appears twice in the duplicate pause callbacks and then in the later stop. The loaded bank ID `9` maps by the CD's native decimal filename formatter to the **expected filename `bnk_9.BNM`**. However, this log does **not** authenticate the user-owned physical BNM file, resolve the specific audio asset/track, prove that the given Theme is the sole audible music stream, or establish the true ESC menu identity. It is a runtime **bank/record-pointer provenance proof**, not an audio source/content hash.

**Critical pause/menu sequence (snapshot timing, not inferred UI events):**

- The native generic `game-script-pause` path yields two `PauseTransitionSxd` calls at **38,779/38,780 ms** on this exact Theme, both with `esc_down=1`, `ui_in_game_menu_at_call=0`, `ui_engine_paused_at_call=0`, `global_pause_depth=1`, and `foreground=1`. The shared driver is demonstrably reached twice from the native resource-routing structure. **The script command is still not an ESC-only opcode.**
- The 500 ms read-only poll at **39,162 ms** observes `in_game_menu=1`, `engine_paused=0` and audio `global_pause_depth=2`; thus the flag changes **after** both sampled audio pauses, within sampling resolution. This is why it would be unsafe to decide whether to suppress an incoming driver pause by reading the UI flag at that very call.
- The later `StopTransitionSxd` at **50,497 ms** targets **the same Theme 9:5 / inner 9:6**, with **`global_pause_depth=2` yet `ui_in_game_menu_at_call=0`**. The next periodic poll at **50,864 ms** also sees `in_game_menu=0`, while `engine_paused` remains 0. Another `StopTransitionSxd` at 55,999 ms targets a *different* Theme (278:0 / 278:4) at depth 2. **Stopping and menu-lifetime flags are independent of the outstanding audio-pause depth. Never resurrect a Theme after native stop based on menu flag changes.**
- There are **zero recorded `ResumeTransitionSxd` calls**. The optional resume hook is installed, and its per-channel sample budget is **16**, so this is not explained by that channel's budget being exhausted. However, the file does not contain a session-end marker or full evidence of all possible native resume paths; **zero sampled calls is not absolute proof that no resume happened at any other point or through a different path**.
- There are no `foreground=0` events: this trace **does not test Alt-Tab**. F1 was not independently classified or contrasted in this log. The engine-pause flag stays `0` in reported UI samples even while the separate audio-pause depth is 2, so these two states must remain distinct in implementation.

The [read-only trace analyzer](../tools/analyze_rayman2_audio_trace.py) now includes **menu/engine/foreground snapshots at owned stops** and explicit observed pause/resume/stop counts. [Synthetic regression tests](../tests/test_audio_trace_evidence.py) model exactly the condition `native_menu_flag=0`, `global_pause_depth=2`, `StopTransitionSxd`, and *no observed resume*, without asserting an ESC-only event or changing audio calls.

**Implication:** the experiment proves that a design based solely on `AI_g_bInGameMenu`, key state, or the first pause callback will be **timing-unsafe**. A future audio policy must distinguish the menu's **confirmed lifetime**, preserve both native transition dispatch paths and their `+0x1C` pause marks, and forward all Theme stops and replacements irrespective of audio depth. This evidence improves the model, **not** the readiness of a runtime music bypass.

### BNM metadata layout: native table offsets, not inferred placement

Independent reference sources: [Rayman2Lib `BNMFile.cs`](https://github.com/szymski/Rayman2Lib/blob/master/csharp_tools/Rayman2Lib/BNMFile.cs), [ACP_Ray2 `SND.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND.h), [`SND_Event.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Event.h), and [`SND_Resource.h`](https://github.com/raytools/ACP_Ray2/blob/master/src/SND/SND_Resource.h). The **non-approved** owner-supplied EXE's native loader `0x453990` reads precisely 44 bytes at `0x4539E7`.

**Important correction to the original research-only BNM inspector:** the two lists are **independently offset-addressed** within the 44-byte `SND_tdstSndBankHead`, **not guaranteed to be adjacent**. The original scanner incorrectly assumed `resource_start=44+event_count*32`; that could read an unrelated region and produce false Theme identifiers.

| Header byte offset | Native field | Verification |
| --- | --- | --- |
| `+0x00` | Bank format version | ACP_Ray2 |
| `+0x04` | `stEventList.hFirstElementOpt`, on-disk **relative offset** | Native `0x453BB8`: `mov ecx,[eax+4]`; `0x453BBB`: add header base |
| `+0x08` | Event count | `0x453C0C`; 32-byte event records |
| `+0x0C` | `stResList.hFirstElementOpt`, on-disk **relative offset** | Native `0x453BC3`: `mov ecx,[eax+0xC]`; `0x453BC6`: add header base |
| `+0x10` | Resource count | `0x453BCE` followed by 92-byte stride computation |
| `+0x14..+0x24` | Declared media offsets (MPEG, MIDI, sample, ADPCM, streaming) | ACP_Ray2; **origin/physical sizes not verified** |
| `+0x28` | `ulTotalSize`, a declared total | Not an independent file-integrity reference |

**Event-to-resource structural mapping:** `SND_tdstBlockEvent` contains `Id (+0)`, `eType (+4)`, `uParams (+8)`, in 32-byte records. For event type `SND_EVT_Son_Play=1`, `uParams.stPlay.uResource.Id` at `+8` is a packed resource reference: the native type-1 dispatch table entry at `0x453E90` selects `0x453DC9`, which loads that DWORD and calls resource resolver `0x453F90` at `0x453DD6`. Native `0x453F90` uses **upper 16 bits of the event's +8 reference to select a loaded bank and lower 16 bits to index its 92-byte resource table**; it **does not compare the returned record's +0 `Id` field** with the requested reference at all. Thus the prior requirement `resource.Id == (bank<<16 | index)` was **incorrect** and could reject legitimate references. In a single BNM file, a `Play` → local `Theme` candidate can be emitted only if a **filename-provided bank ID hint** matches the reference's high16, the low16 index is in range, and the selected record has `eType=11`. This is a syntactic candidate, **not authenticated bank provenance or proof of the song**. Unknown filenames, other-bank references, and out-of-range indices stay unresolved; arbitrary on-disk `Id` values must not cause rejection. Theme playback from another bank cannot be linked from one BNM alone.

The updated read-only [`tools/inspect_rayman2_bnm.py`](../tools/inspect_rayman2_bnm.py) validates independent event/resource ranges within the physical bank, rejects nonzero counts with nonexistent ranges and any overlap, reports `Theme=11` candidate metadata, and emits **only local candidate** `SND_EVT_Son_Play` → Theme references when this bounded direct-index/filename-hint test succeeds. It never decodes, extracts, modifies, or emits sound payloads. Use it locally on user-owned banks:

```bash
python3 tools/inspect_rayman2_bnm.py /path/to/DATA/WORLD/SOUND/BNK_0.BNM
```

The output includes `table_ranges_file_relative`, `play_event_references_candidate`, and `local_play_events_referring_to_themes_candidate`. These describe **candidate static structure**, not which song is heard in game, nor whether the file is an original retail bank. `verified_against_actual_retail_cd_bnm=false` and `audio_extracted=false` are mandatory. Media offsets, claimed data lengths and bank filename suffixes are **not authoritative** for CD installation or integrity. The [synthetic-only tests](../tests/test_audio_bnm_inspector.py) exercise contiguous, gapped, **reversed** lists, truncated records, overlapping or invalid offsets, null/other-bank event references, and a deliberate resource-record-`Id`/lookup-index mismatch that **must not** invalidate the native lookup. No commercial bank/media assets are added to CI.

### Why raw Theme part data is not yet directly addressable

An additional native constraint was verified in the Theme fixup routine: at `0x459847` the loader reads the disk resource's `ulDataOffset` from `[ecx+0x10]`, at `0x45984A` obtains a **separately supplied data-segment base** and at `0x459853` adds the two before walking 24-byte Theme part records. The caller `0x457A10` receives that base from the bank resource converter, which selects different storage/media blocks. This means **`ulDataOffset` is not yet proven to be an absolute byte offset from the beginning of a BNM file**. Parsing `ThemePart` bytes at `file[ulDataOffset]` would be another unjustified shortcut.

Accordingly the metadata inspector reports `disk_data_offset_declared_unverified`, `number_of_parts_candidate` and `outro_offset_candidate`, but **does not dereference the Theme part list or claim it decoded a particular `.APM` track**. The necessary next offline task is to reconstruct each storage mode's data-segment base and validate on a genuine BNM. This protects us from misidentifying music or reading unrelated data.

### Filename mapping proven in the supplied reference EXE

A previously open gap is now closed **for this exact reference executable**. The native `SND_tdstSndBankDescriptorElement` allocator `0x4538E0` stores the numeric `lId` argument at **descriptor+4** (`0x453945`) and passes that **same argument** to `0x453870` at **`0x453948`**. The formatter pushes a literal format string from VA `0x4A252C`, whose bytes decode exactly to:

```text
bnk_%d.BNM
```

Its output is copied into descriptor `szFileName[13]` starting at **+0x0C** (`0x45392E`). Therefore for the supplied reference EXE, **bank ID 23 deterministically formats to `bnk_23.BNM`** (for a valid short numeric name). This is not inferred from the unrelated `.extra2` filename inventory; it is a direct functional mapping visible in the sound-bank loader.

The revised BNM inspector now reports `candidate_reference_bnm_filename` **only from an explicit filename hint such as `BNK_23.BNM`**, never by interpreting the resource records' +0 `Id` fields. The source filename can be renamed or tampered with, so its numeric suffix is **not authenticated bank provenance**. `inspect_bytes()` without a bank hint declines all local `Play`→`Theme` matches instead of silently guessing. Tests intentionally use on-disk `Id` values unrelated to the bank/index pair; valid native index references must continue to resolve. This still **does not** identify audible music or establish equivalence with the owner's retail-CD files.

The standalone PE inspector verifies the exact format-string bytes, the `lId` assignment and the formatter call. No game binary, audio payload or game runtime is changed.

### Proposed independent-cause pause policy — specification only

The new [`tools/audio_pause_policy_model.py`](../tools/audio_pause_policy_model.py) expresses a **proposed desired audio state** for a future *proven-owned Theme stream*. It is **not connected** to the runtime or the native driver and does not fix the game's sound record pause marks (`+0x1C`) or global pause depth.

Conceptually, music may remain audible through **verified ESC-menu pauses only**, while all these causes require silence: focus loss, unknown focus state, any other/native game pause, unknown pause origin or actual Theme stop/level unload. The model also requires a resource ID and a generation token: an old stop event must never affect a newly started theme whose driver handle was recycled. Re-entering the foreground may restore the Theme only while still valid and otherwise eligible.

This is a deliberately narrow **fail-closed state specification**, not a simple `PauseTransitionSxd` interception. [Synthetic tests](../tests/test_audio_pause_policy_model.py) cover ESC→focus loss→focus gain, closing ESC while unfocused, unknown script causes, nested script causes, other audio pauses, level stops, stale generations and an exhaustive set of **4,096** short event sequences, with invariants that no owned Theme can be playing in the background or during an independently active pause cause. No F1 controls are introduced.

**Remaining integration work:** determine the actual Theme bank/resource ownership of the audible soundtrack and verify a way to reconcile this desired state with original `0x4580B0`/`0x458110` markers and driver calls. A model passing synthetic tests is **not** evidence the feature works or is safe under Wine/Windows; original-CD parity and one consolidated end-to-end test are still mandatory before release.

## Correction after owner Wine test — 13 November 2026, 14:40

A critical mismatch in our **research instrument**, not an absence of level music, was identified by comparing `Rayman2Fix(20261113-144018).log` with native x86 disassembly.

- Native allocator `0x4574E0` computes `lea eax,[ecx+0x4B8C98]` at **`0x45752C`** where `ecx=32*ordinal`: it returns a **pointer to an actual 32-byte sound-table record**, not the ordinal.
- Native Theme controller `0x459950` stores that pointer at **`0x4B95E8`** (`0x459AF4`).
- Native Theme pause `0x459C40` and resume `0x459C60` read `[controller+0x30]` and **pass this pointer directly** to `0x4584A0` / `0x458470`. They do *not* convert a slot index.
- Our previous probe wrongly tested the raw pointer with `slot>=64`, so it returned `sequence_slot=-1` and `inner_slot=-1` even with **controller state 1**. The resulting `theme_owned_driver=0` **cannot be used as evidence against Theme ownership**.
- The corrected probe takes the raw pointer as an **unsigned 32-bit address**, checks the pointer falls inside `[module+0xB8C98,module+0xB8C98+64*0x20)`, rejects misalignment and null/`-1` sentinels, and only then converts it to an index `(ptr-base)/0x20`. It never dereferences unvalidated addresses. The new `sequence_ref_class` and `inner_ref_class` distinguish the valid aligned-record-pointer class (3) from null (1), sentinel (2), unreadable (0), or invalid (4). Ordinary audio playback is unchanged.

**Test facts, not assumptions:** the new log has two ESC-triggered Theme-like transition pauses at **49.802–49.803 s**, two resumes at **59.502–59.503 s**, and a second pair of pauses at **63.735–63.736 s**. Each driver call is `game-script-pause` or `game-script-resume`, `sound_slot=1`, `sound_matches=1`, with native controller `sequence_state_class=2` meaning native active DWORD **exactly one**, and **one unique outer resource Theme** in native slot 8: **bank 73, resource 48**. The paused inner driver record reports resource **bank 73, resource 51**. This strongly motivates verifying their relationship with the corrected pointer decoder, but the old instrumentation failed to record it. The log contains no `foreground=0` event, so Alt-Tab has not been validated. A separate `StopTransitionSxd` at **67.387 s** while global depth 2 confirms the importance of forwarding legitimate stops.

This correction supersedes all earlier references in this report that described the Theme's `+0x30` value as a **numeric sound-table index**. Do not use the old `inner_slot=-1` or `sequence_slot=-1` observations to reject Theme attribution.

## Current next step: prove Theme ownership from one native pause event

The goal remains **level music audible during the in-game ESC pause**, while SFX obey their normal pause, the original Theme stop/change operations remain effective, and all audio pauses on loss of window focus. No F1 handling is in scope.

We now have a **concrete structural source-ID test**, independent of a guessed fixed sound-table slot:

1. The 64-entry native sound table `0x4B8C98` stores the **outer** Theme handle returned by `0x459950`/`0x459B3A`, which is the address of the Theme singleton `0x4B95B8`; its resource descriptor must have `SND_E_TR_Theme=11` at `+4`.
2. The same singleton's `+0x30` field at `0x4B95E8` contains the **inner** sound-record pointer, not a numeric index. The inner record must be active, transition-enabled, carry a native SXD resource type `1`, and own a **unique** driver handle.
3. Only if the paused driver handle is exactly this unique inner handle is `theme_owned_driver=1`. The outer Theme resource record exposes a native `Id` field whose **upper16/lower16 components are logged**, but the record's actual originating BNM bank/table index **has not been independently resolved**. The inner record’s `Id` is logged separately. Any absent, inactive, shared or ambiguous record yields **no confirmed owner**.

This is implemented as a **read-only, bounded** `inspect_audio_theme_owner()` probe in the optional `RAYMAN2FIX_AUDIO_TRACE` build only. It reports a separate `RUNTIME-AUDIO-THEME-OWNER` line for the first eight native `PauseTransitionSxd`, `ResumeTransitionSxd` and `StopTransitionSxd` events, with `outer_candidates`, `outer_slot`, `theme_bank`, `theme_resource`, `inner_slot`, `inner_bank`, `inner_resource`, `inner_active`, `inner_transition`, `inner_handle_matches`, and `theme_owned_driver`. No raw pointers or resource-file contents are logged; no driver argument, call order or return value is changed. The ordinary release build compiles the probe out entirely.

The previous `Rayman2Fix(20261113-091415).log` did **not** measure these outer/inner relationships. Although a driver handle matched one native transition record at ESC, **there is no proof yet that it was owned by the Theme controller**. Once that is established, the next safety gate remains reconciling native per-record pause marks and nested script/focus pause depths; naively suppressing one driver call is unsafe. Do **not** enable an experimental INI setting before that gate is resolved. A single consolidated Wine/Windows test of the research-only build (ESC open/close, Alt-Tab while ESC is open, real level transition) can later check the remaining runtime assumptions.

## Owner Wine trace 2026-11-13 21:54 — real bank provenance and menu timing limits

**Source:** user-supplied `Rayman2Fix(20261113-215423).log` (141 lines, **35,619 bytes**, SHA-256 `3beca8832e29b8064e409ec1058a30bae5cf16965fb6f314eec8ebb0b10b6a57`). The run uses **schema-2 optional audio research instrumentation** and native `RUNTIME-AUDIO-THEME-OWNER` provenance fields. This is the first supplied log with **uniquely matched actual loaded-bank IDs and resource table indices**. Unlike the earlier raw `Id` halves `73:48`, these are established by read-only traversal of the original engine's loaded-bank list at `0x514D50`: the scan completes, detects exactly one containing bank for each record and checks the 92-byte record stride.

| Runtime ms and operation | Record `Id` halves (NOT lookup keys) | Actual loaded Theme | Actual loaded inner SXD | Context |
|---|---|---|---|---|
| 5,693 — native `StopTransitionSxd` | Theme `89:90`, SXD `89:81` | **bank 278 / index 0** | **bank 278 / index 3** | Depth 1, game UI flag 0; 31 bank nodes |
| 8,238 — `StopTransitionSxd` | `89:96`, `89:91` | **bank 282 / index 0** | **bank 282 / index 1** | Depth 1; 15 nodes |
| 10,262 — `StopTransitionSxd` | `89:94`, `89:93` | **bank 222 / index 1** | **bank 222 / index 2** | Depth 1; 49 nodes |
| 38,779–38,780 — **two** `PauseTransitionSxd` | `73:48`, `73:49` | **bank 9 / index 5** | **bank 9 / index 6** | Both calls depth **1**, UI flag **0**, `esc_down=1`; 72 nodes; driver calls unchanged |
| 50,497 — native `StopTransitionSxd` | `73:48`, `73:49` | **bank 9 / index 5** | **bank 9 / index 6** | Depth **2**, UI flag **0** at **call time**, foreground **1**; same *resource identity* but **not** proof of identical driver generation |
| 55,999 — another `StopTransitionSxd` | `89:90`, `89:82` | **bank 278 / index 0** | **bank 278 / index 4** | Depth **2**, UI flag **0**; new inner resource |

**Observed native UI flag transitions are windows, not precise button/menu events.** At 38,779–38,780 ms the sampled driver pauses report `ui_in_game_menu_at_call=0`. The next explicit periodic UI sample reports `in_game_menu=1` at 39,162 ms, so the observed **0→1 change is bracketed by [38,780, 39,162] ms**, width **382 ms**. The latest observed `1` is the 39,162 ms poll, while the Theme stop at 50,497 ms directly reads the flag as **0** and the next poll at 50,864 ms confirms zero; thus the observed **1→0 change is bracketed by [39,162, 50,497] ms**, width **11,335 ms**. The wide bound does **not** imply the menu closed at either endpoint: intermediate toggles might be unsampled, and `AI_g_bInGameMenu` is not a verified ESC-only flag. The script pause occurred before the observed flag rise; **checking only that flag inside the initial audio callback would fail this observed cycle**.

**Depth timing matters:** the exact original-CD `0x4580B0` dispatcher iterates sound records and invokes the native driver callbacks **before** it increments global pause depth at `0x458100–0x458102`. A driver call at depth **1**, followed by a periodic read at **2**, is therefore consistent with **one** native global pause invocation. Do not interpret the two callbacks or the depth change as proof that the user issued two independent pause commands. At 50,497 ms the native stop has already observed flag 0 **while** global sound-pause depth remains 2. A nonzero depth does **not** prove the visible ESC menu is still open, nor can the Stop call be safely removed. The last sampled driver Stop at 55,999 ms also has depth 2.

**Observed event totals:** 28 sampled native sound-driver trace calls (`StopSxd=16`, `StopTransitionSxd=5`, `PauseSxd=5`, `PauseTransitionSxd=2`); seven complete Theme-owner observations, no recorded `ResumeTransitionSxd` or `ResumeSxd`, no sampled `foreground=0` / Win32 Alt-Tab sequence. **No recorded resume is not proof that resumes can never occur**, and neither a unique loaded Theme bank nor a transition SXD handle independently establishes which audio stream is audible music. A read-only pointer-to-bank match proves *resource ownership*, not physical file contents or audible sound. No separate F1 cycle appears in this log, so **F1 exclusion remains unproved**.

The [offline audio trace analyzer](../tools/analyze_rayman2_audio_trace.py) now merges call-time and periodic observations in source order to produce `observed_native_menu_flag_change_windows`. It resets at every explicit trace session, refuses to infer a transition across an unknown state or nonmonotonic timestamp, and explicitly labels every interval **not proof of an ESC/F1 menu**. [Synthetic tests](../tests/test_audio_trace_evidence.py) cover the 382 ms / 11,335 ms bounds derived from this trace, timestamp resets and appended sessions **without committing the user's log to the repository**. The original runtime still forwards **all** sound pause/resume/stop driver calls unchanged; no `MusicInPauseMenu` INI key or F1 hook exists. PR #2 remains draft.


**New generation/identity guard in the specification-only state machine:** [`tools/audio_pause_policy_model.py`](../tools/audio_pause_policy_model.py) now requires **both** `owner_is_theme=True` **and** an **independently confirmed music identity** to adopt a newly started stream; a loaded Theme bank and record index alone are insufficient, even when their provenance is unique. Every legitimate native Stop invalidates the current stream **and clears previous menu eligibility** before any replacement generation can receive policy decisions. This is essential given the owner log's bank-9 Stop at depth 2 with `in_game_menu=0`; a future bank-9 record could be a different instance even if its address or internal `Id` were reused. Pure synthetic [policy tests](../tests/test_audio_pause_policy_model.py) now require the new stream to remain silent under the old pause until fresh menu eligibility is proven or the independent native pause has ended. This is **not implemented in the runtime** and does not demonstrate working music-in-pause playback.

## Owner Wine trace 2026-11-13 23:24 — complete native pause/resume, then separate pause/stop

**Source:** owner-supplied `Rayman2Fix(20261113-232408).log`, 185 lines, **57,232 bytes**, SHA-256 `85a4fc1589348971ff3f87e64374aac8523d7212a8ba4e78815f8648a933749a`. This trace is **not committed to the public repository**; only aggregate timings and non-reversible resource identifiers are recorded here. Audio research hooks identify themselves as `schema=2`, and **all original driver calls are forwarded unchanged**. The `research_probes=0` message refers to the standard rendering probes, **not** to the separately enabled audio-trace research hooks.

**This is the first supplied post-bank-inversion log with a complete native pause→resume cycle at the Theme/SXD driver.** It demonstrates the sequence of original driver calls, not the actual audible sound or an ESC-only UI input origin.

| Phase and time (elapsed) | UI flag at driver call | Global audio depth at driver call | Actual native Theme → inner SXD bank:index | Native driver evidence |
|---|---:|---:|---|---|
| First pause: **40,004–40,006 ms** | `0` | `1` | **9:5 → 9:7** | `PauseTransitionSxd` twice (`sample=1,2`), `audio_origin=game-script-pause`, `esc_down=1`, same unique verified loaded-bank identity; **72 nodes**, each owner and inner lookup complete |
| First UI `1`: **40,021 ms** | periodic snapshot | `2` (periodic) | — | `AI_g_bInGameMenu=1`, engine pause byte `0` |
| First resume: **48,154–48,155 ms** | **`1`** | **`1`** | **9:5 → 9:7** | `ResumeTransitionSxd` twice (`sample=1,2`), `audio_origin=game-script-resume`, same live Theme and SXD identity; companion `ResumeSxd` audio callbacks also observed |
| UI `0` again: **48,537 ms** | periodic snapshot | `1` (periodic) | — | `AI_g_bInGameMenu=0` |
| Second pause: **78,862–78,863 ms** | `0` | `1` | **9:5 → 9:10** | Two `PauseTransitionSxd` calls (`sample=3,4`); **same outer Theme table location, different inner SXD** |
| Second UI `1`: **79,328 ms** | periodic snapshot | `2` (periodic) | — | Flag `1` with engine pause byte `0` |
| **Second native stop: 81,731 ms** | **`0`** | **`2`** | **9:5 → 9:10** | `StopTransitionSxd`, `theme_owned_driver=1`, one complete unique bank scan; **no `ResumeTransitionSxd` recorded for this second cycle** |
| Second UI `0`: **82,098 ms** | periodic snapshot | no further depth snapshot | — | Confirms `AI_g_bInGameMenu=0` after Stop |

**Bounds rather than guessed menu timestamps:** the combined periodic/driver sample stream brackets four successive *native menu flag* changes, without guaranteeing the intermediate state was constant or that the flag denotes ESC exclusively:

- First `0→1`: **[40,006; 40,021] ms** (**15 ms**); script-driven audio pause is already underway at flag `0`.
- First `1→0`: **[48,156; 48,537] ms** (**381 ms**); at 48,154–48,155 ms the driver has **resumed** audio while the flag is still `1`. An additional `ResumeSxd` sample at 48,156 ms independently still sees `1`.
- Second `0→1`: **[78,863; 79,328] ms** (**465 ms**).
- Second `1→0`: **[79,328; 81,731] ms** (**2,403 ms**). By 81,731 ms the stopped Theme reports flag `0`, even though global audio depth is still `2`.

There are **52 sampled native audio-driver calls**: 16 `StopSxd`, 16 `PauseSxd`, 9 `ResumeSxd`, 5 `StopTransitionSxd`, 4 `PauseTransitionSxd`, 2 `ResumeTransitionSxd`; **11 unique valid Theme-owner trace lines**. These counters reflect tracer limits, **not an exhaustive log of all runtime calls**. Seven of the Theme-owner samples belong to the two gameplay cycles above. The remaining early `StopTransitionSxd` traces establish loaded banks `278:0→278:4`, `282:0→282:1`, and `222:1→222:2`; the final Stop at 86,416 ms is `278:0→278:3`. All reported lookups have a complete unique bank traversal, but **loaded Theme ownership does not establish which sound is audible music**. No sampled audio event reports `foreground=0`; this is not a test of Alt-Tab behavior and the lack of `foreground=0` is not proof that no transient focus change occurred.

**Independent engine-pause state, still not an ESC marker:** `engine_paused` is recorded as `1` at **62,856 ms** and cleared at **63,366 ms** (510 ms between observations), then again `1` at **78,301 ms** and `0` at **78,812 ms** (511 ms). In all four samples `in_game_menu=0` and `foreground=1`. No corresponding menu flag rise is observed during those intervals, and the global audio depth remains at its last observed baseline of 1. Do **not** classify these engine clock pauses as F1, ESC or actual Alt-Tab events without independent evidence; exact transition instants are unsampled.

**Safety consequences:**

1. A candidate sound-pause suppression conditioned only on the *current* `AI_g_bInGameMenu` flag at `PauseTransitionSxd` would **miss both observed pauses** (`0` at the calls). Conversely, a conditional driver `ResumeTransitionSxd` hook keyed only to `in_game_menu==1` would misclassify **a legitimate native resume**. State ownership and per-record pause mark semantics remain essential.
2. The original CD at `0x4580B0` issues callbacks before increasing global pause depth; at `0x458110`, it **decrements** depth before issuing resume callbacks. The depth-at-call values **1 on both sides** therefore match the native ordering rather than implying two separate global operations.
3. `Theme` **9:5** is observed with different inner resources **9:7** and **9:10**. Do not bind generation, audible music identity or a stable SXD buffer to outer Theme alone. The second valid native Stop at depth 2 must *always* be forwarded and invalidate any provisional playback authorization.
4. The first cycle shows **two native resume callbacks for the same inner SXD**; this does **not** establish that an additional artificial resume is safe. Menu visibility is still only a generic VM boolean, its ESC/F1 distinction is not proven, and focus loss/recovery is not covered.

A new [synthetic test](../tests/test_audio_trace_evidence.py) reconstructs the two trajectories and four bounded flag transitions without checking any proprietary file into GitHub. It asserts preservation of unique loaded-bank provenance, valid paired native driver events, the independent engine-pause pulses, the 2-depth stop, and the explicit **unknown ESC/F1 attribution**. The owner has not yet supplied a confirmed F1-only cycle or a controlled Alt-Tab-in-menu sample. No runtime audio behavior or INI setting has been changed; PR #2 stays draft.


## Read-only candidate actor probe for the in-game ESC menu

The original-CD native menu byte `AI_g_bInGameMenu` at `0x4B9BC8` **does change after the audio pause callbacks**, as the two owner logs show, and the game also has a separate independent engine-clock pause flag. Therefore a new **research-build-only** `RUNTIME-AUDIO-MENU-ACTOR` line samples an additional **candidate**: the engine actor personal type named `YAM_GenMenuMap_I1`. This name is independently used by [Rayman2AP's `MOD_CheckVariables`](https://github.com/Aeltumn/Rayman2AP/blob/main/src/mod.c) when `AI_g_bInGameMenu` is true; the mod then looks for a menu-specific reflex. It is not an authenticated instance name from the owner's original French CD data. The owner has confirmed ESC as the sole menu action in scope. No separate keyboard mode or F1 test is required.

**No game function is called and no actor is modified:** the new `inspect_audio_menu_actor_readonly()` follows the *data structures* described by [ACP_Ray2's HIE object lookup](https://github.com/raytools/ACP_Ray2/blob/master/src/HIE/HIE_Ext.c), [type declarations](https://github.com/raytools/ACP_Ray2/blob/master/src/GAM/GAM_ObjType.h), [HIE layout](https://github.com/raytools/ACP_Ray2/blob/master/src/HIE/HIE.h) and [standard-game object fields](https://github.com/raytools/ACP_Ray2/blob/master/src/GAM/GAM_StdGame.h). Those sources are corroborating reverse-engineering references, not original-CD asset authentication. This implementation independently reads the real original-CD addresses:

| Native structure | Game VA | Research read |
|---|---|---|
| Personal-type list anchor (third of three 12-byte anchors) | `0x5013F8` | first/last/count, bounded to 4096 types |
| Personal-type element | variable | next +0, parent +8, name pointer +12 |
| Active dynamic world root global | `0x500FD0` | root's direct children only |
| Inactive dynamic world root global | `0x500FC4` | root's direct children only |
| `HIE_tdstSuperObject` direct child | variable | object type +0, linked actor +4, next sibling +20, previous +24, parent +28 |
| Actor and `GAM_tdstStandardGame` | variable | standard-game pointer `actor+4`, personal type index `standard+8` |

The actual CD code at `0x403C90` writes the personal-list anchor address `0x5013F8`; `0x407B04` uses its head; `0x407B0D` advances by `next+0`; `0x407B16` reads the element's name at `+12`. The original CD also references the two dynamic-world globals at `0x403B75` and `0x403B7F`. These **six exact instruction anchors** are byte-identical in the owner's original-CD PE and the nonapproved reference and are now pinned by the [CD parity audit](../tools/check_rayman2_cd_audio_parity.py) and [synthetic anchor tests](../tests/test_audio_cd_parity.py). These byte anchors support reading **structural addresses**, not the existence of the actor in the owner's installed level.

**Fail-closed behaviour:** the monitor checks 32-bit pointer bounds, read lengths, parent and predecessor links, duplicate names, cycles and maximum counts of 4096 registry elements / 4096 children per world. It does not recursively inspect scene sectors, inspect or modify AI scripts, read all actor names or publish pointers. Any invalid type list yields `personal_type_found=-1`; any incomplete world tree yields `dynamic_actor=-1 inactive_actor=-1`. `0` means a bounded list was inspected and that candidate was not found *in that snapshot only*; it is never a permanent claim. The inspector runs at the initial **500 ms UI poll** or a subsequent change in the native menu or engine pause flag. A sampled ESC key alone is not proof of menu visibility.

The new log line stores only `t_ms`, `in_game_menu`, `engine_paused`, `esc_down`, the three **tri-state** candidate fields, and counts of inspected nodes:

```text
RUNTIME-AUDIO-MENU-ACTOR: t_ms=... in_game_menu=... engine_paused=...
esc_down=... personal_type_found=-1|0|1
dynamic_actor=-1|0|1 inactive_actor=-1|0|1
type_nodes=... dynamic_nodes=... inactive_nodes=...
(read-only; actor candidate not proof of visible ESC menu)
```

The [offline trace analyzer](../tools/analyze_rayman2_audio_trace.py) records these as `menu_actor_readonly_snapshots` but **never converts presence into a confirmed ESC menu** or absence into a permanent menu-lifetime claim. [Fixture tests](../tests/test_audio_trace_evidence.py) exercise the new schema, legacy logs and invalid states, while [static runtime guards](../tests/test_audio_trace_guards.py) prohibit code-path writes and ensure the diagnostic does not add extra keyboard dependencies. The ordinary non-research DLL has this entire diagnostic **compiled out**. It neither pauses nor resumes the soundtrack and does not expose `MusicInPauseMenu`.

**Outstanding validation:** we have not seen any genuine `RUNTIME-AUDIO-MENU-ACTOR` data under Wine or verified its lifetime during a controlled **ESC-open/close** sequence. No F1 testing is required. Even a positive actor observation would be only one further signal; a future working feature must also satisfy bank-verified audible music identity, native pause-mark/focus semantics and stop-safe generation ownership.


## Safety requirements before a functional `MusicInPauseMenu` option

- Prove a **music resource identity** independently of driver pointer, table index, SXD transport, default volume or descriptor equality.
- Determine the true active type-11 controller state at ESC and the origin of **each** pause/resume call (including nested controller forwarding).
- Preserve ordinary `StopTransitionSxd` at menu/level transitions. Never prevent a legitimate stop.
- Analyze nested `0x4A3CC8` pause-depth behavior carefully: suppressing an ESC driver pause might leave the record marked as already paused, **potentially preventing a later Alt-Tab focus loss from issuing another pause call**. This is a risk deduced from the static global-pause loop, **not yet confirmed in Wine**. A safe implementation will need a deliberately designed state machine for focus loss and correct resume.
- Preserve SFX, ordinary MIDI/CD audio calls, shared Windows/Wine behavior; ignore F1 entirely.
- Keep the normal build free of instrumentation. Do not expose a nonfunctional INI setting or patch the non-approved reference EXE.

## Reproduce offline

```bash
sha256sum 'rayman2(4).exe'
objdump -d -M intel --start-address=0x457E60 --stop-address=0x457F9E 'rayman2(4).exe'
objdump -d -M intel --start-address=0x459950 --stop-address=0x459B46 'rayman2(4).exe'
objdump -d -M intel --start-address=0x4580B0 --stop-address=0x45816D 'rayman2(4).exe'
objdump -d -M intel --start-address=0x459C40 --stop-address=0x459C75 'rayman2(4).exe'
```

### Next offline work

Trace the resource origin of event class 11 via the seven upstream callers; check where their event descriptors are allocated/loaded and connect them to an actual `MUSIC*.DAT` entry if the code proves such a link. Verify controller lifecycle and one-versus-two-pause provenance against the approved CD image when available. Only request a consolidated Wine test for the specific ambiguities remaining after this static reconstruction.


## Owner Wine trace 2026-11-13 17:12 — Theme ownership proven at the driver

**Source/provenance:** owner-supplied \`Rayman2Fix(20261113-171247).log\` (168 lines, 45,702 bytes, SHA-256 \`068adc77c094b0729d10e1b7b62264aecd55e7b1e9e474b00ae0d9bf598dcdf4\`). This is the **first** observed log with the corrected \`RUNTIME-AUDIO-TRACE: enabled=1 schema=2 inner_record_ref=checked-pointer\` format and actual \`inner_ref_class=3\` / \`sequence_ref_class=3\`. This supersedes the prior stale-instrumentation evidence, not the native code audit. Instrumentation remains read-only, original driver calls forwarded unchanged.

**Observed ESC audio cycles** (runtime milliseconds are elapsed time, not wall clock):

| Event | Time | Outer Theme resource | Inner SXD resource | Native depth | Evidence |
| --- | --- | --- | --- | --- | --- |
| Generic VM script audio pause, ESC key held (menu not proven), transition call twice | 87.431–87.432 s | bank 73, resource 48, outer slot 6 | bank 73, resource 50, inner slot 1 | 1 at call, then 2 at 87.748 s | both \`PauseTransitionSxd\` events: \`theme_owned_driver=1\`, \`outer_candidates=1\`, \`inner_handle_matches=1\`, \`sequence_state_class=2\` (native state exactly 1), \`inner_ref_class=3\` (valid aligned record pointer); several other \`PauseSxd\` sounds also paused |
| Generic VM script audio resume, transition call twice | 106.431–106.432 s | bank 73, resource 48, outer slot 6 | bank 73, resource 50, inner slot 1 | 1 at call, remains 1 after 106.748 s | both \`ResumeTransitionSxd\` events also \`theme_owned_driver=1\`; \`ResumeSxd\` operates on other sounds |
| Second generic VM script audio pause, transition call twice | 133.663–133.664 s | bank 73, resource 48, outer slot 6 | **bank 73, resource 51**, inner slot 1 | 1 at call, then 2 at 133.696 s | both \`PauseTransitionSxd\` events \`theme_owned_driver=1\`; native inner resource changed while outer Theme ID stayed the same |
| Transition stop at native audio depth **2** (menu visibility not established) | 138.032 s | bank 73, resource 48, outer slot 6 | bank 73, resource 51 | **2** | \`StopTransitionSxd\` still \`theme_owned_driver=1\`; the stopped Theme is part of a legitimate native transition and must not be frozen |
| Subsequent transition stop | 141.667 s | bank 89, resource 90, outer slot 0 | bank 89, resource 81 | **2** | distinct Theme identity, \`StopTransitionSxd\` with \`theme_owned_driver=1\`, reinforcing that stop and Theme replacement can occur while ESC pause depth remains elevated |

**Interpretation:** the corrected pointer decoder establishes the *structural Theme owner → inner SXD transition → exact paused driver handle* association for generic VM-script pause/resume, rather than guessing from mutable slot 1 or recycled handles. The same outer Theme (bank 73/resource 48) now corresponds to inner resource 50 at the first pause and 51 at the second: **inner resource identity is not a stable proxy for the logical Theme owner**. It is still **not independently confirmed which Theme corresponds to the audible level soundtrack** or whether bank 73 is present and structurally valid on the original retail CD. The **original-CD** formatter would produce \`bnk_73.BNM\` *if the genuine loaded bank ID were 73*, but a high16 component of a record’s `Id` is **not evidence** that the bank ID is 73. The actual owner-CD bank has not been examined here.

These audio calls are classified \`audio_origin=game-script-pause\` or \`game-script-resume\` and were observed around the user's ESC interactions. **Those are generic sound VM opcodes, not proof of actual menu visibility or an ESC-only callback.** Two transition calls per pause/resume have distinct observed stack candidate sequences. This supports multiple native dispatch layers, **not** a license to drop either call. Other SXD sound records have separate pause/resume events; no global bypass is acceptable. The log contains only \`foreground=1\` and **no** independently observed Win32 focus-out/in sequence, so Alt-Tab nested pause safety is still untested.

**Decision/gates:** do **not** implement a driver-only \`PauseTransitionSxd\` skip, which would leave original per-record native pause marks and global pause depth unreconciled; a subsequent focus-loss pause could then be suppressed or a focus return could resume audio while ESC remains open. Preserve **all** \`StopTransitionSxd\` calls, including at native pause depth 2. Keep the current default runtime unchanged, no \`MusicInPauseMenu\` INI key, no F1. Continue **offline** by verifying original-CD Theme resource metadata and designing an origin-aware, generation-safe synchronization between native pause marks and driver state; only then consider a consolidated Wine/Windows ESC+focus+level-transition test. A \`theme_owned_driver=1\` trace proves ownership, **not** working music-in-pause playback.
