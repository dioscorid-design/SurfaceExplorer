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
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    GuiPrivate
    Widgets
    Concurrent
    Network
    Multimedia
    ShaderTools
    ShaderToolsPrivate
)
# 🔹 Includi la cartella sorgente locale
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

qt_standard_project_setup()

qt_add_executable(SurfaceExplorer
    resources.qrc

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



    # --- UTILITIES & INPUT ---
    uistylemanager.cpp
    uistylemanager.h
    inputhandler.cpp
    inputhandler.h

    # --- NUOVO MANAGER UNIFICATO ---
    librarymanager.cpp
    librarymanager.h
    shaders/common.glsl


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
    icon.icns
    $<$<PLATFORM_ID:iOS>:${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets>
    shaders/raymarch_template.txt
    geodesiccalculator.h
    geodesiccalculator.cpp
    inputvalidator.h
    inputvalidator.cpp
    shaders/geodesic.glsl
)

set_source_files_properties(documentation.html PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# 🔹 Collega le librerie Qt
target_link_libraries(SurfaceExplorer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::Multimedia
    Qt6::ShaderTools
    Qt6::ShaderToolsPrivate
    Qt6::Concurrent
)

# =========================================================
# CONFIGURAZIONE PIATTAFORMA (iOS Device vs macOS)
# =========================================================

if (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "Configuring for iOS Device...")
    set_target_properties(SurfaceExplorer PROPERTIES QT_PLUGINS "darwinmediaplugin")
    qt_import_plugins(SurfaceExplorer
        INCLUDE Qt6::QDarwinMediaPlugin
        EXCLUDE Qt6::QFFmpegMediaPlugin
    )
    # 1. FORZATURA GLOBALE DELLA FIRMA (Ferma il bulldozer di CMake)
    set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "AJ655XKJR8")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
    set(CMAKE_XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.dioscorid.surfaceexplorer")
    # 2. Architettura e Versione
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0")
    # 3. Proprietà dell'App
    set_target_properties(SurfaceExplorer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dioscorid.surfaceexplorer"
        MACOSX_BUNDLE_BUNDLE_NAME "Surface Explorer"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        VERSION "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "1"
        XCODE_ATTRIBUTE_MARKETING_VERSION "1.0"
    )

    # --- App icon via Asset Catalog ---
    # Copia Assets.xcassets nelle Resources del bundle; Xcode lo compila in
    # Assets.car automaticamente. ASSETCATALOG_COMPILER_APPICON_NAME dice a
    # iOS quale icon-set usare come app icon.
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(SurfaceExplorer PROPERTIES
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    )

    # 4. Framework di Sistema iOS
    target_link_libraries(SurfaceExplorer PRIVATE
        "-framework Foundation"
        "-framework UIKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics"
    )

elseif (APPLE)
    # --- CONFIGURAZIONE NOME APP MACOS ---
    set_target_properties(SurfaceExplorer PROPERTIES
        MACOSX_BUNDLE TRUE

        # 0. FORZIAMO L'USO DEL PLIST PERSONALIZZATO
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"

        # 1. Nome nella BARRA DEI MENU (Accanto alla Mela )
        MACOSX_BUNDLE_BUNDLE_NAME "Surface Explorer"

        # 2. Nome dell'icona nel Launchpad/Dock
        MACOSX_BUNDLE_DISPLAY_NAME "Surface Explorer"

        # 3. Nome del file fisico (.app)
        OUTPUT_NAME "SurfaceExplorer"

        # 4. Identificativo univoco
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dioscorid.surfaceexplorer"

        # 5. Icona (TUTTO MINUSCOLO!)
        MACOSX_BUNDLE_ICON_FILE "icon.icns"
    )

# Copia documentation.html dentro l'app (risorsa)
    set_source_files_properties(documentation.html PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

elseif (UNIX AND NOT ANDROID)
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
endif()

# --- CONFIGURAZIONE NATIVA PER REGISTRAZIONE VIDEO SU iOS ---
if(IOS)
    # Aggiunge il file Objective-C++ alla compilazione
    target_sources(SurfaceExplorer PRIVATE
        nativevideoencoder.h
        nativevideoencoder_ios.mm
    )

    # 1. Framework Apple e librerie di compressione necessarie a FFmpeg
    target_link_libraries(SurfaceExplorer PRIVATE
        "-framework AVFoundation"
        "-framework CoreVideo"
        "-framework CoreMedia"
        "-framework UIKit"
        "-framework Foundation"
        "-framework VideoToolbox"
        "-framework AudioToolbox"
        "-framework Security"
        "-lbz2"
        "-lz"
        "-liconv"
    )

    # 2. Troviamo dinamicamente la cartella "lib" di Qt 6.10
    get_target_property(QT_CORE_LOC Qt6::Core LOCATION)
    get_filename_component(QT_FRAMEWORK_DIR "${QT_CORE_LOC}" DIRECTORY)
    get_filename_component(QT_LIB_DIR "${QT_FRAMEWORK_DIR}" DIRECTORY)
endif()
