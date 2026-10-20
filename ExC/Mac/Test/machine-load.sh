# machine-load.sh - Carico della macchina per i test (da includere con "source").
#
# Un giro con la macchina occupata (compilazioni in Xcode o Qt Creator,
# indicizzazione di clangd dopo un cambio a un header incluso ovunque, pagine
# pesanti nel browser) dura molto di piu' e conta ricarichi per il watchdog
# della GPU che non sono difetti. Il 2026-10-20 la rete e' passata da ~50 a
# ~95 minuti con lo STESSO codice, e i caricamenti parametrici multi-mesh da
# 2 a 12 secondi: a macchina libera lo stesso binario e' tornato ai tempi di
# prima. Gli script misurano il carico all'inizio e alla fine del giro.

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
