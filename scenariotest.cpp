#include "scenariotest.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "librarymanager.h"
#include "libraryfileoperations.h"
#include "presetserializer.h"
#include "audiocontroller.h"
#include "shadercompilelog.h"
#include "surfaceengine.h"
#include "presetroundtrip.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTimer>
#include <QAbstractButton>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTabBar>
#include <functional>
#include <QTreeWidgetItemIterator>
#include <QTemporaryDir>

namespace {
const char *kParametricRecord = "records/t_motions/3D/Dynamic Mobius Band.json";
const char *kImplicitRecord   = "records/Ray Marching/Morphing Rosette.json";
const char *kMultiMeshRecord  = "records/Solid Wireframe/Multi Mesh/Brieskorn-Pham (3,5).json";
QString onOff(bool b) { return b ? QStringLiteral("on") : QStringLiteral("off"); }

// La parte GRAFICA del codice di una texture: senza l'audio che viaggia con
// lei (blocchi SOUND_BEGIN..SOUND_END, righe //MUSIC: e //SYNTH:) e, con
// dropImage, senza il tag //IMG:. Stesse regole del Save (captureMotionState).
QString graphicsOf(QString c, bool dropImage)
{
    static const QRegularExpression blockRe(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)",
                                            QRegularExpression::DotMatchesEverythingOption
                                                | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression lineRe(R"(^\s*//\s*(MUSIC:|SYNTH:|SOUND_BEGIN|SOUND_END).*$\n?)",
                                           QRegularExpression::MultilineOption
                                               | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression imgRe(R"(^\s*//IMG:.*$\n?)", QRegularExpression::MultilineOption);
    while (c.contains(blockRe)) c.remove(blockRe);
    c.remove(lineRe);
    if (dropImage) c.remove(imgRe);
    return c.trimmed();
}

QString briefCode(const QString &c)
{
    if (c.isEmpty()) return QStringLiteral("(vuoto)");
    const QString first = c.section(QLatin1Char('\n'), 0, 0).simplified();
    return QStringLiteral("\"%1\" (%2 car.)").arg(first.left(40)).arg(c.size());
}
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
    const int o = args.indexOf(QStringLiteral("--scenario-only"));
    if (o >= 0) t->m_only = args.value(o + 1);
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
        if (auto *mb = qobject_cast<QMessageBox *>(w); mb && !m_popupAnswer.isEmpty()) {
            for (QAbstractButton *b : mb->buttons()) {
                if (b->text() == m_popupAnswer) {
                    m_lines.append(QStringLiteral("        popup, %1: ").arg(m_popupAnswer) + desc.simplified());
                    m_popupAnswer.clear();
                    b->click();
                    return;
                }
            }
        }
        // Il popup "lavoro non salvato" durante una scelta in Library: l'utente
        // che vuole proseguire risponde "Don't save" (Annulla lascerebbe la
        // scena com'era, e il gesto in prova non avverrebbe).
        if (auto *mb = qobject_cast<QMessageBox *>(w); mb && m_discardOnPrompt) {
            for (QAbstractButton *b : mb->buttons()) {
                if (mb->buttonRole(b) == QMessageBox::DestructiveRole) {
                    ++m_discardPrompts;
                    m_lines.append(QStringLiteral("        popup, Don't save: ") + desc.simplified());
                    b->click();
                    return;
                }
            }
        }
        m_lastPopup = desc;
        if (auto *mb = qobject_cast<QMessageBox *>(w)) m_lastPopup += QStringLiteral(" | ") + mb->informativeText();
        if (desc.contains(QLatin1String("slowing down"))) m_watchdogFired = true;
        else ++m_popupsClosed;
        m_lines.append(QStringLiteral("        popup chiuso: ") + desc.simplified());
        if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        else w->close();
    });
    watcher->start(100);

    // Gli errori di compilazione degli shader non aprono sempre un popup: si
    // leggono dal log (vedi shadercompilelog.h) e si raccolgono a ogni verifica.
    ShaderCompileLog::install();

    // SENTINELLA: i gestori agganciati al textChanged dei campi di testo sono
    // pensati per la digitazione (scena modificata, tasti Run da riaccendere,
    // avviso di un altro dock). Load e reset devono scrivere quei campi a
    // segnali bloccati (setEqText, setRmText, setScriptText): un textChanged
    // emesso mentre li riempiono e' una scrittura a segnali vivi.
    Ui::MainWindow *ui = mw->ui;
    for (QPlainTextEdit *e : { ui->lineX, ui->lineY, ui->lineZ, ui->lineP,
                               ui->lineU, ui->lineV, ui->lineW,
                               ui->lineExplicitU, ui->lineExplicitV, ui->lineExplicitW,
                               ui->lnU, ui->lnV, ui->lnW, ui->lndU, ui->lndV, ui->lndW,
                               ui->lineConform, ui->lineEquation, ui->lineEquationCrossSection,
                               ui->lineTexture, ui->lineVariations, ui->txtScriptEditor,
                               ui->lineTubeX, ui->lineTubeY, ui->lineTubeZ, ui->lineTubeP }) {
        if (!e) continue;
        connect(e, &QPlainTextEdit::textChanged, this, [this, e] {
            if (!m_mw->m_populatingFields) return;
            QString section;
            for (int i = m_lines.size() - 1; i >= 0 && section.isEmpty(); --i)
                if (m_lines.at(i).startsWith(QLatin1String("=="))) section = m_lines.at(i);
            const QString entry = QStringLiteral("%1 (%2)").arg(e->objectName(), section);
            if (!m_liveWritesDuringLoad.contains(entry)) m_liveWritesDuringLoad << entry;
        });
    }
}

void ScenarioTest::wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void ScenarioTest::check(bool ok, const QString &what)
{
    collectShaderErrors();   // con la sezione del gesto che li ha prodotti
    m_lines.append((ok ? QStringLiteral("OK       ") : QStringLiteral("FALLITO  ")) + what);
    if (!ok) ++m_failures;
    qInfo().noquote() << "[scenariotest]" << m_lines.last();
}

void ScenarioTest::runTubeScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    SurfaceEngine *eng = gl->getEngine();
    // Il clic vero sulla linguetta: tabBarClicked (la conferma) e poi il cambio.
    auto clickSubTab = [&](int index) {
        emit ui->subTabParametric->tabBar()->tabBarClicked(index);
        ui->subTabParametric->setCurrentIndex(index);
        wait(1500);
    };
    auto fieldCurve = [&] {
        const MainWindow::TubeTexts &t = m_mw->m_scene.tube;
        return QStringList{ t.x, t.y, t.z, t.p };
    };
    auto engineCurve = [&] { return QStringList{ eng->tubeX(), eng->tubeY(), eng->tubeZ(), eng->tubeP() }; };
    auto radiusIs = [&](double thickness) {
        return qAbs(double(gl->tubeRadius()) - thickness * MainWindow::kTubeRadiusUnit) < 1e-4;
    };
    // Lo spessore come la tastiera: testo digitato e conferma (editingFinished).
    auto typeThickness = [&](const QString &text, bool confirm) {
        typeInField(ui->lineTubeThickness, text);
        if (confirm) { emit ui->lineTubeThickness->editingFinished(); wait(300); }
    };

    m_lines.append(QStringLiteral("== Tubi: sotto-tab Tubes del parametrico =="));
    m_discardOnPrompt = true;
    ui->tabModeSelector->setCurrentIndex(0);  wait(1500);

    // LINGUETTA TUBES: il trifoglio di default, gia' a schermo.
    clickSubTab(1);
    const MainWindow::TubeTexts def = MainWindow::defaultTubeTexts();
    check(m_mw->tubesShown() && eng->isTubeModeActive()
              && fieldCurve() == QStringList({ def.x, def.y, def.z, def.p }) && engineCurve() == fieldCurve(),
          QStringLiteral("linguetta Tubes -> il trifoglio di default a schermo, motore = campi"));
    check(m_mw->m_scene.tube.thickness == QLatin1String("1") && radiusIs(1.0)
              && ui->tubeThicknessSlider->value() == 100,
          QStringLiteral("linguetta Tubes -> spessore 1: raggio %1, slider %2")
              .arg(gl->tubeRadius()).arg(ui->tubeThicknessSlider->value()));
    check(!ui->btnRunParametric->isEnabled(),
          QStringLiteral("linguetta Tubes -> Run spento (il tubo a schermo e' gia' applicato)"));
    // isHidden, non isVisible: nel test il dock Equations e' chiuso.
    check(ui->vMinEdit->isHidden() && ui->wMinEdit->isHidden() && !ui->panelTubeThickness->isHidden()
              && ui->uMinEdit->isEnabled(),
          QStringLiteral("linguetta Tubes -> limiti v/w nascosti, Thickness visibile, limiti u accesi"));
    checkDirty(QStringLiteral("linguetta Tubes appena aperta"), false, false, false);

    // SPESSORE: lo slider subito, il campo alla conferma, massimo 3.
    ui->tubeThicknessSlider->setValue(150);  wait(300);
    check(radiusIs(1.5) && ui->lineTubeThickness->text() == QLatin1String("1.5"),
          QStringLiteral("slider dello spessore a 1.5 -> tubo subito, campo '%1'").arg(ui->lineTubeThickness->text()));
    checkDirty(QStringLiteral("spessore dallo slider"), true, false, false);
    typeThickness(QStringLiteral("2.5"), false);
    check(radiusIs(1.5), QStringLiteral("spessore 2.5 digitato e non confermato -> il tubo non cambia"));
    typeThickness(QStringLiteral("2.5"), true);
    check(radiusIs(2.5) && ui->tubeThicknessSlider->value() == 250,
          QStringLiteral("spessore 2.5 confermato -> tubo e slider"));
    // Il massimo e' dello SLIDER, non del campo (come le costanti): un valore
    // scritto piu' grande vale e allarga lo slider; tornati sotto, lo slider
    // torna al suo massimo standard. Sotto il minimo 0.01 si riporta a 0.01.
    typeThickness(QStringLiteral("7"), true);
    check(radiusIs(7.0) && ui->lineTubeThickness->text() == QLatin1String("7")
              && ui->tubeThicknessSlider->maximum() == 700 && ui->tubeThicknessSlider->value() == 700,
          QStringLiteral("spessore 7 confermato -> vale 7, slider allargato (max %1)")
              .arg(ui->tubeThicknessSlider->maximum()));
    typeThickness(QStringLiteral("2"), true);
    check(radiusIs(2.0) && ui->tubeThicknessSlider->maximum() == 300,
          QStringLiteral("spessore 2 confermato -> slider di nuovo a max 3 (max %1)")
              .arg(ui->tubeThicknessSlider->maximum()));
    typeThickness(QStringLiteral("0"), true);
    check(radiusIs(0.01) && ui->lineTubeThickness->text() == QLatin1String("0.01")
              && ui->tubeThicknessSlider->value() == 1,
          QStringLiteral("spessore 0 confermato -> riportato al minimo 0.01 (campo '%1')")
              .arg(ui->lineTubeThickness->text()));
    // Campo svuotato + Invio: torna al default 1 (campo, slider e tubo).
    typeInField(ui->lineTubeThickness, QString());
    pressEnter(ui->lineTubeThickness);
    check(ui->lineTubeThickness->text() == QLatin1String("1") && ui->tubeThicknessSlider->value() == 100
              && radiusIs(1.0),
          QStringLiteral("spessore svuotato, Invio -> riportato a 1 (campo '%1', slider %2)")
              .arg(ui->lineTubeThickness->text()).arg(ui->tubeThicknessSlider->value()));

    // CURVA, a tubo fermo: l'Invio aspetta il Run.
    const QString zNew = QStringLiteral("-sin(3*u)/2");
    ui->lineTubeZ->setPlainText(zNew);  wait(300);
    pressEnter(ui->lineTubeZ);
    check(eng->tubeZ() == def.z, QStringLiteral("curva modificata, Invio a tubo fermo -> a schermo resta quella di prima"));
    check(ui->btnRunParametric->isEnabled(), QStringLiteral("curva modificata -> Run acceso"));
    applyEquationEdit(ui->lineTubeZ);
    check(eng->tubeZ() == zNew && engineCurve() == fieldCurve(), QStringLiteral("Run -> la curva nuova nel motore"));

    // COSTANTE citata solo dalla curva: bloccata fino al Run, poi in uso.
    ui->lineTubeX->setPlainText(QStringLiteral("A*(sin(u) + 2*sin(2*u))/3"));  wait(300);
    check(!ui->aSlider->isEnabled(), QStringLiteral("A scritta nella curva, prima del Run -> A bloccata"));
    applyEquationEdit(ui->lineTubeX);
    check(ui->aSlider->isEnabled(), QStringLiteral("dopo il Run -> A sbloccata"));
    setConstantByField(QStringLiteral("A"), QStringLiteral("1.3"));
    checkConstants(QStringLiteral("A = 1.3 usata dalla curva del tubo"), QMap<QString, double>{ { QStringLiteral("A"), 1.3 } });

    // CURVA ANIMATA: in moto l'Invio applica al volo.
    ui->lineTubeY->setPlainText(QStringLiteral("(cos(u) - 2*cos(2*u))/3 + 0.2*sin(t)"));  wait(300);
    applyEquationEdit(ui->lineTubeY);
    check(m_mw->isEquationModuleMoving(), QStringLiteral("curva con t, Run -> la geometria si anima"));
    const QString yMoving = QStringLiteral("(cos(u) - 2*cos(2*u))/3 + 0.3*sin(t)");
    ui->lineTubeY->setPlainText(yMoving);  wait(300);
    pressEnter(ui->lineTubeY);
    check(eng->tubeY() == yMoving, QStringLiteral("in moto, Invio sulla curva -> applicata al volo"));

    // SAVE E RIAPERTURA (Save Record: la stessa cattura del Save Surface).
    const LibraryItem saved = captureSave();
    check(saved.isTube && saved.tubeX == m_mw->m_scene.tube.x && saved.tubeY == yMoving
              && saved.tubeZ == zNew && saved.tubeThickness == QLatin1String("1"),
          QStringLiteral("Save -> scrive la curva e lo spessore del tubo"));
    check(saved.x.isEmpty() && saved.y.isEmpty() && saved.z.isEmpty() && !saved.isScript,
          QStringLiteral("Save di un tubo -> equazioni di Surface vuote, non uno script"));
    clickSubTab(0);   // si esce dal tubo, poi lo si riapre dal Save
    m_mw->applyMotionExample(saved);  wait(1500);
    check(m_mw->tubesShown() && ui->subTabParametric->currentWidget() == ui->subTabParametricTubes
              && fieldCurve() == QStringList({ saved.tubeX, saved.tubeY, saved.tubeZ, saved.tubeP })
              && engineCurve() == fieldCurve() && radiusIs(1.0),
          QStringLiteral("record tubo riaperto -> linguetta Tubes, curva e spessore com'erano"));
    checkConstants(QStringLiteral("record tubo riaperto"), QMap<QString, double>{ { QStringLiteral("A"), 1.3 } });
    if (m_mw->isEquationModuleMoving()) { ui->btnRunParametric->click();  wait(400); }

    // RITORNO A SURFACE: la superficie di default, i limiti v/w di nuovo.
    clickSubTab(0);
    check(!m_mw->tubesShown() && !eng->isTubeModeActive() && !ui->vMinEdit->isHidden()
              && ui->panelTubeThickness->isHidden(),
          QStringLiteral("ritorno a Surface -> superficie di default, limiti v/w visibili, niente Thickness"));
    m_discardOnPrompt = false;
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
    for (int attempt = 0; attempt < 3; ++attempt) {
        m_watchdogFired = false;
        m_mw->applyMotionExample(item);
        if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
        wait(1500);
        if (!m_watchdogFired) break;
        m_lines.append(QStringLiteral("        (watchdog della GPU durante il load: record ricaricato)"));
        wait(1500);
    }
    return true;
}

bool ScenarioTest::loadSurface(const QString &rel)
{
    LibraryManager lm;
    const LibraryItem item = lm.parseJson(m_root + QLatin1Char('/') + rel, LibraryType::Surface);
    if (item.name.isEmpty()) {
        check(false, QStringLiteral("superficie non trovata o non caricabile: ") + rel);
        return false;
    }
    m_mw->applySurfaceExample(item);
    if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
    wait(1200);
    return true;
}

bool ScenarioTest::selectTexture(const QString &rel)
{
    const QList<LibraryItem> &list = m_mw->m_libraryManager.m_textures;
    for (int i = 0; i < list.size(); ++i) {
        if (!QDir::fromNativeSeparators(list.at(i).filePath).endsWith(rel)) continue;
        // Il click VERO sulla voce dell'albero (onExampleItemClicked, che guarda
        // sender()): conferma del lavoro non salvato, ramo del riclic sulla
        // texture gia' attiva, e solo poi handleTextureSelection. Chiamando
        // quest'ultima direttamente il test provava un percorso che il click
        // non fa (il riclic, per esempio, non ci arriva).
        QTreeWidget *tree = m_mw->ui->treeTextures;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const QVariant v = (*it)->data(0, Qt::UserRole + 1);
            if (!v.isValid() || v.toInt() != i || (*it)->childCount() > 0) continue;
            m_discardOnPrompt = true;
            tree->setCurrentItem(*it);
            emit tree->itemClicked(*it, 0);
            wait(600);
            m_discardOnPrompt = false;
            return true;
        }
        check(false, QStringLiteral("texture senza voce nell'albero della Library: ") + rel);
        return false;
    }
    check(false, QStringLiteral("texture non trovata nella libreria dell'app: ") + rel);
    return false;
}

void ScenarioTest::checkTextureEnabled(const QString &step, bool expectedIntent)
{
    GLWidget *gl = m_mw->ui->glWidget;
    const bool intent  = m_mw->m_scene.surfaceTextureState;
    const bool wire    = (m_mw->m_scene.renderMode == 2);
    const bool onBg    = m_mw->editingBackground();
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
    run.path4D   = m_mw->pathRunning(CameraPaths::Path4D);
    run.path3D   = m_mw->pathRunning(CameraPaths::Path3D);
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
    if (!textureCheckboxProblem().isEmpty()) bad << textureCheckboxProblem();

    check(bad.isEmpty(), QStringLiteral("%1 -> texture %2%3")
                             .arg(step, onOff(intent),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::setTexColorBySliders(bool slot2, const QColor &c)
{
    Ui::MainWindow *ui = m_mw->ui;
    (slot2 ? ui->radioTexColor2 : ui->radioTexColor1)->click();
    wait(100);
    ui->sliderR->setValue(c.red());
    ui->sliderG->setValue(c.green());
    ui->sliderB->setValue(c.blue());
    wait(200);
}

void ScenarioTest::checkTexColors(const QString &step, const QColor &global1, const QColor &global2,
                                  const QColor &shown1, const QColor &shown2)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const QColor g1 = gl->globalTexColor1(), g2 = gl->globalTexColor2();

    // Colori EFFICACI del bersaglio, come li disegna il motore: una fascia con
    // colori propri usa quelli, le altre (e la superficie in ambito All) i due
    // slot globali.
    QColor e1 = g1, e2 = g2;
    const int part = gl->activeMeshPart();
    const auto &parts = gl->getEngine()->getMeshParts();
    if (part >= 0 && part < (int)parts.size() && parts[part].hasCustomTexColors()) {
        const MeshPart &p = parts[part];
        e1 = QColor::fromRgbF(p.texCol1R, p.texCol1G, p.texCol1B);
        e2 = QColor::fromRgbF(p.texCol2R, p.texCol2G, p.texCol2B);
    }
    const QColor s1 = m_mw->surfaceTexColor(1), s2 = m_mw->surfaceTexColor(2);

    PresetSerializer::MotionRunState run;
    run.rotating = gl->isAnimating();
    run.path4D   = m_mw->pathRunning(CameraPaths::Path4D);
    run.path3D   = m_mw->pathRunning(CameraPaths::Path3D);
    const LibraryItem saved = m_mw->m_presetSerializer->captureMotionState(m_record, run, true);

    QStringList bad;
    if (global1.isValid() && (g1.name() != global1.name() || g2.name() != global2.name()))
        bad << QStringLiteral("globali %1 %2, attesi %3 %4")
                   .arg(g1.name(), g2.name(), global1.name(), global2.name());
    const QColor x1 = shown1.isValid() ? shown1 : global1, x2 = shown1.isValid() ? shown2 : global2;
    if (x1.isValid() && (e1.name() != x1.name() || e2.name() != x2.name()))
        bad << QStringLiteral("il motore disegna %1 %2, attesi %3 %4")
                   .arg(e1.name(), e2.name(), x1.name(), x2.name());
    if (!m_mw->editingBackground() && (s1.name() != e1.name() || s2.name() != e2.name()))
        bad << QStringLiteral("picker %1 %2, il motore disegna %3 %4%5")
                   .arg(s1.name(), s2.name(), e1.name(), e2.name(),
                        part >= 0 ? QStringLiteral(" (fascia %1)").arg(part + 1) : QString());
    // Gli slider mostrano lo slot scelto quando editano davvero i colori della
    // texture (stessa condizione di onColorTargetChanged).
    const bool texHere = part >= 0 ? gl->activeMeshTextureActive() : m_mw->m_scene.surfaceTextureState;
    if (!m_mw->editingBackground() && !ui->radioWF->isChecked() && texHere
        && m_mw->activeTextureUsesColors()) {
        const QColor sl(ui->sliderR->value(), ui->sliderG->value(), ui->sliderB->value());
        const QColor want = ui->radioTexColor2->isChecked() ? e2 : e1;
        if (sl.name() != want.name())
            bad << QStringLiteral("slider %1, colore %2 %3").arg(sl.name())
                       .arg(ui->radioTexColor2->isChecked() ? 2 : 1).arg(want.name());
    }
    if (saved.texColor1 != g1.name() || saved.texColor2 != g2.name())
        bad << QStringLiteral("il Save scriverebbe %1 %2, globali %3 %4")
                   .arg(saved.texColor1, saved.texColor2, g1.name(), g2.name());

    check(bad.isEmpty(), QStringLiteral("%1 -> colori %2 %3%4")
                             .arg(step, e1.name(), e2.name(),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::checkBackground(const QString &step, const QString &expectedImage)
{
    GLWidget *gl = m_mw->ui->glWidget;
    const bool on = gl->isBackgroundTextureEnabled();
    const QString enginePath = gl->backgroundImagePath();
    const bool engineDefault = enginePath.isEmpty() || enginePath == QLatin1String("background.png");
    const QString shown = engineDefault ? QString() : QFileInfo(enginePath).fileName();

    PresetSerializer::MotionRunState run;
    run.rotating = gl->isAnimating();
    run.path4D   = m_mw->pathRunning(CameraPaths::Path4D);
    run.path3D   = m_mw->pathRunning(CameraPaths::Path3D);
    const LibraryItem saved = m_mw->m_presetSerializer->captureMotionState(m_record, run, true);
    static const QRegularExpression imgRe(QStringLiteral(R"(^\s*//IMG:\s*(.*)$)"),
                                          QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = imgRe.match(saved.bgTextureCode);
    const QString savedImage = m.hasMatch() ? QFileInfo(m.captured(1).trimmed()).fileName() : QString();
    // Lo SCRIPT dello sfondo (editor o slot) dice quale immagine usa.
    const QRegularExpressionMatch ms = imgRe.match(m_mw->backgroundTextureScript());
    const QString scriptImage = ms.hasMatch() ? QFileInfo(ms.captured(1).trimmed()).fileName() : QString();

    auto name = [](const QString &f) { return f.isEmpty() ? QStringLiteral("default") : f; };
    QStringList bad;
    if (saved.bgTextureEnabled != on)
        bad << QStringLiteral("il Save scriverebbe enabled %1, motore %2")
                   .arg(onOff(saved.bgTextureEnabled), onOff(on));
    if (!on) {
        if (!saved.bgTextureCode.trimmed().isEmpty())
            bad << QStringLiteral("sfondo spento ma il Save scriverebbe il codice \"%1\"")
                       .arg(saved.bgTextureCode.simplified().left(60));
        if (!saved.bgLibName.isEmpty())
            bad << QStringLiteral("sfondo spento ma il Save scriverebbe l'ancora \"%1\"").arg(saved.bgLibName);
        if (!engineDefault)
            bad << QStringLiteral("sfondo spento con %1 in GPU: riaccendendo ricomparirebbe").arg(shown);
    } else {
        // L'immagine nel motore e' quella che si salva e che lo script nomina,
        // anche sotto uno script (che puo' campionarla da iChannel0).
        if (savedImage != shown)
            bad << QStringLiteral("a schermo %1, il Save scriverebbe %2").arg(name(shown), name(savedImage));
        if (scriptImage != shown)
            bad << QStringLiteral("a schermo %1, lo script dice %2").arg(name(shown), name(scriptImage));
    }
    if (!expectedImage.isNull() && on && shown != expectedImage)
        bad << QStringLiteral("a schermo %1, atteso %2").arg(name(shown), name(expectedImage));

    check(bad.isEmpty(), QStringLiteral("%1 -> sfondo %2%3%4")
                             .arg(step, onOff(on),
                                  on ? QStringLiteral(", ") + name(shown) : QString(),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::checkSurfaceControls(const QString &step)
{
    Ui::MainWindow *ui = m_mw->ui;
    const bool onBg = m_mw->editingBackground();
    const bool rm   = ui->tabModeSelector->currentIndex() == 1;
    const bool wire = !rm && ui->radioWF->isChecked();

    // isEnabled() tiene conto dei contenitori: e' cio' che l'utente puo' cliccare.
    struct Want { const char *name; QWidget *w; bool on; };
    const QList<Want> wants = {
        { "Base",               ui->radioBasic,      !onBg },
        { "Phong",              ui->radioPhong,      !onBg },
        { "Wireframe",          ui->radioWF,         !onBg && !rm },
        { "densita' wireframe", ui->btnWireUPlus,    !onBg && wire },
        { "Light",              ui->lightSlider,     !onBg && !wire },
        { "Headlight",          ui->fillLightSlider, !onBg && rm },
        { "FOV",                ui->fovSliderMain,   true },
        // Gli slider RGB no: in Background seguono la texture dello sfondo
        // (spenti se e' un'immagine, che non usa colori) -- onColorTargetChanged.
    };
    QStringList bad;
    for (const Want &x : wants)
        if (x.w && x.w->isEnabled() != x.on)
            bad << QStringLiteral("%1 %2 (atteso %3)").arg(QString::fromLatin1(x.name),
                                                           x.w->isEnabled() ? QStringLiteral("acceso") : QStringLiteral("spento"),
                                                           x.on ? QStringLiteral("acceso") : QStringLiteral("spento"));
    // Trasparenza: spenta in Background; su Surface ha anche la guardia dei
    // campi a prodotto in RM (syncImplicitAlphaSlider), quindi solo il caso spento.
    if (onBg && ui->alphaSlider->isEnabled())
        bad << QStringLiteral("Transparency accesa (attesa spenta)");
    const QString light = lightProblem();
    if (!light.isEmpty()) bad << light;

    check(bad.isEmpty(), QStringLiteral("%1 -> comandi superficie %2%3")
                             .arg(step, onBg ? QStringLiteral("spenti") : QStringLiteral("accesi"),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

QString ScenarioTest::presetDisplacement(const QString &rel, LibraryType type)
{
    LibraryManager lm;
    return lm.parseJson(m_root + QLatin1Char('/') + rel, type).displacementCode;
}

void ScenarioTest::checkDisplacement(const QString &step, const QString &expected, bool pendingEdit)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const QString field  = ui->lineVariations->toPlainText().trimmed();
    const QString engine = gl->currentDisplacementCode().trimmed();
    const bool rm = ui->tabModeSelector->currentIndex() == 1;

    PresetSerializer::MotionRunState run;
    run.rotating = gl->isAnimating();
    run.path4D   = m_mw->pathRunning(CameraPaths::Path4D);
    run.path3D   = m_mw->pathRunning(CameraPaths::Path3D);
    const QString saved = m_mw->m_presetSerializer
                              ->captureMotionState(m_record, run, true).displacementCode.trimmed();

    auto brief = [](const QString &c) {
        if (c.isEmpty()) return QStringLiteral("(vuoto)");
        const QString first = c.section(QLatin1Char('\n'), 0, 0).simplified();
        return QStringLiteral("\"%1\" (%2 car.)").arg(first.left(40)).arg(c.size());
    };

    QStringList bad;
    if (!expected.isNull() && field != expected.trimmed())
        bad << QStringLiteral("campo %1, atteso %2").arg(brief(field), brief(expected.trimmed()));
    if (!pendingEdit && engine != field)
        bad << QStringLiteral("motore %1, campo %2").arg(brief(engine), brief(field));
    if (!rm && (!engine.isEmpty() || !field.isEmpty()))
        bad << QStringLiteral("in parametrico: motore %1, campo %2").arg(brief(engine), brief(field));
    if (rm && saved != field)
        bad << QStringLiteral("il Save scriverebbe %1, campo %2").arg(brief(saved), brief(field));

    check(bad.isEmpty(), QStringLiteral("%1 -> displacement %2%3")
                             .arg(step, brief(field),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

LibraryItem ScenarioTest::captureSave()
{
    GLWidget *gl = m_mw->ui->glWidget;
    PresetSerializer::MotionRunState run;
    run.rotating = gl->isAnimating();
    run.path4D   = m_mw->pathRunning(CameraPaths::Path4D);
    run.path3D   = m_mw->pathRunning(CameraPaths::Path3D);
    return m_mw->m_presetSerializer->captureMotionState(m_record, run, true);
}

QString ScenarioTest::presetTextureCode(const QString &rel, LibraryType type, bool dropImage)
{
    LibraryManager lm;
    const LibraryItem it = lm.parseJson(m_root + QLatin1Char('/') + rel, type);
    // Le texture di libreria parametriche portano il codice in scriptCode, quelle
    // Ray Marching in textureCode (scriptCode e' il ripiego dei preset vecchi):
    // stessa scelta di handleTextureSelection.
    return graphicsOf(it.textureCode.isEmpty() ? it.scriptCode : it.textureCode, dropImage);
}

bool ScenarioTest::selectSound(const QString &rel)
{
    const QList<LibraryItem> &list = m_mw->m_libraryManager.m_sounds;
    for (int i = 0; i < list.size(); ++i) {
        if (QDir::fromNativeSeparators(list.at(i).filePath).endsWith(rel)) {
            // Come un click sulla voce dell'albero Sounds: l'indice viaggia nel ruolo UserRole+3.
            QTreeWidgetItem item;
            item.setData(0, Qt::UserRole + 3, i);
            m_mw->onSoundItemClicked(&item, 0);
            if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
            wait(400);
            return true;
        }
    }
    check(false, QStringLiteral("suono non trovato nella libreria dell'app: ") + rel);
    return false;
}

void ScenarioTest::pressEnter(QWidget *w)
{
    // Il tasto Invio vero: lo vede il filtro dei campi (EnterApplyFilter) e, per
    // i QLineEdit, parte anche editingFinished.
    w->setFocus();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &press);
    QCoreApplication::sendEvent(w, &release);
    wait(800);
}

void ScenarioTest::typeInField(QLineEdit *l, const QString &text)
{
    // Digitazione vera (textEdited): setText per Qt e' una scrittura del
    // programma, e i campi limite applicano con Invio solo cio' che e' stato
    // digitato.
    l->setFocus();
    l->selectAll();
    l->insert(text);
    wait(100);
}

void ScenarioTest::applyEquationEdit(QWidget *field)
{
    // Come fa l'utente: con l'animazione in corso il tasto del dock dice Stop e
    // la modifica si applica con Invio nel campo; da fermo l'Invio la lascia in
    // sospeso e si applica col tasto Run.
    QPushButton *run = m_mw->ui->btnRunParametric;
    if (m_mw->isEquationModuleMoving()) {
        pressEnter(field);
    } else if (run->isEnabled()) {
        run->click();
        wait(800);
    } else {
        check(false, QStringLiteral("modifica alle equazioni ma tasto Run spento e animazione ferma"));
    }
}

void ScenarioTest::pressNew()
{
    m_discardOnPrompt = true;
    m_mw->onNewSceneClicked();
    wait(600);
    m_discardOnPrompt = false;
}

void ScenarioTest::setScriptMode(int mode)
{
    for (int i = 0; i < 3 && m_mw->m_currentScriptMode != mode; ++i) {
        m_mw->onToggleScriptMode();
        wait(100);
    }
}

void ScenarioTest::checkTextureCode(const QString &step, const QString &expected, bool pendingEdit)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const bool rm = ui->tabModeSelector->currentIndex() == 1;
    const QString saved = graphicsOf(captureSave().textureCode, /*dropImage=*/false);
    const QString rmField  = ui->lineTexture->toPlainText().trimmed();
    const QString rmEngine = gl->currentTextureCode().trimmed();

    QStringList bad;
    QString shown;
    if (rm) {
        // RAY MARCHING: il campo lineTexture e' l'intenzione (il Save lo scrive
        // anche non eseguito, come ogni editor), il motore l'applicato.
        shown = rmField;
        if (!expected.isNull() && rmField != expected.trimmed())
            bad << QStringLiteral("campo %1, atteso %2").arg(briefCode(rmField), briefCode(expected.trimmed()));
        if (!pendingEdit && rmEngine != rmField)
            bad << QStringLiteral("motore %1, campo %2").arg(briefCode(rmEngine), briefCode(rmField));
        if (saved != rmField)
            bad << QStringLiteral("il Save scriverebbe %1, campo %2").arg(briefCode(saved), briefCode(rmField));
    } else {
        // PARAMETRICO: l'intenzione e' lo script della texture di superficie (il
        // suo slot, di cui l'editor e' la vista). L'applicato e'
        // m_scene.surfaceTextureCode, e il motore deve compilare quello (senza codice:
        // la scacchiera di default, o niente sopra un'immagine).
        if (!rmField.isEmpty() || !rmEngine.isEmpty())
            bad << QStringLiteral("in parametrico: campo RM %1, motore RM %2")
                       .arg(briefCode(rmField), briefCode(rmEngine));
        const QString intent = graphicsOf(m_mw->surfaceTextureScript(), true);
        const QString applied = graphicsOf(m_mw->m_scene.surfaceTextureCode, true);
        const QString engine  = graphicsOf(gl->currentParametricTextureCode(), true);
        shown = intent;
        if (!expected.isNull() && intent != expected.trimmed())
            bad << QStringLiteral("editor %1, atteso %2").arg(briefCode(intent), briefCode(expected.trimmed()));
        if (!pendingEdit && applied != intent)
            bad << QStringLiteral("applicata %1, editor %2").arg(briefCode(applied), briefCode(intent));
        if (m_mw->m_scene.surfaceTextureState) {
            const QString want = !applied.isEmpty() ? applied
                               : m_mw->surfaceHasImage() ? QString()
                                                     : TextureCode::defaultMeshCode();
            if (engine != want)
                bad << QStringLiteral("motore %1, applicata %2").arg(briefCode(engine), briefCode(want));
        } else if (!engine.isEmpty()) {
            // Texture spenta: il motore non deve tenersi compilato il codice di
            // prima (come il rilievo e la texture Ray Marching fuori dal loro modo).
            bad << QStringLiteral("texture spenta ma il motore compila ancora %1").arg(briefCode(engine));
        }
        if (graphicsOf(saved, true) != intent)
            bad << QStringLiteral("il Save scriverebbe %1, editor %2")
                       .arg(briefCode(graphicsOf(saved, true)), briefCode(intent));
    }

    if (!scriptEditorProblem().isEmpty()) bad << scriptEditorProblem();
    if (!rmFieldsProblem().isEmpty()) bad << rmFieldsProblem();

    check(bad.isEmpty(), QStringLiteral("%1 -> codice %2%3")
                             .arg(step, briefCode(shown),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

QString ScenarioTest::rmFieldsProblem() const
{
    // I quattro campi Ray Marching sono l'editor dello stato m_scene.rm: stesso testo.
    Ui::MainWindow *ui = m_mw->ui;
    struct F { const char *name; QPlainTextEdit *edit; const QString &state; };
    const F fs[] = { { "equazione", ui->lineEquation, m_mw->m_scene.rm.equation },
                     { "Cross Section", ui->lineEquationCrossSection, m_mw->m_scene.rm.crossSection },
                     { "texture", ui->lineTexture, m_mw->m_scene.rm.texture },
                     { "rilievo", ui->lineVariations, m_mw->m_scene.rm.displacement } };
    for (const F &f : fs) {
        if (f.edit && f.edit->toPlainText() != f.state)
            return QStringLiteral("Ray Marching, %1: lo stato ha %2, il campo %3")
                .arg(QString::fromLatin1(f.name), briefCode(f.state), briefCode(f.edit->toPlainText()));
    }
    return QString();
}

QString ScenarioTest::lineFieldsProblem() const
{
    QStringList bad;
    for (const auto &f : m_mw->lineFieldTable()) {
        if (!f.first) continue;
        if (*f.second != f.first->text())
            bad << QStringLiteral("%1: stato \"%2\", campo \"%3\"")
                       .arg(f.first->objectName(), *f.second, f.first->text());
    }
    return bad.join(QStringLiteral("; "));
}

QString ScenarioTest::scriptEditorProblem() const
{
    // L'editor del dock Script e' la VISTA dello slot che il dock mostra:
    // stesso testo, a meno degli a capo che il widget normalizza.
    auto canon = [](QString t) { t.replace(QLatin1String("\r\n"), QLatin1String("\n")); return t; };
    const QString shown = canon(m_mw->ui->txtScriptEditor->toPlainText());
    const QString slot  = canon(m_mw->scriptText(m_mw->shownScriptSlot()));
    if (shown == slot) return QString();
    return QStringLiteral("l'editor del dock Script mostra %1, il suo slot ha %2")
        .arg(briefCode(shown), briefCode(slot));
}

void ScenarioTest::setConstantBySlider(const QString &letter, double value)
{
    Ui::MainWindow *ui = m_mw->ui;
    const QMap<QString, QSlider *> sliders = {
        { QStringLiteral("A"), ui->aSlider }, { QStringLiteral("B"), ui->bSlider },
        { QStringLiteral("C"), ui->cSlider }, { QStringLiteral("D"), ui->dSlider },
        { QStringLiteral("E"), ui->eSlider }, { QStringLiteral("F"), ui->fSlider },
        { QStringLiteral("S"), ui->sSlider } };
    QSlider *s = sliders.value(letter);
    if (!s) return;
    // Come il dito: trascina (valueChanged) e rilascia (sliderReleased).
    s->setValue(qRound(value * 100.0));
    emit s->sliderReleased();
    wait(400);
}

void ScenarioTest::setConstantByField(const QString &letter, const QString &text)
{
    Ui::MainWindow *ui = m_mw->ui;
    const QMap<QString, QLineEdit *> lines = {
        { QStringLiteral("A"), ui->lineA }, { QStringLiteral("B"), ui->lineB },
        { QStringLiteral("C"), ui->lineC }, { QStringLiteral("D"), ui->lineD },
        { QStringLiteral("E"), ui->lineE }, { QStringLiteral("F"), ui->lineF },
        { QStringLiteral("S"), ui->lineS } };
    QLineEdit *l = lines.value(letter);
    if (!l) return;
    // Come la tastiera: testo e Invio (editingFinished).
    l->setText(text);
    emit l->editingFinished();
    wait(600);
}

void ScenarioTest::checkConstants(const QString &step, const QMap<QString, double> &expected)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    struct K { const char *letter; QLineEdit *line; QSlider *slider; float field; float saved; };
    const MainWindow::CascadeConstants k = m_mw->resolveCascadeConstants(false);
    const LibraryItem sv = captureSave();
    const QList<K> ks = {
        { "A", ui->lineA, ui->aSlider, k.a, sv.a }, { "B", ui->lineB, ui->bSlider, k.b, sv.b },
        { "C", ui->lineC, ui->cSlider, k.c, sv.c }, { "D", ui->lineD, ui->dSlider, k.d, sv.d },
        { "E", ui->lineE, ui->eSlider, k.e, sv.e }, { "F", ui->lineF, ui->fSlider, k.f, sv.f },
        { "S", ui->lineS, ui->sSlider, k.s, sv.s } };
    const QMap<QString, float> &engine = gl->getConstantsMap();
    auto num = [](double v) { return QString::number(v, 'g', 6); };

    QStringList bad, shown;
    for (const K &c : ks) {
        const QString L = QString::fromLatin1(c.letter);
        const bool isS = L == QLatin1String("S");
        const double def = isS ? 0.0 : 1.0;
        // Il MOTORE usa il valore del campo.
        const float e = engine.value(L, float(def));
        if (qAbs(e - c.field) > 1e-4f)
            bad << QStringLiteral("%1: motore %2, campo %3").arg(L, num(e), num(c.field));
        // Lo SLIDER mostra il campo (passo 0.01; puo' saturare ai suoi estremi).
        const double sl = c.slider->value() / 100.0;
        const bool saturated = c.slider->value() == c.slider->minimum()
                               || c.slider->value() == c.slider->maximum();
        if (qAbs(sl - c.field) > 0.0101 && !saturated)
            bad << QStringLiteral("%1: slider %2, campo %3").arg(L, num(sl), num(c.field));
        // Il SAVE scrive il campo.
        if (qAbs(c.saved - c.field) > 1e-5f)
            bad << QStringLiteral("%1: il Save scriverebbe %2, campo %3").arg(L, num(c.saved), num(c.field));
        // Campo e slider si accendono e spengono insieme; una costante SPENTA
        // (nessun modulo la usa) sta al suo valore neutro.
        if (c.line->isEnabled() != c.slider->isEnabled())
            bad << QStringLiteral("%1: campo %2, slider %3").arg(L, onOff(c.line->isEnabled()), onOff(c.slider->isEnabled()));
        if (!c.line->isEnabled() && qAbs(c.field - def) > 1e-6)
            bad << QStringLiteral("%1 spenta ma vale %2").arg(L, num(c.field));
        if (expected.contains(L) && qAbs(expected.value(L) - c.field) > 1e-4)
            bad << QStringLiteral("%1 vale %2, atteso %3").arg(L, num(c.field), num(expected.value(L)));
        if (c.line->isEnabled()) shown << QStringLiteral("%1=%2").arg(L, num(c.field));
    }
    // Lo STATO (m_scene.constants) e' cio' che i campi mostrano.
    for (MainWindow::ConstField f : MainWindow::constantFields()) {
        const QString t = m_mw->constantFieldEdit(f)->text();
        if (m_mw->m_scene.constants.*f != t)
            bad << QStringLiteral("%1: stato \"%2\", campo \"%3\"")
                       .arg(MainWindow::constantName(f), m_mw->m_scene.constants.*f, t);
    }

    const QString steps = stepsProblem();
    if (!steps.isEmpty()) bad << steps;

    check(bad.isEmpty(), QStringLiteral("%1 -> costanti %2%3")
                             .arg(step, shown.isEmpty() ? QStringLiteral("(nessuna in uso)")
                                                        : shown.join(QLatin1Char(' ')),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

QString ScenarioTest::masterState() const
{
    const MainWindow::MasterActivity a = m_mw->masterActivity();
    QStringList off;
    if (a.eqAvailable && !a.eqRunning)         off << QStringLiteral("equazioni");
    if (a.texAvailable && !a.texRunning)       off << QStringLiteral("texture");
    if (a.bgAvailable && !a.bgRunning)         off << QStringLiteral("sfondo");
    if (a.cameraAvailable && !a.cameraRunning) off << QStringLiteral("camera");
    if (a.audioAvailable && !a.audioRunning)   off << QStringLiteral("suono");
    const QString label = m_mw->m_btnStart ? m_mw->m_btnStart->text().toUpper() : QString();
    return off.isEmpty() ? label : label + QStringLiteral(", spenti: ") + off.join(QStringLiteral(", "));
}

QString ScenarioTest::lightProblem()
{
    // LUCE: globale, l'unica copia e' nel motore; lo slider la mostra (anche in
    // ambito "Mesh") e il Save la scrive.
    Ui::MainWindow *ui = m_mw->ui;
    const float engine = ui->glWidget->globalLightIntensity();
    QStringList bad;
    if (ui->lightSlider->value() != qRound(engine * 100.0f))
        bad << QStringLiteral("luce: slider %1, motore %2").arg(ui->lightSlider->value()).arg(engine);
    const double saved = captureSave().lightIntensity;
    if (qAbs(saved - engine) > 0.006)
        bad << QStringLiteral("luce: il Save scriverebbe %1, motore %2").arg(saved).arg(engine);
    return bad.join(QStringLiteral("; "));
}

QString ScenarioTest::stepsProblem()
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const int slider = ui->stepSlider->value();
    QStringList bad;
    if (m_mw->m_scene.steps != slider)
        bad << QStringLiteral("Steps: stato %1, slider %2").arg(m_mw->m_scene.steps).arg(slider);
    if (ui->lineSteps->text().trimmed() != QString::number(slider))
        bad << QStringLiteral("campo Steps \"%1\", slider %2").arg(ui->lineSteps->text()).arg(slider);
    const bool rm = ui->tabModeSelector->currentIndex() == 1;
    const int engine = rm ? gl->raySteps() : gl->getEngine()->getNumU();
    if (engine != slider)
        bad << QStringLiteral("%1 nel motore %2, slider %3")
                   .arg(rm ? QStringLiteral("Ray Steps") : QStringLiteral("risoluzione"))
                   .arg(engine).arg(slider);
    const int saved = captureSave().steps;
    if (saved != slider)
        bad << QStringLiteral("il Save scriverebbe steps %1, slider %2").arg(saved).arg(slider);
    return bad.join(QStringLiteral("; "));
}

void ScenarioTest::checkMotion(const QString &step, const QString &expectRunning, bool pendingEdit)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    SurfaceEngine *engine = gl->getEngine();
    const LibraryItem sv = captureSave();
    QStringList bad;

    const bool t4 = m_mw->pathRunning(CameraPaths::Path4D);
    const bool t3 = m_mw->pathRunning(CameraPaths::Path3D);
    const bool rot = gl->isAnimating();
    const bool rm = ui->tabModeSelector->currentIndex() == 1;
    const bool keep4D = !rm || ui->subTabImplicit->currentIndex() == 1;
    const QString running = t3 ? QStringLiteral("path3D") : t4 ? QStringLiteral("path4D")
                          : rot ? QStringLiteral("rotation") : QStringLiteral("none");

    // VISTE dei due path.
    auto viewName = [](int m) {
        return m == ModeTangential ? QStringLiteral("Tangent View") : QStringLiteral("Center View");
    };
    if (ui->pushView->text() != viewName(m_mw->m_scene.pathViewMode4D))
        bad << QStringLiteral("vista 4D: tasto \"%1\", membro %2").arg(ui->pushView->text(), viewName(m_mw->m_scene.pathViewMode4D));
    if (ui->pushView3D->text() != viewName(m_mw->m_scene.pathViewMode3D))
        bad << QStringLiteral("vista 3D: tasto \"%1\", membro %2").arg(ui->pushView3D->text(), viewName(m_mw->m_scene.pathViewMode3D));
    if (sv.pathMode4D != int(m_mw->m_scene.pathViewMode4D) || sv.pathMode3D != int(m_mw->m_scene.pathViewMode3D))
        bad << QStringLiteral("viste: il Save scriverebbe %1/%2").arg(sv.pathMode4D).arg(sv.pathMode3D);

    // VELOCITA' dei path: lo stato (m_scene.pathSpeed3D/4D) e' cio' che gli slider
    // mostrano e che il Save scrive.
    if (m_mw->m_scene.pathSpeed3D != ui->speed3DSlider->value() || m_mw->m_scene.pathSpeed4D != ui->speed4DSlider->value())
        bad << QStringLiteral("velocita' path: stato %1/%2, slider %3/%4")
                   .arg(m_mw->m_scene.pathSpeed3D).arg(m_mw->m_scene.pathSpeed4D)
                   .arg(ui->speed3DSlider->value()).arg(ui->speed4DSlider->value());
    if (sv.speedPath3D != m_mw->m_scene.pathSpeed3D || sv.speedPath4D != (keep4D ? m_mw->m_scene.pathSpeed4D : 0))
        bad << QStringLiteral("velocita' path: il Save scriverebbe %1/%2").arg(sv.speedPath3D).arg(sv.speedPath4D);

    // VELOCITA' delle rotazioni: l'etichetta e' cio' che i tasti +/- leggono.
    struct Rot { const char *name; QLabel *label; float engine; float saved; bool is4D; };
    const Rot rots[] = {
        { "precessione", ui->lblPrecVal, gl->getPrecessionSpeed(), sv.speedPrec, false },
        { "nutazione", ui->lblNutVal, gl->getNutationSpeed(), sv.speedNut, false },
        { "spin", ui->lblSpinVal, gl->getSpinSpeed(), sv.speedSpin, false },
        { "omega", ui->lblOmegaVal, gl->getOmegaSpeed(), sv.speedOmega, true },
        { "phi", ui->lblPhiVal, gl->getPhiSpeed(), sv.speedPhi, true },
        { "psi", ui->lblPsiVal, gl->getPsiSpeed(), sv.speedPsi, true },
    };
    for (const Rot &r : rots) {
        const float shown = r.label->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toFloat();
        if (qAbs(shown - r.engine) > 0.006f)
            bad << QStringLiteral("%1: etichetta %2, motore %3").arg(QLatin1String(r.name), r.label->text()).arg(r.engine);
        const float expected = (r.is4D && !keep4D) ? 0.0f : r.engine;
        if (qAbs(r.saved - expected) > 1e-4f)
            bad << QStringLiteral("%1: il Save scriverebbe %2, motore %3").arg(QLatin1String(r.name)).arg(r.saved).arg(r.engine);
    }

    // TASTI E TIMER.
    if ((ui->btnDeparture->text() == QLatin1String("STOP")) != t4)
        bad << QStringLiteral("tasto Departure 4D \"%1\" ma il path 4D %2").arg(ui->btnDeparture->text(), t4 ? "gira" : "e' fermo");
    if ((ui->btnDeparture3D->text() == QLatin1String("STOP")) != t3)
        bad << QStringLiteral("tasto Departure 3D \"%1\" ma il path 3D %2").arg(ui->btnDeparture3D->text(), t3 ? "gira" : "e' fermo");
    if ((ui->btnStart_2->text() == QLatin1String("STOP")) != rot)
        bad << QStringLiteral("tasto GO \"%1\" ma le rotazioni %2").arg(ui->btnStart_2->text(), rot ? "girano" : "sono ferme");
    if (gl->isPathAnimating() != (t4 || t3))
        bad << QStringLiteral("motore: path in corsa %1, timer %2").arg(onOff(gl->isPathAnimating()), onOff(t4 || t3));
    if (int(t4) + int(t3) + int(rot) > 1)
        bad << QStringLiteral("piu' moti camera insieme (4D %1, 3D %2, rotazioni %3)").arg(onOff(t4), onOff(t3), onOff(rot));
    if (running != QLatin1String("none") && m_mw->m_scene.lastCameraMotion != running)
        bad << QStringLiteral("gira %1 ma l'ultimo moto avviato e' \"%2\"").arg(running, m_mw->m_scene.lastCameraMotion);
    if (running != QLatin1String("none") && sv.activeMotion != running)
        bad << QStringLiteral("gira %1 ma il Save scriverebbe activeMotion \"%2\"").arg(running, sv.activeMotion);
    if (!expectRunning.isNull() && running != expectRunning)
        bad << QStringLiteral("in corsa: %1, atteso %2").arg(running, expectRunning);

    // FOV.
    if (qAbs(m_mw->m_scene.fov - ui->fovSliderMain->value()) > 0.5f)
        bad << QStringLiteral("FOV: slider %1, stato %2").arg(ui->fovSliderMain->value()).arg(m_mw->m_scene.fov);
    if (qAbs(gl->cameraFov() - ui->fovSliderMain->value()) > 0.5f)
        bad << QStringLiteral("FOV: slider %1, proiezione del motore %2").arg(ui->fovSliderMain->value()).arg(gl->cameraFov());
    if (qAbs(sv.fov3D - m_mw->m_scene.fov) > 0.01f || qAbs(sv.fov4D - m_mw->m_scene.fov) > 0.01f)
        bad << QStringLiteral("FOV: il Save scriverebbe %1/%2").arg(sv.fov3D).arg(sv.fov4D);

    // PATH APPLICATO: a path in corsa il motore valuta le equazioni dei campi.
    auto field = [](QLineEdit *l) {
        QString t = l->text().trimmed();
        return t.isEmpty() ? QStringLiteral("0") : t.replace(QLatin1Char(','), QLatin1Char('.'));
    };
    if (t4) {
        const QStringList fields = { field(ui->lineX_P), field(ui->lineY_P), field(ui->lineZ_P), field(ui->lineP_P),
                                     field(ui->lineAlpha_P), field(ui->lineBeta_P), field(ui->lineGamma_P) };
        if (!engine->path4DCompiled())
            bad << QStringLiteral("il path 4D gira ma il motore non ha un path valido");
        else if (!pendingEdit && engine->appliedPath4D() != fields)
            bad << QStringLiteral("path 4D: campi [%1], motore [%2]").arg(fields.join(QStringLiteral(" | ")),
                                                                       engine->appliedPath4D().join(QStringLiteral(" | ")));
        else if (pendingEdit && engine->appliedPath4D() == fields)
            bad << QStringLiteral("path 4D: la modifica in sospeso e' gia' nel motore");
    }
    if (t3) {
        const QStringList fields = { field(ui->lineX_P3D), field(ui->lineY_P3D), field(ui->lineZ_P3D), field(ui->lineR_P3D) };
        if (!engine->path3DCompiled())
            bad << QStringLiteral("il path 3D gira ma il motore non ha un path valido");
        else if (!pendingEdit && engine->appliedPath3D() != fields)
            bad << QStringLiteral("path 3D: campi [%1], motore [%2]").arg(fields.join(QStringLiteral(" | ")),
                                                                       engine->appliedPath3D().join(QStringLiteral(" | ")));
        else if (pendingEdit && engine->appliedPath3D() == fields)
            bad << QStringLiteral("path 3D: la modifica in sospeso e' gia' nel motore");
    }
    // Il Save scrive i campi (l'intenzione), in corsa o no.
    if (sv.path4D_x != ui->lineX_P->text() || sv.path4D_alpha != ui->lineAlpha_P->text()
        || sv.path3D_x != ui->lineX_P3D->text() || sv.path3D_roll != ui->lineR_P3D->text())
        bad << QStringLiteral("path: il Save non scriverebbe i campi");

    if (!lineFieldsProblem().isEmpty()) bad << lineFieldsProblem();
    check(bad.isEmpty(), QStringLiteral("%1 -> moti (%2)%3")
                             .arg(step, running, bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::checkDirty(const QString &step, bool scene, bool texture, bool sound)
{
    const bool s = m_mw->hasUnsavedWork();
    const bool t = m_mw->textureModuleDirty();
    const bool a = m_mw->soundModuleDirty();
    auto names = [](bool sc, bool tx, bool sn) {
        QStringList l;
        if (sc) l << QStringLiteral("scena");
        if (tx) l << QStringLiteral("texture");
        if (sn) l << QStringLiteral("suono");
        return l.isEmpty() ? QStringLiteral("niente") : l.join(QStringLiteral(" + "));
    };
    const bool flags = (s == scene && t == texture && a == sound);
    // Di passaggio, la luce (tra i gesti qui c'e' lo slider della luce).
    const QString light = lightProblem();
    QString why;
    if (!flags)
        why = QStringLiteral(" (l'app dice: %1; %2)")
                  .arg(names(s, t, a), m_mw->unsavedKeys().join(QStringLiteral(", ")).left(300));
    if (!light.isEmpty()) why += QStringLiteral(": ") + light;
    check(flags && light.isEmpty(), QStringLiteral("%1 -> da salvare: %2%3")
                                        .arg(step, names(scene, texture, sound), why));
}

void ScenarioTest::checkMotionDefaults(const QString &step)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const LibraryItem sv = captureSave();
    QStringList bad;
    if (m_mw->pathRunning(CameraPaths::Path4D) || m_mw->pathRunning(CameraPaths::Path3D)
        || gl->isAnimating())
        bad << QStringLiteral("un moto camera gira ancora");
    for (QLineEdit *l : { ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P, ui->lineAlpha_P, ui->lineBeta_P,
                          ui->lineGamma_P, ui->lineX_P3D, ui->lineY_P3D, ui->lineZ_P3D, ui->lineR_P3D })
        if (!l->text().trimmed().isEmpty()) { bad << QStringLiteral("campo path \"%1\" non vuoto").arg(l->objectName()); break; }
    if (m_mw->m_scene.pathViewMode4D != ModeTangential || m_mw->m_scene.pathViewMode3D != ModeTangential)
        bad << QStringLiteral("vista dei path rimasta Center (4D %1, 3D %2)")
                   .arg(int(m_mw->m_scene.pathViewMode4D)).arg(int(m_mw->m_scene.pathViewMode3D));
    if (m_mw->m_scene.pathSpeed3D != 10 || m_mw->m_scene.pathSpeed4D != 10
        || ui->speed3DSlider->value() != 10 || ui->speed4DSlider->value() != 10)
        bad << QStringLiteral("velocita' dei path rimaste %1/%2 (slider %3/%4)")
                   .arg(m_mw->m_scene.pathSpeed3D).arg(m_mw->m_scene.pathSpeed4D)
                   .arg(ui->speed3DSlider->value()).arg(ui->speed4DSlider->value());
    const float speeds[] = { gl->getPrecessionSpeed(), gl->getNutationSpeed(), gl->getSpinSpeed(),
                             gl->getOmegaSpeed(), gl->getPhiSpeed(), gl->getPsiSpeed() };
    for (float v : speeds)
        if (qAbs(v) > 1e-4f) { bad << QStringLiteral("velocita' di rotazione rimaste nel motore"); break; }
    if (ui->fovSliderMain->value() != 45)
        bad << QStringLiteral("FOV rimasto %1").arg(ui->fovSliderMain->value());
    if (!m_mw->m_scene.lastCameraMotion.isEmpty())
        bad << QStringLiteral("ultimo moto avviato rimasto \"%1\"").arg(m_mw->m_scene.lastCameraMotion);
    if (sv.activeMotion != QLatin1String("none"))
        bad << QStringLiteral("il Save scriverebbe activeMotion \"%1\"").arg(sv.activeMotion);
    if (gl->getEngine()->path4DCompiled() || gl->getEngine()->path3DCompiled())
        bad << QStringLiteral("nel motore resta compilato il path di prima");
    check(bad.isEmpty(), QStringLiteral("%1 -> nessun moto, comandi ai default%2")
                             .arg(step, bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::checkEquations(const QString &step, bool pendingEdit)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const LibraryItem sv = captureSave();
    const bool rm = ui->tabModeSelector->currentIndex() == 1;
    const bool script = gl->getEngine() && gl->getEngine()->isScriptModeActive();
    auto brief = [](QString t) {
        t = t.simplified();
        return t.isEmpty() ? QStringLiteral("(vuoto)") : QStringLiteral("\"%1\"").arg(t.left(48));
    };

    QStringList bad;
    QString shown;
    if (rm) {
        // RAY MARCHING: due campi (3D e Cross Section), un sotto-tab che dice
        // quale e' attivo, e il motore che compila quello.
        const QString f3D = ui->lineEquation->toPlainText();
        const QString fCS = ui->lineEquationCrossSection->toPlainText();
        const bool tabCS = m_mw->crossSectionTab();
        const QString active = tabCS ? fCS : f3D;
        if ((ui->subTabImplicit->currentIndex() == 1) != tabCS)
            bad << QStringLiteral("stato %1, linguetta %2")
                       .arg(tabCS ? QStringLiteral("Cross Section") : QStringLiteral("3D"),
                            ui->subTabImplicit->currentIndex() == 1 ? QStringLiteral("Cross Section")
                                                                     : QStringLiteral("3D"));
        shown = QStringLiteral("%1 %2").arg(tabCS ? QStringLiteral("Cross Section") : QStringLiteral("3D"),
                                            script ? QStringLiteral("(script)") : brief(active));
        if (gl->implicitUsesCrossSection() != tabCS)
            bad << QStringLiteral("sotto-tab %1, motore sul ramo %2")
                       .arg(tabCS ? QStringLiteral("Cross Section") : QStringLiteral("3D"),
                            gl->implicitUsesCrossSection() ? QStringLiteral("Cross Section") : QStringLiteral("3D"));
        if (sv.usesCrossSection != tabCS)
            bad << QStringLiteral("il Save scriverebbe implicitUsesCrossSection %1").arg(onOff(sv.usesCrossSection));
        // L'equazione compilata e' quella del campo attivo ("L = R" -> "(L) - (R)").
        auto canon = [](QString e, bool wrapPlain) {
            e.remove(QRegularExpression(QStringLiteral("\\s+")));
            if (e.contains(QLatin1Char('=')))
                return QStringLiteral("(%1)-(%2)").arg(e.section(QLatin1Char('='), 0, 0), e.section(QLatin1Char('='), 1));
            return wrapPlain ? QStringLiteral("(%1)-(0.0)").arg(e) : e;
        };
        // (il motore puo' tenere il campo cosi' com'e', o gia' nella forma "(L) - (R)")
        const QString eng = canon(gl->activeImplicitEquation(), false);
        if (!script && !pendingEdit && !active.trimmed().isEmpty()
            && eng != canon(active, true) && eng != canon(active, false))
            bad << QStringLiteral("motore %1, campo %2").arg(brief(gl->activeImplicitEquation()), brief(active));
        if (sv.implicitEq != f3D)
            bad << QStringLiteral("il Save scriverebbe l'equazione 3D %1, campo %2").arg(brief(sv.implicitEq), brief(f3D));
        if (sv.crossSectionEq != fCS)
            bad << QStringLiteral("il Save scriverebbe la Cross Section %1, campo %2").arg(brief(sv.crossSectionEq), brief(fCS));
        // LIMITI SPAZIALI x/y/z: il motore taglia dove dicono i campi (vuoto, o
        // minimo >= massimo: nessun taglio), e il Save scrive quei valori --
        // piu' la formula, quando il campo ne contiene una ("A/2").
        struct S { const char *name; QLineEdit *lo; QLineEdit *hi; float eLo, eHi, sLo, sHi;
                   QString xLo, xHi; };
        const QVector3D mn = gl->spaceRangeMin(), mx = gl->spaceRangeMax();
        const QList<S> sp = {
            { "x", ui->lineXMin, ui->lineXMax, mn.x(), mx.x(), sv.xMin, sv.xMax, sv.xMinExpr, sv.xMaxExpr },
            { "y", ui->lineYMin, ui->lineYMax, mn.y(), mx.y(), sv.yMin, sv.yMax, sv.yMinExpr, sv.yMaxExpr },
            { "z", ui->lineZMin, ui->lineZMax, mn.z(), mx.z(), sv.zMin, sv.zMax, sv.zMinExpr, sv.zMaxExpr } };
        for (const S &a : sp) {
            auto val = [this](QLineEdit *e, float def) {
                return e->text().trimmed().isEmpty() ? def : m_mw->parseLimitField(e->text());
            };
            auto formula = [](QLineEdit *e) {
                const QString t = e->text().trimmed();
                bool number = false;
                QString(t).replace(',', '.').toFloat(&number);
                return (t.isEmpty() || number) ? QString() : t;
            };
            if (a.xLo != formula(a.lo) || a.xHi != formula(a.hi))
                bad << QStringLiteral("limiti %1: il Save scriverebbe le formule '%2'..'%3', campi '%4'..'%5'")
                           .arg(QString::fromLatin1(a.name), a.xLo, a.xHi, a.lo->text(), a.hi->text());
            const float fLo = val(a.lo, -1000.0f), fHi = val(a.hi, 1000.0f);
            float wLo = fLo, wHi = fHi;
            if (wLo >= wHi) { wLo = -1000.0f; wHi = 1000.0f; }
            if (!pendingEdit && (qAbs(a.eLo - wLo) > 1e-3f || qAbs(a.eHi - wHi) > 1e-3f))
                bad << QStringLiteral("limiti %1: motore %2..%3, campi %4..%5").arg(QString::fromLatin1(a.name))
                           .arg(a.eLo).arg(a.eHi).arg(wLo).arg(wHi);
            if (qAbs(a.sLo - fLo) > 1e-3f || qAbs(a.sHi - fHi) > 1e-3f)
                bad << QStringLiteral("limiti %1: il Save scriverebbe %2..%3, campi %4..%5").arg(QString::fromLatin1(a.name))
                           .arg(a.sLo).arg(a.sHi).arg(fLo).arg(fHi);
        }
    } else {
        // PARAMETRICO: i campi X/Y/Z/P sono l'intenzione, lo snapshot dell'ultimo
        // Run l'applicato; il Save scrive i campi.
        struct F { const char *name; QPlainTextEdit *edit; MainWindow::EqField member; QString saved; };
        const QList<F> fs = {
            { "x", ui->lineX, &MainWindow::EquationTexts::x, sv.x }, { "y", ui->lineY, &MainWindow::EquationTexts::y, sv.y },
            { "z", ui->lineZ, &MainWindow::EquationTexts::z, sv.z }, { "p", ui->lineP, &MainWindow::EquationTexts::p, sv.w },
            // Composizione e vincoli: stessa regola.
            { "U", ui->lineU, &MainWindow::EquationTexts::u, sv.defU }, { "V", ui->lineV, &MainWindow::EquationTexts::v, sv.defV },
            { "W", ui->lineW, &MainWindow::EquationTexts::w, sv.defW },
            { "vincolo u", ui->lineExplicitU, &MainWindow::EquationTexts::explicitU, sv.explicitU },
            { "vincolo v", ui->lineExplicitV, &MainWindow::EquationTexts::explicitV, sv.explicitV },
            { "vincolo w", ui->lineExplicitW, &MainWindow::EquationTexts::explicitW, sv.explicitW },
            // Flusso geodetico: punto e direzione iniziali, fattore conforme.
            { "geo u0", ui->lnU, &MainWindow::EquationTexts::geoU, sv.geoU0 }, { "geo v0", ui->lnV, &MainWindow::EquationTexts::geoV, sv.geoV0 },
            { "geo w0", ui->lnW, &MainWindow::EquationTexts::geoW, sv.geoW0 }, { "geo du", ui->lndU, &MainWindow::EquationTexts::geoDU, sv.geoDU },
            { "geo dv", ui->lndV, &MainWindow::EquationTexts::geoDV, sv.geoDV }, { "geo dw", ui->lndW, &MainWindow::EquationTexts::geoDW, sv.geoDW },
            { "conforme", ui->lineConform, &MainWindow::EquationTexts::conform, sv.geoConform } };
        shown = script ? QStringLiteral("(script)") : brief(ui->lineX->toPlainText());
        for (const F &f : fs) {
            const QString field = f.edit->toPlainText();
            const QString applied = m_mw->m_eqApplied ? (*m_mw->m_eqApplied).*(f.member) : QString();
            // Lo STATO (m_scene.eq) e' il testo del campo: il campo ne e' l'editor.
            if (m_mw->m_scene.eq.*(f.member) != field)
                bad << QStringLiteral("%1: lo stato ha %2, il campo %3")
                           .arg(QString::fromLatin1(f.name), brief(m_mw->m_scene.eq.*(f.member)), brief(field));
            if (!pendingEdit && applied != field)
                bad << QStringLiteral("%1: applicata %2, campo %3").arg(QString::fromLatin1(f.name), brief(applied), brief(field));
            if (f.saved != field)
                bad << QStringLiteral("%1: il Save scriverebbe %2, campo %3").arg(QString::fromLatin1(f.name), brief(f.saved), brief(field));
        }
        // LIMITI u, v: il dominio del motore e' quello dei campi (quando ci sono:
        // con uno script il dominio puo' venire da li').
        struct L { const char *name; QLineEdit *edit; float engine; };
        SurfaceEngine *en = gl->getEngine();
        const QList<L> ls = {
            { "uMin", ui->uMinEdit, en->getUMin() }, { "uMax", ui->uMaxEdit, en->getUMax() },
            { "vMin", ui->vMinEdit, en->getVMin() }, { "vMax", ui->vMaxEdit, en->getVMax() } };
        for (const L &l : ls) {
            if (pendingEdit || script || l.edit->text().trimmed().isEmpty()) continue;
            bool ok = false;
            const float want = m_mw->parseLimitField(l.edit->text(), &ok);
            if (ok && qAbs(want - l.engine) > 1e-3f)
                bad << QStringLiteral("%1: motore %2, campo %3").arg(QString::fromLatin1(l.name))
                           .arg(l.engine).arg(want);
        }
        // SCRIPT di superficie: lo scritto (slot) e l'applicato (che il Save legge).
        if (!pendingEdit && m_mw->m_scene.surfaceScriptApplied != m_mw->m_scene.surfaceScriptText)
            bad << QStringLiteral("script: l'applicato e' diverso dallo scritto (%1 / %2)")
                       .arg(brief(m_mw->m_scene.surfaceScriptApplied), brief(m_mw->m_scene.surfaceScriptText));
    }

    if (!scriptEditorProblem().isEmpty()) bad << scriptEditorProblem();
    if (!rmFieldsProblem().isEmpty()) bad << rmFieldsProblem();
    if (!lineFieldsProblem().isEmpty()) bad << lineFieldsProblem();

    check(bad.isEmpty(), QStringLiteral("%1 -> equazioni %2%3")
                             .arg(step, shown,
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::checkSurfaceImage(const QString &step, const QString &expectedImage)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    static const QRegularExpression imgRe(QStringLiteral(R"(^\s*//IMG:\s*(.*)$)"),
                                          QRegularExpression::MultilineOption);
    auto imageOf = [](const QString &code) {
        const QRegularExpressionMatch m = imgRe.match(code);
        return m.hasMatch() ? QFileInfo(m.captured(1).trimmed()).fileName() : QString();
    };
    auto name = [](const QString &f) { return f.isEmpty() ? QStringLiteral("nessuna") : f; };

    const QString shown = QFileInfo(gl->surfaceImagePath()).fileName();
    const LibraryItem saved = captureSave();
    const QString savedImage = imageOf(saved.textureCode);
    const bool rm = ui->tabModeSelector->currentIndex() == 1;
    // Lo SCRIPT dice quale immagine usa (il Run senza tag la toglie): in Ray
    // Marching il campo lineTexture, in parametrico l'editor o il suo slot.
    const QString scriptImage = imageOf(rm ? ui->lineTexture->toPlainText()
                                           : m_mw->surfaceTextureScript());

    QStringList bad;
    if (saved.textureEnabled) {
        if (savedImage != shown)
            bad << QStringLiteral("a schermo %1, il Save scriverebbe %2").arg(name(shown), name(savedImage));
        if (scriptImage != shown)
            bad << QStringLiteral("a schermo %1, lo script dice %2").arg(name(shown), name(scriptImage));
    } else {
        if (!savedImage.isEmpty())
            bad << QStringLiteral("texture spenta ma il Save scriverebbe l'immagine %1").arg(savedImage);
        if (!shown.isEmpty())
            bad << QStringLiteral("texture spenta con %1 in GPU").arg(shown);
    }
    if (!expectedImage.isNull() && shown != expectedImage)
        bad << QStringLiteral("a schermo %1, attesa %2").arg(name(shown), name(expectedImage));

    check(bad.isEmpty(), QStringLiteral("%1 -> immagine %2%3")
                             .arg(step, name(shown),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::run()
{
    Ui::MainWindow *ui = m_mw->ui;
    auto click = [this](QAbstractButton *b) { b->click(); wait(200); };

    // Una sola sezione, per provare in fretta la parte su cui si lavora.
    if (m_only == QLatin1String("script-dock")) {
        runScriptDockScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("texture-target")) {
        runTextureTargetScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("mesh-image")) {
        runMeshImageScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("record-texture")) {
        runRecordTextureScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("mesh-clocks")) {
        runMeshClockScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("library-folder")) {
        runLibraryFolderScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("library-paste")) {
        runLibraryPasteScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("library-rename")) {
        runLibraryRenameScenarios();
        finish();
        return;
    }
    if (m_only.startsWith(QLatin1String("master-records"))) {
        runMasterRecordsScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("reset-scene")) {
        runResetSceneScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("tubes")) {
        runTubeScenarios();
        finish();
        return;
    }
    if (m_only == QLatin1String("reset-button")) {
        runResetButtonScenarios();
        finish();
        return;
    }

    // ---------------------------------------------------------------------
    // AVVIO: la superficie di default non e' lavoro dell'utente. Se qui la
    // scena risultasse da salvare, il primo preset scelto farebbe uscire
    // l'avviso su un'app appena aperta.
    m_lines.append(QStringLiteral("== Avvio =="));
    checkDirty(QStringLiteral("app appena aperta"), false, false, false);
    m_discardOnPrompt = true;
    ui->tabModeSelector->setCurrentIndex(1);  wait(1500);
    checkDirty(QStringLiteral("linguetta Ray Marching (superficie di default)"), false, false, false);
    ui->subTabImplicit->setCurrentIndex(1);  wait(1500);
    checkDirty(QStringLiteral("sotto-tab Cross Section (superficie di default)"), false, false, false);
    ui->subTabImplicit->setCurrentIndex(0);  wait(1500);
    ui->tabModeSelector->setCurrentIndex(0);  wait(1500);
    m_discardOnPrompt = false;
    checkDirty(QStringLiteral("linguetta Parametric (superficie di default)"), false, false, false);
    m_lines.append(QString());

    // ---------------------------------------------------------------------
    // SOTTO-TAB TUBES (vedi runTubeScenarios).
    runTubeScenarios();
    m_lines.append(QString());

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

    // ---------------------------------------------------------------------
    // COLORI DELLA TEXTURE, superficie parametrica.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Colori texture: parametrico (%1) ==")
                       .arg(QString::fromLatin1(kParametricRecord)));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        checkTexColors(QStringLiteral("record caricato"));
        const QColor m1(QStringLiteral("#1c3664")), m2(QStringLiteral("#e28d57"));
        if (selectTexture(QStringLiteral("textures/Procedurals/Mandelbrot.json")))
            checkTexColors(QStringLiteral("Mandelbrot dalla Library"), m1, m2);
        const QColor n2(10, 200, 30);
        setTexColorBySliders(true, n2);
        checkTexColors(QStringLiteral("colore 2 dagli slider"), m1, n2);
        click(ui->radioTexColor1);  checkTexColors(QStringLiteral("slot Colore 1"), m1, n2);
        click(ui->radioWF);         checkTexColors(QStringLiteral("poi Wireframe"), m1, n2);
        click(ui->radioPhong);      checkTexColors(QStringLiteral("poi Phong"), m1, n2);
        click(ui->radioBackground); checkTexColors(QStringLiteral("entrata in Background"), m1, n2);
        setTexColorBySliders(false, QColor(200, 10, 10));
        checkTexColors(QStringLiteral("colore dello sfondo dagli slider"), m1, n2);
        click(ui->radioSurface);    checkTexColors(QStringLiteral("ritorno a Surface"), m1, n2);
        click(ui->chkBoxTexture);   checkTexColors(QStringLiteral("texture spenta"), m1, n2);
        // Riaccendere riparte dalla scacchiera di default (clearTextureMemory
        // allo spegnimento): verde e nero, voluto.
        const QColor d1(QStringLiteral("#33cc33")), d2(Qt::black);
        click(ui->chkBoxTexture);   checkTexColors(QStringLiteral("texture riaccesa (default)"), d1, d2);
        setTexColorBySliders(false, QColor(40, 40, 160));
        checkTexColors(QStringLiteral("colore 1 dagli slider"), QColor(40, 40, 160), d2);
        m_mw->onStartClicked();  wait(600);
        checkTexColors(QStringLiteral("Run"), QColor(40, 40, 160), d2);
    }

    // ---------------------------------------------------------------------
    // COLORI DELLA TEXTURE, multi-mesh: i colori di una fascia sono suoi, i
    // globali appartengono alla superficie e li eredita chi non ne ha.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Colori texture: multi-mesh (%1) ==")
                       .arg(QString::fromLatin1(kMultiMeshRecord)));
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        checkTexColors(QStringLiteral("record caricato"));
        // Il record si apre nell'ambito salvato (qui Mesh): la texture globale
        // si sceglie in All.
        if (!ui->radioMeshAll->isChecked()) click(ui->radioMeshAll);
        checkTexColors(QStringLiteral("ambito All"));
        const QColor m1(QStringLiteral("#1c3664")), m2(QStringLiteral("#e28d57"));
        if (selectTexture(QStringLiteral("textures/Procedurals/Mandelbrot.json")))
            checkTexColors(QStringLiteral("All: Mandelbrot dalla Library"), m1, m2);
        if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(1);  wait(300);
        const QColor d1(QStringLiteral("#33cc33")), d2(Qt::black);
        checkTexColors(QStringLiteral("fascia 1 (colori propri)"), m1, m2, d1, d2);
        const QColor b1(QStringLiteral("#f2e4d8")), b2(QStringLiteral("#8a4a32"));
        if (selectTexture(QStringLiteral("textures/Procedurals/Porous Bone.json")))
            checkTexColors(QStringLiteral("fascia 1: Porous Bone dalla Library"), m1, m2, b1, b2);
        const QColor o1(250, 120, 0);
        setTexColorBySliders(false, o1);
        checkTexColors(QStringLiteral("fascia 1: colore 1 dagli slider"), m1, m2, o1, b2);
        // Fascia 4: nel record e' in wireframe e senza texture propria, quindi
        // eredita i colori globali. (Le fasce sono 15: A*B con A=3, B=5.)
        ui->spinMeshSel->setValue(4);  wait(300);
        checkTexColors(QStringLiteral("fascia 4 (eredita i globali)"), m1, m2);
        click(ui->radioBasic);
        checkTexColors(QStringLiteral("fascia 4 in Base"), m1, m2);
        click(ui->chkBoxTexture);
        checkTexColors(QStringLiteral("fascia 4: texture accesa (default)"), m1, m2, d1, d2);
        const QColor v2(0, 90, 250);
        setTexColorBySliders(true, v2);
        checkTexColors(QStringLiteral("fascia 4: colore 2 dagli slider"), m1, m2, d1, v2);
        ui->spinMeshSel->setValue(1);  wait(300);
        checkTexColors(QStringLiteral("di nuovo fascia 1"), m1, m2, o1, b2);
        click(ui->radioMeshAll);    checkTexColors(QStringLiteral("ritorno ad All"), m1, m2);
        const QColor y1(255, 255, 0);
        setTexColorBySliders(false, y1);
        checkTexColors(QStringLiteral("All: colore 1 dagli slider"), y1, m2);
        if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(1);  wait(300);
        checkTexColors(QStringLiteral("fascia 1 dopo il cambio globale"), y1, m2, o1, b2);
        ui->spinMeshSel->setValue(4);  wait(300);
        checkTexColors(QStringLiteral("fascia 4 dopo il cambio globale"), y1, m2, d1, v2);
        ui->spinMeshSel->setValue(6);  wait(300);
        checkTexColors(QStringLiteral("fascia 6 (eredita i globali)"), y1, m2);
        click(ui->radioMeshAll);
    }

    // ---------------------------------------------------------------------
    // DISPLACEMENT (rilievi), Ray Marching: il campo e il motore insieme.
    const QString kDispRecord = QStringLiteral("records/Ray Marching/Blistered Gyroid.json");
    const QString kLimestone  = QStringLiteral("textures/Ray Marching/Limestone.json");
    const QString kFbm        = QStringLiteral("textures/Ray Marching/Fractal Noise FBm.json");
    const QString kFractal    = QStringLiteral("textures/Ray Marching/Fractal Noise.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Displacement: Ray Marching (%1) ==").arg(kDispRecord));
    if (loadRecord(kDispRecord)) {
        checkDisplacement(QStringLiteral("record caricato"),
                          presetDisplacement(kDispRecord, LibraryType::Motion));
        if (selectTexture(kLimestone))
            checkDisplacement(QStringLiteral("Limestone dalla Library"),
                              presetDisplacement(kLimestone, LibraryType::Texture));
        if (selectTexture(kFbm))
            checkDisplacement(QStringLiteral("texture senza rilievi dalla Library"),
                              presetDisplacement(kFbm, LibraryType::Texture));
        if (selectTexture(kFractal))
            checkDisplacement(QStringLiteral("Fractal Noise dalla Library"),
                              presetDisplacement(kFractal, LibraryType::Texture));
        click(ui->radioWF);         checkDisplacement(QStringLiteral("poi Wireframe"));
        click(ui->radioBasic);      checkDisplacement(QStringLiteral("poi Base"));
        click(ui->radioBackground); checkDisplacement(QStringLiteral("entrata in Background"));
        click(ui->radioSurface);    checkDisplacement(QStringLiteral("ritorno a Surface"));
        m_mw->onStartClicked();  wait(600);
        checkDisplacement(QStringLiteral("Run"));
        click(ui->chkBoxTexture);   checkDisplacement(QStringLiteral("texture spenta"), QStringLiteral(""));
        click(ui->chkBoxTexture);   checkDisplacement(QStringLiteral("texture riaccesa (default)"));
        // Modifica a mano: finche' non si esegue il motore resta indietro, il
        // Save scrive il campo. Il Run li riallinea.
        const QString edited = QStringLiteral("float rilievo = sin(pModel.x * 10.0);\n"
                                              "d_surf -= rilievo * 0.02;");
        ui->lineVariations->setPlainText(edited);  wait(200);
        checkDisplacement(QStringLiteral("campo modificato a mano"), edited, /*pendingEdit=*/true);
        m_mw->onStartClicked();  wait(800);
        checkDisplacement(QStringLiteral("Run dopo la modifica"), edited);
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord)))
        checkDisplacement(QStringLiteral("poi un record parametrico"), QStringLiteral(""));
    const QString kTunnel = QStringLiteral("records/Ray Marching/Torus Tunnel.json");
    if (loadRecord(kTunnel))
        checkDisplacement(QStringLiteral("poi Torus Tunnel"),
                          presetDisplacement(kTunnel, LibraryType::Motion));
    if (loadRecord(QString::fromLatin1(kImplicitRecord)))
        checkDisplacement(QStringLiteral("poi un record RM senza rilievi"),
                          presetDisplacement(QString::fromLatin1(kImplicitRecord), LibraryType::Motion));

    // ---------------------------------------------------------------------
    // SFONDO-IMMAGINE PERSO NEI RECORD. La catena: record con un'immagine di
    // sfondo, poi un record con lo sfondo SPENTO (ma col codice nel file), poi
    // si riaccende lo sfondo. Prima si vedeva l'immagine del primo e il Save
    // scriveva enabled true con codice vuoto: al reload, la default.
    const QString kRoman = QStringLiteral("records/Rotations/Roman Surface.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Sfondo: immagine persa nei record (%1, poi %2) ==")
                       .arg(QString::fromLatin1(kParametricRecord), kRoman));
    if (loadRecord(QString::fromLatin1(kParametricRecord)))
        checkBackground(QStringLiteral("record con immagine di sfondo"), QStringLiteral("8.png"));
    if (loadRecord(kRoman)) {
        checkBackground(QStringLiteral("record con lo sfondo spento"));
        click(ui->radioBackground);
        click(ui->chkBoxTexture);   checkBackground(QStringLiteral("sfondo riacceso"), QStringLiteral(""));
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkBackground(QStringLiteral("immagine dalla Library"), QStringLiteral("14.png"));
        click(ui->chkBoxTexture);   checkBackground(QStringLiteral("sfondo spento dal checkbox"));
        click(ui->chkBoxTexture);   checkBackground(QStringLiteral("riacceso"), QStringLiteral(""));
        click(ui->radioSurface);    checkBackground(QStringLiteral("ritorno a Surface"), QStringLiteral(""));
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord)))
        checkBackground(QStringLiteral("di nuovo il record con immagine"), QStringLiteral("8.png"));
    const QString kClifford6 = QStringLiteral("records/Solid Wireframe/Multi Mesh/Clifford 6-Tubes Rotation.json");
    if (loadRecord(kClifford6))
        checkBackground(QStringLiteral("Clifford 6-Tubes Rotation (sfondo spento)"));

    // ---------------------------------------------------------------------
    // IMMAGINE DI SFONDO sotto uno script: resta quella nel motore finche' lo
    // script la nomina, e se ne va quando non la nomina piu'.
    const QString kBgProc = QStringLiteral("records/Geodesic Flow/H^3/Breathing Hyperboloid.json");
    const QString kBgMix  = QStringLiteral("records/Geodesic Flow/H^3/Poincare's Slice.json");
    const QString kBgPlasma = QStringLiteral("textures/Procedurals/Plasma.json");
    static const QRegularExpression bgImgRe(QStringLiteral(R"(^\s*//IMG:\s*(.*)$\n?)"),
                                            QRegularExpression::MultilineOption);
    auto presetBgImage = [this](const QString &rel) {
        LibraryManager lm;
        const QRegularExpressionMatch mm =
            bgImgRe.match(lm.parseJson(m_root + QLatin1Char('/') + rel, LibraryType::Motion).bgTextureCode);
        return mm.hasMatch() ? QFileInfo(mm.captured(1).trimmed()).fileName() : QString();
    };
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Sfondo: immagine e script =="));
    if (loadRecord(kBgMix))
        checkBackground(QStringLiteral("record con immagine e script di sfondo"), presetBgImage(kBgMix));
    if (loadRecord(kBgProc))
        checkBackground(QStringLiteral("poi un record con sfondo procedurale"), QStringLiteral(""));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        click(ui->radioBackground);
        setScriptMode(MainWindow::ScriptModeTexture);
        checkBackground(QStringLiteral("record con immagine di sfondo, dock Script sullo sfondo"), QStringLiteral("8.png"));
        if (selectTexture(kBgPlasma))
            checkBackground(QStringLiteral("procedurale sopra l'immagine"), QStringLiteral("8.png"));
        // Run dello script senza il tag: l'immagine non e' piu' nominata.
        QString noTag = ui->txtScriptEditor->toPlainText();
        noTag.remove(bgImgRe);
        ui->txtScriptEditor->setPlainText(noTag);  wait(200);
        m_mw->onRunCurrentScript();  wait(800);
        checkBackground(QStringLiteral("Run dello script senza il tag"), QStringLiteral(""));
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkBackground(QStringLiteral("immagine dalla Library"), QStringLiteral("14.png"));
        setScriptMode(MainWindow::ScriptModeSurface);
        click(ui->radioSurface);
        checkBackground(QStringLiteral("ritorno a Surface"), QStringLiteral("14.png"));
    }
    // Texture Ray Marching scelta come SFONDO: incompatibile, lo sfondo torna
    // alla default (il popup si chiude da solo) e la superficie non si tocca.
    if (loadRecord(QStringLiteral("records/Rotations/Boy Surface.json"))) {
        click(ui->radioBackground);
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkBackground(QStringLiteral("immagine di sfondo dalla Library"), QStringLiteral("14.png"));
        if (selectTexture(QStringLiteral("textures/Ray Marching/Fractal Noise FBm.json")))
            checkBackground(QStringLiteral("texture Ray Marching come sfondo (incompatibile)"), QStringLiteral(""));
        click(ui->radioSurface);
        checkSurfaceImage(QStringLiteral("la superficie dopo la texture incompatibile sullo sfondo"),
                          QStringLiteral("2k_venus_surface.jpg"));
    }

    // ---------------------------------------------------------------------
    // COMANDI DELLA SUPERFICIE col bersaglio Background: spenti fino al ritorno
    // su Surface, senza perdere le regole proprie (wireframe, Ray Marching).
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Comandi della superficie in Background =="));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        if (!ui->radioSurface->isChecked()) click(ui->radioSurface);
        checkSurfaceControls(QStringLiteral("parametrico, Surface"));
        click(ui->radioBackground); checkSurfaceControls(QStringLiteral("Background"));
        click(ui->radioSurface);    checkSurfaceControls(QStringLiteral("di nuovo Surface"));
        click(ui->radioWF);         checkSurfaceControls(QStringLiteral("Wireframe"));
        click(ui->radioBackground); checkSurfaceControls(QStringLiteral("Background dal wireframe"));
        click(ui->radioSurface);    checkSurfaceControls(QStringLiteral("Surface, ancora in wireframe"));
        click(ui->radioBasic);      checkSurfaceControls(QStringLiteral("Base"));
    }
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        if (!ui->radioSurface->isChecked()) click(ui->radioSurface);
        checkSurfaceControls(QStringLiteral("Ray Marching, Surface"));
        click(ui->radioBackground); checkSurfaceControls(QStringLiteral("Background"));
        click(ui->radioSurface);    checkSurfaceControls(QStringLiteral("di nuovo Surface"));
    }

    // ---------------------------------------------------------------------
    // CODICE DELLA TEXTURE, Ray Marching: campo lineTexture e motore insieme.
    // Lo spegnimento prima della modifica a mano: con codice modificato il
    // checkbox chiede conferma, e il popup chiuso vale Annulla.
    const QString kSound = QStringLiteral("sounds/procedural/Alps.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Codice texture: Ray Marching (%1) ==")
                       .arg(QString::fromLatin1(kImplicitRecord)));
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        checkTextureCode(QStringLiteral("record caricato"),
                         presetTextureCode(QString::fromLatin1(kImplicitRecord), LibraryType::Motion, false));
        // Una texture DIVERSA da quella del record (che usa gia' la FBm: quella
        // sarebbe un riclic, che non riapplica il codice).
        const QString kWaves = QStringLiteral("textures/Ray Marching/Spreading Waves.json");
        if (selectTexture(kWaves))
            checkTextureCode(QStringLiteral("Spreading Waves dalla Library"),
                             presetTextureCode(kWaves, LibraryType::Texture, false));
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkTextureCode(QStringLiteral("immagine dalla Library"));
        click(ui->chkBoxTexture);   checkTextureCode(QStringLiteral("texture spenta"), QStringLiteral(""));
        click(ui->chkBoxTexture);   checkTextureCode(QStringLiteral("texture riaccesa (default)"));
        if (selectSound(kSound))
            checkTextureCode(QStringLiteral("suono dalla Library"));
        const QString edited = QStringLiteral(
            "textureCol = mix(ubuf.u_col1, ubuf.u_col2, 0.5 + 0.5 * sin(pModel.x * 8.0));");
        ui->lineTexture->setPlainText(edited);  wait(200);
        checkTextureCode(QStringLiteral("campo modificato a mano"), edited, /*pendingEdit=*/true);
        m_mw->onStartClicked();  wait(800);
        checkTextureCode(QStringLiteral("Run dopo la modifica"), edited);
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord)))
        checkTextureCode(QStringLiteral("poi un record parametrico"),
                         presetTextureCode(QString::fromLatin1(kParametricRecord), LibraryType::Motion, true));
    if (loadRecord(kTunnel))
        checkTextureCode(QStringLiteral("poi Torus Tunnel"),
                         presetTextureCode(kTunnel, LibraryType::Motion, false));

    // ---------------------------------------------------------------------
    // CODICE DELLA TEXTURE, parametrico: lo script del dock Script. Il Save
    // scrive l'intenzione (l'editor, anche non eseguito) qualunque modulo il
    // dock stia mostrando.
    const QString kMandelbrot = QStringLiteral("textures/Procedurals/Mandelbrot.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Codice texture: parametrico (%1) ==")
                       .arg(QString::fromLatin1(kParametricRecord)));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        const QString recordCode =
            presetTextureCode(QString::fromLatin1(kParametricRecord), LibraryType::Motion, true);
        checkTextureCode(QStringLiteral("record caricato"), recordCode);
        setScriptMode(MainWindow::ScriptModeTexture);
        checkTextureCode(QStringLiteral("dock Script sulla texture"), recordCode);
        const QString mandelbrot = presetTextureCode(kMandelbrot, LibraryType::Texture, true);
        if (selectTexture(kMandelbrot))
            checkTextureCode(QStringLiteral("Mandelbrot dalla Library"), mandelbrot);
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkTextureCode(QStringLiteral("immagine dalla Library"), QStringLiteral(""));
        if (selectTexture(kMandelbrot))
            checkTextureCode(QStringLiteral("Mandelbrot sopra l'immagine"), mandelbrot);
        click(ui->chkBoxTexture);   checkTextureCode(QStringLiteral("texture spenta"), QStringLiteral(""));
        click(ui->chkBoxTexture);   checkTextureCode(QStringLiteral("texture riaccesa (default)"), QStringLiteral(""));
        if (selectTexture(kMandelbrot))
            checkTextureCode(QStringLiteral("di nuovo Mandelbrot"), mandelbrot);
        const QString edited = QStringLiteral("vec2 g = floor(vec2(u, v) * 4.0);\n"
                                              "float c = mod(g.x + g.y, 2.0);\n"
                                              "return mix(u_col2, u_col1, c);");
        ui->txtScriptEditor->setPlainText(edited);  wait(200);
        checkTextureCode(QStringLiteral("script modificato a mano"), edited, /*pendingEdit=*/true);
        setScriptMode(MainWindow::ScriptModeSound);
        checkTextureCode(QStringLiteral("dock Script passato al suono"), edited, true);
        // Il suono non applica la texture: ne' il Run Sound ne' un suono della
        // Library devono far risultare applicato lo script in sospeso.
        m_mw->onRunCurrentScript();  wait(400);
        check(m_mw->m_audioController && m_mw->m_audioController->isPlaying(),
              QStringLiteral("Run Sound -> suona"));
        if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
        checkTextureCode(QStringLiteral("Run Sound con lo script in sospeso"), edited, true);
        if (selectSound(kSound))
            checkTextureCode(QStringLiteral("suono dalla Library con lo script in sospeso"), edited, true);
        setScriptMode(MainWindow::ScriptModeTexture);
        checkTextureCode(QStringLiteral("di nuovo sulla texture"), edited, true);
        m_mw->onRunCurrentScript();  wait(800);
        checkTextureCode(QStringLiteral("Run dello script"), edited);
        if (selectSound(kSound))
            checkTextureCode(QStringLiteral("suono dalla Library"), edited);
        // Suono scritto a mano nel dock Sound, GLSL senza marcatori: suona, e il
        // Save lo scrive dentro il suo blocco, non in mezzo alla texture.
        setScriptMode(MainWindow::ScriptModeSound);
        ui->txtScriptEditor->setPlainText(QStringLiteral(
            "vec2 mainSound(int samp, float time) {\n"
            "    return vec2(0.1 * sin(6.2831853 * 440.0 * time));\n"
            "}"));
        wait(200);
        m_mw->onRunCurrentScript();  wait(600);
        check(m_mw->m_audioController && m_mw->m_audioController->isPlaying(),
              QStringLiteral("suono GLSL scritto a mano -> suona"));
        if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
        checkTextureCode(QStringLiteral("suono GLSL scritto a mano"), edited);
        setScriptMode(MainWindow::ScriptModeSurface);
        checkTextureCode(QStringLiteral("dock Script sulla superficie"), edited);
    }
    if (loadRecord(QString::fromLatin1(kImplicitRecord)))
        checkTextureCode(QStringLiteral("poi un record Ray Marching"),
                         presetTextureCode(QString::fromLatin1(kImplicitRecord), LibraryType::Motion, false));

    // ---------------------------------------------------------------------
    // AUDIO: la scena ha UN brano. Un record che lo porta in due slot del file
    // (texture e sfondo: i Save vecchi lo scrivevano in entrambi) lo suona e lo
    // risalva una volta sola.
    const QString kTwiceSound = QStringLiteral("records/Rotations/Kerr Black Hole.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Audio: un solo brano (%1) ==").arg(kTwiceSound));
    if (loadRecord(kTwiceSound)) {
        static const QRegularExpression beginRe(R"(^\s*//\s*SOUND_BEGIN\s*$)",
                                                QRegularExpression::MultilineOption);
        auto blocks = [](const QString &c) {
            int n = 0;
            auto it = beginRe.globalMatch(c);
            while (it.hasNext()) { it.next(); ++n; }
            return n;
        };
        const LibraryItem saved = captureSave();
        const int inScene = blocks(m_mw->soundCode());
        const int inTex = blocks(saved.textureCode);
        const int inBg = blocks(saved.bgTextureCode);
        check(inScene == 1, QStringLiteral("record caricato -> un blocco audio nella scena (sono %1)")
                                .arg(inScene));
        check(inTex + inBg == 1,
              QStringLiteral("record caricato -> un blocco audio nel Save (texture %1, sfondo %2)")
                  .arg(inTex).arg(inBg));
        if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
    }

    // ---------------------------------------------------------------------
    // CODICE APPLICATO E MOTORE, parametrico: uno script che non compila non
    // diventa "applicato", e a texture spenta il motore non tiene il codice di
    // prima.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Codice texture: applicato e motore (%1) ==")
                       .arg(QString::fromLatin1(kParametricRecord)));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        setScriptMode(MainWindow::ScriptModeTexture);
        if (selectTexture(kMandelbrot))
            checkTextureCode(QStringLiteral("Mandelbrot dalla Library"),
                             presetTextureCode(kMandelbrot, LibraryType::Texture, true));
        const QString broken = QStringLiteral("return mix(u_col1, u_col2, questo_non_esiste);");
        ui->txtScriptEditor->setPlainText(broken);  wait(200);
        m_mw->onRunCurrentScript();  wait(800);
        checkTextureCode(QStringLiteral("Run di uno script che non compila"), broken, /*pendingEdit=*/true);
        setScriptMode(MainWindow::ScriptModeSurface);
        checkTextureCode(QStringLiteral("dock Script sulla superficie"), broken, true);
        pressNew();
        checkTextureCode(QStringLiteral("tasto NEW"), QStringLiteral(""));
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord))
        && loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json")))
        checkTextureCode(QStringLiteral("record con texture, poi una superficie"), QStringLiteral(""));
    // Ambito Mesh col dock Script sulla texture: l'editor mostra lo script della
    // FASCIA. Il Run delle equazioni non deve farlo diventare la texture di
    // superficie (sospetto annotato il 2026-08-03, mai riprodotto prima).
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        // La texture di SUPERFICIE resta quella del record, qualunque cosa si
        // faccia sulla fascia.
        const QString globalCode =
            presetTextureCode(QString::fromLatin1(kMultiMeshRecord), LibraryType::Motion, true);
        if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(1);  wait(300);
        if (selectTexture(QStringLiteral("textures/Procedurals/Porous Bone.json")))
            checkTextureCode(QStringLiteral("multi-mesh, fascia 1: Porous Bone dalla Library"), globalCode);
        setScriptMode(MainWindow::ScriptModeTexture);
        checkTextureCode(QStringLiteral("dock Script sulla texture della fascia"), globalCode);
        m_mw->onStartClicked();  wait(800);
        checkTextureCode(QStringLiteral("Run delle equazioni con la fascia selezionata"), globalCode);
        m_mw->onRunCurrentScript();  wait(800);
        checkTextureCode(QStringLiteral("Run dello script della fascia"), globalCode);
        setScriptMode(MainWindow::ScriptModeSurface);
        click(ui->radioMeshAll);
        checkTextureCode(QStringLiteral("ritorno ad All"), globalCode);
        // La scacchiera di default usa u_col1/u_col2: i picker Colore servono.
        // Il Run dello script di una FASCIA non deve cambiarlo (il flag "codice
        // custom" della texture globale si sporcava col codice della fascia).
        m_discardOnPrompt = true;
        click(ui->chkBoxTexture);  click(ui->chkBoxTexture);
        m_discardOnPrompt = false;
        checkTextureCode(QStringLiteral("All: texture spenta e riaccesa (default)"), QStringLiteral(""));
        check(m_mw->activeTextureUsesColors() && ui->radioTexColor1->isEnabled(),
              QStringLiteral("All: scacchiera di default -> picker Colore accesi"));
        click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(1);  wait(300);
        setScriptMode(MainWindow::ScriptModeTexture);
        m_mw->onRunCurrentScript();  wait(800);
        setScriptMode(MainWindow::ScriptModeSurface);
        click(ui->radioMeshAll);
        checkTextureCode(QStringLiteral("Run sulla fascia, poi All"), QStringLiteral(""));
        check(m_mw->activeTextureUsesColors() && ui->radioTexColor1->isEnabled(),
              QStringLiteral("dopo il Run sulla fascia -> picker Colore ancora accesi"));
    }

    // ---------------------------------------------------------------------
    // IMMAGINE DELLA TEXTURE DI SUPERFICIE: quella in GPU e' quella che dice lo
    // script e che scriverebbe il Save.
    auto presetImage = [this](const QString &rel) {
        static const QRegularExpression re(QStringLiteral(R"(^\s*//IMG:\s*(.*)$)"),
                                           QRegularExpression::MultilineOption);
        LibraryManager lm;
        const QRegularExpressionMatch m =
            re.match(lm.parseJson(m_root + QLatin1Char('/') + rel, LibraryType::Motion).textureCode);
        return m.hasMatch() ? QFileInfo(m.captured(1).trimmed()).fileName() : QString();
    };
    const QString kImageOnly   = QStringLiteral("records/Rotations/Hyperbolic Enneper.json");
    const QString kImageScript = QStringLiteral("records/Rotations/Boy Surface.json");
    const QString kRmImage     = QStringLiteral("records/Ray Marching/Schwarz P Circuit.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Immagine della texture di superficie: parametrico =="));
    if (loadRecord(kImageOnly))
        checkSurfaceImage(QStringLiteral("record con la sola immagine"), presetImage(kImageOnly));
    if (loadRecord(kImageScript)) {
        checkSurfaceImage(QStringLiteral("record con immagine e script"), presetImage(kImageScript));
        setScriptMode(MainWindow::ScriptModeTexture);
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkSurfaceImage(QStringLiteral("immagine dalla Library"), QStringLiteral("14.png"));
        setScriptMode(MainWindow::ScriptModeSound);
        checkSurfaceImage(QStringLiteral("dock Script passato al suono"), QStringLiteral("14.png"));
        setScriptMode(MainWindow::ScriptModeTexture);
        checkSurfaceImage(QStringLiteral("di nuovo sulla texture"), QStringLiteral("14.png"));
        if (selectTexture(kMandelbrot))
            checkSurfaceImage(QStringLiteral("Mandelbrot sopra l'immagine"), QStringLiteral("14.png"));
        click(ui->chkBoxTexture);   checkSurfaceImage(QStringLiteral("texture spenta"), QStringLiteral(""));
        click(ui->chkBoxTexture);   checkSurfaceImage(QStringLiteral("texture riaccesa (default)"), QStringLiteral(""));
        if (selectTexture(QStringLiteral("textures/Images/15.png")))
            checkSurfaceImage(QStringLiteral("altra immagine dalla Library"), QStringLiteral("15.png"));
        // Scelta col dock Script su un altro modulo: lo slot dello script deve
        // dire l'immagine come l'editor l'avrebbe detta.
        setScriptMode(MainWindow::ScriptModeSurface);
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkSurfaceImage(QStringLiteral("immagine scelta col dock sulla superficie"), QStringLiteral("14.png"));
        setScriptMode(MainWindow::ScriptModeTexture);
        checkSurfaceImage(QStringLiteral("dock Script sulla texture"), QStringLiteral("14.png"));
        setScriptMode(MainWindow::ScriptModeSurface);
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord)))
        checkSurfaceImage(QStringLiteral("poi un record senza immagine"), QStringLiteral(""));
    if (loadRecord(kImageScript)) {
        // Il riclic sulla stessa procedurale scarta l'immagine della superficie:
        // riguarda la SUPERFICIE, non lo sfondo.
        const QString kPlasma = QStringLiteral("textures/Procedurals/Plasma.json");
        click(ui->radioBackground);
        if (selectTexture(kPlasma) && selectTexture(kPlasma))
            checkSurfaceImage(QStringLiteral("sfondo: stessa procedurale ricliccata"), presetImage(kImageScript));
        // La stessa voce scelta di nuovo SENZA essere riconosciuta come attiva
        // (sfondo spento nel frattempo): qui il vecchio codice toglieva dal Save
        // l'immagine della superficie.
        click(ui->chkBoxTexture);
        if (selectTexture(kPlasma))
            checkSurfaceImage(QStringLiteral("sfondo spento e stessa procedurale scelta di nuovo"), presetImage(kImageScript));
        click(ui->radioSurface);
        checkSurfaceImage(QStringLiteral("ritorno a Surface"), presetImage(kImageScript));
        pressNew();
        checkSurfaceImage(QStringLiteral("tasto NEW"), QStringLiteral(""));
    }
    if (loadRecord(kImageScript)) {
        if (selectTexture(kMandelbrot))
            checkSurfaceImage(QStringLiteral("Mandelbrot sopra l'immagine del record"), presetImage(kImageScript));
        // Riclic sulla texture gia' attiva: riparte l'orologio, tornano colori e
        // inquadratura del preset, l'immagine resta.
        if (selectTexture(kMandelbrot))
            checkSurfaceImage(QStringLiteral("Mandelbrot ricliccato: l'immagine resta"), presetImage(kImageScript));
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Immagine della texture di superficie: Ray Marching (%1) ==").arg(kRmImage));
    if (loadRecord(kRmImage)) {
        checkSurfaceImage(QStringLiteral("record con immagine"), presetImage(kRmImage));
        if (selectTexture(QStringLiteral("textures/Images/15.png")))
            checkSurfaceImage(QStringLiteral("immagine dalla Library"), QStringLiteral("15.png"));
        if (selectTexture(kFbm))
            checkSurfaceImage(QStringLiteral("texture procedurale dalla Library"), QStringLiteral(""));
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            checkSurfaceImage(QStringLiteral("di nuovo un'immagine"), QStringLiteral("14.png"));
        click(ui->chkBoxTexture);   checkSurfaceImage(QStringLiteral("texture spenta"), QStringLiteral(""));
        click(ui->chkBoxTexture);   checkSurfaceImage(QStringLiteral("texture riaccesa (default)"), QStringLiteral(""));
    }
    if (loadRecord(QString::fromLatin1(kImplicitRecord)))
        checkSurfaceImage(QStringLiteral("poi un record RM senza immagine"), QStringLiteral(""));

    // ---------------------------------------------------------------------
    // COSTANTI A..F/S: campo, slider, motore e Save dicono lo stesso valore.
    using KV = QMap<QString, double>;
    const QString kPlasmaTex = QStringLiteral("textures/Procedurals/Plasma.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Costanti: parametrico (%1) ==").arg(QString::fromLatin1(kParametricRecord)));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        checkConstants(QStringLiteral("record caricato"), KV{ { "A", 3.22 }, { "B", 0.21 } });
        setConstantBySlider(QStringLiteral("A"), 2.5);
        checkConstants(QStringLiteral("A dallo slider"), KV{ { "A", 2.5 }, { "B", 0.21 } });
        setConstantByField(QStringLiteral("B"), QStringLiteral("0.4"));
        checkConstants(QStringLiteral("B dal campo"), KV{ { "A", 2.5 }, { "B", 0.4 } });
        setConstantByField(QStringLiteral("B"), QStringLiteral("A/10"));
        checkConstants(QStringLiteral("B = A/10 (cascata)"), KV{ { "A", 2.5 }, { "B", 0.25 } });
        setConstantBySlider(QStringLiteral("A"), 3.0);
        checkConstants(QStringLiteral("A dallo slider, B la segue"), KV{ { "A", 3.0 }, { "B", 0.3 } });
        setConstantByField(QStringLiteral("B"), QStringLiteral("0.21"));
        if (selectTexture(kMandelbrot))
            checkConstants(QStringLiteral("Mandelbrot dalla Library (usa F)"), KV{ { "A", 3.0 }, { "B", 0.21 } });
        setConstantBySlider(QStringLiteral("F"), 2.0);
        checkConstants(QStringLiteral("F dallo slider"), KV{ { "F", 2.0 } });
        if (selectTexture(kPlasmaTex))
            checkConstants(QStringLiteral("poi una texture che non usa F"), KV{ { "A", 3.0 }, { "F", 1.0 } });
        click(ui->radioBackground);
        if (selectTexture(kMandelbrot))
            checkConstants(QStringLiteral("Mandelbrot come sfondo (usa F)"));
        setConstantBySlider(QStringLiteral("F"), 1.5);
        checkConstants(QStringLiteral("F dallo slider, per lo sfondo"), KV{ { "F", 1.5 } });
        click(ui->radioSurface);
        checkConstants(QStringLiteral("ritorno a Surface"), KV{ { "A", 3.0 }, { "F", 1.5 } });
        pressNew();
        checkConstants(QStringLiteral("tasto NEW"));
    }
    // Una costante che CADE IN DISUSO torna al valore neutro, e il motore lo sa.
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        if (selectTexture(kMandelbrot)) {
            setConstantBySlider(QStringLiteral("F"), 2.0);
            checkConstants(QStringLiteral("Mandelbrot con F = 2"), KV{ { "F", 2.0 } });
            m_discardOnPrompt = true;
            click(ui->chkBoxTexture);
            m_discardOnPrompt = false;
            checkConstants(QStringLiteral("texture spenta: F non serve piu'"), KV{ { "F", 1.0 } });
        }
        // Equazioni riscritte senza B (come digitando nei campi), poi Run.
        ui->lineX->setPlainText(QStringLiteral("A * cos(u)"));
        ui->lineY->setPlainText(QStringLiteral("A * sin(u)"));
        ui->lineZ->setPlainText(QStringLiteral("v"));
        wait(400);
        // Prima del Run a schermo c'e' ancora la superficie che usa B: B resta
        // accesa col suo valore.
        checkConstants(QStringLiteral("equazioni riscritte senza B, prima del Run"),
                       KV{ { "A", 3.22 }, { "B", 0.21 } });
        // B rimessa nell'equazione PRIMA del Run: il suo valore non deve essersi perso.
        ui->lineZ->setPlainText(QStringLiteral("B * v"));
        wait(400);
        checkConstants(QStringLiteral("B rimessa nell'equazione prima del Run"), KV{ { "A", 3.22 }, { "B", 0.21 } });
        // Il tasto Run del dock Equations (non la funzione chiamata a mano: e' il
        // tasto, come l'Invio, ad aggiornare lo snapshot dell'ultimo Run).
        check(ui->btnRunParametric->isEnabled(), QStringLiteral("equazioni modificate -> tasto Run acceso"));
        click(ui->btnRunParametric);  wait(800);
        checkConstants(QStringLiteral("Run"), KV{ { "A", 3.22 }, { "B", 0.21 } });
        // Tolta di nuovo, e questa volta col Run: ora B non serve davvero piu'.
        ui->lineZ->setPlainText(QStringLiteral("v"));
        wait(400);
        click(ui->btnRunParametric);  wait(800);
        checkConstants(QStringLiteral("B tolta e Run: torna neutra"), KV{ { "A", 3.22 }, { "B", 1.0 } });
    }
    // CASCATA: B = A/10, poi equazioni che usano solo B. A resta in uso tramite
    // B: non deve tornare a 1 al Run (cambierebbe B, e la superficie).
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        setConstantByField(QStringLiteral("B"), QStringLiteral("A/10"));
        checkConstants(QStringLiteral("B = A/10"), KV{ { "A", 3.22 }, { "B", 0.322 } });
        ui->lineX->setPlainText(QStringLiteral("(1 + B * cos(v)) * cos(u)"));
        ui->lineY->setPlainText(QStringLiteral("(1 + B * cos(v)) * sin(u)"));
        ui->lineZ->setPlainText(QStringLiteral("B * sin(v)"));
        wait(400);
        checkConstants(QStringLiteral("equazioni con la sola B, prima del Run"), KV{ { "A", 3.22 }, { "B", 0.322 } });
        click(ui->btnRunParametric);  wait(800);
        // A non compare piu' nelle equazioni, ma B = A/10 la usa: resta in uso.
        checkConstants(QStringLiteral("Run: A resta in uso tramite B = A/10"), KV{ { "A", 3.22 }, { "B", 0.322 } });
        check(ui->lineA->isEnabled(), QStringLiteral("A accesa (la usa B)"));
        setConstantBySlider(QStringLiteral("A"), 2.0);
        checkConstants(QStringLiteral("A dallo slider, B la segue"), KV{ { "A", 2.0 }, { "B", 0.2 } });
    }
    // Texture con costanti su una FASCIA (Ergosphere Band usa A e B).
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        checkConstants(QStringLiteral("multi-mesh caricato"));
        if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(1);  wait(300);
        if (selectTexture(QStringLiteral("textures/Procedurals/Static Holography.json")))
            checkConstants(QStringLiteral("fascia 1: Static Holography (usa F)"));
        setConstantBySlider(QStringLiteral("F"), 2.0);
        checkConstants(QStringLiteral("F dallo slider, per la fascia"), KV{ { "F", 2.0 } });
        click(ui->radioMeshAll);
        checkConstants(QStringLiteral("ritorno ad All"), KV{ { "F", 2.0 } });
    }
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Costanti: Ray Marching (%1) ==").arg(kTunnel));
    if (loadRecord(kTunnel)) {
        checkConstants(QStringLiteral("record caricato"),
                       KV{ { "A", 5 }, { "B", 1.54 }, { "D", 4.02 }, { "F", 0.52 }, { "S", 0.2 } });
        setConstantBySlider(QStringLiteral("A"), 3.4);
        checkConstants(QStringLiteral("A discreta: al rilascio scatta all'intero"), KV{ { "A", 3 } });
        setConstantByField(QStringLiteral("D"), QStringLiteral("2.5"));
        checkConstants(QStringLiteral("D dal campo"), KV{ { "A", 3 }, { "D", 2.5 } });
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord)))
        checkConstants(QStringLiteral("poi il record parametrico"), KV{ { "A", 3.22 }, { "B", 0.21 } });
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json")))
        checkConstants(QStringLiteral("poi una superficie"));
    if (loadRecord(kTunnel))
        checkConstants(QStringLiteral("di nuovo il record Ray Marching"),
                       KV{ { "A", 5 }, { "B", 1.54 }, { "D", 4.02 }, { "F", 0.52 }, { "S", 0.2 } });

    // ---------------------------------------------------------------------
    // COSTANTI DISCRETE E MINIMI (direttive "B := int(1,6);", "A := min(1.0);"
    // dello script): valgono per la scena che le dichiara, non per quella dopo.
    const QString kLabyrinth = QStringLiteral("records/Solid Wireframe/Clifford Labyrinth.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Costanti discrete e minimi (%1) ==").arg(kLabyrinth));
    if (loadRecord(kLabyrinth)) {
        checkConstants(QStringLiteral("record caricato"), KV{ { "A", 2.42 }, { "B", 3 } });
        setConstantBySlider(QStringLiteral("B"), 4.4);
        checkConstants(QStringLiteral("B discreta: 4.4 scatta a 4"), KV{ { "B", 4 } });
        setConstantBySlider(QStringLiteral("B"), 9.0);
        checkConstants(QStringLiteral("B discreta: 9 si ferma a 6"), KV{ { "B", 6 } });
        setConstantBySlider(QStringLiteral("A"), 0.5);
        checkConstants(QStringLiteral("A con minimo 1: 0.5 torna a 1"), KV{ { "A", 1.0 } });
        setConstantByField(QStringLiteral("A"), QStringLiteral("0.2"));
        checkConstants(QStringLiteral("A dal campo: 0.2 torna a 1"), KV{ { "A", 1.0 } });
        check(captureSave().discreteConstants.contains(QStringLiteral("B")),
              QStringLiteral("il Save scrive B fra le costanti discrete"));
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        setConstantBySlider(QStringLiteral("A"), 2.34);
        setConstantBySlider(QStringLiteral("B"), 0.37);
        checkConstants(QStringLiteral("poi un record senza direttive: A e B libere"),
                       KV{ { "A", 2.34 }, { "B", 0.37 } });
        check(captureSave().discreteConstants.isEmpty(),
              QStringLiteral("record senza direttive -> il Save non scrive costanti discrete"));
    }
    // Superficie con minimo (Octahedron Bands: A := min(0.3)), poi una
    // superficie a equazioni che usa A: il minimo non deve seguirla.
    if (loadSurface(QStringLiteral("surfaces/Parametric/Multimesh/Solid Wireframe/Octahedron Bands.json"))) {
        setConstantBySlider(QStringLiteral("A"), 0.1);
        checkConstants(QStringLiteral("superficie con minimo 0.3: 0.1 torna a 0.3"), KV{ { "A", 0.3 } });
    }
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json"))) {
        checkConstants(QStringLiteral("poi una superficie a equazioni"), KV{ { "A", 0.8 }, { "B", 0.3 } });
        setConstantBySlider(QStringLiteral("A"), 0.1);
        checkConstants(QStringLiteral("A libera: 0.1 resta 0.1"), KV{ { "A", 0.1 } });
    }

    // ---------------------------------------------------------------------
    // IL FILE VINCE SULLE DIRETTIVE DI VALORE DELLO SCRIPT. Una superficie da
    // script che dichiara "A := 10.0;" e "u_max := 1.0;", salvata con A e u max
    // ritoccati, si riapre coi valori salvati: le direttive valgono al Run
    // dell'utente. Prima il load le riapplicava (A dagli slider nelle
    // superfici, u max dal campo anche nei record) e il ritocco andava perso.
    {
        const QString kEnneper = QStringLiteral("surfaces/Parametric/Equations/R3/Symmetrized Double Enneper.json");
        m_lines.append(QString());
        m_lines.append(QStringLiteral("== Direttive dello script contro valori salvati (%1) ==").arg(kEnneper));
        QTemporaryDir tmp;
        // Save -> file -> stesso parser dell'albero -> load, come l'utente.
        auto reopen = [&](const LibraryItem &saved, LibraryType type) {
            const QString path = tmp.path() + QStringLiteral("/saved.json");
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            f.write(QJsonDocument(LibraryManager::toJson(saved)).toJson());
            f.close();
            LibraryManager lm;
            const LibraryItem item = lm.parseJson(path, type);
            if (item.name.isEmpty()) return false;
            if (type == LibraryType::Surface) m_mw->applySurfaceExample(item);
            else                              m_mw->applyMotionExample(item);
            if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
            wait(1500);
            return true;
        };
        auto touchUp = [&]() {
            setConstantBySlider(QStringLiteral("A"), 7.5);
            typeInField(ui->uMaxEdit, QStringLiteral("0.8"));
            pressEnter(ui->uMaxEdit);
        };
        auto checkKept = [&](const QString &how) {
            checkConstants(how + QStringLiteral(": A resta 7.5"), KV{ { "A", 7.5 }, { "B", 0.1 } });
            check(ui->uMaxEdit->text() == QLatin1String("0.8"),
                  how + QStringLiteral(": u max resta 0.8 (campo '%1')").arg(ui->uMaxEdit->text()));
        };
        if (tmp.isValid() && loadSurface(kEnneper)) {
            checkConstants(QStringLiteral("superficie da script caricata"), KV{ { "A", 10 }, { "B", 0.1 } });
            touchUp();
            check(reopen(m_mw->m_presetSerializer->captureSurfaceState(QStringLiteral("saved")),
                         LibraryType::Surface),
                  QStringLiteral("Save Surface riaperto"));
            checkKept(QStringLiteral("riaperta come superficie"));
        }
        if (tmp.isValid() && loadSurface(kEnneper)) {
            touchUp();
            m_record = QStringLiteral("saved");
            check(reopen(captureSave(), LibraryType::Motion), QStringLiteral("Save Record riaperto"));
            checkKept(QStringLiteral("riaperta come record"));
        }
        // Record da script aperto dopo uno METRICO: l'uscita dalla modalita'
        // metrica rigiudica i limiti, e su una superficie da script (X/Y/Z/P
        // vuoti) svuotava u e v. Li riscrivevano le direttive, riapplicate a
        // ogni load; ora vale il file, e i limiti devono restare.
        if (loadRecord(QStringLiteral("records/Rotations/Wormhole.json"))
            && loadRecord(QStringLiteral("records/Solid Wireframe/Clifford Labyrinth.json"))) {
            check(ui->uMaxEdit->isEnabled() && !ui->uMaxEdit->text().isEmpty()
                      && !ui->vMaxEdit->text().isEmpty(),
                  QStringLiteral("record da script dopo uno metrico: limiti u/v presenti (u max '%1', v max '%2')")
                      .arg(ui->uMaxEdit->text(), ui->vMaxEdit->text()));
        }
    }

    // ---------------------------------------------------------------------
    // COSTANTE PIU' FINE DEL PASSO DELLO SLIDER (0.01): la fonte e' il campo.
    // Chi rileggeva lo slider vedeva 0.005 come 0 -- il fattore conforme del
    // flusso geodetico "200*A" risultava nullo e il Run dava errore.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Costanti: valore piu' fine del passo dello slider =="));
    if (loadSurface(QStringLiteral("surfaces/Parametric/Geodesic Flow/R^3/Helicoid.json"))) {
        ui->lineConform->setPlainText(QStringLiteral("200*A"));  wait(400);
        // Una costante scritta e non ancora eseguita resta BLOCCATA fino al Run
        // (deciso con l'utente il 2026-10-07): lo slider non muoverebbe nulla.
        check(!ui->aSlider->isEnabled() && !ui->lineA->isEnabled(),
              QStringLiteral("fattore conforme 200*A scritto, prima del Run: A bloccata"));
        applyEquationEdit(ui->lineConform);
        check(ui->aSlider->isEnabled() && ui->lineA->isEnabled(),
              QStringLiteral("dopo il Run: A sbloccata"));
        setConstantByField(QStringLiteral("A"), QStringLiteral("0.005"));
        checkConstants(QStringLiteral("fattore conforme 200*A, A = 0.005 dal campo"), KV{ { "A", 0.005 } });
        // Riscritto: il Run torna disponibile e rilegge le costanti.
        ui->lineConform->setPlainText(QStringLiteral("200*A"));  wait(400);
        const int popups = m_popupsClosed;
        applyEquationEdit(ui->lineConform);
        check(m_popupsClosed == popups,
              QStringLiteral("Run col fattore conforme 200*A = 1 -> nessun errore"));
        check(m_mw->m_eqApplied && m_mw->m_eqApplied->conform == QLatin1String("200*A"),
              QStringLiteral("Run col fattore conforme 200*A = 1 -> applicato"));
        checkConstants(QStringLiteral("dopo il Run"), KV{ { "A", 0.005 } });
    }
    // Lo stesso con lo script di superficie: dopo il Run il motore ha il valore
    // del campo, non quello dello slider.
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json"))) {
        setConstantByField(QStringLiteral("A"), QStringLiteral("0.805"));
        setScriptMode(MainWindow::ScriptModeSurface);
        ui->txtScriptEditor->setPlainText(QStringLiteral(
            "return vec4((A + 0.3*cos(v))*cos(u), (A + 0.3*cos(v))*sin(u), 0.3*sin(v), 0.0);"));
        wait(200);
        const int popups = m_popupsClosed;
        m_mw->onRunCurrentScript();  wait(800);
        check(m_popupsClosed == popups && ui->glWidget->getEngine()->isScriptModeActive(),
              QStringLiteral("Run di uno script che usa A = 0.805 -> a schermo, nessun errore"));
        checkConstants(QStringLiteral("dopo il Run dello script"), KV{ { "A", 0.805 } });
    }

    // ---------------------------------------------------------------------
    // RESET DELLA SCENA (NEW, cambio di linguetta): la scena nuova non eredita
    // il bersaglio Background ne' l'inquadratura 2D della texture di prima.
    // Il checkbox, la sua etichetta e l'editor seguono il bersaglio Surface,
    // in entrambe le modalita' e da qualunque stato si parta.
    auto checkTextureReset = [this, ui](const QString &step) {
        GLWidget *gl = ui->glWidget;
        QStringList bad;
        if (m_mw->editingBackground()) bad << QStringLiteral("bersaglio ancora su Background");
        if (!editTargetProblem().isEmpty()) bad << editTargetProblem();
        if (ui->chkBoxTexture->text() != QLatin1String("Texture"))
            bad << QStringLiteral("etichetta del checkbox \"%1\"").arg(ui->chkBoxTexture->text());
        if (ui->chkBoxTexture->isChecked()) bad << QStringLiteral("checkbox acceso");
        if (m_mw->m_scene.surfaceTextureState) bad << QStringLiteral("intenzione accesa");
        if (gl->isBackgroundTextureEnabled()) bad << QStringLiteral("sfondo acceso");
        if (m_mw->m_currentScriptMode == MainWindow::ScriptModeTexture
            && !ui->txtScriptEditor->toPlainText().trimmed().isEmpty())
            bad << QStringLiteral("editor ancora pieno: ") + briefCode(ui->txtScriptEditor->toPlainText());
        if (qAbs(gl->globalTexZoom() - 1.0f) > 1e-5f || !gl->globalTexPan().isNull()
            || qAbs(gl->globalTexRotation()) > 1e-5f)
            bad << QStringLiteral("inquadratura della texture: zoom %1, pan (%2, %3), rotazione %4")
                       .arg(gl->globalTexZoom()).arg(gl->globalTexPan().x())
                       .arg(gl->globalTexPan().y()).arg(gl->globalTexRotation());
        if (qAbs(gl->backgroundZoom() - 1.0f) > 1e-5f || !gl->backgroundPan().isNull()
            || qAbs(gl->backgroundRotation()) > 1e-5f)
            bad << QStringLiteral("inquadratura dello sfondo: zoom %1, pan (%2, %3), rotazione %4")
                       .arg(gl->backgroundZoom()).arg(gl->backgroundPan().x())
                       .arg(gl->backgroundPan().y()).arg(gl->backgroundRotation());
        if (m_mw->m_textureModified)
            bad << QStringLiteral("la texture risulta ancora modificata a mano");
        check(bad.isEmpty(), step + QStringLiteral(" -> ")
                                 + (bad.isEmpty() ? QStringLiteral("bersaglio Surface, texture spenta, inquadratura al default")
                                                  : bad.join(QStringLiteral("; "))));
    };
    auto switchModeTab = [this, ui](int index) {
        m_discardOnPrompt = true;
        ui->tabModeSelector->setCurrentIndex(index);  wait(1500);
        m_discardOnPrompt = false;
    };
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Reset della scena: bersaglio e inquadratura della texture =="));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        check(qAbs(ui->glWidget->globalTexZoom() - 1.0f) > 0.5f,
              QStringLiteral("record caricato -> texture zoomata (zoom %1)").arg(ui->glWidget->globalTexZoom()));
        // Script della texture ritoccato a mano, poi buttato via col NEW.
        setScriptMode(MainWindow::ScriptModeTexture);
        ui->txtScriptEditor->setPlainText(ui->txtScriptEditor->toPlainText() + QStringLiteral("\n// ritocco"));
        wait(200);
        pressNew();
        checkTextureReset(QStringLiteral("tasto NEW dal bersaglio Surface"));
    }
    // Sfondo zoomato e ruotato dal record: la scena nuova riparte dal default.
    if (loadRecord(QStringLiteral("records/Rotations/Scherk's Second Surface.json"))) {
        check(qAbs(ui->glWidget->backgroundZoom() - 1.0f) > 0.5f,
              QStringLiteral("record caricato -> sfondo zoomato (zoom %1)").arg(ui->glWidget->backgroundZoom()));
        pressNew();
        checkTextureReset(QStringLiteral("tasto NEW dopo un record con lo sfondo zoomato"));
    }
    if (loadRecord(QStringLiteral("records/Ray Marching/Metaball.json"))) {
        pressNew();
        checkTextureReset(QStringLiteral("Ray Marching: tasto NEW dopo un record con lo sfondo zoomato"));
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        click(ui->radioBackground);
        setScriptMode(MainWindow::ScriptModeTexture);
        selectTexture(kBgPlasma);
        pressNew();
        checkTextureReset(QStringLiteral("tasto NEW dal bersaglio Background, sfondo con script"));
        checkDirty(QStringLiteral("tasto NEW dal bersaglio Background"), false, false, false);
    }
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        click(ui->radioBackground);
        switchModeTab(1);
        checkTextureReset(QStringLiteral("linguetta Ray Marching dal bersaglio Background"));
        switchModeTab(0);
        checkTextureReset(QStringLiteral("ritorno alla linguetta Parametric"));
    }
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        click(ui->radioBackground);
        pressNew();
        checkTextureReset(QStringLiteral("Ray Marching: tasto NEW dal bersaglio Background"));
    }
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        click(ui->radioBackground);
        switchModeTab(0);
        checkTextureReset(QStringLiteral("Ray Marching: linguetta Parametric dal bersaglio Background"));
    }
    setScriptMode(MainWindow::ScriptModeSurface);

    // ---------------------------------------------------------------------
    // CROSS SECTION: e' del preset che lo dichiara. Un record (o una superficie)
    // Ray Marching DA SCRIPT caricato dopo un Cross Section non deve restare
    // nel sotto-tab e nel ramo del motore di quello di prima.
    const QString kCrossRec  = QStringLiteral("records/Ray Marching/Cross Sections/T3/Morphing 3-Torus.json");
    const QString kCrossSurf = QStringLiteral("surfaces/Ray Marching/Cross Sections/Duocylinder.json");
    auto checkBranch = [this, ui](const QString &step, bool expectedCross) {
        const bool saved  = captureSave().usesCrossSection;
        const bool tab    = ui->subTabImplicit->currentIndex() == 1;
        const bool engine = ui->glWidget->implicitUsesCrossSection();
        QStringList bad;
        if (tab != expectedCross)
            bad << QStringLiteral("sotto-tab %1").arg(tab ? QStringLiteral("Cross Section") : QStringLiteral("3D"));
        if (engine != expectedCross)
            bad << QStringLiteral("motore sul ramo %1").arg(engine ? QStringLiteral("Cross Section") : QStringLiteral("3D"));
        if (saved != expectedCross)
            bad << QStringLiteral("il Save scriverebbe implicitUsesCrossSection %1").arg(onOff(saved));
        check(bad.isEmpty(), QStringLiteral("%1 -> ramo %2%3")
                                 .arg(step, expectedCross ? QStringLiteral("Cross Section") : QStringLiteral("3D"),
                                      bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
    };
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Cross Section e preset Ray Marching da script =="));
    if (loadRecord(kCrossRec))
        checkBranch(QStringLiteral("record Cross Section"), true);
    if (loadRecord(QString::fromLatin1(kImplicitRecord)))
        checkBranch(QStringLiteral("poi un record Ray Marching da script"), false);
    if (loadRecord(kCrossRec))
        checkBranch(QStringLiteral("di nuovo il record Cross Section"), true);
    if (loadSurface(QStringLiteral("surfaces/Ray Marching/Bitorus.json")))
        checkBranch(QStringLiteral("poi una superficie Ray Marching da script"), false);
    if (loadSurface(kCrossSurf))
        checkBranch(QStringLiteral("superficie Cross Section"), true);
    if (loadRecord(kTunnel))
        checkBranch(QStringLiteral("poi un record Ray Marching da script"), false);
    if (loadSurface(kCrossSurf) && loadRecord(QString::fromLatin1(kParametricRecord))
        && loadRecord(QString::fromLatin1(kImplicitRecord)))
        checkBranch(QStringLiteral("Cross Section, parametrico, poi Ray Marching da script"), false);

    // ---------------------------------------------------------------------
    // EQUAZIONI: campi, applicato, Save. Il Run si da' col tasto del dock.
    const QString kRmEq = QStringLiteral("records/Ray Marching/Sphere-Tori Merging.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Equazioni: Ray Marching (%1) ==").arg(kRmEq));
    if (loadRecord(kRmEq)) {
        checkEquations(QStringLiteral("record caricato"));
        ui->lineEquation->setPlainText(QStringLiteral("x^2 + y^2 + z^2 = 1.5"));  wait(300);
        checkEquations(QStringLiteral("equazione 3D modificata a mano"), /*pendingEdit=*/true);
        click(ui->btnImplicit);  wait(800);
        checkEquations(QStringLiteral("Run"));
        // Limiti spaziali digitati: in sospeso fino al Run.
        typeInField(ui->lineXMin, QStringLiteral("-1"));
        typeInField(ui->lineXMax, QStringLiteral("0.5"));
        checkEquations(QStringLiteral("limiti x digitati"), /*pendingEdit=*/true);
        click(ui->btnImplicit);  wait(800);
        checkEquations(QStringLiteral("Run coi limiti"));
        // Una costante nel limite: si applica al Run e poi segue lo slider.
        typeInField(ui->lineXMax, QStringLiteral("A/2"));
        click(ui->btnImplicit);  wait(800);
        checkEquations(QStringLiteral("x max = A/2, Run"));
        setConstantBySlider(QStringLiteral("A"), 1.6);
        checkEquations(QStringLiteral("x max = A/2, slider di A a 1.6"));
        ui->subTabImplicit->setCurrentIndex(1);  wait(1000);
        checkEquations(QStringLiteral("sotto-tab Cross Section (default)"));
        ui->lineEquationCrossSection->setPlainText(QStringLiteral("x^2 + y^2 + z^2 + p^2 = 1.2"));  wait(300);
        checkEquations(QStringLiteral("equazione Cross Section modificata a mano"), true);
        click(ui->btnImplicit);  wait(800);
        checkEquations(QStringLiteral("Run"));
        ui->subTabImplicit->setCurrentIndex(0);  wait(1000);
        checkEquations(QStringLiteral("ritorno al sotto-tab 3D"));
    }
    if (loadSurface(kCrossSurf))
        checkEquations(QStringLiteral("superficie Cross Section"));
    if (loadRecord(kRmEq))
        checkEquations(QStringLiteral("poi il record 3D"));
    if (loadRecord(QString::fromLatin1(kImplicitRecord)))
        checkEquations(QStringLiteral("poi un record da script"));
    // Cambio di modalita' dopo un Cross Section in rotazione: lo stato 4D della
    // sezione non deve seguire la scena nuova.
    if (loadRecord(kCrossRec)) {
        checkEquations(QStringLiteral("record Cross Section in rotazione"));
        m_discardOnPrompt = true;
        ui->tabModeSelector->setCurrentIndex(0);  wait(1200);
        m_discardOnPrompt = false;
        checkEquations(QStringLiteral("linguetta Parametric"));
        check(qFuzzyIsNull(ui->glWidget->crossSectionP()),
              QStringLiteral("in parametrico -> nessuna traslazione del piano di sezione"));
        m_discardOnPrompt = true;
        ui->tabModeSelector->setCurrentIndex(1);  wait(1200);
        m_discardOnPrompt = false;
        // Si riapre il sotto-tab dov'era (Cross Section, con la sua default):
        // checkEquations verifica che sotto-tab, motore e Save siano d'accordo.
        checkEquations(QStringLiteral("di nuovo linguetta Ray Marching"));
    }

    // STATO 4D DEL CROSS SECTION: angoli, velocita' delle rotazioni 4D e
    // traslazione p del piano di sezione vivono nel motore e sono della sezione.
    // Nel Cross Section il Save scrive cio' che il motore mostra; nel Ray
    // Marching 3D non resta nulla ne' nel motore (un moto 4D che continua a
    // girare senza effetto) ne' nel Save.
    auto check4D = [this, ui](const QString &step, bool expectCross) {
        GLWidget *gl = ui->glWidget;
        const LibraryItem sv = captureSave();
        auto same = [](float a, float b) { return qAbs(a - b) < 1e-4f; };
        const bool moving = !same(gl->getOmegaSpeed(), 0) || !same(gl->getPhiSpeed(), 0)
                            || !same(gl->getPsiSpeed(), 0);
        QStringList bad;
        if (sv.usesCrossSection != expectCross)
            bad << QStringLiteral("il Save scriverebbe implicitUsesCrossSection %1").arg(onOff(sv.usesCrossSection));
        if (expectCross) {
            if (!same(sv.crossSectionP, gl->crossSectionP()))
                bad << QStringLiteral("p: Save %1, motore %2").arg(sv.crossSectionP).arg(gl->crossSectionP());
            if (!same(sv.speedOmega, gl->getOmegaSpeed()) || !same(sv.speedPhi, gl->getPhiSpeed())
                || !same(sv.speedPsi, gl->getPsiSpeed()))
                bad << QStringLiteral("velocita' 4D: Save %1/%2/%3, motore %4/%5/%6")
                           .arg(sv.speedOmega).arg(sv.speedPhi).arg(sv.speedPsi)
                           .arg(gl->getOmegaSpeed()).arg(gl->getPhiSpeed()).arg(gl->getPsiSpeed());
            if (!moving && (!same(sv.startOmega, gl->getOmega()) || !same(sv.startPhi, gl->getPhi())
                            || !same(sv.startPsi, gl->getPsi())))
                bad << QStringLiteral("angoli 4D: Save %1/%2/%3, motore %4/%5/%6")
                           .arg(sv.startOmega).arg(sv.startPhi).arg(sv.startPsi)
                           .arg(gl->getOmega()).arg(gl->getPhi()).arg(gl->getPsi());
        } else {
            if (!same(gl->crossSectionP(), 0))
                bad << QStringLiteral("nel motore resta la traslazione p = %1").arg(gl->crossSectionP());
            if (moving)
                bad << QStringLiteral("nel motore restano le velocita' 4D %1/%2/%3")
                           .arg(gl->getOmegaSpeed()).arg(gl->getPhiSpeed()).arg(gl->getPsiSpeed());
            // Il riposo del motore e' 0.0001, non 0 (GLWidget: posa 4D mai
            // esattamente piatta).
            auto rest = [](float a) { return qAbs(a) < 1e-3f; };
            if (!rest(gl->getOmega()) || !rest(gl->getPhi()) || !rest(gl->getPsi()))
                bad << QStringLiteral("nel motore restano gli angoli 4D %1/%2/%3")
                           .arg(gl->getOmega()).arg(gl->getPhi()).arg(gl->getPsi());
            if (!same(sv.speedOmega, 0) || !same(sv.speedPhi, 0) || !same(sv.speedPsi, 0)
                || !same(sv.startOmega, 0) || !same(sv.startPhi, 0) || !same(sv.startPsi, 0))
                bad << QStringLiteral("il Save scriverebbe stato 4D fuori dal Cross Section");
        }
        check(bad.isEmpty(), QStringLiteral("%1 -> stato 4D %2%3")
                                 .arg(step, expectCross ? QStringLiteral("della sezione") : QStringLiteral("assente"),
                                      bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
    };
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Stato 4D del Cross Section (%1) ==").arg(kCrossRec));
    if (loadRecord(kCrossRec)) {
        check4D(QStringLiteral("record Cross Section in rotazione"), true);
        // La sezione spostata lungo p (tasti della tastiera): e' stato da salvare.
        ui->glWidget->moveCrossSectionP(0.25f);  wait(200);
        check4D(QStringLiteral("piano di sezione spostato"), true);
        m_discardOnPrompt = true;
        ui->subTabImplicit->setCurrentIndex(0);  wait(1200);
        check4D(QStringLiteral("sotto-tab 3D"), false);
        ui->subTabImplicit->setCurrentIndex(1);  wait(1200);
        m_discardOnPrompt = false;
        check4D(QStringLiteral("di nuovo sotto-tab Cross Section (default)"), true);
    }
    if (loadRecord(kCrossRec)) {
        ui->glWidget->moveCrossSectionP(0.25f);  wait(200);
        if (loadRecord(kRmEq))
            check4D(QStringLiteral("record in rotazione, poi un record Ray Marching 3D"), false);
    }
    if (loadRecord(kCrossRec) && loadSurface(QStringLiteral("surfaces/Ray Marching/Bitorus.json")))
        check4D(QStringLiteral("record in rotazione, poi una superficie da script"), false);
    if (loadRecord(kCrossRec) && loadSurface(QStringLiteral("surfaces/Ray Marching/Sphere.json")))
        check4D(QStringLiteral("record in rotazione, poi una superficie a equazione"), false);
    // Una superficie e la scena vuota non hanno moto: le rotazioni 4D del
    // record di prima si fermano anche restando nel Cross Section.
    auto still4D = [ui]() {
        const GLWidget *gl = ui->glWidget;
        return qFuzzyIsNull(gl->getOmegaSpeed()) && qFuzzyIsNull(gl->getPhiSpeed())
               && qFuzzyIsNull(gl->getPsiSpeed());
    };
    if (loadRecord(kCrossRec) && loadSurface(kCrossSurf)) {
        check4D(QStringLiteral("record in rotazione, poi una superficie Cross Section"), true);
        check(still4D(), QStringLiteral("superficie Cross Section dopo il record -> rotazioni 4D ferme"));
    }
    if (loadRecord(kCrossRec)) {
        pressNew();
        check4D(QStringLiteral("record in rotazione, poi NEW"), ui->subTabImplicit->currentIndex() == 1);
        check(still4D() && qFuzzyIsNull(ui->glWidget->crossSectionP()),
              QStringLiteral("NEW -> rotazioni 4D ferme, piano di sezione a 0 (p = %1)")
                  .arg(ui->glWidget->crossSectionP()));
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Equazioni: parametrico (%1) ==").arg(QString::fromLatin1(kParametricRecord)));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        checkEquations(QStringLiteral("record caricato"));
        ui->lineZ->setPlainText(QStringLiteral("B*v * sin(u/2 + t) + 0.1"));  wait(300);
        checkEquations(QStringLiteral("z modificata a mano"), /*pendingEdit=*/true);
        // Il record e' animato: il tasto del dock dice Stop. La modifica si
        // applica con Invio nel campo.
        applyEquationEdit(ui->lineZ);
        checkEquations(QStringLiteral("modifica applicata (Invio o Run)"));
        // Si ferma l'animazione (tasto Stop del dock), poi Invio su una COSTANTE:
        // e' un commit di servizio, riusa le equazioni applicate e non deve
        // cambiarle -- nemmeno se sono state applicate al volo durante il moto.
        if (m_mw->isEquationModuleMoving()) { click(ui->btnRunParametric);  wait(400); }
        const QString applied1 = ui->glWidget->parametricEquationsApplied();
        ui->lineA->setText(QStringLiteral("3"));
        pressEnter(ui->lineA);
        check(ui->glWidget->parametricEquationsApplied() == applied1,
              QStringLiteral("Invio su una costante -> le equazioni applicate restano quelle"));
        checkEquations(QStringLiteral("dopo l'Invio sulla costante"));
        typeInField(ui->uMaxEdit, QStringLiteral("3.14"));
        pressEnter(ui->uMaxEdit);
        checkEquations(QStringLiteral("uMax dal campo, Invio"));
        typeInField(ui->vMaxEdit, QStringLiteral("A/4"));
        pressEnter(ui->vMaxEdit);
        checkEquations(QStringLiteral("vMax = A/4, Invio"));
    }
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json"))) {
        checkEquations(QStringLiteral("poi una superficie a equazioni"));
        // Record, poi superficie, poi Invio su una costante: le equazioni a
        // schermo devono restare quelle della superficie.
        const QString applied2 = ui->glWidget->parametricEquationsApplied();
        ui->lineA->setText(QStringLiteral("0.9"));
        pressEnter(ui->lineA);
        check(ui->glWidget->parametricEquationsApplied() == applied2,
              QStringLiteral("superficie dopo un record, Invio su una costante -> equazioni della superficie"));
        checkEquations(QStringLiteral("dopo l'Invio sulla costante"));
        // Il Run e' anche l'indicatore di completezza del dominio, e deve
        // seguire la digitazione stessa: u max vuoto -> spento, riscritto ->
        // acceso. Il gestore di textEdited legge lo stato, che Qt aggiorna
        // con textChanged DOPO: senza la scrittura anche su textEdited il
        // tasto restava indietro di un tasto.
        const QString uMax0 = ui->uMaxEdit->text();
        typeInField(ui->uMaxEdit, QString());
        check(!ui->btnRunParametric->isEnabled(), QStringLiteral("u max svuotato -> tasto Run spento"));
        typeInField(ui->uMaxEdit, QStringLiteral("A"));
        check(ui->btnRunParametric->isEnabled(), QStringLiteral("u max = A -> tasto Run acceso"));
        // Un limite confermato che cita una costante segue lo slider, come le
        // equazioni: il dominio del motore e' quello del campo rivalutato.
        pressEnter(ui->uMaxEdit);
        setConstantBySlider(QStringLiteral("A"), 2.0);
        checkEquations(QStringLiteral("u max = A, slider di A a 2"));
        typeInField(ui->uMaxEdit, uMax0);
        pressEnter(ui->uMaxEdit);
    }
    // COMPOSIZIONE scritta e non eseguita (Hyperbolic Saddle: W = C*u*cos(2*v),
    // le equazioni usano U, V, W): l'Invio su una costante non deve applicarla
    // a meta', cioe' composizione nuova sulle equazioni di prima.
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/H2xR/Hyperbolic Saddle.json"))) {
        checkEquations(QStringLiteral("superficie con composizione"));
        const QString applied3 = ui->glWidget->parametricEquationsApplied();
        ui->lineW->setPlainText(QStringLiteral("C * u * cos(3*v)"));  wait(300);
        checkEquations(QStringLiteral("composizione modificata, non eseguita"), /*pendingEdit=*/true);
        ui->lineC->setText(QStringLiteral("0.5"));
        pressEnter(ui->lineC);
        check(ui->glWidget->parametricEquationsApplied() == applied3,
              QStringLiteral("composizione in sospeso, Invio su una costante -> a schermo restano le equazioni di prima"));
        applyEquationEdit(ui->lineW);
        check(ui->glWidget->parametricEquationsApplied() != applied3,
              QStringLiteral("Run -> la composizione entra"));
        checkEquations(QStringLiteral("dopo il Run"));
    }
    // VINCOLO esplicito scritto e non eseguito (Tractricoid: w = u+v).
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/H2xR/Tractricoid.json"))) {
        checkEquations(QStringLiteral("superficie con vincolo"));
        const QString explicit0 = ui->glWidget->getEngine()->getExplicitW();
        ui->lineExplicitW->setPlainText(QStringLiteral("u-v"));  wait(300);
        ui->lineA->setText(QStringLiteral("1.2"));
        pressEnter(ui->lineA);
        check(ui->glWidget->getEngine()->getExplicitW() == explicit0,
              QStringLiteral("vincolo in sospeso, Invio su una costante -> a schermo resta il vincolo di prima"));
        applyEquationEdit(ui->lineExplicitW);
        check(ui->glWidget->getEngine()->getExplicitW() != explicit0,
              QStringLiteral("Run -> il vincolo entra"));
        checkEquations(QStringLiteral("dopo il Run"));
    }
    // FLUSSO GEODETICO: superficie, record, e poi una scena che non lo usa.
    if (loadSurface(QStringLiteral("surfaces/Parametric/Geodesic Flow/R^3/Helicoid.json"))) {
        checkEquations(QStringLiteral("superficie con flusso geodetico"));
        // Punto iniziale riscritto e non eseguito, poi Invio su una costante:
        // il flusso a schermo resta quello di prima, e il Run serve ancora.
        const QString u0 = ui->lnU->toPlainText();
        ui->lnU->setPlainText(u0 + QStringLiteral(" + 0.1"));  wait(300);
        ui->lineA->setText(QStringLiteral("1.1"));
        pressEnter(ui->lineA);
        check(m_mw->m_eqApplied && m_mw->m_eqApplied->geoU == u0,
              QStringLiteral("flusso in sospeso, Invio su una costante -> applicato resta il punto iniziale di prima"));
        checkEquations(QStringLiteral("flusso ancora in sospeso"), /*pendingEdit=*/true);
        applyEquationEdit(ui->lnU);
        checkEquations(QStringLiteral("flusso applicato"));
    }
    if (loadRecord(QStringLiteral("records/Geodesic Flow/R^3/Ruled Hyperboloid.json")))
        checkEquations(QStringLiteral("record con flusso geodetico"));
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json")))
        checkEquations(QStringLiteral("poi una superficie senza flusso"));
    if (loadSurface(QStringLiteral("surfaces/Parametric/Multimesh/Hopf Tori.json")))
        checkEquations(QStringLiteral("poi una superficie da script"));
    if (loadRecord(QString::fromLatin1(kParametricRecord)))
        checkEquations(QStringLiteral("poi di nuovo il record a equazioni"));
    pressNew();
    checkEquations(QStringLiteral("tasto NEW"));

    // ---------------------------------------------------------------------
    // PATH E MOTI: viste, velocita', tasti e timer, moto attivo, FOV, path
    // applicato (vedi checkMotion).
    const QString kPath4D   = QStringLiteral("records/Paths/Coiled Coil.json");       // path 4D, vista Center
    const QString kPath3D   = QStringLiteral("records/Paths/Calabi-Yau Orbit.json");  // path 3D, FOV 69, spin
    const QString kPathBoth = QStringLiteral("records/Paths/Cytherean Coil.json");    // path 4D + 3D + rotazioni
    const QString kRotRec   = QStringLiteral("records/Rotations/Boy Surface.json");
    const QString kTorusSurf = QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json");
    SurfaceEngine *engine = ui->glWidget->getEngine();

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Moti: path 4D (%1) ==").arg(kPath4D));
    if (loadRecord(kPath4D)) {
        checkMotion(QStringLiteral("record caricato"), QStringLiteral("path4D"));
        click(ui->btnDeparture);   checkMotion(QStringLiteral("Stop del path"), QStringLiteral("none"));
        click(ui->btnDeparture);   checkMotion(QStringLiteral("Departure"), QStringLiteral("path4D"));
        click(ui->pushView);       checkMotion(QStringLiteral("vista cambiata"), QStringLiteral("path4D"));
        click(ui->pushView);       checkMotion(QStringLiteral("vista rimessa"), QStringLiteral("path4D"));
        ui->speed4DSlider->setValue(40);  wait(200);
        checkMotion(QStringLiteral("velocita' dallo slider"), QStringLiteral("path4D"));
        ui->fovSliderMain->setValue(80);  wait(200);
        checkMotion(QStringLiteral("FOV dallo slider"), QStringLiteral("path4D"));
        // Modifica al volo: il campo riscritto resta in sospeso fino all'Invio.
        const QString x0 = ui->lineX_P->text();
        typeInField(ui->lineX_P, x0 + QStringLiteral(" + 0.1"));
        checkMotion(QStringLiteral("campo X riscritto"), QStringLiteral("path4D"), /*pendingEdit=*/true);
        pressEnter(ui->lineX_P);
        checkMotion(QStringLiteral("Invio: ricompilato al volo"), QStringLiteral("path4D"));
        // Un'equazione che non compila non deve togliere il path a quello in corsa.
        const QStringList applied0 = engine->appliedPath4D();
        typeInField(ui->lineX_P, QStringLiteral("cos(qq)"));
        pressEnter(ui->lineX_P);
        check(engine->path4DCompiled() && engine->appliedPath4D() == applied0,
              QStringLiteral("Invio su un'equazione che non compila -> a schermo resta il path di prima (motore [%1])")
                  .arg(engine->appliedPath4D().join(QStringLiteral(" | "))));
        typeInField(ui->lineX_P, x0);
        pressEnter(ui->lineX_P);
        checkMotion(QStringLiteral("equazione rimessa, Invio"), QStringLiteral("path4D"));
        // Rotazioni: il tasto + cambia la velocita', GO ferma il path.
        click(ui->btnSpinPlus);    checkMotion(QStringLiteral("spin +"), QStringLiteral("path4D"));
        click(ui->btnStart_2);     checkMotion(QStringLiteral("GO"), QStringLiteral("rotation"));
        click(ui->btnDeparture);   checkMotion(QStringLiteral("Departure a rotazioni in corso"), QStringLiteral("path4D"));
        // Campi svuotati e Invio: il path si ferma.
        for (QLineEdit *l : { ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P,
                              ui->lineAlpha_P, ui->lineBeta_P, ui->lineGamma_P })
            typeInField(l, QString());
        pressEnter(ui->lineX_P);
        checkMotion(QStringLiteral("campi svuotati, Invio"), QStringLiteral("none"));
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Moti: path 3D (%1) ==").arg(kPath3D));
    if (loadRecord(kPath3D)) {
        checkMotion(QStringLiteral("record caricato"), QStringLiteral("path3D"));
        click(ui->btnDeparture3D); checkMotion(QStringLiteral("Stop del path"), QStringLiteral("none"));
        click(ui->btnDeparture3D); checkMotion(QStringLiteral("Departure"), QStringLiteral("path3D"));
        click(ui->pushView3D);     checkMotion(QStringLiteral("vista cambiata"), QStringLiteral("path3D"));
        ui->speed3DSlider->setValue(30);  wait(200);
        checkMotion(QStringLiteral("velocita' dallo slider"), QStringLiteral("path3D"));
        const QString x0 = ui->lineX_P3D->text();
        const QStringList applied0 = engine->appliedPath3D();
        typeInField(ui->lineX_P3D, QStringLiteral("cos(qq)"));
        pressEnter(ui->lineX_P3D);
        check(engine->path3DCompiled() && engine->appliedPath3D() == applied0,
              QStringLiteral("Invio su un'equazione che non compila -> a schermo resta il path di prima (motore [%1])")
                  .arg(engine->appliedPath3D().join(QStringLiteral(" | "))));
        typeInField(ui->lineX_P3D, x0);
        pressEnter(ui->lineX_P3D);
        checkMotion(QStringLiteral("equazione rimessa, Invio"), QStringLiteral("path3D"));
        click(ui->btnStart_2);     checkMotion(QStringLiteral("GO"), QStringLiteral("rotation"));
        click(ui->btnStart_2);     checkMotion(QStringLiteral("Stop delle rotazioni"), QStringLiteral("none"));
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Moti: due path e rotazioni (%1) ==").arg(kPathBoth));
    if (loadRecord(kPathBoth)) {
        checkMotion(QStringLiteral("record caricato"), QStringLiteral("path4D"));
        click(ui->btnDeparture3D); checkMotion(QStringLiteral("Departure 3D a path 4D in corso"), QStringLiteral("path3D"));
        click(ui->btnDeparture);   checkMotion(QStringLiteral("Departure 4D a path 3D in corso"), QStringLiteral("path4D"));
        click(ui->btnStart_2);     checkMotion(QStringLiteral("GO a path in corso"), QStringLiteral("rotation"));
        click(ui->btnPrecessionPlus);  checkMotion(QStringLiteral("precessione +"), QStringLiteral("rotation"));
        click(ui->btnOmegaMinus);  checkMotion(QStringLiteral("omega -"), QStringLiteral("rotation"));
    }
    if (loadRecord(kRotRec)) {
        checkMotion(QStringLiteral("poi un record di sole rotazioni"), QStringLiteral("rotation"));
        click(ui->btnStart_2);     checkMotion(QStringLiteral("Stop"), QStringLiteral("none"));
        click(ui->btnStart_2);     checkMotion(QStringLiteral("GO"), QStringLiteral("rotation"));
    }
    if (loadRecord(QString::fromLatin1(kImplicitRecord)))
        checkMotion(QStringLiteral("poi un record Ray Marching"));
    if (loadRecord(kCrossRec))
        checkMotion(QStringLiteral("poi un record Cross Section"));

    // TASTO MASTER: Stop ferma il moto camera, Start fa ripartire quello di prima.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Moti: tasto master =="));
    if (loadRecord(kPath4D)) {
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("path 4D, master Stop"), QStringLiteral("none"));
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("master Start"), QStringLiteral("path4D"));
    }
    if (loadRecord(kPath3D)) {
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("path 3D, master Stop"), QStringLiteral("none"));
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("master Start"), QStringLiteral("path3D"));
    }
    if (loadRecord(kRotRec)) {
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("rotazioni, master Stop"), QStringLiteral("none"));
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("master Start"), QStringLiteral("rotation"));
    }
    // Path scritto da capo su una superficie, con una sola coordinata (basta
    // quella per il Departure): il master Start lo deve avviare come il tasto.
    if (loadSurface(kTorusSurf)) {
        typeInField(ui->lineY_P3D, QStringLiteral("2*sin(t)"));
        click(m_mw->m_btnStart);
        checkMotion(QStringLiteral("superficie, path 3D con la sola Y, master Start"), QStringLiteral("path3D"));
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("master Stop"), QStringLiteral("none"));
    }
    if (loadSurface(kTorusSurf)) {
        typeInField(ui->lineZ_P, QStringLiteral("3 + sin(t)"));
        click(m_mw->m_btnStart);
        checkMotion(QStringLiteral("superficie, path 4D con la sola Z, master Start"), QStringLiteral("path4D"));
        click(m_mw->m_btnStart);   checkMotion(QStringLiteral("master Stop"), QStringLiteral("none"));
    }
    // Il master dice STOP solo con TUTTI i moduli accendibili accesi: accende
    // tutto, spegne tutto; i singoli moduli vanno coi loro tasti. Prima bastava
    // un modulo in moto per avere STOP, e accendere il resto voleva due
    // pressioni (STOP, poi START).
    if (loadSurface(kTorusSurf)) {
        auto label = [this] { return m_mw->m_btnStart->text().toUpper(); };
        ui->lineX->setPlainText(QStringLiteral("(0.8 + 0.3*cos(v))*cos(u + t)"));  wait(300);
        click(ui->btnRunParametric);  wait(800);
        typeInField(ui->lineY_P3D, QStringLiteral("2*sin(t)"));
        check(m_mw->isEquationModuleMoving() && !m_mw->pathRunning(CameraPaths::Path3D) && label() == QLatin1String("START"),
              QStringLiteral("geometria in moto, path 3D scritto e fermo -> master START (%1)").arg(label()));
        click(m_mw->m_btnStart);  wait(600);
        check(m_mw->isEquationModuleMoving() && m_mw->pathRunning(CameraPaths::Path3D) && label() == QLatin1String("STOP"),
              QStringLiteral("master START accende il path e lascia la geometria in moto -> STOP (%1)").arg(label()));
        click(m_mw->m_btnStart);  wait(400);
        check(!m_mw->isAnythingMoving() && label() == QLatin1String("START"),
              QStringLiteral("master STOP spegne tutto -> START (%1)").arg(label()));
        click(ui->btnDeparture3D);  wait(400);
        check(m_mw->pathRunning(CameraPaths::Path3D) && !m_mw->isEquationModuleMoving() && label() == QLatin1String("START"),
              QStringLiteral("solo il path col suo tasto, geometria ferma -> master resta START (%1)").arg(label()));
        click(ui->btnDeparture3D);  wait(300);
    }

    // Dopo un record in movimento, cio' che svuota la scena non ne tiene i moti.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Moti: la scena nuova parte dai default =="));
    if (loadRecord(kPathBoth)) {
        pressNew();
        checkMotionDefaults(QStringLiteral("record con path e rotazioni, poi NEW"));
        checkMotion(QStringLiteral("dopo NEW"), QStringLiteral("none"));
    }
    if (loadRecord(kPath3D) && loadSurface(kTorusSurf)) {
        checkMotionDefaults(QStringLiteral("record con path 3D, poi una superficie"));
        checkMotion(QStringLiteral("dopo la superficie"), QStringLiteral("none"));
    }
    if (loadRecord(kPath4D)) {
        m_discardOnPrompt = true;
        ui->tabModeSelector->setCurrentIndex(1);  wait(1200);
        checkMotionDefaults(QStringLiteral("record con path 4D, poi linguetta Ray Marching"));
        checkMotion(QStringLiteral("dopo il cambio di modalita'"), QStringLiteral("none"));
        ui->tabModeSelector->setCurrentIndex(0);  wait(1200);
        m_discardOnPrompt = false;
        checkMotionDefaults(QStringLiteral("di nuovo linguetta Parametric"));
    }
    if (loadRecord(kCrossRec)) {
        m_discardOnPrompt = true;
        ui->subTabImplicit->setCurrentIndex(0);  wait(1200);
        m_discardOnPrompt = false;
        checkMotionDefaults(QStringLiteral("record Cross Section in rotazione, poi sotto-tab 3D"));
        checkMotion(QStringLiteral("dopo il cambio di sotto-tab"), QStringLiteral("none"));
    }
    if (loadRecord(kRotRec) && loadSurface(kTorusSurf)) {
        checkMotionDefaults(QStringLiteral("record di rotazioni, poi una superficie"));
        checkMotion(QStringLiteral("dopo la superficie"), QStringLiteral("none"));
    }
    pressNew();

    // ---------------------------------------------------------------------
    // LAVORO NON SALVATO: che cosa l'avviso "vuoi salvare?" difende dopo ogni
    // gesto. Regole decise con l'utente: si difende cio' che e' andato a
    // schermo (non il testo mai eseguito), il lavoro sui campi di una texture
    // o di un suono e' del loro MODULO, una texture o un suono presi dalla
    // Library cambiano la scena ma non sono lavoro del modulo, il checkbox
    // della texture mostra/nasconde e non e' lavoro, muovere la vista di una
    // scena vuota nemmeno.
    const QString kRec = QString::fromLatin1(kParametricRecord);
    auto release = [this](QSlider *sl, int v) { sl->setValue(v); emit sl->sliderReleased(); wait(300); };
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Lavoro non salvato: record parametrico (%1) ==").arg(kRec));
    if (loadRecord(kRec)) {
        checkDirty(QStringLiteral("record caricato"), false, false, false);
        ui->lineZ->setPlainText(QStringLiteral("B*v * sin(u/2 + t) + 0.2"));  wait(300);
        checkDirty(QStringLiteral("equazione riscritta, non eseguita"), false, false, false);
        applyEquationEdit(ui->lineZ);
        checkDirty(QStringLiteral("equazione applicata"), true, false, false);
    }
    if (loadRecord(kRec)) {
        checkDirty(QStringLiteral("record ricaricato"), false, false, false);
        release(ui->alphaSlider, 60);
        checkDirty(QStringLiteral("trasparenza dallo slider"), true, false, false);
    }
    if (loadRecord(kRec)) {
        emit ui->glWidget->userMovedView();  wait(200);
        checkDirty(QStringLiteral("vista mossa col mouse"), true, false, false);
    }
    if (loadRecord(kRec)) {
        click(ui->btnSpinPlus);
        checkDirty(QStringLiteral("spin +"), true, false, false);
    }
    if (loadRecord(kRec)) {
        typeInField(ui->lineA, QStringLiteral("1.3"));
        pressEnter(ui->lineA);
        checkDirty(QStringLiteral("costante dal campo"), true, false, false);
    }
    if (loadRecord(kRec)) {
        setConstantBySlider(QStringLiteral("A"), 1.4);
        checkDirty(QStringLiteral("costante dallo slider"), true, false, false);
    }
    if (loadRecord(kRec)) {
        click(ui->radioPhong->isChecked() ? ui->radioBasic : ui->radioPhong);
        checkDirty(QStringLiteral("modo di resa cambiato"), true, false, false);
    }
    if (loadRecord(kRec)) {
        click(ui->chkBoxTexture);
        checkDirty(QStringLiteral("texture nascosta col checkbox"), false, false, false);
        click(ui->chkBoxTexture);
        checkDirty(QStringLiteral("texture rimessa col checkbox"), false, false, false);
    }
    if (loadRecord(kRec)) {
        if (selectTexture(kMandelbrot))
            checkDirty(QStringLiteral("un'altra texture dalla Library"), true, false, false);
    }
    if (loadRecord(kRec)) {
        setScriptMode(MainWindow::ScriptModeTexture);
        checkDirty(QStringLiteral("dock Script sulla texture"), false, false, false);
        ui->txtScriptEditor->setPlainText(QStringLiteral("return mix(u_col2, u_col1, step(0.5, fract(u * 4.0)));"));
        wait(300);
        checkDirty(QStringLiteral("script della texture riscritto"), false, true, false);
        m_mw->onRunCurrentScript();  wait(800);
        checkDirty(QStringLiteral("script della texture eseguito"), false, true, false);
        setScriptMode(MainWindow::ScriptModeSurface);
    }
    if (loadRecord(kRec)) {
        setScriptMode(MainWindow::ScriptModeSound);
        ui->txtScriptEditor->setPlainText(QStringLiteral(
            "vec2 mainSound(int samp, float time) {\n    return vec2(0.1 * sin(6.2831853 * 330.0 * time));\n}"));
        wait(300);
        checkDirty(QStringLiteral("suono riscritto a mano"), false, false, true);
        setScriptMode(MainWindow::ScriptModeSurface);
    }
    if (loadRecord(kRec)) {
        if (selectSound(kSound)) {
            if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
            checkDirty(QStringLiteral("un altro suono dalla Library"), true, false, false);
        }
    }
    if (loadRecord(kRec)) {
        typeInField(ui->lineX_P3D, QStringLiteral("3*cos(t)"));
        checkDirty(QStringLiteral("campo di un path scritto"), true, false, false);
    }
    if (loadRecord(kRec)) {
        release(ui->fovSliderMain, 70);
        checkDirty(QStringLiteral("FOV dallo slider"), true, false, false);
    }
    if (loadRecord(kRec)) {
        typeInField(ui->uMaxEdit, QStringLiteral("3.0"));
        pressEnter(ui->uMaxEdit);
        checkDirty(QStringLiteral("limite u dal campo, Invio"), true, false, false);
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Lavoro non salvato: Ray Marching (%1) ==").arg(QString::fromLatin1(kImplicitRecord)));
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        checkDirty(QStringLiteral("record caricato"), false, false, false);
        ui->lineTexture->setPlainText(QStringLiteral(
            "textureCol = mix(ubuf.u_col1, ubuf.u_col2, 0.5 + 0.5 * sin(pModel.y * 6.0));"));
        wait(300);
        checkDirty(QStringLiteral("campo texture riscritto"), false, true, false);
    }
    if (loadRecord(kRmEq)) {
        ui->lineEquation->setPlainText(QStringLiteral("x^2 + y^2 + z^2 = 1.3"));  wait(300);
        checkDirty(QStringLiteral("equazione riscritta, non eseguita"), false, false, false);
        click(ui->btnImplicit);  wait(800);
        checkDirty(QStringLiteral("equazione eseguita"), true, false, false);
    }
    if (loadRecord(kRmEq)) {
        release(ui->stepSlider, ui->stepSlider->value() > 300 ? 200 : 400);
        checkDirty(QStringLiteral("Ray Steps dallo slider"), true, false, false);
    }

    // PER VALORE: conta lo stato, non il gesto. Riportare un comando dov'era
    // rende la scena di nuovo pulita, e un comando qualunque la sporca anche se
    // nessuno l'aveva cablato.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Lavoro non salvato: conta lo stato, non il gesto =="));
    if (loadRecord(kRec)) {
        const int a0 = ui->alphaSlider->value();
        release(ui->alphaSlider, a0 == 60 ? 50 : 60);
        checkDirty(QStringLiteral("trasparenza cambiata"), true, false, false);
        release(ui->alphaSlider, a0);
        checkDirty(QStringLiteral("trasparenza riportata dov'era"), false, false, false);
    }
    if (loadRecord(kRec)) {
        const QString z0 = ui->lineZ->toPlainText();
        ui->lineZ->setPlainText(QStringLiteral("B*v * sin(u/2 + t) + 0.2"));  wait(300);
        applyEquationEdit(ui->lineZ);
        checkDirty(QStringLiteral("equazione cambiata ed eseguita"), true, false, false);
        ui->lineZ->setPlainText(z0);  wait(300);
        applyEquationEdit(ui->lineZ);
        checkDirty(QStringLiteral("equazione rimessa com'era ed eseguita"), false, false, false);
    }
    if (loadSurface(kTorusSurf)) {
        click(ui->btnSpinPlus);
        checkDirty(QStringLiteral("spin +"), true, false, false);
        click(ui->btnSpinMinus);
        checkDirty(QStringLiteral("spin - (di nuovo a zero)"), false, false, false);
    }
    if (loadRecord(kPath3D)) {
        click(ui->pushView3D);
        checkDirty(QStringLiteral("vista del path cambiata"), true, false, false);
        click(ui->pushView3D);
        checkDirty(QStringLiteral("vista del path rimessa"), false, false, false);
        // Fermare un moto non e' qualcosa che il record salva.
        click(ui->btnDeparture3D);
        checkDirty(QStringLiteral("path fermato"), false, false, false);
    }
    if (loadRecord(kRotRec)) {
        click(ui->btnStart_2);
        checkDirty(QStringLiteral("rotazioni fermate"), false, false, false);
    }
    if (loadSurface(kTorusSurf)) {
        // Su una superficie: la texture presa dalla Library cambia la scena (il
        // record la salverebbe), il checkbox che poi la nasconde non aggiunge
        // ne' toglie nulla.
        if (selectTexture(kMandelbrot))
            checkDirty(QStringLiteral("texture dalla Library su una superficie"), true, false, false);
        click(ui->chkBoxTexture);
        checkDirty(QStringLiteral("texture nascosta col checkbox"), true, false, false);
    }
    if (loadRecord(kRec)) {
        setScriptMode(MainWindow::ScriptModeTexture);
        ui->txtScriptEditor->setPlainText(QStringLiteral("return mix(u_col2, u_col1, step(0.5, fract(v * 4.0)));"));
        wait(300);
        checkDirty(QStringLiteral("script della texture riscritto"), false, true, false);
        // Salvata come texture: il modulo e' su disco, la scena non c'entra.
        m_mw->markTextureSaved();
        checkDirty(QStringLiteral("dopo Save Texture"), false, false, false);
        setScriptMode(MainWindow::ScriptModeSurface);
        checkDirty(QStringLiteral("dock Script di nuovo sulla superficie"), false, false, false);
    }
    if (loadRecord(kRec)) {
        release(ui->lightSlider, ui->lightSlider->value() == 70 ? 60 : 70);
        setScriptMode(MainWindow::ScriptModeTexture);
        ui->txtScriptEditor->setPlainText(QStringLiteral("return mix(u_col2, u_col1, step(0.5, fract(v * 6.0)));"));
        wait(300);
        checkDirty(QStringLiteral("luce cambiata e script della texture riscritto"), true, true, false);
        // Save Surface scrive la scena, non la texture: il suo lavoro resta da salvare.
        m_mw->markSurfaceSaved();
        checkDirty(QStringLiteral("dopo Save Surface"), false, true, false);
        // Save Record scrive tutto.
        m_mw->markSceneClean();
        checkDirty(QStringLiteral("dopo Save Record"), false, false, false);
        setScriptMode(MainWindow::ScriptModeSurface);
    }
    if (loadRecord(kRec)) {
        // I moduli del dock Script: passare da uno all'altro non e' lavoro.
        setScriptMode(MainWindow::ScriptModeTexture);
        setScriptMode(MainWindow::ScriptModeSound);
        checkDirty(QStringLiteral("dock Script sul suono"), false, false, false);
        setScriptMode(MainWindow::ScriptModeSurface);
        checkDirty(QStringLiteral("dock Script sulla superficie"), false, false, false);
        wait(3000);
        checkDirty(QStringLiteral("record lasciato girare"), false, false, false);
    }

    // Gesti che scelgono su che cosa lavorare, o che fermano e riavviano: non
    // cambiano niente di cio' che il record salva.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Lavoro non salvato: gesti che non sono lavoro (%1) ==")
                       .arg(QString::fromLatin1(kMultiMeshRecord)));
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        checkDirty(QStringLiteral("record multi-mesh caricato"), false, false, false);
        click(ui->radioBackground);
        checkDirty(QStringLiteral("bersaglio Background"), false, false, false);
        click(ui->radioSurface);
        checkDirty(QStringLiteral("bersaglio Surface"), false, false, false);
        click(ui->radioMeshAll->isChecked() ? ui->radioMeshOne : ui->radioMeshAll);
        checkDirty(QStringLiteral("ambito All/Mesh cambiato"), false, false, false);
        click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(ui->spinMeshSel->value() == 3 ? 2 : 3);  wait(300);
        checkDirty(QStringLiteral("un'altra fascia selezionata"), false, false, false);
        click(m_mw->m_btnStart);
        checkDirty(QStringLiteral("master Stop"), false, false, false);
        click(m_mw->m_btnStart);
        checkDirty(QStringLiteral("master Start"), false, false, false);
    }
    // Reset della vista: riporta il FOV a quello d'apertura del record, e lo
    // slider lo segue (il motore cambiava con lo slider fermo). Cambiato il FOV
    // e poi resettata la vista, la scena e' di nuovo quella del file.
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        const int fov0 = ui->fovSliderMain->value();
        m_mw->applyCameraFov(fov0 == 80 ? 60.0f : 80.0f);
        m_mw->resetView();  wait(300);
        check(ui->fovSliderMain->value() == fov0 && qAbs(ui->glWidget->cameraFov() - fov0) < 0.51f,
              QStringLiteral("Reset della vista -> FOV del record (%1) sullo slider e nel motore (slider %2, motore %3)")
                  .arg(fov0).arg(ui->fovSliderMain->value()).arg(ui->glWidget->cameraFov()));
        checkDirty(QStringLiteral("Reset della vista (FOV del record %1)").arg(fov0), false, false, false);
        checkMotion(QStringLiteral("dopo il Reset della vista"));
    }
    if (loadRecord(kPath3D)) {
        const int fov0 = ui->fovSliderMain->value();
        m_mw->resetView();  wait(300);
        check(ui->fovSliderMain->value() == fov0,
              QStringLiteral("Reset della vista a path in corsa -> il FOV del path resta (slider %1)")
                  .arg(ui->fovSliderMain->value()));
        checkDirty(QStringLiteral("Reset della vista a path in corsa"), false, false, false);
    }
    if (loadRecord(kPathBoth)) {
        click(m_mw->m_btnStart);
        click(m_mw->m_btnStart);
        checkDirty(QStringLiteral("record con path: master Stop e Start"), false, false, false);
    }
    if (loadRecord(kCrossRec)) {
        click(m_mw->m_btnStart);
        click(m_mw->m_btnStart);
        checkDirty(QStringLiteral("record Cross Section: master Stop e Start"), false, false, false);
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Lavoro non salvato: superfici e scena vuota =="));
    if (loadSurface(kTorusSurf)) {
        checkDirty(QStringLiteral("superficie caricata"), false, false, false);
        click(ui->chkBoxTexture);
        checkDirty(QStringLiteral("texture accesa col checkbox"), false, false, false);
        release(ui->lightSlider, 70);
        checkDirty(QStringLiteral("luce dallo slider"), true, false, false);
    }
    if (loadSurface(kTorusSurf)) {
        // Un'equazione che da' errore non va a schermo: niente da difendere.
        ui->lineX->setPlainText(QStringLiteral("(A + B*cos(v))*cos(u"));  wait(300);
        click(ui->btnRunParametric);  wait(800);
        checkDirty(QStringLiteral("equazione con errore, Run"), false, false, false);
    }
    if (loadRecord(kRec)) {
        release(ui->alphaSlider, 55);
        checkDirty(QStringLiteral("record modificato"), true, false, false);
        pressNew();
        checkDirty(QStringLiteral("tasto NEW"), false, false, false);
        emit ui->glWidget->userMovedView();  wait(200);
        checkDirty(QStringLiteral("vista mossa a scena vuota"), false, false, false);
        ui->lineX->setPlainText(QStringLiteral("cos(u)*cos(v)"));
        ui->lineY->setPlainText(QStringLiteral("sin(u)*cos(v)"));
        ui->lineZ->setPlainText(QStringLiteral("sin(v)"));
        wait(300);
        checkDirty(QStringLiteral("equazioni scritte da capo, non eseguite"), false, false, false);
    }
    pressNew();

    // HINT: ogni legenda dice di CHI sono i suoi slider, anche da sola
    // (composedHintText): "Surface", "Texture", "Background".
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Hint: il ruolo degli slider =="));
    if (loadRecord(QStringLiteral("records/Ray Marching/Morphing Sphere-Cube.json"))) {
        const QString h = m_mw->composedHintText();
        check(h.startsWith(QLatin1String("Surface slider A")) && h.contains(QLatin1String("\nTexture sliders\n")),
              QStringLiteral("hint di superficie e di texture -> %1").arg(QString(h).replace(QLatin1Char('\n'), QLatin1String(" / "))));
    }
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        const QString h = m_mw->composedHintText();
        check(h.isEmpty() || h.startsWith(QLatin1String("Surface slider")),
              QStringLiteral("hint della sola superficie -> %1").arg(QString(h).replace(QLatin1Char('\n'), QLatin1String(" / "))));
    }

    runScriptDockScenarios();
    runTextureTargetScenarios();
    runMeshImageScenarios();
    runMeshClockScenarios();
    runRecordTextureScenarios();
    runLibraryFolderScenarios();
    runLibraryPasteScenarios();
    runLibraryRenameScenarios();
    runResetSceneScenarios();
    runResetButtonScenarios();

    finish();
}

void ScenarioTest::runResetButtonScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Tasto RESET: vista e moti alla posa d'apertura =="));
    const float o = GLWidget::kClockOrigin;
    const QString kTorus = QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json");
    const QString kAnim  = QString::fromLatin1(kParametricRecord);                     // t, niente rotazioni
    const QString kRot   = QStringLiteral("records/Rotations/Boy Surface.json");       // rotazioni
    const QString kPath  = QStringLiteral("records/Paths/Calabi-Yau Orbit.json");      // path 3D
    const QString kMesh  = QStringLiteral("records/Solid Wireframe/Multi Mesh/Hopf Tori.json");
    const QString kGeo   = QStringLiteral("records/Geodesic Flow/H^3/Breathing Hyperboloid.json");

    auto sameCamera = [this, gl]() {
        return (gl->getCameraPos() - m_mw->m_startFraming.cameraPos).length() < 1e-4f;
    };
    auto sameOrientation = [this, gl]() {
        return qFuzzyCompare(gl->getRotationQuat(), m_mw->m_startOrientation.rotation);
    };

    // Scena ferma: nessuna domanda, tornano camera (zoom compreso) e
    // orientamento girato col mouse.
    if (loadSurface(kTorus)) {
        const int popups = m_popupsClosed;
        gl->addObjectRotation(40.0f, 20.0f, 0.0f);
        gl->zoomCamera(1.5f);
        m_mw->onResetClicked();  wait(300);
        check(m_popupsClosed == popups && sameCamera() && sameOrientation(),
              QStringLiteral("superficie ferma -> camera e orientamento d'apertura, senza popup (popup %1)")
                  .arg(m_popupsClosed - popups));
    }

    if (loadRecord(kAnim)) {
        // Annulla: niente cambia, l'animazione prosegue.
        const int popups = m_popupsClosed;
        const float g0 = gl->clockTimes().geom;
        m_mw->onResetClicked();  wait(300);
        // Dal testo: su macOS QMessageBox ignora il titolo della finestra.
        check(m_popupsClosed == popups + 1 && m_lastPopup.contains(QLatin1String("What do you want to reset?"))
                  && gl->clockTimes().geom > g0,
              QStringLiteral("record animato, Annulla -> popup del Reset, t prosegue (%1 -> %2)")
                  .arg(g0).arg(gl->clockTimes().geom));

        // Solo la vista: zoom e FOV tornano, gli orologi non si toccano.
        const int fov0 = ui->fovSliderMain->value();
        gl->zoomCamera(1.5f);
        m_mw->applyCameraFov(fov0 == 80 ? 60.0f : 80.0f);
        const float g1 = gl->clockTimes().geom;
        m_popupAnswer = QStringLiteral("View");
        m_mw->onResetClicked();  wait(300);
        check(gl->clockTimes().geom > g1 && sameCamera() && ui->fovSliderMain->value() == fov0,
              QStringLiteral("View -> zoom e FOV d'apertura (FOV %1), t prosegue (%2 -> %3)")
                  .arg(ui->fovSliderMain->value()).arg(g1).arg(gl->clockTimes().geom));
        checkDirty(QStringLiteral("View dopo zoom e FOV"), false, false, false);

        // Moti in corso: t riparte dall'origine e prosegue.
        m_popupAnswer = QStringLiteral("Motions");
        m_mw->onResetClicked();
        const float g2 = gl->clockTimes().geom;
        wait(500);
        const float g3 = gl->clockTimes().geom;
        check(g2 < 0.2f && g3 - g2 > 0.3f && gl->isSurfaceAnimating(),
              QStringLiteral("Motions, in moto -> t riparte dall'origine e prosegue (%1, poi %2)")
                  .arg(g2).arg(g3));
        checkDirty(QStringLiteral("Motions"), false, false, false);

        // Moti fermi dal master: t all'origine, e resta li'. Il master e' STOP
        // solo con tutti i moduli accesi: se non lo e', prima li accende.
        if (m_mw->m_btnStart->text() != QLatin1String("STOP")) { m_mw->m_btnStart->click();  wait(300); }
        m_mw->m_btnStart->click();  wait(300);
        m_popupAnswer = QStringLiteral("Both");
        m_mw->onResetClicked();  wait(500);
        const GLWidget::ClockTimes c = gl->clockTimes();
        check(c.geom == o && c.tex == o && c.bg == o && !gl->isSurfaceAnimating()
                  && m_mw->m_btnStart->text() == QLatin1String("START"),
              QStringLiteral("Both, master fermo -> orologi all'origine e fermi (%1, %2, %3; master %4)")
                  .arg(c.geom).arg(c.tex).arg(c.bg).arg(m_mw->m_btnStart->text()));
        m_mw->m_btnStart->click();  wait(300);
    }

    // Rotazioni: Motions le rimette alla posa d'apertura e il GO prosegue da li'.
    if (loadRecord(kRot)) {
        const bool turned = !sameOrientation();
        m_popupAnswer = QStringLiteral("Motions");
        m_mw->onResetClicked();
        const bool back = sameOrientation();
        wait(500);
        check(turned && back && gl->isAnimating() && !sameOrientation(),
              QStringLiteral("rotazioni -> Motions le rimette alla posa d'apertura, il GO prosegue "
                             "(girato %1, rimesso %2, gira %3)")
                  .arg(turned).arg(back).arg(gl->isAnimating()));
    }

    // Path: la camera del path torna SUBITO al punto di partenza (in corsa
    // come da fermo), View a path in corsa non gliela toglie.
    if (loadRecord(kPath)) {
        const QVector3D start = gl->getEngine()->evaluatePath3DPosition(0.0f).toVector3D();
        auto atStart = [gl, start]() { return (gl->getCameraPos() - start).length() < 1e-3f; };

        // In corsa, View: la camera resta al path, nessun salto.
        const bool running = m_mw->pathRunning(CameraPaths::Path3D);
        const QVector3D before = gl->getCameraPos();
        m_mw->resetView();
        const bool kept = (gl->getCameraPos() - before).length() < 1e-3f && gl->isPathFollowing();
        check(running && kept, QStringLiteral("path in corsa, View -> la camera resta al path (in corsa %1, ferma %2)")
                                   .arg(running).arg(kept));

        // In corsa, Motions: riparte da capo e prosegue.
        wait(300);
        m_popupAnswer = QStringLiteral("Motions");
        m_mw->onResetClicked();
        const bool runStart = atStart() && m_mw->m_paths->time(CameraPaths::Path3D) == 0.0f;
        wait(300);
        check(runStart && m_mw->pathRunning(CameraPaths::Path3D) && !atStart(),
              QStringLiteral("path in corsa, Motions -> camera al punto di partenza, poi prosegue (%1)")
                  .arg(runStart));

        // Fermo, Motions: camera al punto di partenza, t = 0, resta fermo; al
        // Departure riparte dall'inizio.
        wait(300);
        ui->btnDeparture3D->click();  wait(200);
        const float t0 = m_mw->m_paths->time(CameraPaths::Path3D);
        const bool movedAway = !atStart();
        m_popupAnswer = QStringLiteral("Motions");
        m_mw->onResetClicked();  wait(200);
        const float t1 = m_mw->m_paths->time(CameraPaths::Path3D);
        const bool still = !m_mw->pathRunning(CameraPaths::Path3D);
        const bool back = atStart();
        ui->btnDeparture3D->click();  wait(300);
        const float t2 = m_mw->m_paths->time(CameraPaths::Path3D);
        check(t0 > 0.0f && movedAway && t1 == 0.0f && still && back && t2 < t0,
              QStringLiteral("path fermo, Motions -> t %1 -> %2, camera al punto di partenza (%3), resta fermo; "
                             "al Departure riparte dall'inizio (%4)")
                  .arg(t0).arg(t1).arg(back).arg(t2));

        // Fermo, Both: anche qui la camera e' del path, al punto di partenza.
        wait(300);
        ui->btnDeparture3D->click();  wait(200);
        m_popupAnswer = QStringLiteral("Both");
        m_mw->onResetClicked();  wait(200);
        check(atStart() && gl->isPathFollowing() && !m_mw->pathRunning(CameraPaths::Path3D),
              QStringLiteral("path fermo, Both -> camera al punto di partenza del path, fermo"));
    }

    // Fasce: anche gli orologi per-mesh tornano all'origine.
    if (loadRecord(kMesh)) {
        m_mw->resetMotions();
        QStringList bad;
        const auto &parts = gl->getEngine()->getMeshParts();
        for (int i = 0; i < parts.size(); ++i)
            if (parts.at(i).timeTex != 0.0f)
                bad << QStringLiteral("fascia %1 a %2").arg(i + 1).arg(parts.at(i).timeTex);
        check(bad.isEmpty(), QStringLiteral("fasce -> orologi per-mesh all'origine%1")
                                 .arg(bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
    }

    // Flusso geodetico: geoTime a 0, e se scorre continua a scorrere.
    if (loadRecord(kGeo)) {
        const double before = m_mw->m_geoTime;
        const bool flowing = m_mw->isGeodesicMotionActive();
        m_mw->resetMotions();
        const double after = m_mw->m_geoTime;
        wait(500);
        check(before > 0.0 && after == 0.0 && (!flowing || m_mw->m_geoTime > 0.0),
              QStringLiteral("flusso geodetico -> geoTime %1 -> %2, poi %3 (%4)")
                  .arg(before).arg(after).arg(m_mw->m_geoTime)
                  .arg(flowing ? QStringLiteral("in moto") : QStringLiteral("fermo")));
    }
}

void ScenarioTest::runResetSceneScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Reset della scena = scena di default (MainWindow::defaultScene) =="));
    // La previsione si calcola PRIMA del reset: dipende dalla scena di
    // partenza (al cambio di linguetta alcune parti sopravvivono) e dalle
    // memorie per modalita'. Le derivazioni (costante non usata a 1, limite di
    // un asse non usato vuoto) sono quelle del round-trip.
    auto checkReset = [this](const QString &what, int index, bool loadDefault, bool sameTab,
                             const std::function<void()> &trigger) {
        const MainWindow::SceneState want = m_mw->defaultScene(index, loadDefault, sameTab);
        trigger();
        const QStringList diff = PresetRoundTrip::withoutDerivations(
            PresetRoundTrip::diffScene(want, m_mw->m_scene), m_mw);
        check(diff.isEmpty(), what + (diff.isEmpty() ? QStringLiteral(" -> scena di default")
                                                     : QStringLiteral(" -> diversa: ")
                                                           + diff.join(QStringLiteral("; "))));
    };
    auto modeTab = [this, ui](int index) {
        m_discardOnPrompt = true;
        ui->tabModeSelector->setCurrentIndex(index);  wait(1500);
        m_discardOnPrompt = false;
    };
    auto reclick = [this, ui]() {
        m_discardOnPrompt = true;
        emit ui->tabModeSelector->tabBar()->tabBarClicked(ui->tabModeSelector->currentIndex());
        wait(1500);
        m_discardOnPrompt = false;
    };
    const QString kParam = QString::fromLatin1(kParametricRecord);
    const QString kRM = QString::fromLatin1(kImplicitRecord);
    const QString kCS = QStringLiteral("records/Ray Marching/Cross Sections/KxS1/Klein Bundle in R5.json");

    if (loadRecord(kParam))
        checkReset(QStringLiteral("record parametrico, linguetta Ray Marching"), 1, true, false,
                   [&] { modeTab(1); });
    checkReset(QStringLiteral("poi linguetta Parametric"), 0, true, false, [&] { modeTab(0); });
    if (loadRecord(kParam))
        checkReset(QStringLiteral("record parametrico, riclic su Parametric"), 0, true, true, reclick);
    if (loadRecord(kRM))
        checkReset(QStringLiteral("record Ray Marching, riclic su Ray Marching"), 1, true, true, reclick);
    if (loadRecord(kCS))
        checkReset(QStringLiteral("record Cross Section, riclic (superficie di default del sotto-tab)"),
                   1, true, true, reclick);
    if (loadRecord(kCS))
        checkReset(QStringLiteral("record Cross Section, linguetta Parametric"), 0, true, false,
                   [&] { modeTab(0); });
    checkReset(QStringLiteral("poi linguetta Ray Marching (sotto-tab Cross Section)"), 1, true, false,
               [&] { modeTab(1); });
    if (loadRecord(kParam))
        checkReset(QStringLiteral("record parametrico, NEW"), 0, false, false, [&] { pressNew(); });
    if (loadRecord(kRM))
        checkReset(QStringLiteral("record Ray Marching, NEW"), 1, false, false, [&] { pressNew(); });

    // MEMORIE PER MODALITA': un riclic o un NEW restando in un modo non tocca
    // la memoria dell'altro. Prima il reset la riscriveva coi valori del modo
    // corrente: un riclic in Ray Marching dava al parametrico 400 di Steps, uno
    // in parametrico al Ray Marching 100 Ray Steps. Sotto-tab 3D: il Cross
    // Section impone i suoi 350 e nasconderebbe il difetto.
    modeTab(1);
    if (m_mw->crossSectionTab()) {
        m_discardOnPrompt = true;
        ui->subTabImplicit->setCurrentIndex(0);  wait(1500);
        m_discardOnPrompt = false;
    }
    const int rmSteps = m_mw->m_scene.steps;
    modeTab(0);
    const int paramSteps = m_mw->m_scene.steps;
    reclick();
    pressNew();
    modeTab(1);
    check(m_mw->m_scene.steps == rmSteps,
          QStringLiteral("riclic e NEW in Parametric, poi Ray Marching -> Ray Steps %1 (attesi %2)")
              .arg(m_mw->m_scene.steps).arg(rmSteps));
    reclick();
    pressNew();
    modeTab(0);
    check(m_mw->m_scene.steps == paramSteps,
          QStringLiteral("riclic e NEW in Ray Marching, poi Parametric -> Steps %1 (attesi %2)")
              .arg(m_mw->m_scene.steps).arg(paramSteps));
}

void ScenarioTest::runMasterRecordsScenarios()
{
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Master su tutti i record: dopo il load tutto acceso, tasto STOP =="));
    const QList<LibraryItem> records = m_mw->m_libraryManager.m_motions;
    const QString filter = m_only.section(QLatin1Char(':'), 1);
    int ok = 0;
    for (const LibraryItem &item : records) {
        if (item.name.isEmpty()) continue;
        if (!filter.isEmpty() && !item.filePath.contains(QRegularExpression(filter, QRegularExpression::CaseInsensitiveOption))) continue;
        for (int attempt = 0; attempt < 3; ++attempt) {
            m_watchdogFired = false;
            m_mw->applyMotionExample(item);
            wait(1500);
            if (!m_watchdogFired) break;
        }
        // Il testo del tasto COM'E' a schermo, senza ricalcolarlo: un modulo che
        // parte in differita dopo l'ultimo ricalcolo lo lascerebbe indietro.
        const QString shown = m_mw->m_btnStart ? m_mw->m_btnStart->text().toUpper() : QString();
        const MainWindow::MasterActivity a = m_mw->masterActivity();
        QStringList off;
        if (a.eqAvailable && !a.eqRunning)         off << QStringLiteral("equazioni");
        if (a.texAvailable && !a.texRunning)       off << QStringLiteral("texture");
        if (a.bgAvailable && !a.bgRunning)         off << QStringLiteral("sfondo");
        if (a.cameraAvailable && !a.cameraRunning) off << QStringLiteral("camera");
        if (a.audioAvailable && !a.audioRunning)   off << QStringLiteral("suono");
        const QString expected = (a.anyRunning() && a.allRunning()) ? QStringLiteral("STOP") : QStringLiteral("START");
        const QString rel = QDir(m_root).relativeFilePath(item.filePath);
        if (!off.isEmpty() || shown != expected) {
            check(false, QStringLiteral("%1 -> tasto %2, atteso %3%4").arg(rel, shown, expected,
                      off.isEmpty() ? QString() : QStringLiteral(" (spenti: ") + off.join(QStringLiteral(", ")) + QLatin1Char(')')));
        } else {
            ++ok;
        }
        if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
    }
    check(ok > 0, QStringLiteral("record con tutti i moduli accesi dopo il load: %1 su %2").arg(ok).arg(records.size()));
}

void ScenarioTest::runLibraryRenameScenarios()
{
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Library: Rename (in una cartella temporanea) =="));

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        check(false, QStringLiteral("cartella temporanea non creata"));
        return;
    }
    const QString root = tmp.path();
    auto touch = [](const QString &path) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write("{}");
    };
    QDir().mkpath(root + QStringLiteral("/Paths"));
    touch(root + QStringLiteral("/Paths/a.json"));
    touch(root + QStringLiteral("/Paths/x.json"));

    QString error;
    QString got = m_mw->renameLibraryPath(root + QStringLiteral("/Paths/a.json"), QStringLiteral("b"), &error);
    check(got == root + QStringLiteral("/Paths/b.json") && QFile::exists(got)
              && !QFile::exists(root + QStringLiteral("/Paths/a.json")),
          QStringLiteral("file rinominato (l'estensione resta)"));

    got = m_mw->renameLibraryPath(root + QStringLiteral("/Paths/b.json"), QStringLiteral("c.d"), &error);
    check(got.isEmpty() && !error.isEmpty() && QFile::exists(root + QStringLiteral("/Paths/b.json")),
          QStringLiteral("nome col punto -> rifiutato col motivo, file intatto"));

    got = m_mw->renameLibraryPath(root + QStringLiteral("/Paths/b.json"), QStringLiteral("x"), &error);
    check(got.isEmpty() && !error.isEmpty() && QFile::exists(root + QStringLiteral("/Paths/b.json")),
          QStringLiteral("nome gia' usato nella cartella -> rifiutato"));

    got = m_mw->renameLibraryPath(root + QStringLiteral("/Paths/b.json"), QStringLiteral("B"), &error);
    check(got == root + QStringLiteral("/Paths/B.json") && QDir(root + QStringLiteral("/Paths")).entryList().contains(QStringLiteral("B.json")),
          QStringLiteral("cambiano solo le maiuscole -> permesso"));

    // Cartella: il record in scena e gli appunti di Copia che stanno dentro la
    // seguono.
    const QString keepRecord = m_mw->m_currentRecordPath;
    const QStringList keepCut = m_mw->m_cutFilePaths;
    m_mw->m_currentRecordPath = root + QStringLiteral("/Paths/B.json");
    m_mw->m_cutFilePaths = { root + QStringLiteral("/Paths") };
    got = m_mw->renameLibraryPath(root + QStringLiteral("/Paths"), QStringLiteral("Routes"), &error);
    check(got == root + QStringLiteral("/Routes") && QFile::exists(root + QStringLiteral("/Routes/B.json")),
          QStringLiteral("cartella rinominata col suo contenuto"));
    check(m_mw->m_currentRecordPath == root + QStringLiteral("/Routes/B.json")
              && m_mw->m_cutFilePaths == QStringList{ root + QStringLiteral("/Routes") },
          QStringLiteral("cartella rinominata -> record in scena e appunti di Copia la seguono (%1, %2)")
              .arg(m_mw->m_currentRecordPath, m_mw->m_cutFilePaths.join(QStringLiteral(", "))));
    m_mw->m_currentRecordPath = keepRecord;
    m_mw->m_cutFilePaths = keepCut;
}

void ScenarioTest::runLibraryPasteScenarios()
{
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Library: copia e incolla di una cartella (in una cartella temporanea) =="));

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        check(false, QStringLiteral("cartella temporanea non creata"));
        return;
    }
    const QString root = tmp.path();
    auto touch = [](const QString &path) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write("{}");
    };
    QDir().mkpath(root + QStringLiteral("/Paths/sub"));
    QDir().mkpath(root + QStringLiteral("/Paths2"));
    touch(root + QStringLiteral("/Paths/a.json"));
    touch(root + QStringLiteral("/Paths/sub/b.json"));

    // Come il menu: Copy riempie la lista, Paste la incolla nella destinazione.
    auto copyPaste = [&](const QString &src, const QString &dest) {
        m_mw->m_cutFilePaths = { src };
        m_mw->m_isCopyOperation = true;
        m_mw->m_fileOps->performPasteExample(dest);
        wait(300);
    };

    copyPaste(root + QStringLiteral("/Paths"), root);
    check(QFile::exists(root + QStringLiteral("/Paths_copy1/a.json"))
              && QFile::exists(root + QStringLiteral("/Paths_copy1/sub/b.json")),
          QStringLiteral("cartella incollata accanto a se stessa -> Paths_copy1 col suo contenuto"));

    copyPaste(root + QStringLiteral("/Paths"), root + QStringLiteral("/Paths2"));
    check(QFile::exists(root + QStringLiteral("/Paths2/Paths/sub/b.json")),
          QStringLiteral("incollata in Paths2 (nome con lo stesso prefisso) -> copiata"));

    copyPaste(root + QStringLiteral("/Paths"), root + QStringLiteral("/Paths/sub"));
    check(!QDir(root + QStringLiteral("/Paths/sub/Paths")).exists(),
          QStringLiteral("incollata in una sua sottocartella -> rifiutata"));

    m_mw->m_cutFilePaths.clear();
}

void ScenarioTest::runLibraryFolderScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Library: aprire e chiudere una cartella non sposta il focus =="));

    // Clic VERI sul viewport: la selezione la fa Qt al press, come per l'utente,
    // quindi l'albero dev'essere a schermo.
    ui->dockSurfaces->show();
    ui->dockSurfaces->raise();
    ui->tabWidget->setCurrentWidget(ui->Surface);
    wait(300);

    QTreeWidget *tree = ui->treeSurfaces;
    QTreeWidgetItem *leaf = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it && !leaf; ++it)
        if ((*it)->childCount() == 0 && (*it)->parent() && (*it)->data(0, Qt::UserRole).isValid())
            leaf = *it;
    if (!leaf) {
        check(false, QStringLiteral("nessun preset dentro una cartella delle Surfaces"));
        return;
    }
    QTreeWidgetItem *folder = leaf->parent();
    for (QTreeWidgetItem *p = folder; p; p = p->parent()) p->setExpanded(true);

    // Il preset caricato col click, come fa l'utente.
    m_discardOnPrompt = true;
    tree->setCurrentItem(leaf);
    emit tree->itemClicked(leaf, 0);
    wait(1200);
    m_discardOnPrompt = false;
    tree->scrollToItem(folder);
    wait(200);

    // Una texture evidenziata in un ALTRO albero: il clic su una cartella delle
    // Surfaces non deve togliergliela.
    QTreeWidgetItem *tex = nullptr;
    for (QTreeWidgetItemIterator it(ui->treeTextures); *it && !tex; ++it)
        if ((*it)->childCount() == 0 && (*it)->data(0, Qt::UserRole + 1).isValid()) tex = *it;
    if (tex) ui->treeTextures->setCurrentItem(tex);

    const QString name = leaf->text(0);
    auto focusOk = [&](const QString &step) {
        QStringList bad;
        if (!leaf->isSelected()) bad << QStringLiteral("il preset non e' evidenziato");
        if (folder->isSelected()) bad << QStringLiteral("la cartella e' selezionata");
        if (tree->currentItem() != leaf) bad << QStringLiteral("l'elemento corrente non e' il preset");
        if (tex && !tex->isSelected()) bad << QStringLiteral("la texture ha perso l'evidenziazione");
        check(bad.isEmpty(), QStringLiteral("%1 -> focus su '%2'%3").arg(step, name,
                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
    };
    auto mouse = [&](QEvent::Type type, QTreeWidgetItem *it) {
        const QPoint pos = tree->visualItemRect(it).center();
        QMouseEvent e(type, pos, tree->viewport()->mapToGlobal(pos), Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      Qt::NoModifier);
        QCoreApplication::sendEvent(tree->viewport(), &e);
    };
    auto doubleClick = [&](QTreeWidgetItem *it) {
        mouse(QEvent::MouseButtonPress, it);
        mouse(QEvent::MouseButtonRelease, it);
        mouse(QEvent::MouseButtonDblClick, it);
        mouse(QEvent::MouseButtonRelease, it);
        wait(300);
    };

    focusOk(QStringLiteral("preset caricato"));
    doubleClick(folder);
    check(!folder->isExpanded(), QStringLiteral("doppio clic sulla cartella -> si chiude"));
    focusOk(QStringLiteral("cartella chiusa col doppio clic"));
    doubleClick(folder);
    check(folder->isExpanded(), QStringLiteral("doppio clic sulla cartella -> si riapre"));
    focusOk(QStringLiteral("cartella riaperta col doppio clic"));

    // Clic singolo: la cartella resta selezionata (serve a Copy/Cut/Paste/Delete),
    // ma gli altri alberi non perdono la loro evidenziazione.
    mouse(QEvent::MouseButtonPress, folder);
    mouse(QEvent::MouseButtonRelease, folder);
    wait(300);
    check(folder->isSelected() && !leaf->isSelected(),
          QStringLiteral("clic singolo sulla cartella -> la cartella e' selezionata"));
    check(!tex || tex->isSelected(),
          QStringLiteral("clic singolo su una cartella delle Surfaces -> la texture resta evidenziata"));

    // Tap su mobile: il press seleziona la cartella, poi onExampleItemClicked la
    // apre o la chiude (setExpanded). Qui il secondo passo e' simulato.
    folder->setExpanded(false);
    wait(200);
    focusOk(QStringLiteral("tap mobile simulato, cartella chiusa"));
    mouse(QEvent::MouseButtonPress, folder);
    mouse(QEvent::MouseButtonRelease, folder);
    folder->setExpanded(true);
    wait(200);
    focusOk(QStringLiteral("tap mobile simulato, cartella riaperta"));

    // Un'espansione del PROGRAMMA con un altro preset evidenziato non lo tocca.
    QTreeWidgetItem *other = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it && !other; ++it)
        if ((*it)->childCount() == 0 && *it != leaf && (*it)->data(0, Qt::UserRole).isValid()) other = *it;
    if (other) {
        tree->setCurrentItem(other);
        folder->setExpanded(false);
        folder->setExpanded(true);
        wait(200);
        check(other->isSelected() && !leaf->isSelected(),
              QStringLiteral("espansione del programma con un altro preset evidenziato -> resta quello"));
    }
}

void ScenarioTest::runRecordTextureScenarios()
{
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Load di record: la texture di superficie compilata e' quella del record =="));
    for (const char *rel : { "records/Rotations/Kerr Spin Animated.json", "records/Static/Oloid.json" }) {
        const QString r = QString::fromLatin1(rel);
        if (!loadRecord(r)) continue;
        wait(600);
        checkTextureCode(QStringLiteral("load di %1").arg(QFileInfo(r).completeBaseName()), QString(), false);
    }
}

QString ScenarioTest::textureCheckboxProblem() const
{
    Ui::MainWindow *ui = m_mw->ui;
    const bool onBg = m_mw->editingBackground();
    const QString label = ui->chkBoxTexture->text();
    const QString wantLabel = onBg ? QStringLiteral("Background Texture") : QStringLiteral("Texture");
    if (label != wantLabel)
        return QStringLiteral("etichetta del checkbox \"%1\" col bersaglio %2")
            .arg(label, onBg ? QStringLiteral("Background") : QStringLiteral("Surface"));
    const QString target = editTargetProblem();
    if (!target.isEmpty()) return target;
    const QString render = renderModeProblem();
    if (!render.isEmpty()) return render;
    const QString implicit = implicitRenderProblem();
    if (!implicit.isEmpty()) return implicit;
    const QString scope = meshScopeProblem();
    if (!scope.isEmpty()) return scope;
    const QString mode = modeProblem();
    if (!mode.isEmpty()) return mode;
    // Bersaglio Background: il checkbox mostra lo sfondo. (Per la superficie
    // e le fasce lo verifica checkTextureEnabled.)
    if (onBg && ui->chkBoxTexture->isChecked() != ui->glWidget->isBackgroundTextureEnabled())
        return QStringLiteral("checkbox %1, sfondo %2")
            .arg(onOff(ui->chkBoxTexture->isChecked()), onOff(ui->glWidget->isBackgroundTextureEnabled()));
    return QString();
}

QString ScenarioTest::modeProblem() const
{
    Ui::MainWindow *ui = m_mw->ui;
    const bool tab = ui->tabModeSelector->currentIndex() == 1;
    const bool engine = ui->glWidget->getEngineMode() == GLWidget::ModeImplicit;
    if (m_mw->implicitMode() != tab)
        return QStringLiteral("modalita': stato %1, linguetta %2")
            .arg(m_mw->implicitMode() ? QStringLiteral("Implicit") : QStringLiteral("Parametric"),
                 tab ? QStringLiteral("Implicit") : QStringLiteral("Parametric"));
    if (tab != engine)
        return QStringLiteral("modalita': linguetta %1, motore %2")
            .arg(tab ? QStringLiteral("Implicit") : QStringLiteral("Parametric"),
                 engine ? QStringLiteral("Implicit") : QStringLiteral("Parametric"));
    return QString();
}

QString ScenarioTest::meshScopeProblem() const
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const bool all = m_mw->meshScopeAll();
    if (ui->radioMeshAll->isChecked() != all || ui->radioMeshOne->isChecked() == all)
        return QStringLiteral("ambito: stato %1, radio All %2 e Mesh %3")
            .arg(all ? QStringLiteral("All") : QStringLiteral("Mesh"),
                 onOff(ui->radioMeshAll->isChecked()), onOff(ui->radioMeshOne->isChecked()));
    if (ui->tabModeSelector->currentIndex() == 1 || gl->meshPartCount() <= 1) return QString();
    if (all && (gl->activeMeshPart() != -1 || !gl->meshAppearanceUniform()))
        return QStringLiteral("ambito All, ma nel motore fascia attiva %1, aspetto uniforme %2")
            .arg(gl->activeMeshPart() + 1).arg(onOff(gl->meshAppearanceUniform()));
    if (!all && gl->meshAppearanceUniform())
        return QStringLiteral("ambito Mesh, ma nel motore aspetto uniforme");
    return QString();
}

QString ScenarioTest::implicitRenderProblem() const
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const bool shell = m_mw->implicitShellSelected();
    if (ui->radioShell->isChecked() != shell || ui->radioSolid->isChecked() == shell)
        return QStringLiteral("Shell/Solid: stato %1, radio Shell %2 e Solid %3")
            .arg(shell ? QStringLiteral("Shell") : QStringLiteral("Solid"),
                 onOff(ui->radioShell->isChecked()), onOff(ui->radioSolid->isChecked()));
    // In Ray Marching la modalita' globale del motore e' Shell (1) o Solid (0).
    if (ui->tabModeSelector->currentIndex() == 1 && gl->globalRenderMode() != (shell ? 1 : 0))
        return QStringLiteral("Shell/Solid: stato %1, motore in modalita' %2")
            .arg(shell ? QStringLiteral("Shell") : QStringLiteral("Solid")).arg(gl->globalRenderMode());
    if (ui->radioMarcherPrecise->isChecked() != gl->hybridMarcher()
        || ui->radioMarcherFast->isChecked() == gl->hybridMarcher())
        return QStringLiteral("marcher: motore %1, radio Fast %2 e Precise %3")
            .arg(gl->hybridMarcher() ? QStringLiteral("Precise") : QStringLiteral("Fast"),
                 onOff(ui->radioMarcherFast->isChecked()), onOff(ui->radioMarcherPrecise->isChecked()));
    return QString();
}

QString ScenarioTest::renderModeProblem() const
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    // In Ray Marching i radio classici non governano nulla.
    if (ui->tabModeSelector->currentIndex() == 1) return QString();
    static const char *const names[3] = { "Base", "Phong", "Wireframe" };
    auto name = [](int m) { return (m >= 0 && m < 3) ? QString::fromLatin1(names[m]) : QStringLiteral("nessuno"); };
    const int global = m_mw->m_scene.renderMode;
    if (gl->globalRenderMode() != global)
        return QStringLiteral("modalita' globale: stato %1, motore %2").arg(name(global), name(gl->globalRenderMode()));
    const bool onePart = gl->activeMeshPart() >= 0 && gl->meshPartCount() > 1;
    const int shown = onePart ? gl->activeMeshEffectiveRenderMode() : global;
    const int radio = ui->radioWF->isChecked() ? 2 : ui->radioPhong->isChecked() ? 1
                    : ui->radioBasic->isChecked() ? 0 : -1;
    if (radio != shown)
        return QStringLiteral("radio %1, %2 %3").arg(name(radio),
                                                     onePart ? QStringLiteral("fascia") : QStringLiteral("globale"),
                                                     name(shown));
    return QString();
}

QString ScenarioTest::editTargetProblem() const
{
    Ui::MainWindow *ui = m_mw->ui;
    // Lo stato e' il bersaglio; i radio la sua vista.
    const bool onBg = m_mw->editingBackground();
    if (ui->radioBackground->isChecked() != onBg || ui->radioSurface->isChecked() == onBg)
        return QStringLiteral("bersaglio %1, radio Surface %2 e Background %3")
            .arg(onBg ? QStringLiteral("Background") : QStringLiteral("Surface"),
                 onOff(ui->radioSurface->isChecked()), onOff(ui->radioBackground->isChecked()));
    // Zoom, pan e rotazione 2D (mouse in vista 2D, Save Texture) agiscono sul
    // bersaglio della vista 2D del motore: deve essere quello del dock.
    const int flat = ui->glWidget->flatViewTarget();
    if (flat != (onBg ? 1 : 0))
        return QStringLiteral("vista 2D del motore sul%1 col bersaglio %2")
            .arg(flat == 1 ? QStringLiteral("lo sfondo") : QStringLiteral("la superficie"),
                 onBg ? QStringLiteral("Background") : QStringLiteral("Surface"));
    return QString();
}

void ScenarioTest::runTextureTargetScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    auto click = [this](QAbstractButton *b) { b->click(); wait(200); };
    auto master = [this](const char *expectedLabel) {
        // Il tasto master della status bar: START o STOP secondo lo stato.
        if (!m_mw->m_btnStart) return false;
        if (m_mw->m_btnStart->text().toUpper() != QLatin1String(expectedLabel)) return false;
        m_mw->m_btnStart->click();
        wait(800);
        return true;
    };
    auto viewOk = [this](const QString &step) {
        const QString p = textureCheckboxProblem();
        check(p.isEmpty(), step + QStringLiteral(" -> checkbox ") + (p.isEmpty() ? QStringLiteral("coerente col bersaglio") : p));
    };

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Bersaglio Background: la texture di superficie resta della superficie (parametrico) =="));
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json"))) {
        // Superficie statica con una texture animata: l'unico moto e' il suo.
        click(ui->chkBoxTexture);
        setScriptMode(MainWindow::ScriptModeTexture);
        ui->txtScriptEditor->setPlainText(QStringLiteral("return vec3(fract(u + t), 0.5, 0.5);"));  wait(300);
        m_mw->onRunCurrentScript();  wait(800);
        check(gl->isSurfaceTextureAnimating(), QStringLiteral("texture animata eseguita -> il suo orologio gira"));
        click(ui->radioBackground);
        viewOk(QStringLiteral("bersaglio Background, sfondo spento"));
        check(!gl->isBackgroundTextureEnabled() && gl->isSurfaceTextureAnimating(),
              QStringLiteral("bersaglio Background, sfondo spento -> la texture di superficie gira ancora"));
        check(master("STOP"), QStringLiteral("master: il tasto dice STOP (%1)").arg(masterState()));
        check(!gl->isSurfaceTextureAnimating(), QStringLiteral("master Stop -> texture ferma"));
        check(master("START"), QStringLiteral("master: il tasto dice START"));
        check(gl->isSurfaceTextureAnimating(),
              QStringLiteral("master Start dal bersaglio Background -> la texture di superficie riparte"));
        // Sfondo acceso e poi spento dal checkbox: non tocca la superficie.
        click(ui->chkBoxTexture);
        viewOk(QStringLiteral("sfondo acceso dal checkbox"));
        check(gl->isBackgroundTextureEnabled() && m_mw->m_scene.surfaceTextureState,
              QStringLiteral("sfondo acceso dal checkbox -> sfondo on, texture di superficie on"));
        click(ui->chkBoxTexture);
        viewOk(QStringLiteral("sfondo spento dal checkbox"));
        check(!gl->isBackgroundTextureEnabled() && m_mw->m_scene.surfaceTextureState && gl->isSurfaceTextureAnimating(),
              QStringLiteral("sfondo spento dal checkbox -> la texture di superficie resta accesa e in moto"));
        click(ui->radioSurface);
        viewOk(QStringLiteral("ritorno a Surface"));
        checkTextureEnabled(QStringLiteral("ritorno a Surface"), true);
        setScriptMode(MainWindow::ScriptModeSurface);
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Bersaglio Background: la texture di superficie resta della superficie (Ray Marching, %1) ==")
                       .arg(QString::fromLatin1(kImplicitRecord)));
    if (loadRecord(QString::fromLatin1(kImplicitRecord))
        && selectTexture(QStringLiteral("textures/Ray Marching/Hellish Plasma.json"))) {
        wait(600);
        check(m_mw->hasTimeVariable(m_mw->m_scene.rm.texture),
              QStringLiteral("texture Ray Marching animata dalla Library -> il codice usa il tempo"));
        check(gl->isSurfaceTextureAnimating(),
              QStringLiteral("texture Ray Marching animata dalla Library -> il suo orologio gira"));
        click(ui->radioBackground);
        viewOk(QStringLiteral("bersaglio Background"));
        if (ui->chkBoxTexture->isChecked()) { m_discardOnPrompt = true; click(ui->chkBoxTexture); m_discardOnPrompt = false; }
        viewOk(QStringLiteral("sfondo spento dal checkbox"));
        check(!gl->isBackgroundTextureEnabled() && m_mw->m_scene.surfaceTextureState,
              QStringLiteral("sfondo spento dal checkbox -> sfondo off, texture di superficie on"));
        check(gl->isSurfaceTextureAnimating(), QStringLiteral("sfondo spento -> la texture di superficie gira ancora"));
        if (master("STOP")) {
            check(!gl->isSurfaceTextureAnimating(), QStringLiteral("master Stop -> texture ferma"));
            check(master("START"), QStringLiteral("master: il tasto dice START"));
            check(gl->isSurfaceTextureAnimating(),
                  QStringLiteral("master Start dal bersaglio Background -> la texture di superficie riparte"));
        } else {
            check(false, QStringLiteral("master: il tasto dice STOP (%1)").arg(masterState()));
        }
        // Il tasto Run/Stop della texture nel dock Equations.
        if (ui->btnTextureCode->text() == QLatin1String("Stop")) { click(ui->btnTextureCode); wait(300); }
        check(!gl->isSurfaceTextureAnimating(), QStringLiteral("Stop della texture (dock Equations) -> ferma"));
        click(ui->btnTextureCode);  wait(600);
        check(gl->isSurfaceTextureAnimating(),
              QStringLiteral("Run della texture dal bersaglio Background -> la texture di superficie riparte"));
        click(ui->radioSurface);
        viewOk(QStringLiteral("ritorno a Surface"));
        checkTextureEnabled(QStringLiteral("ritorno a Surface"), true);
    }

    // Load e reset col bersaglio su Background: il checkbox segue il bersaglio
    // nuovo (Surface) e lo stato della sua texture.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Bersaglio Background: load e scelte in Library =="));
    if (loadRecord(QString::fromLatin1(kParametricRecord))) {
        click(ui->radioBackground);
        viewOk(QStringLiteral("bersaglio Background su un record con sfondo"));
        if (selectTexture(QStringLiteral("textures/Procedurals/Plasma.json")))
            viewOk(QStringLiteral("procedurale di sfondo dalla Library"));
        if (selectTexture(QStringLiteral("textures/Images/14.png")))
            viewOk(QStringLiteral("immagine di sfondo dalla Library"));
        if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
            viewOk(QStringLiteral("load di un record dal bersaglio Background"));
            checkTextureEnabled(QStringLiteral("load di un record dal bersaglio Background"), true);
        }
        click(ui->radioBackground);
        if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json"))) {
            viewOk(QStringLiteral("load di una superficie dal bersaglio Background"));
            checkTextureEnabled(QStringLiteral("load di una superficie dal bersaglio Background"), false);
        }
    }

    // TEXTURE DI MODALITA' OPPOSTA a scena e texture da salvare: UN popup solo
    // (quello della scena, che elenca anche la texture). Prima il click
    // chiedeva della texture e poi il cambio di modalita' della scena: due
    // popup, e il secondo riproponeva la texture.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Texture di modalita' opposta: un popup solo =="));
    if (loadSurface(QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json"))
        && selectTexture(QStringLiteral("textures/Procedurals/Plasma.json"))) {
        setScriptMode(MainWindow::ScriptModeTexture);
        ui->txtScriptEditor->setPlainText(ui->txtScriptEditor->toPlainText()
                                          + QStringLiteral("\nfloat seTestUnused = 1.0;"));
        wait(300);
        check(m_mw->textureModuleDirty() && m_mw->hasUnsavedWork(),
              QStringLiteral("texture modificata su un toro: scena e texture da salvare"));
        const int prompts = m_discardPrompts;
        const int closed = m_popupsClosed;
        if (selectTexture(QStringLiteral("textures/Ray Marching/Fluid Iridescence.json"))) {
            check(m_discardPrompts == prompts + 1 && m_popupsClosed == closed,
                  QStringLiteral("texture Ray Marching -> popup %1 (atteso 1), altri %2")
                      .arg(m_discardPrompts - prompts).arg(m_popupsClosed - closed));
            check(m_mw->implicitMode(), QStringLiteral("poi la scena e' in Ray Marching"));
        }
    }
}

void ScenarioTest::runScriptDockScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    auto click = [this](QAbstractButton *b) { b->click(); wait(200); };
    auto type = [this, ui](const QString &text) { ui->txtScriptEditor->setPlainText(text); wait(300); };
    auto shows = [ui](const QString &text) { return ui->txtScriptEditor->toPlainText().trimmed() == text.trimmed(); };
    auto runOn = [ui] { return ui->btnRunCurrentScript->isEnabled(); };
    auto shownBrief = [ui] { return briefCode(ui->txtScriptEditor->toPlainText()); };
    const QString kTorus = QStringLiteral("surfaces/Parametric/Equations/R3/Torus.json");
    const QString kStripes4 = QStringLiteral("return mix(u_col2, u_col1, step(0.5, fract(u * 4.0)));");
    const QString kStripes6 = QStringLiteral("return mix(u_col2, u_col1, step(0.5, fract(u * 6.0)));");

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Dock Script: script di superficie scritto e non eseguito =="));
    if (loadSurface(kTorus)) {
        setScriptMode(MainWindow::ScriptModeSurface);
        const QString draft = QStringLiteral(
            "return vec4((0.8 + 0.3*cos(v))*cos(u), (0.8 + 0.3*cos(v))*sin(u), 0.5*sin(v), 0.0);");
        type(draft);
        check(runOn(), QStringLiteral("script scritto -> tasto Run acceso"));
        setScriptMode(MainWindow::ScriptModeTexture);
        check(!shows(draft), QStringLiteral("modulo Texture -> l'editor non mostra lo script di superficie (%1)").arg(shownBrief()));
        setScriptMode(MainWindow::ScriptModeSound);
        setScriptMode(MainWindow::ScriptModeSurface);
        check(shows(draft), QStringLiteral("giro dei moduli e ritorno -> lo script e' ancora nell'editor (%1)").arg(shownBrief()));
        check(runOn(), QStringLiteral("giro dei moduli e ritorno -> tasto Run ancora acceso (non e' mai stato eseguito)"));
        check(!gl->getEngine()->isScriptModeActive(),
              QStringLiteral("giro dei moduli e ritorno -> a schermo c'e' ancora la superficie delle equazioni"));
        // La superficie e' ancora quella delle equazioni: il loro dock comanda.
        setScriptMode(MainWindow::ScriptModeTexture);
        ui->lineZ->setPlainText(QStringLiteral("0.4*sin(v)"));  wait(400);
        check(ui->btnRunParametric->isEnabled(),
              QStringLiteral("script in sospeso, equazione modificata -> tasto Run delle equazioni acceso"));
        applyEquationEdit(ui->lineZ);
        check(!gl->getEngine()->isScriptModeActive() && m_mw->m_eqApplied && m_mw->m_eqApplied->z == QLatin1String("0.4*sin(v)"),
              QStringLiteral("Run delle equazioni -> applicata l'equazione, non lo script in sospeso"));
        // Una texture dalla Library col dock sullo script di superficie.
        setScriptMode(MainWindow::ScriptModeSurface);
        if (selectTexture(QStringLiteral("textures/Procedurals/Plasma.json")))
            check(shows(draft), QStringLiteral("texture dalla Library col dock sulla superficie -> l'editor tiene lo script (%1)").arg(shownBrief()));
        // Ora lo si esegue.
        const int popups = m_popupsClosed;
        m_mw->onRunCurrentScript();  wait(800);
        check(m_popupsClosed == popups && gl->getEngine()->isScriptModeActive(),
              QStringLiteral("Run dello script -> a schermo"));
        check(!runOn(), QStringLiteral("script statico eseguito -> tasto Run spento"));
        setScriptMode(MainWindow::ScriptModeTexture);
        setScriptMode(MainWindow::ScriptModeSurface);
        check(shows(draft) && !runOn(), QStringLiteral("giro dei moduli -> script eseguito, tasto Run spento"));
    }

    // Superficie DA SCRIPT con un ritocco in sospeso: l'Invio su una costante
    // (commit di servizio) riapplica lo script a schermo, non il ritocco; il
    // master Start invece esegue cio' che e' scritto, e il Save lo scrive.
    if (loadSurface(kTorus)) {
        const QString s1 = QStringLiteral(
            "return vec4((A + 0.3*cos(v))*cos(u), (A + 0.3*cos(v))*sin(u), 0.3*sin(v), 0.0);");
        const QString s2 = QStringLiteral(
            "return vec4((A + 0.3*cos(v))*cos(u), (A + 0.3*cos(v))*sin(u), 0.6*sin(v), 0.0);");
        setScriptMode(MainWindow::ScriptModeSurface);
        type(s1);
        m_mw->onRunCurrentScript();  wait(800);
        const QString glsl1 = gl->getEngine()->getScriptCodeGLSL();
        check(gl->getEngine()->isScriptModeActive() && m_mw->m_scene.surfaceScriptApplied == s1,
              QStringLiteral("script con la costante A eseguito -> a schermo"));
        type(s2);
        ui->lineA->setText(QStringLiteral("0.9"));
        pressEnter(ui->lineA);
        check(gl->getEngine()->getScriptCodeGLSL() == glsl1 && m_mw->m_scene.surfaceScriptApplied == s1,
              QStringLiteral("ritocco in sospeso, Invio su una costante -> a schermo resta lo script di prima"));
        setScriptMode(MainWindow::ScriptModeSurface);
        check(shows(s2) && runOn(), QStringLiteral("ritocco in sospeso, Invio su una costante -> il ritocco aspetta ancora il Run"));
        checkEquations(QStringLiteral("ritocco ancora in sospeso"), /*pendingEdit=*/true);
        if (m_mw->m_btnStart) { m_mw->m_btnStart->click();  wait(800); }
        check(gl->getEngine()->getScriptCodeGLSL() != glsl1 && m_mw->m_scene.surfaceScriptApplied == s2,
              QStringLiteral("master Start -> esegue lo script com'e' scritto"));
        check(captureSave().scriptCode == s2, QStringLiteral("master Start -> il Save scrive lo script eseguito"));
        if (m_mw->m_btnStart && m_mw->m_btnStart->text().toUpper() == QLatin1String("STOP")) {
            m_mw->m_btnStart->click();  wait(400);
        }
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Dock Script: texture di superficie scritta e non eseguita =="));
    if (loadSurface(kTorus)) {
        click(ui->chkBoxTexture);
        setScriptMode(MainWindow::ScriptModeTexture);
        type(kStripes4);
        m_mw->onRunCurrentScript();  wait(800);
        checkTextureCode(QStringLiteral("texture scritta ed eseguita"), kStripes4);
        check(!runOn(), QStringLiteral("texture statica eseguita -> tasto Run spento"));
        type(kStripes6);
        check(runOn(), QStringLiteral("texture ritoccata -> tasto Run acceso"));
        checkTextureCode(QStringLiteral("texture ritoccata, non eseguita"), kStripes6, /*pendingEdit=*/true);
        setScriptMode(MainWindow::ScriptModeSound);
        setScriptMode(MainWindow::ScriptModeSurface);
        checkTextureCode(QStringLiteral("dock su un altro modulo"), kStripes6, /*pendingEdit=*/true);
        setScriptMode(MainWindow::ScriptModeTexture);
        check(shows(kStripes6), QStringLiteral("giro dei moduli e ritorno -> il ritocco e' ancora nell'editor (%1)").arg(shownBrief()));
        check(runOn(), QStringLiteral("giro dei moduli e ritorno -> tasto Run ancora acceso"));
        click(ui->radioBackground);
        check(!shows(kStripes6), QStringLiteral("bersaglio Background -> l'editor mostra lo sfondo (%1)").arg(shownBrief()));
        checkTextureCode(QStringLiteral("bersaglio Background"), kStripes6, /*pendingEdit=*/true);
        click(ui->radioSurface);
        check(shows(kStripes6), QStringLiteral("ritorno a Surface -> il ritocco e' ancora nell'editor (%1)").arg(shownBrief()));
        check(runOn(), QStringLiteral("ritorno a Surface -> tasto Run ancora acceso"));
        check(graphicsOf(m_mw->m_scene.surfaceTextureCode, true) == kStripes4,
              QStringLiteral("ritorno a Surface -> a schermo c'e' ancora la texture eseguita"));
        m_mw->onRunCurrentScript();  wait(800);
        checkTextureCode(QStringLiteral("Run del ritocco"), kStripes6);
        check(!runOn(), QStringLiteral("Run del ritocco -> tasto Run spento"));
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Dock Script: sfondo scritto e non eseguito =="));
    if (loadSurface(kTorus)) {
        const QString bg1 = QStringLiteral("return vec3(uv.x, uv.y, 0.5);");
        const QString bg2 = QStringLiteral("return vec3(uv.x, uv.y, 0.25);");
        click(ui->radioBackground);
        if (!ui->chkBoxTexture->isChecked()) click(ui->chkBoxTexture);
        setScriptMode(MainWindow::ScriptModeTexture);
        type(bg1);
        m_mw->onRunCurrentScript();  wait(800);
        check(graphicsOf(m_mw->m_scene.bgTextureCode, true) == bg1, QStringLiteral("sfondo scritto ed eseguito -> applicato"));
        check(!runOn(), QStringLiteral("sfondo statico eseguito -> tasto Run spento"));
        type(bg2);
        check(runOn(), QStringLiteral("sfondo ritoccato -> tasto Run acceso"));
        click(ui->radioSurface);
        check(!shows(bg2), QStringLiteral("bersaglio Surface -> l'editor non mostra lo sfondo (%1)").arg(shownBrief()));
        click(ui->radioBackground);
        check(shows(bg2), QStringLiteral("ritorno a Background -> il ritocco e' ancora nell'editor (%1)").arg(shownBrief()));
        check(runOn(), QStringLiteral("ritorno a Background -> tasto Run ancora acceso"));
        setScriptMode(MainWindow::ScriptModeSound);
        setScriptMode(MainWindow::ScriptModeTexture);
        check(shows(bg2) && runOn(), QStringLiteral("giro dei moduli e ritorno -> ritocco nell'editor, tasto Run acceso"));
        check(graphicsOf(m_mw->m_scene.bgTextureCode, true) == bg1,
              QStringLiteral("giro dei moduli e ritorno -> a schermo c'e' ancora lo sfondo eseguito"));
        check(graphicsOf(captureSave().bgTextureCode, true) == bg2,
              QStringLiteral("il Save scrive lo sfondo com'e' scritto"));
        click(ui->radioSurface);
        setScriptMode(MainWindow::ScriptModeSurface);
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Dock Script: suono scritto e non eseguito =="));
    if (loadSurface(kTorus)) {
        const QString snd = QStringLiteral("//SOUND_BEGIN\nvec2 mainSound(int samp, float time) { return vec2(0.0); }\n//SOUND_END");
        setScriptMode(MainWindow::ScriptModeSound);
        type(snd);
        setScriptMode(MainWindow::ScriptModeSurface);
        check(!shows(snd), QStringLiteral("modulo Surface -> l'editor non mostra il suono (%1)").arg(shownBrief()));
        check(captureSave().textureCode.contains(QLatin1String("mainSound")),
              QStringLiteral("dock su un altro modulo -> il Save scrive il suono com'e' scritto"));
        setScriptMode(MainWindow::ScriptModeSound);
        check(shows(snd), QStringLiteral("giro dei moduli e ritorno -> il suono e' ancora nell'editor (%1)").arg(shownBrief()));
        pressNew();
        check(ui->txtScriptEditor->toPlainText().isEmpty(), QStringLiteral("tasto NEW -> editor vuoto"));
        setScriptMode(MainWindow::ScriptModeSurface);
    }

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Dock Script: texture di una fascia (%1) ==").arg(QString::fromLatin1(kMultiMeshRecord)));
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
        ui->spinMeshSel->setValue(1);  wait(300);
        setScriptMode(MainWindow::ScriptModeTexture);
        const QString strip = gl->activeMeshEffectiveTextureCode();
        check(!strip.trimmed().isEmpty() && shows(strip),
              QStringLiteral("fascia 1 -> l'editor mostra la sua texture (%1)").arg(shownBrief()));
        setScriptMode(MainWindow::ScriptModeSound);
        setScriptMode(MainWindow::ScriptModeSurface);
        setScriptMode(MainWindow::ScriptModeTexture);
        check(shows(strip), QStringLiteral("giro dei moduli e ritorno -> ancora la texture della fascia (%1)").arg(shownBrief()));
        click(ui->radioBackground);
        click(ui->radioSurface);
        check(shows(strip), QStringLiteral("Background e ritorno -> ancora la texture della fascia (%1)").arg(shownBrief()));
        ui->spinMeshSel->setValue(4);  wait(300);
        check(shows(gl->activeMeshEffectiveTextureCode()),
              QStringLiteral("fascia 4 -> l'editor mostra la sua (%1)").arg(shownBrief()));
        click(ui->radioMeshAll);
        check(shows(m_mw->surfaceTextureScript()),
              QStringLiteral("ambito All -> l'editor mostra la texture della superficie (%1)").arg(shownBrief()));
        checkTextureCode(QStringLiteral("ambito All"));
        setScriptMode(MainWindow::ScriptModeSurface);
    }

    // RAY MARCHING: la texture di superficie si scrive nel dock Equations, il
    // dock Script in Texture non la mostra. onApplyTextureScriptClicked ha un
    // cambio di linguetta forzato verso il parametrico (applicata una texture
    // parametrica in Ray Marching): da qui non ci si deve arrivare, o la scena
    // RM se ne andrebbe senza avviso.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Dock Script: modulo Texture in Ray Marching (%1) ==")
                       .arg(QString::fromLatin1(kImplicitRecord)));
    if (loadRecord(QString::fromLatin1(kImplicitRecord))) {
        if (m_mw->editingBackground()) click(ui->radioSurface);
        setScriptMode(MainWindow::ScriptModeTexture);
        check(ui->txtScriptEditor->toPlainText().trimmed().isEmpty(),
              QStringLiteral("bersaglio Surface -> l'editor non mostra la texture RM (%1)").arg(shownBrief()));
        const QString rmTexBefore = ui->lineTexture->toPlainText();
        const int popups = m_popupsClosed;
        type(kStripes4);
        m_mw->onRunCurrentScript();  wait(800);
        check(ui->tabModeSelector->currentIndex() == 1 && m_mw->implicitMode()
                  && ui->lineTexture->toPlainText() == rmTexBefore,
              QStringLiteral("scritto e Run -> resta in Ray Marching con la sua texture (popup: %1)")
                  .arg(m_popupsClosed - popups));
        m_mw->onApplyTextureScriptClicked();  wait(800);
        check(ui->tabModeSelector->currentIndex() == 1 && m_mw->implicitMode()
                  && ui->lineTexture->toPlainText() == rmTexBefore,
              QStringLiteral("applicazione della texture di superficie -> resta in Ray Marching (slot: %1)")
                  .arg(briefCode(m_mw->surfaceTextureScript())));
        setScriptMode(MainWindow::ScriptModeSurface);
    }
}

void ScenarioTest::runMeshClockScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    auto click = [this](QAbstractButton *b) { b->click(); wait(200); };
    // Fasce (da 1) col proprio orologio acceso.
    auto animating = [gl] {
        QList<int> on;
        const auto &parts = gl->getEngine()->getMeshParts();
        for (int k = 0; k < (int)parts.size(); ++k)
            if (parts[k].texAnimating) on << k + 1;
        return on;
    };
    auto names = [](const QList<int> &l) {
        QStringList s;
        for (int k : l) s << QString::number(k);
        return s.isEmpty() ? QStringLiteral("nessuna") : s.join(QLatin1Char(','));
    };
    const QString rec = QStringLiteral("records/Solid Wireframe/Multi Mesh/Hopf Parallel.json");
    const QString staticTex = QStringLiteral("textures/Procedurals/Stripes.json");
    const QString animTex = QStringLiteral("textures/Procedurals/Animated Gradient.json");

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Orologi delle fasce a master fermo (%1) ==").arg(rec));
    if (!loadRecord(rec)) return;
    if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
    check(!animating().isEmpty(), QStringLiteral("load -> fasce animate in moto (%1)").arg(names(animating())));
    if (m_mw->m_btnStart->text().toUpper() == QLatin1String("STOP")) click(m_mw->m_btnStart);
    wait(300);
    check(animating().isEmpty(), QStringLiteral("master Stop -> nessuna fascia in moto (%1)").arg(names(animating())));

    ui->spinMeshSel->setValue(6);  wait(300);
    if (selectTexture(staticTex))
        check(animating().isEmpty(),
              QStringLiteral("fascia 6, texture statica dalla Library -> nessuna riparte (%1)").arg(names(animating())));
    ui->spinMeshSel->setValue(2);  wait(300);
    if (selectTexture(animTex))
        check(animating() == QList<int>{ 2 },
              QStringLiteral("fascia 2, texture animata dalla Library -> parte solo lei (%1)").arg(names(animating())));
    if (selectTexture(animTex))
        check(animating() == QList<int>{ 2 },
              QStringLiteral("fascia 2, riclic sulla stessa -> ancora solo lei (%1)").arg(names(animating())));
    click(ui->chkBoxTexture);
    check(animating().isEmpty(),
          QStringLiteral("fascia 2, checkbox spento -> nessuna in moto (%1)").arg(names(animating())));
    click(ui->chkBoxTexture);
    check(animating() == QList<int>{ 2 },
          QStringLiteral("fascia 2, checkbox riacceso -> parte solo lei (%1)").arg(names(animating())));
    // Riferimento: il Run del dock Script sulla fascia 3 (animata nel record).
    ui->spinMeshSel->setValue(3);  wait(300);
    setScriptMode(MainWindow::ScriptModeTexture);
    m_mw->onRunCurrentScript();  wait(400);
    check(animating() == QList<int>{ 2, 3 },
          QStringLiteral("fascia 3, Run del dock Script -> riparte anche lei (%1)").arg(names(animating())));
    setScriptMode(MainWindow::ScriptModeSurface);
    // Un gesto che non riguarda le fasce non le riaccende.
    pressEnter(ui->lineA);
    check(animating() == QList<int>{ 2, 3 },
          QStringLiteral("Invio su una costante -> nessun'altra fascia riparte (%1)").arg(names(animating())));
    // Fascia animata nel record (ferma dal master) che riceve una STATICA.
    ui->spinMeshSel->setValue(1);  wait(300);
    if (selectTexture(staticTex))
        check(animating() == QList<int>{ 2, 3 },
              QStringLiteral("fascia 1, texture statica dalla Library -> nessun'altra riparte (%1)").arg(names(animating())));
    ui->spinMeshSel->setValue(4);  wait(300);
    if (selectTexture(animTex))
        check(animating() == QList<int>{ 2, 3, 4 },
              QStringLiteral("fascia 4, texture animata dalla Library -> parte anche lei (%1)").arg(names(animating())));
    wait(2000);
    check(animating() == QList<int>{ 2, 3, 4 },
          QStringLiteral("dopo due secondi -> nessuna ripartita da sola (%1)").arg(names(animating())));
}

void ScenarioTest::runMeshImageScenarios()
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    auto click = [this](QAbstractButton *b) { b->click(); wait(200); };
    auto part = [gl](int n) -> const MeshPart * {   // n = numero della fascia, da 1
        const auto &parts = gl->getEngine()->getMeshParts();
        return (n >= 1 && n <= (int)parts.size()) ? &parts[n - 1] : nullptr;
    };
    auto tagOf = [](const MeshPart *p) {
        return p ? QFileInfo(GLWidget::imagePathInTextureCode(p->textureCode)).fileName() : QString();
    };
    auto logicOf = [](const MeshPart *p) { return p ? graphicsOf(p->textureCode, true) : QString(); };
    auto loaded = [gl](int n) { return QFileInfo(gl->meshPartLoadedImagePath(n - 1)).fileName(); };
    // Colore medio di cio' che e' a schermo e di un file immagine.
    auto meanOf = [](const QImage &src) {
        const QImage img = src.convertToFormat(QImage::Format_RGB32).scaled(64, 64);
        double r = 0, g = 0, b = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QRgb c = img.pixel(x, y);
                r += qRed(c); g += qGreen(c); b += qBlue(c);
            }
        const double n = img.width() * img.height();
        return QVector3D(r / n, g / n, b / n);
    };
    auto rgb = [](const QVector3D &v) {
        return QStringLiteral("(%1,%2,%3)").arg(qRound(v.x())).arg(qRound(v.y())).arg(qRound(v.z()));
    };
    const QString imgA = QStringLiteral("textures/Images/14.png");   // azzurra
    const QString imgB = QStringLiteral("textures/Images/2.png");    // rossa
    const QVector3D meanA = meanOf(QImage(m_root + QLatin1Char('/') + imgA));
    const QVector3D meanB = meanOf(QImage(m_root + QLatin1Char('/') + imgB));

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Immagini sulle singole mesh (%1) ==").arg(QString::fromLatin1(kMultiMeshRecord)));
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        if (!ui->radioMeshOne->isChecked()) click(ui->radioMeshOne);
        const bool surfIntent = m_mw->m_scene.surfaceTextureState;
        const QString surfImage = gl->surfaceImagePath();
        const QString surfCode = m_mw->m_scene.surfaceTextureCode;
        const QString strip1 = part(1) ? part(1)->textureCode : QString();
        const int popups = m_popupsClosed;

        // Fascia 3 (ha una procedurale): l'immagine la sostituisce, ferma.
        ui->spinMeshSel->setValue(3);  wait(300);
        if (selectTexture(imgA)) {
            wait(600);
            const MeshPart *p = part(3);
            check(p && p->effectiveTextureEnabledMulti() && tagOf(p) == QLatin1String("14.png")
                      && logicOf(p).isEmpty(),
                  QStringLiteral("immagine con la fascia 3 selezionata -> lo script della fascia e' la sola immagine (tag '%1', resto %2)")
                      .arg(tagOf(p), briefCode(logicOf(p))));
            check(loaded(3) == QLatin1String("14.png"),
                  QStringLiteral("immagine sulla fascia 3 -> in GPU la fascia ha la sua immagine ('%1')").arg(loaded(3)));
            check(p && !p->texAnimating, QStringLiteral("immagine sulla fascia 3 -> il suo orologio e' fermo"));
            check(m_mw->m_scene.surfaceTextureState == surfIntent && gl->surfaceImagePath() == surfImage
                      && m_mw->m_scene.surfaceTextureCode == surfCode,
                  QStringLiteral("immagine sulla fascia 3 -> la texture della superficie non cambia (immagine '%1')")
                      .arg(QFileInfo(gl->surfaceImagePath()).fileName()));
            check(part(1) && part(1)->textureCode == strip1,
                  QStringLiteral("immagine sulla fascia 3 -> la fascia 1 tiene la sua texture"));
            check(m_popupsClosed == popups, QStringLiteral("immagine sulla fascia 3 -> nessun avviso"));
            checkTextureEnabled(QStringLiteral("immagine sulla fascia 3"), surfIntent);
            const QString pb = textureCheckboxProblem();
            check(ui->chkBoxTexture->isChecked() && pb.isEmpty(),
                  QStringLiteral("immagine sulla fascia 3 -> checkbox Texture acceso") + (pb.isEmpty() ? QString() : QStringLiteral(" (") + pb + QLatin1Char(')')));
            check(!ui->radioTexColor1->isEnabled() && !ui->radioTexColor2->isEnabled(),
                  QStringLiteral("immagine sulla fascia 3 -> picker Colore 1/2 spenti (una foto non ha tinte)"));
            setScriptMode(MainWindow::ScriptModeTexture);
            check(ui->txtScriptEditor->toPlainText().trimmed() == (QStringLiteral("//IMG:") + GLWidget::imagePathInTextureCode(p ? p->textureCode : QString())),
                  QStringLiteral("immagine sulla fascia 3 -> l'editor mostra il suo tag (%1)").arg(briefCode(ui->txtScriptEditor->toPlainText())));
            setScriptMode(MainWindow::ScriptModeSurface);
        }

        // Fascia 5: un'ALTRA immagine. Ognuna tiene la sua.
        ui->spinMeshSel->setValue(5);  wait(300);
        if (selectTexture(imgB)) {
            wait(600);
            check(tagOf(part(5)) == QLatin1String("2.png") && loaded(5) == QLatin1String("2.png"),
                  QStringLiteral("altra immagine sulla fascia 5 -> la fascia 5 ha la sua ('%1' in GPU)").arg(loaded(5)));
            check(tagOf(part(3)) == QLatin1String("14.png") && loaded(3) == QLatin1String("14.png"),
                  QStringLiteral("altra immagine sulla fascia 5 -> la fascia 3 tiene la sua ('%1' in GPU)").arg(loaded(3)));
            check(gl->surfaceImagePath() == surfImage,
                  QStringLiteral("altra immagine sulla fascia 5 -> l'immagine della superficie non cambia"));
        }

        // A SCHERMO: la vista 2D della fascia mostra la SUA immagine. (Il tasto
        // 2D si accende col dock Script sul modulo Texture.)
        setScriptMode(MainWindow::ScriptModeTexture);
        if (ui->btnFlatPreview->isEnabled()) {
            click(ui->btnFlatPreview);  wait(600);
            const QVector3D shown5 = meanOf(gl->grabFramebuffer());
            ui->spinMeshSel->setValue(3);  wait(600);
            const QVector3D shown3 = meanOf(gl->grabFramebuffer());
            check((shown5 - meanB).length() < (shown5 - meanA).length() && (shown5 - meanB).length() < 40.0f,
                  QStringLiteral("vista 2D della fascia 5 -> a schermo l'immagine rossa: colore medio %1, immagine %2")
                      .arg(rgb(shown5), rgb(meanB)));
            check((shown3 - meanA).length() < (shown3 - meanB).length() && (shown3 - meanA).length() < 40.0f,
                  QStringLiteral("vista 2D della fascia 3 -> a schermo l'immagine azzurra: colore medio %1, immagine %2")
                      .arg(rgb(shown3), rgb(meanA)));
            click(ui->btnFlatPreview);  wait(400);
        } else {
            check(false, QStringLiteral("tasto 2D acceso su una fascia con la sua immagine"));
        }
        setScriptMode(MainWindow::ScriptModeSurface);

        // Il Save scrive le due immagini nelle due fasce.
        LibraryItem savedWithImages;
        {
            const LibraryItem s = captureSave();
            savedWithImages = s;
            const QString t3 = s.meshParts.size() > 2 ? QFileInfo(GLWidget::imagePathInTextureCode(s.meshParts[2].textureCode)).fileName() : QString();
            const QString t5 = s.meshParts.size() > 4 ? QFileInfo(GLWidget::imagePathInTextureCode(s.meshParts[4].textureCode)).fileName() : QString();
            check(t3 == QLatin1String("14.png") && t5 == QLatin1String("2.png"),
                  QStringLiteral("Save -> le fasce 3 e 5 scrivono ciascuna la sua immagine ('%1', '%2')").arg(t3, t5));
        }

        // Uno script di "Animated Images" sulla fascia 3: anima la SUA immagine.
        ui->spinMeshSel->setValue(3);  wait(300);
        if (selectTexture(QStringLiteral("textures/Procedurals/Animated Images/Rotating Image.json"))) {
            wait(600);
            const MeshPart *p = part(3);
            check(tagOf(p) == QLatin1String("14.png") && !logicOf(p).isEmpty() && loaded(3) == QLatin1String("14.png"),
                  QStringLiteral("Rotating Image sulla fascia 3 -> lo script tiene l'immagine della fascia (tag '%1', '%2' in GPU)")
                      .arg(tagOf(p), loaded(3)));
            check(p && p->texAnimating, QStringLiteral("Rotating Image sulla fascia 3 -> il suo orologio gira"));
            check(loaded(5) == QLatin1String("2.png"),
                  QStringLiteral("Rotating Image sulla fascia 3 -> la fascia 5 tiene la sua immagine"));
        }

        // Ambito All: l'aspetto delle fasce e' sospeso, immagini comprese.
        click(ui->radioMeshAll);  wait(400);
        check(loaded(3).isEmpty() && loaded(5).isEmpty(),
              QStringLiteral("ambito All -> le immagini delle fasce sono sospese ('%1', '%2')").arg(loaded(3), loaded(5)));
        click(ui->radioMeshOne);  wait(400);
        check(loaded(3) == QLatin1String("14.png") && loaded(5) == QLatin1String("2.png"),
              QStringLiteral("ritorno a Mesh -> ogni fascia ritrova la sua immagine ('%1', '%2')").arg(loaded(3), loaded(5)));

        // Una procedurale che non campiona nulla toglie l'immagine dalla fascia.
        ui->spinMeshSel->setValue(3);  wait(300);
        if (selectTexture(QStringLiteral("textures/Procedurals/Plasma.json"))) {
            wait(600);
            check(tagOf(part(3)).isEmpty() && loaded(3).isEmpty() && !logicOf(part(3)).isEmpty(),
                  QStringLiteral("Plasma sulla fascia 3 -> l'immagine della fascia non c'e' piu' (tag '%1', '%2' in GPU)")
                      .arg(tagOf(part(3)), loaded(3)));
        }

        // Checkbox: spegne e riaccende la texture della fascia 5 con la sua immagine.
        ui->spinMeshSel->setValue(5);  wait(300);
        click(ui->chkBoxTexture);  wait(300);
        check(part(5) && !part(5)->effectiveTextureEnabledMulti() && tagOf(part(5)) == QLatin1String("2.png"),
              QStringLiteral("checkbox spento sulla fascia 5 -> texture spenta, immagine conservata"));
        click(ui->chkBoxTexture);  wait(400);
        check(part(5) && part(5)->effectiveTextureEnabledMulti() && tagOf(part(5)) == QLatin1String("2.png")
                  && loaded(5) == QLatin1String("2.png"),
              QStringLiteral("checkbox riacceso sulla fascia 5 -> di nuovo la sua immagine ('%1' in GPU)").arg(loaded(5)));

        // Fascia mai configurata, e senza immagine sulla superficie: uno script
        // di Animated Images non le da' un'immagine (campiona quella della
        // superficie, qui la scacchiera di ripiego).
        ui->spinMeshSel->setValue(16);  wait(300);
        if (selectTexture(QStringLiteral("textures/Procedurals/Animated Images/Still Image.json"))) {
            wait(600);
            check(tagOf(part(16)).isEmpty() && loaded(16).isEmpty(),
                  QStringLiteral("Still Image su una fascia senza immagine -> nessun tag: campiona l'immagine della superficie (tag '%1')")
                      .arg(tagOf(part(16))));
        }

        // Il record salvato con le due immagini, ricaricato: ogni fascia ritrova
        // la sua (il load passa dallo stesso percorso di un record della Library).
        if (!savedWithImages.meshParts.empty()) {
            m_discardOnPrompt = true;
            m_mw->applyMotionExample(savedWithImages);
            if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
            wait(1800);
            m_discardOnPrompt = false;
            check(tagOf(part(3)) == QLatin1String("14.png") && loaded(3) == QLatin1String("14.png")
                      && tagOf(part(5)) == QLatin1String("2.png") && loaded(5) == QLatin1String("2.png"),
                  QStringLiteral("record salvato e ricaricato -> le fasce 3 e 5 ritrovano le loro immagini ('%1', '%2' in GPU)")
                      .arg(loaded(3), loaded(5)));
            check(part(3) && part(3)->effectiveTextureEnabledMulti() && part(5) && part(5)->effectiveTextureEnabledMulti(),
                  QStringLiteral("record salvato e ricaricato -> le due texture sono accese"));
            checkDirty(QStringLiteral("record salvato e ricaricato"), false, false, false);
        }
        if (!ui->radioMeshAll->isChecked()) click(ui->radioMeshAll);
    }

    // RECORD SALVATI PRIMA: il tag della fascia ripete l'immagine della
    // superficie (e in una fascia porta il percorso di un altro dispositivo).
    // Non si carica nulla due volte, e l'aspetto resta quello di prima.
    const QString oldRecord = QStringLiteral("records/Solid Wireframe/Multi Mesh/Hopf Half Tori.json");
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Immagini sulle singole mesh: record salvato prima (%1) ==").arg(oldRecord));
    if (loadRecord(oldRecord)) {
        const QString surf = QFileInfo(gl->surfaceImagePath()).fileName();
        check(!surf.isEmpty(), QStringLiteral("load -> la superficie ha la sua immagine ('%1')").arg(surf));
        bool anyOwn = false, allSame = true, allReadable = true;
        int tagged = 0;
        const auto &parts = gl->getEngine()->getMeshParts();
        for (int k = 0; k < (int)parts.size(); ++k) {
            const QString path = GLWidget::imagePathInTextureCode(parts[k].textureCode);
            if (path.isEmpty()) continue;
            ++tagged;
            if (!gl->meshPartLoadedImagePath(k).isEmpty()) anyOwn = true;
            if (path != gl->surfaceImagePath()) allSame = false;
            if (!QFileInfo(path).isReadable()) allReadable = false;
        }
        check(tagged > 0 && allReadable,
              QStringLiteral("load -> i tag delle fasce (%1) puntano a file leggibili su questo dispositivo").arg(tagged));
        check(allSame && !anyOwn,
              QStringLiteral("load -> stesso file della superficie: nessuna immagine caricata due volte"));
    }

    // IMMAGINE DI UNA FASCIA NON TROVATA: stesso avviso di superficie e
    // sfondo, prima di toccare la scena; la fascia ripiega sull'immagine della
    // superficie. Prima nessun avviso.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Immagine di una fascia non trovata (%1) ==").arg(QString::fromLatin1(kMultiMeshRecord)));
    {
        LibraryManager lm;
        LibraryItem item = lm.parseJson(m_root + QLatin1Char('/') + QString::fromLatin1(kMultiMeshRecord),
                                        LibraryType::Motion);
        const QString missing = QStringLiteral("/nonexistent/Missing Strip Image.png");
        if (!item.name.isEmpty() && !item.meshParts.empty()) {
            MeshPart &mp = item.meshParts[0];
            mp.textureCode = QStringLiteral("//IMG:") + missing + QLatin1Char('\n') + mp.textureCode;
            mp.hasCustomTexture = true;
            mp.textureEnabled = true;
            m_lastPopup.clear();
            const int popups = m_popupsClosed;
            m_mw->applyMotionExample(item);
            if (m_mw->m_audioController) m_mw->m_audioController->stopAll();
            wait(1500);
            check(m_popupsClosed == popups + 1 && m_lastPopup.contains(QLatin1String("was not found"))
                      && m_lastPopup.contains(missing),
                  QStringLiteral("load -> avviso col percorso della fascia (%1)").arg(m_lastPopup.simplified().left(160)));
        } else {
            check(false, QStringLiteral("record multi-mesh senza fasce"));
        }
    }
}

void ScenarioTest::collectShaderErrors()
{
    const QList<ShaderCompileLog::Entry> got = ShaderCompileLog::take();
    if (got.isEmpty()) return;
    QString section;
    for (int i = m_lines.size() - 1; i >= 0 && section.isEmpty(); --i)
        if (m_lines.at(i).startsWith(QLatin1String("=="))) section = m_lines.at(i);
    for (const ShaderCompileLog::Entry &e : got) {
        if (e.counts) m_shaderErrors.append(QStringLiteral("%1 (%2)").arg(e.text, section));
        else          m_shaderExcused.append(QStringLiteral("%1 (%2)  [non conta: %3]").arg(e.text, section, e.excuse));
    }
}

void ScenarioTest::finish()
{
    collectShaderErrors();
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Shader: nessuno che non compila (dal log) =="));
    for (const QString &x : m_shaderExcused) m_lines.append(QStringLiteral("        ") + x);
    check(m_shaderErrors.isEmpty(),
          m_shaderErrors.isEmpty()
              ? QStringLiteral("nessun errore di compilazione che conti")
              : QStringLiteral("errori di compilazione: ") + m_shaderErrors.join(QStringLiteral("; ")));

    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Load e reset: nessun campo di testo scritto a segnali vivi =="));
    check(m_liveWritesDuringLoad.isEmpty(),
          m_liveWritesDuringLoad.isEmpty()
              ? QStringLiteral("nessun textChanged durante load e reset")
              : QStringLiteral("textChanged durante load e reset: ") + m_liveWritesDuringLoad.join(QStringLiteral("; ")));

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
