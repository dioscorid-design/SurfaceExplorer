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
#include <QRegularExpression>
#include <QTimer>
#include <QAbstractButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>

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
        // Il popup "lavoro non salvato" durante una scelta in Library: l'utente
        // che vuole proseguire risponde "Don't save" (Annulla lascerebbe la
        // scena com'era, e il gesto in prova non avverrebbe).
        if (auto *mb = qobject_cast<QMessageBox *>(w); mb && m_discardOnPrompt) {
            for (QAbstractButton *b : mb->buttons()) {
                if (mb->buttonRole(b) == QMessageBox::DestructiveRole) {
                    m_lines.append(QStringLiteral("        popup, Don't save: ") + desc.simplified());
                    b->click();
                    return;
                }
            }
        }
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
    run.path4D   = m_mw->pathTimer && m_mw->pathTimer->isActive();
    run.path3D   = m_mw->pathTimer3D && m_mw->pathTimer3D->isActive();
    const LibraryItem saved = m_mw->m_presetSerializer->captureMotionState(m_record, run, true);

    QStringList bad;
    if (global1.isValid() && (g1.name() != global1.name() || g2.name() != global2.name()))
        bad << QStringLiteral("globali %1 %2, attesi %3 %4")
                   .arg(g1.name(), g2.name(), global1.name(), global2.name());
    const QColor x1 = shown1.isValid() ? shown1 : global1, x2 = shown1.isValid() ? shown2 : global2;
    if (x1.isValid() && (e1.name() != x1.name() || e2.name() != x2.name()))
        bad << QStringLiteral("il motore disegna %1 %2, attesi %3 %4")
                   .arg(e1.name(), e2.name(), x1.name(), x2.name());
    if (!ui->radioBackground->isChecked() && (s1.name() != e1.name() || s2.name() != e2.name()))
        bad << QStringLiteral("picker %1 %2, il motore disegna %3 %4%5")
                   .arg(s1.name(), s2.name(), e1.name(), e2.name(),
                        part >= 0 ? QStringLiteral(" (fascia %1)").arg(part + 1) : QString());
    // Gli slider mostrano lo slot scelto quando editano davvero i colori della
    // texture (stessa condizione di onColorTargetChanged).
    const bool texHere = part >= 0 ? gl->activeMeshTextureActive() : m_mw->m_surfaceTextureState;
    if (!ui->radioBackground->isChecked() && !ui->radioWF->isChecked() && texHere
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
    run.path4D   = m_mw->pathTimer && m_mw->pathTimer->isActive();
    run.path3D   = m_mw->pathTimer3D && m_mw->pathTimer3D->isActive();
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
    const bool onBg = ui->radioBackground->isChecked();
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
    run.path4D   = m_mw->pathTimer && m_mw->pathTimer->isActive();
    run.path3D   = m_mw->pathTimer3D && m_mw->pathTimer3D->isActive();
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
    run.path4D   = m_mw->pathTimer && m_mw->pathTimer->isActive();
    run.path3D   = m_mw->pathTimer3D && m_mw->pathTimer3D->isActive();
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
        // PARAMETRICO: l'intenzione e' lo script della texture di superficie --
        // l'editor, se la sta mostrando, altrimenti il suo slot. L'applicato e'
        // m_surfaceTextureCode, e il motore deve compilare quello (senza codice:
        // la scacchiera di default, o niente sopra un'immagine).
        if (!rmField.isEmpty() || !rmEngine.isEmpty())
            bad << QStringLiteral("in parametrico: campo RM %1, motore RM %2")
                       .arg(briefCode(rmField), briefCode(rmEngine));
        const bool editorShows = m_mw->m_currentScriptMode == MainWindow::ScriptModeTexture
                                 && !ui->radioBackground->isChecked() && gl->activeMeshPart() < 0;
        const QString intent = graphicsOf(editorShows ? ui->txtScriptEditor->toPlainText()
                                                      : m_mw->m_surfaceTextureScriptText, true);
        const QString applied = graphicsOf(m_mw->m_surfaceTextureCode, true);
        const QString engine  = graphicsOf(gl->currentParametricTextureCode(), true);
        shown = intent;
        if (!expected.isNull() && intent != expected.trimmed())
            bad << QStringLiteral("editor %1, atteso %2").arg(briefCode(intent), briefCode(expected.trimmed()));
        if (!pendingEdit && applied != intent)
            bad << QStringLiteral("applicata %1, editor %2").arg(briefCode(applied), briefCode(intent));
        if (m_mw->m_surfaceTextureState) {
            const QString want = !applied.isEmpty() ? applied
                               : m_mw->surfaceHasImage() ? QString()
                                                     : m_mw->defaultMeshTextureCode();
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

    check(bad.isEmpty(), QStringLiteral("%1 -> codice %2%3")
                             .arg(step, briefCode(shown),
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
