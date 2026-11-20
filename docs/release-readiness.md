# Rayman2Fix — préparation de la première release

État au **20 novembre 2026**. Ce document décrit les critères de publication,
pas une annonce de release. La branche `main` intègre les PR #35, #36 et #37.
Le workflow de compilation et de tests Linux/Windows x86 de `main` est passé
après la fusion #37 : [run #44755064157](https://github.com/legluondunet/Rayman2Fix/actions/runs/44755064157).

## Points bloquants avant publication

- [ ] **Licence de Rayman2Fix** : le propriétaire doit sélectionner explicitement
  la licence du code (et la version exacte, par exemple GPL-3.0-only ou
  GPL-3.0-or-later). Ajouter le texte officiel `LICENSE` et l'intégrer à
  `docs/` dans la distribution. Conserver la notice **Apache 2.0** propre
  à GameFrame, sans la présenter comme la licence de Rayman2Fix. Vérifier
  les notices de tous les autres composants inclus.
- [ ] **Distribution juridiquement et techniquement autonome** : choisir la
  politique de distribution du remplacement de l'exécutable d'origine.
  Le paquet CI actuel n'embarque **ni** le jeu, **ni** l'exécutable
  déchiffré, **ni** le delta binaire privé. Pour les éditions CD actuellement
  validées, l'utilisateur doit encore fournir localement un exécutable
  déchiffré dont la taille et le SHA-256 sont approuvés. Un éventuel delta
  intégré devra faire l'objet d'une revue juridique et d'une validation
  byte-for-byte, et ne devra pas être publié à partir de fichiers
  commerciaux sans autorisation.
- [ ] **Version et tag** : sélectionner le numéro de release, remplacer
  les libellés `0.1.0-bootstrap` si nécessaire, puis taguer **le même
  commit que l'artefact testé**. Aucune version stable n'a encore été
  publiée sur le dépôt.

## Matrice de validation à clôturer

- [x] **CI** : compilation et tests automatisés Linux et Windows x86 sur
  `main` après #37.
- [x] **Wine 11.19** : installation depuis ISO avec un exécutable approuvé,
  vérification de 2 095 fichiers, lancement et sortie propres, saut natif
  `jail_20` puis `learn_10`, et auto-bascule `Log=1` → `Log=0`
  validés lors des tests de novembre.
- [x] **Journaux conservés** : les deux fichiers du premier lancement
  restent présents après le deuxième lancement avec `Log=0`; tests
  automatiques de non-modification octet par octet.
- [ ] **Windows natif** : essai utilisateur de la *compilation standard*
  Windows x86 sous Windows 11 (et non une DLL de recherche) :
  installation ISO/CUE propre ; affichage sans bordures ;
  `AspectRatio=Auto` et `4:3` ; FOV, HUD, textes et cinématiques ;
  `SkipCinematics=1` pour `jail_20` et `learn_10` ;
  `MusicInPauseMenu=1` ; perte/reprise du focus et Alt+Tab ;
  sortie normale et éventuelle relance.
- [ ] **Relance installateur** : conserver l'ISO et les fichiers `game/` /
  `state/`, puis relancer le même `Rayman2Fix.exe` pour vérifier
  l'installation existante sans effacer les fichiers utilisateur ni
  les deux premiers journaux.
- [ ] **Revue du livrable** : vérifier qu'un paquet public contient seulement
  le `Rayman2Fix.exe` standard, le code source et les notices applicables,
  sans données du CD, EXE déchiffré, outils de recherche activés ni logs
  personnels. Examiner les dépendances, le SHA-256 de GameFrame et la
  reproductibilité des informations de build.

## Périmètre et recherches non bloquantes

- Les PR de recherche #30, #31 et #32 sont **encore ouvertes en brouillon**,
  avec des branches empilées. Leur ancien prototype de saut `learn_10`
  ne doit pas être confondu avec la route F248 validée et déjà dans `main`.
  Les clôturer ou les archiver explicitement avant le nettoyage des branches
  demande une décision séparée ; ne pas les fusionner pour préparer
  une release.
- Le contrôleur SDL3 est reporté et ne fait pas partie de la première
  fonctionnalité validée. Ne pas annoncer la prise en charge d'une manette.
- Le logo HD facultatif exige un fichier PNG fourni par l'utilisateur ;
  il ne doit pas être inclus dans le paquet public.
- Les options de recherche audio/affichage et les artefacts
  `audio-trace` et `esc-music-prototype` restent exclus des paquets
  destinés aux utilisateurs.

## Procédure d'installation prévue pour la première version

Placer **un** support CD `.iso` ou `.cue` (avec ses fichiers `.bin`)
à côté de `Rayman2Fix.exe`, et l'exécutable déchiffré approuvé si aucun
delta approuvé n'a été intégré au lanceur. Exécuter **`Rayman2Fix.exe`
sans argument** pour installer, vérifier et lancer. `--install-only` ne
sert qu'à installer/vérifier sans démarrer le jeu. Après la première
session vérifiée terminée normalement, `[Debug] Log=0` est écrit dans
l'INI ; **les deux journaux initiaux sont conservés sans modification**.
