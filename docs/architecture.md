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
    rayman2.exe.manifest             automatically generated PMv2 DPI compatibility
    GameFrame-LICENSE-APACHE.txt      license copied from embedded resource
    SDL3.dll                         future phase; not required at startup
    Rayman2Fix.ini                   single user-maintained configuration
    ubi.ini                          legacy game-facing file (auto-generated/synced)
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

## Automatic workflow (installer complete; first supported EXE launch test)

1. Execute **`Rayman2Fix.exe` without arguments**. The former `--init`
   command has been deleted; `--verify`, `--inspect` and `--extract`
   remain diagnostic commands.
2. Use the executable's own directory as the root, create `Rayman2Fix/`
   and a minimal `Rayman2Fix.ini` only when missing. Deploy the bundled
   `Rayman2Fix.dll`, retaining exactly one rolling backup:
   `Rayman2Fix.dll.bak` (the immediately previous DLL). An older `.bak` is
   discarded after successful replacement; legacy numbered backups migrate.
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
   Show real progress (bytes and completed files) through a callback, with
   a throttled human-readable command-line reporter shared by the automatic
   and diagnostic `--extract` workflows. Summaries must count actual
   completed output, and appear only after successful publish/verification.
   Duplicate `dll/snd_cpa_mmx.ini` as `dll/snd_cpa.ini` and create a
   SHA-256 receipt listing immutable extracted files (not mutable `ubi.ini`).
6. Preserve launcher-generated INI/runtime and its single latest DLL backup from
   the bootstrap folder. Refuse unexpected existing files; promote the
   prepared installation to `Rayman2Fix/`, with rollback on failure.
   The original CD image and executable are never modified.
7. Reverify the published files by size and SHA-256. Translate the user
   `Rayman2Fix.ini` graphics resolution into game-facing `ubi.ini` on
   installation and at each launcher run. Preserve other existing CD keys.
   After this verification, launch **only** the exact authenticated
   unprotected executable supplied by the owner (SHA-256
   `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713`).
   The original `rayman2.exe.bak` must match the protected SHA-256
   `7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`,
   and the patch marker and GameFrame must pass integrity checks.
   Spawn `rayman2.exe` via Win32 `CreateProcessW`, with `Rayman2Fix/`
   as current directory, then wait and log the real exit code/duration.
   **Do not assert successful gameplay or no-CD rendering before tests.**
   SDL3 is deferred.

**Current launch contract:** the exact approved protected/unprotected
executable pair is the only recognized launchable candidate. All other
editions are refused. The first Wine/Windows tests will establish whether
Rayman 2 itself launches without CD or requires additional compatibility
work. Future editions require separate validated hash profiles.

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

### FPS cap at the Glide frame boundary (active experimental integration)

The user maintains only `[Graphics] FPSLimit=60` in `Rayman2Fix.ini`.
0 disables the cap; valid nonzero values are 15..240. The launcher parses
this setting and passes `RAYMAN2FIX_FPS_LIMIT` to the child game process
only, restoring its previous environment after CreateProcessW. In CI the
exact pinned Apache-2.0 GameFrame source receives a reproducible,
fail-closed addition in `grBufferSwap`, after the game's submitted frame
is flushed. It schedules the next frame on the **producer** (game) thread
using `Instant` and `sleep` so the asynchronous renderer cannot build up
an unbounded backlog; stalls reset the cadence. No new DLL or other
user-facing INI is added. Unit tests cover source-anchor safety and
configuration/child environment behavior. Real game timing at 60 FPS
still requires testing on Windows and Wine.

### 6. Integrate SDL3 and options — deferred

Native x86 SDL3 gamepad support (hotplug, consistent mappings, controller
reconnection, keyboard fallback), initialized outside `DllMain`.
Keep the configuration short: mandatory compatibility fixes are not toggles.
`Rayman2Fix.ini` is the only user-editable settings file. The active
`[Graphics] Resolution=Desktop` option controls the legacy `ubi.ini`
`GLI_Mode`; mandatory `GLI_DllFile=GliVd1` and `GLI_Dll=Glide2` are
written automatically. SDL3-related options remain deferred.

## Borderless native output and Hor+ camera integration (first real build)

**Exact physical monitor dimensions and DPI:** The launcher is DPI-aware
(per-monitor-v2 where Windows/Wine supports it), obtains the primary monitor's
Win32 `MONITORINFO.rcMonitor` bounds and DPI, and uses that same coordinate
system to supervise the child game's window. No exclusive screen mode switch
or `HWND_TOPMOST` is permitted.

**Two resolutions, not two user settings:** `Rayman2Fix.ini` remains the
only user-maintained configuration, with `[Graphics] Resolution=Desktop` by
default. The game-facing `ubi.ini` keeps a supported internal Glide mode
`1 - 1024 x 768 x 16`. GameFrame is independently instructed to render
at the **native output** via `GLIDE_SIZE=WxH` in the spawned game process.
Native-resolution windowing is mandatory; user-selected resolutions can
override Desktop, but are never mistaken for the internal 3dfx mode.

**Borderless:** The launcher watches the actual game window and uses
`WS_POPUP | WS_VISIBLE`, removing caption/frame, and positions it exactly
over the physical primary monitor. It neither creates a separate fake
fullscreen overlay nor changes desktop resolution. It is not topmost;
Alt+Tab must be tested.

**True horizontal FOV:** Only the original-to-unprotected EXE pair
validated by SHA-256 is eligible. `Rayman2Fix.dll` is injected into that
running Win32 process by a separately initialized remote LoadLibrary call.
The runtime independently matches the exact Win32 PE timestamp and the
original seven code bytes at image RVA `0x22AB0` before hooking
`GLI_xAdjustCameraToViewport2`. Camera xAlphaY at offset `0x68`
is the reference angle and horizontal FOV expands with native aspect;
xAlphaX at offset `0x64` is modified before the original camera
projection function is called. The game EXE stays unchanged on disk.
The initialization happens outside DllMain, with no game callbacks under
the loader lock. On a hash/signature mismatch, it refuses the hook.
The first Wine/Windows gameplay tests will confirm injection and scene
geometry; **this is not yet a validated HUD/menus/aspect correction**.
Synthetic tests cover FOV math, Win32 non-game rejection, creation of a
borderless child at physical monitor bounds and inherited GLIDE_SIZE.

**DPI manifest:** `rayman2.exe.manifest` is a generated technical file
with PMv2/PM settings; it is not user-editable and must not mutate the
approved EXE. An existing unknown manifest is never overwritten.

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
- The initial **game launch is implemented** for the one known and
  SHA256-verified unprotected EXE, with the protected original retained
  under `rayman2.exe.bak`. The game is created with an explicit working
  directory and its actual exit code is logged. No-CD gameplay, Glide
  rendering and pirate-head behavior are **not yet validated**.
  SDL3 remains deferred.
- Synthetic Windows x86/Linux tests validate the installer and refusal
  paths, but NOT real retail disc playability.

## Licensing

Select the main project license before release. Keep distinct attributions
for GameFrame, SDL3 and all other third-party dependencies.
