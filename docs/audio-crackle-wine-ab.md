# Rayman2Fix — test audio comparatif sous Wine (recherche)

Le 20 novembre 2026, le mainteneur signale des **craquements audio partout** :
en jeu normal, dans les cinématiques et dans le menu Échap. Les journaux
actuels confirment les pilotes `WAVMWbvr.dll` et `APMMXbvr.dll`, mais **ne
mesurent pas** les sous-alimentations de la sortie PCM. Ce test cherche
uniquement à distinguer une contrainte de timing Wine/PipeWire d'un défaut
du décodage ou du mixage du jeu.

## Essai sur Manjaro avec Wine-staging

Dans le répertoire de l'installation où se trouve `Rayman2Fix.exe` :

```bash
curl -fL -o audio_wine_ab.py \
  https://raw.githubusercontent.com/legluondunet/Rayman2Fix/research/audio-crackle-wine-ab/tools/audio_wine_ab.py

# Séance A : paramètres audio habituels, sans nouvel override
python3 audio_wine_ab.py baseline

# Après avoir quitté normalement le jeu :
# Séance B : demander une latence PulseAudio de 60 ms
python3 audio_wine_ab.py latency60
```

Conserver **le même runner Wine**, le même préfixe, le même périphérique audio
et idéalement le même passage du jeu. Pour chaque séance, noter si les
craquements sont audibles en **jeu**, en **cinématique** et dans le **menu
Échap**. Si le deuxième test n'améliore rien, on peut éventuellement essayer
`python3 audio_wine_ab.py latency100` pour compléter l'observation.

Les deux modes n'éditent **ni l'INI, ni le jeu, ni ses sauvegardes, ni ses
logs initiaux**. `PULSE_LATENCY_MSEC=60` est défini **seulement pour le
processus Wine du deuxième essai**. C'est une demande de latence, pas une
preuve que Wine/PipeWire applique la valeur. Aucun changement persistant
du système, du préfixe Wine ou du moteur sonore n'est effectué.

Le script peut observer PipeWire avec `pw-top --batch-mode --iterations 240`.
Il ne capture aucun son, ne nécessite aucun accès administrateur et ne
publie aucune donnée sur Internet. Il enregistre localement :

- `audio-diagnostics/<horodatage>-baseline.json` : mode demandé, durée,
  code de sortie et éventuels overrides de latence.
- `audio-diagnostics/<horodatage>-baseline.pw-top.txt` : relevés PipeWire
  s'il est disponible, dont `ERR`.

**Attention :** `pw-top` peut contenir des noms de périphériques et d'autres
applications audio ; examiner le contenu avant de le partager.

Si l'observateur est absent, le test reste possible. Pour le désactiver :

```bash
python3 audio_wine_ab.py baseline --no-pw-top
python3 audio_wine_ab.py latency60 --no-pw-top
```

Un compteur `ERR` en hausse au même moment que les craquements suggère un
retard du graphe audio, pas nécessairement un défaut dans Rayman2Fix.
Un `ERR` stable n'exclut pas les craquements créés *avant* PipeWire.
L'amélioration avec `latency60` suggérerait un problème de timing, sans
justifier à elle seule un changement automatique de configuration.
Si les craquements persistent, poursuivre l'audit du mixer original
(`WAVMWbvr.dll` / `APMMXbvr.dll`), de la cadence des `waveOutWrite` et
du comportement sous Windows natif.

## Sources et contraintes

- Recherches du dépôt :
  `docs/wavmwbvr-1999-static-analysis.md` et
  `docs/adpcm-handle-lifetime-aba.md`.
- PipeWire pw-top : https://docs.pipewire.org/page_man_pw-top_1.html
- Discussions Wine sur les marges audio :
  https://list.winehq.org/hyperkitty/list/wine-gitlab%40list.winehq.org/thread/PYMDSVLGU2A4DBQ7ANHBXZJ5HG4EN2BY/

Aucune DLL propriétaire n'est distribuée ou modifiée ; ne pas présenter
ce test diagnostique comme une correction des craquements.
