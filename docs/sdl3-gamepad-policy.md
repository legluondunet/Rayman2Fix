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
