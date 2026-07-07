# Guida: correggere un bug e ricaricare una nuova build su App Store Connect

Procedura passo-passo per quando trovi un bug (su TestFlight o altrove), lo correggi nel
codice, e devi far arrivare la build aggiornata ai tester/App Store. Scritta dopo il primo
giro di rilascio della 3.0, che ha incontrato alcuni intoppi — sono documentati qui sotto
insieme alla soluzione, per non doverli ridiagnosticare da zero.

---

## 0. Percorsi e strumenti di riferimento

| Cosa | Percorso |
|---|---|
| Repo sorgente | `/Users/dioscorid/Projects/C/SurfaceExplorer` |
| Progetto Xcode iOS (generato) | `/Users/dioscorid/Projects/C/SurfaceExplorer/build/ios-appstore/SurfaceExplorer.xcodeproj` |
| qt-cmake (per rigenerare il progetto) | `/Users/dioscorid/Qt/6.10.1/ios/bin/qt-cmake` |
| Bundle ID | `com.dioscorid.surfaceexplorer` |
| Team | GAETANO MOSCHETTI (AJ655XKJR8) |
| Apple ID developer | adenio@libero.it |
| App su App Store Connect | Surface Explorer — App ID 6787015297 |
| Gruppo TestFlight | Internal Testers |

---

## 1. Correggi il bug nel codice

Modifica i file sorgente in `/Users/dioscorid/Projects/C/SurfaceExplorer` come sempre
(mainwindow.cpp, ecc.). Nessuna differenza rispetto al normale lavoro su desktop.

## 2. Testa velocemente in locale (facoltativo ma consigliato)

Per iterare rapidamente SENZA rifare tutto il giro Xcode/App Store Connect ogni volta:
- Compila ed esegui da **Qt Creator** (kit iOS) sul tuo device via cavo/wireless, oppure
- Compila la build **Desktop** e verifica lì la logica se il bug non è iOS-specifico.

Solo quando sei soddisfatto del fix, procedi al packaging per App Store Connect.

## 3. Incrementa il build number (OBBLIGATORIO)

Apple non accetta due upload con lo stesso build number, anche se il precedente
non è mai stato pubblicato. Ad ogni ricarica, incrementa di 1.

Apri **CMakeLists.txt**, sezione `if (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")`, e cambia
DUE numeri (devono essere uguali tra loro):
```cmake
MACOSX_BUNDLE_BUNDLE_VERSION "N"          # <- incrementa
XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "N"  # <- incrementa (stesso valore)
```
Poi apri **Info.plist** e cambia lo stesso numero:
```xml
<key>CFBundleVersion</key>
<string>N</string>
```
(`CFBundleShortVersionString` / `MARKETING_VERSION` = "3.0" resta invariato finché non
cambi la versione "commerciale", es. per la 3.1 o 4.0.)

Committa e pusha (buona pratica, non strettamente necessario per il build):
```bash
cd /Users/dioscorid/Projects/C/SurfaceExplorer
git add -A
git commit -m "Fix <descrizione bug>; build number -> N"
git push origin v1
```

## 4. Rigenera il progetto Xcode PULITO (passo critico)

⚠️ **Questo è il passo che ci ha causato problemi la prima volta**: rigenerare il progetto
Xcode ripetutamente senza pulire la cache può far sì che Xcode riusi oggetti compilati
VECCHI invece di ricompilare i file modificati, producendo un binario con codice stantio
che PASSA la validazione ma non contiene i tuoi fix.

**Fai SEMPRE così, in quest'ordine:**
```bash
# 1. Pulisci la cache di build di Xcode per questo progetto
rm -rf ~/Library/Developer/Xcode/DerivedData/SurfaceExplorer-*

# 2. Cancella la cartella del progetto iOS generato
rm -rf build/ios-appstore

# 3. Rigenera da zero con qt-cmake (NON usare cmake generico: serve il toolchain iOS)
cd /Users/dioscorid/Projects/C/SurfaceExplorer
/Users/dioscorid/Qt/6.10.1/ios/bin/qt-cmake -S . -B build/ios-appstore -G Xcode
```
Verifica che i valori siano quelli attesi:
```bash
grep -h "MARKETING_VERSION\|CURRENT_PROJECT_VERSION\|PRODUCT_BUNDLE_IDENTIFIER" \
  build/ios-appstore/SurfaceExplorer.xcodeproj/project.pbxproj | sort -u
```

## 5. Apri Xcode e archivia

```bash
open /Users/dioscorid/Projects/C/SurfaceExplorer/build/ios-appstore/SurfaceExplorer.xcodeproj
```
1. In alto, seleziona la destinazione **"Any iOS Device (arm64)"** (NON un simulatore:
   con un simulatore selezionato "Archive" resta disabilitato).
2. Verifica **Signing & Capabilities** del target SurfaceExplorer: Team = GAETANO MOSCHETTI,
   "Automatically manage signing" spuntato. Se vedi errori tipo "No Account for Team" o
   "No profiles found", vedi la sezione Troubleshooting sotto.
3. Menu **Product → Archive** (usa il menu in alto, non i pulsanti ▶/■ della toolbar).
4. ⚠️ Con la cache pulita la compilazione sarà LENTA (parecchi minuti, Qt è statico e
   pesante da linkare): NON interromperla, anche se sembra ferma.
5. Al termine si apre l'**Organizer** con il nuovo archivio. Verifica in alto a destra che
   **Version** mostri il build number appena impostato (es. "3.0 (N)").

## 6. Distribuisci (upload su App Store Connect)

Nell'Organizer, con l'archivio nuovo selezionato:
1. **Distribute App**
2. **App Store Connect** → Next
3. **Upload** → Next
4. Lascia **"Upload your app's symbols"** attivo (per crash report leggibili) → Next
5. **Automatically manage signing** → Next
6. **Upload**

Se compare **"Upload completed with warnings" / "Upload Symbols Failed"**: è un warning
NON bloccante sui file dSYM, capitato regolarmente con questo progetto Qt/CMake. Clicca
**Done** e prosegui — l'upload del binario è comunque riuscito. (Se un giorno vuoi
risolverlo alla radice, serve configurare `DEBUG_INFORMATION_FORMAT` nel CMakeLists, ma
non è mai stato necessario finora.)

## 7. Attendi l'elaborazione e verifica

1. Aspetta **10-30 minuti**.
2. Controlla la posta **adenio@libero.it** (anche spam): se Apple respinge la build per un
   problema di conformità (es. ITMS-xxxxx), arriva un'email con la spiegazione. Se non
   arriva nulla, buon segno.
3. Vai su **appstoreconnect.apple.com → Surface Explorer → TestFlight**: la nuova build
   deve comparire con stato **verde "Completo"**.
4. Se compare **"⚠️ Conformità mancante"** (Export Compliance): clicca **Gestisci** →
   alla domanda sugli algoritmi di crittografia scegli **"Nessuno degli algoritmi citati
   sopra"** (l'app non usa crittografia propria) → Salva. Va ripetuto per OGNI build.

## 8. Assegna la build al gruppo tester e reinstalla

1. Tab **TestFlight** → gruppo **Internal Testers** → tab **Build** → assegna/seleziona
   la nuova build (di solito è automatico se è l'unica "pronta").
2. Sul device (iPad 11 / iPhone 16e): apri l'app **TestFlight** → dovrebbe mostrare
   l'aggiornamento disponibile → **Update**.
3. Apri Surface Explorer e verifica che il fix sia presente.

---

## Troubleshooting — problemi già incontrati e soluzione

### "No Account for Team AJ655XKJR8" / "No profiles for com.dioscorid.surfaceexplorer were found"
Il pulsante "Download Manual Profiles" in Xcode → Settings → Apple Accounts a volte si
blocca silenziosamente (nessun errore, nessun risultato) anche con l'account regolarmente
loggato. Soluzione che ha funzionato:
1. Vai su **developer.apple.com/account/resources/profiles/list** (browser)
2. **+** → **iOS App Development** → Continue
3. Seleziona l'App ID `com.dioscorid.surfaceexplorer` → Continue
4. Seleziona il certificato "Apple Development: adenio@libero.it" → Continue
5. Seleziona i device di test → Continue
6. Nome profilo (es. "SurfaceExplorer Development") → **Generate**
7. **Download** il file `.mobileprovision` (va in ~/Downloads)
8. Installalo manualmente (se il doppio click non basta):
   ```bash
   UUID=$(security cms -D -i ~/Downloads/NOMEFILE.mobileprovision 2>/dev/null | \
     grep -A1 "UUID" | grep string | head -1 | sed -E 's/.*<string>(.*)<\/string>.*/\1/')
   cp ~/Downloads/NOMEFILE.mobileprovision \
     ~/Library/MobileDevice/Provisioning\ Profiles/"$UUID.mobileprovision"
   ```
9. Riapri Xcode e riprova Archive.

### Il binario caricato non contiene i fix recenti (bug "vecchi" nonostante il codice sia aggiornato)
Causa: cache DerivedData/build incrociata da rigenerazioni ripetute del progetto Xcode →
oggetti .o non ricompilati per i file modificati. Prevenzione: segui SEMPRE il passo 4
sopra (pulizia completa prima di rigenerare). Per verificare che un archivio sia
genuinamente fresco:
```bash
ARCHIVE=$(ls -dt ~/Library/Developer/Xcode/Archives/*/*.xcarchive | head -1)
BIN="$ARCHIVE/Products/Applications/SurfaceExplorer.app/SurfaceExplorer"
stat -f "%Sm" -t "%Y-%m-%d %H:%M:%S" "$BIN"   # deve essere DOPO l'ultima modifica ai sorgenti
strings "$BIN" | grep "<una stringa univoca del tuo fix più recente>"   # deve trovarla
```

### ITMS-90683: Missing purpose string in Info.plist
Se il codice referenzia API camera/microfono (anche solo simbolicamente, es. tramite un
framework multimediale linkato) senza le relative purpose string, Apple respinge la build
con questo errore via email. Le chiavi sono già presenti in Info.plist
(`NSCameraUsageDescription`, `NSMicrophoneUsageDescription`) — non serve ripetere il fix,
ma se in futuro aggiungi un NUOVO framework che tocca altre API sensibili (foto, posizione,
bluetooth...), potrebbe servire una chiave analoga.

### Divergenza git dopo un ripristino manuale di file
Se hai ripristinato manualmente vecchi file (es. da un backup) e `git push` rifiuta con
"fetch first"/rifiuto per divergenza, NON forzare con `--force`. Usa:
```bash
git fetch origin v1
git log --oneline origin/v1..HEAD   # cosa hai tu che loro non hanno
git log --oneline HEAD..origin/v1   # cosa hanno loro che tu non hai
git pull origin v1 --no-edit        # merge automatico se i file non si sovrappongono
git push origin v1
```

### Screenshot iPad "Display da 13"" richiesto ma hai solo iPad 11"
App Store Connect ha sezioni separate per iPad 11" e iPad 13" (12,9"). Puoi caricare gli
screenshot del tuo iPad 11 nella sezione corrispondente ("iPad — Display da 11""); se il
salvataggio della scheda richiede obbligatoriamente anche la 13", bisogna generarli dal
Simulatore Xcode (device "iPad Pro 13-inch") oppure ridimensionare quelli esistenti alle
risoluzioni esatte: 2064×2752, 2752×2064, 2048×2732 o 2732×2048 px.

---

## Riepilogo comandi rapidi (copia-incolla)

```bash
# --- Dopo aver corretto il bug e aggiornato i numeri di versione a mano ---
cd /Users/dioscorid/Projects/C/SurfaceExplorer
git add -A && git commit -m "Fix <descrizione>; build number -> N" && git push origin v1

rm -rf ~/Library/Developer/Xcode/DerivedData/SurfaceExplorer-*
rm -rf build/ios-appstore
/Users/dioscorid/Qt/6.10.1/ios/bin/qt-cmake -S . -B build/ios-appstore -G Xcode

open build/ios-appstore/SurfaceExplorer.xcodeproj
# -> in Xcode: destinazione "Any iOS Device (arm64)", Product > Archive,
#    poi Distribute App > App Store Connect > Upload
```
