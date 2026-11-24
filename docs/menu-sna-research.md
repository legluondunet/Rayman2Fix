# Rayman 2 PC menu.sna: offline inventory (21 November 2026)

**Scope:** analysis of the maintainer-supplied `Data/World/Levels/Menu/Menu.sna`,
not an implementation of the read-only Options → Resolution screen.

The file has **638,463 bytes**, SHA-256
`73e754887d85954ee611b6fbff118293fa9f264f5f165c3e9b2823176998ecbb`.
The first four bytes are `79 cc b5 6a` (little endian `0x6AB5CC79`).
The rest is Rayman 2's known advancing XOR mask. With it removed, the
stream parses **exactly to EOF into 13 memory blocks**, of which **3
have nonzero payload sizes**:

| Module | ID | Payload start (decoded file offset) | Payload bytes | Purpose (tentative) |
|---|---|---:|---:|---|
| 4 | 1 | 257 | 62,328 | Actor / scene name structures |
| 5 | 2 | 62,608 | 268,020 | Geometry or model content (not decompiled) |
| 13 | 1 | 330,651 | 307,812 | Navigation / waypoint structures (not decompiled) |

All other blocks in this SNA have a zero-sized payload, but still carry
relocation metadata. Header fields and block sizes are consistent with
the parser in `szymski/Rayman2Lib` and `byvar/raymap`.

### Identifiers observed in the supplied PC file

- `YAM_GenMenu_I1` / `YAM_GenMenu`: strong **menu actor candidates**
- `YAM_CurseurMenu_I1` / `ARG_CurseurMenu`: menu cursor candidates
- `YAM_GenCibleCamera_I1`: menu camera target candidate
- `Levelmenu`: scene family or world identifier
- `YAM_Reso_Francesco` / `YAM_Reso_Baleine` occur in the **module 13 waypoint
  content**, beside `Menu\\Menu.wp^Isolate^WayPoint:`. The substring
  `Reso` **does not prove** that either denotes video resolution.

Strings `Resolution`, `Résolution`, `Options`, `GLI_Mode`, `Glide`,
`video` and `graphics` have no plaintext equivalent in the decoded
file. This is a statement about the **string inventory**, not a finding
that corresponding AI script functionality is absent: script calls and
localization text IDs are binary.

### Critical limitation

**Do not infer a specific Options → Résolution state from `menu.sna`
on these findings alone.** The validated Wine trace logs generic
menu state and the engine's static 1024x768x16 mode, not a submenu ID.
No video-changing hook is justified yet.

The next non-invasive investigation is to resolve the **AI model,
behaviors, and script IDs** for `YAM_GenMenu` and correlate them with
the game scene during Options → Résolution entry/exit. A copy of
`Data/World/Levels/Fix.sna` may help resolve shared text labels (and
`Menu.dsb` may help behavior names), but neither automatically proves
submenu ownership. Never distribute modified/full original SNA assets.

### Reproduction

```bash
python3 tools/inspect_rayman2_sna.py /path/to/Data/World/Levels/Menu/Menu.sna
python3 -m unittest discover -s tests -p 'test_sna_menu_inventory.py'
```

The script only prints a bounded JSON inventory (metadata, SHA-256,
candidate actor names/offsets). It does **not** emit an unencrypted
resource or patch game data. Synthetic tests use no proprietary assets.

Public format algorithm references:
- https://github.com/szymski/Rayman2Lib/blob/master/d_tools/rayman2lib/source/decoder.d
- https://github.com/byvar/raymap/blob/master/Assets/Scripts/OpenSpace/IO/Reader.cs
- https://github.com/byvar/raymap/blob/master/Assets/Scripts/OpenSpace/FileFormat/SNA.cs
