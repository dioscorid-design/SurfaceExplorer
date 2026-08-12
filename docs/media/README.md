# docs/media

Immagini e video pubblicati sul sito (https://dioscorid-design.github.io/SurfaceExplorer/).
Referenziati da `gallery.html` e `videos.html` come `media/<nome>`.

## Video: anteprima qui, versione completa su YouTube

La pagina video non ospita i render interi. Ogni voce e' una **clip muta di ~5
secondi che va in loop**, piu' un pulsante al video completo su YouTube. Cosi' il
sito resta leggero e la versione lunga in alta risoluzione non pesa sul
repository.

Per generare l'anteprima:

    ./docs/tools/make-preview.sh presets/renders/3-torus.mp4 3-torus 12

Argomenti: file sorgente, nome di uscita, secondo iniziale (default 5) e durata
(default 5). Produce `<nome>.mp4` e `<nome>-poster.jpg` qui dentro. Poi si copia
il blocco HTML dal commento dentro `videos.html` e si mette il link YouTube.

A mano:

    ffmpeg -ss 12 -t 5 -i input.mp4 -vf "scale=1920:-2,fps=24" \
           -c:v libx264 -crf 26 -preset slow -movflags +faststart \
           -an docs/media/nome.mp4

### Pesi misurati

Su un export reale dell'app da 30 s a 1080p60 (22 MB), tagliando 5 secondi:

| formato | peso |
|---|---|
| 1080p24 crf 26 | **0.95 MB** (consigliato) |
| 1080p24 crf 30 | 0.63 MB |
| 720p24 crf 26 | 0.41 MB |
| 1080p30 crf 26 | 1.02 MB |

E' la **durata** a determinare il peso, non la risoluzione: scendere a 720p
risparmia mezzo mega e butta via meta' del dettaglio geometrico, che su queste
superfici e' proprio cio' che vale la pena mostrare. Conviene restare a 1080p e
semmai accorciare.

Con questi numeri dieci anteprime stanno in ~10 MB.

### Dettagli che contano

- `-movflags +faststart` mette l'indice in testa al file: parte subito, senza
  scaricare tutto.
- `-an` toglie l'audio. Le anteprime sono in autoplay, e un autoplay con audio
  viene bloccato dai browser.
- Nel markup servono `loop muted autoplay playsinline`: senza `muted` non parte,
  senza `playsinline` iOS apre il player a schermo intero.

## Immagini

PNG per superfici a tinte piatte e wireframe, JPEG (qualita' ~85) per gradienti e
texture. Larghezza 1600 px e' gia' abbondante: la griglia non le mostra mai piu'
grandi. Tenerle sotto ~1 MB l'una.

## Limiti

Sito 1 GB, singolo file 100 MB, banda 100 GB/mese. I file committati restano
nella storia di git anche se poi cancellati, quindi comprimere **prima** di
aggiungerli.
