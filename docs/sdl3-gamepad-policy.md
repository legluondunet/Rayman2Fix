# Rayman2Fix — SDL3 manettes, étape 0 : politique de focus et hotplug

Statut : **premières routes SDL3 natives, à valider sur matériel**. Le lanceur embarque SDL3 ; le runtime acquiert les entrées et limite leur transmission aux menus authentifiés et à Jail_20. Le gameplay analogique, Learn_10, les autres menus Pause et la neutralisation des anciennes manettes DirectInput restent à implémenter.

## Pourquoi commencer ici ?

Le lanceur actuel recense certains périphériques XInput / DirectInput, mais il n'acquiert aucune entrée. Il n'existe pas encore de contrat natif vérifié pour transmettre les boutons au moteur Rayman 2. Synthétiser des touches globales avec `SendInput` serait prématuré : perte de focus, répétitions et doubles entrées clavier sont difficiles à maîtriser.

`include/rayman2fix/gamepad_input_policy.hpp` définit une **politique C++17 sans dépendance SDL3** :

- entrée normalisée par l'adaptateur SDL3 futur (identifiant de périphérique, connected, foreground, axes, d-pad, boutons) ;
- priorité absolue au focus du vrai jeu : aucune entrée si hors focus ;
- libération des actions à la déconnexion, au changement de propriétaire ou à la perte de focus, sans répétition de libérations ;
- transitions `held / pressed / released` déterministes, sans répétition synthétique ;
- seuil analogique signé avec zone morte et neutralisation des directions contradictoires ;
- isolation complète du clavier natif (aucune injection pour ce modèle).

`tests/gamepad_input_policy_tests.cpp` couvre la perte de focus, l'absence de manette, le changement de périphérique, la reconnexion, les boutons et les axes extrêmes. CI Windows x86 et Linux, sans SDL3 liée.

## Étapes suivantes (issue #40)

1. Déterminer **l'API d'entrée réellement consommée par l'exécutable CD vérifié**, sans écriture mémoire ni hook spéculatif.
2. Choisir une révision SDL3 Windows x86 reproductible, auditer les notices et empreintes SHA-256, l'intégrer à l'EXE unique sous `game/`, à côté de `fix.dll` et `glide2x.dll`.
3. Ajouter l'initialisation SDL3 et les événements Gamepad, puis alimenter le modèle depuis une source de snapshots bornée ; éviter les événements globaux, les fenêtres SDL parasites et les blocages de rendu.
4. Construire un adaptateur indépendant du clavier qui applique les transitions uniquement au point d'entrée moteur confirmé, avec arrêt propre et repli clavier.
5. Tester sous Windows et Wine les manettes Xbox 360 et DualShock 4, l'entrée dans les menus, le gameplay, la caméra, le branchement à chaud, Alt-Tab et les régressions audio.

La disposition Dreamcast adaptée aux manettes SDL3 est approuvée par le mainteneur (24 novembre 2026). Les identifiants de commandes natifs restent à vérifier. Voir le contrat ci-dessous.

## Reprise après l'intégration VIDEO (#53)

Base : main, commit 9a2f7ab9. La PR #48 est reprise sans modifier
ses tests ni activer de nouvelles entrées dans le runtime.

Le hook existant `src/runtime/video_navigation_input.cpp` authentifie
la sortie de GetDeviceState(256) du clavier au RVA 0x966B3. C'est une
route candidate pour les commandes numériques, pas une preuve de
compatibilité avec toutes les actions natives. Ne pas installer un
second detour au même emplacement. La future intégration devra partager
le hook, préserver son HRESULT et appliquer le filtre VIDEO après la
fusion clavier/manette. Le mode analogique, les actions de caméra et
l'évitement de la double entrée DirectInput restent à caractériser.

Les touches ESC, Enter et les raccourcis cinématiques, ainsi que le
menu Contrôles, devront faire partie des tests. Pas de SendInput global.
La dépendance SDL3 x86 et sa licence zlib seront vérifiées et embarquées
avant l'activation réelle. Voir #40 pour la version candidate et son hash.

## SDL3 dependency verification (24 November 2026)

The official SDL release listing publishes SDL3 3.4.20 for Windows x86:
https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20

Archive SHA-256: `f6fbf5afa42195a01b1da8adf25567377d0e212f4b2b6770923903eb3feea254`.
The Windows CI downloads this exact archive and audits its hash, PE32 i386
DLL identity, required Gamepad API exports and zlib notice before writing
only fixed output filenames. The audit JSON records the extracted DLL and
notice digests. A separate x86 test loads the audited DLL by absolute path,
checks the exact SDL version, initializes only GAMEPAD on its main thread,
disables event processing, updates and enumerates devices, then shuts down.
No controller is required on the CI runner. This smoke test is not proof
of game input or physical hotplug. No SDL binary is embedded or executed
inside Rayman 2 by this phase. The final DLL hash must be pinned from the successful audit before packaging. CI run 45411813631 passed the archive audit and the x86 gamepad-only smoke test.

Native integration must use SDL initialization on the game's main thread,
not the existing background menu discovery workers. Preserve the original
GetDeviceState result and the VIDEO navigation filter. The analog route
and duplicate native controller handling remain open; no native engine input binding is approved yet.

## Mapping approuvé — Dreamcast + manettes modernes

SDL3 utilise les positions physiques normalisées, jamais un index
DirectInput propre à un modèle de manette.

| Action en jeu | SDL3 | Xbox | DualShock |
|---|---|---|---|
| Déplacement proportionnel | Left X/Y | Stick gauche | Stick gauche |
| Saut / hélicoptère | South | A | Croix |
| Tir / tir chargé | East | B | Rond |
| Caméra gauche | West, Left Shoulder, Right X négatif | X, LB, stick droit | Carré, L1, stick droit |
| Caméra droite | North, Right Shoulder, Right X positif | Y, RB, stick droit | Triangle, R1, stick droit |
| Ciblage / recentrage / plongée selon contexte natif | Left Trigger | LT | L2 |
| Statistiques | Right Trigger | RT | R2 |
| Connaissance du monde | D-pad Up | Haut | Haut |
| Murphy | D-pad Down | Bas | Bas |
| Pause | Start | Start | Options |

Dans les menus : stick gauche ou croix directionnelle, South valide,
East annule. Dans le panneau VIDEO en lecture seule, seul East (retour)
est transmis par ce modèle. Pendant les cinématiques sautables, South
demande le saut ; le moteur et ses gardes restent responsables du
comportement réel.

Le déplacement conserve un vecteur analogique normalisé, avec zone
morte radiale de 25 %, sans accélération diagonale. Le stick droit
commande la rotation horizontale ; aucune rotation verticale nouvelle
n'est inventée. Les seuils initiaux seront ajustés si les tests matériels
le justifient. Les transitions de contexte libèrent les actions et
attendent la neutralisation des boutons avant de réarmer les commandes,
pour éviter un saut déclenché par la validation précédente.

Ces correspondances sont codées et testées au niveau sémantique, mais
pas encore reliées aux commandes natives. L'identité des actions
Connaissance du monde / Murphy et le comportement exact en nage restent
à vérifier sur le moteur PC ; ne pas les simuler avec des touches
arbitraires.

## Menu Contrôleur — contrat approuvé

Le mapping Dreamcast enrichi est unique, sans remapping, profil ni option
INI pour cette première release. Le menu natif Contrôleur devient une
page informative en lecture seule, accessible au clavier même si aucune
manette n'est connectée.

- Conserver le titre, les transitions et les éléments décoratifs natifs
  lorsque leur identité est vérifiée. Réutiliser la présentation du panneau
  VIDEO (bleu nuit, opacité 40 %, contour bleu, angles arrondis, texte lissé,
  proportions 4:3) plutôt que créer une fenêtre externe.
- Une silhouette originale de manette au centre, les symboles intégrés aux boutons
  et des flèches fines vers les actions autour de la manette, dans le cadre 4:3. La silhouette ne doit pas reprendre des images commerciales.
  Les onze lignes du modèle `gamepad_control_info.hpp` sont la référence
  des libellés; les actions et libellés génériques seront localisés dans
  la langue du jeu. Vérifier leur lisibilité avant de figer la géométrie.
- En-tête : famille de la manette active, identifiée via SDL3 (Xbox,
  PlayStation, générique). Si aucune manette : afficher explicitement
  « Aucune manette connectée » et les positions physiques génériques,
  sans masquer la page. Ne pas inférer les symboles physiques d'une
  manette inconnue à partir d'un nom de périphérique.
- Actualiser les libellés sur connexion, déconnexion ou changement de
  manette active. Ne jamais modifier le mapping sous-jacent.
- Aucun curseur de sélection ni édition; retour via Échap ou bouton
  East (B / Rond / bouton droit). Les autres commandes sont neutralisées
  uniquement pendant la page authentifiée.
- Préserver le clavier natif, les autres menus, la musique ESC et le
  focus. Appliquer le remplacement aux menus Contrôleur effectivement
  présents, après identification de leurs contrôleurs natifs; ne pas
  supposer que les structures MENU et Pause sont identiques.

La politique sémantique possède un contexte `controller_info` et un
modèle de présentation séparé. Les tests vérifient les libellés par famille,
le cas sans manette et l'isolation de la navigation. Le dessin, la détection
SDL3 réelle de la famille, la localisation et la substitution du menu
natif restent à implémenter. L'écran ne doit pas annoncer des commandes
non opérationnelles dans une compilation distribuée.

## Packaging SDL3 (phase 1)

Windows CI 45411813631 verified the official SDL 3.4.20 x86 dependency:
- SDL3.dll SHA-256: `6d7e653aa9c762ceda4fffc054a504dcce273e016eb9f1fe079b6409a058a9f7`
- Original zlib notice SHA-256: `1c040b8271b37e5076359f8fd54240e371114112924d2df81ef87c7d6a1dfdfd`

Standard x86 CI supplies the audited directory to CMake. Both extracted hashes are pinned at configure time. RCDATA 1107 contains SDL3.dll and 1108 contains its original license. Normal provisioning compares installed bytes with the embedded payload, stages exclusively and replaces atomically if necessary. Deployment targets `game/SDL3.dll` and `docs/SDL3-LICENSE.txt`, per maintainer preference. The fresh-install workspace permits the DLL, and atomic CD publication copies and verifies its pinned SHA-256 separately from the CD receipt. `mods/` remains reserved for mod assets, not runtime DLLs. The notice is provisioned first, without altering the GPLv3 or GameFrame notices. No download or configuration by the user, no permanent backup, no global DLL installation. Unbundled developer builds remain possible. The read-only runtime now uses the absolute game-local path and verifies the pinned SHA-256 before loading SDL3. The original keyboard is preserved except for the existing VIDEO filter. No global DLL search or global input injection is allowed.

Tests exercise initial deployment, repeat provisioning, replacement of corrupt DLL and notice, and preservation across atomic CD publication. This is packaging only, not proof of physical controller functionality.

## In-game acquisition (phase 2, read-only)

`SdlGamepads` binds the verified x86 ABI, initializes only GAMEPAD, disables
gamepad event delivery, manually updates and polls the active controller,
and selects the first successfully opened device by instance ID. The active
device is retained until disconnected. Scans are spaced 500 ms apart, and
disconnect publishes a neutral sample before reassignment. SDL family IDs
2/3 indicate Xbox, 4/5/6 PlayStation; others use the generic labels.
No gamepad buttons or axes are read for foreground=false.

The standalone smoke uses the actual acquisition implementation, checks
refusal of nonabsolute/missing DLL paths, focus-neutral sampling, explicit
shutdown and reinitialization. In the game, initialization is deferred to
the existing authenticated camera/window thread, after existing audio and
intro decisions. The game instance has process lifetime; do not unload
SDL under DllMain. Explicit same-thread shutdown is tested by the smoke.
The logs `RUNTIME-SDL3:` and `RUNTIME-SDL3-SAMPLE:` report initialization,
connection/disconnection, focus and bounded abstract mapping samples.
The latter uses the gameplay policy for diagnostics only, not proof of
the current native context or any game control injection.

The transition gate independently requires physical neutral input on
initial attach, focus return, device change and context change. It also
suppresses analog movement while blocked, including radial diagonals and
buttons ignored on information screens. CI covers these cases. Native
action bindings, analog engine input, safe removal of duplicate native
controller input and menu replacement still need source verification.
A diagnostic build must be clearly identified as read-only until those
are implemented.

## First native integration: MENU, VIDEO, Jail_20

The previous acquisition-only CI 45431086104 passed on Windows x86 and
Linux. The next phase uses the existing authenticated DirectInput
GetDeviceState(256) return hook, never a new detour at the same address.
A live MENU level and verified controller model enable arrows (left stick
or D-pad), Return (South) and Escape (East). The approved drawn VIDEO
page accepts only East/Escape. Only successful writable native reads in
foreground receive a bitwise OR of these scan codes; physical keyboard
bits remain intact and the existing VIDEO suppression runs afterwards.

The same original camera thread supplies a snapshot with a maximum age
of 100 ms, rechecking live context and focus at consumption. The native
frame flip can refresh SDL3 independently of the cinematic/audio
scheduler, without invoking additional native audio or level changes.
The physical neutral gate protects focus, owner and context boundaries.
Jail_20 South is passed into the existing IntroSkipGuard, which still
queues early requests until E_EM_ModePlaying=9 and dispatches at most
once per scene. No new level-change route or global synthetic key event.
Diagnostic markers native-route=MENU, VIDEO-back and jail_20 identify
delivery separately from abstract input sampling.

This is a scoped test candidate, not complete gamepad support. Native
DirectInput joystick duplicate handling is still unresolved; gameplay
and unverified menus are deliberately not mapped. Do not claim support
for movement, camera, shooting or Learn_10 from this build.

## Native input discovery after Wine validation

Build 0660075e confirmed SDL 3.4.20 initialization under Wine, PlayStation
type=5, MENU DirectInput merge, and two VIDEO returns. The original camera
thread and foreground HWND owner differ; retain the same-process focus and
original-camera-thread guards. Jail_20 and Learn_10 transitions worked,
but SDL-origin skip was not proved by that log.

RayTools' MIT-licensed ACP_Ray2 documentation is a **reference only**, not
evidence of CD offsets: https://github.com/raytools/ACP_Ray2/tree/master/src/IPT
(IPT.c, IPT.h, IPT_Const.h; copyright (c) 2021 RayTools).
It identifies an input table candidate at RVA 0x109E60 with count/table
descriptor at +0x700, 24-byte entry descriptors, names, native counters
and analog values. Candidate first indices 0/1 (axes), 10/11 (jump/fire).
The new Log=1 read-only observation uses the already verified game thread,
bounds and double-reads the table header, checks layout and all names,
and checks those four independent name anchors before logging changing
native state signs and raw analog bits. It reserves per-entry budgets for
MENU, loading/cinematics, and post-Learn_10 playable-scene candidates
(maximum 336 state entries and four candidate layouts), so earlier menu
navigation cannot exhaust the later analog observation budget. It
does not report pointer values, and never writes to the table or calls a
candidate engine function. Keyboard controls and SDL menu routes remain
unchanged. A candidate match must be confirmed against the approved CD
binary and action behavior before any gameplay write, hook, or override.

The controller illustration contract now uses a central controller and
arrows toward labels, following the maintainer's approved 4:3 reference.

## Retail executable verification (owner-supplied PE32)

SHA-256 9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713.
Timestamp 0x3807B68B, image size 0x1D7000. Disassembly 42E090, 42E0C0,
42E798-42E842 confirms descriptor at 50A560/50A564, stride 24, name +8,
state +0xC, analog float +0x10 (42E8D8) and BYTE active +0x14 (42E7A0).
Unused names are legal; the previous probe incorrectly read padding as BOOL.
The exact E8 caller at 42E7AD evaluates each active entry before original
counter advancement. Merge SDL actions only after native predicate returns
false and the live table index/name and Learn_10 handover are checked.
The native analog consumer calls 42E8D0 at 46E755/46E774, using script-owned
axis pointers 4B9B54/58, signed byte calibration 4A40A4/A8/AC/B0 and
centers 4B9B5C/60. Two source-verified getter calls merge calibrated SDL
values when a stick is active; the existing native keyboard path and
native world-vector calculation remain untouched. No action-table writes.
All three signatures are preflighted together. First hardware candidate
is intentionally limited to Learn_10 after story1091 handover, not all
levels or cinematics. Generic Pause uses the live model and flag; VIDEO
retains its original scope. World knowledge and exact swimming behavior
remain unverified. Duplicate native joystick ownership is not resolved.

## Wine test ccab2261

The 20261124-213128 log confirms the three callsite signatures installed,
138 active-table entries and stripped names (first 28), plus transition
to gameplay context 1 after Learn_10 handover and verified Pause context
6. No IPT delivery marker was emitted because the name guard refused the
retail rows. The revised guard checks exact table count and all 28 observed
keyword counts and aligned non-null keyword pointers. Analog identity
uses the exact native consumer's current script-owned axis pointer rather
than requiring reference index 0/1. All other focus and handover constraints
remain. The reference action indices are still candidates until tested;
no claim of validated gameplay. No native action table writes.

## Controller feedback 9c5ee4ce

Wine log 20261124-220157 verifies actual IPT delivery: axis 0/1 calibrated
-50/0/50 and action indices 10,11,13,16,17,22,24. Physical gameplay
semantics still need confirmation. D-pad down = Murphy (F1); D-pad up
world knowledge remains unimplemented. South = next dialogue candidate
native index 21 in gameplay, in addition to native jump; no system-wide
Space injection. First jail_10 and Learn_10 opening now have separate
cinematic contexts, using native cut/next-text indices 20/21 only when
Playing and skip enabled. Learn_10 also requires original F248 enabled and
story handover false. Jail_20 retains the existing guarded skip request;
held button cannot cross cinematic boundaries. Native acceptance untested.
Pause music was enabled but no paired opening was adopted: the script
pause guard required physical Escape. A one-shot same-thread token from
actual successful SDL keyboard-buffer ESC merge now supplies the second
input provenance option, with current gameplay/focus/100ms validation.
All native menu, pause depth and audio-owner checks remain unchanged.

## Wine af5ce27c / 20261124-221815

Jail_10 native indices 20/21 delivered, Jail_20 South accepted by original
skip guard (exit 389 ms), Learn_10 handover 1592 ms with no keyboard Enter
edge and SDL skip sampled. First Pause adopted and paired native resumes
requested; voice observed active and unpaused. Four later openings adopted
but provisional owner rejected (phase idle), native resumes forwarded.
No unsafe audio bypass: idle-phase render tick now publishes the previously
lost bounded predriver rejection evidence, after the original callbacks.
The exact Mapmonde scene was observed inactive with controller input. Add
separate world-map context only in Playing with generic pause flag clear;
use existing DirectInput menu navigation/confirm/cancel, not gameplay axes.
The player can still use physical keys. World-map behavior needs hardware
validation. General level gameplay and knowledge action remain pending.

## Wine 184fbd50 / 20261124-223631

Mapmonde is a 3D Lums traversal scene, not just a menu. Owner reports
South cannot jump on the fairy-glade portal. Context 9 samples show
South mapped only to confirm, with no native jump. Preserve the working
arrow/confirm/back route and additionally merge native IPT jump index 10
only for live world-map context, foreground, fresh sample and authenticated
retail table. This is a candidate native jump (hardware test required),
not a general gameplay/analog expansion; no verified map substate split.
Original Frame producer logged 14..94 / 61.60..73.40 in the map, matching
the visible tutorial rectangle after 4:3 centering. Add that exact geometry
with 0.15 percent-point tolerance to RemoveDialogueBackgrounds; do not
broaden the existing lower-screen rule or suppress other producers.
The screenshot-to-source correspondence is a hypothesis until retested.


## Wine e3347b62 / 20261125-035419: general gameplay routing

The Mapmonde jump reached IPT index 10 and the dialogue backdrop filter
reported suppression. The owner entered Learn_30 but could not move:
SDL3 continued to report physical axes and buttons while native context
was inactive. The original route only enabled Learn_10 after handover.

Use a 55-scene exact, case-insensitive PC playable-scene catalog,
including bonus and vehicle/boss scenes as candidates, rather than
adding a level after each owner test. Map classification reference:
https://raymanpc.com/forum/threads/project-unlockray-ulr-rel.2406/page-19
PC asset names cross-check: https://steamdb.info/depot/15061/ .
These are catalog references, not hardware validation of every scene.
Dedicated movie, score, council, credits, menu and unknown scenes remain
excluded from general gameplay. Mapmonde keeps its distinct route.
Learn_10 alone requires the native story handover. All playable scenes
require live Playing and a readable clear native pause flag; the existing
owner, foreground, freshness, IPT fingerprint and native axis-pointer
checks remain. Level identity is checked at sampling and consumption;
require physical neutral input even between two gameplay scenes that
share the same semantic context. No changes to native level transitions,
original keyboard, story state or audio policy. Special bonus/vehicle
controls and cutscenes within a playable map remain to be verified.


## Wine 9385f7d2 / 20261125-041530

The owner log confirms Mapmonde native jump index 10, dialogue frame
suppression, and Learn_30 (catalog scene 2) transitioning from inactive
loading to gameplay. Both IPT axis consumers report calibration -50/0/50;
indices 10/21 and 11 show positive and negative native counter states
following South and East. The previous gameplay-context blocker is gone.
START opens the verified native pause menu; two paired audio resumes are
requested, with voice active and unpaused at the initial snapshots. Later
stream changes cause the existing safe native passthrough, not a reason to
relax audio ownership checks. Return to MENU is observed. No subsequent
playable scene, Alt+Tab or hotplug is established by this log, and actual
on-screen motion and audible quality are not proved by diagnostics alone.
The 128-sample cap was exhausted before the end of the first pause test.
Add a separately bounded context/focus/device/scene transition trace
(256 entries), including release bits and explicit exhaustion markers.
No change to input delivery, native audio or rendering in this commit.


## Completing the Dreamcast-style semantic routes

Reference: original Dreamcast US manual, controls and swimming sections:
https://segaretro.org/images/f/f2/Rayman2_DC_US_Manual.pdf . Left trigger
is used to dive and South/A to surface. D-pad Up accesses world knowledge;
D-pad Down calls Murfy. Original PC manual:
https://raymanpc.com/wiki/images/0/02/R2UKPCManual.pdf . The PC version
combines world knowledge and Murfy in F1. Reference action enumeration:
https://github.com/raytools/ACP_Ray2/blob/master/src/IPT/IPT_Const.h .
The retail fingerprint matches the first 28 native action descriptors;
reference indices 14/15 are Nage_Plonger/Nage_Remonter and index 22 is
AppelerMurphy. Route the existing target/jump semantics also to the native
swim entries and route world_knowledge/murphy to the same native entry 22.
The actor script retains ownership of when each action is consumed; no
new global keyboard emulation, no memory writes, and no expansion of
menu, map or cinematic scope. These additions are hardware-test candidates:
knowledge activation may depend on the original PC F1 hold behavior and
actor state, swimming behavior is not yet confirmed. Keep the full retail
fingerprint, foreground, thread, freshness and scene checks. Move the
explicit gameplay action table into the pure policy for regression tests.


## Controller information page: localized presentation contract

Prepare the five original PC language variants (English, French, Spanish,
Italian, German), independent of runtime menu detection. The model builds
all eleven rows from the same semantic controls and verified SDL family;
disconnection switches to a localized generic layout immediately. Explicit
pending markers distinguish the untested world-knowledge and swimming
routes. UTF-8 authored labels must be converted to wide text by the future
GDI renderer. The menu bridge now has a pure back-only controller_info
case, matching VIDEO semantics. No native menu state is classified as
controller_info yet; no original scripts, glyphs, cursor, renderer or
input interception have been changed by this commit. Real source evidence
for the main and pause controller-menu behaviors is still required before
activation. The old page remains usable in the interim.


## Offscreen controller diagram renderer

The 920x420 panel fits the 1024x768 native 4:3 canvas below the original
heading. A pure geometry contract validates all eleven distinct action
callouts, status, pending and back labels. The original polygon silhouette
and button symbols are drawn by a separate GDI preview renderer, not copied
from a product image. UTF-8 labels are converted to wide strings and measured
with GDI; rendering fails instead of clipping a label. The Windows CTest
renders 20 locale/device variants in a memory DIB with no game or display
window. This preview is deliberately opaque and linked only to the test;
production Glide blending, native submenu identification and integration
are still pending. No gameplay or original menu changes.


## Private translucent controller composition

The offscreen GDI renderer can now draw the identical foreground over
black and white, recovering channel-wise premultiplied coverage instead
of copying the preview background into the native scene. The pure
RGB565 compositor uses the existing VIDEO panel's 40% midnight-blue tint,
2-pixel blue border, 12-pixel radius and 4x corner coverage, with explicit
GameFrame LFB red/blue ordering. Outside rounded corners remain unchanged.
Windows offscreen tests cover the French PlayStation overlay and a synthetic
scene; cross-platform tests cover bounds, alpha and color ordering. This
is a private buffer operation only, not yet called by the game. Native
submenu authentication and live Glide presentation remain pending.


## Native controller submenu discovery checkpoint

CI 45546925442 passed Linux and Windows x86 including the offscreen
controller renderer and private composition. Optimize whole-pixel panel
coverage outside the four rounded corners; compare every pixel against
the original 4x coverage algorithm in CTest. No change in appearance.
Add RUNTIME-CONTROLLER-MENU transition telemetry at the verified native
flip, on the original camera thread only, with Log=1. Sample at most
20 Hz and log only changes, separately capped at 256 records. Report
MENU and PAUSE model behavior indices, verified MENU selection when
available, and exit. The original menus and input remain untouched.
The next owner session should enter and leave the existing controller
submenu in startup Options and in the native pause Options (if present),
pausing briefly on each screen, then test swimming/knowledge if practical.
Those observations distinguish the real submenu from adjacent Options
states; do not infer a writable script or activate the replacement from
behavior numbers alone. No source asset was available for new offline
verification in this session. A menu visit is the remaining external
validation dependency.


## Initial live controller panel integration

Owner log 20261125-045122, build 87c11283, confirms three MENU normal21/reflex0 visits and one PAUSE normal17/reflex2 visit. Both distinguish VIDEO and parent Options. Use those exact live authenticated models on the native camera thread. Render cached localized overlay through existing Glide LFB access before original flip. Locale derives from original LANGUAGE handle 370, unknown falls back to English. Only after successful panel rendering, preserve native title glyphs and suppress other native JFFTXT glyphs in the authenticated page. SDL3 context becomes controller_info (East back only); keyboard Enter/Space/arrows are suppressed only in that successfully rendered foreground page. Native Escape and other screens remain intact. Legacy DirectInput gamepad/configuration script dispatch is not yet disabled; visual layout and that remaining route require testing. Log also shows index15 delivery but no proof of swimming or world knowledge. No original game resource changed.


## Read-only controller page: native confirmation guard

The keyboard filter is now a separately tested pure policy, retaining
Escape and unrelated physical keys. The existing authenticated IPT
predicate additionally suppresses only original Menu_Valider (index 9)
when the foreground render owner has a successfully drawn controller
panel in the live MENU21/0 or PAUSE17/2 state, and the full retail table
fingerprint and entry membership pass. No action table writes, no blanket
DirectInput disabling, and no changes to native cancellation or gameplay.
This closes the known predicate path, not every possible original input
consumer: the real calibration menu must still be tested for accidental
activation. Log successful SDL back delivery and any blocked confirmation.


## Owner visual feedback (20261125-050627)

Both controller pages render, keyboard calibration blocked and SDL back
confirmed. Correct the panel's physical aspect using the existing HUD
squeeze at the LFB boundary, with box-filtered premultiplied coverage.
At 16:9, 920 logical pixels occupy 690 internal pixels, centered at x167;
true 4:3 retains 920 at x52. Shared VIDEO top y284. Preserve longer native
CONTROL heading glyphs in a bounded title band (owner log x19.3..23.5,
y24.97..30.03) and normalize title to y20..25. Candidate native companion
lift of 0.18 on existing validated centered actor source routes in both
controller pages, including true 4:3. Exact visual match to VIDEO needs
owner validation; no claim of identical position yet. Mapmonde entry log
shows GLI Frame x0..100/y0..100; preserve that full-screen source through
the native HUD hook, including downstream double-squeeze guard. Original
material and fade remain untouched; other geometry and levels unaffected.
This is a candidate visual fix, not yet confirmed in the game.


## Owner 20261125-053902: follow-up

The 88ab00fb log confirms the Mapmonde full-width route and the panel
squeeze. Pause header/luciole visually good, MENU title absent and
butterfly intermittently behind the panel. Original MENU script CONTROL
handle 373 target row uses V13[0]=150, V13[1]=500; allow the native 15%
heading band and retain the native font, with bounded glyph diagnostics.
MENU companion travel is compressed around y=.29 instead of subtracting
.18 only on the old narrow band. Preserve Pause lift unchanged.
The only Xbox type2 device in the provided log appears at the end in
MENU, after id1 PlayStation disconnects; no Xbox gameplay sample.
Do not swap SDL South/East globally. Enumerate up to eight SDL devices;
an idle owner can yield to a previously neutral controller's fresh
physical activity. The existing gate releases old inputs and requires
neutral on handover before accepting gameplay; no simultaneous merge.
Record device name/VID/PID and independent South/East/Start edge traces
per device. Native legacy input interference remains a hypothesis, and
this change does not disable the original DirectInput device globally.


## Owner 20261125-055436: duplicate native joystick actions

Build 4c420c7f: SDL owner switches between PS4 054c:05c4 and Xbox
045e:028e. In Learn_30, id2 south=1 east=0 produces SDL mask 0x0010
while native IPT 10,11,21 become positive together (log lines 858–864,
875–880,896–901). East produces only SDL 0x0020; Start produces 0x0040
and the game enters pause. Do not swap mapping. Offline objdump of the
owner retail PE SHA256 9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713
confirms real joystick GetDeviceState(80) at 496F37/496F4C, distinct from
keyboard GetDeviceState(256). At 496F9C..49701D, native code computes six
center-relative axes at state+0x0c..0x20 and button mask at state+4.
The sole direct call at 496ACA returns to 496ACF before 497050 computes
directions and 4970C0 records history. Hook that exact call as a fourth
atomic verified IPT installation site; invoke original and neutralize
only those output fields on successful poll, fresh foreground SDL
ownership and supported context. Preserve flags, keyboard, status,
unknown contexts and no-device fallback. No global DirectInput patch,
no original assets redistributed. Hardware test still required.


## Automatic first useful input and family-specific diagram

Owner 20261125-061927 confirms native joystick neutralization triggered
and two owner handovers. At MENU controller_info, the first id2 East
press selects Xbox but is discarded by the generic context/device gate;
the next press exits. Deliver the first fresh, previously neutral
secondary-device action only when the old owner was idle and the same
context/scene remains active. Attach/focus/scene changes retain the
physical neutral gate. Start remains the pause command, never activation.
Use family-specific original vector schematics with offset Xbox sticks,
symmetric PlayStation sticks, Start/Options upper right, Back/Share
upper left (drawn only, no new gameplay binding), matching callout
anchors and French PlayStation button names. B/Rond still exits.
No change to the native menu companion positioning in this revision.


## Owner 20261125-063547: controller artwork refinement

Build 6458ae3d shows the title and family-specific panel; log confirms
first Xbox South handover delivered to MENU without Start (219–227).
The source butterfly y=.288–.291 overlaps the title in the supplied
capture. Move only MENU controller companion to y=.145 with narrow
animation, preserving pause and other menus. The actual on-screen
bounds still require owner validation. Redraw both original controller
silhouettes using cubic curves, layered stick caps, rounded shoulders
and Xbox button colors. Use family-specific label order (Xbox movement
above D-pad actions), edge anchors and a compile-time segment intersection
check across every callout. No new gamepad mappings, assets or dependencies.


## Owner 20261125-064744: visual finish

The last screenshot shows the butterfly above the heading and the owner
now requests it beneath. Keep the pause heading and companion unchanged.
Move only the MENU heading to 12..17% and source butterfly to .32 with
narrow travel, based on the visible center offset from the last capture.
The panel origin stays y284. Redraw curved shell with a shaded finish,
recessed shoulder/center buttons, textured stick rings and subtle color
buttons. Render cached overlays at 2x resolution, box-filter recovered
premultiplied foreground coverage, and keep the existing RGB565 physical
aspect correction. Leader geometry stays unchanged and intersection-tested;
replace arrowheads with small anchor dots for a cleaner reading order.
No new dependencies or assets; mapping and ownership untouched. Real
visual bounds/appearance still need validation in-game.


## Enchanted controller artwork direction

Owner approved the existing positioning and input behavior, and approved
an enchanted Rayman-inspired concept. Treat the concept as artistic
reference, not a screenshot or static localized page. This revision
implements original scalable illustrations in a separate private
GDI+ art layer (not a generated raster of the full concept): curved
ivory/lavender shells, turquoise spiral caps, jewel-like face buttons,
carved-style status plaque and restrained leaf/spiral accents. Exact
control centers and the non-crossing leaders remain in the existing
layout contract. Paint into a 2x transparent premultiplied DIB, blend
into existing black/white foreground passes, then retain the tested
GameFrame composition. GDI+ and AlphaBlend are Windows/Wine system
APIs; no extra runtime redistributable or game asset required. The
native title, moving companion, localized strings, input and scene
hooks are untouched. This is an initial production illustration, not
a pixel-exact reproduction of the concept. Visual validation and
potential refinements (organic panel border) remain open.


## Final decorative polish and live physical-input indicators

Owner approved the enchanted rendering (3c35b1d4). Add a softly glowing
organic inner rim, carved plaque detail and a subtle text shadow without
changing the safe panel bounds or original title/companion. Display live
physical input on the cached diagram: face buttons, Start/Back, shoulders,
triggers, D-pad and sticks, including vertical right-stick movement and
stick clicks. Display-only fields do not change mapping or owner selection.
Indicators are composited in bounded regions after the cached static art,
with no per-frame GDI allocation/rerasterization. Focus loss, stale sample,
disconnection and non-controller context extinguish them. Existing native
menu cancellation remains functional (B/Rond may exit before a highlight
is visible). No decorative animation added to avoid unnecessary visual load.
Unit tests cover both aspect widths, physical masks and reset conditions.
