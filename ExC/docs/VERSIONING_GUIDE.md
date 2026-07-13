# Guida rapida — bump versione e release TestFlight

Comandi da lanciare **dalla root del progetto**.

## 1. Aggiornare la versione marketing (X.Y)

```sh
./ExC/Mac/bump-version.sh 1.1              # aggiorna i file e committa
./ExC/Mac/bump-version.sh 1.1 --no-commit  # aggiorna soltanto i file
```

Aggiorna `CMakeLists.txt` (VERSION, MACOSX_BUNDLE_SHORT_VERSION_STRING,
XCODE_ATTRIBUTE_MARKETING_VERSION) e `Info.plist` (CFBundleShortVersionString).
NON tocca il build number.

> ⚠️ Aggiorna anche a mano la stringa "Version X.Y" nel dialogo About
> (`mainwindow.cpp`, dentro `QMessageBox::about`): non è toccata dallo script.

Poi:
```sh
git push
```

## 2. Rilasciare su TestFlight (build number)

```sh
./ExC/Mac/release_testflight.sh
```

Incrementa il build number (+1) in `CMakeLists.txt` + `Info.plist`, committa,
pulisce la cache Xcode, rigenera il progetto iOS e archivia. Apre l'Organizer
di Xcode per l'upload manuale (Distribute App → App Store Connect → Upload).

Varianti:
```sh
./ExC/Mac/release_testflight.sh --no-commit    # non committa il bump
./ExC/Mac/release_testflight.sh --no-archive   # solo bump + rigenera progetto Xcode
```

Non tocca la versione marketing: se serve cambiare anche X.Y, lancia prima
`bump-version.sh`.

## 3. Rilasciare il .dmg su GitHub (macOS)

```sh
./ExC/Mac/release_macos.sh --upload
```

Firma, crea il `.dmg`, notarizza, carica sulla release GitHub `vX.Y` (letta da
`CMakeLists.txt`). Richiede di aver ricompilato la `.app` dopo il bump.

## Ordine tipico per una nuova versione X.Y

```sh
./ExC/Mac/bump-version.sh 1.1
git push
./ExC/Mac/release_testflight.sh      # TestFlight
./ExC/Mac/release_macos.sh --upload  # .dmg GitHub
```

Per dettagli completi (troubleshooting notarizzazione, Linux, Windows) vedi
[`RELEASE_GUIDE.md`](RELEASE_GUIDE.md).
