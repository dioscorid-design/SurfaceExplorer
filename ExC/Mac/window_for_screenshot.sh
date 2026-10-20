#!/usr/bin/env bash
#
# window_for_screenshot.sh - Dimensiona la finestra per gli screenshot Mac App Store.
#
# Uso:
#   ./ExC/Mac/window_for_screenshot.sh            # 1440x900 punti (default)
#   ./ExC/Mac/window_for_screenshot.sh 1280 800
#
# 1. Apri SurfaceExplorer, ESCI dal tutto schermo.
# 2. Lancia questo script: sposta la finestra sul 4K E la ridimensiona.
#    NON spostarla piu' a mano dopo: trascinandola macOS la rimpicciolisce.
# 3. Cattura: Cmd+Shift+4, BARRA SPAZIATRICE, clic sulla finestra.
#
# Monitor diverso dal 4K: ORIGIN_X=0 ORIGIN_Y=60 ./window_for_screenshot.sh
#
# Una volta sola, prima della prima cattura:
#   defaults write com.apple.screencapture disable-shadow -bool true
#   killall SystemUIServer
#
# Verifica: sips -g pixelWidth -g pixelHeight <file>   -> deve dare 2880x1800.

set -euo pipefail

W="${1:-1440}"
H="${2:-900}"
APP="SurfaceExplorer"

# Angolo in alto a sinistra del monitor su cui portare la finestra, in PUNTI.
# Su questa macchina il 4K (2x) sta a destra del Full HD (principale, 1920 punti
# di larghezza), quindi le sue x partono da 1920. Con un solo monitor, o con una
# disposizione diversa, cambia questi due valori (o passa 0 0 per il principale).
ORIGIN_X="${ORIGIN_X:-1940}"
ORIGIN_Y="${ORIGIN_Y:-60}"

running=$(osascript -e "tell application \"System Events\" to (name of processes) contains \"$APP\"")
if [ "$running" != "true" ]; then
  echo "ERRORE: $APP non e' in esecuzione. Aprila e rilancia." >&2
  exit 1
fi

osascript \
  -e "tell application \"System Events\" to tell process \"$APP\"" \
  -e '  set frontmost to true' \
  -e '  if (value of attribute "AXFullScreen" of window 1) is true then' \
  -e '    set value of attribute "AXFullScreen" of window 1 to false' \
  -e '    delay 1.5' \
  -e '  end if' \
  -e "  set position of window 1 to {$ORIGIN_X, $ORIGIN_Y}" \
  -e '  delay 0.3' \
  -e "  set size of window 1 to {$W, $H}" \
  -e '  delay 0.3' \
  -e "  set position of window 1 to {$ORIGIN_X, $ORIGIN_Y}" \
  -e "  set size of window 1 to {$W, $H}" \
  -e 'end tell' >/dev/null

got=$(osascript -e "tell application \"System Events\" to tell process \"$APP\" to get size of window 1" | tr -d ' ')

if [ "$got" != "$W,$H" ]; then
  echo "ERRORE: la finestra e' ${got} invece di ${W},${H}." >&2
  echo "Esci dal tutto schermo, oppure prova 1280 800." >&2
  echo "Se il monitor non e' quello giusto: ORIGIN_X=<x> $0 $W $H" >&2
  exit 1
fi

pos=$(osascript -e "tell application \"System Events\" to tell process \"$APP\" to get position of window 1" | tr -d ' ')

echo "Finestra: ${W}x${H} punti, posizione ${pos}."
echo "NON spostarla a mano: trascinandola macOS la rimpicciolisce."
echo "Cattura:  Cmd+Shift+4  ->  barra spaziatrice  ->  clic sulla finestra"
echo "Sul monitor 4K (2x) ottieni $((W*2))x$((H*2)) pixel."
