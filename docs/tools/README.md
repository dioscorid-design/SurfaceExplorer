# docs/tools

Strumenti per il sito https://dioscorid-design.github.io/SurfaceExplorer/

---

## Come si pubblica (leggere prima di tutto)

**Il sito E' il repository.** GitHub Pages serve solo file **committati e
pushati**: non esiste un pannello dove caricarli. Un file che sta solo sul disco
non esiste per il sito — la pagina lo cerca e riceve 404.

Quindi ogni pubblicazione ha sempre questi quattro passi:

    1. il file va in docs/media/
    2. il blocco HTML va nella pagina (docs/gallery.html o docs/videos.html)
    3. git add del FILE **e** della PAGINA        <-- il passo che si dimentica
    4. git commit && git push

Saltare il passo 3 e' l'errore tipico: la pagina online mostra un'immagine rotta
o un video che non parte, mentre in locale sembra tutto a posto.

Dopo il push il sito si ricostruisce da solo (~1 minuto). Verifica:

    curl -o /dev/null -w "%{http_code}\n" https://dioscorid-design.github.io/SurfaceExplorer/media/NOME.mp4

### Aggiungere un'immagine

    1. salvala in docs/media/ (PNG per tinte piatte e wireframe, JPEG q~85 per
       gradienti e texture; larghezza 1600 px basta, sotto ~1 MB)
    2. in docs/gallery.html copia il blocco <figure> dal template (sta in un
       commento HTML in cima), cambia nome file e didascalia
    3. git add docs/media/NOME.png docs/gallery.html
    4. git commit && git push

### Aggiungere un video

    1. ./docs/tools/make-preview.sh <sorgente> <nome> <secondo-iniziale>
       -> scrive docs/media/<nome>.mp4 e <nome>-poster.jpg
    2. carica il video INTERO su YouTube e copiane il link
    3. in docs/videos.html scommenta il blocco template, metti nome e link
    4. git add docs/media/<nome>.mp4 docs/media/<nome>-poster.jpg docs/videos.html
    5. git commit && git push

### Cosa si committa e cosa no

Il criterio e' il **peso**, non il tipo di file: cio' che entra nella storia di
git non ne esce piu' — resta anche se lo cancelli, e chi clona se lo scarica.

| file | in repo? |
|---|---|
| screenshot PNG/JPEG (~0.5 MB) | si', e' il suo scopo |
| anteprima 1080p24 di 5 s (~1 MB) | si' |
| export originale 1080p60 (22 MB) | **mai** — resta in `presets/renders/`, non tracciato |
| video completo | no — va su YouTube, linkato dalla pagina |

---

## make-preview.sh

Genera l'anteprima video per `docs/videos.html`:

    ./docs/tools/make-preview.sh presets/renders/3-torus.mp4 3-torus 12

Argomenti: file sorgente, nome di uscita, secondo iniziale (default 5), durata
(default 5). Scrive `docs/media/<nome>.mp4` e `docs/media/<nome>-poster.jpg`.

Taglia la clip, la porta a 1080p24 con crf 26, toglie l'audio ed estrae il
poster. Non tocca le pagine ne' git: il blocco HTML e i `git add` restano da
fare a mano (vedi sopra).

Pesi misurati su un export reale da 30 s 1080p60 (22 MB), tagliando 5 secondi:
1080p24 crf 26 = 0.95 MB, crf 30 = 0.63 MB, 720p24 = 0.41 MB. E' la **durata** a
decidere il peso, non la risoluzione: conviene restare a 1080p e semmai
accorciare.

---

## Modificare le pagine del manuale

Si modificano **a mano**, una per una: non c'e' piu' un generatore.

Sono servite sia dal sito sia, come risorse qrc, dal manuale dentro l'app, dove
il visualizzatore e' un `QTextBrowser`. Quel motore ignora gran parte del CSS
(niente flexbox, niente `border-left`): la gerarchia visiva usa bande realizzate
con tabelle `bgcolor`, `<hr>` e `font-size` inline. Vedi il commento in
`mainwindow.cpp:1129`.

Aggiungendo o togliendo un capitolo vanno aggiornati anche `CMakeLists.txt`
(`_doc_chapters`) e `update_resources.py` (`DOC_CHAPTERS`): entrambi hanno una
guardia che fa fallire il build se una pagina manca dal `.qrc`.

---

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
