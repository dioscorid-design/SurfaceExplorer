#!/usr/bin/env bash
#
# scenario-test.sh - Test degli scenari d'uso (vedi scenariotest.h).
#
# Apre l'app (build Debug), carica due record della libreria C/presets e preme
# i controlli veri come farebbe l'utente -- checkbox Texture, Base/Phong/
# Wireframe, Surface/Background -- verificando dopo ogni gesto che le copie
# dello stato della scena (intenzione, motore, checkbox, cio' che scriverebbe
# il Save) restino coerenti. Dura circa 15 secondi.
#
# Quando lanciarlo: dopo ogni modifica alla gestione della texture o dei
# controlli di resa, e prima di un rilascio. E' la rete dei GESTI: il test dei
# preset copre load e save, non i clic.
#
# Uso:
#   ./ExC/Mac/scenario-test.sh
#   PRESETS=/altra/libreria ./ExC/Mac/scenario-test.sh
#
# Report in build/test-reports/scenario-<data>/scenario-report.txt, stampato alla fine.

set -euo pipefail

# --- root del progetto: lo script vive in ExC/Mac/, la root e' due cartelle sopra ---
PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$PROJECT_DIR/build/Desktop_Qt_6_10-Debug/SurfaceExplorer.app/Contents/MacOS/SurfaceExplorer}"
# La libreria su cui si lavora e' C/presets, accanto al repo (NON quella dentro il repo).
PRESETS="${PRESETS:-$(cd "$PROJECT_DIR/.." && pwd)/presets}"
REPORTS="$PROJECT_DIR/build/test-reports"

err() { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

[ -x "$BIN" ] || err "binario non trovato: $BIN
Compila la build Debug (Qt Creator, oppure:
  ~/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/Desktop_Qt_6_10-Debug -j8)"
[ -d "$PRESETS/records" ] || err "libreria non trovata o senza records/: $PRESETS"

# Build stantia: un binario piu' vecchio dei sorgenti prova il codice di prima.
STALE="$(find "$PROJECT_DIR" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' -o -name '*.ui' \) -newer "$BIN" | head -3)"
if [ -n "$STALE" ]; then
    printf 'ATTENZIONE: il binario e'"'"' piu'"'"' vecchio di questi sorgenti -- ricompila prima:\n%s\n\n' "$STALE"
fi

OUT="$REPORTS/scenario-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

echo "Libreria: $PRESETS"
echo "Test in corso (l'app si apre da sola e si chiude alla fine, ~15 s)..."

RC=0
"$BIN" --scenario-test "$PRESETS" "$OUT" > "$OUT/app.log" 2>&1 || RC=$?

[ -f "$OUT/scenario-report.txt" ] || err "il test non ha prodotto il report (codice $RC): vedi $OUT/app.log"

echo
cat "$OUT/scenario-report.txt"
echo
case "$RC" in
    0) echo "ESITO: tutte le verifiche passate." ;;
    1) echo "ESITO: qualche verifica FALLITA -- vedi le righe FALLITO qui sopra." ;;
    *) echo "ESITO: il test non e' partito correttamente (codice $RC): vedi $OUT/app.log" ;;
esac
exit "$RC"
