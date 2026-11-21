# Rayman2Fix — diagnostic natif Options → Résolution (phase 1)

Cette branche **ne remplace pas encore les options graphiques**. Le but est de
localiser, sur l'exécutable CD SHA-256 approuvé
`7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3`,
les **états du menu natif et du gestionnaire de modes vidéo**, afin d'éviter
un patch qui bloquerait par erreur l'initialisation ou les autres menus.

## Résultats statiques vérifiés

À partir du désassemblage de l'exécutable CD de 676352 octets :

- La configuration de démarrage lit `GLI_Mode` via une référence à
  `0x49F480` depuis `0x405B5A`. Le lanceur conserve la source de vérité.
- `GLI_DRV_hChangeMode` est résolu depuis la chaîne à `0x49FF00`,
  son pointeur de fonction étant placé à `0x504820` par la séquence
  `0x421D0C..0x421D2C`.
- **L'appel réel au pointeur** se trouve à `0x4212E4`,
  à l'intérieur de la fonction `0x4211C0`, dans le chemin de transition
  du mode vidéo. Ces adresses sont des VA pour l'ImageBase d'origine 0x400000.
- `0x422160` ajoute les modes enumerés dans la table de records à
  `0x504320`, avec les champs `[mode-id, largeur, hauteur, bits]` de
  16 octets. Le nombre de records figure à `0x4A7238`, l'index sélectionné
  à `0x504318`.
- Les données `MENU0.DAT`, `MENU1.DAT`, `MENU2.DAT` et
  `GRAPHICS0.DAT` apparaissent comme noms d'archives dans la table de
  ressources du jeu. **Ce ne sont pas une preuve du chemin de navigation de
  l'écran Résolution**.

Le processus de rendu possède déjà un drapeau `in_game_menu` à
`0x4B9BC8`, utilisé par les diagnostics audio. Ce drapeau est **générique** :
ne pas le confondre avec `Options → Résolution`.

## Prototype de journalisation sans risque de changement vidéo

La compilation supplémentaire `Rayman2Fix-Windows-x86-resolution-menu-trace`
active uniquement `RAYMAN2FIX_GRAPHICS_MENU_TRACE=ON`.
Elle contient le même moteur standard et ses protections audio/intro, avec
un observateur indépendant et temporaire (maximum 5 minutes) qui :

1. Lit les valeurs de statut du moteur, du menu générique, du compteur et
   de l'index de modes, et un mode de la table lorsque l'index est valide ;
2. Relève **uniquement les fronts des six touches de navigation**
   Haut/Bas/Gauche/Droite/Entrée/Échap **quand le jeu est au premier plan** ;
3. Journalise les transitions et un battement toutes les 4 secondes, dans
   `Rayman2Fix.log`, **maximum 180 lignes**, uniquement avec `[Debug] Log=1`.

**Aucune injection de touches, pas de commande GLI, pas d'écriture mémoire du
jeu, pas de hook vidéo ou de fenêtre externe.** Les valeurs ne sont pas des
informations d'identification de la personne. La build n'est pas une release.

## Protocole manuel à confirmer avant toute modification du sous-menu

1. Utiliser la compilation de diagnostic et `[Debug] Log=1`, puis lancer le jeu.
2. Dans le menu principal, attendre 4 secondes ; ouvrir **Options**.
3. Entrer dans **Résolution** ; **ne pas sélectionner d'autre mode ni
   Sauver résolution**, et attendre environ 5 secondes.
4. Revenir avec Échap, entrer puis ressortir une seconde fois de
   Résolution. Fermer le jeu.
5. Remettre la compilation normale `main` après le test si souhaité.
   Transmettre le journal `Rayman2Fix.log`, ainsi que les horaires approximatifs
   ou l'ordre des écrans si la navigation diffère.

Interprétation : cette instrumentation peut corréler les états graphiques et
les gestes du joueur, **elle ne prouve pas à elle seule l'identité du sous-menu**.
La phase 2 nécessitera d'établir une signature native de ce menu, de vérifier
le point d'entrée du changement de mode, puis de remplacer les commandes par
les informations approuvées en PR #50. La production `main` est inchangée.
