# Guida: provare la PRIMA APERTURA come la vede un utente nuovo

Serve a rispondere a una domanda sola: **chi scarica l'applicazione per la prima volta,
cosa si trova?** Un utente nuovo ha il container sandbox vuoto — niente `libraryRootPath`,
nessun bookmark, nessuna cartella creata. Sul tuo Mac quello stato non c'è mai, perché le
build precedenti lo hanno riempito: va quindi ricreato a mano.

Scritta dopo il caso dell'8/9/2026, in cui la build TestFlight sembrava installare i quattro
rami della libreria sparsi in `~/Projects` invece che dentro `~/Projects/presets`. Non era un
difetto del codice: era una preferenza lasciata da una build precedente. La spiegazione sta
in fondo, al paragrafo "Perché serve azzerare".

---

## In breve: i comandi in sequenza

Chiudi prima l'applicazione (tutte le versioni aperte). Poi, in blocco:

```bash
# 1. trova il container e mostra lo stato attuale (se stampa libraryRootPath, non e' prima apertu

# 2. azzera (= stato di prima installazione)
find "$CONTAINER/Data" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null
defaults delete com.dioscorid.surfaceexplorer 2>/dev/null
killall -u "$USER" cfprefsd 2>/dev/null

# 3. apri la build TestFlight dal Launchpad o da /Applications
#    (per la sola build LOCALE bastano i punti 1-3 in un colpo:
#     ./ExC/Mac/test_sandbox_local.sh --reset)

# 4. verifica dopo aver scelto la cartella nel pannello
ls <cartella-scelta>/presets                              # records sounds surfaces textures
find <cartella-scelta>/presets -name "*.json" | wc -l     # ~420
```

Il resto della guida spiega cosa fa ogni passo, cosa deve succedere a schermo e come
riconoscere un difetto.

---

## 0. La trappola da conoscere prima di cominciare

**La build locale e quella TestFlight condividono lo stesso bundle id**
(`com.dioscorid.surfaceexplorer`) e quindi **lo stesso container sandbox**:

```
~/Library/Containers/<UUID>/Data/
```

Ne discendono due conseguenze pratiche:

1. le preferenze scritte da una build le legge anche l'altra — per questo TestFlight può
   partire citando percorsi del tuo Mac che un utente non avrebbe mai;
2. azzerare il container azzera lo stato di **entrambe**.

Il container non si chiama sempre come il bundle id: si trova cercando il plist.

---

## 1. Trovare il container

```bash
ls -d "$HOME/Library/Containers"/*/Data/Library/Preferences/com.dioscorid.surfaceexplorer.plist
```

Il percorso stampato contiene l'UUID del container. Per vedere lo stato attuale:

```bash
plutil -p <percorso-del-plist> | grep -i libraryRoot
```

Se compare `libraryRootPath`, l'app **non** è in stato di prima apertura.

---

## 2. Azzerare il container

Chiudi prima l'applicazione (entrambe le versioni, se aperte).

```bash
find "$HOME/Library/Containers/<UUID>/Data" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null
defaults delete com.dioscorid.surfaceexplorer 2>/dev/null
killall -u "$USER" cfprefsd 2>/dev/null
```

Le tre righe sono le stesse che esegue `test_sandbox_local.sh --reset`:

- la prima svuota `Data/` senza rimuoverla (il sistema la ricrea coi suoi permessi);
- la seconda cancella le preferenze;
- la terza riavvia `cfprefsd`, che le tiene in cache e altrimenti le rimetterebbe.

**Cosa perdi:** il percorso della libreria e i bookmark di sandbox. Alla riapertura dovrai
riselezionare la cartella dei preset — è esattamente ciò che farai durante il test.

---

## 3. Aprire la build da provare

**TestFlight** — dal Launchpad o da `/Applications`. Attenzione a non lanciare per sbaglio
la build locale in `build/macos-sandbox-test/`: hanno lo stesso bundle id e lo stesso
container, ma sono binari diversi.

**Locale (equivalente e più rapido)** — fa reset e lancio in un colpo:

```bash
./ExC/Mac/test_sandbox_local.sh --reset
```

La build locale è sandboxata con gli stessi entitlements della TestFlight, quindi per questo
tipo di verifica **le due sono equivalenti**: quello che si vede in una si vede nell'altra.
Conviene provare prima in locale (secondi, nessun build number bruciato) e usare TestFlight
solo per la conferma finale.

---

## 4. Cosa deve succedere

| Passo | Comportamento atteso |
|---|---|
| Avvio | **Nessun** popup "Library Not Accessible" — non c'è nulla da recuperare |
| Avvio | Compare "Choose a location to install your Library", senza citare percorsi |
| Scegli una cartella **pulita** | La libreria va in `<scelta>/presets`, coi quattro rami dentro |
| Cartella già libreria | Popup "Folder Already Contains a Library": la adotta com'è, **senza** creare `presets` |
| Cartella in iCloud/Documenti | Popup "Folder Synced to the Cloud" prima di procedere |
| Rinunci a scegliere | Popup "Library Not Installed", libreria vuota, nulla creato |

Verifica finale sul disco:

```bash
ls <cartella-scelta>/presets          # deve mostrare: records sounds surfaces textures
find <cartella-scelta>/presets -name "*.json" | wc -l    # ~420 preset
```

**Segnali di difetto** (da indagare, non normali):

- i quattro rami finiscono **liberi** nella cartella scelta invece che dentro `presets/`;
- compare il popup di recupero pur avendo azzerato il container;
- la libreria resta vuota dopo aver scelto una cartella valida.

---

## 5. Perché serve azzerare (il caso dell'8/9/2026)

Aprendo la build TestFlight, prima ancora della finestra dell'applicazione, è comparso un
popup che diceva di non trovare la libreria in `/Users/dioscorid/Projects` e chiedeva di
indicare un nuovo percorso. Indicando `~/Projects`, l'app vi ha creato i quattro rami
**liberi**, senza il livello `presets`.

Sembrava un bug dell'installazione. Non lo era:

1. il container conteneva `libraryRootPath = /Users/dioscorid/Projects`, scritta da una build
   locale precedente (stesso bundle id, stesso container);
2. quella cartella non era accessibile alla sandbox di TestFlight → è partito il dialogo di
   **recupero** (`mainwindow.cpp`, ~4060), non quello di prima installazione;
3. il ramo di recupero, per progetto, **non crea** il livello `presets`: dà per scontato che
   la libreria esista già e che l'utente la stia solo ri-autorizzando. Quando la cartella
   indicata non è una libreria, i rami finiscono sparsi.

Un utente nuovo non passa mai di lì, perché senza `libraryRootPath` il codice prende il ramo
di prima installazione, che crea correttamente `<scelta>/presets` — verificato con
`test_sandbox_local.sh --reset`.

**Resta aperta una questione di robustezza:** il dialogo di recupero accetta una cartella che
non è una libreria e ci installa dentro i rami sparsi. Non capita all'utente finale, ma
capita a chi ha una `libraryRootPath` che punta a una cartella non più valida.

Vedi anche `GUIDA_rilascio_build_ios_macos.md` per il flusso di rilascio, e l'intestazione di
`ExC/Mac/test_sandbox_local.sh` per il ciclo "funziona al primo avvio, non al secondo".
