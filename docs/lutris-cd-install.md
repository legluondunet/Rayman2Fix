# Rayman 2 PC CD installation — evidence from the Lutris recipe

Source: [the author's existing Lutris installer](https://lutris.net/games/install/39141/view)
(`rayman-2-the-great-escape-cd-ray2fix`). This is a **behavioral
reference**, not source code to copy from Ray2Fix. The old Lutris
recipe uses third-party patches, dgVoodoo2 and Ray2Fix. **None** of those
three distributions are the planned runtime of our independent Rayman2Fix.

## What the existing installation actually did

1. Request a mounted original CD that contains `Rayman2.exe`.
2. Copy all visible CD files to `drive_c/UbiSoft/Rayman2/` and make
   them user-writable.
3. Copy `Dll/SND_CPA_MMX.ini` to `Dll/SND_CPA.ini` (audio configuration).
4. Back up the original `Rayman2.exe`.
5. Apply a separate `rayman2_patch.zip` and Ray2Fix release.
6. Install dgVoodoo2's `Glide2x.dll`, and consolidate the upper-/lower-
   case `DLL` and `Dll` folders.
7. Write the game's native `ubi.ini` `[Rayman2] GLI_Mode` resolution.
   Write the old dgVoodoo configuration.
8. Remove installation-only content (`Acrobat`, `DIRECTX`,
   `Drivers`, `InstData`, `Pads`, `Tonic Trouble`, `Autorun.inf`,
   `D3DImm.dll`, `DDraw.dll`, `DSETUP*`, `GXSetup.exe`,
   `setup.exe`, `SetupUbi.exe` and the duplicate `DLL` folder).
9. Remove **23 exact `.DAT` entries from `Data/World/Levels/`**.
   These exclusions are recorded below. The recipe does not establish
   whether *every one* is a deliberately malformed disc-protection file;
   it only establishes that the existing installer discarded them.
10. Under Wine, use native/builtin fallback configuration for `dinput`
    and `dinput8` (historical workaround; not necessarily appropriate
    when SDL3 replaces gamepad input).

The old script required the CD only during setup, but depended on external
patches. Copying the CD on its own **does not demonstrate** no-CD gameplay.

## Current exact file exclusion rule

For volume identifier **`RAYMAN2`** only, the ISO 9660 scanner may
recognize these **exact paths** from the author's Lutris recipe.

**Long name source:** the installer now prefers the disc's **Joliet supplementary
directory** over ISO 9660 primary 8.3 names, and exports all filenames and
directories in lowercase, with writable attributes. Both source identifiers
and output paths can therefore be diagnosed. Where no Joliet tree exists,
the reader cannot invent arbitrary long names and clearly reports the ISO
primary fallback.

**Retail ISO 9660 short-name aliases (fallback):** an actual RAYMAN2 CD inspection
reported `DATA/WORLD/LEVELS/GRAPHI~3.DAT`, while the existing script
removes `GRAPHICS0.DAT`, `GRAPHICS1.DAT`, and `GRAPHICS2.DAT`.
The primary-volume directory exposes short 8.3 names rather than the
long names used by Lutris after mounting. To account for all six missing
entries in the first log, only under `DATA/WORLD/LEVELS/`, the scanner
also accepts **`GRAPHI~1.DAT` through `GRAPHI~3.DAT`** and
**`TEXTUR~1.DAT` through `TEXTUR~3.DAT`** as installation exclusions.
The exact mapping from alias number to long-name suffix is not assumed.

```text
Data/World/Levels/GRAPHICS2.DAT
Data/World/Levels/MAP1.DAT
Data/World/Levels/ANIMS1.DAT
Data/World/Levels/MUSIC2.DAT
Data/World/Levels/MUSIC1.DAT
Data/World/Levels/SOUNDS0.DAT
Data/World/Levels/GRAPHICS0.DAT
Data/World/Levels/SOUNDS1.DAT
Data/World/Levels/LEVELS1.DAT
Data/World/Levels/ANIMS0.DAT
Data/World/Levels/MAP2.DAT
Data/World/Levels/SOUNDS2.DAT
Data/World/Levels/TEXTURES2.DAT
Data/World/Levels/ANIMS2.DAT
Data/World/Levels/MENU2.DAT
Data/World/Levels/MENU0.DAT
Data/World/Levels/MUSIC0.DAT
Data/World/Levels/TEXTURES0.DAT
Data/World/Levels/TEXTURES1.DAT
Data/World/Levels/LEVELS2.DAT
Data/World/Levels/MAP0.DAT
Data/World/Levels/MENU1.DAT
Data/World/Levels/GRAPHICS1.DAT
```

Why this matters: the real Rayman 2 retail CD is known to expose
*dummy/junk file entries* with nominal sizes vastly exceeding the CD's
physical capacity. A ~650 MB CD with ~7 GB of declared files has also
been reported historically. Some records may point into previously
occupied extents and some may extend beyond the image.

The bootstrap's `--inspect` **retains all 23 paths in the inventory**,
marks matched entries as `[SKIP: historical CD recipe]`, and reports
both the nominal total and the actual planned copy amount. The launcher
also logs each omitted path and its declared size.

For `--extract` these **exact** entries are excluded from output,
without reading their possibly fictitious extents. All *other* files and
all directories must pass physical-image bounds checks before extraction.
We never skip a file merely because it is unusually large. Unrecognized
or malformed files outside this narrow list still fail closed.

The installation remains a **bootstrap-only CD content extraction** until
we identify real executable SHA-256 values, verify the reconstructed game,
and independently implement the game renderer and fixes.

## Other installer-only files and directories

The same Lutris script removes these **root directories** after copying
and setting up the old runtime: `Acrobat/`, `DIRECTX/`, `Drivers/`,
`InstData/`, `Pads/`, `Tonic Trouble/`. The bootstrap excludes the files
inside these roots when its disc volume is `RAYMAN2`, along with the
8.3 folder alias `TONICT~1/` for `Tonic Trouble/`.

The script also removes these **root files**: `Autorun.inf`,
`D3DImm.dll`, `DDraw.dll`, `DSETUP*`, `GXSetup.exe`,
`setup.exe` and `SetupUbi.exe`. These are excluded by exact root paths
(or the original `DSETUP*` pattern), not by arbitrary filename matches
elsewhere in game directories.

**Correction — the original CD contains a single DLL directory.**
The historical Lutris script merged `DLL/` and `Dll/` only because the
external dgVoodoo2 and Ray2Fix components used inconsistent capitalization.
**Rayman2Fix does not use either component**, so it simply extracts the
original CD directory as `dll/`, with no dedicated merge, deletion, or
conflict-resolution step. Unexpected duplicate paths after lowercasing
are rejected rather than silently overwritten.

The exclusions are an **installation recipe** based on a version of the
author's CD script, not a universal claim about every possible Rayman 2
edition. Future editions require their own verified layouts. No game
executables or DRM modules from the old Lutris patch are redistributed.

## Additional Rayman2Fix exclusions: six exact CD root files

Beyond the historical Lutris list, the project owner explicitly excludes
these files from the `RAYMAN2` installation:

- `00000001.tmp`, `0000040c.016`, `0000040c.256`, identified as
  old SafeDisc-era auxiliary data.
- `secdrv.sys`, `rayman2.icd`, `dxplayerx.dll`, newly requested as
  installation exclusions. Their precise roles are not inferred.

The exclusion is **exact root path, only for the `RAYMAN2` volume**.
It never removes unrelated extensions, files with the same name in
`data/`, or content on other volumes. Existing installations migrate
automatically: validate all retained files against the saved SHA-256
receipt, remove the six root-only entries from the receipt, and delete
those precise files without re-extracting the CD. If unrelated files
fail verification, no cleanup is attempted.

## CD inspection evidence (2026-11-09)

With the previous 23-long-name filter, the user's ISO was reported as
`RAYMAN2`, **3,718 file records**, **7,450,158,839 declared bytes**,
**678,168,576 physical image bytes**, **17 known deletions**
(`5,150,888,419` declared bytes), and **1 unknown suspect** named
`DATA/WORLD/LEVELS/GRAPHI~3.DAT` (`LBA=200578`,
`437,458,886` declared bytes). The remaining five short-name aliases
likely account for much of the original excess nominal size. Their
exact presence and sizes **must be confirmed by another inspection**;
the ISO itself has not been uploaded.

The parser now logs all ignored paths and leaves any unrecognized suspect
files visible for inspection but blocks extraction if they remain.

## Adaptation roadmap for the new Rayman2Fix

- Keep one launcher `Rayman2Fix.exe` and `Rayman2Fix.log` beside it.
- Build in a staging folder and promote only a verified installation
  to `Rayman2Fix/`.
- Copy required game assets and apply the required `Dll/SND_CPA.ini`
  initialization, after verifying source and target.
- Preserve the exact original retail EXE in a backup within the user
  installation. Never ship it.
- Audit other install-only files and runtime-required files per edition.
- Use **GameFrame's own x86 `glide2x.dll`** for the **3dfx** path; do
  not install dgVoodoo2 or the game's DirectX renderer.
- Use **our own** `Rayman2Fix.dll` and SDL3 x86 library. No source code
  from Ray2Fix and no dependency on Ray2Fix archives.
- Establish trusted disc manifests; support multiple languages/editions
  only after validating their file paths, hashes and no-CD behavior.
- Diagnose in-game anticopy symptoms, including the reported pirate-head
  overlay and progression blocking, independently per verified game build.

## Current test evidence

The user's first ISO was reported as `ISO9660 / 2048-byte sectors`,
volume `RAYMAN2`, **3718 files** and **7,450,158,839 nominal file
bytes**, in an image described as ~662,000 KB. We have **not** received
the image itself, so these numbers do not identify the 23 file entries or
prove that they alone account for the difference. Run the updated inspector
and compare its omission log and physical byte counts before promoting an
edition-specific installation rule.
