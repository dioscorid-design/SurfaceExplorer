#!/usr/bin/env bash
#
# make-preview.sh — builds the preview clip for docs/videos.html out of an
# export produced by the application.
#
#   ./docs/tools/make-preview.sh <video> <name> [start-second] [duration]
#
#   ./docs/tools/make-preview.sh presets/renders/3-torus.mp4 3-torus 12
#
# Writes docs/media/<name>.mp4 (silent clip, looped on the page) and
# docs/media/<name>-poster.jpg (still frame shown while it loads).
#
# Remember: the files must be COMMITTED to appear on the site. GitHub Pages
# serves only what is in the repository — see docs/tools/README.md.
#
# WHY THESE SETTINGS
#   1080p24, crf 26: measured on a real 30 s 1080p60 export (22 MB), 5 seconds
#   cost 0.95 MB. DURATION drives the size, not resolution: dropping to 720p
#   saves half a megabyte and throws away half the detail, which on these
#   surfaces is exactly what is worth showing.
#
#   -movflags +faststart moves the index to the front: playback starts without
#   downloading the whole file.
#
#   -an strips the audio: previews autoplay, and browsers block autoplay with
#   sound (besides being unpleasant).
#
set -euo pipefail

# Usage text = the header block above, from the line after the shebang down to
# the last comment line before `set -euo pipefail`. Computed rather than
# hardcoded as a line range, which silently drifts whenever the header is
# edited.
usage() {
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
}

if [ $# -lt 2 ]; then
    usage
    exit 1
fi

SRC="$1"
NAME="$2"
START="${3:-5}"
DUR="${4:-5}"

if [ ! -f "$SRC" ]; then
    echo "Error: '$SRC' does not exist." >&2
    exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "Error: ffmpeg not found. Install it with 'brew install ffmpeg'." >&2
    exit 1
fi

# Repository root, so the script works from any directory.
# It lives in docs/tools/, so the root is two levels up.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTDIR="$ROOT/docs/media"
mkdir -p "$OUTDIR"

OUT="$OUTDIR/$NAME.mp4"
POSTER="$OUTDIR/$NAME-poster.jpg"

# The source duration is only needed for a warning: if START is past the end,
# ffmpeg would produce an empty file without complaining in any obvious way.
if command -v ffprobe >/dev/null 2>&1; then
    TOTAL=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$SRC" 2>/dev/null || echo "")
    if [ -n "$TOTAL" ]; then
        if awk -v s="$START" -v t="$TOTAL" 'BEGIN { exit !(s >= t) }'; then
            echo "Error: start second ($START s) is past the end of the video (${TOTAL%.*} s)." >&2
            exit 1
        fi
    fi
fi

echo "Source : $SRC"
echo "Trim   : from ${START}s for ${DUR}s"

ffmpeg -y -loglevel error -ss "$START" -t "$DUR" -i "$SRC" \
       -vf "scale=1920:-2,fps=24" \
       -c:v libx264 -crf 26 -preset slow \
       -movflags +faststart -an "$OUT"

# Poster taken from the FIRST frame of the already-trimmed clip, so the still
# matches what is on screen when the video is paused at the start, rather than
# being some other moment.
ffmpeg -y -loglevel error -i "$OUT" -vframes 1 -q:v 3 "$POSTER"

SIZE=$(awk -v b="$(wc -c < "$OUT")" 'BEGIN { printf "%.2f", b/1048576 }')

echo
echo "Created:"
echo "  docs/media/$NAME.mp4          ($SIZE MB)"
echo "  docs/media/$NAME-poster.jpg"
echo
if awk -v s="$SIZE" 'BEGIN { exit !(s > 5) }'; then
    echo "WARNING: over 5 MB. Shorten the clip (duration argument) or raise the"
    echo "crf, otherwise the file stays in git history forever."
    echo
fi
echo "Next: add the block to docs/videos.html — the template is in an HTML"
echo "comment inside the page — then git add the two files above together with"
echo "the page, and push. Uncommitted media gives a 404 on the site."
