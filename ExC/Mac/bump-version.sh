#!/usr/bin/env bash
#
# bump-version.sh - Aggiorna il numero di versione marketing in TUTTI i punti che
# devono restare coerenti, poi committa. Evita il disallineamento (es. 1.0 vs 3.0)
# nato dal doverli modificare a mano uno per uno.
#
# Aggiorna:
#   CMakeLists.txt : project(SurfaceExplorer VERSION X.Y ...)  <- unico punto
#   Info.plist     : CFBundleShortVersionString
#
# I campi del bundle (MACOSX_BUNDLE_SHORT_VERSION_STRING, XCODE_..._MARKETING_VERSION)
# e la define APP_VERSION letta dal dialogo About derivano da ${PROJECT_VERSION}:
# non contengono il numero e non vanno aggiornati.
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
OLD="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9.]+).*/\1/p' "$CMAKE" | head -1)"
OLD="${OLD:-?}"

# --- 1. CMakeLists.txt (3 campi) ---------------------------------------------
# NB: il primo pattern ancora 'VERSION' a inizio riga (dopo soli spazi) per NON
# toccare cmake_minimum_required(VERSION ...) ne' i *_BUNDLE_VERSION (build number).
# Dalla 1.2 il numero sta in UN SOLO punto: project(SurfaceExplorer VERSION X.Y ...).
# I campi del bundle e la define APP_VERSION lo derivano da ${PROJECT_VERSION},
# quindi qui non vanno piu' toccati (se li si tocca si rompe la derivazione).
tmp="$(mktemp)"
sed -E \
  -e "s/^(project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+)[0-9][0-9.]*/\1${NEW}/" \
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
check "^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+${NEW}[[:space:]]" "$CMAKE" "CMakeLists project(VERSION)"
# La derivazione deve restare in piedi: se qualcuno rimette un numero letterale
# in questi campi, il bump aggiornerebbe project() e loro resterebbero indietro.
check 'MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+"\$\{PROJECT_VERSION\}"' "$CMAKE" "SHORT_VERSION_STRING derivato da PROJECT_VERSION"
check 'XCODE_ATTRIBUTE_MARKETING_VERSION[[:space:]]+"\$\{PROJECT_VERSION\}"'  "$CMAKE" "MARKETING_VERSION derivato da PROJECT_VERSION"
check 'APP_VERSION="\$\{PROJECT_VERSION\}"'                                   "$CMAKE" "define APP_VERSION derivata da PROJECT_VERSION"
grep -A1 'CFBundleShortVersionString' "$PLIST" | grep -qE "<string>${NEW}</string>" \
  || err "sostituzione fallita: Info.plist CFBundleShortVersionString"

printf '\nVersione: %s -> %s   (aggiornati CMakeLists.txt + Info.plist)\n' "$OLD" "$NEW"

# --- Il dialogo About NON va piu' toccato: dalla 1.2 stampa APP_VERSION, che
# arriva da project(... VERSION ...) nel CMakeLists via target_compile_definitions.
# Se un giorno tornasse a mostrare un numero sbagliato, il colpevole e' un binario
# stantio (ricompilare) oppure qualcuno che ha riscritto a mano quella stringa.

# --- 3. commit (salvo --no-commit) -------------------------------------------
if [ "$DO_COMMIT" -eq 1 ]; then
  ( cd "$PROJECT_DIR" && git add CMakeLists.txt Info.plist \
      && git commit -m "Bump versione $NEW" )
  printf '\nCommit creato. Ricorda di: git push\n'
else
  printf '\n--no-commit: file aggiornati ma non committati.\n'
fi
