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
2. Choisir une révision SDL3 Windows x86 reproductible, auditer les notices et empreintes SHA-256, l'intégrer à l'EXE unique sous `game/`, à côté de `fix.dll` et `glide2x.dll`.
3. Ajouter l'initialisation SDL3 et les événements Gamepad, puis alimenter le modèle depuis une source de snapshots bornée ; éviter les événements globaux, les fenêtres SDL parasites et les blocages de rendu.
4. Construire un adaptateur indépendant du clavier qui applique les transitions uniquement au point d'entrée moteur confirmé, avec arrêt propre et repli clavier.
5. Tester sous Windows et Wine les manettes Xbox 360 et DualShock 4, l'entrée dans les menus, le gameplay, la caméra, le branchement à chaud, Alt-Tab et les régressions audio.

La disposition Dreamcast adaptée aux manettes SDL3 est approuvée par le mainteneur (24 novembre 2026). Les identifiants de commandes natifs restent à vérifier. Voir le contrat ci-dessous.

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
inside Rayman 2 by this phase. The final DLL hash must be pinned from the successful audit before packaging. CI run 45411813631 passed the archive audit and the x86 gamepad-only smoke test.

Native integration must use SDL initialization on the game's main thread,
not the existing background menu discovery workers. Preserve the original
GetDeviceState result and the VIDEO navigation filter. The analog route
and duplicate native controller handling remain open; no native engine input binding is approved yet.

## Mapping approuvé — Dreamcast + manettes modernes

SDL3 utilise les positions physiques normalisées, jamais un index
DirectInput propre à un modèle de manette.

| Action en jeu | SDL3 | Xbox | DualShock |
|---|---|---|---|
| Déplacement proportionnel | Left X/Y | Stick gauche | Stick gauche |
| Saut / hélicoptère | South | A | Croix |
| Tir / tir chargé | East | B | Rond |
| Caméra gauche | West, Left Shoulder, Right X négatif | X, LB, stick droit | Carré, L1, stick droit |
| Caméra droite | North, Right Shoulder, Right X positif | Y, RB, stick droit | Triangle, R1, stick droit |
| Ciblage / recentrage / plongée selon contexte natif | Left Trigger | LT | L2 |
| Statistiques | Right Trigger | RT | R2 |
| Connaissance du monde | D-pad Up | Haut | Haut |
| Murphy | D-pad Down | Bas | Bas |
| Pause | Start | Start | Options |

Dans les menus : stick gauche ou croix directionnelle, South valide,
East annule. Dans le panneau VIDEO en lecture seule, seul East (retour)
est transmis par ce modèle. Pendant les cinématiques sautables, South
demande le saut ; le moteur et ses gardes restent responsables du
comportement réel.

Le déplacement conserve un vecteur analogique normalisé, avec zone
morte radiale de 25 %, sans accélération diagonale. Le stick droit
commande la rotation horizontale ; aucune rotation verticale nouvelle
n'est inventée. Les seuils initiaux seront ajustés si les tests matériels
le justifient. Les transitions de contexte libèrent les actions et
attendent la neutralisation des boutons avant de réarmer les commandes,
pour éviter un saut déclenché par la validation précédente.

Ces correspondances sont codées et testées au niveau sémantique, mais
pas encore reliées aux commandes natives. L'identité des actions
Connaissance du monde / Murphy et le comportement exact en nage restent
à vérifier sur le moteur PC ; ne pas les simuler avec des touches
arbitraires.

## Menu Contrôleur — contrat approuvé

Le mapping Dreamcast enrichi est unique, sans remapping, profil ni option
INI pour cette première release. Le menu natif Contrôleur devient une
page informative en lecture seule, accessible au clavier même si aucune
manette n'est connectée.

- Conserver le titre, les transitions et les éléments décoratifs natifs
  lorsque leur identité est vérifiée. Réutiliser la présentation du panneau
  VIDEO (bleu nuit, opacité 40 %, contour bleu, angles arrondis, texte lissé,
  proportions 4:3) plutôt que créer une fenêtre externe.
- Une silhouette originale générique de manette à gauche, les commandes
  à droite. La silhouette ne doit pas reprendre des images commerciales.
  Les onze lignes du modèle `gamepad_control_info.hpp` sont la référence
  des libellés; les actions et libellés génériques seront localisés dans
  la langue du jeu. Vérifier leur lisibilité avant de figer la géométrie.
- En-tête : famille de la manette active, identifiée via SDL3 (Xbox,
  PlayStation, générique). Si aucune manette : afficher explicitement
  « Aucune manette connectée » et les positions physiques génériques,
  sans masquer la page. Ne pas inférer les symboles physiques d'une
  manette inconnue à partir d'un nom de périphérique.
- Actualiser les libellés sur connexion, déconnexion ou changement de
  manette active. Ne jamais modifier le mapping sous-jacent.
- Aucun curseur de sélection ni édition; retour via Échap ou bouton
  East (B / Rond / bouton droit). Les autres commandes sont neutralisées
  uniquement pendant la page authentifiée.
- Préserver le clavier natif, les autres menus, la musique ESC et le
  focus. Appliquer le remplacement aux menus Contrôleur effectivement
  présents, après identification de leurs contrôleurs natifs; ne pas
  supposer que les structures MENU et Pause sont identiques.

La politique sémantique possède un contexte `controller_info` et un
modèle de présentation séparé. Les tests vérifient les libellés par famille,
le cas sans manette et l'isolation de la navigation. Le dessin, la détection
SDL3 réelle de la famille, la localisation et la substitution du menu
natif restent à implémenter. L'écran ne doit pas annoncer des commandes
non opérationnelles dans une compilation distribuée.

## Packaging SDL3 (phase 1)

Windows CI 45411813631 verified the official SDL 3.4.20 x86 dependency:
- SDL3.dll SHA-256: `6d7e653aa9c762ceda4fffc054a504dcce273e016eb9f1fe079b6409a058a9f7`
- Original zlib notice SHA-256: `1c040b8271b37e5076359f8fd54240e371114112924d2df81ef87c7d6a1dfdfd`

Standard x86 CI supplies the audited directory to CMake. Both extracted hashes are pinned at configure time. RCDATA 1107 contains SDL3.dll and 1108 contains its original license. Normal provisioning compares installed bytes with the embedded payload, stages exclusively and replaces atomically if necessary. Deployment targets `game/SDL3.dll` and `docs/SDL3-LICENSE.txt`, per maintainer preference. The fresh-install workspace permits the DLL, and atomic CD publication copies and verifies its pinned SHA-256 separately from the CD receipt. `mods/` remains reserved for mod assets, not runtime DLLs. The notice is provisioned first, without altering the GPLv3 or GameFrame notices. No download or configuration by the user, no permanent backup, no global DLL installation. Unbundled developer builds remain possible. The DLL is not yet loaded by the game. Later runtime initialization must use the absolute installation-owned path, verify provenance and fail back to native keyboard input if unavailable. Do not load from the process search path.

Tests exercise initial deployment, repeat provisioning, replacement of corrupt DLL and notice, and preservation across atomic CD publication. This is packaging only, not proof of physical controller functionality.
