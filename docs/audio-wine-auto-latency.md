# Rayman2Fix — latence PulseAudio automatique sous Wine (expérimental)

## Observation, non preuve générale

Le 20 novembre 2026, deux sessions sous Manjaro/Wine 11.19 avec le
même lanceur et les mêmes fichiers du jeu ont montré :

| | Sans override | `PULSE_LATENCY_MSEC=60` |
| --- | --- | --- |
| Stream `rayman2.exe` | `S16LE 2 22050` | `S16LE 2 22050` |
| PipeWire QUANT | 118 frames | 331 frames |
| Période de bloc | 5,35 ms | 15,01 ms |
| `pw-top ERR` sur le jeu | 0 | 0 |

Le mainteneur a rapporté **ne pas réussir à reproduire les craquements**
pendant la séance `latency60`. Le problème était auparavant audible
en jeu, dans les cinématiques et dans le menu Échap. C'est un indice
fort d'un défaut de timing ou de marge audio sous Wine, pas une preuve
d'un bug dans PipeWire ou de la latence réelle de 60 ms.

## Comportement de la branche expérimentale

La compilation standard de cette branche (non fusionnée) demande
automatiquement `PULSE_LATENCY_MSEC=60` **pour le processus
`game/rayman2.exe` seulement**, sous les conditions suivantes :

- L'installateur a **vérifié le jeu** et s'apprête à le lancer.
- Le lanceur Windows détecte réellement **Wine** grâce à l'export
  `ntdll!wine_get_version`. Sous **Windows natif**, aucun override n'est
  appliqué.
- Aucune variable `PULSE_LATENCY_MSEC` n'est déjà définie. Les réglages
  explicites de l'utilisateur gagnent, même vides ou égaux à 0.

Le lanceur restaure l'environnement parent immédiatement après
`CreateProcessW`. La variable est héritée par le jeu, mais ne modifie
ni les préférences de l'environnement graphique, ni le préfixe Wine, ni
le daemon PipeWire/PulseAudio, ni l'INI, ni les fichiers ou logs du jeu.

Un message dans le terminal confirme l'application :
`AUDIO-WINE-LATENCY: requested=60ms scope=game-only`.
Le message n'apparaît pas si le réglage est préexistant ou si le jeu
tourne sur Windows natif. Le nom `60ms` est la **valeur demandée**,
pas une durée de tampon mesurée ou garantie.

La commande `--launch-no-verify` n'applique pas cet essai.
Aucune API SDL3 n'est modifiée et aucun hook audio natif n'est activé.

## Validation utilisateur

- Télécharger **l'archive standard Windows x86 de la PR** et remplacer
  seulement `Rayman2Fix.exe` dans l'installation actuelle.
- Lancer **`wine Rayman2Fix.exe` sans script Python** : le terminal doit
  montrer la ligne `AUDIO-WINE-LATENCY`. `pw-top` doit présenter, à
  configuration équivalente, un quantum similaire à 331 frames pour le jeu.
- Écouter le même passage en jeu normal, durant les cinématiques, et dans
  le menu Échap avec `MusicInPauseMenu=1`.
- Vérifier sortie, relance, Alt+Tab, conservation des logs initiaux
  sous `Log=0`.
- Si possible, vérifier sur Windows natif : pas de message d'override,
  aucune régression audio ou graphique.
- Vérifier que l'application d'un réglage explicitement fourni par
  l'utilisateur (`PULSE_LATENCY_MSEC=... wine Rayman2Fix.exe`) n'est
  jamais écrasée.

**Critère de fusion** : les craquements doivent rester absents avec un
lancement normal de Rayman2Fix sous Wine ; les tests natifs et CI doivent
passer ; aucun changement permanent du système.
