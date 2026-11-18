#!/usr/bin/env bash
# Test experimental Rayman2Fix Learn_10 under Wine and audit known PC saves.
# Run from the same folder as the TEST COPY of Rayman2Fix.exe.
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)" || exit 1
cd -- "$SCRIPT_DIR" || exit 1

if [[ ! -f ./Rayman2Fix.exe ]]; then
    printf 'ERREUR : Rayman2Fix.exe introuvable dans %s\n' "$PWD" >&2
    exit 1
fi
for program in curl python3 wine; do
    if ! command -v "$program" >/dev/null 2>&1; then
        printf 'ERREUR : commande manquante : %s\n' "$program" >&2
        exit 1
    fi
done

printf 'Dossier de test : %s\n' "$PWD"
printf 'Tous les rapports et le script Python seront conservés ici.\n'

# Download only into the installer folder; never use /tmp.
if ! curl -fLsS --retry 2 \
    'https://raw.githubusercontent.com/legluondunet/Rayman2Fix/experiment/learn10-native-nomovies/tools/learn10_save_audit.py' \
    -o ./learn10_save_audit.py; then
    printf 'ERREUR : téléchargement de l’auditeur impossible.\n' >&2
    exit 1
fi

# Fail closed: without a readable initial inventory, no game launch.
if ! python3 ./learn10_save_audit.py snapshot "$PWD" "$PWD/rayman2-before.json"; then
    printf 'ERREUR : inventaire initial incomplet ou invalide. Le jeu ne sera pas lancé.\n' >&2
    printf 'Vérifier Data/SaveGame ou Menu/SaveGame (dans game/ si besoin).\n' >&2
    exit 1
fi

printf '\nLancement Wine (copie de test uniquement).\n'
printf 'Dans learn_10, attendre environ une seconde avant Entrée.\n'
printf 'Quitter le jeu sans sauvegarder pour ce premier contrôle.\n\n'

# To use an already prepared, ISOLATED Wine prefix, set WINEPREFIX before running.
RAYMAN2FIX_LEARN10_NOMOVIES_PROTOTYPE=1 wine ./Rayman2Fix.exe
wine_status=$?

printf '\nComparaison des fichiers de sauvegarde...\n'
python3 ./learn10_save_audit.py compare "$PWD" \
    "$PWD/rayman2-before.json" "$PWD/rayman2-after.json"
audit_status=$?

printf '\nRésultats dans %s :\n' "$PWD"
printf '  learn10_save_audit.py\n  rayman2-before.json\n  rayman2-after.json\n'
printf '  Rayman2Fix.log (si généré par le jeu)\n'
printf 'Codes de sortie : wine=%s, audit=%s\n' "$wine_status" "$audit_status"
if (( audit_status != 0 )); then
    exit "$audit_status"
fi
exit "$wine_status"
