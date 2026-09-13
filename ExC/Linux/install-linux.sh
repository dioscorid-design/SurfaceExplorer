#!/usr/bin/env bash
#
# install-linux.sh - Integra Surface Explorer (AppImage) nel desktop Linux.
#
# Uso:
#   ./install-linux.sh [percorso/Surface...AppImage]   installa o aggiorna
#   ./install-linux.sh --uninstall                      rimuove l'integrazione
#   ./install-linux.sh --help                           mostra questo aiuto
#
# Senza argomenti cerca automaticamente un file SurfaceExplorer*.AppImage
# nella cartella dello script (o nella cartella corrente).
#
# Non richiede root: installa solo nella home dell'utente.

set -euo pipefail

# --- Configurazione ---------------------------------------------------------
APP_ID="surface-explorer"                       # basename di .desktop e icona
APP_NAME="Surface Explorer"
WM_CLASS="SurfaceExplorer"                       # deve combaciare con StartupWMClass dell'app
INSTALL_DIR="${HOME}/Applications"              # dove vive l'AppImage
TARGET_APPIMAGE="${INSTALL_DIR}/${WM_CLASS}.AppImage"   # nome fisso => Exec stabile tra le versioni
DESKTOP_DIR="${HOME}/.local/share/applications"
# Radice del tema icone: ogni PNG va nella cartella della PROPRIA dimensione.
# Prima qui c'era una cartella sola, cablata a 256x256, e ci finiva dentro
# un'icona da 64px: il desktop la ingrandiva 4 volte e appariva sfocata.
ICON_BASE="${HOME}/.local/share/icons/hicolor"
DESKTOP_FILE="${DESKTOP_DIR}/${APP_ID}.desktop"

msg()  { printf '>>> %s\n' "$*"; }
err()  { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

usage() { sed -n '3,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# --- Disinstallazione -------------------------------------------------------
uninstall() {
  msg "Rimuovo l'integrazione desktop di ${APP_NAME}..."
  rm -f "$DESKTOP_FILE"
  # Le icone stanno in piu' cartelle, una per dimensione: vanno rimosse tutte,
  # altrimenti la disinstallazione ne lascia in giro otto su nove.
  rm -f "$ICON_BASE"/*/apps/"${APP_ID}.png"
  if [ -f "$TARGET_APPIMAGE" ]; then
    printf '    Rimuovo anche %s ? [s/N] ' "$TARGET_APPIMAGE"
    read -r ans
    case "$ans" in
      s|S|y|Y) rm -f "$TARGET_APPIMAGE"; msg "AppImage rimossa." ;;
      *)       msg "AppImage lasciata al suo posto." ;;
    esac
  fi
  refresh_caches
  msg "Disinstallazione completata."
  exit 0
}

# --- Trova l'AppImage sorgente ---------------------------------------------
# Cerca (senza ricorsione profonda) nelle posizioni piu' probabili:
# cartella dello script, cartella corrente, i loro sottocartelle dist/,
# e ~/Downloads. Sceglie la piu' recente se ce n'e' piu' d'una.
find_source() {
  local self_dir
  self_dir="$(cd "$(dirname "$0")" && pwd)"
  local dirs=("$self_dir" "$self_dir/dist" "$self_dir/.." "$self_dir/../dist" "$PWD" "$PWD/dist" "$HOME/Downloads")
  local matches=()
  local dir cand
  for dir in "${dirs[@]}"; do
    [ -d "$dir" ] || continue
    while IFS= read -r cand; do
      [ -n "$cand" ] && matches+=("$cand")
    done < <(find "$dir" -maxdepth 1 -iname 'SurfaceExplorer*.AppImage' 2>/dev/null)
  done
  [ "${#matches[@]}" -eq 0 ] && return 1
  # piu' recente per data di modifica
  ls -t "${matches[@]}" 2>/dev/null | head -1
}

# --- Aggiorna le cache di menu e icone --------------------------------------
refresh_caches() {
  command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
  command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache -f -t "${HOME}/.local/share/icons/hicolor" 2>/dev/null || true
}

# --- Estrai l'icona dall'AppImage (best effort) -----------------------------
extract_icon() {
  local src="$1" tmp
  tmp="$(mktemp -d)"
  # trap per pulire la cartella temporanea in ogni caso
  trap 'rm -rf "$tmp"' RETURN
  # L'AppImage contiene il set completo (16..512): CMakeLists lo installa in
  # usr/share/icons/hicolor. Ogni PNG va copiata nella cartella del tema che
  # corrisponde alla sua dimensione REALE, letta dal percorso di origine.
  # NON si usa .DirIcon: e' la miniatura da 64px della AppImage, e finiva nella
  # cartella 256x256 -- il desktop la ingrandiva 4 volte e appariva sfocata.
  local png dir n=0
  ( cd "$tmp" && "$src" --appimage-extract 'usr/share/icons/hicolor/*' >/dev/null 2>&1 ) || true
  for png in "$tmp"/squashfs-root/usr/share/icons/hicolor/*/apps/*.png; do
    [ -f "$png" ] || continue
    dir="$(basename "$(dirname "$(dirname "$png")")")"      # es. 256x256
    case "$dir" in
      [0-9]*x[0-9]*)
        install -Dm644 "$png" "${ICON_BASE}/${dir}/apps/${APP_ID}.png" && n=$((n+1)) ;;
    esac
  done
  if [ "$n" -gt 0 ]; then
    msg "Icone installate: $n dimensioni sotto ${ICON_BASE}"
    return 0
  fi

  # Ripiego: .DirIcon, collocata in 64x64 che e' la sua dimensione abituale.
  # Una cartella piccola al massimo fa rimpicciolire l'immagine, e rimpicciolire
  # non sfoca; il contrario si', ed e' il difetto che questa funzione elimina.
  ( cd "$tmp" && "$src" --appimage-extract .DirIcon >/dev/null 2>&1 ) || true
  if [ -e "$tmp/squashfs-root/.DirIcon" ]; then
    install -Dm644 "$tmp/squashfs-root/.DirIcon" "${ICON_BASE}/64x64/apps/${APP_ID}.png"
    msg "Icone del tema non trovate: uso .DirIcon come ripiego (64x64)."
    return 0
  fi
  return 1
}

# --- Main -------------------------------------------------------------------
[ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] && usage
[ "${1:-}" = "--uninstall" ] && uninstall

SOURCE="${1:-}"
if [ -z "$SOURCE" ]; then
  SOURCE="$(find_source)" || err "Nessun file SurfaceExplorer*.AppImage trovato. Passalo come argomento."
fi
[ -f "$SOURCE" ] || err "File non trovato: $SOURCE"

msg "Sorgente:      $SOURCE"
msg "Installo in:   $TARGET_APPIMAGE"

# 1. Copia l'AppImage nella posizione stabile (nome fisso => aggiornamento pulito)
mkdir -p "$INSTALL_DIR"
if [ "$(readlink -f "$SOURCE")" != "$(readlink -f "$TARGET_APPIMAGE" 2>/dev/null || echo /nonexistent)" ]; then
  install -Dm755 "$SOURCE" "$TARGET_APPIMAGE"
else
  chmod +x "$TARGET_APPIMAGE"
fi

# 2. Icone (tutte le dimensioni presenti nell'AppImage; extract_icon riferisce)
if extract_icon "$TARGET_APPIMAGE"; then
  ICON_VALUE="$APP_ID"
else
  msg "Icona non estratta: uso un'icona generica di sistema."
  ICON_VALUE="application-x-executable"
fi

# 3. File .desktop
mkdir -p "$DESKTOP_DIR"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=${APP_NAME}
Comment=Real-time parametric surface explorer
Exec=${TARGET_APPIMAGE} %F
Icon=${ICON_VALUE}
Terminal=false
Categories=Education;Science;Math;Graphics;
StartupNotify=true
StartupWMClass=${WM_CLASS}
EOF
chmod +x "$DESKTOP_FILE"
msg "Voce di menu creata: $DESKTOP_FILE"

# 4. Aggiorna le cache
refresh_caches

cat <<EOF

=== FATTO ===
${APP_NAME} e' ora nel menu applicazioni.
  Eseguibile: ${TARGET_APPIMAGE}
  Menu:       ${DESKTOP_FILE}

Se non compare subito, esci e rientra dalla sessione (o riavvia la shell grafica).
Per aggiornare a una nuova versione: rilancia questo script con la nuova AppImage.
Per disinstallare:                    ./install-linux.sh --uninstall
EOF
