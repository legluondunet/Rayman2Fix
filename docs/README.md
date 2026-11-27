# Rayman2Fix — Guide de l'installation

Rayman2Fix modernise les éditions PC CD-ROM et GOG prises en charge de **Rayman 2: The Great Escape**.
Cet exécutable ne contient ni les fichiers commerciaux du jeu, ni son exécutable déchiffré.

## Installation neuve uniquement

1. Créer un **nouveau dossier vide** pour Rayman2Fix. Ne pas placer le nouveau
   lanceur dans une installation utilisant l'ancienne arborescence `rayman2/`
   ou `Rayman2Fix/` : aucune migration automatique n'est prise en charge.
2. Copier `Rayman2Fix.exe` et **une seule source** dans ce dossier :
   - CD : une image `.iso` ou `.cue` avec ses pistes référencées, ainsi que
     l'exécutable déchiffré `Rayman2.exe` approuvé si aucun delta approuvé
     n'est intégré au lanceur.
   - GOG : l'installateur hors ligne `.exe` et tous ses fichiers `.bin`
     associés. Aucun exécutable de jeu supplémentaire ni GOG Galaxy requis.
     Le lanceur utilise innoextract intégré sans exécuter le setup d'origine.
3. Exécuter simplement **`Rayman2Fix.exe`** : le lanceur installe,
   vérifie et démarre le jeu automatiquement.
   L'option `--install-only` est facultative : elle sert uniquement à
   installer ou vérifier **sans lancer le jeu**.

Le lanceur refuse les versions inconnues de l'exécutable ou un support CD
non reconnu ou un installateur GOG incompatible. Il conserve l'original protégé dans `game/rayman2.exe.bak`
lorsqu'un remplacement CD approuvé a lieu. L'exécutable GOG original
est conservé, sans remplacement. Aucune copie d'un ancien jeu ou de
ses sauvegardes n'est effectuée automatiquement.

## Arborescence

```text
Rayman2Fix.exe
Rayman2Fix.ini
Rayman2Fix_installer.log    (créé à l'installation ; conservé ensuite)
Rayman2Fix.log              (créé au premier lancement ; conservé ensuite)
Rayman2Fix_inspect.log      (lors de --inspect)
game/
    rayman2.exe
    rayman2.exe.bak         (si correctif approuvé appliqué)
    fix.dll
    glide2x.dll             (GameFrame Glide 2, vérifié par SHA-256)
    SDL3.dll                (SDL3 x86, vérifiée par SHA-256)
    glide2x.dll.bak         (ancienne DLL différente, si remplacement)
    rayman2.exe.manifest
    ubi.ini
    data/
    dll/
mods/
    textures/
        logo.rgba           (facultatif, fourni par l'utilisateur)
docs/
    LICENSE                   (licence GNU GPLv3 du projet)
    GameFrame-LICENSE-APACHE.txt
    SDL3-LICENSE.txt
    innoextract-notices.txt
    build-info.txt
    README.md
state/
    .rayman2fix-cd-files.tsv
    .rayman2fix-exe-patch.tsv  (si correctif CD approuvé appliqué)
    .rayman2fix-gog-files.tsv  (GOG, à la place des reçus CD)
```

- `game/` : fichiers nécessaires à Rayman 2 et DLL native du correctif.
  Toute version inconnue de `game/fix.dll` est remplacée par celle embarquée
  dans le lanceur, sans conserver de `fix.dll.bak`. Les anciens fichiers
  de sauvegarde propres à `fix.dll` sont supprimés après vérification.
  Cette règle ne concerne pas `game/rayman2.exe.bak`.
- `mods/` : ressources facultatives personnelles. Si `Log=1`, le journal
  d'installation contient `MODS: intro_logo=missing`, `invalid-size`,
  `invalid-header` ou `header-valid` pour le fichier facultatif
  `mods/textures/logo.rgba`. `header-valid` vérifie seulement le format du
  fichier : seul un message `RUNTIME-GLIDE: INTRO-LOGO-HD: tile=...`
  dans le journal du jeu confirme le remplacement effectif d'une texture.
- `game/glide2x.dll` : GameFrame intégré au lanceur, installé et vérifié
  à côté de l’exécutable du jeu. Une DLL différente est conservée en
  `game/glide2x.dll.bak` avant remplacement ; aucun ajout au `PATH` n’est requis.
- **`docs/` : documentation, licence GNU GPLv3 du projet, notice Apache 2.0
  de GameFrame et `build-info.txt`.** Le lanceur extrait automatiquement
  `docs/LICENSE` et `docs/GameFrame-LICENSE-APACHE.txt` séparément.
- **`state/` : reçus d'installation et d'intégrité SHA-256 uniquement.**
  Ne pas les supprimer : ils sont indispensables à la vérification normale.
- `Rayman2Fix.ini` : unique fichier de configuration utilisateur du correctif.
  Lors d'une installation neuve, `[Debug] Log=1` permet de recueillir
  `Rayman2Fix_installer.log` puis `Rayman2Fix.log` pendant le premier
  lancement **vérifié et terminé normalement**. Après cette première
  session, l'installateur remplace automatiquement `Log=1` par `Log=0`.
  `--install-only`, `--verify` et les échecs de démarrage ne déclenchent
  pas ce changement. Un `Log=1` réactivé manuellement reste activé.
  **Les deux premiers journaux sont conservés**, y compris lors des
  lancements suivants avec `Log=0`. Ces lancements ne créent, ne modifient
  et ne suppriment aucun journal. Pour de nouveaux diagnostics, réactiver
  manuellement `Log=1` (les journaux seront alors remplacés à chaque
  nouvelle session correspondante, et non cumulés).
  La résolution de sortie suit automatiquement celle de l'écran principal ;
  il n'existe plus d'option `Resolution`. Le FOV 3D s'adapte au format
  de l'affichage, sans modifier le mode Glide interne 1024×768 en 4:3.
  `AspectRatio=Auto` conserve le panoramique et `AspectRatio=4:3` centre
  l'image classique. `game/ubi.ini` reste une configuration interne au jeu.
- Les journaux sont conservés à côté du lanceur, jamais dans `state/`
  ni dans `docs/`.

## Utilisation et options

- `Rayman2Fix.exe --verify` : vérifier les données locales CD ou GOG.
- `Rayman2Fix.exe --inspect image.iso` : inspecter le support et produire
  `Rayman2Fix_inspect.log`.
- `Rayman2Fix.exe --help` : consulter les commandes disponibles.
- `[Features] SkipCinematics=1` (défaut) : autoriser Espace/Entrée/Échap
  à passer `jail_20` et la cinématique `learn_10` (saut natif F248 protégé).
  La première introduction `jail_10` et les autres cinématiques restent inchangées.
  Pour le débogage uniquement, ajouter manuellement `[Debug] DisableLearn10Skip=1`
  pour désactiver le saut de `learn_10` sans toucher à celui de `jail_20`.
  Cette option cachée n'est jamais générée par l'installateur.
- `[Features] RemoveDialogueBackgrounds=1` (défaut) : masquer les fonds
  natifs noirs derrière les dialogues.
- `[Features] RemoveCinematicBars=1` (défaut) : essayer de retirer les
  bandes cinématiques natives ; fonctionnalité expérimentale encore à
  confirmer visuellement dans le jeu.
- `[Features] MusicInPauseMenu=1` (défaut) : maintenir la musique du
  niveau pendant le menu Échap. Mettre une option à `0` pour la désactiver.

Ces quatre options sont activées pour une installation neuve. Une valeur
`0` déjà présente dans un INI existant reste respectée.

Le chargement du runtime `game/fix.dll` utilise l'injection validée ; la
bibliothèque GameFrame est chargée depuis `game/glide2x.dll`.
Aucune DLL de substitution `winmm.dll` n'est distribuée par Rayman2Fix.

## Manettes

La bibliothèque SDL3 3.4.20 x86 est installée automatiquement. Aucune
configuration supplémentaire ni activation par Start n'est nécessaire.
Une seule manette commande le jeu à la fois ; une nouvelle action sur
une autre manette peut prendre le relais lorsque la précédente est au repos.
Le clavier reste utilisable.

Dans Options → Contrôles (menu principal ou pause Échap), le dessin
s'adapte à la famille de manette et les commandes physiques s'illuminent.
Ce menu est informatif : il ne permet pas de modifier le mappage.
Rond/B ou Échap permettent de revenir au menu précédent.

| Action | Xbox | PlayStation |
|---|---|---|
| Déplacement | Stick gauche | Stick gauche |
| Sauter / remonter* | A | Croix |
| Tirer | B | Rond |
| Caméra gauche / droite | X ou LB / Y ou RB | Carré ou L1 / Triangle ou R1 |
| Rotation caméra | Stick droit | Stick droit |
| Cibler / plonger* | LT | L2 |
| Statistiques | RT | R2 |
| Connaissance du monde* / appeler Murfy | Croix haut / bas | Croix haut / bas |
| Pause | Start | Options |

*La nage et la connaissance du monde restent à vérifier spécifiquement
en jeu. L'illumination valide la réception de l'entrée physique, pas
l'effet natif dans chaque situation. Les deux familles ont été testées
sous Wine. Les tests de jeu sous Windows natif sont différés faute de
matériel disponible et ne bloquent pas la publication ; les retours de la
communauté seront bienvenus. La CI Windows x86 reste obligatoire.

La notice zlib de SDL3 est installée dans `docs/SDL3-LICENSE.txt`.

## Documentation complète

La documentation de développement, les recherches et les protocoles d'essai
sont publiés dans le [répertoire docs du dépôt officiel](https://github.com/legluondunet/Rayman2Fix/tree/main/docs).

Le lanceur est distribué en **un seul fichier** et installe lui-même ses
composants libres au premier démarrage. Il ne distribue ni les données du
jeu ni un exécutable commercial déchiffré. Pour l'édition CD actuellement
prise en charge, il faut encore fournir localement un exécutable
déchiffré approuvé ou un delta de patch approuvé déjà intégré au lanceur.
Rayman2Fix est placé sous licence GNU GPL version 3 uniquement
(`GPL-3.0-only`) ; le texte intégral est installé dans `docs/LICENSE`.
La licence Apache 2.0 de GameFrame est fournie séparément. Les notices
innoextract et de ses dépendances sont installées dans
`docs/innoextract-notices.txt`. L'édition GOG conserve son exécutable
original approuvé ; les archives d'origine restent intactes.
