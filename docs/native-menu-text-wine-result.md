# Wine result — menu text trace 21 November 2026, 12:21

The owner tested the approved diagnostic executable from GitHub Actions
`44902313872`, commit `5caeb22dab80a94d871ed66a249ad6df3811db80`.

Source: locally supplied `Rayman2Fix(20261121-122118).log`,
114,752 bytes, 569 lines, SHA-256
`05eda739e929bae40d2df9e32ef6edf2b9dad8dff479b1ab3155e147f596f54d`.
Only aggregate telemetry is summarized here, not the full owner log.

## Validated native interception

- `RUNTIME-MENU-TEXT: status=installed va=0x494C70
  verified_original_bytes=17 forwarding=1` — the guarded resolver probe
  installed successfully, with no logged fault or resolver mismatch.
- `RUNTIME-VIDEO-MENU-TRACE: status=accepted-image
  kind=deprotected-x86 size=0x001D7000`.
- 45 time-stamped `RUNTIME-MENU-TEXT` snapshots at 14.515–51.055 seconds.
  Another 63 `RUNTIME-VIDEO-MENU-TRACE` samples at 0.194–59.886 seconds.
  The observer's `status=finished` line is absent, consistent with game
  process exit before the five-minute logging limit.
- The effective original GLI selected mode remains
  `mode_count=8 selected_index=5 selected_mode=1024x768x16`;
  `driver_bound=1`. **This is the engine's mode, not necessarily the
  physical desktop resolution.** No change to this selected mode was
  detected in this test.
- The independent `learn_10` handover check reported
  `status=pass` and `slot_restored=1`. This doesn't establish visual
  or audio quality, which require subjective game testing.

## Key negative result: text lookup does not identify the submenu

The final text counters are:

| Text handle | Description | Total |
|---|---|---:|
| 370 | language | 1 |
| 371 | VIDEO | 197 |
| 372 | sound | 2 |
| 373 | other options | 2 |
| 379 | RESOLUTION | **195** |
| 380 | SAVE RESOLUTION | **195** |
| 381 | LOW | **195** |
| 382 | MEDIUM | **195** |
| 383 | HIGH | **195** |
| 384 | OTHER | **0** |
| 385 | BRIGHTNESS | **195** |
| AI-origin 379 | RESOLUTION | **195** |
| AI-origin 380 | SAVE RESOLUTION | **195** |

Crucially, **in all 45 snapshots**, the eight counters for
`379,380,381,382,383,385,ai379,ai380` are exactly equal.
They increase together during many windows, without independent signal.

The legacy generic pause-menu flag changes:

- `generic_menu=1` around 13.015–26.839 s,
- another short interval 27.641–28.238 s,
- then 28.637–31.247 s.

At 33.034–51.055 s, **while generic_menu=0**, labels for
RESOLUTION and BRIGHTNESS increase together from **82 to 195**. This
demonstrates that the native text lookup path is called from UI update
logic even when the generic menu state is unset. Lookup for BRIGHTNESS
is **not proof** that the brightness screen is open; likewise labels
379–383 **cannot serve as an active Resolution submenu predicate**.

Handle `384` never appears. This proves only that the resolver did not
lookup that string in this particular run; it does not show whether the
"Other" graphical option is available.

The matching `ai379/ai380` totals confirm those two labels are read
through the identified AI text expression path, **not** that the
resolution-selection script was executed. The native detour is technically
validated but the semantic hypothesis that text calls identify submenu
presence is **rejected**.

## Consequence and next instrumentation target

Do NOT:
- conditionally block resolution changes when text handle 379 is read;
- hide/replace text 385 because it shares lookup timing;
- infer the active submenu from the generic pause flag;
- globally detour `GLI_DRV_hChangeMode`, which is needed at startup.

Instead, follow the previously identified **video menu AI controller**
(`SuperObject 0x07AB2250 → Perso 0x07E70908 → Mind 0x07920990 →
AIModel 0x079209A8` in decoded SCENE addresses). Instrument the
**currently selected normal/reflex behavior** at runtime using
validated mapped pointers, without equating scene VAs to runtime
addresses, and explicitly separate normal behavior 5 and 19 from
reflex behavior 5. The screen-specific predicate must be validated in
Wine before any replacement of the original menus.

An optional later diagnostic can observe native calls to the
resolution-selection and save functions without modifying their
outcomes, but the presence/absence of calls is not itself proof
that the page is open. Do not require users to intentionally apply
a different video mode just to validate a dangerous hook.

**Status:** PR #51 remains a research draft. The approved `main`
runtime and menu are unchanged. No new game test should repeat the
same text-handle-only experiment.
