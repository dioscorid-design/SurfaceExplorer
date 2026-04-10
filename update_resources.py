#!/usr/bin/env python3
import os

# --- CONFIGURAZIONE ---

# 1. Nome del file QRC
QRC_FILENAME = "resources.qrc"

# 2. Mappa dei gruppi: "Prefisso Qt" -> [Lista Cartelle Fisiche]
# 2. Mappa dei gruppi: "Prefisso Qt" -> [Lista Cartelle Fisiche]

RESOURCE_GROUPS = {
    "/library": [
        "presets/surfaces",
        "presets/textures",
        "presets/records",  
        "presets/sounds",
        "shaders"
    ],
    "/": [
        "shaders"
    ]
}

# 3. File singoli principali da includere SEMPRE nel prefisso "/"
STATIC_FILES = [
    "icon.png",
    "background.png",
    "documentation.html",
]

def generate_qrc():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    print(f"📂 Cartella Progetto rilevata: {base_dir}")

    qrc_path = os.path.join(base_dir, QRC_FILENAME)
    qrc_content = '<RCC>\n'
    total_files = 0

    # Itera su ogni gruppo
    for prefix, folders in RESOURCE_GROUPS.items():
        qrc_content += f'    <qresource prefix="{prefix}">\n'

        # --- FIX: Aggiungiamo i file statici se siamo nel prefisso root "/" ---
        if prefix == "/":
            for static_file in STATIC_FILES:
                if os.path.exists(os.path.join(base_dir, static_file)):
                    qrc_content += f'        <file>{static_file}</file>\n'
                    total_files += 1
                else:
                    print(f"⚠️  ATTENZIONE: File statico non trovato: {static_file}")

        for folder in folders:
            abs_folder_path = os.path.join(base_dir, folder)

            if not os.path.exists(abs_folder_path):
                print(f"⚠️  ATTENZIONE: Cartella non trovata: {abs_folder_path}")
                continue
            else:
                print(f"   Scanning: {folder}...")

            for root, dirs, files in os.walk(abs_folder_path):
                for file in files:
                    if file.startswith('.'): continue

                    abs_file_path = os.path.join(root, file)
                    rel_path = os.path.relpath(abs_file_path, base_dir).replace("\\", "/")

                    qrc_content += f'        <file>{rel_path}</file>\n'
                    total_files += 1

        qrc_content += '    </qresource>\n'

    qrc_content += '</RCC>'

    try:
        with open(qrc_path, 'w') as f:
            f.write(qrc_content)
        print(f"✅ Successo! '{QRC_FILENAME}' aggiornato con {total_files} file.")
    except Exception as e:
        print(f"Errore: {e}")

if __name__ == "__main__":
    generate_qrc()
