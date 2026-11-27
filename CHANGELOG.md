# Changelog

## Unreleased

### Added
- Support for GOG offline installers using embedded, pinned innoextract 1.9, with independent executable approval, edition-specific SHA-256 receipts and shared runtime deployment.
- SDL3 3.4.20 x86, automatic controller handover, native input integration and keyboard coexistence.
- Integrated Controls pages in startup and pause Options, with localized enchanted controller diagrams and live physical-input highlights.
- Read-only Video information in both the main and in-game Pause Options menus, included in the standard single-EXE installer.
- Six-row rounded, antialiased panel: output resolution and aspect, GPU, driver, GameFrame/Glide2, internal mode, and Rayman2Fix version-commit.
- Windows GPU detection using the actual GameFrame renderer and matched PCI/SetupAPI information; compact Radeon 760M/780M names where the revision is known.
- Host Vulkan driver information under Wine when the adapter match is verified.
- Rayman2Fix menu caption and official repository link, with early caption replacement during initial loading.

### Changed
- Automatic verified SDL3 deployment and separate zlib notice; strict archive, DLL, notice and runtime-export audits.
- GPL-3.0-only project licensing and Wine-first validation; Windows x86 automated CI remains required.
- The obsolete native resolution and brightness options are replaced in authenticated Video menus; Escape and other menus retain their native behavior.
- The legacy passive resolution diagnostic runs only in explicit research builds. Normal Video functionality is independent of logging.

### Fixed
- Repeated Jail_20 intro skips after restarting from the Pause menu, with a guarded single-bit reset and native level transition; preserve Learn_10 F248 restoration and the original no-movies flag.
- Move verbose intro research diagnostics behind the existing research-build switch without making normal director identification depend on logging.
- Late website caption replacement, obsolete Pause Video widgets and residual brightness gauge in the tested menu paths.
- Oversized generic Windows GPU descriptions shrinking all panel text.

Swimming and world-knowledge effects remain pending targeted Wine gameplay validation. Native Windows hardware testing is optional community follow-up.

See [VIDEO technical documentation](docs/standalone-video-page.md) for safeguards, historical research, and validation scope.
