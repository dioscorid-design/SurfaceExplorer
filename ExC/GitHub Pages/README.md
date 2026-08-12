# ExC/GitHub Pages

Strumenti per il sito https://dioscorid-design.github.io/SurfaceExplorer/

Qui stanno solo gli **script**. Il sito vero e' `docs/`, nella radice del repo.

## make-preview.sh

Genera l'anteprima video per `docs/videos.html`:

    ./"ExC/GitHub Pages/make-preview.sh" presets/renders/3-torus.mp4 3-torus 12

Argomenti: file sorgente, nome di uscita, secondo iniziale (default 5), durata
(default 5). Scrive `docs/media/<nome>.mp4` e `docs/media/<nome>-poster.jpg`.

Taglia la clip, la porta a 1080p24 con crf 26, toglie l'audio ed estrae il
poster. Non tocca le pagine ne' git: il blocco HTML va aggiunto a mano
(il template sta in un commento dentro `videos.html`).

## split_doc.py

**Non piu' eseguibile.** Generava i 16 `doc_*.html` piu' l'indice a partire da
un `documentation.html` monolitico con `<section id="...">`. Quel sorgente non
esiste piu' — lo script ha sovrascritto il proprio input — e non e' mai stato
committato. Oggi si ferma con "sezione mancante: intro".

Conservato come documentazione del formato e dei vincoli delle pagine. Le
pagine si modificano a mano.

## Perche' docs/media/ non sta qui

GitHub Pages e' configurato su branch `v1`, cartella `/docs`: **tutto e solo
quello che sta in `docs/` viene pubblicato**. Le immagini e i video devono
stare li' dentro o il browser non li trova (404) e le pagine si rompono.

Quindi la divisione e':

    docs/                  il SITO (pagine + media serviti al browser)
    ExC/GitHub Pages/      gli STRUMENTI che lo producono (non pubblicati)

I sorgenti pesanti da cui si ricavano le anteprime (`presets/renders/`) restano
fuori da entrambe: non sono committati, e non devono esserlo — un export
1080p60 di 30 s pesa 22 MB e resterebbe per sempre nella storia di git.
