# Bootstrap validation

Build `Rayman2Fix.exe` and the inert `Rayman2Fix.dll` on Windows x86.
Run `ctest` on Windows and Linux to exercise synthetic ISO and CUE/BIN data.

Synthetic tests MUST never depend on distribution of Rayman 2 assets.

## Manual checks

- `Rayman2Fix.exe --help` and `--version`.
- `--inspect disc.iso` and `--inspect disc.cue`, each containing one ISO9660
  MODE1 data track. Unsupported formats fail without side effects.
- `--extract disc.cue` writes to `Rayman2Fix/DiscContent`.
- Repeat the extraction: it must refuse to overwrite the prior directory.
- Truncated / malicious image paths fail without writing outside the staging dir.
- Check UTF-16 Windows path support on filenames with French accents.

**No playable game support, GameFrame render integration, SDL3 initialization
or runtime hooks are asserted by these bootstrap tests.**
