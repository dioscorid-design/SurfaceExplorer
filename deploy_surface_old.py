import os
import subprocess
import shutil

# --- CONFIGURAZIONE PERCORSI ---
QT_PATH = os.path.expanduser("~/Qt/6.10.2/gcc_64")
CQT_PATH = os.path.expanduser("~/CQtDeployer/1.6/bin/CQtDeployer")
PROJECT_DIR = os.path.expanduser("~/Projects/Surface Explorer")
RELEASE_DIR = os.path.join(PROJECT_DIR, "SurfaceExplorer_Release")

def run_command(command, description):
    print(f"\n>>> {description}...")
    result = subprocess.run(command, shell=True, cwd=PROJECT_DIR)
    if result.returncode != 0:
        print(f"ERRORE: {description} fallito.")
        exit(1)

# 1. COMPILAZIONE
run_command("cmake --build build -j$(nproc)", "Compilazione sorgenti")

# 2. CQTDEPLOYER (Deploy base)
deploy_cmd = (
    f"LD_LIBRARY_PATH=~/CQtDeployer/1.6/lib:~/CQtDeployer/1.6/bin {CQT_PATH} "
    f"-bin build/surface-explorer -qmake {QT_PATH}/bin/qmake "
    f"-targetDir {RELEASE_DIR} -platform xcb -clear 1"
)
run_command(deploy_cmd, "Esecuzione CQtDeployer")

# 3. FIX WAYLAND (Copia plugin e lib mancanti)
print(">>> Applicazione Fix Wayland per Fedora...")
plugins_dest = os.path.join(RELEASE_DIR, "plugins/platforms")
libs_dest = os.path.join(RELEASE_DIR, "lib")

# Copia plugin
for f in os.listdir(os.path.join(QT_PATH, "plugins/platforms")):
    if f.startswith("libqwayland"):
        shutil.copy(os.path.join(QT_PATH, "plugins/platforms", f), plugins_dest)

# Copia librerie (mantenendo i link simbolici con -P via shell)
subprocess.run(f"cp -P {QT_PATH}/lib/libQt6Wayland*.so* {libs_dest}/", shell=True)

# 4. FIX DRIVER AMD (Rimozione librerie in conflitto)
print(">>> Rimozione librerie stdc++/gcc per compatibilità AMD...")
for lib in ["libstdc++.so.6", "libgcc_s.so.1"]:
    target = os.path.join(libs_dest, lib)
    if os.path.exists(target):
        os.remove(target)

# 5. FIX SCRIPT DI AVVIO (Aggiunta export Wayland)
print(">>> Aggiornamento script .sh...")
sh_file = os.path.join(RELEASE_DIR, "surface-explorer.sh")
with open(sh_file, "r") as f:
    lines = f.readlines()

with open(sh_file, "w") as f:
    for line in lines:
        f.write(line)
        if 'BASE_DIR=' in line:
            f.write('export QT_QPA_PLATFORM="wayland;xcb"\n')

# 6. AVVIO TEST FINALE
print("\n=== DEPLOY COMPLETATO CON SUCCESSO ===")
print("Lancio dell'applicazione per il test finale...")
subprocess.run("./surface-explorer.sh", shell=True, cwd=RELEASE_DIR)
