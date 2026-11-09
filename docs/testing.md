# Bootstrap validation

Build the x86 Windows `Rayman2Fix.exe`, which **embeds** the project's
inert `Rayman2Fix.dll`. The workflow archive must contain **only one EXE**;
the INI, log and runtime directory must be created by that executable.
The same EXE must now also embed and deploy the pinned GameFrame
`glide2x.dll` and Apache 2.0 license with verified SHA-256.
Run `ctest` on Windows and Linux for synthetic ISO/CUE and automatic CD
installation, verification and refusal behavior.

Synthetic tests MUST never depend on distribution of Rayman 2 assets.

## Automatic startup checks

- Launch **`Rayman2Fix.exe` without arguments** and without any CD or
  `Rayman2Fix/` folder: it creates the folder, its own embedded runtime DLL,
  the minimal `[Debug] Log=1` and `[Graphics] Resolution=Desktop` INI
  and the single launcher-side log, then
  reports the missing CD cleanly. The former `--init` is unsupported.
- Place exactly one original `.iso` or `.cue` beside the launcher. The
  default command inspects it, verifies required file paths, extracts into
  a sibling `Rayman2Fix.installing/` staging folder, initializes
  `dll/snd_cpa.ini` from `snd_cpa_mmx.ini`, writes a size receipt and
  promotes the assets to `Rayman2Fix/`. The installed layout must not use
  `DiscContent/`.
- Remove or unmount the source ISO and run without arguments again.
  Existing game files must be **verified without reinstalling**. An
  unpatched, unsupported or modified game executable must **never** run.
  Only the exact approved unprotected executable with original `.bak`,
  patch marker, GameFrame and sound/ubi.ini assets can be launched.
- Modify/remove one installed game file (including a same-size modification):
  SHA-256 verification and default launch must report the mismatch and
  leave the user's files untouched.
- Fresh RAYMAN2 installs must omit exactly six root-only entries:
  `00000001.tmp`, `0000040c.016`, `0000040c.256`, `secdrv.sys`,
  `rayman2.icd`, `dxplayerx.dll`, including their SHA-256 receipt
  entries. Same-named files under `data/` are preserved.
- Existing installs recorded by older builds must migrate without the
  original CD: remove those six files and receipt entries, tolerate an
  already-deleted obsolete file, preserve remaining SHA-256 hashes and
  the user INI, then verify normally.

- With two images, no images, a malformed CD, or a foreign folder already
  occupying `Rayman2Fix/`, the launcher must fail without silently
  deleting any data.
- Generate `ubi.ini` on the first install from the CD's settings, preserving
  unrelated keys and sections, but force Glide 2 and user-selected
  `[Graphics] Resolution` (e.g. `1024x768`) as
  `GLI_Mode=1 - 1024 x 768 x 16`.
- Change `Resolution` to `1280x720` with the original CD removed.
  On the next launcher run, check that only the managed graphics values
  change, and the game-facing `ubi.ini` is updated. Removing `ubi.ini`
  should cause it to be regenerated. Invalid resolution must fail safely.
- Older manifests that recorded `ubi.ini` must migrate its entry out of
  the immutable SHA-256 receipt without deleting the game-facing file.
- Validate `--verify` on installed CD files. It checks **size and SHA-256** against
  a locally generated receipt, but source-edition authentication and
  no-CD/Glide compatibility are not yet certified.
- Test an exe-only copy of the workflow artifact: verify its own
  `Rayman2Fix.dll` and pinned GameFrame `glide2x.dll` SHA-256 match the
  original build outputs. Verify the extracted Apache license is identical
  to the pinned GameFrame source and no DLL/INI/log sidecars are shipped.
- Replace the installed `Rayman2Fix.dll` twice in succession, with
  a preexisting `Rayman2Fix.dll.bak` and old
  `Rayman2Fix.dll.backup.N` files: after each replacement, retain
  **one** `Rayman2Fix.dll.bak` with the immediately previous runtime.
  Delete earlier backup generations; a subsequent unchanged launch
  must not change the backup.
- Replace the installed `glide2x.dll` with an unknown file, rerun the
  launcher, verify the old file is preserved as `glide2x.dll.bak` and
  the pinned DLL is restored. A third launch must be idempotent.
- Verify SDL3 is NOT a prerequisite and does not produce a missing-runtime
  warning; gamepad input remains unimplemented until the next phase.
- Check that `Log=0` suppresses and removes stale logs, while preserving
  existing user settings.
- With untrusted/incorrect `rayman2.exe` and an invalid patch marker, the
  Win32 preflight **must refuse** to spawn any process. CI tests this
  refusal on both Windows and Linux.
- The Windows-specific CreateProcessW smoke test launches a harmless
  fixture with spaces in its path and an explicit working directory,
  waits for it to finish, verifies the working directory marker and
  records the exact nonzero exit code. No commercial executable is
  distributed or executed by CI.

## First approved-game Wine/Windows launch test

After installing the supported retail CD and applying the owner's
`Rayman2Fix-executable.r2patch` (sidecar next to the launcher), check that
`Rayman2Fix/rayman2.exe.bak` still hashes to
`7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`
and `rayman2.exe` hashes to
`9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`.

Run `WINEDEBUG=-all wine Rayman2Fix.exe` (or `Rayman2Fix.exe` on Windows).
This build should now attempt to spawn `rayman2.exe` in its own directory
and **wait until the game process exits**. Observe any window/dialog, intro,
menu, graphics (Glide), sound and keyboard navigation. Record the console
output and `Rayman2Fix.log` (look for `LAUNCH-READY`,
`LAUNCH-START`, `LAUNCH-EXIT`, or `LAUNCH-ERROR`).
A nonzero exit code may indicate a game crash or a missing runtime component.
Do not claim playability from the launch event alone.

## Manual checks

- `Rayman2Fix.exe --help` and `--version`.
- `--inspect disc.iso` and `--inspect disc.cue`, each containing one ISO9660
  MODE1 data track. Unsupported formats fail without side effects.
- `--extract disc.cue` writes to `Rayman2Fix/DiscContent`.
  Check the console shows image inspection, extraction progress in approximately
  5% steps, finalization, and a factual summary (files/bytes copied,
  exact skipped entries, destination, elapsed time). Ensure even one
  large file emits progress during its transfer rather than only at the end.
  The automatic no-argument installer should show its eight active stages,
  including SHA-256 receipt generation and verification, then a final summary.
  Both modes must flush line-oriented progress for Wine/PowerShell.
- Repeat the extraction: it must refuse to overwrite the prior directory
  without incorrectly reporting success. An interrupted/failed extraction
  must not leave a promoted output or a false success summary.
- Truncated / malicious image paths fail without writing outside the staging dir.
- Check UTF-16 Windows path support on filenames with French accents.
- When a Joliet supplementary descriptor is present, use its actual long names
  instead of the ISO9660 primary 8.3 aliases; show the source in `--inspect`.
- Export every directory and file component in lowercase, including accented
  characters. Confirm the output is writable and has no Windows read-only bit.
- Extract the CD's single original DLL directory to `dll/`, with no extra
  `DLL`/`Dll` merge. Reject unexpected case-colliding source entries.
- Without Joliet, clearly label the ISO9660 primary-name fallback instead of
  inventing long file names.

**The ability to spawn a separate Windows process is unit-tested; actual
Rayman 2 rendering, playable no-CD launch, SDL3 initialization or game
hooks are not established by CI.**
