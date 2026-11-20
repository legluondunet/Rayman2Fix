# Rayman2Fix — feuille de route de développement

État au **20 novembre 2026**. **Aucune release n'est prévue à court terme.**
La branche `main` contient les PR #35 (saut F248), #36 (Glide dans
`game/`) et #37 (journaux du premier lancement conservés).
[Dernière CI `main` réussie](https://github.com/legluondunet/Rayman2Fix/actions/runs/44755064157).

## Chantier prioritaire A — corriger les craquements audio

- [ ] Reproduire séparément les craquements de musique, effets sonores
  et transitions (démarrage, jeu, ESC, reprise et Alt+Tab). Noter
  si les symptômes diffèrent entre Wine 11.19 et Windows natif.
- [ ] Établir un **témoin** en `[Features] MusicInPauseMenu=0`,
  puis comparer à `=1` sans désactiver durablement la fonction.
  Ne pas présumer que les défauts proviennent des hooks du menu ESC :
  l'audio ordinaire du jeu et sa chaîne native sont une cause possible distincte.
- [ ] Analyser sans modifier le flux audio les paramètres du jeu
  (`snd_cpa.ini`), les pilotes `WAVMWbvr.dll` / `APMMXbvr.dll`,
  les décodages ADPCM, les transitions SXD, la cadence et les
  tailles tampons. Éviter les logs sur le thread audio.
- [ ] Isoler et corriger une cause **générale** plutôt que des sons
  individuellement. Ne pas ajouter de réparation qui traite des données
  propriétaires ou modifie l'état de sauvegarde.
- [ ] Confirmer l'absence de craquements et de régressions sur
  Windows/Wine, musique pendant Échap, reprise, changement de piste,
  Alt+Tab et retour au bureau.

## Chantier prioritaire B — SDL3 et manettes

- [ ] Ajouter la dépendance **SDL3 32 bits Windows** au processus
  d'assemblage, vérifier ses notices applicables et l'embarquer dans
  le **seul** `Rayman2Fix.exe`. Installer la DLL auprès du runtime
  sans dépendance à une installation SDL3 externe.
- [ ] Définir l'architecture d'injection des entrées SDL3 avec
  clavier natif préservé, sans changement de rendu, de FOV,
  de focus ou des traitements audio.
- [ ] Découvrir/ouvrir/fermer les manettes SDL3, gérer branchement
  et débranchement à chaud ; une manette absente ne doit pas
  empêcher le jeu de démarrer.
- [ ] Implémenter les axes, gâchettes et boutons utiles avec
  états maintenus, seuils d'analogique, anti-répétition et neutralisation
  lors de la perte de focus ; éviter les événements clavier « collés ».
- [ ] Tester les contrôleurs Xbox 360 et DualShock 4 sous Wine
  et Windows ; vérifier aussi le fonctionnement sans manette
  et avec le clavier seul.
- [ ] Vérifier indépendamment les menus, mouvements, caméra,
  pause, reprise, reconnexion et Alt+Tab.

## État des pistes déjà terminées et isolées

- [x] `SkipCinematics=1` : saut F248 de `learn_10` validé et intégré
  dans `main` ; les **PR #30, #31 et #32 ont été fermées sans fusion**.
- [x] `[Debug] Log=1` seulement pour la première installation et la
  première session vérifiée réussie ; passage automatique à `Log=0`
  et **conservation des deux journaux initiaux** sur les lancements suivants.
- [x] Tests de compilation et d'intégrité automatisés Linux et Windows x86
  sur `main` après PR #37.
- [x] Licence choisie : **GNU GPL version 3 uniquement**
  (`GPL-3.0-only`). Le texte officiel figure dans `LICENSE`
  et sera déployé sous `docs/LICENSE` par le lanceur.
  GameFrame conserve séparément sa licence Apache 2.0.

## Préparation d'une release — volontairement reportée

Ne reprendre ce chantier **qu'après les validations audio et SDL3**.
Les points à réexaminer alors : test de la compilation standard sous
Windows natif, politique légale et technique des patchs de l'exécutable
commercial, couverture des éditions CD, notices des dépendances,
tag/version et archive publique dépourvue de données du jeu.

Le paquet CI standard actuel n'embarque ni les données de Rayman 2,
ni son exécutable déchiffré, ni le patch privé. L'installation automatique
utilise une ISO/CUE et un binaire déchiffré local approuvé, ou un delta
approuvé déjà embarqué. Ne pas présenter cela comme un installateur
autonome universel tant que le problème de distribution n'est pas résolu.
