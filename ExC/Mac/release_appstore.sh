#!/bin/bash
#
# release_appstore.sh - Pipeline MAC APP STORE per Surface Explorer.
#
#   macdeployqt (framework nel bundle) -> firma sandboxed (Apple Distribution)
#   -> .pkg (productbuild) -> validazione/upload su App Store Connect.
#
# NON sostituisce release_macos.sh: quello resta la distribuzione DIRETTA
# (DMG Developer ID + notarizzazione), che continua a funzionare come prima.
# Qui cambia tutto il canale: certificati diversi, entitlements diversi
# (sandbox), pacchetto .pkg invece del .dmg, destinazione App Store Connect.
#
# Uso:
#   ./release_appstore.sh                deploy + firma + .pkg (nessun upload)
#   ./release_appstore.sh --validate     come sopra, poi valida su App Store Connect
#   ./release_appstore.sh --upload       come sopra, poi CARICA su App Store Connect
#   ./release_appstore.sh --help
#
# PREREQUISITI (una volta sola, su developer.apple.com e Xcode):
#   1. Certificati installati in portachiavi:
#        "Apple Distribution: ..."            (firma dell'app)
#        "3rd Party Mac Developer Installer"  (firma del .pkg)
#      Si creano da Certificates, Identifiers & Profiles -> Certificates (+).
#   2. Provisioning profile "Mac App Store" per com.dioscorid.surfaceexplorer,
#      scaricato in ExC/Mac/embedded.provisionprofile.
#   3. App creata su App Store Connect con lo stesso bundle id.
#   4. Per --upload/--validate: credenziali notarytool/altool. Il modo piu'
#      semplice e' una app-specific password:
#        xcrun notarytool store-credentials appstore-profile \
#              --apple-id adenio@libero.it --team-id AJ655XKJR8
#      oppure esporta ASC_API_KEY / ASC_API_ISSUER per la chiave API.
#
# PRIMA di lanciare: compila la build RELEASE del progetto.

set -e

# --- CONFIGURAZIONE ---
APP_NAME="SurfaceExplorer"
BUNDLE_ID="com.dioscorid.surfaceexplorer"
TEAM_ID="AJ655XKJR8"
APPLE_ID="adenio@libero.it"

# Canale App Store: certificati DIVERSI da quelli del DMG.
SIGN_APP="Apple Distribution"                     # prefisso: lo script risolve il nome completo
SIGN_PKG="3rd Party Mac Developer Installer"      # idem

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # ExC/Mac
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"               # root del progetto
BUILD_DIR="$PROJECT_DIR/build/Desktop_Qt_6_10-Release"
SRC_APP="$BUILD_DIR/$APP_NAME.app"

# Lavoriamo su una COPIA: macdeployqt e la firma sandboxed modificano il bundle
# in modo irreversibile, e la build di sviluppo deve restare avviabile.
STAGE_DIR="$BUILD_DIR/appstore_staging"
APP_PATH="$STAGE_DIR/$APP_NAME.app"
PKG_PATH="$SCRIPT_DIR/$APP_NAME.pkg"

ENTITLEMENTS="$SCRIPT_DIR/macos_appstore.entitlements"
PROFILE="$SCRIPT_DIR/embedded.provisionprofile"

MACDEPLOYQT="$HOME/Qt/6.10.1/macos/bin/macdeployqt"

usage() { sed -n '3,35p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# --- ARGOMENTI ---
DO_UPLOAD=0
DO_VALIDATE=0
for a in "$@"; do
  case "$a" in
    --upload)   DO_UPLOAD=1 ;;
    --validate) DO_VALIDATE=1 ;;
    --help|-h)  usage ;;
    *) echo "ERRORE: argomento sconosciuto: $a (usa --help)"; exit 1 ;;
  esac
done

# --- VERSIONE (stessa fonte di release_macos.sh: project(...) in CMakeLists) ---
VERSION="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' \
           "$PROJECT_DIR/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || { echo "ERRORE: versione non trovata in project(...) di CMakeLists.txt"; exit 1; }
echo ">>> Versione: $VERSION"

# --- CONTROLLI PRELIMINARI -------------------------------------------------
# Meglio fallire qui con un messaggio chiaro che a meta' pipeline con un errore
# di codesign incomprensibile.

[ -d "$SRC_APP" ] || {
  echo "ERRORE: $SRC_APP non trovato."
  echo "        Compila prima la build Release del progetto."
  exit 1
}

[ -f "$ENTITLEMENTS" ] || { echo "ERRORE: entitlements non trovati: $ENTITLEMENTS"; exit 1; }

[ -x "$MACDEPLOYQT" ] || {
  echo "ERRORE: macdeployqt non trovato in $MACDEPLOYQT"
  echo "        Correggi MACDEPLOYQT in cima allo script."
  exit 1
}

# Risolve il nome COMPLETO dei certificati dal portachiavi (contengono il nome
# e il Team ID, che cambiano da account ad account: non li scriviamo a mano).
resolve_identity() {
  security find-identity -v -p codesigning 2>/dev/null \
    | grep "\"$1" | head -1 | sed -E 's/.*"(.*)"$/\1/'
}
SIGN_APP_FULL="$(resolve_identity "$SIGN_APP")"
[ -n "$SIGN_APP_FULL" ] || {
  echo "ERRORE: certificato \"$SIGN_APP...\" non trovato nel portachiavi."
  echo ""
  echo "  Il canale App Store richiede certificati DIVERSI da quelli del DMG."
  echo "  Creali su https://developer.apple.com/account/resources/certificates"
  echo "    - Apple Distribution            (firma dell'app)"
  echo "    - Mac Installer Distribution    (firma del .pkg)"
  echo "  poi scaricali e aprili con doppio clic per installarli."
  echo ""
  echo "  Certificati attualmente disponibili:"
  security find-identity -v -p codesigning | sed 's/^/    /'
  exit 1
}
echo ">>> Firma app: $SIGN_APP_FULL"

# Il certificato installer serve solo per il .pkg: se manca lo diciamo ora.
SIGN_PKG_FULL="$(security find-identity -v 2>/dev/null | grep "\"$SIGN_PKG" | head -1 | sed -E 's/.*"(.*)"$/\1/')"
[ -n "$SIGN_PKG_FULL" ] || {
  echo "ERRORE: certificato \"$SIGN_PKG...\" non trovato nel portachiavi."
  echo "        Serve per firmare il .pkg. Crealo come 'Mac Installer Distribution'."
  exit 1
}
echo ">>> Firma pkg: $SIGN_PKG_FULL"

# --- 1. COPIA PULITA -------------------------------------------------------
echo ""
echo ">>> 1. Copia del bundle in staging..."
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp -R "$SRC_APP" "$APP_PATH"

# --- 2. MACDEPLOYQT --------------------------------------------------------
# PASSO CRITICO. Senza, il bundle non ha Contents/Frameworks/ e punta ai
# framework in ~/Qt (Team ID di Qt, diverso dal nostro): la firma sandboxed
# fallirebbe con "Library not loaded ... different Team IDs", errore che SEMBRA
# un problema di sandbox e invece e' solo bundle non deployato.
echo ""
echo ">>> 2. macdeployqt (incorpora i framework Qt nel bundle)..."
"$MACDEPLOYQT" "$APP_PATH" -verbose=1

[ -d "$APP_PATH/Contents/Frameworks" ] || {
  echo "ERRORE: macdeployqt non ha creato Contents/Frameworks. Bundle non deployato."
  exit 1
}

# --- 3. PROVISIONING PROFILE ----------------------------------------------
if [ -f "$PROFILE" ]; then
  echo ""
  echo ">>> 3. Incorporo il provisioning profile..."
  cp "$PROFILE" "$APP_PATH/Contents/embedded.provisionprofile"
else
  echo ""
  echo ">>> 3. ATTENZIONE: $PROFILE assente."
  echo "       L'upload su App Store Connect lo richiede. Scarica il profilo"
  echo "       'Mac App Store' per $BUNDLE_ID e salvalo li'."
fi

# --- 4. FIRMA SANDBOXED ----------------------------------------------------
# Dall'interno verso l'esterno: prima i framework e gli helper, poi l'app. Gli
# entitlements (sandbox) vanno SOLO sull'eseguibile principale.
echo ""
echo ">>> 4. Firma dei componenti interni..."
find "$APP_PATH/Contents/Frameworks" \
     \( -name "*.framework" -o -name "*.dylib" \) -maxdepth 1 -print0 2>/dev/null \
  | while IFS= read -r -d '' item; do
      codesign --force --timestamp --options runtime \
               --sign "$SIGN_APP_FULL" "$item" 2>/dev/null || true
    done

# Plugin Qt (ognuno e' un .dylib dentro Contents/PlugIns)
find "$APP_PATH/Contents/PlugIns" -name "*.dylib" -print0 2>/dev/null \
  | while IFS= read -r -d '' item; do
      codesign --force --timestamp --options runtime \
               --sign "$SIGN_APP_FULL" "$item" 2>/dev/null || true
    done

echo ">>> 4b. Firma del bundle con gli entitlements sandbox..."
codesign --force --timestamp --options runtime \
  --entitlements "$ENTITLEMENTS" \
  --sign "$SIGN_APP_FULL" \
  "$APP_PATH"

echo ""
echo ">>> 5. Verifica firma ed entitlements..."
codesign --verify --deep --strict --verbose=2 "$APP_PATH"
echo "--- entitlements effettivi ---"
codesign -d --entitlements - --xml "$APP_PATH" 2>/dev/null | plutil -p - || true
echo "------------------------------"

# La sandbox deve risultare ATTIVA: se manca, l'App Store rifiuta il pacchetto.
if ! codesign -d --entitlements - --xml "$APP_PATH" 2>/dev/null | grep -q "app-sandbox"; then
  echo "ERRORE: la sandbox non risulta attiva sul bundle firmato."
  exit 1
fi

# --- 6. PKG ----------------------------------------------------------------
echo ""
echo ">>> 6. Creazione del .pkg..."
rm -f "$PKG_PATH"
productbuild --component "$APP_PATH" /Applications \
             --sign "$SIGN_PKG_FULL" \
             "$PKG_PATH"

echo ""
echo "=== PKG PRONTO: $PKG_PATH ==="

# --- 7. VALIDAZIONE / UPLOAD ----------------------------------------------
if [ "$DO_VALIDATE" -eq 0 ] && [ "$DO_UPLOAD" -eq 0 ]; then
  echo ""
  echo "Nessun upload. Prossimi passi:"
  echo "  validazione:  $0 --validate"
  echo "  upload:       $0 --upload"
  echo "  (in alternativa: apri Transporter e trascina il .pkg)"
  exit 0
fi

# Credenziali: chiave API se presente, altrimenti app-specific password.
if [ -n "${ASC_API_KEY:-}" ] && [ -n "${ASC_API_ISSUER:-}" ]; then
  AUTH=(--apiKey "$ASC_API_KEY" --apiIssuer "$ASC_API_ISSUER")
  echo ">>> Autenticazione: chiave API App Store Connect"
elif [ -n "${ASC_PASSWORD:-}" ]; then
  AUTH=(--username "$APPLE_ID" --password "$ASC_PASSWORD")
  echo ">>> Autenticazione: app-specific password (ASC_PASSWORD)"
else
  echo "ERRORE: nessuna credenziale per App Store Connect."
  echo "        Esporta ASC_PASSWORD (app-specific password di $APPLE_ID)"
  echo "        oppure ASC_API_KEY + ASC_API_ISSUER."
  echo "        In alternativa carica il .pkg con l'app Transporter."
  exit 1
fi

if [ "$DO_VALIDATE" -eq 1 ]; then
  echo ""
  echo ">>> 7. Validazione su App Store Connect..."
  xcrun altool --validate-app -f "$PKG_PATH" -t macos "${AUTH[@]}"
  echo ">>> Validazione superata."
fi

if [ "$DO_UPLOAD" -eq 1 ]; then
  echo ""
  echo ">>> 8. Upload su App Store Connect..."
  xcrun altool --upload-app -f "$PKG_PATH" -t macos "${AUTH[@]}"
  echo ""
  echo "=== CARICATO SU APP STORE CONNECT ==="
  echo "La build compare in TestFlight/App Store dopo l'elaborazione (qualche minuto)."
fi
