# Rayman2Fix

**A new, independent compatibility project for Rayman 2: The Great Escape (PC CD editions).**

Rayman2Fix is being developed **from scratch**. It is **not a fork** of
Ray2Fix and does not contain code copied from that project.

> **Development status: CD installation bootstrap.** The launcher can
> automatically prepare the CD's game files, but no retail executable is
> authenticated as playable and the game **cannot yet be launched** through
> Rayman2Fix. This is not a no-CD patch or complete runtime.

## Automatic launcher workflow

Run **`Rayman2Fix.exe` without arguments**. The obsolete `--init` command
has been removed.

- When `Rayman2Fix/rayman2.exe` already exists, the launcher checks the
  local CD installation receipt (original filenames, sizes and SHA-256) and audio assets
  without needing the source CD again. Missing or changed files are reported,
  not silently overwritten.
- Otherwise, it searches for **exactly one** adjacent `.iso` or `.cue` file.
  It refuses absent or ambiguous media, and requires an identifiable
  `RAYMAN2` volume with `rayman2.exe`, `dll/snd_cpa_mmx.ini`, game data,
  and no unrecognized invalid file extents.
- It extracts the supported CD files to a sibling staging directory, omitting
  the known installer-only material and dummy entries, preserving long Joliet
  names in lowercase and writable. It creates `dll/snd_cpa.ini` from
  `dll/snd_cpa_mmx.ini`, writes a local **SHA-256** CD file receipt,
  then publishes directly into `Rayman2Fix/` transactionally. Existing
  unrelated files are preserved; an incomplete/unrecognized installation is
  never silently overwritten.
- The launcher embeds and deploys its own x86 `Rayman2Fix.dll`, creating
  the `Rayman2Fix/` directory and minimal INI automatically. If the
  runtime DLL changes, its **immediately previous version** is kept as
  `Rayman2Fix.dll.bak`; the older backup is replaced, never accumulated
  as numbered history.

**Important:** a SHA-256 receipt generated from the same CD verifies subsequent
file integrity, but does not authenticate the original retail edition. Starting the game is still **blocked** until the original retail EXE and
no-CD compatibility are validated. **GameFrame Glide2x is now bundled**;
SDL3/gamepad integration is deliberately deferred to a later phase.

The planned native `Rayman2Fix.dll` will handle validated per-executable
memory fixes, display and FOV/HUD behavior, window focus and graphics settings.
GameFrame's Apache-2.0 Glide 2.x OpenGL wrapper is now embedded and
automatically installed as `glide2x.dll`. Actual Rayman 2 renderer behavior,
borderless fullscreen, input and in-game fixes are **not yet validated**.
SDL3 will be considered in a separate phase and is not required at startup.

## Installed-file layout

The workflow archive now contains **one executable only**:

```text
Rayman2Fix.exe
```

On first execution it generates (and subsequently preserves):

```text
Rayman2Fix.exe
Rayman2Fix.log                  (generated beside EXE when Log=1)
Rayman2Fix/
    Rayman2Fix.dll             (unpacked from the EXE; inert placeholder)
    Rayman2Fix.dll.bak         (only if updated; one latest backup)
    Rayman2Fix.ini             (generated once if missing)
    rayman2.exe                (from original CD)
    dll/
        snd_cpa_mmx.ini         (from original CD)
        snd_cpa.ini             (initialized from MMX ini)
        ...
    data/ ...                  (original CD assets)
    .rayman2fix-cd-files.tsv   (local size + SHA-256 receipt)
    glide2x.dll                (GameFrame x86: deployed from launcher)
    GameFrame-LICENSE-APACHE.txt (Apache 2.0 license: deployed from launcher)
    SDL3.dll                   (future phase: not bundled or required)
```

The correct Glide 2 filename is **`glide2x.dll`**, not `glide2.dll`.
The launcher embeds the **pinned GameFrame x86 Glide DLL**, its Apache 2.0
license, and the project's own inert runtime DLL. It generates the INI and
log itself. There is still **only one file** in the Windows workflow artifact:
`Rayman2Fix.exe`. SDL3 and copyrighted game files are never bundled.

The only currently supported option in `Rayman2Fix.ini` is:

```ini
[Debug]
Log=1
```

Do not generate inactive graphics or controller settings. With `Log=1`, the
launcher resets `Rayman2Fix.log` each run and records a privacy-conscious
report (OS/Wine/Proton hints, CPU/cores, RAM, GPU, displays and controllers).
With `Log=0` it suppresses diagnostics and deletes any stale launcher log.
No usernames, private home paths, serials or network identifiers are logged.

## Available now (PR #1)

The initial Windows x86 executable can inspect and safely extract **ISO 9660**
images (2048-byte sectors) and **CUE/BIN with one MODE1/2352 or MODE1/2048 data
track**. ISO 9660 boot descriptors are handled. Audio tracks are not extracted.

**Filename handling:** the inspector searches for a **Joliet supplementary
volume descriptor** and uses its real UCS-2 long filenames instead of the
8.3 names in the ISO 9660 primary tree. All extracted files **and directory
components are lowercased**, including accented Unicode letters, and are
marked writable (no Windows read-only attribute). The original CD has a single DLL directory, extracted as `dll/`.
The historical Lutris `DLL`/`Dll` merge belonged to the Ray2Fix/dgVoodoo2
installation and is **not required** here. Unexpected collisions after
lowercasing (files or directories) are refused rather than silently merged.

If the CD has **no Joliet tree**, the inspector clearly reports that it
only has primary ISO 9660 names and that original long names may be
unavailable. No invented name mapping is applied to necessary game files.

On the original `RAYMAN2` CD, these **six exact root-level paths**
are excluded from the installation:

- `00000001.tmp`, `0000040c.016`, `0000040c.256` (original
  SafeDisc-era auxiliaries).
- `secdrv.sys`, `rayman2.icd`, `dxplayerx.dll` (additional exclusions
  explicitly requested by the project owner).

The exclusion never matches other files by extension or similarly named
files under `data/`. Existing installations are migrated without the CD:
after verifying all retained files by SHA-256, the launcher removes these
exact root files and their receipt entries. User settings and other game
assets are preserved.

On a disc whose volume label is exactly `RAYMAN2`, we recognize **23 exact
`Data/World/Levels/*.DAT` paths** discarded by the author's original
[Lutris CD installation script](https://lutris.net/games/install/39141/view).
Those entries stay visible in `--inspect`, but are skipped during
`--extract` (including any unreadable/fictitiously large extents).
**Other unrecognized out-of-range files no longer abort `--inspect`**: they
are shown with a `SUSPECT` label and their path, LBA and size are recorded
in the log. `--extract` still refuses to start if any such unknown entry
exists. No invalid data is silently copied. The log
distinguishes **declared total bytes**, **source image size**, **selected
extraction bytes** and each known exclusion. Unknown/unlisted oversized files
still cause a safe error; no blanket skip-by-size is performed.

```text
Rayman2Fix.exe                    (automatic install/verify workflow)
Rayman2Fix.exe --verify           (check local CD file-size receipt)
Rayman2Fix.exe --inspect "my-disc.iso"
Rayman2Fix.exe --inspect "my-disc.cue"
Rayman2Fix.exe --extract "my-disc.cue"
Rayman2Fix.exe --extract "my-disc.iso" "custom-output-folder"
Rayman2Fix.exe --help
```

The explicit diagnostic `--extract` command still targets
`Rayman2Fix/DiscContent/` by default. **The no-argument installer does
not use DiscContent**: it publishes the staged assets directly under
`Rayman2Fix/`. Extraction never runs the CD's own installers or modifies
the retail executable. `--verify` checks installed file presence, sizes and SHA-256,
but is not a trusted retail-edition allowlist/profile check.

GameFrame integration means the correct open-source graphics DLL is installed
and SHA-256-verified; it does **not** prove that Rayman 2 can render with it.
The retail executable/no-CD launch gate remains deliberately active.

## Original executable and patch backup

The user supplied an unprotected **x86 `Rayman2.exe`** for compatibility
research (size **1,468,928 bytes**, SHA-256
`9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`).
This commercial executable is **never committed or distributed**.

A patch generator and strict R2FXP01 binary-delta interpreter are included:

- `tools/build_exe_patch.py` requires the **original CD `rayman2.exe`**
  and the independently supplied unprotected executable. It generates a
  SHA-256-locked candidate patch only if it can reconstruct the exact target.
  It rejects patches containing too much literal target content.
- Future verified patch data can be embedded as RCDATA resource `#1102`.
  The current GitHub build contains **no executable patch resource**, because
  the original protected CD EXE bytes are not yet available for comparison.
- On an exactly recognized original SHA-256, the installer writes and
  verifies the new executable to a temporary file, **renames the untouched
  original to `rayman2.exe.bak`**, publishes the new `rayman2.exe`, and
  records both SHA-256 hashes in `.rayman2fix-exe-patch.tsv`.
- A subsequent `--verify` checks the CD's original hash against the
  **backup**, and the patch target hash against the installed EXE. An existing
  backup is *never* silently overwritten; a failed replacement rolls back.

**No EXE patch is applied and no `rayman2.exe.bak` is created until the
source CD version has been matched and a verified delta is bundled.** A
file-size receipt or volume label cannot establish a safe patch target.
Game launch still requires no-CD and actual Glide renderer validation;
SDL3 is explicitly out of scope for this stage.

## Build

The source uses C++17 and CMake. On Windows, build in 32-bit mode:

```powershell
cmake -S . -B build -A Win32 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CI checks out a fixed GameFrame upstream revision
(`f195a074a63cc150673c38becdec696163b0b3d1`), compiles the 32-bit
`glide2x.dll` in Rust/MSVC and checks its 156 canonical Glide exports.
CMake embeds it in `Rayman2Fix.exe` together with its Apache license,
and the executable verifies the SHA-256 of the installed DLL on every start.
If a different `glide2x.dll` exists, the launcher saves it as
`glide2x.dll.bak` (or the next unused numbered backup) before installing
the pinned DLL. No pre-existing backup is overwritten.

Linux and Windows CI run synthetic CD installation/verification tests.
GitHub Actions ships **only the Windows x86 EXE**, with **30-day** artifact
retention. Windows tests additionally compare the compiled DLL to the
unpacked copy byte-for-byte, and exercise the upgrade/backup path.

## Architecture and evidence

- [Design and roadmap](docs/architecture.md)
- [How to document retail disc editions](docs/disc-evidence.md)
- [CD installation steps from the author's Lutris installer](docs/lutris-cd-install.md)
- [Bootstrap test protocol](docs/testing.md)

No copyrighted Rayman 2 content, proprietary executables, disc images,
activation keys or protection-removal payloads are stored or distributed here.
Some CD executables use obsolete SafeDisc protection; compatibility must be
verified separately from ISO extraction. Support will be claimed only after
testing the corresponding edition on Windows and Wine/Proton.

Rayman2Fix's own project license is **to be selected before release**.
Third-party components will retain their respective notices and licenses.

Prepared by legluondunet, assisted by ChatGPT (OpenAI).
