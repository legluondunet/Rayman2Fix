# Rayman2Fix — architecture and staged implementation

Rayman2Fix is a **new, clean-room** project for the original PC CD-ROM editions
of **Rayman 2: The Great Escape**. It has no source-code dependency on Ray2Fix.

## Permanent layout

```text
Rayman2Fix.exe                       launcher / installer / verifier
Rayman2Fix.log                       one log, reset at each launcher start
Rayman2Fix/
    rayman2.exe                      copied from original CD (not yet playable)
    Rayman2Fix.dll                   unpacked from launcher (currently inert)
    glide2x.dll                      pinned GameFrame x86 Glide 2 wrapper (deployed)
    GameFrame-LICENSE-APACHE.txt      license copied from embedded resource
    SDL3.dll                         future phase; not required at startup
    Rayman2Fix.ini                   generated if missing, user-owned
    dll/snd_cpa.ini                  copied from CD's snd_cpa_mmx.ini
    data/...                         original CD content, lowercase and writable
    .rayman2fix-cd-files.tsv         local file-size and SHA-256 receipt
```

**Important:** the Glide 2 API DLL is **`glide2x.dll`**, not `glid2.dll`.
The final game will use **only the 3dfx Glide path**, never a separate
DirectX game renderer. GameFrame implements Glide using OpenGL.

The bare launcher uses `Rayman2Fix.installing/` next to itself while
extracting, then verifies sizes and publishes to `Rayman2Fix/` without
overwriting unknown existing user files. `Rayman2Fix/DiscContent` is used
**only** by the explicit diagnostic `--extract` command.

The Windows CI archive now contains **only `Rayman2Fix.exe`**, with the
project's own inert x86 DLL embedded as an RCDATA resource; it is unpacked
automatically. **GameFrame's pinned 32-bit glide2x.dll and its Apache
license are now embedded** in the single EXE, installed and SHA-256 checked
on first launch. SDL3, CD game files, INI and logs are not shipped.
SDL3 and controller support are deferred to a subsequent phase.

## Automatic workflow (implemented foundation, incomplete game runtime)

1. Execute **`Rayman2Fix.exe` without arguments**. The former `--init`
   command has been deleted; `--verify`, `--inspect` and `--extract`
   remain diagnostic commands.
2. Use the executable's own directory as the root, create `Rayman2Fix/`
   and a minimal `Rayman2Fix.ini` only when missing. Deploy the bundled
   `Rayman2Fix.dll`, backing up mismatched versions without deleting them.
   Write a single optional log beside the launcher with passive system info.
3. If `Rayman2Fix/rayman2.exe` exists, check its **local CD receipt**:
   expected file names, sizes and SHA-256 hashes, sound initialization and game executable.
   If any file is missing or changed, refuse startup, **never auto-repair or
   overwrite**. The SHA-256 baseline comes from the unverified user CD and is not yet
a cryptographic authentication of a specific trusted retail edition.
4. If there is no installed game, discover exactly one adjacent `.iso` or
   `.cue` and examine it. Require volume `RAYMAN2`, essential game files
   and no unrecognized suspect entries. Refuse missing or ambiguous images.
5. Extract only the historically necessary CD files into a sibling staging
   directory, using Joliet long names, lowercase paths and writable output.
   Duplicate `dll/snd_cpa_mmx.ini` as `dll/snd_cpa.ini` and create a
   SHA-256 receipt listing all extracted files.
6. Preserve launcher-generated INI/runtime and earlier DLL backups from
   the bootstrap folder. Refuse unexpected existing files; promote the
   prepared installation to `Rayman2Fix/`, with rollback on failure.
   The original CD image and executable are never modified.
7. Reverify the published files by size and SHA-256. **Do not start the game yet**: there is no
   trusted retail executable/hash profile, working no-CD runtime or tested
   Rayman 2 graphics. The **GameFrame 32-bit Glide wrapper is now integrated**
   and SHA-256 verified, while SDL3 is deliberately deferred. Log the
   blocker rather than claiming gameplay support. This stage remains a safe
   *installation foundation*.

**Future launch contract:** after a verified retail executable profile and
working fixes are added, the default command must launch the game only
after strict hashes, data and runtime checks pass. The original CD will
not be needed for subsequent validated launches.

## Diagnostic inventory (independently implemented, same categories as SpiderMan2k1Fix)

- Rayman2Fix version + commit, build platform, selected game edition and
  executable hash, source format and verification status.
- Windows runtime version; whether Wine or Proton is detected; Wine version,
  Linux host OS/kernel as reported by Wine, Proton **hint** when exposed,
  Linux desktop/session **hint** when exposed. Never mistake Wine's emulated
  Windows version for the host OS.
- CPU model, logical processor count and physical cores when available.
- RAM total and available.
- GPUs and adapter driver description/version as reported by Win32/D3D9
  (not an unverified Linux kernel/Mesa/NVIDIA driver version).
- Monitor count, primary/virtual desktop sizes, resolution and refresh rate.
- XInput and DirectInput controller presence at launcher startup (passive
  enumeration, without acquiring input); SDL3 device names/mappings after
  input integration, without exposing private identifiers.
- Active settings, GameFrame/SDL3 versions, DX/Glide export checks,
  lifecycle, focus/Alt+Tab, failure codes and integrity checks.

The log is **not** the persistent configuration; `Rayman2Fix.ini` is.
The logging policy must remain privacy-conscious, with unknown information
marked unavailable rather than guessed.

## Implementation stages (in strict order)

### 1. Reconstruct a healthy installation from real PC CDs

Collect known-good image formats and build **per-edition profiles**:
language, region, disc layout, source file SHA-256, executable SHA-256,
install layout, resource decoding and expected output. Retail CD installers
may compress or transform assets: ISO extraction alone is not enough.

### 2. Make a correctly installed edition runnable without the CD

Find how each supported game checks disc presence and what legitimate
compatibility mechanisms are available. Preserve and validate original
media assets; verify behavior on Windows 11 and Wine without a CD mount.
Do **not** assume SafeDisc-wrapped executables run after extraction.

### 3. Investigate and eliminate unintended anticopy side effects

Catalog the reported in-game *pirate-head* and other anticopy symptoms
against **exact executable versions**. An independently acquired GOG
executable may be compared (hash, PE structure, code/data patterns) as a
research reference, without redistributing it or importing another project's
code. Any binary or memory correction requires exact-version gating,
original-byte validation, backups and reproducible before/after tests.

### 4. GameFrame Glide 2x integration — DLL complete; game validation pending

The pinned Apache-2.0 GameFrame source
(`f195a074a63cc150673c38becdec696163b0b3d1`) is compiled to x86
`glide2x.dll` by Windows CI. Its 156 canonical decorated Glide exports
are verified, then its SHA-256 and Apache license are embedded in the
standalone launcher. On startup an incorrect or unknown Glide DLL is backed
up to `glide2x.dll.bak` (or unique numbered backup) and replaced by
the correct bundled library. Pre-existing backups are preserved.

**Still to validate in Rayman 2:** textures, lighting, video, HUD, aspect
correction, horizontal FOV and proper 4:3 positioning of overlays.
The DirectX *game renderer* is unsupported.
Avoid treating a Win32 diagnostic API (D3D9 GPU enumeration) as a game renderer.

### 5. Replace exclusive fullscreen with stable borderless fullscreen

Use a window occupying the selected monitor's desktop bounds, **without
changing desktop display modes**. Preserve focus ownership, Alt+Tab,
cursor behavior and pause/resume across Windows/Wine/Proton; never force
topmost over another application. Handle 4:3, 16:9, high DPI and multimonitor.
Avoid game-specific resolution hacks.

### 6. Integrate SDL3 and options — deferred

Native x86 SDL3 gamepad support (hotplug, consistent mappings, controller
reconnection, keyboard fallback), initialized outside `DllMain`.
Keep the configuration short: mandatory compatibility fixes are not toggles.
The `Rayman2Fix.ini` defaults will become active when the corresponding
features have been tested.

## Runtime protection contract

- `Rayman2Fix.dll` is loaded through a version-checked entrypoint, **never**
  unconditionally by executing `DllMain` patching code.
- No offsets or hooks before matching SHA-256 and verifying expected original
  bytes and per-build ABI.
- The launcher must refuse unrecognized executable builds with a helpful log.
- Never ship Rayman game assets, proprietary executables, CD images, activation
  keys or vendor DRM code in the repository or CI artifacts.

## Current PR #1 bootstrap status

- Default bare execution performs **automatic CD detection, staged extraction,
  preparation, local-size verification** or verification of an existing game.
  `--init` is removed.
- `--inspect`, `--extract` and `--verify` are available for diagnostics.
- The **single Windows EXE** embeds its own inert x86 DLL and creates its
  INI when absent; the workflow archive has no sidecar files.
- The Windows EXE now embeds the pinned 32-bit GameFrame Glide library and
  its Apache license. Windows CI verifies the DLL and the SHA-256, while
  preserving replaced versions under `glide2x.dll.bak`.
- **No game launch yet**, no SafeDisc/no-CD compatibility, no verified
  executable hash and no pirate-head fixes. Actual graphics compatibility
  has not been tested. **SDL3 is deferred** and is not a startup requirement.
- Synthetic Windows x86/Linux tests validate the installer and refusal
  paths, but NOT real retail disc playability.

## Licensing

Select the main project license before release. Keep distinct attributions
for GameFrame, SDL3 and all other third-party dependencies.
