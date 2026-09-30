#include "clocktest.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "librarymanager.h"
#include "surfaceengine.h"
#include "audiocontroller.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QTimer>

#include <cmath>

namespace {

// Record della libreria di fabbrica usati dal test (radice/<percorso>).
const char *kAnimatedRecord  = "records/t_motions/3D/Dynamic Mobius Band.json";
const char *kMultiMeshRecord = "records/Solid Wireframe/Multi Mesh/Hopf Tori.json";

QString num(float v) { return QString::number(v, 'f', 4); }

} // namespace

bool ClockTest::requested(const QStringList &args)
{
    return args.contains(QStringLiteral("--clock-test"));
}

void ClockTest::start(MainWindow *mw, const QStringList &args)
{
    const int i = args.indexOf(QStringLiteral("--clock-test"));
    const QString root = args.value(i + 1);
    const QString out  = args.value(i + 2);
    if (root.isEmpty() || out.isEmpty() || !QDir(root).exists()) {
        qCritical("uso: SurfaceExplorer --clock-test <radice preset> <cartella uscita>");
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(2); });
        return;
    }
    auto *t = new ClockTest(mw, QDir(root).absolutePath(), QDir(out).absolutePath());
    // A finestra aperta e avvio concluso, come il test dei preset.
    QTimer::singleShot(2000, t, &ClockTest::run);
}

ClockTest::ClockTest(MainWindow *mw, const QString &root, const QString &outDir)
    : QObject(mw), m_mw(mw), m_root(root), m_outDir(outDir)
{
    QDir().mkpath(m_outDir);

    // Popup modali (immagine mancante, errori): si annotano e si chiudono, o
    // l'exec() fermerebbe il test.
    auto *watcher = new QTimer(this);
    connect(watcher, &QTimer::timeout, this, [this] {
        QWidget *w = QApplication::activeModalWidget();
        if (!w) return;
        QString desc = w->windowTitle();
        if (auto *mb = qobject_cast<QMessageBox *>(w)) desc += QStringLiteral(": ") + mb->text();
        m_lines.append(QStringLiteral("     popup chiuso: ") + desc.simplified());
        if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        else w->close();
    });
    watcher->start(100);
}

void ClockTest::wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void ClockTest::check(bool ok, const QString &what)
{
    m_lines.append((ok ? QStringLiteral("OK    ") : QStringLiteral("FALLITO ")) + what);
    if (!ok) ++m_failures;
    qInfo().noquote() << "[clocktest]" << m_lines.last();
}

bool ClockTest::loadRecord(const QString &rel)
{
    const QString path = m_root + QLatin1Char('/') + rel;
    LibraryManager lm;
    const LibraryItem item = lm.parseJson(path, LibraryType::Motion);
    if (item.name.isEmpty()) {
        check(false, QStringLiteral("record non trovato o non caricabile: ") + rel);
        return false;
    }
    m_mw->applyMotionExample(item);
    if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
    return true;
}

void ClockTest::run()
{
    GLWidget *gl = m_mw->ui->glWidget;
    const float tol = 1e-4f;

    // ---------------------------------------------------------------------
    // SCENA: geometria animata (t nelle equazioni del record), texture FERMATA
    // a mano, sfondo animato.
    if (!loadRecord(QString::fromLatin1(kAnimatedRecord))) return finish();
    wait(800);
    gl->setSurfaceAnimating(true);
    gl->setSurfaceTextureAnimating(false);
    gl->setBackgroundTextureAnimating(true);
    gl->startAnimationTimer();
    wait(300);

    // ---------------------------------------------------------------------
    // 1. DAL VIVO: gli orologi accesi seguono il tempo reale, quello spento no.
    {
        const GLWidget::ClockTimes a = gl->clockTimes();
        QElapsedTimer el; el.start();
        wait(1000);
        const GLWidget::ClockTimes b = gl->clockTimes();
        const float real = el.elapsed() / 1000.0f;
        check(std::fabs((b.geom - a.geom) - real) < 0.15f,
              QStringLiteral("dal vivo la geometria segue il tempo reale (+%1 s in %2 s)")
                  .arg(num(b.geom - a.geom), num(real)));
        check(std::fabs((b.bg - a.bg) - real) < 0.15f,
              QStringLiteral("dal vivo lo sfondo segue il tempo reale (+%1 s)").arg(num(b.bg - a.bg)));
        check(b.tex == a.tex, QStringLiteral("dal vivo la texture fermata resta ferma (%1)").arg(num(b.tex)));
    }

    // ---------------------------------------------------------------------
    // 2. STOP/START DELL'OROLOGIO, come fa il Save Record: niente salti.
    {
        const GLWidget::ClockTimes a = gl->clockTimes();
        gl->setSurfaceAnimating(false);
        gl->stopAnimationTimer();
        wait(500);
        gl->setSurfaceAnimating(true);
        gl->startAnimationTimer();
        const GLWidget::ClockTimes b = gl->clockTimes();
        check(std::fabs(b.geom - a.geom) < 0.1f,
              QStringLiteral("stop/start: la geometria non salta (%1 -> %2)").arg(num(a.geom), num(b.geom)));
        check(b.tex == a.tex,
              QStringLiteral("stop/start: la texture fermata non salta (%1 -> %2)").arg(num(a.tex), num(b.tex)));
    }
    wait(300);

    // ---------------------------------------------------------------------
    // 3. REGISTRAZIONE: stesse chiamate del recorder (videorecorder.cpp).
    const int fps = 30;
    const float ts = 1.0f / fps;
    const int frames = 30;
    GLWidget::ClockTimes t0;
    {
        gl->stopAllTimers();
        t0 = gl->clockTimes();                 // cio' che lo schermo mostra ora
        gl->setClocksDrivenByRecorder(true);

        float maxErrGeom = 0.0f, maxErrTex = 0.0f, maxErrBg = 0.0f, firstGeom = 0.0f;
        bool realTimeIgnored = true;
        for (int i = 0; i < frames; ++i) {
            gl->advanceClocksBy(ts);
            gl->getFrameForVideo(320, 240, true);
            // Tempo che e' arrivato DAVVERO allo shader per questo frame.
            const float geomUbo = gl->m_uboData.time;
            const float texUbo  = gl->m_uboData.dummyZero.x();
            if (i == 0) firstGeom = geomUbo;
            const float expected = t0.geom + (i + 1) * ts;
            maxErrGeom = std::max(maxErrGeom, std::fabs(geomUbo - expected));
            maxErrTex  = std::max(maxErrTex, std::fabs(texUbo - t0.tex));
            maxErrBg   = std::max(maxErrBg, std::fabs(gl->clockTimes().bg - (t0.bg + (i + 1) * ts)));

            if (i == frames / 2) {
                // Il tempo REALE che passa fra due frame (render lenti,
                // repaint dello schermo) non deve contare.
                const GLWidget::ClockTimes before = gl->clockTimes();
                gl->update();
                wait(300);
                const GLWidget::ClockTimes after = gl->clockTimes();
                realTimeIgnored = (before.geom == after.geom && before.bg == after.bg);
            }
        }
        check(std::fabs(firstGeom - (t0.geom + ts)) < tol,
              QStringLiteral("REC: il 1o frame continua dallo schermo (schermo %1, frame %2)")
                  .arg(num(t0.geom), num(firstGeom)));
        check(maxErrGeom < tol,
              QStringLiteral("REC: la geometria avanza di 1/%1 s a frame, velocita' 1x (errore max %2)")
                  .arg(fps).arg(maxErrGeom, 0, 'g', 3));
        check(maxErrTex < tol,
              QStringLiteral("REC: la texture fermata resta ferma nello shader (errore max %1)")
                  .arg(maxErrTex, 0, 'g', 3));
        check(maxErrBg < tol,
              QStringLiteral("REC: lo sfondo avanza di 1/%1 s a frame (errore max %2)")
                  .arg(fps).arg(maxErrBg, 0, 'g', 3));
        check(realTimeIgnored, QStringLiteral("REC: il tempo reale fra i frame non fa avanzare gli orologi"));
    }

    // ---------------------------------------------------------------------
    // 4. FINE REC: lo schermo riprende da dove il video e' arrivato.
    {
        gl->setClocksDrivenByRecorder(false);
        gl->setSurfaceAnimating(true);
        gl->startAnimationTimer();
        const GLWidget::ClockTimes end = gl->clockTimes();
        check(std::fabs(end.geom - (t0.geom + frames * ts)) < tol,
              QStringLiteral("fine REC: la geometria e' all'ultimo frame del video (%1)").arg(num(end.geom)));
        wait(500);
        const GLWidget::ClockTimes after = gl->clockTimes();
        const float d = after.geom - end.geom;
        check(d > 0.3f && d < 0.8f,
              QStringLiteral("fine REC: la geometria prosegue dal vivo senza salti (+%1 s in 0.5 s)").arg(num(d)));
        check(after.tex == t0.tex,
              QStringLiteral("fine REC: la texture fermata e' ancora sul frame di prima del REC (%1)").arg(num(after.tex)));
    }

    // ---------------------------------------------------------------------
    // 5. RESET: gli orologi tornano all'origine (mai t = 0 esatto).
    {
        gl->resetTime();
        const GLWidget::ClockTimes c = gl->clockTimes();
        const float o = GLWidget::kClockOrigin;
        check(c.geom == o && c.tex == o && c.bg == o,
              QStringLiteral("resetTime: orologi all'origine (%1, %2, %3)").arg(c.geom).arg(c.tex).arg(c.bg));
    }

    // ---------------------------------------------------------------------
    // 6. FASCE: ogni parte segue il proprio flag, anche in registrazione.
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        wait(1500);
        SurfaceEngine *eng = gl->getEngine();
        auto &parts = eng->mutableMeshParts();
        if (parts.size() < 2) {
            check(false, QStringLiteral("fasce: il record multi-mesh ha %1 parti").arg(parts.size()));
        } else {
            gl->stopAllTimers();
            parts[0].texAnimating = true;
            parts[1].texAnimating = false;
            const float p0 = parts[0].timeTex, p1 = parts[1].timeTex;
            gl->setClocksDrivenByRecorder(true);
            for (int i = 0; i < frames; ++i) {
                gl->advanceClocksBy(ts);
                gl->getFrameForVideo(320, 240, true);
            }
            gl->setClocksDrivenByRecorder(false);
            check(std::fabs(parts[0].timeTex - (p0 + frames * ts)) < tol,
                  QStringLiteral("fasce: la parte animata avanza col video (+%1 s)").arg(num(parts[0].timeTex - p0)));
            check(parts[1].timeTex == p1,
                  QStringLiteral("fasce: la parte fermata resta ferma (%1)").arg(num(parts[1].timeTex)));
        }
    }

    finish();
}

void ClockTest::finish()
{
    QStringList rep;
    rep << QStringLiteral("TEST DEGLI OROLOGI DI ANIMAZIONE")
        << QStringLiteral("radice: %1").arg(m_root)
        << QStringLiteral("esito: %1").arg(m_failures == 0 ? QStringLiteral("tutte le verifiche passate")
                                                           : QStringLiteral("%1 verifiche FALLITE").arg(m_failures))
        << QString() << m_lines;
    QFile f(m_outDir + QStringLiteral("/clock-report.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(rep.join(QLatin1Char('\n')).toUtf8() + '\n');
    qInfo().noquote() << "[clocktest]" << rep.at(2) << "- report:" << f.fileName();
    QCoreApplication::exit(m_failures == 0 ? 0 : 1);
}
