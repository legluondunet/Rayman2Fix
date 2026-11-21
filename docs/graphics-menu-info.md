# Options → Résolution : informations graphiques non modifiables

Phase 0 de [#49](https://github.com/legluondunet/Rayman2Fix/issues/49).
**Cette étape ne remplace pas encore le sous-menu du jeu.** Aucun hook, mutation de scène, nouvelle fenêtre ni injection clavier n'a été ajouté.

## Données de présentation : sources distinctes

- **Carte graphique** : la valeur du périphérique tel que rapporté par Windows/Wine, actuellement inventoriée via `D3D9::GetAdapterIdentifier` dans `src/launcher/system_inventory.cpp`. L'identité matérielle réellement utilisée par GameFrame sur l'hôte Linux peut différer. Ne jamais présenter l'adaptateur virtuel Wine comme une preuve du GPU hôte.
- **Pilote** : afficher d'abord **GameFrame / Glide2** en tant que moteur du jeu ; afficher séparément le pilote `D3D9` signalé par Windows/Wine, avec indication de la provenance. Le véritable pilote Linux (Mesa/RADV/NVIDIA, version) reste `Indisponible` tant qu'une interrogation native documentée ne l'établit pas.
- **Résolution de sortie** : utiliser `ChildDisplayOptions::output_width/height` passé par le lanceur (dimension de `GLIDE_SIZE`). Les dimensions physiques du bureau ne sont pas forcément celles de la sortie en mode `4:3`.
- **Profondeur** : **16 bits internes** ; `ubi.ini` contient `GLI_Mode=1 - 1024 x 768 x 16`. Ne pas afficher `32 bits` comme profondeur du rendu interne simplement parce que le bureau ou la fenêtre utilisent une surface 32 bits.
- **Ratio** : rapport exact de la sortie (`16:10` conservé pour les écrans compatibles, autres ratios réduits) ; `3440x1440` est précisément `43:18`, pas `21:9`. Afficher aussi le mode `Auto` ou `4:3`.

Le module pur `include/rayman2fix/graphics_menu_info.hpp` prépare uniquement les valeurs, avec tests C++17 sous Linux/Windows. Les libellés visibles doivent à terme être localisés conformément à la langue du jeu.

## Phase 1 : étude de l'interface native CD vérifiée

Le fichier `src/runtime/runtime.cpp` identifie déjà le drapeau **générique** `in_game_menu` pour les pauses ESC. **Il ne prouve pas l'entrée dans Options → Résolution**. L'utiliser pour y dessiner du texte afficherait des informations sur d'autres menus, et pourrait provoquer des régressions audio.

Avant toute modification de la navigation du menu, tracer sur le binaire CD déjà hash-verrouillé :
1. La signature en lecture seule de l'ouverture/fermeture du sous-menu Résolution.
2. La fonction native exacte appelée par l'interface pour changer le mode vidéo, et les effets potentiels sur les sauvegardes ou sur `ubi.ini`.
3. Le point de rendu de texte natif capable d'afficher des lignes sans fenêtre séparée ni changement de focus.

## Critères finaux

Une fois ces points validés, brancher les champs en lecture seule sur l'écran Résolution : seules les touches de retour restent utiles ; aucune option ne doit changer `GLI_Mode`, `GLIDE_SIZE`, la profondeur ou le ratio actif ; conserver luminosité, son, commandes, langue, pauses ESC et cinématiques ; vérifier sous Windows et Wine en `Auto` et `4:3`.
