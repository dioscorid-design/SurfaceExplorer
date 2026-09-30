#include "scenariotest.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "librarymanager.h"
#include "presetserializer.h"
#include "audiocontroller.h"
#include "surfaceengine.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>

namespace {
const char *kParametricRecord = "records/t_motions/3D/Dynamic Mobius Band.json";
const char *kImplicitRecord   = "records/Ray Marching/Morphing Rosette.json";
const char *kMultiMeshRecord  = "records/Solid Wireframe/Multi Mesh/Brieskorn-Pham (3,5).json";
QString onOff(bool b) { return b ? QStringLiteral("on") : QStringLiteral("off"); }
} // namespace

bool ScenarioTest::requested(const QStringList &args)
{
    return args.contains(QStringLiteral("--scenario-test"));
}

void ScenarioTest::start(MainWindow *mw, const QStringList &args)
{
    const int i = args.indexOf(QStringLiteral("--scenario-test"));
    const QString root = args.value(i + 1);
    const QString out  = args.value(i + 2);
    if (root.isEmpty() || out.isEmpty() || !QDir(root).exists()) {
        qCritical("uso: SurfaceExplorer --scenario-test <radice preset> <cartella uscita>");
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(2); });
        return;
    }
    auto *t = new ScenarioTest(mw, QDir(root).absolutePath(), QDir(out).absolutePath());
    QTimer::singleShot(2000, t, &ScenarioTest::run);
}

ScenarioTest::ScenarioTest(MainWindow *mw, const QString &root, const QString &outDir)
    : QObject(mw), m_mw(mw), m_root(root), m_outDir(outDir)
{
    QDir().mkpath(m_outDir);
    // Popup modali: si annotano e si chiudono (reject = Annulla).
    auto *watcher = new QTimer(this);
    connect(watcher, &QTimer::timeout, this, [this] {
        QWidget *w = QApplication::activeModalWidget();
        if (!w) return;
        QString desc = w->windowTitle();
        if (auto *mb = qobject_cast<QMessageBox *>(w)) desc += QStringLiteral(": ") + mb->text();
        m_lines.append(QStringLiteral("        popup chiuso: ") + desc.simplified());
        if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        else w->close();
    });
    watcher->start(100);
}

void ScenarioTest::wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void ScenarioTest::check(bool ok, const QString &what)
{
    m_lines.append((ok ? QStringLiteral("OK       ") : QStringLiteral("FALLITO  ")) + what);
    if (!ok) ++m_failures;
    qInfo().noquote() << "[scenariotest]" << m_lines.last();
}

bool ScenarioTest::loadRecord(const QString &rel)
{
    LibraryManager lm;
    const LibraryItem item = lm.parseJson(m_root + QLatin1Char('/') + rel, LibraryType::Motion);
    if (item.name.isEmpty()) {
        check(false, QStringLiteral("record non trovato o non caricabile: ") + rel);
        return false;
    }
    m_record = QFileInfo(rel).baseName();
    m_mw->applyMotionExample(item);
    if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
    wait(1500);
    return true;
}

bool ScenarioTest::selectTexture(const QString &rel)
{
    const QList<LibraryItem> &list = m_mw->m_libraryManager.m_textures;
    for (int i = 0; i < list.size(); ++i) {
        if (QDir::fromNativeSeparators(list.at(i).filePath).endsWith(rel)) {
            m_mw->handleTextureSelection(i);
            wait(600);
            return true;
        }
    }
    check(false, QStringLiteral("texture non trovata nella libreria dell'app: ") + rel);
    return false;
}

void ScenarioTest::checkTextureEnabled(const QString &step, bool expectedIntent)
{
    GLWidget *gl = m_mw->ui->glWidget;
    const bool intent  = m_mw->m_surfaceTextureState;
    const bool wire    = (m_mw->m_savedRenderMode == 2);
    const bool onBg    = m_mw->ui->radioBackground->isChecked();
    // AMBITO MESH (fascia selezionata su una multi-mesh): il wireframe globale
    // non spegne la texture globale, e il checkbox mostra la FASCIA -- il suo
    // stato efficace (MeshPart::effectiveTextureEnabledMulti) -- non l'intenzione.
    const int part = gl->activeMeshPart();
    const bool editingOneMesh = part >= 0 && gl->meshPartCount() > 1;
    const bool globalWire = wire && !editingOneMesh;
    const bool engine  = gl->isTextureEnabled();
    const bool chk     = m_mw->ui->chkBoxTexture->isChecked();
    const bool chkOn   = m_mw->ui->chkBoxTexture->isEnabled();

    PresetSerializer::MotionRunState run;
    run.rotating = gl->isAnimating();
    run.path4D   = m_mw->pathTimer && m_mw->pathTimer->isActive();
    run.path3D   = m_mw->pathTimer3D && m_mw->pathTimer3D->isActive();
    const bool saved = m_mw->m_presetSerializer
                           ->captureMotionState(m_record, run, true).textureEnabled;

    QStringList bad;
    if (intent != expectedIntent)
        bad << QStringLiteral("intenzione %1, attesa %2").arg(onOff(intent), onOff(expectedIntent));
    if (engine != (intent && !globalWire))
        bad << QStringLiteral("motore %1 (intenzione %2, wireframe %3)")
                   .arg(onOff(engine), onOff(intent), onOff(globalWire));
    if (!onBg && editingOneMesh) {
        // Fascia in wireframe: la sua texture non si disegna, checkbox spento
        // (come la superficie in wireframe in ambito All).
        const auto &parts = gl->getEngine()->getMeshParts();
        const bool partWire = gl->activeMeshEffectiveRenderMode() == 2;
        const bool eff = !partWire && parts[part].effectiveTextureEnabledMulti();
        if (chk != eff)
            bad << QStringLiteral("checkbox %1, fascia %2 %3%4").arg(onOff(chk)).arg(part + 1)
                       .arg(onOff(eff), partWire ? QStringLiteral(" (in wireframe)") : QString());
    } else if (!onBg) {
        if (wire && (chk || chkOn))
            bad << QStringLiteral("checkbox in wireframe: %1, %2")
                       .arg(onOff(chk), chkOn ? QStringLiteral("abilitato") : QStringLiteral("disabilitato"));
        if (!wire && (!chkOn || chk != intent))
            bad << QStringLiteral("checkbox %1 (%2), intenzione %3")
                       .arg(onOff(chk), chkOn ? QStringLiteral("abilitato") : QStringLiteral("disabilitato"),
                            onOff(intent));
    }
    if (saved != intent)
        bad << QStringLiteral("il Save scriverebbe %1, intenzione %2").arg(onOff(saved), onOff(intent));

    check(bad.isEmpty(), QStringLiteral("%1 -> texture %2%3")
                             .arg(step, onOff(intent),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::run()
{
    Ui::MainWindow *ui = m_mw->ui;
    auto click = [this](QAbstractButton *b) { b->click(); wait(200); };

    // ---------------------------------------------------------------------
    // ACCENSIONE DELLA TEXTURE, superficie parametrica (record in Base).
    m_lines.append(QStringLiteral("== Accensione texture: parametrico (%1) ==")
                       .arg(QString::fromLatin1(kParametricRecord)));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        checkTextureEnabled(QStringLiteral("record caricato"), true);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("spenta dal checkbox"), false);
        click(ui->radioPhong);      checkTextureEnabled(QStringLiteral("poi Phong"), false);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("riaccesa dal checkbox"), true);
        click(ui->radioWF);         checkTextureEnabled(QStringLiteral("poi Wireframe"), true);
        click(ui->radioPhong);      checkTextureEnabled(QStringLiteral("di nuovo Phong"), true);
        click(ui->radioBackground); checkTextureEnabled(QStringLiteral("entrata in Background"), true);
        click(ui->radioSurface);    checkTextureEnabled(QStringLiteral("ritorno a Surface"), true);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("spenta in Phong"), false);
        click(ui->radioWF);         checkTextureEnabled(QStringLiteral("poi Wireframe"), false);
        click(ui->radioBasic);      checkTextureEnabled(QStringLiteral("poi Base"), false);
        click(ui->radioPhong);      checkTextureEnabled(QStringLiteral("poi Phong"), false);
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkTextureEnabled(QStringLiteral("immagine dalla Library"), true);
        click(ui->radioWF);         checkTextureEnabled(QStringLiteral("poi Wireframe"), true);
        if (selectTexture(QStringLiteral("textures/Images/15.png")))
            checkTextureEnabled(QStringLiteral("altra immagine scelta in Wireframe"), true);
        click(ui->radioPhong);      checkTextureEnabled(QStringLiteral("di nuovo Phong"), true);
    }

    // ---------------------------------------------------------------------
    // ACCENSIONE DELLA TEXTURE, Ray Marching.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Accensione texture: Ray Marching (%1) ==")
                       .arg(QString::fromLatin1(kImplicitRecord)));
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        checkTextureEnabled(QStringLiteral("record caricato"), true);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("spenta dal checkbox"), false);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("riaccesa dal checkbox"), true);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("spenta di nuovo"), false);
        if (selectTexture(QStringLiteral("textures/Ray Marching/Fractal Noise FBm.json")))
            checkTextureEnabled(QStringLiteral("texture RM dalla Library"), true);
        m_mw->onStartClicked();
        wait(600);
        checkTextureEnabled(QStringLiteral("Run"), true);
    }

    // ---------------------------------------------------------------------
    // ACCENSIONE DELLA TEXTURE, multi-mesh in ambito Mesh: i gesti sulla fascia
    // non devono toccare l'intenzione ne' il motore globale.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Accensione texture: multi-mesh, ambito Mesh (%1) ==")
                       .arg(QString::fromLatin1(kMultiMeshRecord)));
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        checkTextureEnabled(QStringLiteral("record caricato"), true);
        if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(1);  wait(300);
        checkTextureEnabled(QStringLiteral("fascia 1 selezionata"), true);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("texture della fascia spenta"), true);
        click(ui->chkBoxTexture);   checkTextureEnabled(QStringLiteral("texture della fascia riaccesa"), true);
        ui->spinMeshSel->setValue(4);  wait(300);
        checkTextureEnabled(QStringLiteral("fascia 4 (senza texture propria)"), true);
        click(ui->radioWF);         checkTextureEnabled(QStringLiteral("fascia 4 in Wireframe"), true);
        click(ui->radioBasic);      checkTextureEnabled(QStringLiteral("fascia 4 di nuovo Base"), true);
        m_mw->onStartClicked();  wait(600);
        checkTextureEnabled(QStringLiteral("Run con una fascia selezionata"), true);
        click(ui->radioMeshAll);    checkTextureEnabled(QStringLiteral("ritorno ad All"), true);
        click(ui->radioWF);         checkTextureEnabled(QStringLiteral("All in Wireframe"), true);
        click(ui->radioBasic);      checkTextureEnabled(QStringLiteral("All di nuovo Base"), true);
    }

    finish();
}

void ScenarioTest::finish()
{
    QStringList rep;
    rep << QStringLiteral("TEST DEGLI SCENARI D'USO")
        << QStringLiteral("radice: %1").arg(m_root)
        << QStringLiteral("esito: %1").arg(m_failures == 0 ? QStringLiteral("tutte le verifiche passate")
                                                           : QStringLiteral("%1 verifiche FALLITE").arg(m_failures))
        << QString() << m_lines;
    QFile f(m_outDir + QStringLiteral("/scenario-report.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(rep.join(QLatin1Char('\n')).toUtf8() + '\n');
    qInfo().noquote() << "[scenariotest]" << rep.at(2) << "- report:" << f.fileName();
    QCoreApplication::exit(m_failures == 0 ? 0 : 1);
}
