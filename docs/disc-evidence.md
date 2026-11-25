# Retail CD evidence checklist

Gather evidence **locally** from game discs you own. Do not publish copyrighted
game data, executables, BIN/CUE images, serials or activation information.

For each distinct edition, record:

- Printed retail title, edition, region, language, disc count and disc label.
- Container format (`.iso`, `.cue`/`.bin`, other); number of tracks and
  sectors per track.
- ISO 9660 volume identifier, directory list, file sizes and SHA-256 for
  relevant source assets, installer and candidate executables.
- Installer behavior and expected on-disk game layout; which files must be
  copied/expanded rather than merely extracted.
- EXE header architecture, timestamps, SHA-256, imported libraries, and
  whether its CD protection permits lawful modern use.
- Results under Wine, including language and regression notes. Native Windows
  and Proton results are welcome when available but not required from the maintainer.
- Source provenance. Label any assumption or missing test clearly.

Avoid relying on CD volume labels alone. A known edition must match a
strict manifest before any runtime hook is allowed.
