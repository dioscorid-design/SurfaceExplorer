# Screenshot per il Mac App Store

Formati accettati (16:10, esatti): **2880×1800**, 2560×1600, 1440×900, 1280×800.

---

## Una volta sola

```bash
defaults write com.apple.screencapture disable-shadow -bool true
killall SystemUIServer
```

Senza, l'ombra della finestra si somma all'immagine, le dimensioni non tornano e
App Store Connect rifiuta il file.

L'impostazione è **permanente**: sta su disco, sopravvive a riavvii e aggiornamenti,
e resta finché non la si cambia. Non va ridata a ogni sessione.

Verifica (`1` = ombra disattivata):

```bash
defaults read com.apple.screencapture disable-shadow
```

Per rimetterla, quando serve l'ombra altrove (documentazione, email):

```bash
defaults write com.apple.screencapture disable-shadow -bool false
killall SystemUIServer
```

---

## Per ogni screenshot

1. Apri SurfaceExplorer ed **esci dal tutto schermo**.
2. Ridimensiona:

   ```bash
   ./ExC/Mac/window_for_screenshot.sh
   ```

   Sposta la finestra sul 4K **e** la porta a 1440×900. Se stampa un errore,
   **fermati e risolvi**: la cattura uscirebbe di misura sbagliata.

3. **Non spostare né ridimensionare la finestra a mano** dopo questo punto.
   Trascinandola tra i monitor macOS la rimpicciolisce e il formato si perde.

4. Prepara la scena nell'app.
5. Cattura: **Cmd+Shift+4**, poi **barra spaziatrice**, poi **clic sulla finestra**.

   La barra spaziatrice è il passaggio critico: il puntatore deve diventare una
   **macchina fotografica** e la finestra evidenziarsi in blu. Se resta un mirino
   a croce e trascini, catturi un rettangolo a mano e la misura è sbagliata.

   L'unica verifica attendibile è misurare il file (sotto). `defaults read
   com.apple.screencapture style` **non** dice come è stata fatta l'ultima
   cattura: può restare `selection` anche dopo una cattura-finestra corretta.

---

## Verifica prima di caricare

```bash
sips -g pixelWidth -g pixelHeight ~/Desktop/Screenshot*.png
```

Deve dare **2880 × 1800**. Qualsiasi altro valore: rifai la cattura, non ritagliare.

Controllo di tutta la cartella:

```bash
cd ~/Desktop/AppStore_Mac
for f in *.png; do
  sips -g pixelWidth -g pixelHeight "$f" | awk -v n="$f" \
    '/pixelWidth/{w=$2} /pixelHeight/{h=$2} END{printf "%-16s %5d x %5d %s\n", n, w, h, (w==2880 && h==1800 ? "ok" : "DA RIFARE")}'
done
```

---

## Se la misura è sbagliata

Per capire da dove viene l'errore, dividi per 2 le dimensioni del file (sul 4K a 2x)
e confronta con la finestra:

```bash
osascript -e 'tell application "System Events" to tell process "SurfaceExplorer" to get size of window 1'
```

Se dice `1440, 900` la finestra è giusta e il problema è nella cattura; altrimenti è
il ridimensionamento che non ha funzionato.

| Sintomo | Causa |
|---|---|
| Il file è **il doppio esatto** della finestra, ma la finestra non è 1440×900 | La finestra è stata **spostata a mano** dopo lo script: trascinandola tra i monitor macOS la rimpicciolisce (misurato: torna a 1200×832 → 2400×1664 px). Rilancia lo script e non toccarla più |
| Il file esce 1440×900 px | La finestra è finita sul **Full HD (1x)**. Lo script la porta sul 4K: se la disposizione dei monitor è cambiata, correggi `ORIGIN_X` |
| Resta 1920×1080 (16:9) | App a **tutto schermo**: macOS ignora ogni ridimensionamento |
| Altezza troncata (es. 832 invece di 900 punti) | Finestra a contatto col **bordo inferiore** dello schermo |
| Esce 1440×900 px invece di 2880×1800 | Finestra sul monitor **Full HD (1x)** anziché sul 4K (2x) |
| Molto più grande della finestra (misurato: 1200×832 → 1600×1500) | **Ombra** attiva: vedi "Una volta sola" |
| Dimensioni senza rapporto con la finestra | Catturata un'area a mano: serve la **barra spaziatrice** |

Le immagini fuori formato non si recuperano: ritagliare perde contenuto, riscalare
deforma la geometria. Si rifà la cattura.

---

## Perché il monitor conta

macOS misura le finestre in **punti**; su un display 2x ogni punto vale 2 pixel.

| Finestra | Schermo | Cattura |
|---|---|---|
| 1440×900 punti | 4K scalato 2x | 2880×1800 px — **il migliore** |
| 1440×900 punti | Full HD 1x | 1440×900 px — valido |
| 1280×800 punti | Full HD 1x | 1280×800 px — minimo |

Su questa macchina il 4K è 2x e il Full HD è 1x: per ottenere 2880×1800 la finestra
va tenuta sul 4K.
