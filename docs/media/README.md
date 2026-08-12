# docs/media

Immagini e video pubblicati sul sito (https://dioscorid-design.github.io/SurfaceExplorer/).
Referenziati da `gallery.html` e `videos.html` come `media/<nome>`.

## Immagini

PNG per superfici a tinte piatte e wireframe, JPEG (qualità ~85) per gradienti e
texture. Larghezza 1600 px è già abbondante: la griglia non le mostra mai più
grandi. Tenerle sotto ~1 MB l'una.

## Video

Un export 1080p60 dell'app è troppo pesante da committare (un 30 s sta sui 22 MB).
Va ricompresso prima:

    ffmpeg -i input.mp4 -vf "scale=1280:-2" -c:v libx264 -crf 26 \
           -preset slow -movflags +faststart -an output.mp4

`-movflags +faststart` sposta l'indice in testa al file, così la riproduzione
parte senza scaricare tutto; `-an` toglie l'audio (i render non ne hanno).
Con `-crf` più alto il file è più piccolo: 26 è un buon compromesso, 30 comincia
a mostrare artefatti sui gradienti.

Poster frame, mostrato mentre il video carica:

    ffmpeg -i output.mp4 -ss 3 -vframes 1 -q:v 3 output-poster.jpg

Tenere ogni clip sotto ~5 MB. Per video lunghi conviene YouTube o un asset di
release: GitHub Pages serve il file intero, non fa streaming.

## Limiti

Sito 1 GB, singolo file 100 MB, banda 100 GB/mese. I file committati restano
nella storia di git anche se cancellati, quindi conviene comprimere PRIMA di
aggiungerli.
