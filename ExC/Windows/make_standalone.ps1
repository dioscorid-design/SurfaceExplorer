<#
    make_standalone.ps1
    -------------------
    Crea un'applicazione Windows indipendente (portable) di SurfaceExplorer e la
    impacchetta in un archivio .zip pronto da pubblicare. Copia l'eseguibile
    Release e vi affianca tutte le DLL di Qt, i plugin e il runtime MinGW tramite
    windeployqt: il contenuto dello zip parte su qualsiasi PC Windows senza Qt.

    Il bundle viene costruito in una cartella temporanea e, una volta creato lo
    zip, la cartella viene eliminata: in Windows\ resta solo l'archivio .zip.

    Uso tipico (dalla cartella Windows del progetto, dopo aver compilato la Release):
        powershell -ExecutionPolicy Bypass -File .\make_standalone.ps1

    Lo zip e' pronto da caricare a mano come asset di una GitHub Release
    (https://github.com/<utente>/<repo>/releases/new).

    Parametri opzionali:
        -ExePath <path>   Percorso di SurfaceExplorer.exe (autorilevato se omesso)
        -Version <str>    Numero di versione usato nel nome dello zip (default 3.0)
        -KeepFolder       Mantiene anche la cartella non compressa (per verifica locale)
#>

[CmdletBinding()]
param(
    [string]$ExePath,
    [string]$Version  = "3.0",
    [string]$QtBin    = "C:\Qt\6.10.2\mingw_64\bin",
    [string]$MinGWBin = "C:\Qt\Tools\mingw1310_64\bin",
    [switch]$KeepFolder
)

$ErrorActionPreference = "Stop"

# --- Percorsi base -------------------------------------------------------
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition   # ...\ExC\Windows
$ProjectDir = Split-Path -Parent (Split-Path -Parent $ScriptDir)      # radice sorgenti (due su: ExC\Windows -> root)
$AppName    = "SurfaceExplorer"
$ZipPath    = Join-Path $ScriptDir "$AppName-$Version-windows-x64.zip"

# --- 1. Trova l'eseguibile Release --------------------------------------
if (-not $ExePath) {
    $candidates = @(
        "build\Desktop_Qt_6_10_2_MinGW_64_bit-Release\$AppName.exe",
        "build\Desktop_Qt_6_10_2-Release\$AppName.exe"
    ) | ForEach-Object { Join-Path $ProjectDir $_ }

    # In fallback, cerca qualsiasi Release build MinGW
    $ExePath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $ExePath) {
        $ExePath = Get-ChildItem -Path (Join-Path $ProjectDir "build") -Recurse -Filter "$AppName.exe" -ErrorAction SilentlyContinue |
                   Where-Object { $_.FullName -match "Release" } |
                   Select-Object -First 1 -ExpandProperty FullName
    }
}

if (-not $ExePath -or -not (Test-Path $ExePath)) {
    Write-Error "Eseguibile Release non trovato. Compila la Release oppure passa -ExePath <percorso>."
    exit 1
}
Write-Host "Eseguibile:  $ExePath" -ForegroundColor Cyan

# --- 2. Verifica gli strumenti Qt ---------------------------------------
$windeployqt = Join-Path $QtBin "windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    Write-Error "windeployqt non trovato in '$QtBin'. Correggi il parametro -QtBin."
    exit 1
}
# Le DLL di Qt e del compilatore devono essere raggiungibili durante il deploy
$env:PATH = "$QtBin;$MinGWBin;$env:PATH"

# --- 3. Cartella di lavoro (temporanea, salvo -KeepFolder) --------------
if ($KeepFolder) {
    $BuildDir = Join-Path $ScriptDir $AppName            # ...\Windows\SurfaceExplorer
} else {
    $BuildDir = Join-Path ([System.IO.Path]::GetTempPath()) ("$AppName-deploy-" + [guid]::NewGuid().ToString('N').Substring(0,8))
}
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Copy-Item -Path $ExePath -Destination (Join-Path $BuildDir "$AppName.exe") -Force

# --- 4. Raccogli le dipendenze Qt + runtime MinGW -----------------------
Write-Host "Esecuzione di windeployqt..." -ForegroundColor Cyan
& $windeployqt `
    --release `
    --compiler-runtime `
    --no-translations `
    --dir $BuildDir `
    (Join-Path $BuildDir "$AppName.exe")

if ($LASTEXITCODE -ne 0) {
    Write-Error "windeployqt e' terminato con errore (codice $LASTEXITCODE)."
    exit $LASTEXITCODE
}

# --- 5. Crea l'archivio .zip pronto per la GitHub Release ----------------
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Write-Host "Creazione archivio $ZipPath..." -ForegroundColor Cyan
# Comprime il contenuto della cartella (l'exe resta alla radice dello zip)
Compress-Archive -Path (Join-Path $BuildDir "*") -DestinationPath $ZipPath -CompressionLevel Optimal

# --- 6. Rimuovi la cartella temporanea (salvo -KeepFolder) --------------
if (-not $KeepFolder) {
    Remove-Item -Recurse -Force $BuildDir
}

# --- Riepilogo -----------------------------------------------------------
$zipMB = [math]::Round(((Get-Item $ZipPath).Length / 1MB), 1)
Write-Host ""
Write-Host "OK - Archivio standalone creato:" -ForegroundColor Green
Write-Host "     $ZipPath  ($zipMB MB)"
if ($KeepFolder) { Write-Host "     Cartella:  $BuildDir" -ForegroundColor Green }
Write-Host ""
Write-Host "Per pubblicarlo su GitHub (upload manuale):" -ForegroundColor Yellow
Write-Host "  1. Apri  https://github.com/<utente>/<repo>/releases/new"
Write-Host "  2. Crea un tag (es. v$Version) e un titolo per la release"
Write-Host "  3. Trascina il file .zip qui sopra nella zona 'Attach binaries'"
Write-Host "  4. Pubblica la release"
if ($zipMB -ge 100) {
    Write-Host "  ATTENZIONE: lo zip supera i 100 MB: come asset di Release va bene," -ForegroundColor Red
    Write-Host "  ma NON caricarlo come file normale nel repo (limite 100 MB per file)." -ForegroundColor Red
}
