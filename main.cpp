#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QSettings>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // 1. GESTIONE SCALING
    // Anche se non hai Retina, lascia queste impostazioni: assicurano
    // che l'app funzioni correttamente se sposti la finestra su un altro monitor.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);

    // 2. CONFIGURAZIONE VERSIONE OPENGL
#ifdef Q_OS_ANDROID
    // Disattiva le maniglie di selezione e il mini-popup taglia/copia/incolla
    // del plugin Android di Qt (lato Java): il menu di modifica completo
    // (con undo, redo, elimina) viene mostrato dall'app nel MobileInputFilter.
    qputenv("QT_QPA_NO_TEXT_HANDLES", "1");

    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(3, 0);
#elif defined(Q_OS_IOS)
    // iOS usa OpenGLES. La versione 3.0 è sicura e supportata da quasi tutti i dispositivi recenti.
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(3, 0);
#elif defined(Q_OS_MAC)
    // --- SPECIFICO PER MAC (M1/M2/M3/M4) ---
    // Apple supporta nativamente solo fino alla 4.1 Core Profile.
    // Chiedere versioni superiori (come la 4.6) farebbe fallire il contesto.
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
#else
    // --- WINDOWS / LINUX ---
    // Qui possiamo spingere al massimo supportato dalla scheda video (4.6)
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CoreProfile);
#endif

    // 3. MIGLIORAMENTO GRAFICO (Fondamentale per schermi Non-Retina)
    // Attiva l'antialiasing 4x per evitare linee seghettate
#ifdef Q_OS_ANDROID
    format.setSamples(0);    // Su Android: Disattiva per evitare crash
#else
    format.setSamples(4);    // Su Desktop: Attiva per bellezza
#endif

    format.setAlphaBufferSize(0);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);

    // Applica come default
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    app.setWindowIcon(QIcon(":/icon.png"));
    app.setDesktopFileName("SurfaceExplorer");
    QCoreApplication::setOrganizationName("Dioscorid");

    // NOME DIVERSO PER I DUE CANALI MACOS, altrimenti le preferenze restano in
    // comune anche con bundle id distinti.
    //
    // QSettings su macOS NON guarda il CFBundleIdentifier: costruisce il dominio
    // da organizationName + applicationName. Con gli stessi due valori, DMG e
    // App Store scrivono entrambi su com.dioscorid.SurfaceExplorer.plist --
    // quindi NON possono puntare a librerie diverse: l'ultima che scrive
    // libraryRootPath vince e l'altra si ritrova la libreria cambiata sotto i
    // piedi (MISURATO con le due installate insieme).
    // Separare il bundle id in CMakeLists non basta: risolve Dock e TCC, non
    // questo. Serve che il nome differisca qui.
    //
    // Il discriminante e' lo stesso di CMakeLists (canale DMG vs App Store), ma
    // qui va letto a RUNTIME dal bundle: il preprocessore non sa da quale
    // generatore si sta compilando.
#if defined(Q_OS_MACOS)
    {
        // Il suffisso ".dmg" nell'id lo mette CMakeLists al solo canale
        // Developer ID: e' l'unica cosa che distingue i due bundle a runtime.
        // Si legge dall'Info.plist del bundle (main.cpp e' C++ puro, niente
        // NSBundle); fuori da un bundle il file non c'e' e la stringa resta
        // vuota, quindi si ricade sul nome storico.
        const QSettings bundleInfo(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/../Info.plist"),
                                   QSettings::NativeFormat);
        const QString id = bundleInfo.value(QStringLiteral("CFBundleIdentifier")).toString();
        QCoreApplication::setApplicationName(id.endsWith(QLatin1String(".dmg"))
                                             ? QStringLiteral("SurfaceExplorer-dmg")
                                             : QStringLiteral("SurfaceExplorer"));
    }
#else
    QCoreApplication::setApplicationName("SurfaceExplorer");
#endif

    MainWindow w;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    w.showMaximized();
#else
    // --- FIX PER WINDOWS ---
    // 1. Resettiamo i flag della finestra per assicurarci che abbia la barra del titolo
    w.setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                     Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                     Qt::WindowCloseButtonHint);

    // 2. Impostiamo una dimensione di partenza ragionevole
    w.resize(1200, 800);

    // 3. Mostriamo la finestra normalmente
    w.showNormal();
#endif

    return app.exec();
}
