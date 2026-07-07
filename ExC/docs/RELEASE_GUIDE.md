# Guida al rilascio — Surface Explorer

Come pubblicare una nuova versione su GitHub Releases per Linux, macOS e Windows,
e integrarla nel desktop. Riassume il processo e gli strumenti per piattaforma:

| Script | Piattaforma | Cosa fa |
|--------|-------------|---------|
| [`Linux/release_linux.sh`](../Linux/release_linux.sh) | Linux | Compila → AppImage → carica su GitHub |
| [`Mac/release_macos.sh`](../Mac/release_macos.sh) | macOS | Firma → `.dmg` → notarizza (da lanciare **sul Mac**) |
| [`Windows/make_standalone.ps1`](../Windows/make_standalone.ps1) | Windows | Bundle DLL con `windeployqt` → `.zip` (upload manuale) |
| [`Linux/install-linux.sh`](../Linux/install-linux.sh) | Linux (utente finale) | Integra l'AppImage nel menu applicazioni |

> ℹ️ Gli script vivono in `ExC/Linux/`, `ExC/Mac/`, `ExC/Windows/` (cartella `ExC/`
> esclusa da `.gitignore`). I comandi qui sotto assumono di lanciarli dalla **root
> del progetto**, es. `./ExC/Linux/release_linux.sh`.

---

## 🆕 Promemoria: cosa cambiare per una NUOVA versione

Prima di tutto, **aggiorna il numero di versione in due file** (devono coincidere):

1. **`CMakeLists.txt`** (blocco iOS, righe ~144-148):
   - `VERSION "X.Y"`
   - `MACOSX_BUNDLE_SHORT_VERSION_STRING "X.Y"`  ← lo script Linux legge da qui il tag
   - `MACOSX_BUNDLE_BUNDLE_VERSION "N"` e `XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "N"` ← **build number**, va incrementato a ogni upload su App Store/TestFlight
   - `XCODE_ATTRIBUTE_MARKETING_VERSION "X.Y"`

2. **`Info.plist`**:
   - `CFBundleShortVersionString` → `X.Y`  (versione mostrata)
   - `CFBundleVersion` → `N`  (build number, come sopra)

3. **Committa e pusha** le modifiche di versione **prima** di lanciare gli script:
   ```sh
   git add CMakeLists.txt Info.plist
   git commit -m "Bump versione X.Y build N"
   git push origin v3
   ```

Il tag della release sarà `vX.Y`, derivato automaticamente da `CMakeLists.txt`.

> ⚠️ **Regola d'oro:** X.Y = versione marketing (cambia raramente); N = build number
> (incrementa a **ogni** invio ad Apple, anche a parità di X.Y). Apple rifiuta due
> upload con lo stesso build number.

---

## 🐧 Rilascio Linux (automatico)

Un solo comando fa tutto: compila, crea l'AppImage autonoma e la carica su GitHub.

```sh
# solo build + AppImage in ExC/Linux/ (senza toccare GitHub)
./ExC/Linux/release_linux.sh

# build + AppImage + creazione/aggiornamento release GitHub + upload asset
./ExC/Linux/release_linux.sh --upload
```

Cosa fa `--upload`, nell'ordine:
1. Legge la versione da `CMakeLists.txt` → tag `vX.Y`.
2. Compila la release con CMake+Ninja (Qt in `QT_DIR`, default `~/Qt/6.10.2/gcc_64`).
3. Popola l'`AppDir` con le regole `install()` del `CMakeLists.txt` (binario + `.desktop` + icone).
4. Costruisce `SurfaceExplorer-vX.Y-linux-x86_64.AppImage` con `linuxdeploy` + plugin Qt
   (scaricati e messi in cache in `build/appimage-tools/`, senza bisogno di FUSE).
5. Genera lo `.sha256`.
6. Se il tag non esiste sul remoto lo crea su `HEAD` e lo pusha; poi crea/riusa la
   release e carica **AppImage + install-linux.sh**, sostituendo asset omonimi.
   Prima dell'upload rimuove dalla release gli AppImage di **versioni precedenti**
   (nome `SurfaceExplorer-vX.Y-linux-x86_64.AppImage`), che altrimenti resterebbero
   affiancati al nuovo (il `--clobber` sostituisce solo asset con lo stesso nome).

Opzioni utili:
```sh
QT_DIR=~/Qt/6.11.0/gcc_64 ./ExC/Linux/release_linux.sh --upload   # altro Qt
./ExC/Linux/release_linux.sh --skip-build --upload                # riusa un build gia' fatto
GH_TOKEN=ghp_xxx ./ExC/Linux/release_linux.sh --upload            # token esplicito
```

Il token GitHub viene preso da `GH_TOKEN` oppure da `~/.git-credentials`
(git `credential.helper store`). Serve permesso di scrittura sul repo.

---

## 🍎 Rilascio macOS (sul Mac)

Sul Mac, dopo aver compilato la `.app` in `build/Desktop_Qt_6_10-Release`
(con `macdeployqt` incluso):

```sh
./ExC/Mac/release_macos.sh  # firma Developer ID + hardened runtime, crea .dmg, notarizza
```

Poi carica manualmente il `.dmg` risultante come asset della release `vX.Y`
(o via API, come per Linux).

> ⚠️ Prima di ogni build iOS/macOS: `git restore Info.plist && plutil -lint Info.plist`.
> L'`Info.plist` si è già corrotto in passato con una sincronizzazione a copia-cartella
> (testo spurio dentro `<dict>`, invalido per la DTD Apple). Vedi la nota sotto.

---

## 🪟 Rilascio Windows (su Windows)

Ora c'è uno script che automatizza il bundle; la **pubblicazione resta manuale**
(git/gh non sono sulla macchina Windows). Con il kit **Qt 6.10.2 MinGW**:

1. **Compila la Release** in Qt Creator (o via CMake), ottenendo `SurfaceExplorer.exe`
   in `build\Desktop_Qt_6_10_2_MinGW_64_bit-Release\`.
   L'icona dell'exe arriva da `app.rc` → `icon.ico` (vedi nota in fondo).

2. **Genera l'app indipendente** con lo script (dalla cartella `ExC\Windows`):
   ```bat
   .\make_standalone.bat
   ```
   La versione nel nome dello zip viene letta da `CMakeLists.txt` (come per Linux);
   puoi comunque forzarla con `-Version X.Y`.
   Lo script:
   - rileva automaticamente l'exe Release (o passalo con `-ExePath`);
   - costruisce il bundle in una cartella **temporanea** e vi lancia `windeployqt`
     (DLL Qt, plugin di piattaforma, runtime MinGW e i binari FFmpeg per Multimedia);
   - crea l'archivio `ExC\Windows\SurfaceExplorer-X.Y-windows-x64.zip` e poi elimina la
     cartella temporanea: in `ExC\Windows\` resta **solo lo .zip**.

   Opzioni: `-KeepFolder` (mantiene anche la cartella scompattata, per una verifica
   locale), `-QtBin`/`-MinGWBin` (percorsi Qt/MinGW se diversi dai default
   `C:\Qt\6.10.2\mingw_64\bin` e `C:\Qt\Tools\mingw1310_64\bin`).

3. **Verifica** scompattando lo `.zip` (oppure rilanciando con `-KeepFolder`) e
   avviando `SurfaceExplorer.exe` su una macchina pulita (senza Qt installato):
   deve partire usando solo le DLL presenti accanto all'eseguibile.

4. **Carica lo `.zip`** come asset della release `vX.Y`, manualmente sulla pagina
   *Releases → Draft a new release* (trascinalo nella zona *Attach binaries*).
   Lo script stampa a fine esecuzione il promemoria dei passaggi e la dimensione
   dell'archivio (deve restare sotto i 100 MB, limite GitHub per file).

> ℹ️ Gli `.zip` (e la cartella `ExC\Windows\SurfaceExplorer\` prodotta con `-KeepFolder`)
> sono esclusi da `.gitignore`: sono artefatti di build, non vanno committati.

> ⚠️ **SmartScreen:** l'exe non è firmato con un certificato Authenticode, quindi al
> primo avvio Windows può mostrare *"Windows ha protetto il tuo PC"* → **Ulteriori
> informazioni → Esegui comunque**. Per eliminarlo servirebbe un certificato di
> code-signing (a pagamento), non ancora previsto — a differenza di macOS, che è
> firmato e notarizzato.

---

## 🖥️ Integrazione desktop (utente finale, Linux)

L'utente scarica dalla release **l'AppImage + `install-linux.sh`** nella stessa
cartella, poi:

```sh
chmod +x install-linux.sh
./install-linux.sh            # auto-rileva l'AppImage, crea voce di menu + icona
```

- Installa in `~/Applications/SurfaceExplorer.AppImage` (nome fisso → aggiornamenti puliti).
- Crea `~/.local/share/applications/surface-explorer.desktop` e installa l'icona
  estratta dall'AppImage stessa.
- **Aggiornare:** rilancia lo script con la nuova AppImage.
- **Disinstallare:** `./install-linux.sh --uninstall`.

Se l'AppImage dà "Permission denied": manca il bit di esecuzione → `chmod +x`.
Se manca FUSE: `./SurfaceExplorer-...AppImage --appimage-extract-and-run`.

---

## ⚠️ Sincronizzazione tra Mac e Linux: usa git, non la copia-cartella

Copiare la cartella del progetto tra le macchine (drive condiviso, AirDrop…) ha
causato: `Info.plist` corrotto, file spazzatura macOS (`._*`, `.DS_Store`) sparsi,
e `._pack-*` dentro `.git` che rompono git. **Sincronizza sempre con
`git pull` / `git push`** (branch di lavoro: `v3`). Ogni macchina tiene la propria
working tree; git trasferisce solo contenuti versionati e validi.

Pulizia rapida dei file spazzatura macOS, se ricompaiono:
```sh
find . -name '._*' -not -path './.git/*' -delete
find . -name '.DS_Store' -not -path './.git/*' -delete
```

---

## 📦 Cosa contiene una release completa

Sulla pagina della release `vX.Y` devono comparire:
- `SurfaceExplorer-vX.Y-linux-x86_64.AppImage` (+ `.sha256`)
- `install-linux.sh`
- `SurfaceExplorer-X.Y-windows-x64.zip` (Windows, da `ExC/Windows/make_standalone.ps1`)
- `SurfaceExplorer.dmg` (macOS)
- *Source code (zip/tar.gz)* — generati automaticamente da GitHub dal **tag**:
  per essere aggiornati il tag deve puntare all'ultimo commit committato.
