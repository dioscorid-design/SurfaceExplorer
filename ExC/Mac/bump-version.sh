#!/usr/bin/env bash
#
# bump-version.sh - Aggiorna il numero di versione marketing in TUTTI i punti che
# devono restare coerenti, poi committa. Evita il disallineamento (es. 1.0 vs 3.0)
# nato dal doverli modificare a mano uno per uno.
#
# Aggiorna:
#   CMakeLists.txt : VERSION / MACOSX_BUNDLE_SHORT_VERSION_STRING / XCODE_..._MARKETING_VERSION
#   Info.plist     : CFBundleShortVersionString
#
# NON tocca il BUILD NUMBER (MACOSX_BUNDLE_BUNDLE_VERSION / CFBundleVersion): quello
# si incrementa a parte, a ogni upload verso Apple (vedi RELEASE_GUIDE.md).
#
# Uso:
#   ./ExC/Mac/bump-version.sh 1.1          aggiorna i file e committa "Bump versione 1.1"
#   ./ExC/Mac/bump-version.sh 1.1 --no-commit   aggiorna soltanto i file
#
# Portabile Linux/macOS (bash + sed/awk POSIX). Su Windows: Git Bash o WSL.

set -euo pipefail

# --- root del progetto: lo script vive in ExC/Mac/, la root e' due cartelle sopra ---
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
CMAKE="$PROJECT_DIR/CMakeLists.txt"
PLIST="$PROJECT_DIR/Info.plist"

err() { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

# --- argomenti ---
NEW="${1:-}"
DO_COMMIT=1
[ "${2:-}" = "--no-commit" ] && DO_COMMIT=0
[ -n "$NEW" ] || err "Uso: $0 X.Y [--no-commit]   (es. $0 1.1)"
printf '%s' "$NEW" | grep -qE '^[0-9]+\.[0-9]+$' || err "Versione '$NEW' non valida: attesa X.Y (es. 1.1)"
[ -f "$CMAKE" ] || err "CMakeLists.txt non trovato in $PROJECT_DIR"
[ -f "$PLIST" ] || err "Info.plist non trovato in $PROJECT_DIR"

# --- versione attuale (per il riepilogo) ---
OLD="$(sed -nE 's/.*MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+"([0-9.]+)".*/\1/p' "$CMAKE" | head -1)"
OLD="${OLD:-?}"

# --- 1. CMakeLists.txt (3 campi) ---------------------------------------------
# NB: il primo pattern ancora 'VERSION' a inizio riga (dopo soli spazi) per NON
# toccare cmake_minimum_required(VERSION ...) ne' i *_BUNDLE_VERSION (build number).
tmp="$(mktemp)"
sed -E \
  -e "s/^([[:space:]]*VERSION[[:space:]]+\")[0-9][0-9.]*(\")/\1${NEW}\2/" \
  -e "s/(MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+\")[0-9][0-9.]*(\")/\1${NEW}\2/" \
  -e "s/(XCODE_ATTRIBUTE_MARKETING_VERSION[[:space:]]+\")[0-9][0-9.]*(\")/\1${NEW}\2/" \
  "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"

# --- 2. Info.plist (valore sulla riga DOPO la chiave) ------------------------
tmp="$(mktemp)"
awk -v v="$NEW" '
  f && /<string>[0-9][0-9.]*<\/string>/ { sub(/<string>[0-9][0-9.]*<\/string>/, "<string>" v "</string>"); f=0 }
  /CFBundleShortVersionString/ { f=1 }
  { print }
' "$PLIST" > "$tmp" && mv "$tmp" "$PLIST"

# --- verifica che i 4 punti riportino ora la nuova versione ------------------
check() { grep -qE "$1" "$2" || err "sostituzione fallita: $3 (pattern non trovato dopo l'edit)"; }
check "^[[:space:]]*VERSION[[:space:]]+\"${NEW}\""                       "$CMAKE" "CMakeLists VERSION"
check "MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+\"${NEW}\""         "$CMAKE" "CMakeLists SHORT_VERSION_STRING"
check "XCODE_ATTRIBUTE_MARKETING_VERSION[[:space:]]+\"${NEW}\""          "$CMAKE" "CMakeLists MARKETING_VERSION"
grep -A1 'CFBundleShortVersionString' "$PLIST" | grep -qE "<string>${NEW}</string>" \
  || err "sostituzione fallita: Info.plist CFBundleShortVersionString"

printf '\nVersione: %s -> %s   (aggiornati CMakeLists.txt + Info.plist)\n' "$OLD" "$NEW"

# --- PROMEMORIA: la stringa mostrata dal dialogo About (mainwindow.cpp,
# "Version X.Y" dentro QMessageBox::about) e' hardcoded e NON viene toccata da
# questo script: va aggiornata a mano.
printf '\nPROMEMORIA: aggiorna a mano "Version %s" nel dialogo About (mainwindow.cpp).\n' "$NEW"

# --- 3. commit (salvo --no-commit) -------------------------------------------
if [ "$DO_COMMIT" -eq 1 ]; then
  ( cd "$PROJECT_DIR" && git add CMakeLists.txt Info.plist \
      && git commit -m "Bump versione $NEW" )
  printf '\nCommit creato. Ricorda di: git push\n'
else
  printf '\n--no-commit: file aggiornati ma non committati.\n'
fi
