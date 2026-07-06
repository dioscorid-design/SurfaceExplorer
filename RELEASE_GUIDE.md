# Guida al rilascio — Surface Explorer

Come pubblicare una nuova versione su GitHub Releases e integrarla nel desktop
Linux. Riassume il processo e i due script del repo:

| Script | Piattaforma | Cosa fa |
|--------|-------------|---------|
| [`release_linux.sh`](release_linux.sh) | Linux | Compila → AppImage → carica su GitHub |
| [`release_macos.sh`](release_macos.sh) | macOS | Firma → `.dmg` → notarizza (da lanciare **sul Mac**) |
| [`install-linux.sh`](install-linux.sh) | Linux (utente finale) | Integra l'AppImage nel menu applicazioni |

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
# solo build + AppImage in dist/ (senza toccare GitHub)
./release_linux.sh

# build + AppImage + creazione/aggiornamento release GitHub + upload asset
./release_linux.sh --upload
```

Cosa fa `--upload`, nell'ordine:
1. Legge la versione da `CMakeLists.txt` → tag `vX.Y`.
2. Compila la release con CMake+Ninja (Qt in `QT_DIR`, default `~/Qt/6.10.2/gcc_64`).
3. Popola l'`AppDir` con le regole `install()` del `CMakeLists.txt` (binario + `.desktop` + icone).
4. Costruisce `SurfaceExplorer-vX.Y-linux-x86_64.AppImage` con `linuxdeploy` + plugin Qt
   (scaricati e messi in cache in `build/appimage-tools/`, senza bisogno di FUSE).
5. Genera lo `.sha256`.
6. Se il tag non esiste sul remoto lo crea su `HEAD` e lo pusha; poi crea/riusa la
   release e carica **AppImage + .sha256 + install-linux.sh**, sostituendo asset omonimi.

Opzioni utili:
```sh
QT_DIR=~/Qt/6.11.0/gcc_64 ./release_linux.sh --upload   # altro Qt
./release_linux.sh --skip-build --upload                # riusa un build gia' fatto
GH_TOKEN=ghp_xxx ./release_linux.sh --upload            # token esplicito
```

Il token GitHub viene preso da `GH_TOKEN` oppure da `~/.git-credentials`
(git `credential.helper store`). Serve permesso di scrittura sul repo.

---

## 🍎 Rilascio macOS (sul Mac)

Sul Mac, dopo aver compilato la `.app` in `build/Desktop_Qt_6_10-Release`
(con `macdeployqt` incluso):

```sh
./release_macos.sh      # firma Developer ID + hardened runtime, crea .dmg, notarizza
```

Poi carica manualmente il `.dmg` risultante come asset della release `vX.Y`
(o via API, come per Linux).

> ⚠️ Prima di ogni build iOS/macOS: `git restore Info.plist && plutil -lint Info.plist`.
> L'`Info.plist` si è già corrotto in passato con una sincronizzazione a copia-cartella
> (testo spurio dentro `<dict>`, invalido per la DTD Apple). Vedi la nota sotto.

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
- `SurfaceExplorer-vX.Y-win64.zip` (Windows)
- `SurfaceExplorer.dmg` (macOS)
- *Source code (zip/tar.gz)* — generati automaticamente da GitHub dal **tag**:
  per essere aggiornati il tag deve puntare all'ultimo commit committato.
