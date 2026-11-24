# Rayman2Fix — analyse de LEVELS0.DAT et du menu Résolution

**Date :** 21 novembre 2026  
**Méthode :** lecture seule, sans modification ni redistribution des ressources originales.

## Sources examinées

- `levels0.dat` — 150 775 808 octets ; SHA-256 `8a9a1dc7118b31c9d63fdb901889a073f23641a5d0933d3af839e0df71e82b17`.
- `menu.sna` — 638 463 octets ; SHA-256 `73e754887d85954ee611b6fbff118293fa9f264f5f165c3e9b2823176998ecbb`.
- Résultats précédents établis avec `fix.sna` et `fix.rtb` : tables multilingues, identifiants de texte 379–385.

## 1. Table de relocation du niveau Menu : extraite et vérifiée

L'indexation du fichier suit la transformation décrite dans le projet libre Rayman2Lib (`getOffsetInBigFile` et `readRelocationTableFromBigFileAuto`). Pour **le niveau 0 (`Menu`) et la table 0 (`RTB`)**, le calcul aboutit à :

| Élément | Valeur |
| --- | --- |
| Index DAT (70 niveaux) | 0 |
| Index de table (`RTB`) | 0 |
| En-tête d'index secondaire | `0x009F1000` |
| Début de la table de relocation | **`0x00E65000`** |
| Taille de la table encodée | **68 203 octets** |
| Groupes | **3** |
| Total des entrées | **8 522** |

Les groupes décodés sont exactement compatibles avec les blocs de `menu.sna` :

| Module / bloc | Relocations |
| --- | ---: |
| 4 / 1 | 1 600 |
| 5 / 2 | 5 325 |
| 13 / 1 | 1 597 |

**7 923** références pointent vers un bloc non vide présent dans ce `menu.sna` ; leurs adresses cibles sont validées dans ses limites. **599** références ciblent d'autres blocs (dont des blocs communs/externes ou à charge utile vide), qui ne peuvent pas être résolues directement avec ce seul fichier. Ce sont des références externes, pas des erreurs démontrées.

## 2. Chaîne contrôleur → intelligence artificielle du menu

Les pointeurs reconstruits permettent de suivre le contrôleur graphique depuis un SuperObject jusqu'à son modèle d'IA :

| Structure | Adresse virtuelle **des données de scène** |
| --- | --- |
| SuperObject du contrôleur | `0x07AB2250` |
| Personnage / acteur | `0x07E70908` |
| Brain | `0x07E70AEC` |
| Mind | `0x07920990` |
| AIModel | **`0x079209A8`** |

Le modèle possède **27 comportements normaux** et **7 comportements réflexes**. Sur les scripts que nous avons pu analyser, **9** comportent des appels vidéo ou des identifiants de texte du menu Résolution.

**Nuance importante :** l'acteur dont le nom est `YAM_GenMenu_I1` possède une autre chaîne (`Perso=0x07E70B14`, `AIModel=0x07963C44`) : ses scripts ne contiennent pas directement les commandes de résolution repérées ci-dessous. Le contrôleur de ces commandes est l'acteur à `0x07E70908` ; il ne faut pas attribuer à tort toutes les fonctions vidéo à `YAM_GenMenu_I1`.

## 3. Appels du moteur identifiés dans les scripts

L'interprétation des identifiants d'appel est fondée sur les tables d'AI **Rayman 2** publiées par Raymap (`AITypes_R2.cs`), et les nœuds proviennent du décodage des fichiers fournis.

| Comportement | Script (adresse de scène) | Appels vérifiés |
| --- | --- | --- |
| Normal 5, script 3 | **`0x0792F210`** | `VID_SelectResolution` × 3, `VID_SaveCurrentResolution`, textes 379–385, requêtes de modes disponibles |
| Normal 19, script 2 | **`0x0795074C`** | `VID_SelectResolution` × 1 |
| Réflexe 5, script 2 | **`0x0795A7D8`** | `VID_ChangeBrightness`, lecture de luminosité et des modes disponibles |

Les trois premiers appels de `VID_SelectResolution` sont aux adresses de scène : `0x0792F350`, `0x0792F3A0`, `0x0792F3F0` ; leurs arguments constants observés sont **0, 1 et 2**. Un autre appel existe à `0x07950794`.

L'appel `VID_SaveCurrentResolution` apparaît à **`0x0792FF70`**. L'appel `VID_ChangeBrightness` apparaît à **`0x0795AF30`**.

Ces résultats démontrent le **lien statique entre textes, scripts et API vidéo**, mais ne démontrent **pas encore quel comportement est actif lorsque l'écran « Résolution » est ouvert dans une session réelle**. Les adresses indiquées proviennent des données SNA décodées, pas d'une résolution fiable de l'adresse mémoire du jeu à l'exécution.

## 4. Conséquences pour Rayman2Fix

La suite ne nécessite pas de rechercher un `Menu.rtb` autonome ni de modifier les ressources commerciales : les informations de relocation du menu sont bien présentes dans `LEVELS0.DAT`.

Pour remplacer le menu par une page **carte graphique, pilote, résolution, profondeur, ratio** non modifiable, il faudra encore :

1. Repérer à l'exécution l'état exact du sous-menu via le contrôleur et ses comportements, sans se fier au drapeau de menu générique.
2. Empêcher uniquement les appels d'application/enregistrement de résolution **issus de ce sous-menu**, pas les changements indispensables à l'initialisation Glide.
3. Réutiliser les fonctions de rendu/texte natives pour l'affichage en lecture seule, compatible multilingue et modes Auto/4:3.
4. Conserver **`VID_ChangeBrightness`** et les autres options ; vérifier sous Wine et Windows.

**Aucun hook ni changement de mode vidéo n'a été appliqué pendant cette recherche.**

## Reproduction locale

```bash
python3 inspect_menu_levels.py /chemin/vers/levels0.dat /chemin/vers/menu.sna > rapport.json
python3 -m unittest discover -s tests -v
```

Le script ne modifie aucun fichier du jeu et n'émet que des métadonnées et identifiants de fonctions.

## Références publiques de formats

- [Rayman2Lib — DAT et index des ressources](https://github.com/szymski/Rayman2Lib/blob/master/d_tools/rayman2lib/source/formats/datfile.d)
- [Rayman2Lib — relocations RTB dans les DAT](https://github.com/szymski/Rayman2Lib/blob/master/d_tools/rayman2lib/source/formats/relocationtable.d)
- [Raymap — fonctions/procédures IA de Rayman 2](https://github.com/byvar/raymap/blob/master/Assets/Scripts/OpenSpace/AI/AITypes/AITypes_R2.cs)