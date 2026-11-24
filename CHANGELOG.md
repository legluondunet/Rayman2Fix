# Changelog

## Unreleased

### Added
- Read-only Video information in both the main and in-game Pause Options menus, included in the standard single-EXE installer.
- Six-row rounded, antialiased panel: output resolution and aspect, GPU, driver, GameFrame/Glide2, internal mode, and Rayman2Fix version-commit.
- Windows GPU detection using the actual GameFrame renderer and matched PCI/SetupAPI information; compact Radeon 760M/780M names where the revision is known.
- Host Vulkan driver information under Wine when the adapter match is verified.
- Rayman2Fix menu caption and official repository link, with early caption replacement during initial loading.

### Changed
- The obsolete native resolution and brightness options are replaced in authenticated Video menus; Escape and other menus retain their native behavior.
- The legacy passive resolution diagnostic runs only in explicit research builds. Normal Video functionality is independent of logging.

### Fixed
- Late website caption replacement, obsolete Pause Video widgets and residual brightness gauge in the tested menu paths.
- Oversized generic Windows GPU descriptions shrinking all panel text.

See [VIDEO technical documentation](docs/standalone-video-page.md) for safeguards, historical research, and validation scope.
