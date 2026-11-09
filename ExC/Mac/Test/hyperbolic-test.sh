#!/usr/bin/env bash
#
# hyperbolic-test.sh - Test del nucleo matematico di H3 (hyperbolicspace.h).
#
# Compila e lancia build/Tests/hyperbolic-test: un programma a se', senza
# finestra ne' GPU, che confronta il nucleo con le formule chiuse della
# geometria iperbolica (distanze, angoli dei triangoli, rotazione del giro di
# un triangolo, angolo di parallelismo), con lo sfondo Poincare' Ball e con la
# deriva della matrice della camera. Dura meno di un secondo; compila solo il
# suo bersaglio, non l'app.
#
# Quando lanciarlo: dopo ogni modifica a hyperbolicspace.*.
#
# Uso:
#   ./ExC/Mac/Test/hyperbolic-test.sh
#
# Report in build/test-reports/hyperbolic-<data>/hyperbolic-report.txt, stampato alla fine.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="$PROJECT_DIR/build/Tests/hyperbolic-test"
OUT="$PROJECT_DIR/build/test-reports/hyperbolic-$(date +%Y%m%d-%H%M%S)"

# Con bash: nel repo gli script sono 644 (core.fileMode=false), senza bit x.
TARGET=hyperbolic-test bash "$(dirname "$0")/build-tests.sh"
mkdir -p "$OUT"

RC=0
"$BIN" > "$OUT/hyperbolic-report.txt" 2>&1 || RC=$?
cat "$OUT/hyperbolic-report.txt"
echo
case "$RC" in
    0) echo "ESITO: tutte le verifiche passate." ;;
    1) echo "ESITO: qualche verifica FALLITA -- vedi le righe FALLITO qui sopra." ;;
    *) echo "ESITO: il test non e' partito correttamente (codice $RC)." ;;
esac
exit "$RC"
