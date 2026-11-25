# GOG offline installation (#61)

The single Rayman2Fix.exe accepts one local GOG offline setup EXE plus its matching BIN slices, or one supported CD source. Existing verified installations take precedence. Ambiguous fresh-install sources are refused.

Use an audited, pinned Windows innoextract executable embedded in the launcher if it supports the actual archive. Do not run the original setup, use Galaxy, download game assets, or require a system extractor. Keep dependency notices and verify the embedded extractor before invoking it by absolute path without a shell. Unknown Inno formats, failed integrity checks, missing parts and unknown game executables fail closed.

The GOG product ID is 1207658940 (GOG support and GOGDB). This is identification, not authentication. Collect local setup names, sizes, SHA-256, Inno data version, game ID, languages and file listing with sizes and checksums before deciding the supported profile. Do not assume all older or newer installers are equivalent.

Developer-only inventory (not a production installation):
```sh
sha256sum setup*.exe setup*.bin
innoextract --data-version SETUP.exe
innoextract --no-extract-unknown --gog-game-id --silent SETUP.exe
innoextract --no-extract-unknown --list-languages SETUP.exe
innoextract --no-extract-unknown --list --list-sizes --list-checksums SETUP.exe
```
Replace SETUP.exe with the actual local installer name, and hash only existing matching files. Do not upload commercial assets.

Extraction goes into isolated staging; inspect paths, file types and case-fold collisions before normalization. Do not delete GOG-specific files until their purpose is established. Build the same game/ + mods/ + docs/ + state/ layout, reuse transactional publication and common launcher configuration and dependencies. Preserve source installers. Add an edition-specific immutable receipt and approved SHA-256 profile; never use the CD executable replacement route for GOG. No game launch until the GOG runtime ABI and hook bytes have been independently validated. Keep CD behavior unchanged.

References:
- https://support.gog.com/hc/en-us/categories/201400969?game=1207658940
- https://www.gogdb.org/product/1207658940
- https://constexpr.org/innoextract/innoextract.1
- https://constexpr.org/innoextract/

Status: design and evidence collection only. No GOG extraction or runtime support implemented yet.
