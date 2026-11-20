# Rayman2Fix — démarrage audio automatique côté Unix (essai)

## Pourquoi cette seconde expérience ?

Le mainteneur a constaté des craquements audio partout dans le jeu
sous Wine 11.19. Lors du test A/B du 20 novembre 2026 :

- **baseline** : flux `rayman2.exe` `QUANT=118` à 22 050 Hz (~5,35 ms),
  craquements présents ;
- **`PULSE_LATENCY_MSEC=60 wine Rayman2Fix.exe`** : `QUANT=331`
  (~15,01 ms), craquements non reproduits ;
- **PR #42** : `SetEnvironmentVariableW` juste avant
  `CreateProcessW(game/rayman2.exe)` ; `QUANT=118` sur les 89 snapshots
  actifs, craquements toujours présents. **PR #42 fermée sans fusion.**

Le problème n'est pas résolu par un simple paramètre Windows transmis
à l'enfant ; les sources Wine 11.19 montrent que son processus Unix
`fork/exec` hérite d'un environnement hôte distinct.

## Nouvelle expérience : une seule EXE, le même runner Wine

Ce correctif est **expérimental** et ne doit pas être fusionné avant
validation sur Wine. La version installée se compose toujours d'un
**unique `Rayman2Fix.exe`**, accompagné des fichiers de jeu habituels.

Sur **un lancement automatique sans argument**, et uniquement si
`ntdll!wine_get_version` est présent et qu'aucun
`PULSE_LATENCY_MSEC` n'est défini :

1. Le premier `Rayman2Fix.exe` récupère le **chargeur exact de Wine**
   depuis la variable native `WINELOADER`, fournie par Wine 11.19.
2. Les chemins du chargeur et de ce même `Rayman2Fix.exe` sont
   convertis en chemins Unix via l'export Wine
   `kernel32!wine_get_unix_file_name`.
3. L'export Wine `ntdll!__wine_unix_spawnvp` lance
   `/usr/bin/env PULSE_LATENCY_MSEC=60 <WINELOADER> <Rayman2Fix.exe>
   --wine-audio-host-ready`. Ce nouveau processus Wine hérite du
   préfixe/environnement **Unix** original, à l'exception de la
   latence spécifique au nouvel enfant.
4. Le paramètre caché `--wine-audio-host-ready` évite toute boucle
   de relancement, même en cas de changement du comportement de Wine.
5. La seconde instance effectue **le flux habituel** : installation
   éventuelle, vérification, lancement, instrumentation graphique et
   journaux. Le premier processus n'ouvre aucun log, ne crée aucun
   fichier et ne modifie pas l'INI avant la relance ; il attend le
   résultat de la seconde instance.

L'override ne touche **ni au système, ni au préfixe Wine, ni à la
configuration de PipeWire, ni à Windows natif** ; il ne force pas une
valeur préexistante et n'affecte pas `--verify`, `--install-only`,
`--launch-no-verify`, `--help` ou `--version`.

Si un export interne de Wine est absent, ou si la conversion/le spawn
échoue, le lanceur retombe sur le démarrage Windows traditionnel, avec
un message d'avertissement sur le terminal. L'absence de cette
amélioration ne doit jamais empêcher de jouer.

**Attention** : `wine_get_unix_file_name` et
`__wine_unix_spawnvp` sont **des interfaces Wine internes,
non garanties sur tous les runners**, notamment Proton et les versions
futures. Il faut valider le résultat effectif sur la machine du
mainteneur, et ne pas présenter une CI Windows positive comme une
preuve de succès sous Wine.

## Test de validation

Remplacer uniquement le `Rayman2Fix.exe` du dossier installé par la
compilation de cette branche, sans modifier `Rayman2Fix.ini`.

1. Lancer **`env -u PULSE_LATENCY_MSEC wine Rayman2Fix.exe`**.
   Le terminal doit afficher `AUDIO-WINE-HOST: relaunch through original
   Wine loader with PULSE_LATENCY_MSEC=60`.
2. Observer `pw-top` : si la relance a atteint le backend audio,
   `QUANT` pour `rayman2.exe` devrait approcher **331 frames**,
   contre 118 lors des précédentes séances sans override.
3. Écouter en jeu normal, pendant les cinématiques et dans le menu
   pause, puis vérifier Alt+Tab, sortie et relance.
4. Vérifier les deux journaux initiaux avec `Log=0`. Au premier
   lancement, contrôler que le log installateur n'est pas doublé.
5. Tester `PULSE_LATENCY_MSEC=100 wine Rayman2Fix.exe` pour
   confirmer qu'un réglage utilisateur existant n'est pas écrasé.
6. Sur Windows natif, vérifier l'absence de relancement et toute
   régression.

Si le quantum reste `118`, **ne pas fusionner**, même si le terminal
annonce une demande de 60 ms. Si l'une des API Wine échoue, relever
le message `AUDIO-WINE-HOST: fallback`.

## Sources techniques

- `wine-11.19/dlls/ntdll/unix/process.c`, fonction
  `spawn_process` (fork, exec du chargeur exact), et export
  `__wine_unix_spawnvp`.
- `wine-11.19/dlls/ntdll/unix/env.c`, `WINELOADER` et
  traitement de l'environnement initial.
- `wine-11.19/dlls/kernel32/path.c`, export Wine
  `wine_get_unix_file_name`. L'allocation de son résultat appartient
  au tas du processus : le code utilise `HeapFree(GetProcessHeap())`.
- Issue Rayman2Fix #39, PR #41 (protocole A/B), PR #42 (échec).
