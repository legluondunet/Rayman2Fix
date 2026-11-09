# Rayman2Fix — architecture and staged implementation

Rayman2Fix is a **new, clean-room** project for the original PC CD-ROM editions
of **Rayman 2: The Great Escape**. It has no source-code dependency on Ray2Fix.

## Permanent layout

```text
Rayman2Fix.exe                       launcher / installer / verifier
Rayman2Fix.log                       one log, reset at each launcher start
Rayman2Fix/
    Rayman2.exe                      installed, edition-verified game executable (future)
    Rayman2Fix.dll                   our x86 in-memory patch/input/window runtime
    glide2x.dll                      GameFrame's x86 Glide 2 compatibility DLL
    SDL3.dll                         SDL3 x86 controller runtime
    Rayman2Fix.ini                   persistent user options
    ...                              original game data and resource layout
```

**Important:** the Glide 2 API DLL is **`glide2x.dll`**, not `glid2.dll`.
The final game will use **only the 3dfx Glide path**, never a separate
DirectX game renderer. GameFrame implements Glide using OpenGL.

The installer may use a *temporary* sibling staging folder while extracting,
but must publish only verified files into `Rayman2Fix/`. The current bootstrap's
`Rayman2Fix/DiscContent` is an interim extraction test, **not** the final
installed-game directory layout.

The long-term release goal is **one launcher executable** embedding its
own runtime payload. At present the Actions build bundles a prearranged
launcher plus `Rayman2Fix/` runtime folder; it does not yet embed those DLLs.

## Launcher workflow

1. On start, determine the directory containing **Rayman2Fix.exe**, regardless
   of shell working directory. Create `Rayman2Fix/` and only generate
   `Rayman2Fix.ini` if absent. Do not delete user settings.
2. Write exactly one `Rayman2Fix.log` *beside the launcher*, beginning with
   build/commit ID and passive machine inventory. Avoid personal identifiers,
   usernames, home paths, serials, IP and private environment values.
3. If an installed game is present, **verify** assets, executable hash, edition
   manifest and our runtime DLL versions; only then launch.
4. Otherwise discover exactly one supported CD image (or request an explicit
   source); fail on ambiguous choices. Inspect ISO/CUE layouts and verify a
   strict edition manifest, not merely a disc volume name.
5. Extract into a temporary staging directory. Reconstruct the game files
   and language resources using edition-specific extraction/copy rules.
6. Validate hashes, and only then promote staging to `Rayman2Fix/` (with
   backups and safe recovery). Never silently overwrite modified user assets.
7. Launch the verified 3dfx game with the exact expected Glide DLL, our
   runtime DLL and SDL3 in the same installation directory. Diagnose runtime
   errors in the single launcher log.

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

### 4. Add GameFrame, and support only the 3dfx renderer

Build or bundle a pinned, attributed open-source GameFrame Glide2x x86
library with the expected `glide2x.dll` exports. Verify textures,
lighting, videos, HUD, aspect correction, horizontal FOV and proper
4:3 positioning of overlays. The DirectX *game renderer* is unsupported.
Avoid treating a Win32 diagnostic API (D3D9 GPU enumeration) as a game renderer.

### 5. Replace exclusive fullscreen with stable borderless fullscreen

Use a window occupying the selected monitor's desktop bounds, **without
changing desktop display modes**. Preserve focus ownership, Alt+Tab,
cursor behavior and pause/resume across Windows/Wine/Proton; never force
topmost over another application. Handle 4:3, 16:9, high DPI and multimonitor.
Avoid game-specific resolution hacks.

### 6. Integrate SDL3 and options

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

- `--init` prepares the launcher-relative workspace and log.
- `--inspect` and `--extract` implement cautious MODE1 ISO/CUE handling.
- The Windows x86 runtime DLL is **inert**.
- Installer, no-CD play, pirate-head fixes, GameFrame and SDL3 are **planned**.
- Synthetic Windows x86 and Linux tests establish the build baseline, not real
  retail CD compatibility.

## Licensing

Select the main project license before release. Keep distinct attributions
for GameFrame, SDL3 and all other third-party dependencies.
