#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>
#include <QWindow>
#include <QScreen>
#include "mainwindow.h"
// Test interni: compilati solo nelle build Debug desktop e in build/Tests
// (SE_SELF_TESTS, vedi CMakeLists.txt e ExC/Mac/Test/build-tests.sh).
#ifdef SE_SELF_TESTS
#include "presetroundtrip.h"
#include "clocktest.h"
#include "scenariotest.h"
#include "imagetest.h"
#include "testactivity.h"
#endif

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
        // Ogni app DMG ha il suo dominio, preso dal numero dopo "surfaceexplorer" nell'id
        // (CMakeLists): "" la 1.x, "2" la 2.0, "21" la 2.1. Cosi' stanno in Applicazioni
        // una accanto all'altra senza contendersi le impostazioni.
        const bool isDmg = id.endsWith(QLatin1String(".dmg"));
        static const QRegularExpression dmgIdRe(QStringLiteral("surfaceexplorer(\\d*)\\.dmg$"));
        const QRegularExpressionMatch idMatch = dmgIdRe.match(id);
        const QString series = idMatch.hasMatch() ? idMatch.captured(1) : QString();
        QCoreApplication::setApplicationName(isDmg ? QStringLiteral("SurfaceExplorer%1-dmg").arg(series)
                                                   : QStringLiteral("SurfaceExplorer"));
        // Primo avvio col dominio nuovo: si riparte dalle impostazioni della versione
        // precedente (cartella della libreria, ultime cartelle dei dialoghi, scelte del
        // REC) invece di chiedere tutto da capo. Da qui in poi le impostazioni sono
        // separate: resta in comune solo la cartella della libreria, finche' si sceglie
        // la stessa. La 2.0 parte dalla 1.x; dalla 2.1 in poi si parte dalla 2.0, o
        // dalla 1.x se la 2.0 non e' mai stata installata.
        if (isDmg && !series.isEmpty()) {
            const QString prefs = QDir::homePath() + QStringLiteral("/Library/Preferences/");
            const QString from1x = prefs + QStringLiteral("com.dioscorid.SurfaceExplorer-dmg.plist");
            const QString from20 = prefs + QStringLiteral("com.dioscorid.SurfaceExplorer2-dmg.plist");
            const QStringList sources = (series == QLatin1String("2")) ? QStringList{ from1x }
                                                                        : QStringList{ from20, from1x };
            // La chiave della 2.0 resta quella di sempre (la sua installazione l'ha gia').
            const QString doneKey = (series == QLatin1String("2")) ? QStringLiteral("settings1xImportDone")
                                                                    : QStringLiteral("settingsImportDone");
            // Non si guarda allKeys(): su macOS QSettings unisce anche i domini piu' larghi
            // (organizzazione, globale), quindi non e' mai vuoto. Basta la cartella della
            // libreria per dire "primo avvio": la chiede sempre, e la versione precedente
            // la ha gia'. Le impostazioni precedenti si leggono dal loro file, non dal
            // dominio, per non portarsi dietro anche le chiavi globali del sistema.
            QSettings mine;
            // Una volta sola: la cartella puo' mancare anche dopo (una radice di sistema
            // si toglie), e ripetere l'import cancellerebbe le scelte fatte qui.
            if (!mine.value(doneKey).toBool()) {
                if (!mine.contains(QStringLiteral("libraryRootPath"))) {
                    for (const QString &file : sources) {
                        const QSettings old(file, QSettings::NativeFormat);
                        if (!old.contains(QStringLiteral("libraryRootPath"))) continue;
                        for (const QString &key : old.allKeys())
                            if (!key.startsWith(QLatin1String("settings")) || !key.endsWith(QLatin1String("ImportDone")))
                                mine.setValue(key, old.value(key));
                        break;
                    }
                }
                mine.setValue(doneKey, true);
                mine.sync();
            }
        }
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

    // 2. Dimensione di partenza: 1320x880, il 3:2 di prima (1200x800)
    //    ingrandito del 10%, perche' la barra di stato entri tutta anche col
    //    REC acceso: la barra di avanzamento la porta da 1073 a 1229px
    //    (misurato sul Mac; con una scena, selettore e orologio, ~1450).
    //    Dove lo spazio libero dello schermo non basta si riduce, stesso
    //    rapporto. I bordi della cornice (barra del titolo) si conoscono solo
    //    con la finestra nativa: winId() la crea.
    w.winId();
    const QMargins frame = w.windowHandle() ? w.windowHandle()->frameMargins() : QMargins();
    QSize startSize(1320, 880);
    if (const QScreen *screen = w.screen()) {
        const QSize room = screen->availableGeometry().size()
                           - QSize(frame.left() + frame.right(), frame.top() + frame.bottom());
        startSize = startSize.scaled(startSize.boundedTo(room), Qt::KeepAspectRatio);
    }
    w.resize(startSize);

    // 3. Mostriamo la finestra normalmente
    w.showNormal();

#ifdef SE_SELF_TESTS
    // Un test qualsiasi: niente App Nap per tutta la sua durata (testactivity.h).
    if (PresetRoundTrip::requested(app.arguments()) || ClockTest::requested(app.arguments())
        || ScenarioTest::requested(app.arguments()) || ImageTest::requested(app.arguments()))
        beginTestActivity();
    // Test di andata e ritorno dei preset (--roundtrip-test): vedi presetroundtrip.h.
    if (PresetRoundTrip::requested(app.arguments()))
        PresetRoundTrip::start(&w, app.arguments());
    // Test degli orologi di animazione (--clock-test): vedi clocktest.h.
    if (ClockTest::requested(app.arguments()))
        ClockTest::start(&w, app.arguments());
    // Test degli scenari d'uso (--scenario-test): vedi scenariotest.h.
    if (ScenarioTest::requested(app.arguments()))
        ScenarioTest::start(&w, app.arguments());
    // Test delle immagini dei preset (--image-test): vedi imagetest.h.
    if (ImageTest::requested(app.arguments()))
        ImageTest::start(&w, app.arguments());
#endif // SE_SELF_TESTS
#endif

    return app.exec();
}
