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
ICON_DIR="${HOME}/.local/share/icons/hicolor/256x256/apps"
DESKTOP_FILE="${DESKTOP_DIR}/${APP_ID}.desktop"
ICON_FILE="${ICON_DIR}/${APP_ID}.png"

msg()  { printf '>>> %s\n' "$*"; }
err()  { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

usage() { sed -n '3,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

# --- Disinstallazione -------------------------------------------------------
uninstall() {
  msg "Rimuovo l'integrazione desktop di ${APP_NAME}..."
  rm -f "$DESKTOP_FILE" "$ICON_FILE"
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
  local dirs=("$self_dir" "$self_dir/dist" "$PWD" "$PWD/dist" "$HOME/Downloads")
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
  ( cd "$tmp" && "$src" --appimage-extract .DirIcon >/dev/null 2>&1 ) || true
  if [ -f "$tmp/squashfs-root/.DirIcon" ]; then
    install -Dm644 "$tmp/squashfs-root/.DirIcon" "$ICON_FILE"
    return 0
  fi
  # fallback: prova una PNG qualsiasi dentro usr/share/icons
  ( cd "$tmp" && "$src" --appimage-extract 'usr/share/icons/*' >/dev/null 2>&1 ) || true
  local png
  png="$(find "$tmp/squashfs-root" -name '*.png' 2>/dev/null | head -1 || true)"
  if [ -n "$png" ]; then
    install -Dm644 "$png" "$ICON_FILE"
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

# 2. Icona
mkdir -p "$ICON_DIR"
if extract_icon "$TARGET_APPIMAGE"; then
  msg "Icona installata in $ICON_FILE"
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
