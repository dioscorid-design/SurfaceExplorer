#!/usr/bin/env bash
#
# release_linux.sh - Pipeline di release Linux per Surface Explorer.
#
#   Compila (Qt/CMake) -> impacchetta in AppImage autonoma -> (opz.) carica
#   su GitHub Releases, accanto a install-linux.sh.
#
# Uso:
#   ./release_linux.sh                build + AppImage in dist/
#   ./release_linux.sh --upload       come sopra, poi crea/aggiorna la release
#                                     GitHub del tag corrente e carica gli asset
#   ./release_linux.sh --skip-build   riusa un binario gia' compilato
#   ./release_linux.sh --help
#
# >>> PER UNA NUOVA VERSIONE, prima di lanciare lo script aggiorna la versione
# >>> (vedi RELEASE_GUIDE.md) in:
# >>>   - CMakeLists.txt : VERSION / MACOSX_BUNDLE_SHORT_VERSION_STRING / _BUNDLE_VERSION
# >>>   - Info.plist     : CFBundleShortVersionString / CFBundleVersion
# >>> poi committa e pusha. La versione (=> tag vX.Y) viene letta da CMakeLists.txt.
#
# Requisiti: cmake, ninja, un Qt 6 gcc_64 (variabile QT_DIR), wget, curl, git.
# Non richiede FUSE (gli AppImage-tool girano in modalita' extract-and-run).

set -euo pipefail

# ============================ CONFIGURAZIONE ================================
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
QT_DIR="${QT_DIR:-$HOME/Qt/6.10.2/gcc_64}"      # override: QT_DIR=... ./release_linux.sh
REPO="dioscorid-design/SurfaceExplorer"
APP_BIN="surface-explorer"                       # OUTPUT_NAME Linux (vedi CMakeLists.txt)

BUILD_DIR="$PROJECT_DIR/build/linux-release"
APPDIR="$PROJECT_DIR/build/AppDir"
DIST_DIR="$PROJECT_DIR/dist"
TOOLS_DIR="$PROJECT_DIR/build/appimage-tools"    # cache di linuxdeploy

LD_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
LDQT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"

msg()  { printf '\n\033[1;34m>>> %s\033[0m\n' "$*"; }
err()  { printf '\033[1;31mERRORE:\033[0m %s\n' "$*" >&2; exit 1; }
usage() { sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# ============================ ARGOMENTI ====================================
DO_UPLOAD=0; SKIP_BUILD=0
for a in "$@"; do
  case "$a" in
    --upload)     DO_UPLOAD=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    --help|-h)    usage ;;
    *) err "Argomento sconosciuto: $a (usa --help)" ;;
  esac
done

# ============================ VERSIONE =====================================
VERSION="$(grep -oE 'MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+"[0-9.]+"' "$PROJECT_DIR/CMakeLists.txt" \
           | grep -oE '[0-9]+\.[0-9]+' | head -1)"
[ -n "$VERSION" ] || VERSION="$(grep -oE 'VERSION[[:space:]]+"[0-9.]+"' "$PROJECT_DIR/CMakeLists.txt" | grep -oE '[0-9.]+' | head -1)"
[ -n "$VERSION" ] || err "Versione non trovata in CMakeLists.txt"
TAG="v$VERSION"
OUTPUT="SurfaceExplorer-${TAG}-linux-x86_64.AppImage"
msg "Versione: $VERSION   Tag: $TAG   Output: $OUTPUT"

# ============================ 1. BUILD =====================================
if [ "$SKIP_BUILD" -eq 0 ]; then
  [ -x "$QT_DIR/bin/qmake" ] || err "Qt non trovato in QT_DIR=$QT_DIR (impostalo: QT_DIR=... $0)"
  msg "1/4 Compilo la release (CMake + Ninja) con Qt in $QT_DIR ..."
  cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_DIR"
  cmake --build "$BUILD_DIR"
else
  msg "1/4 --skip-build: salto la compilazione (riuso $BUILD_DIR)"
fi
[ -f "$BUILD_DIR/$APP_BIN" ] || err "Binario $APP_BIN non trovato in $BUILD_DIR. Compila prima (senza --skip-build)."

# ============================ 2. AppDir ====================================
# Usa le regole install() del CMakeLists (bin + .desktop + icone hicolor)
msg "2/4 Popolo l'AppDir con 'cmake --install' ..."
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null
[ -f "$APPDIR/usr/bin/$APP_BIN" ] || err "install non ha prodotto $APPDIR/usr/bin/$APP_BIN"

# ============================ 3. AppImage ==================================
msg "3/4 Costruisco l'AppImage con linuxdeploy (+plugin Qt) ..."
mkdir -p "$TOOLS_DIR"

# Scarica ed ESTRAE gli AppImage-tool (l'estrazione via --appimage-extract non
# richiede FUSE; eseguirli poi tramite AppRun evita del tutto la dipendenza FUSE).
setup_tool() {  # $1=url  $2=nome-base
  local url="$1" name="$2"
  local ai="$TOOLS_DIR/$name.AppImage" ed="$TOOLS_DIR/$name.dir"
  [ -x "$ed/AppRun" ] && return 0            # gia' estratto e valido
  local try
  for try in 1 2; do
    [ -f "$ai" ] || wget -q -O "$ai" "$url"
    chmod +x "$ai"
    rm -rf "$ed" "$TOOLS_DIR/squashfs-root"
    if ( cd "$TOOLS_DIR" && "$ai" --appimage-extract >/dev/null 2>&1 ) && [ -x "$TOOLS_DIR/squashfs-root/AppRun" ]; then
      mv "$TOOLS_DIR/squashfs-root" "$ed"; return 0
    fi
    # download corrotto/troncato: butta e riprova
    rm -f "$ai"
  done
  err "Impossibile scaricare/estrarre $name (download corrotto). Riprova."
}
setup_tool "$LD_URL"   linuxdeploy
setup_tool "$LDQT_URL" linuxdeploy-plugin-qt

mkdir -p "$DIST_DIR"
rm -f "$DIST_DIR/$OUTPUT"
# linuxdeploy scrive l'output nella CWD: lo eseguo dentro dist/ cosi' l'AppImage
# ci finisce direttamente. APPIMAGE_EXTRACT_AND_RUN=1 serve per appimagetool
# (invocato internamente); il plugin qt viene trovato via PATH (usr/bin estratto).
( cd "$DIST_DIR" && env APPIMAGE_EXTRACT_AND_RUN=1 \
    QMAKE="$QT_DIR/bin/qmake" \
    PATH="$TOOLS_DIR/linuxdeploy-plugin-qt.dir/usr/bin:$PATH" \
    LD_LIBRARY_PATH="$QT_DIR/lib:${LD_LIBRARY_PATH:-}" \
    OUTPUT="$OUTPUT" \
    "$TOOLS_DIR/linuxdeploy.dir/AppRun" \
      --appdir "$APPDIR" \
      --plugin qt \
      --output appimage )
[ -f "$DIST_DIR/$OUTPUT" ] || err "linuxdeploy non ha prodotto $OUTPUT"
( cd "$DIST_DIR" && sha256sum "$OUTPUT" > "$OUTPUT.sha256" )
msg "AppImage pronta: $DIST_DIR/$OUTPUT ($(du -h "$DIST_DIR/$OUTPUT" | cut -f1))"

# ============================ 4. UPLOAD (opz.) =============================
if [ "$DO_UPLOAD" -eq 0 ]; then
  cat <<EOF

=== BUILD COMPLETATA (nessun upload) ===
  $DIST_DIR/$OUTPUT
  $DIST_DIR/$OUTPUT.sha256
Per caricare su GitHub:  $0 --upload
EOF
  exit 0
fi

msg "4/4 Upload su GitHub Releases ($REPO, tag $TAG) ..."
TOKEN="${GH_TOKEN:-$(grep 'github.com' "$HOME/.git-credentials" 2>/dev/null | sed -E 's#https://([^:]+):([^@]+)@.*#\2#' | head -1)}"
[ -n "$TOKEN" ] || err "Nessun token GitHub (imposta GH_TOKEN o configura git credential.helper store)."
API="https://api.github.com/repos/$REPO"; UPL="https://uploads.github.com/repos/$REPO"
gh() { curl -s -H "Authorization: token $TOKEN" -H "Accept: application/vnd.github+json" "$@"; }

# 4a. assicura il tag vX.Y su origin (lo crea su HEAD se manca; NON sposta uno esistente)
if ! git ls-remote --tags origin "$TAG" | grep -q "$TAG"; then
  msg "Tag $TAG assente sul remoto: lo creo su HEAD ($(git rev-parse --short HEAD)) e lo pusho"
  git tag -a "$TAG" -m "Release $TAG" 2>/dev/null || git tag "$TAG"
  git push origin "$TAG"
else
  msg "Tag $TAG gia' presente sul remoto (lo uso cosi' com'e')"
fi

# 4b. trova (o crea) la release del tag
RID="$(gh "$API/releases/tags/$TAG" | python3 -c 'import json,sys;d=json.load(sys.stdin);print(d.get("id",""))')"
if [ -z "$RID" ]; then
  msg "Creo la release per $TAG"
  RID="$(gh -X POST "$API/releases" -d "{\"tag_name\":\"$TAG\",\"name\":\"Surface Explorer $TAG\",\"draft\":false}" \
         | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')"
fi
[ -n "$RID" ] || err "Impossibile ottenere/creare la release."
msg "Release id: $RID"

# 4c. carica gli asset, sostituendo eventuali omonimi
upload_asset() {
  local file="$1" name ctype="$2" existing
  name="$(basename "$file")"
  existing="$(gh "$API/releases/$RID/assets" | python3 -c "import json,sys;print(next((a['id'] for a in json.load(sys.stdin) if a['name']=='$name'),''))")"
  [ -n "$existing" ] && { msg "Rimuovo asset esistente $name"; gh -X DELETE "$API/releases/assets/$existing" >/dev/null; }
  msg "Carico $name"
  curl -s --max-time 600 -H "Authorization: token $TOKEN" -H "Content-Type: $ctype" \
    --data-binary @"$file" -o /dev/null -w "  -> HTTP %{http_code}\n" "$UPL/$RID/assets?name=$name"
}
upload_asset "$DIST_DIR/$OUTPUT"        "application/octet-stream"
upload_asset "$DIST_DIR/$OUTPUT.sha256" "text/plain"
[ -f "$PROJECT_DIR/install-linux.sh" ] && upload_asset "$PROJECT_DIR/install-linux.sh" "application/x-shellscript"

cat <<EOF

=== RELEASE $TAG PUBBLICATA ===
  https://github.com/$REPO/releases/tag/$TAG
Asset caricati: $OUTPUT (+.sha256) e install-linux.sh
EOF
