# machine-load.sh - Stato della macchina per i test (da includere con "source").
#
# DUE cose rallentano un giro senza che il codice c'entri (2026-10-20, rete
# passata da ~50 a ~95 minuti con lo STESSO binario, 19 ricarichi per il
# watchdog della GPU):
#  1. la macchina INATTIVA: lo schermo si spegne dopo un minuto e, senza
#     nessuno davanti, ogni caricamento che aspetta dei frame va a singhiozzo
#     e il watchdog scambia i frame lenti per un collasso della GPU. Misurato
#     sul campione di 52 preset: 99 s a schermo spento, 64 s tenendolo sveglio
#     (= i tempi del 10-18, quando lo schermo era rimasto acceso). Rimedio:
#     keep_machine_awake, un'asserzione di caffeinate che dura quanto lo script
#     e non cambia le impostazioni di risparmio energetico;
#  2. la macchina CARICA (compilazioni in Xcode o Qt Creator, clangd che
#     reindicizza dopo un cambio a un header incluso ovunque): il carico si
#     stampa all'inizio e alla fine del giro.

# Soglia: i core di prestazione (sugli Apple Silicon il test gira li').
LOAD_LIMIT="$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || sysctl -n hw.ncpu)"

# Carico medio dell'ultimo minuto.
machine_load() { sysctl -n vm.loadavg | awk '{ print $2 }'; }

# Stampa il carico; sopra la soglia avvisa e mostra i processi piu' attivi.
#   check_machine_load inizio|fine
check_machine_load() {
    local load
    load="$(machine_load)"
    if awk -v l="$load" -v m="$LOAD_LIMIT" 'BEGIN { exit !(l > m) }'; then
        printf 'ATTENZIONE (%s): macchina carica, load %s con %s core di prestazione.\n' "$1" "$load" "$LOAD_LIMIT"
        printf 'Tempi e ricarichi per watchdog di questo giro non sono confrontabili. Piu'"'"' attivi:\n'
        ps -Ao pcpu=,comm= -r | head -5 | sed -E 's|^ *([0-9.]+) +(.*/)?([^/]+)$|  \1%  \3|'
    else
        printf 'Carico della macchina (%s): %s (soglia %s)\n' "$1" "$load" "$LOAD_LIMIT"
    fi
}

# Aspetta (al piu' $1 secondi) che il carico scenda sotto la soglia: serve
# dopo un lavoro dello script stesso, come la build di riferimento.
wait_machine_load() {
    local waited=0
    while awk -v l="$(machine_load)" -v m="$LOAD_LIMIT" 'BEGIN { exit !(l > m) }' \
          && [ "$waited" -lt "$1" ]; do
        sleep 5
        waited=$((waited + 5))
    done
}

# Accende lo schermo se e' spento e tiene sveglia la macchina (schermo e
# sistema) finche' lo script che la chiama e' vivo.
keep_machine_awake() {
    caffeinate -u -t 2 >/dev/null 2>&1 || true
    caffeinate -d -i -w $$ >/dev/null 2>&1 &
}
