#!/bin/bash
#
# release_macos.sh - Pipeline di release macOS per Surface Explorer.
#
#   Firma (Developer ID + Hardened Runtime) -> .dmg (con link ad /Applications)
#   -> notarizza -> staple -> (opz.) carica su GitHub Releases.
#
# Uso:
#   ./release_macos.sh              firma + .dmg + notarizza (nessun upload)
#   ./release_macos.sh --upload     come sopra, poi crea/aggiorna la release
#                                   GitHub del tag corrente e carica il .dmg
#   ./release_macos.sh --help
#
# >>> PER UNA NUOVA VERSIONE, prima di lanciare lo script aggiorna la versione
# >>> (vedi RELEASE_GUIDE.md) in CMakeLists.txt / Info.plist, poi committa e
# >>> pusha. La versione (=> tag vX.Y) viene letta da CMakeLists.txt.
#
# Il token GitHub viene preso da GH_TOKEN oppure da ~/.git-credentials.

set -e

# --- CONFIGURAZIONE ---
APP_NAME="SurfaceExplorer"
BUNDLE_ID="com.dioscorid.surfaceexplorer"
SIGN_IDENTITY="Developer ID Application: GAETANO MOSCHETTI (BAPKX72394)"
NOTARY_PROFILE="notarytool-profile"
REPO="dioscorid-design/SurfaceExplorer"

# Lo script vive in ExC/Mac/ : la root del progetto e' due cartelle sopra
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # ExC/Mac
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"               # root del progetto
BUILD_DIR="$PROJECT_DIR/build/Desktop_Qt_6_10-Release"
APP_PATH="$BUILD_DIR/$APP_NAME.app"
DMG_NAME="$APP_NAME.dmg"
DMG_PATH="$SCRIPT_DIR/$DMG_NAME"                             # .dmg prodotto accanto allo script, in ExC/Mac

ENTITLEMENTS="$SCRIPT_DIR/macos_release.entitlements"

usage() { sed -n '3,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# --- ARGOMENTI ---
DO_UPLOAD=0
for a in "$@"; do
  case "$a" in
    --upload)  DO_UPLOAD=1 ;;
    --help|-h) usage ;;
    *) echo "ERRORE: argomento sconosciuto: $a (usa --help)"; exit 1 ;;
  esac
done

# --- VERSIONE / TAG (letti da CMakeLists.txt, come nello script Linux) ---
# Unica fonte: project(SurfaceExplorer VERSION X.Y ...). I campi del bundle la
# derivano da ${PROJECT_VERSION} e NON contengono piu' un numero, quindi qui non
# si possono leggere. Il vecchio fallback generico su 'VERSION "[0-9.]+"' era
# peggio che inutile: pescava MACOSX_BUNDLE_BUNDLE_VERSION "15", cioe' il BUILD
# NUMBER, e produceva il tag v15 al posto di v1.2.
VERSION="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' \
           "$PROJECT_DIR/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || { echo "ERRORE: versione non trovata in project(...) di CMakeLists.txt"; exit 1; }
TAG="v$VERSION"
ASSET_NAME="SurfaceExplorer-${TAG}-macos.dmg"               # nome versionato dell'asset su GitHub

if [ ! -d "$APP_PATH" ]; then
  echo "ERRORE: $APP_PATH non trovato. Compila prima il progetto (macdeployqt incluso)."
  exit 1
fi

echo ">>> 1. Firma con Developer ID + Hardened Runtime..."
codesign --force --deep --options runtime \
  --entitlements "$ENTITLEMENTS" \
  --sign "$SIGN_IDENTITY" \
  "$APP_PATH"

echo ">>> 2. Verifica firma..."
codesign --verify --deep --strict --verbose=2 "$APP_PATH"

echo ">>> 3. Creazione .dmg (con collegamento alla cartella Applicazioni)..."
rm -f "$DMG_PATH"
STAGING_DIR="$BUILD_DIR/dmg_staging"
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"
cp -R "$APP_PATH" "$STAGING_DIR/"
ln -s /Applications "$STAGING_DIR/Applications"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGING_DIR" -ov -format UDZO "$DMG_PATH"
rm -rf "$STAGING_DIR"

echo ">>> 4. Firma del .dmg..."
codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH"

echo ">>> 5. Invio a notarizzazione (attende il risultato, puo' richiedere alcuni minuti)..."
xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait

echo ">>> 6. Staple del ticket di notarizzazione..."
xcrun stapler staple "$DMG_PATH"

echo ""
echo "=== DMG PRONTO E NOTARIZZATO: $DMG_PATH ==="
echo "Verifica: spctl -a -t open --context context:primary-signing-identifier -v \"$DMG_PATH\""

# --- 7. UPLOAD su GitHub Releases (opz.) ---
if [ "$DO_UPLOAD" -eq 0 ]; then
  echo ""
  echo "Nessun upload. Per caricare su GitHub:  $0 --upload"
  exit 0
fi

echo ""
echo ">>> 7. Upload su GitHub Releases ($REPO, tag $TAG)..."
# Token GitHub: GH_TOKEN esplicito -> gh CLI (keyring) -> ~/.git-credentials (store)
TOKEN="${GH_TOKEN:-}"
[ -n "$TOKEN" ] || TOKEN="$(gh auth token 2>/dev/null || true)"
[ -n "$TOKEN" ] || TOKEN="$(grep 'github.com' "$HOME/.git-credentials" 2>/dev/null | sed -E 's#https://([^:]+):([^@]+)@.*#\2#' | head -1)"
[ -n "$TOKEN" ] || { echo "ERRORE: nessun token GitHub (imposta GH_TOKEN, oppure 'gh auth login', oppure configura git credential.helper store)."; exit 1; }
API="https://api.github.com/repos/$REPO"
gh_api() { curl -s -H "Authorization: token $TOKEN" -H "Accept: application/vnd.github+json" "$@"; }

# 7a. assicura il tag vX.Y su origin (lo crea su HEAD se manca; NON sposta uno esistente)
if ! git -C "$PROJECT_DIR" ls-remote --tags origin "$TAG" | grep -q "$TAG"; then
  echo ">>> Tag $TAG assente sul remoto: lo creo su HEAD ($(git -C "$PROJECT_DIR" rev-parse --short HEAD)) e lo pusho"
  git -C "$PROJECT_DIR" tag -a "$TAG" -m "Release $TAG" 2>/dev/null || git -C "$PROJECT_DIR" tag "$TAG"
  git -C "$PROJECT_DIR" push origin "$TAG"
else
  echo ">>> Tag $TAG gia' presente sul remoto (lo uso cosi' com'e')"
fi

# 7b. trova (o crea) la release del tag
RID="$(gh_api "$API/releases/tags/$TAG" | python3 -c 'import json,sys;d=json.load(sys.stdin);print(d.get("id",""))')"
if [ -z "$RID" ]; then
  echo ">>> Creo la release per $TAG"
  RID="$(gh_api -X POST "$API/releases" -d "{\"tag_name\":\"$TAG\",\"name\":\"Surface Explorer $TAG\",\"draft\":false}" \
         | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')"
fi
[ -n "$RID" ] || { echo "ERRORE: impossibile ottenere/creare la release."; exit 1; }
echo ">>> Release id: $RID"

# 7c. rimuovi i .dmg di VERSIONI PRECEDENTI rimasti sulla release (nome diverso dal
#     nuovo => --clobber non li tocca). Cancelliamo tutti i .dmg tranne quello nuovo.
echo ">>> Rimuovo eventuali .dmg di versioni precedenti dalla release..."
STALE_IDS="$(gh_api "$API/releases/$RID/assets" | python3 -c '
import json,sys
keep=sys.argv[1]
for a in json.load(sys.stdin):
    n=a.get("name","")
    if n.startswith("SurfaceExplorer-v") and n.endswith("-macos.dmg") and n!=keep:
        print(a["id"])
' "$ASSET_NAME")"
for aid in $STALE_IDS; do
  echo ">>>   elimino .dmg obsoleto (asset id $aid)"
  gh_api -X DELETE "$API/releases/assets/$aid" >/dev/null
done

# 7d. carica il .dmg con nome versionato. gh richiede che il file abbia il nome
#     dell'asset: copio il .dmg col nome versionato in una staging temporanea.
UPLOAD_DIR="$(mktemp -d)"
cp "$DMG_PATH" "$UPLOAD_DIR/$ASSET_NAME"
echo ">>> Carico $ASSET_NAME ($(du -h "$UPLOAD_DIR/$ASSET_NAME" | cut -f1))..."
if command -v gh >/dev/null 2>&1; then
  GH_TOKEN="$TOKEN" gh release upload "$TAG" "$UPLOAD_DIR/$ASSET_NAME" --clobber -R "$REPO" \
    || { echo "ERRORE: gh release upload di $ASSET_NAME fallito (vedi output sopra)."; rm -rf "$UPLOAD_DIR"; exit 1; }
  echo ">>>   -> OK ($ASSET_NAME via gh)"
else
  # Fallback: REST API via curl. Se 422 (nome gia' presente) rimuove il vecchio e riprova.
  UPL="https://uploads.github.com/repos/$REPO"
  do_upload() { curl -s -X POST --max-time 900 -H "Authorization: token $TOKEN" \
    -H "Content-Type: application/octet-stream" -H "Expect:" \
    --data-binary @"$1" -w $'\n%{http_code}' "$UPL/releases/$RID/assets?name=$(basename "$1")"; }
  resp="$(do_upload "$UPLOAD_DIR/$ASSET_NAME")"; code="${resp##*$'\n'}"
  if [ "$code" = "422" ]; then
    existing="$(gh_api "$API/releases/$RID/assets" | python3 -c "import json,sys;print(next((a['id'] for a in json.load(sys.stdin) if a['name']=='$ASSET_NAME'),''))")"
    [ -n "$existing" ] && { echo ">>>   asset omonimo presente: lo sostituisco"; gh_api -X DELETE "$API/releases/assets/$existing" >/dev/null; }
    resp="$(do_upload "$UPLOAD_DIR/$ASSET_NAME")"; code="${resp##*$'\n'}"
  fi
  [ "$code" = "201" ] || { echo "ERRORE: upload di $ASSET_NAME fallito (HTTP $code). Risposta: ${resp%$'\n'*}"; rm -rf "$UPLOAD_DIR"; exit 1; }
  echo ">>>   -> OK ($ASSET_NAME)"
fi
rm -rf "$UPLOAD_DIR"

echo ""
echo "=== RELEASE $TAG PUBBLICATA ==="
echo "  https://github.com/$REPO/releases/tag/$TAG"
echo "Asset caricato: $ASSET_NAME"
