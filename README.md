# Rayman2Fix

**An independent, free and open-source compatibility project for Rayman 2: The Great Escape (PC CD).**

Rayman2Fix is developed from scratch, not a fork of Ray2Fix. It combines
a verified CD installer, launcher and runtime compatibility fixes in one
Windows x86 executable.

> **Development status:** tested in gameplay under Wine. Automated Linux
> and Windows x86 CI tests pass on the audited SDL3 integration. Native
> Windows gameplay is not fully validated. No public release has been
> authorized yet; the Actions artifacts are development builds.

## Features

- Automatic installation from original ISO or supported CUE/BIN media,
  verification and launch.
- Pinned GameFrame Glide 2 renderer, DPI-aware borderless fullscreen at
  desktop resolution, widescreen Hor+ world rendering and centered 4:3 HUD.
- Optional centered 4:3 output and configurable FPS limit (60 by default).
- SDL3 controller support, automatic controller selection and an integrated
  illustrated Controls page with live button indicators.
- Read-only Video information in the main and pause Options menus, showing
  GPU, driver, output and internal resolution, aspect ratio and build.
- Optional removal of native dialogue backgrounds and cinematic bars;
  native viewport-based cinematic handling (scene coverage may vary).
- Deliberate skips of the second and third opening cinematics
  (`jail_20` and `learn_10`) and continued native music in the pause menu.
- One user-maintained INI and non-cumulative diagnostic logs.

## Installation

1. Obtain the standard `Rayman2Fix-Windows-x86-bootstrap` artifact from a
   successful [GitHub Actions build](https://github.com/legluondunet/Rayman2Fix/actions).
   Extract `Rayman2Fix.exe` to a new empty directory.
2. Place exactly one original CD image (`.iso` or `.cue` with its referenced
   data track) next to the launcher.
3. Supply the approved local unprotected `Rayman2.exe`, unless a local build
   already contains an approved binary delta. The public CI artifact contains
   neither a game executable nor that private delta.
4. Run `Rayman2Fix.exe` without arguments (under your normal Wine runner on
   Linux). The launcher installs, verifies and launches automatically.

Use a clean directory: no automatic migration of older installation layouts.
The first installation preserves the verified protected executable as
`game/rayman2.exe.bak`. Subsequent normal launches check the installed
assets and approved executable before running the game.

## Compatibility

Normal game launch is restricted to the exact approved PC CD executable
pair. ISO extraction support is not proof of compatibility with every
regional edition or re-release. GOG and other modified executables are
not claimed as supported.

| Executable | Bytes | SHA-256 |
|---|---:|---|
| Protected CD original | 676352 | `7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3` |
| Approved unprotected target | 1468928 | `9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713` |

The CD receipt verifies consistency with the source media, not its retail
provenance. Unknown executable hashes fail closed in the normal verified
launch path. See [disc evidence](docs/disc-evidence.md) and
[release readiness](docs/release-readiness.md).

## Controller support (SDL3)

Connect a compatible gamepad and play: SDL3 is installed automatically
and no separate configuration or Start-to-activate step is required.
One controller owns input at a time; a fresh input on a second controller
can take over when the previous one is idle. Keyboard input is preserved.

Open Options → Controls in the main or Escape pause menu to see the
fixed mapping, with Xbox, PlayStation or generic artwork and live
physical button/stick indicators. Press B / Circle or Escape to return.
The page is informational, not a remapping or calibration tool.

| Action | Xbox | PlayStation |
|---|---|---|
| Move | Left stick | Left stick |
| Jump / surface* | A | Cross |
| Shoot | B | Circle |
| Camera left / right | X or LB / Y or RB | Square or L1 / Triangle or R1 |
| Camera rotation | Right stick | Right stick |
| Target / dive* | LT | L2 |
| Statistics | RT | R2 |
| World knowledge* / call Murfy | D-pad up / down | D-pad up / down |
| Pause | Start | Options |

*Swimming and world-knowledge behavior remain pending targeted in-game
validation. The physical input indicators are not proof that a particular
native game action works in every scene. The owner tested both PS4 and
Xbox-compatible controllers under Wine; broader hardware and native
Windows gameplay coverage is incomplete; native Windows testing is deferred
to optional community feedback, not a release prerequisite. Automated
Windows x86 CI remains required.

SDL3 3.4.20 (x86) is bundled with its separate zlib license. No new INI
setting is required. See [technical notes](docs/sdl3-gamepad-policy.md).

## Configuration

`Rayman2Fix.ini` is the only user-maintained configuration file.

```ini
[Debug]
Log=1

[Graphics]
FPSLimit=60
AspectRatio=Auto

[Features]
SkipCinematics=1
RemoveDialogueBackgrounds=1
RemoveCinematicBars=1
MusicInPauseMenu=1
```

| Setting | Behavior |
|---|---|
| FPSLimit | 15–240 FPS, or 0 to disable |
| AspectRatio | Auto for desktop aspect ratio; 4:3 for the classic centered image |
| SkipCinematics | Space/Enter/Escape or South (A/Cross) requests supported intro skips |
| RemoveDialogueBackgrounds | Hide native dialogue rectangles |
| RemoveCinematicBars | Expand recognized cinematic viewports, not 4:3 sidebars |
| MusicInPauseMenu | Keep native level music during ESC pause |
| Log | 1 for diagnostics; 0 leaves existing logs untouched |

Set a feature to 0 to disable it. On the first successful verified game
session the one-shot default Log=1 is automatically changed to Log=0;
manual re-enabling persists. The runtime log is reset only for a new
actual logged game launch. There is no manual Resolution option; the
internal Glide resolution is 1024×768, independently of desktop output.

## Installed layout

```text
Rayman2Fix.exe
Rayman2Fix.ini
Rayman2Fix_installer.log
Rayman2Fix.log
game/
    rayman2.exe
    rayman2.exe.bak
    rayman2.exe.manifest
    fix.dll
    glide2x.dll
    SDL3.dll
    ubi.ini
    data/
    dll/
mods/
    textures/
        logo.rgba (optional local asset)
docs/
    README.md
    LICENSE
    GameFrame-LICENSE-APACHE.txt
    SDL3-LICENSE.txt
    build-info.txt
state/
    .rayman2fix-cd-files.tsv
    .rayman2fix-exe-patch.tsv
```

Logs are created when enabled; the patch receipt and original backup are
created when the approved replacement is applied. Optional user textures
are not distributed or modified by the installer. An optional local HD
title-logo atlas can be prepared with
[tools/prepare_intro_logo_hd.py](tools/prepare_intro_logo_hd.py);
the [historical technical README](https://github.com/legluondunet/Rayman2Fix/blob/ea077278a7a974396dcf1fdc7297415b9ce702ad/README.md#optional-real-hd-rayman-2-opening-logo-experimental-issue-16)
documents the format, source and browser-download fallback.

## Command line

| Command | Purpose |
|---|---|
| No arguments | Install / verify / launch |
| --install-only | Install / verify / update without launching |
| --verify | Verify installed CD assets against local receipts |
| --inspect image.iso | Read-only disc inspection, produces Rayman2Fix_inspect.log |
| --inspect image.cue [track.bin or track.img] | Read-only CUE inspection |
| --launch-no-verify | Troubleshooting only; bypass checks and provisioning |
| --version | Show version |
| --help | Show help |
| --verbose | Optional console diagnostics modifier |

For troubleshooting set `[Debug] Log=1` and attach both launcher/runtime
logs with the version-commit, Wine version, level and steps to reproduce.
Do not upload commercial game binaries or images.

## Development and license

C++17 and CMake; the game runtime requires Windows x86. See the
[pinned dependency build workflow](.github/workflows/build.yml) for the
complete build, audit and tests. The standard artifact is retained for
seven days. See [CHANGELOG](CHANGELOG.md), [user guide](docs/README.md),
[architecture](docs/architecture.md), [testing](docs/testing.md),
[controller policy](docs/sdl3-gamepad-policy.md) and
[Video technical notes](docs/standalone-video-page.md).

The extensive earlier research and installation evidence remain accessible
in the [previous README at an immutable commit](https://github.com/legluondunet/Rayman2Fix/blob/ea077278a7a974396dcf1fdc7297415b9ce702ad/README.md).
It is historical documentation, not the current support policy.

Rayman2Fix is licensed under **GNU GPL version 3 only**
([GPL-3.0-only](LICENSE)). GameFrame retains its separate Apache 2.0
license and SDL3 its zlib license; the launcher installs all three notices.
No copyrighted game assets, commercial executables or private patch
payloads are distributed in the public build.

Maintained by legluondunet, assisted by ChatGPT (OpenAI).
