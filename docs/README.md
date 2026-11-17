# Rayman2Fix — Guide de l'installation

Rayman2Fix modernise l'édition PC CD-ROM prise en charge de **Rayman 2: The Great Escape**.
Cet exécutable ne contient ni les fichiers commerciaux du jeu, ni son exécutable déchiffré.

## Installation neuve uniquement

1. Créer un **nouveau dossier vide** pour Rayman2Fix. Ne pas placer le nouveau
   lanceur dans une installation utilisant l'ancienne arborescence `rayman2/`
   ou `Rayman2Fix/` : aucune migration automatique n'est prise en charge.
2. Copier `Rayman2Fix.exe` et **une seule** image CD d'origine `.iso` ou
   `.cue` (avec ses fichiers `.bin` requis) dans ce dossier.
3. Placer l'exécutable déchiffré `Rayman2.exe` approuvé à côté du lanceur,
   si aucun correctif binaire approuvé n'est déjà intégré au lanceur.
   L'installateur vérifie sa taille et son empreinte SHA-256.
4. Exécuter `Rayman2Fix.exe --install-only` pour installer et vérifier, sans
   lancer le jeu, puis `Rayman2Fix.exe` pour jouer.

Le lanceur refuse les versions inconnues de l'exécutable ou un support CD
non reconnu. Il conserve l'original protégé dans `game/rayman2.exe.bak`
lorsqu'un remplacement approuvé a lieu. Aucune copie d'un ancien jeu ou de
ses sauvegardes n'est effectuée automatiquement.

## Arborescence

```text
Rayman2Fix.exe
Rayman2Fix.ini
Rayman2Fix_installer.log    (si [Debug] Log=1)
Rayman2Fix.log              (journal du jeu, si activé)
Rayman2Fix_inspect.log      (lors de --inspect)
build-info.txt
game/
    rayman2.exe
    rayman2.exe.bak         (si correctif approuvé appliqué)
    fix.dll
    rayman2.exe.manifest
    ubi.ini
    data/
    dll/
mods/
    textures/
        logo.rgba           (facultatif, fourni par l'utilisateur)
third_party/
    gameframe/
        glide2x.dll
        GameFrame-LICENSE-APACHE.txt
docs/
    README.md
state/
    .rayman2fix-cd-files.tsv
    .rayman2fix-exe-patch.tsv  (si correctif approuvé appliqué)
```

- `game/` : fichiers nécessaires à Rayman 2 et DLL native du correctif.
- `mods/` : ressources facultatives personnelles.
- `third_party/` : bibliothèques tierces et leurs notices de licence.
- **`docs/` : documentation utilisateur uniquement.**
- **`state/` : reçus d'installation et d'intégrité SHA-256 uniquement.**
  Ne pas les supprimer : ils sont indispensables à la vérification normale.
- `Rayman2Fix.ini` : unique fichier de configuration utilisateur du correctif.
  `game/ubi.ini` est une configuration interne propre au jeu.
- Les journaux sont conservés à côté du lanceur, jamais dans `state/`
  ni dans `docs/`.

## Utilisation et options

- `Rayman2Fix.exe --verify` : vérifier les données locales du CD.
- `Rayman2Fix.exe --inspect image.iso` : inspecter le support et produire
  `Rayman2Fix_inspect.log`.
- `Rayman2Fix.exe --help` : consulter les commandes disponibles.
- `[Features] MusicInPauseMenu=1` : maintenir la musique du niveau pendant le
  menu Échap, désactivé par défaut. Une valeur `0` conserve le comportement natif.

Le chargement du runtime `game/fix.dll` utilise l'injection validée ; la
bibliothèque GameFrame est recherchée dans `third_party/gameframe/`.
Aucune DLL de substitution `winmm.dll` n'est distribuée par Rayman2Fix.

## Documentation complète

La documentation de développement, les recherches et les protocoles d'essai
sont publiés dans le [répertoire docs du dépôt officiel](https://github.com/legluondunet/Rayman2Fix/tree/main/docs).

L'exécutable est distribué en **un seul fichier**, puis extrait les fichiers
autorisés de Rayman2Fix au premier démarrage. La licence du projet lui-même
n'a pas encore été sélectionnée ; la notice Apache 2.0 du composant GameFrame
est conservée séparément avec cette dépendance.
