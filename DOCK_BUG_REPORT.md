# Bug: dock Library allungato/dimezzato su iPhone — REPORT (IRRISOLTO)

_Ultimo aggiornamento: 2026-07-01. Stato: **IRRISOLTO**, indagine sospesa. Diagnostica a schermo lasciata nel codice._

## Sintomo
Solo **iPhone** (iPad no). Il dock **Library** (`dockSurfaces`, che ospita il `QTabWidget`
Surfaces/Textures/Sounds/Records) a volte si **allunga** verso il basso fino a coprire la status
bar; il contenuto utile (albero + bottone "Restore Factory Presets") resta pinnato in alto con una
banda vuota sotto. È **intermittente e casuale**: la stessa identica sequenza a volte lo produce e a
volte no. Si manifesta tipicamente attorno al **salvataggio** (apertura/chiusura del
`MobileSaveDialog`), ma **non solo**: anche "Restore Factory Presets" (che usa solo `QMessageBox`,
niente tastiera) lo ha prodotto.

## Diagnosi confermata dai log a schermo (dbgDockLog)
Overlay verde che stampa, per `dockSurfaces`, ad ogni evento: `sH`=height, `min`=minimumHeight,
`max`=maximumHeight, `hint`=sizeHint.h, `mhint`=minimumSizeHint.h, `win`=MainWindow.height,
`cr`=contentsRect.height. Landscape iPhone: `win=390`, `cr=370`.

Quando il dock è rotto:
```
sH=419  min=419  max=524287  hint=501  mhint=419  win=390  cr=370
```
- **`sH=419 > win=390`**: il dock è più alto della finestra → sfora sotto la status bar. QUESTO è
  l'allungamento.
- **`min=419 == mhint=419`**: non c'è un minimo esplicito; `min` riflette il **minimumSizeHint** del
  contenuto (il `QTabWidget` interno + i `QTreeWidget` propagano ~419).
- Qt **non può** rendere il dock più basso del suo minimumSizeHint → 419 vince.

Quando invece è a posto (dopo aver forzato `min=0`):
```
sH=355  min=211  ...  win=390
```

## Perché è "casuale"
Il `minimumSizeHint` del contenuto dipende da **quanto è espanso l'albero** dei record e da quali
pagine/tab sono state toccate. A volte "nasce" ≤370 (ok), a volte 419 (rotto). Non è legato a un
singolo evento del salvataggio: il save fa da innesco perché il **refresh degli alberi** (nuovo
record aggiunto) **rialza il minimumSizeHint**.

## Cosa è stato provato e NON ha funzionato (non ripetere)
1. **Compressione del dock via `keyboardRectangleChanged`** (margine `kbdHeight` in basso). Era la
   pipista iniziale "è la tastiera". FALSO: iOS porta già i campi in-dock sopra la tastiera, e il
   dock si rompe pure senza tastiera. Solo dannosa. **RIMOSSA.**
2. **`resizeDocks(visibleDocks, contentsRect().height())`** ai momenti di chiusura overlay.
   IGNORATO: `resizeDocks` non scende sotto il minimumSizeHint → `sH` resta 419.
3. **"Martello" `setMaximumHeight(370)` + rilascio.** IGNORATO: `min=419 > max=370` è una
   contraddizione che Qt risolve a favore del **minimo** → `sH` resta 419.
4. **`setMinimumHeight(0)` su `tabWidget` + `QTreeWidget` + widget del dock, UNA VOLTA all'avvio.**
   Funziona all'inizio (`min` scende a ~211, `sH`~355) ma il **refresh degli alberi dopo il save
   RIALZA il minimo a 419**. Non basta una volta sola.
5. **`clampLibraryDockMinHeight()` (idem #4) richiamato anche in `refreshRepositories` e nel
   relayout.** Nell'ultimo log `min` restava comunque 419 → o la chiamata non copriva tutti i punti
   di risalita, o `setMinimumHeight(0)` non abbassa il `minimumSizeHint` effettivo (che è calcolato
   dal contenuto, non un minimo esplicito). **RIMOSSO.**
6. **Relayout cause-agnostic** (`relayoutDocksAfterOverlay` da `changeEvent`/ActivationChange +
   `MobileSaveDialog::hideEvent` con doppio `singleShot` 50/350ms). Sembrava risolvere "Restore" ma
   NON il salvataggio, e comunque combatteva il sintomo, non la causa. **RIMOSSO.**

## Ipotesi per la prossima sessione (in ordine di probabilità)
1. **Il minimumSizeHint del `QTabWidget`/`QTreeWidget` è la radice.** `setMinimumHeight(0)` sul dock
   NON cambia il minimumSizeHint del contenuto. Provare invece a rendere il contenuto realmente
   comprimibile: avvolgere il `QTabWidget` della Library in una `QScrollArea` con
   `setWidgetResizable(true)` (come gli altri dock, che oggi NON ce l'hanno perché `setupDockScroll`
   esce presto per `isExamplesDock`). Oppure `QSizePolicy::Ignored` in verticale sul contenuto.
2. **Safe-area / status bar.** `sH=419` e `win=390`: la differenza (~29px) è ~l'altezza della status
   bar. Verificare se l'area dock su iPhone si estende sotto la status bar (contentsMargins della
   MainWindow che non tiene conto della safe-area in landscape). Se sì, la cura è nei margini della
   finestra, non nel dock.
3. **`resizeDocks` una volta abbassato il minimo.** Se (1) rende il contenuto comprimibile, allora
   un `resizeDocks(370)` finalmente attecchisce.

## Bug correlato ancora aperto
Tappando il **nodo di categoria "Surfaces"** (ramo principale) il `MobileSaveDialog` si apre **dentro
"Ray Marching"** invece che alla radice `surfaces`: `startPath`/`lastFolder` eredita una sottocartella
da una navigazione precedente. Da sistemare separatamente.

## Fix di questa sessione che INVECE funzionano (NON toccare)
- Tasto **Up** intrappolato in sandbox: `canNavigateUp()` + `currentDir.refresh()` in
  `MobileSaveDialog`.
- **Floor** di navigazione (l'Up non sale oltre la radice del tipo): parametro `navFloor`.
- **`startPath` inesistente** tappando il nodo di categoria → dialog vuoto + save fallito: fallback a
  `presetsRootPath()+"/<tipo>"` + `mkpath`, in `saveSurface`/`saveMotion`/`saveTextureAs`/`saveSoundAs`.
- Titolo dialog corretto in `saveMotionAs`: "Save Record As...".

## Diagnostica — RIMOSSA (2026-07-01)
Dopo la pulizia il bug non era piu' riproducibile (dipende dallo stato del layout della Library,
non c'e' una sequenza deterministica). Su richiesta utente TUTTA la diagnostica e l'apparato dock
sono stati RIMOSSI: `dbgDockLog`, `m_dbgLabel`, l'handler `keyboardRectangleChanged` su iPhone,
`relayoutDocksAfterOverlay`, `clampLibraryDockMinHeight`, `MobileSaveDialog::hideEvent`. `mainwindow.h`
e' tornato identico all'originale. Se il bug si ripresenta: ri-aggiungere `dbgDockLog` (overlay
QLabel con `sH/min/max/hint/mhint/win/cr` di `dockSurfaces`) e ripartire dalle ipotesi qui sopra.
La cronologia dei tentativi in questo file resta valida per non ripeterli.
