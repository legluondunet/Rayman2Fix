# Read-only localization of the Rayman 2 Resolution menu

Verified from the maintainer's owned Fix.sna, Fix.rtb and Menu.dsb.
No original assets or decrypted game data are committed.

Fix.sna: 7,682,868 bytes, 12 blocks (5 nonempty), masked XOR decoded.
Fix.rtb: 235,995 bytes, 5 relocation groups, 29,495 pointer records.
Its module 14 yields five pointer tables with 666 entries each.
Menu.dsb: 369 bytes, masked script; first section allocates memory and
the next section is 0x46, containing a Rayman.bmp bitmap reference.
Menu.dsb is NOT proof of submenu navigation code.

Native handles identical across English, French, Spanish, Italian, German:

ID    English           French
370   LANGUAGE          LANGUE
371   VIDEO             VIDEO
372   SOUND             SON
379   RESOLUTION        RESOLUTION
380   SAVE RESOLUTION   SAUVER RESOLUTION
381   LOW               BASSE
382   MEDIUM            MOYENNE
383   HIGH              HAUTE
384   OTHER             AUTRE
385   LUMINOSITY        LUMINOSITE

Handle 385 belongs to brightness; it must remain adjustable.
The full analysis contains the other three translations as well.

The approved 1999 x86 game EXE has a text lookup resolver at VA
0x494C70 / RVA 0x94C70. Its bytes match exactly in both owner-supplied
original and deprotected builds. It selects the current-language table
using globals 0x500FA8 and 0x50026C. Direct caller 0x4829E9 invokes it
when a compiled AI text expression has type 0x24.

This does not yet identify the active Resolution submenu or authorize
patching video mode changes. The matching Menu.rtb companion to Menu.sna
is needed to relocate the original YAM_GenMenu_I1 AI model and scripts.
There is no new runtime hook in this commit.

Reproduction with locally owned game assets:
python3 tools/inspect_rayman2_menu_texts.py Fix.sna Fix.rtb Menu.dsb
python3 -m unittest discover -s tests -p test_menu_texts.py

SNA/RTB syntax:
https://github.com/szymski/Rayman2Lib/blob/master/d_tools/rayman2lib/source/formats/relocationtable.d
https://github.com/byvar/raymap/blob/master/Assets/Scripts/OpenSpace/Text/LocalizationStructure.cs
