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


## Owner-supplied GOG v2 evidence — 25 November 2026

The combined French/Polish inventory successfully lists Inno Setup 5.6.2 Unicode, product metadata 1207658940. Listing is not an integrity test. Both list the same Rayman2.exe MD5 ef4954015d8b67e3534c9dc40ca3ad6e; localized assets (Textures.cnt, Fix.gpt) differ. Preserve each installation's own resources; do not assume identical archive or data hashes.

French setup build 86383, SHA-256 calculated locally from the actual supplied files:
- setup_rayman_2_-_the_great_escape_1.0_gog_v2_(french)_(86383).exe: 1053624 bytes, 604bb057b69186989404434669e82d833c076131c1be0dcf7fefc1ce94aeaa69
- setup_rayman_2_-_the_great_escape_1.0_gog_v2_(french)_(86383)-1.bin: 358957056 bytes, 1d73f82cca92be83101901abb78dcb4160d3442147a1b94d9c6963a6310f22b0

Owner-extracted GOG_reference.zip:
- Rayman2.exe: 1469440 bytes, SHA-256 82e56041bde6b135a29af70ec213d716ef9d3253d46fa2c893bd25a6cff7f891, x86 PE, timestamp 1999-10-15 23:19:39 UTC, image base 0x400000, entry RVA 0x5fcc0.
- ubi.ini: 141 bytes, SHA-256 946e8049cd824007eac00be2b56c93991dbd499f2d0f38974ce606cdaa2a4fb4; Glide2/GliVd1, 1024x768x16, Language=English, ParticuleRate=High.
- DLL/Snd_Cpa.ini: 359 bytes, SHA-256 39f30bfff1265a9669c23ed986993d2c1d654258b8e4d665b5d67401cd00006d; CD audio OFF, WAV/MPEG/ADPCM ON, SXD default WAVPW / MMX WAVMW, ADPCM default APMPW / MMX APMMX.
- GOG original EXE differs from approved local CD EXE (1468928 bytes, 9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713). Do not infer equivalent ABI from size.
- Compared actual GOG .text bytes to owner-validated CD runtime log Rayman2Fix(20261125-153417).log: all 500 bytes across nine logged overlay regions match (RVAs 710C0, 75C7F, 75C96, 7135C, 7115C, 7126B, 71100, 71346, 7131E). This is limited read-only evidence; all active hooks and native data structures still require preflight. No GOG gameplay validated.

Installation candidate: retain original data/ and dll/ plus GOG EXE; keep the supplied snd_cpa.ini rather than requiring the CD MMX template. Review GOG wrapper imports and side effects before excluding ddraw.dll, dinput.dll, 3DfxSpl2.dll and goginput.ini. Replace Glide with GameFrame under existing policy. Exclude installer-only temp/redist/commonappdata and obsolete config utilities after dependency review. Preserve game documentation and language resources.

Local Python can read supplied files and compute hashes; innoextract is not installed in the analysis environment. Full extraction/integrity testing and the Windows extractor build are outstanding. No proprietary bytes added to repository. No launch authorization for this profile yet.
