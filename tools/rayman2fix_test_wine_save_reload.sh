#!/usr/bin/env bash
# Rayman2Fix PR31: two-stage disposable Wine save/reload test.
# All downloaded tools and output files stay next to Rayman2Fix.exe.
# NEVER run against a real game installation or personal saves.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")" && pwd -P)"
cd -- "$ROOT"
for cmd in curl python3 wine tar find; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERREUR : commande absente : $cmd" >&2
        exit 2
    fi
done
if [[ ! -f Rayman2Fix.exe || ! -d game ]]; then
    echo "ERREUR : executer depuis une COPIE de l'installation, a cote de Rayman2Fix.exe et game/." >&2
    exit 2
fi

PREFIX="rayman2-save-reload-$(date +%Y%m%d-%H%M%S)"
if [[ -e "$PREFIX-before.json" ]]; then
    echo "ERREUR : ce nom de rapport existe deja." >&2
    exit 2
fi

echo "INSTALLATION DE TEST : $ROOT"
echo "Tous les outils, rapports et sauvegardes copiees restent dans ce dossier."
echo "ATTENTION : l'archive finale contiendra des sauvegardes brutes."
echo "Le script ne restaure, ne supprime, ni ne modifie les fichiers du jeu."
echo "Les deux lancements utilisent le WINEPREFIX actuel : il doit etre isole."
echo

curl -fLsS --retry 2 \
    'https://raw.githubusercontent.com/legluondunet/Rayman2Fix/experiment/learn10-native-nomovies/tools/learn10_save_audit.py' \
    -o ./learn10_save_audit.py

snapshot() {
    local label="$1"
    python3 ./learn10_save_audit.py snapshot "$ROOT" "$ROOT/$PREFIX-$label.json"
    python3 - "$ROOT" "$ROOT/$PREFIX-$label-saves.zip" <<'PY'
import os
from pathlib import Path
import sys
import zipfile
sys.path.insert(0, sys.argv[1])
import learn10_save_audit as audit

root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
dirs = audit.save_dirs(root)
if not dirs:
    raise SystemExit("ERREUR : aucun SaveGame connu a archiver")
# Current.cfg identifies the PC save slots, so include Data/Options if present.
for prefix in (root, root / "game"):
    node = prefix
    for part in ("Data", "Options"):
        if not node.is_dir() or node.is_symlink():
            break
        matched = [p for p in node.iterdir()
                   if p.name.casefold() == part.casefold()]
        if len(matched) != 1 or not matched[0].is_dir() or matched[0].is_symlink():
            break
        node = matched[0]
    else:
        dirs.append(node)

count = 0
with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for folder in dict.fromkeys(dirs):
        for base, children, names in os.walk(folder, followlinks=False):
            base = Path(base)
            children[:] = [n for n in children if not (base / n).is_symlink()]
            for name in names:
                path = base / name
                if path.is_symlink() or not path.is_file():
                    raise SystemExit("ERREUR : fichier special/lien : " + str(path))
                if path.stat().st_size > 64 * 1024 * 1024:
                    raise SystemExit("ERREUR : sauvegarde >64 Mio : " + str(path))
                count += 1
                if count > 4096:
                    raise SystemExit("ERREUR : trop de fichiers")
                archive.write(path, path.relative_to(root).as_posix())
print(f"ARCHIVE : {out.name} ({count} fichiers; aucune ecriture dans game/)")
PY
}

capture_log() {
    local stage="$1"
    local marker="$2"
    local dest="$ROOT/$PREFIX-$stage.log"
    local recent=""
    recent="$(find "$ROOT" -maxdepth 1 -type f -name 'Rayman2Fix(*).log' \
        -newer "$marker" -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)"
    if [[ -z "$recent" || ! -f "$recent" ]]; then
        echo "ERREUR : aucun nouveau journal Rayman2Fix(...) pour $stage" >&2
        return 2
    fi
    cp -p -- "$recent" "$dest"
    echo "JOURNAL : $dest"
}

echo "=== ETAT INITIAL ==="
snapshot before

echo
echo "=== SESSION 1 : SAUT NOMOVIES ACTIVE ==="
echo "Demarrer une partie jetable, attendre ~1 seconde dans learn_10,"
echo "appuyer sur Entree, verifier le jeu, sauvegarder normalement si possible."
echo "Quitter COMPLETEMENT le jeu apres le test."
touch "$ROOT/$PREFIX-session1.marker"
set +e
RAYMAN2FIX_LEARN10_NOMOVIES_PROTOTYPE=1 wine ./Rayman2Fix.exe
wine1=$?
set -e
snapshot after-skip
python3 ./learn10_save_audit.py compare "$ROOT" \
    "$ROOT/$PREFIX-before.json" "$ROOT/$PREFIX-before-vs-skip.json"
capture_log run1 "$ROOT/$PREFIX-session1.marker"
if [[ $wine1 -ne 0 ]] || \
   ! grep -q 'RUNTIME-LEARN10-PROTOTYPE: enabled=1' "$ROOT/$PREFIX-run1.log" || \
   ! grep -q 'phase=handover-verification status=pass' "$ROOT/$PREFIX-run1.log"; then
    echo "ARRET : pas de preuve d'un saut natif restaure dans la premiere session." >&2
    echo "Fournir les fichiers du prefixe $PREFIX plutot que poursuivre." >&2
    exit 3
fi

echo
echo "=== SESSION 2 : RECHARGEMENT SANS NOMOVIES ==="
echo "Depuis le menu, charger la sauvegarde du test si elle est disponible."
echo "Verifier progression, camera et controles, puis quitter normalement."
echo "Si aucune sauvegarde n'est chargeable, ce test ne prouvera pas la persistance."
read -r -p "Appuyer sur Entree pour demarrer le deuxieme lancement... " _
touch "$ROOT/$PREFIX-session2.marker"
set +e
env -u RAYMAN2FIX_LEARN10_NOMOVIES_PROTOTYPE wine ./Rayman2Fix.exe
wine2=$?
set -e
snapshot after-reload
python3 ./learn10_save_audit.py compare "$ROOT" \
    "$ROOT/$PREFIX-after-skip.json" "$ROOT/$PREFIX-skip-vs-reload.json"
capture_log run2 "$ROOT/$PREFIX-session2.marker"

if grep -q 'RUNTIME-LEARN10-PROTOTYPE: enabled=1' "$ROOT/$PREFIX-run2.log"; then
    echo "ALERTE : la session 2 a active le prototype !" >&2
    exit 4
fi

tar -czf "$ROOT/$PREFIX-diagnostics.tar.gz" \
    "$PREFIX-before.json" "$PREFIX-after-skip.json" "$PREFIX-after-reload.json" \
    "$PREFIX-before-vs-skip.json" "$PREFIX-skip-vs-reload.json" \
    "$PREFIX-before-saves.zip" "$PREFIX-after-skip-saves.zip" \
    "$PREFIX-after-reload-saves.zip" "$PREFIX-run1.log" "$PREFIX-run2.log"

echo
echo "RESULTAT : $ROOT/$PREFIX-diagnostics.tar.gz"
echo "Wine : session1=$wine1 session2=$wine2"
echo "ATTENTION : l'archive contient des sauvegardes brutes; ne pas publier sans verifier."
echo "L'audit seul ne prouve jamais l'absence du bit 1188 en sauvegarde."
if [[ $wine2 -ne 0 ]]; then
    exit "$wine2"
fi
