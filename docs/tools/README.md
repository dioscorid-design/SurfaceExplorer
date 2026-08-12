# docs/tools

Strumenti per il sito https://dioscorid-design.github.io/SurfaceExplorer/

Qui stanno solo gli **script**. Il sito vero e' `docs/`, nella radice del repo.

## make-preview.sh

Genera l'anteprima video per `docs/videos.html`:

    ./docs/tools/make-preview.sh presets/renders/3-torus.mp4 3-torus 12

Argomenti: file sorgente, nome di uscita, secondo iniziale (default 5), durata
(default 5). Scrive `docs/media/<nome>.mp4` e `docs/media/<nome>-poster.jpg`.

Taglia la clip, la porta a 1080p24 con crf 26, toglie l'audio ed estrae il
poster. Non tocca le pagine ne' git: il blocco HTML va aggiunto a mano
(il template sta in un commento dentro `videos.html`).

## Le pagine del manuale si modificano a mano

C'era uno `split_doc.py` che generava i 16 `doc_*.html` piu' l'indice da un
`documentation.html` monolitico con `<section id="...">`. **Rimosso nel commit
che aggiunge questa riga**: aveva sovrascritto il proprio sorgente con l'indice
generato, il monolite non era mai stato committato, e rilanciarlo si fermava con
"sezione mancante: intro". Restava solo una trappola per chi ci fosse ricascato.

Se un giorno servisse rigenerarle, va prima ricostruito il monolite
concatenando i `doc_*.html` dentro `<section id="...">`; lo script e'
recuperabile con `git show 78026aa:"ExC/GitHub Pages/split_doc.py"`.

Vincoli da rispettare modificando le pagine: sono servite sia dal sito sia,
come risorse qrc, dal manuale dentro l'app, dove il visualizzatore e' un
`QTextBrowser`. Quel motore ignora gran parte del CSS (niente flexbox, niente
`border-left`): la gerarchia visiva usa bande realizzate con tabelle `bgcolor`,
`<hr>` e `font-size` inline. Vedi il commento in `mainwindow.cpp:1129`.

L'elenco ordinato dei capitoli e' in `CMakeLists.txt` (`_doc_chapters`) e in
`update_resources.py` (`DOC_CHAPTERS`), entrambi con una guardia che fa fallire
il build se una pagina manca dal `.qrc`.

## Struttura

    docs/            il SITO pubblicato
      *.html         le pagine
      media/         immagini e anteprime video servite al browser
      tools/         questi script

GitHub Pages e' configurato su branch `v1`, cartella `/docs`, e in modalita'
"deploy from a branch" accetta **solo** `/` o `/docs`: nessun altro percorso e'
ammesso (l'API risponde *"Must be one of the following: /, /docs"*). Per questo
`docs/` non si puo' rinominare e `media/` non si puo' spostare fuori: il sito
smetterebbe di essere pubblicato.

Conseguenza di stare dentro `docs/`: **anche questa cartella viene pubblicata**.
`https://.../SurfaceExplorer/tools/make-preview.sh` e' raggiungibile. Non e' un
problema — e' uno script gia' pubblico nel repo, senza credenziali — ma va
saputo: qui dentro non vanno chiavi ne' file privati.

I sorgenti pesanti da cui si ricavano le anteprime (`presets/renders/`) restano
fuori: non sono committati, e non devono esserlo — un export 1080p60 di 30 s
pesa 22 MB e resterebbe per sempre nella storia di git.
