#!/usr/bin/env bash
#
# release_testflight_mac.sh - Prepara e archivia una nuova build macOS per TestFlight.
#
# Gemello di release_testflight.sh (che resta la versione iOS, invariata). Stessa
# struttura e stessi passi; cambia solo cio' che e' davvero specifico del Mac:
#   - qt-cmake della toolchain macos invece di ios
#   - -destination 'generic/platform=macOS'
#   - cartella di build separata (build/macos-appstore)
#
#   1. Incrementa il BUILD NUMBER (+1) in TUTTI i punti coerenti:
#        CMakeLists.txt : MACOSX_BUNDLE_BUNDLE_VERSION, XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION
#        Info.plist     : CFBundleVersion
#      (la VERSIONE MARKETING X.Y NON viene toccata: si cambia con ExC/bump-version.sh)
#      NB: i campi sono gli stessi che usa la build iOS — il build number e'
#      condiviso fra le due piattaforme. Non e' un problema per App Store Connect,
#      che li tiene distinti per piattaforma, ma spiega perche' il numero "salta"
#      dopo un rilascio iOS.
#   2. Committa il bump (a meno di --no-commit).
#   3. Pulisce la cache Xcode (DerivedData) e la cartella del progetto generato:
#      il passo CRITICO — senza, Xcode riusa oggetti stantii e produce un binario
#      che passa la validazione ma non contiene i fix.
#   4. Rigenera il progetto Xcode PULITO con qt-cmake (toolchain macOS).
#   5. Archivia con `xcodebuild archive`.
#   6. Apre l'Organizer di Xcode per l'upload manuale su App Store Connect.
#
# NON fa l'upload: quello resta manuale in Xcode (nessuna API key richiesta).
#
# PERCHE' QUESTO E NON release_appstore.sh: qui firma, embedding dei framework e
# creazione del .pkg li fa Xcode durante Distribute App, con "Automatically
# manage signing". release_appstore.sh fa gli stessi passi a mano (macdeployqt +
# codesign + productbuild) ed e' molto piu' fragile: resta come alternativa se
# si vuole il .pkg senza passare da Xcode.
#
# PREREQUISITI (una volta sola):
#   - Certificati "Apple Distribution" e "Mac Installer Distribution" nel
#     portachiavi (creali su developer.apple.com -> Certificates).
#   - Su App Store Connect, la piattaforma macOS va ABILITATA sulla stessa app
#     (stesso bundle id dell'iOS): l'upload viene rifiutato se manca.
#
# Uso:
#   ./ExC/Mac/release_testflight_mac.sh                 bump +1, commit, clean, archivia, apri Organizer
#   ./ExC/Mac/release_testflight_mac.sh --no-commit     come sopra ma NON committa il bump
#   ./ExC/Mac/release_testflight_mac.sh --no-archive    solo bump + clean + rigenera Xcode, poi apre il progetto
#   ./ExC/Mac/release_testflight_mac.sh --help
#
# Richiede: macOS, Xcode + command line tools, qt-cmake macOS (Qt 6.10.1).

set -euo pipefail

# ---------------------------------------------------------------------------
# Configurazione
# ---------------------------------------------------------------------------
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"      # lo script vive in ExC/Mac/
CMAKE="$PROJECT_DIR/CMakeLists.txt"
PLIST="$PROJECT_DIR/Info.plist"
BUILD_DIR="$PROJECT_DIR/build/macos-appstore"
XCODEPROJ="$BUILD_DIR/SurfaceExplorer.xcodeproj"
SCHEME="SurfaceExplorer"
QT_CMAKE="$HOME/Qt/6.10.1/macos/bin/qt-cmake"
GIT_BRANCH="v1"
ARCHIVE_DIR="$HOME/Library/Developer/Xcode/Archives"

err()  { printf '\033[31mERRORE:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[32m✓\033[0m %s\n' "$*"; }

# ---------------------------------------------------------------------------
# Argomenti
# ---------------------------------------------------------------------------
DO_COMMIT=1
DO_ARCHIVE=1
for arg in "$@"; do
  case "$arg" in
    --no-commit)  DO_COMMIT=0 ;;
    --no-archive) DO_ARCHIVE=0 ;;
    --help|-h)
      sed -n '2,48p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) err "Argomento sconosciuto: $arg  (usa --help)" ;;
  esac
done

# ---------------------------------------------------------------------------
# Controlli preliminari
# ---------------------------------------------------------------------------
[ "$(uname)" = "Darwin" ] || err "Questo script gira solo su macOS."
[ -f "$CMAKE" ] || err "CMakeLists.txt non trovato in $PROJECT_DIR"
[ -f "$PLIST" ] || err "Info.plist non trovato in $PROJECT_DIR"
command -v xcodebuild >/dev/null || err "xcodebuild non trovato (installa Xcode + command line tools)."
[ -x "$QT_CMAKE" ] || err "qt-cmake macOS non trovato/eseguibile: $QT_CMAKE"
[ -f "$PROJECT_DIR/ExC/Mac/macos_appstore.entitlements" ] \
  || err "Entitlements sandbox non trovati: ExC/Mac/macos_appstore.entitlements"

# Certificato di distribuzione: senza, l'archive gira ma Distribute App fallisce
# a meta' strada. Meglio dirlo ORA (avviso, non errore: l'archive resta utile
# anche solo per verificare che la build stia in piedi).
if ! security find-identity -v -p codesigning 2>/dev/null | grep -q "Apple Distribution"; then
  info "ATTENZIONE: nessun certificato \"Apple Distribution\" nel portachiavi."
  info "  L'archive verra' creato, ma l'upload su App Store Connect fallira'."
  info "  Crealo su https://developer.apple.com/account/resources/certificates"
  info "  (servono \"Apple Distribution\" per l'app e \"Mac Installer Distribution\" per il .pkg)."
fi

cd "$PROJECT_DIR"

if [ "$DO_COMMIT" -eq 1 ] && ! git diff --quiet HEAD 2>/dev/null; then
  info "Working tree con modifiche non committate: verranno incluse nel commit del bump."
fi

# ---------------------------------------------------------------------------
# 1. Leggi e incrementa il build number
# ---------------------------------------------------------------------------
CUR_BUILD="$(sed -nE 's/.*MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"([0-9]+)".*/\1/p' "$CMAKE" | head -1)"
[ -n "$CUR_BUILD" ] || err "Build number non trovato in CMakeLists.txt (MACOSX_BUNDLE_BUNDLE_VERSION)."
NEW_BUILD=$(( CUR_BUILD + 1 ))
MARKETING="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' "$CMAKE" | head -1)"
MARKETING="${MARKETING:-?}"

info "Versione marketing: $MARKETING  |  Build number: $CUR_BUILD -> $NEW_BUILD"

# --- CMakeLists.txt: i campi build number (devono restare tutti uguali) ---
# NB: dopo l'aggiunta del ramo macOS i campi sono QUATTRO (due per iOS, due per
# macOS): la sed li aggiorna tutti perche' non e' ancorata a un ramo.
tmp="$(mktemp)"
sed -E \
  -e "s/(MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
  -e "s/(XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+\")[0-9]+(\")/\1${NEW_BUILD}\2/" \
  "$CMAKE" > "$tmp" && mv "$tmp" "$CMAKE"

# --- Info.plist: <string> sulla riga DOPO la chiave CFBundleVersion ---
tmp="$(mktemp)"
awk -v v="$NEW_BUILD" '
  f && /<string>[0-9]+<\/string>/ { sub(/<string>[0-9]+<\/string>/, "<string>" v "</string>"); f=0 }
  /<key>CFBundleVersion<\/key>/ { f=1 }
  { print }
' "$PLIST" > "$tmp" && mv "$tmp" "$PLIST"

# --- Verifica coerenza: TUTTE le occorrenze, non solo la prima ---
BAD="$(grep -E 'MACOSX_BUNDLE_BUNDLE_VERSION[[:space:]]+"[0-9]+"|XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION[[:space:]]+"[0-9]+"' "$CMAKE" \
        | grep -vE "\"${NEW_BUILD}\"" || true)"
[ -z "$BAD" ] || err "Bump incoerente in CMakeLists.txt, righe rimaste indietro:\n$BAD"
P1="$(awk '/<key>CFBundleVersion<\/key>/{getline; gsub(/[^0-9]/,""); print; exit}' "$PLIST")"
[ "$P1" = "$NEW_BUILD" ] || err "Bump incoerente: Info.plist($P1), atteso $NEW_BUILD."
ok "Build number aggiornato a $NEW_BUILD (CMakeLists + Info.plist)."

# Info.plist deve restare un plist valido (una copia-cartella lo puo' corrompere).
plutil -lint "$PLIST" >/dev/null || err "Info.plist non valido dopo il bump (plutil -lint fallito)."

# ---------------------------------------------------------------------------
# 2. Commit del bump
# ---------------------------------------------------------------------------
if [ "$DO_COMMIT" -eq 1 ]; then
  git add "$CMAKE" "$PLIST"
  git commit -m "macOS: build number -> $NEW_BUILD (TestFlight)" >/dev/null
  ok "Commit del bump creato."
  info "Ricorda di fare 'git push origin $GIT_BRANCH' quando vuoi."
else
  info "--no-commit: bump applicato ai file ma NON committato."
fi

# ---------------------------------------------------------------------------
# 3. Pulizia cache Xcode + progetto generato (passo critico)
# ---------------------------------------------------------------------------
info "Pulizia DerivedData (SurfaceExplorer-*) e $BUILD_DIR ..."
find "$HOME/Library/Developer/Xcode/DerivedData" -maxdepth 1 -name 'SurfaceExplorer-*' -exec rm -rf {} + 2>/dev/null || true
rm -rf "$BUILD_DIR"
ok "Cache e progetto puliti."

# ---------------------------------------------------------------------------
# 4. Rigenera il progetto Xcode PULITO con qt-cmake (toolchain macOS)
# ---------------------------------------------------------------------------
info "Rigenerazione progetto Xcode macOS con qt-cmake ..."
"$QT_CMAKE" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Xcode >/dev/null
[ -d "$XCODEPROJ" ] || err "Rigenerazione fallita: $XCODEPROJ non creato."
ok "Progetto Xcode rigenerato."

info "Valori nel progetto generato:"
grep -h "MARKETING_VERSION\|CURRENT_PROJECT_VERSION\|PRODUCT_BUNDLE_IDENTIFIER\|CODE_SIGN_ENTITLEMENTS" \
  "$XCODEPROJ/project.pbxproj" | sort -u | sed 's/^/    /'

if [ "$DO_ARCHIVE" -eq 0 ]; then
  info "--no-archive: apro il progetto in Xcode. Fai Product -> Archive a mano."
  open "$XCODEPROJ"
  ok "Fatto (preparazione build)."
  exit 0
fi

# ---------------------------------------------------------------------------
# 5. Archive con xcodebuild
# ---------------------------------------------------------------------------
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
ARCHIVE_PATH="$ARCHIVE_DIR/$(date +%Y-%m-%d)/SurfaceExplorer_mac_${MARKETING}_${NEW_BUILD}_${STAMP}.xcarchive"
mkdir -p "$(dirname "$ARCHIVE_PATH")"

info "Archiviazione (xcodebuild archive) — può richiedere PARECCHI minuti."
info "NON interrompere anche se sembra ferma."
xcodebuild archive \
  -project "$XCODEPROJ" \
  -scheme "$SCHEME" \
  -configuration Release \
  -destination 'generic/platform=macOS' \
  -archivePath "$ARCHIVE_PATH" \
  CODE_SIGN_STYLE=Automatic \
  | tail -40
XC_STATUS=${PIPESTATUS[0]}
[ "$XC_STATUS" -eq 0 ] || err "xcodebuild archive fallito (exit $XC_STATUS)."
[ -d "$ARCHIVE_PATH" ] || err "Archivio non prodotto: $ARCHIVE_PATH"
ok "Archivio creato: $ARCHIVE_PATH"

# Verifica anti-stantìo + controllo che la sandbox sia davvero attiva: se manca,
# l'App Store rifiuta il pacchetto, ed e' molto meglio accorgersene qui.
BIN="$ARCHIVE_PATH/Products/Applications/SurfaceExplorer.app"
if [ -d "$BIN" ]; then
  info "Bundle archiviato: $(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "$BIN")"
  if codesign -d --entitlements - --xml "$BIN" 2>/dev/null | grep -q "app-sandbox"; then
    ok "Sandbox attiva sul bundle archiviato."
  else
    info "ATTENZIONE: sandbox NON rilevata sul bundle. L'App Store rifiutera' il pacchetto."
    info "  Verifica XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS nel ramo macOS del CMakeLists."
  fi
fi

# ---------------------------------------------------------------------------
# 6. Apri l'Organizer per l'upload manuale
# ---------------------------------------------------------------------------
info "Apro l'Organizer di Xcode. Da lì:"
cat <<'STEPS'
    1. Seleziona l'archivio appena creato (in alto la Version deve mostrare il nuovo build).
    2. Distribute App -> App Store Connect -> Upload -> Next.
    3. Automatically manage signing -> Next -> Upload.
       (Xcode firma, incorpora i framework Qt e costruisce il .pkg da solo.)
    4. Poi: attendi l'elaborazione, gestisci Export Compliance ("Nessuno degli algoritmi"),
       assegna la build al gruppo "Internal Testers".
    5. I tester su Mac installano dall'app TestFlight PER MAC (va scaricata a parte
       dal Mac App Store): a differenza di iOS non e' gia' presente sul sistema.
STEPS
open -a Xcode "$ARCHIVE_PATH"
ok "Build macOS $MARKETING ($NEW_BUILD) pronta per l'upload su TestFlight."
