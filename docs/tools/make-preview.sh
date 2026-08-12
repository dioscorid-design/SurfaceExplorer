#!/usr/bin/env bash
#
# make-preview.sh — genera l'anteprima per docs/videos.html a partire da un
# export dell'app.
#
#   ./docs/tools/make-preview.sh <video> <nome> [secondo-iniziale] [durata]
#
#   ./docs/tools/make-preview.sh presets/renders/3-torus.mp4 3-torus 12
#
# Produce docs/media/<nome>.mp4 (clip muta, in loop sulla pagina) e
# docs/media/<nome>-poster.jpg (fermo immagine mostrato mentre carica).
#
# PERCHE' QUESTI PARAMETRI
#   1080p24, crf 26: misurato su un export reale da 30 s 1080p60 (22 MB),
#   5 s costano 0.95 MB. E' la DURATA a decidere il peso, non la risoluzione:
#   scendere a 720p ne risparmia 0.5 e butta via meta' del dettaglio, che su
#   queste superfici e' esattamente cio' che vale la pena mostrare.
#
#   -movflags +faststart sposta l'indice in testa: la riproduzione parte senza
#   scaricare tutto il file.
#
#   -an toglie l'audio: le anteprime vanno in autoplay, e un autoplay con audio
#   viene bloccato dai browser (oltre a essere sgradevole).
#
set -euo pipefail

if [ $# -lt 2 ]; then
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

SRC="$1"
NAME="$2"
START="${3:-5}"
DUR="${4:-5}"

if [ ! -f "$SRC" ]; then
    echo "Errore: '$SRC' non esiste." >&2
    exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "Errore: ffmpeg non trovato. Installalo con 'brew install ffmpeg'." >&2
    exit 1
fi

# Radice del repo, così lo script funziona da qualunque cartella.
# Lo script sta in docs/tools/, quindi la radice è due livelli sopra.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTDIR="$ROOT/docs/media"
mkdir -p "$OUTDIR"

OUT="$OUTDIR/$NAME.mp4"
POSTER="$OUTDIR/$NAME-poster.jpg"

# La durata della sorgente serve solo per un avviso: se START e' oltre la fine,
# ffmpeg produrrebbe un file vuoto senza lamentarsi in modo evidente.
if command -v ffprobe >/dev/null 2>&1; then
    TOTAL=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$SRC" 2>/dev/null || echo "")
    if [ -n "$TOTAL" ]; then
        if awk -v s="$START" -v t="$TOTAL" 'BEGIN { exit !(s >= t) }'; then
            echo "Errore: il secondo iniziale ($START s) supera la durata del video (${TOTAL%.*} s)." >&2
            exit 1
        fi
    fi
fi

echo "Sorgente : $SRC"
echo "Taglio   : da ${START}s per ${DUR}s"

ffmpeg -y -loglevel error -ss "$START" -t "$DUR" -i "$SRC" \
       -vf "scale=1920:-2,fps=24" \
       -c:v libx264 -crf 26 -preset slow \
       -movflags +faststart -an "$OUT"

# Poster dal PRIMO fotogramma della clip già tagliata: così l'immagine statica
# combacia con ciò che si vede a video fermo, invece di essere un altro istante.
ffmpeg -y -loglevel error -i "$OUT" -vframes 1 -q:v 3 "$POSTER"

SIZE=$(awk -v b="$(wc -c < "$OUT")" 'BEGIN { printf "%.2f", b/1048576 }')

echo
echo "Creati:"
echo "  docs/media/$NAME.mp4          ($SIZE MB)"
echo "  docs/media/$NAME-poster.jpg"
echo
if awk -v s="$SIZE" 'BEGIN { exit !(s > 5) }'; then
    echo "ATTENZIONE: oltre 5 MB. Accorcia la clip (--durata) o alza il crf,"
    echo "altrimenti il file resta per sempre nella storia di git."
    echo
fi
echo "Ora aggiungi il blocco in docs/videos.html: il template sta in un"
echo "commento HTML dentro la pagina. Serve anche il link YouTube del video"
echo "completo."
