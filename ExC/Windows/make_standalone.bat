@echo off
REM Lancia lo script PowerShell che crea l'applicazione Windows indipendente.
REM Doppio clic per generare lo .zip standalone da caricare su GitHub.
REM Opzioni:  -Version 3.1 (nome dello zip)   -KeepFolder (tiene anche la cartella non compressa)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_standalone.ps1" %*
pause
