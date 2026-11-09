#!/bin/bash
# =============================================================================
# reset_dmg_state.sh — riporta il canale DMG allo stato "mai aperto"
# =============================================================================
#
# A COSA SERVE
# Provare il PRIMO AVVIO della build DMG (Developer ID): la scelta della
# cartella della libreria, l'avviso sulle cartelle sincronizzate con iCloud, la
# creazione della sottocartella "presets". Senza reset l'app ritrova
# libraryRootPath e salta tutto.
#
# L'EQUIVALENTE PER LA BUILD SANDBOXED e' test_sandbox_local.sh --reset, che
# agisce su un ALTRO bundle id (com.dioscorid.surfaceexplorer) e su un altro
# container: i due canali hanno preferenze separate da 34398c0/da1aa4f, quindi
# azzerarne uno NON tocca l'altro. E' voluto, ma va ricordato.
#
# COSA CANCELLA (e perche' non basta un solo posto)
#   1. ~/Library/Preferences/com.dioscorid.SurfaceExplorer2-dmg.plist
#      Il dominio di QSettings: qui sta libraryRootPath. Il nome NON e' il
#      bundle id -- QSettings su macOS costruisce il dominio da
#      organizationName + applicationName (vedi main.cpp), non dall'id.
#   2. ~/Library/Preferences/com.dioscorid.surfaceexplorer2.dmg.plist
#      Lo scrive Cocoa (stato dei pannelli, ultima cartella dei dialoghi):
#      lasciarlo non falsa il test della libreria, ma il pannello si
#      riaprirebbe dove eri l'ultima volta invece che sulla home.
#   3. ~/Library/Containers/com.dioscorid.surfaceexplorer2.dmg/
#      Il DMG non e' sandboxed e non dovrebbe averne uno, ma un container con
#      questo id ESISTE su questa macchina: lo ha creato la build di test
#      sandbox quando ereditava l'id del DMG (corretto in 0f85329). Dentro ci
#      sono anche i security bookmark. E' un residuo, e va tolto o il prossimo
#      avvio potrebbe ripescarlo.
#   4. La cache di cfprefsd, che tiene in memoria le preferenze appena
#      cancellate e le riscriverebbe su disco alla chiusura dell'app.
#
# NON TOCCA la libreria su disco: i preset restano dove sono. Se vuoi provare
# davvero una prima installazione, sposta o rinomina anche la cartella della
# libreria -- lo script te lo ricorda ma non lo fa da se', perche' li' dentro
# c'e' il tuo lavoro.
#
# A QUALE APP SI APPLICA: a TUTTE quelle del canale DMG.
# Non azzera "un'applicazione" ma un CANALE: le preferenze cancellate qui sono
# condivise da qualunque bundle con id "...surfaceexplorer2.dmg", ovunque si
# trovi -- /Applications, build/Qt_6_12_0_for_macOS_Release,
# build/Qt_6_12_0_for_macOS_Debug. Tutti leggono lo stesso libraryRootPath, quindi
# NON serve installare il DMG per provare una modifica: basta lanciare il
# binario appena compilato con --app.
#
# USO
#   ./ExC/Mac/reset_dmg_state.sh                     azzera e basta
#   ./ExC/Mac/reset_dmg_state.sh --run               azzera e lancia /Applications
#   ./ExC/Mac/reset_dmg_state.sh --run --app build/Qt_6_12_0_for_macOS_Release/SurfaceExplorer.app
#                                                    azzera e lancia quel bundle
#   ./ExC/Mac/reset_dmg_state.sh --import            come sopra, ma il primo avvio importa
#                                                    le impostazioni della versione precedente
#                                                    (--import-1x vale ancora)
# -----------------------------------------------------------------------------
set -euo pipefail

# Identita' del canale DMG di QUESTA versione, come in CMakeLists.txt e main.cpp: la
# x.0 e' "Surface Explorer v2" (id ...surfaceexplorer2.dmg, dominio SurfaceExplorer2-dmg),
# dalla x.1 ogni minore ha la sua: la 2.1 e' "Surface Explorer v2.1" (id
# ...surfaceexplorer21.dmg, dominio SurfaceExplorer21-dmg) e sta accanto alla 2.0.
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(sed -nE 's/^project\(SurfaceExplorer[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/p' \
           "$PROJECT_DIR/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || { echo "versione non trovata in CMakeLists.txt" >&2; exit 1; }
MAJOR="${VERSION%%.*}"
MINOR="${VERSION#*.}"
if [ "$MINOR" = "0" ]; then SERIES="$MAJOR"; APP_LABEL="v$MAJOR"; IMPORT_KEY="settings1xImportDone"
else                        SERIES="$MAJOR$MINOR"; APP_LABEL="v$VERSION"; IMPORT_KEY="settingsImportDone"; fi
BUNDLE_ID="com.dioscorid.surfaceexplorer$SERIES.dmg"
# Dominio QSettings: NON coincide col bundle id. Vedi il commento sopra.
QT_DOMAIN="com.dioscorid.SurfaceExplorer$SERIES-dmg"
APP="/Applications/Surface Explorer $APP_LABEL.app"

info() { printf '\033[36m•\033[0m %s\n' "$1"; }
ok()   { printf '\033[32m✓\033[0m %s\n' "$1"; }
warn() { printf '\033[33m!\033[0m %s\n' "$1"; }
err()  { printf '\033[31m✗ %s\033[0m\n' "$1" >&2; exit 1; }

DO_RUN=0
IMPORT_1X=0
while [ $# -gt 0 ]; do
  case "$1" in
    --run) DO_RUN=1 ;;
    --import|--import-1x) IMPORT_1X=1 ;;
    # Percorso di un .app da lanciare al posto di quello in /Applications: le
    # preferenze sono le stesse per ogni bundle con id "...dmg", quindi si puo'
    # provare il binario appena compilato senza installarlo.
    --app) shift; [ -n "${1:-}" ] || err "--app richiede un percorso"; APP="$1" ;;
    *) err "argomento sconosciuto: $1 (usa --run, --import, --app <percorso.app>)" ;;
  esac
  shift
done

# Prima di cancellare: con l'app assente le impostazioni sparirebbero senza la prova.
if [ "$DO_RUN" -eq 1 ]; then
  [ -d "$APP" ] || err "App non trovata: $APP"
fi

# L'app DEVE essere chiusa: se gira, cfprefsd riscrive le preferenze alla
# chiusura e il reset viene annullato senza che si veda nulla.
#
# Si cercano TUTTI i binari del canale, non solo quello in /Applications:
# queste preferenze sono condivise da qualunque bundle con id "...dmg" --
# /Applications, build/Qt_6_12_0_for_macOS_Release, build/Qt_6_12_0_for_macOS_Debug --
# quindi il reset si applica a tutti e tre e uno qualsiasi di essi, se in
# esecuzione, puo' annullarlo. Guardare solo /Applications lasciava passare
# proprio il caso di chi prova il binario appena compilato.
RUNNING="$(pgrep -fl "\.app/Contents/MacOS/SurfaceExplorer" 2>/dev/null || true)"
if [ -n "$RUNNING" ]; then
  printf '\033[31m✗ Surface Explorer e in esecuzione:\033[0m\n%s\n' "$RUNNING" >&2
  err "Chiudila prima: alla chiusura cfprefsd riscriverebbe le preferenze e il reset sarebbe annullato."
fi

info "Radice attuale della libreria:"
defaults read "$QT_DOMAIN" libraryRootPath 2>/dev/null || echo "   (nessuna: risulta gia' mai aperta)"

# 1+2. Domini delle preferenze.
for d in "$QT_DOMAIN" "$BUNDLE_ID"; do
  if defaults read "$d" >/dev/null 2>&1; then
    defaults delete "$d" 2>/dev/null || true
    ok "Dominio azzerato: $d"
  else
    info "Dominio gia' assente: $d"
  fi
  rm -f "$HOME/Library/Preferences/$d.plist" 2>/dev/null || true
done

# 3. Container residuo. Si svuota Data/ invece di cancellare la cartella: dentro
#    c'e' .com.apple.containermanagerd.metadata.plist, di proprieta' del sistema
#    e non rimovibile -- un rm -rf fallirebbe e con `set -e` abortirebbe lo
#    script (stessa trappola gia' documentata in test_sandbox_local.sh).
CONTAINER="$HOME/Library/Containers/$BUNDLE_ID"
if [ -d "$CONTAINER/Data" ]; then
  find "$CONTAINER/Data" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
  ok "Container svuotato: $CONTAINER/Data"
elif [ -d "$CONTAINER" ]; then
  info "Container presente ma senza Data/."
else
  info "Nessun container da svuotare."
fi

# 4. cfprefsd tiene le preferenze in cache: senza questo, il valore appena
#    cancellato puo' tornare da solo.
killall -u "$USER" cfprefsd 2>/dev/null || true
ok "Cache cfprefsd svuotata."

# 5. Con la versione precedente sullo stesso Mac (la 1.x per la 2.0; la 2.0, o la
#    1.x, per la 2.1) il primo avvio ne importerebbe le impostazioni (main.cpp),
#    libraryRootPath compresa, e il primo avvio vero non si vedrebbe mai.
if [ "$IMPORT_1X" -eq 0 ]; then
  defaults write "$QT_DOMAIN" "$IMPORT_KEY" -bool true
  info "Import dalla versione precedente saltato (--import per provarlo)."
fi

echo
ok "Il canale DMG risulta ora MAI APERTO."
warn "La libreria su disco NON e' stata toccata: se vuoi provare una vera prima"
warn "installazione, sposta o rinomina anche la cartella dei preset."
echo

if [ "$DO_RUN" -eq 1 ]; then
  info "Avvio $APP ..."
  # Data del binario: il test piu' frequente e' su una modifica appena fatta, e
  # firmare un bundle NON lo ricompila (release_macos.sh firma cio' che trova).
  # Vedere qui una data vecchia evita di attribuire al codice un comportamento
  # che viene semplicemente da un binario precedente -- e' gia' successo.
  info "Binario del $(stat -f '%Sm' "$APP/Contents/MacOS/SurfaceExplorer")"
  open "$APP"
fi
