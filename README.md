# Rayman2Fix

A clean-room compatibility project for **Rayman 2: The Great Escape (PC CD editions)**.

> Early development. No retail disc edition is claimed compatible yet.

## Direction

- One `Rayman2Fix.exe` for offline CD-image inspection, safe extraction, installation verification and launch.
- A separate native 32-bit `Rayman2Fix.dll` for version-specific in-memory fixes, display and input integration.
- Open-source GameFrame Glide as the planned 3D renderer (borderless fullscreen, no exclusive mode switches).
- SDL3 as the planned universal controller layer, with keyboard fallback.
- Only patch **known executable builds**; never apply unverified offsets.
- No game data, copyrighted binaries, disc images, keys or DRM replacements are distributed.

This repository deliberately starts **from scratch**: it does **not** incorporate source from Ray2Fix. Its name and architecture are independent.

Development proceeds through reviewable pull requests. See the first architecture PR for initial code, tests, and the release acceptance criteria.

Prepared by legluondunet, assisted by ChatGPT (OpenAI).
