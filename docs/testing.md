# Bootstrap validation

Build the x86 Windows `Rayman2Fix.exe`, which **embeds** the project's
inert `Rayman2Fix.dll`. The workflow archive must contain **only one EXE**;
the INI, log and runtime directory must be created by that executable.
Run `ctest` on Windows and Linux for synthetic ISO/CUE and automatic CD
installation, verification and refusal behavior.

Synthetic tests MUST never depend on distribution of Rayman 2 assets.

## Automatic startup checks

- Launch **`Rayman2Fix.exe` without arguments** and without any CD or
  `Rayman2Fix/` folder: it creates the folder, its own embedded runtime DLL,
  the minimal `[Debug] Log=1` INI and the single launcher-side log, then
  reports the missing CD cleanly. The former `--init` is unsupported.
- Place exactly one original `.iso` or `.cue` beside the launcher. The
  default command inspects it, verifies required file paths, extracts into
  a sibling `Rayman2Fix.installing/` staging folder, initializes
  `dll/snd_cpa.ini` from `snd_cpa_mmx.ini`, writes a size receipt and
  promotes the assets to `Rayman2Fix/`. The installed layout must not use
  `DiscContent/`.
- Remove or unmount the source ISO and run without arguments again. Existing
  game files must be **verified without reinstalling**. Since no functional
  runtime is integrated, the game must NOT be executed yet.
- Modify/remove one installed game file (including a same-size modification):
  SHA-256 verification and default launch must report the mismatch and
  leave the user's files untouched.
- Fresh RAYMAN2 installs must omit exactly the three root-level files
  `00000001.tmp`, `0000040c.016`, `0000040c.256` and omit their
  SHA-256 receipt entries. Same-named files under `data/` are preserved.
- Existing installs recorded by older builds must migrate on no-argument
  launch without needing the original CD: remove the three obsolete files
  and receipt entries, tolerate an already-deleted obsolete file, preserve
  the game's other SHA-256 hashes and user INI, then verify cleanly.

- With two images, no images, a malformed CD, or a foreign folder already
  occupying `Rayman2Fix/`, the launcher must fail without silently
  deleting any data.
- Validate `--verify` on installed CD files. It checks **size and SHA-256** against
  a locally generated receipt, but source-edition authentication and
  no-CD/Glide compatibility are not yet certified.
- Test an exe-only copy of the workflow artifact: verify extracted DLL SHA-256
  equals the build output and that no sidecar INI/log/DLL is shipped.
- Check that `Log=0` suppresses and removes stale logs, while preserving
  existing user settings.

## Manual checks

- `Rayman2Fix.exe --help` and `--version`.
- `--inspect disc.iso` and `--inspect disc.cue`, each containing one ISO9660
  MODE1 data track. Unsupported formats fail without side effects.
- `--extract disc.cue` writes to `Rayman2Fix/DiscContent`.
- Repeat the extraction: it must refuse to overwrite the prior directory.
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

**No playable game support, GameFrame render integration, SDL3 initialization
or runtime hooks are asserted by these bootstrap tests.**
