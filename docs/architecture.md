# Rayman2Fix — clean-room architecture (v0)

## Public contract

The end-user entry point is a single `Rayman2Fix.exe`, eventually containing
the installation payload and DLL dependencies. An installed game resides in
`Rayman2Fix/` relative to the launcher.

The game itself is **not** redistributed. End users provide their own original
CD images. A runtime must not alter original game media in place.

## Components

1. **Launcher / installer**: native Windows application, with offline CD source
   detection, installation staging, SHA-256 manifest verification, safe backups,
   predictable launch, a single log next to the executable and repair/verify.
2. **Disc layer**: ISO9660 and CUE/BIN readers; explicit MODE1 support; separate
   future handling for ISO9660 Joliet, multi-session and multiple data tracks.
   Unknown layouts and unverified editions fail closed.
3. **Edition profiles**: immutable ID, language, disc fingerprints, required
   source paths, expected SHA-256 for extracted files, executable hashes and
   machine-specific patch map. Do not infer executable identity from filename.
4. **Rayman2Fix.dll**: native x86 runtime, loaded only through a verified
   entrypoint. Graphics fixes, FOV/aspect ratio, background window behavior,
   bug-fix hooks and controller bridge are independent, testable modules.
5. **Rendering**: the open-source GameFrame Glide implementation is the planned
   3Dfx API backend, and should use a desktop-sized borderless window; no
   exclusive resolution changes. The original game may require a Glide DLL
   export name (`glide2x.dll`) or a legacy GLI entrypoint: these are separate
   loading boundaries, not a reason to borrow Ray2Fix code.
6. **Input**: SDL3 gamepad layer, with stable mappings, hotplug and keyboard
   fallback. SDL must initialize outside `DllMain`.
7. **Packaging**: sources and third-party licenses disclosed; no game assets,
   SafeDisc payloads or vendor executables in git or CI archives.

## Non-negotiable safety and correctness

- No memory offsets, opcodes, FOV parameters or patches until the **exact
  executable SHA-256** and original bytes have been validated.
- Each installation is transactional: stage, verify, then rename. Existing
  installed directories are not silently overwritten.
- Distinguish **disc extracted** from **playable installed game**.
- Proprietary disc protection is distinct from file extraction. Some retail
  builds may require an official compatible executable or other lawful path;
  support must not be claimed before verified on Windows and Wine.
- Preserve original frame timing where possible; never advertise speedrun
  equivalence without tests.
- No code copied from Ray2Fix. Implement from independently documented
  behavior and permitted interface analysis.

## PR milestones

1. **Bootstrap (this PR)**: read-only ISO/CUE identification, safe extraction,
   empty Windows DLL ABI skeleton, automated builds/tests.
2. **Evidence**: inventory real retail disc samples, compile fingerprints,
   extract ISO path/locale table, establish a supported-edition manifest.
3. **Installer**: verified installation layout, unique log, CLI --verify,
   executable selection, backup/repair, packaging of own assets.
4. **Renderer**: open-source Glide and borderless 4:3/widescreen, correct FOV,
   HUD and cutscenes, focus/Alt+Tab.
5. **SDL3**: gamepad mapping, startup recognition and reconnection, input tests.
6. **Versioned hooks**: safe per-build bug fixes and diagnostics.
7. **Release**: Windows and Wine/Proton validation of each advertised edition.

## Bootstrap limitations

The first build only supports `--inspect`, `--extract`, `--version` and
`--help`. The DLL exports an informational API but applies no hooks, does
not load SDL3 and does not initialize a Glide renderer. No CD release is
currently identified or playable through this bootstrap.
