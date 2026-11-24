# Rayman2Fix — SDL3 manettes, étape 0 : politique de focus et hotplug

Statut : **base de tests uniquement**. Ce patch ne fait **pas encore fonctionner une manette dans Rayman 2**. L’exécutable, le runtime, les ressources embarquées et le comportement du clavier sont inchangés.

## Pourquoi commencer ici ?

Le lanceur actuel recense certains périphériques XInput / DirectInput, mais il n'acquiert aucune entrée. Il n'existe pas encore de contrat natif vérifié pour transmettre les boutons au moteur Rayman 2. Synthétiser des touches globales avec `SendInput` serait prématuré : perte de focus, répétitions et doubles entrées clavier sont difficiles à maîtriser.

`include/rayman2fix/gamepad_input_policy.hpp` définit une **politique C++17 sans dépendance SDL3** :

- entrée normalisée par l'adaptateur SDL3 futur (identifiant de périphérique, connected, foreground, axes, d-pad, boutons) ;
- priorité absolue au focus du vrai jeu : aucune entrée si hors focus ;
- libération des actions à la déconnexion, au changement de propriétaire ou à la perte de focus, sans répétition de libérations ;
- transitions `held / pressed / released` déterministes, sans répétition synthétique ;
- seuil analogique signé avec zone morte et neutralisation des directions contradictoires ;
- isolation complète du clavier natif (aucune injection pour ce modèle).

`tests/gamepad_input_policy_tests.cpp` couvre la perte de focus, l'absence de manette, le changement de périphérique, la reconnexion, les boutons et les axes extrêmes. CI Windows x86 et Linux, sans SDL3 liée.

## Étapes suivantes (issue #40)

1. Déterminer **l'API d'entrée réellement consommée par l'exécutable CD vérifié**, sans écriture mémoire ni hook spéculatif.
2. Choisir une révision SDL3 Windows x86 reproductible, auditer les notices et empreintes SHA-256, l'intégrer à l'EXE unique sous `game/` ou `mods/` selon le point de chargement validé.
3. Ajouter l'initialisation SDL3 et les événements Gamepad, puis alimenter le modèle depuis une source de snapshots bornée ; éviter les événements globaux, les fenêtres SDL parasites et les blocages de rendu.
4. Construire un adaptateur indépendant du clavier qui applique les transitions uniquement au point d'entrée moteur confirmé, avec arrêt propre et repli clavier.
5. Tester sous Windows et Wine les manettes Xbox 360 et DualShock 4, l'entrée dans les menus, le gameplay, la caméra, le branchement à chaud, Alt-Tab et les régressions audio.

La future correspondance entre `primary/secondary/camera_*` et les actions de Rayman 2 reste **à valider** ; ce n'est pas une promesse de mapping définitif.

## Reprise après l'intégration VIDEO (#53)

Base : main, commit 9a2f7ab9. La PR #48 est reprise sans modifier
ses tests ni activer de nouvelles entrées dans le runtime.

Le hook existant `src/runtime/video_navigation_input.cpp` authentifie
la sortie de GetDeviceState(256) du clavier au RVA 0x966B3. C'est une
route candidate pour les commandes numériques, pas une preuve de
compatibilité avec toutes les actions natives. Ne pas installer un
second detour au même emplacement. La future intégration devra partager
le hook, préserver son HRESULT et appliquer le filtre VIDEO après la
fusion clavier/manette. Le mode analogique, les actions de caméra et
l'évitement de la double entrée DirectInput restent à caractériser.

Les touches ESC, Enter et les raccourcis cinématiques, ainsi que le
menu Contrôles, devront faire partie des tests. Pas de SendInput global.
La dépendance SDL3 x86 et sa licence zlib seront vérifiées et embarquées
avant l'activation réelle. Voir #40 pour la version candidate et son hash.

## SDL3 dependency verification (24 November 2026)

The official SDL release listing publishes SDL3 3.4.20 for Windows x86:
https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20

Archive SHA-256: `f6fbf5afa42195a01b1da8adf25567377d0e212f4b2b6770923903eb3feea254`.
The Windows CI downloads this exact archive and audits its hash, PE32 i386
DLL identity, required Gamepad API exports and zlib notice before writing
only fixed output filenames. The audit JSON records the extracted DLL and
notice digests. A separate x86 test loads the audited DLL by absolute path,
checks the exact SDL version, initializes only GAMEPAD on its main thread,
disables event processing, updates and enumerates devices, then shuts down.
No controller is required on the CI runner. This smoke test is not proof
of game input or physical hotplug. No SDL binary is embedded or executed
inside Rayman 2 by this phase. The final DLL hash will be recorded from a
successful audit before packaging.

Native integration must use SDL initialization on the game's main thread,
not the existing background menu discovery workers. Preserve the original
GetDeviceState result and the VIDEO navigation filter. The analog route
and duplicate native controller handling remain open; no keyboard mapping
is approved yet.
