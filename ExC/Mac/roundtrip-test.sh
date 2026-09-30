#!/usr/bin/env bash
#
# roundtrip-test.sh - Test di andata e ritorno dei preset (vedi presetroundtrip.h).
#
# Apre l'app (build Debug), carica da sola ogni superficie e ogni record della
# libreria due volte -- in ordine e al contrario -- e confronta cio' che
# scriverebbe un Save col file, e fra i due passaggi (= stato rimasto dal preset
# caricato prima). Non scrive nulla nella libreria.
# Dura circa 12 minuti: nel frattempo non usare l'app.
#
# Quando lanciarlo:
#   - prima di un rilascio (beta, App Store);
#   - dopo modifiche a load/save o quando si aggiunge uno stato alla scena;
#   - prima e dopo ogni tappa del refactoring "stato unico della scena";
#   - dopo aver sistemato molti preset.
#
# Uso:
#   ./ExC/Mac/roundtrip-test.sh                          tutta la libreria C/presets
#   ./ExC/Mac/roundtrip-test.sh --filter "Paths|Kerr"    solo i preset che combaciano
#   ./ExC/Mac/roundtrip-test.sh --single-pass            un solo passaggio (meta' tempo,
#                                                        niente controllo dell'ordine)
#   PRESETS=/altra/libreria ./ExC/Mac/roundtrip-test.sh
#
# Report in build/test-reports/roundtrip-<data>/report.txt, aperto alla fine.
# Come leggerlo: "Dipendono dal preset caricato prima" e "Con un popup di errore"
# devono restare a 0; "Il Save cambia o perde qualcosa" oggi non e' 0 (residui
# nei dati che il Save ripulisce): si confronta col report precedente, il cui
# riepilogo viene stampato accanto a quello nuovo.

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
[ -d "$PRESETS/surfaces" ] || err "libreria non trovata o senza surfaces/: $PRESETS"

# Build stantia: un binario piu' vecchio dei sorgenti prova il codice di prima.
STALE="$(find "$PROJECT_DIR" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' -o -name '*.ui' \) -newer "$BIN" | head -3)"
if [ -n "$STALE" ]; then
    printf 'ATTENZIONE: il binario e'"'"' piu'"'"' vecchio di questi sorgenti -- ricompila prima:\n%s\n\n' "$STALE"
fi

OUT="$REPORTS/roundtrip-$(date +%Y%m%d-%H%M%S)"
PREV="$(ls -td "$REPORTS"/roundtrip-* 2>/dev/null | head -1 || true)"
mkdir -p "$OUT"

echo "Libreria: $PRESETS"
echo "Report:   $OUT"
echo "Test in corso (l'app si apre da sola e si chiude alla fine)..."

"$BIN" --roundtrip-test "$PRESETS" "$OUT" "$@" > "$OUT/app.log" 2>&1 &
PID=$!
# Avanzamento ogni 20 s, dall'ultima riga di progress.log.
while kill -0 "$PID" 2>/dev/null; do
    sleep 20
    [ -f "$OUT/progress.log" ] && printf '  %s\n' "$(tail -1 "$OUT/progress.log" | cut -c1-110)"
done
RC=0
wait "$PID" || RC=$?

[ -f "$OUT/report.txt" ] || err "il test non ha prodotto il report (codice $RC): vedi $OUT/app.log"

echo
echo "== Riepilogo =="
sed -n '3p;5,10p' "$OUT/report.txt"
if [ -n "$PREV" ] && [ -f "$PREV/report.txt" ]; then
    echo
    echo "== Riepilogo del report precedente ($(basename "$PREV")) =="
    sed -n '3p;5,10p' "$PREV/report.txt"
fi
echo
case "$RC" in
    0) echo "ESITO: tutto identico, nessuna dipendenza dall'ordine, nessun popup di errore." ;;
    1) echo "ESITO: ci sono differenze -- confronta col report precedente (sopra)." ;;
    *) echo "ESITO: il test non e' partito correttamente (codice $RC): vedi $OUT/app.log" ;;
esac
open "$OUT/report.txt" 2>/dev/null || true
