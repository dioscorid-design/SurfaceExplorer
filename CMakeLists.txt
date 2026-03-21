cmake_minimum_required(VERSION 3.16)
project(SurfaceExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

if(MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()

# 🔹 Trova i moduli necessari di Qt
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Concurrent)
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Network Multimedia)

# 🔹 Includi la cartella sorgente locale
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

qt_standard_project_setup()

qt_add_executable(SurfaceExplorer
    # --- MAIN & UI ---
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    # --- ENGINE & PARSING ---
    surfaceengine.cpp
    surfaceengine.h
    expressionparser.h

    # --- RENDERING & GL ---
    glwidget.cpp
    glwidget.h
    geometrybuilder.cpp
    geometrybuilder.h
    texturemanager.cpp
    texturemanager.h

    # --- UTILITIES & INPUT ---
    uistylemanager.cpp
    uistylemanager.h
    inputhandler.cpp
    inputhandler.h

    # --- NUOVO MANAGER UNIFICATO ---
    librarymanager.cpp
    librarymanager.h
    shaders/common.glsl

    resources.qrc
    shaders/surface.frag
    shaders/surface.vert

    documentation.html
    synthesizer.h
    synthesizer.cpp
    update_resources.py
    glsltranslator.h
    glsltranslator.cpp
    videorecorder.h
    videorecorder.cpp
    librarymenucontroller.h
    librarymenucontroller.cpp
    presetserializer.h
    presetserializer.cpp
    libraryfileoperations.h
    libraryfileoperations.cpp
    librarydragdrophandler.h
    librarydragdrophandler.cpp
    audiocontroller.h
    audiocontroller.cpp
)

set_source_files_properties(documentation.html PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# 🔹 Collega le librerie Qt
target_link_libraries(SurfaceExplorer PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Multimedia
)

# =========================================================
# CONFIGURAZIONE PIATTAFORMA (iOS Device vs macOS)
# =========================================================

if (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")

    # --- CONFIGURAZIONE SPECIFICA PER IPAD FISICO ---
    message(STATUS "Configuring for iOS Device...")

    # 1. Architettura e Versione
    set(CMAKE_OSX_ARCHITECTURES "arm64")   # Forza processore reale (no simulatore)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0")

    set_target_properties(SurfaceExplorer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dioscorid.surface4d"
        MACOSX_BUNDLE_BUNDLE_NAME "Surface Explorer"

        # Identificativo univoco per la firma
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.dioscorid.surface4d"

        # Famiglia dispositivi: 1=iPhone, 2=iPad (1,2 = entrambi)
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"

        # FIRMA: Lasciamo "Automatic" così Xcode usa il tuo account personale
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )

    # 2. Framework di Sistema iOS
    target_link_libraries(SurfaceExplorer PRIVATE
        "-framework Foundation"
        "-framework UIKit"
        "-framework OpenGLES"
        "-framework QuartzCore"
        "-framework CoreGraphics"
    )

elseif (APPLE)
    # Su macOS usiamo i Frameworks.
    target_link_libraries(SurfaceExplorer PRIVATE
        "-framework OpenGL"
    )

    # --- CONFIGURAZIONE NOME APP MACOS ---
    set_target_properties(SurfaceExplorer PROPERTIES
        MACOSX_BUNDLE TRUE

        # 1. Nome nella BARRA DEI MENU (Accanto alla Mela )
        MACOSX_BUNDLE_BUNDLE_NAME "Surface Explorer"

        # 2. Nome dell'icona nel Launchpad/Dock
        MACOSX_BUNDLE_DISPLAY_NAME "Surface Explorer"

        # 3. Nome del file fisico (.app)
        OUTPUT_NAME "Surface Explorer"

        # 4. Identificativo univoco
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dioscorid.surfaceexplorer"

        # 5. Icona (se hai il file .icns, altrimenti usa quella di default)
        # MACOSX_BUNDLE_ICON_FILE "Icon.icns"
    )

    # Copia documentation.html dentro l'app (risorsa)
    set_source_files_properties(documentation.html PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

elseif (UNIX AND NOT ANDROID)
    # Configurazione classica per Linux
    target_link_libraries(SurfaceExplorer PRIVATE GL)

    # Impostiamo il nome dell'eseguibile finale (tutto minuscolo per standard Linux)
    set_target_properties(SurfaceExplorer PROPERTIES OUTPUT_NAME "surface-explorer")

    # Salva i percorsi delle librerie Qt dentro l'eseguibile per farlo avviare dal menu
        set_target_properties(SurfaceExplorer PROPERTIES
            INSTALL_RPATH "${CMAKE_PREFIX_PATH}/lib"
            INSTALL_RPATH_USE_LINK_PATH TRUE
        )

    # 1. Installa l'eseguibile in /usr/local/bin
    install(TARGETS SurfaceExplorer DESTINATION bin)

    # 2. Installa il file .desktop per il menu
    install(FILES SurfaceExplorer.desktop DESTINATION share/applications)

    # 3. Installa l'icona nella cartella di sistema rinominandola come richiesto dal file .desktop
    # (Icon=surface-explorer nel file .desktop cerca surface-explorer.png)
    install(FILES icon.png
            DESTINATION share/icons/hicolor/48x48/apps
            RENAME surface-explorer.png)

    # ------------------------------
endif()

# =========================================================
# CONFIGURAZIONE ORIENTAMENTO E FIRMA (FIX DEFINITIVO)
# =========================================================

if(IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")

    # 1. IL TUO TEAM ID (Quello che hai appena copiato da Xcode)
    set(MY_TEAM_ID "AJ655XKJR8")

    # 2. IL NUOVO NOME DELL'APP (Quello che ha funzionato: v2)
    set(MY_BUNDLE_ID "com.dioscorid.surface4d")

    set_target_properties(SurfaceExplorer PROPERTIES
        # Impostiamo firma e team
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MY_TEAM_ID}"

        # Aggiorniamo l'identificativo ovunque con la versione
        MACOSX_BUNDLE_GUI_IDENTIFIER "${MY_BUNDLE_ID}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MY_BUNDLE_ID}"

        # Usa il file Info.plist (quello che forza il Landscape)
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    )
endif()

if(ANDROID)
    # Imposta la cartella sorgente per il pacchetto Android (dove sta il Manifest)
    set_property(TARGET SurfaceExplorer PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")

    # Configurazioni specifiche per l'APK
    set_target_properties(SurfaceExplorer PROPERTIES
        QT_ANDROID_VERSION_NAME "1.0"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_TARGET_SDK_VERSION 34
        QT_ANDROID_MIN_SDK_VERSION 26
    )

    # Link esplicito alle librerie grafiche Android (Essenziale per SurfaceExplorer)
    target_link_libraries(SurfaceExplorer PRIVATE
        EGL
        GLESv2
    #  Qt6::AndroidExtras
    )
endif()
