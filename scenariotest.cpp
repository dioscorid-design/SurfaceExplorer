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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
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
        if (desc.contains(QLatin1String("slowing down"))) m_watchdogFired = true;
        else ++m_popupsClosed;
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
        // m_surfaceTextureCode, e il motore deve compilare quello (senza codice:
        // la scacchiera di default, o niente sopra un'immagine).
        if (!rmField.isEmpty() || !rmEngine.isEmpty())
            bad << QStringLiteral("in parametrico: campo RM %1, motore RM %2")
                       .arg(briefCode(rmField), briefCode(rmEngine));
        const QString intent = graphicsOf(m_mw->surfaceTextureScript(), true);
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

    if (!scriptEditorProblem().isEmpty()) bad << scriptEditorProblem();
    if (!rmFieldsProblem().isEmpty()) bad << rmFieldsProblem();

    check(bad.isEmpty(), QStringLiteral("%1 -> codice %2%3")
                             .arg(step, briefCode(shown),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

QString ScenarioTest::rmFieldsProblem() const
{
    // I quattro campi Ray Marching sono l'editor dello stato m_rm: stesso testo.
    Ui::MainWindow *ui = m_mw->ui;
    struct F { const char *name; QPlainTextEdit *edit; const QString &state; };
    const F fs[] = { { "equazione", ui->lineEquation, m_mw->m_rm.equation },
                     { "Cross Section", ui->lineEquationCrossSection, m_mw->m_rm.crossSection },
                     { "texture", ui->lineTexture, m_mw->m_rm.texture },
                     { "rilievo", ui->lineVariations, m_mw->m_rm.displacement } };
    for (const F &f : fs) {
        if (f.edit && f.edit->toPlainText() != f.state)
            return QStringLiteral("Ray Marching, %1: lo stato ha %2, il campo %3")
                .arg(QString::fromLatin1(f.name), briefCode(f.state), briefCode(f.edit->toPlainText()));
    }
    return QString();
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

    check(bad.isEmpty(), QStringLiteral("%1 -> costanti %2%3")
                             .arg(step, shown.isEmpty() ? QStringLiteral("(nessuna in uso)")
                                                        : shown.join(QLatin1Char(' ')),
                                  bad.isEmpty() ? QString() : QStringLiteral(": ") + bad.join(QStringLiteral("; "))));
}

void ScenarioTest::checkMotion(const QString &step, const QString &expectRunning, bool pendingEdit)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    SurfaceEngine *engine = gl->getEngine();
    const LibraryItem sv = captureSave();
    QStringList bad;

    const bool t4 = m_mw->pathTimer && m_mw->pathTimer->isActive();
    const bool t3 = m_mw->pathTimer3D && m_mw->pathTimer3D->isActive();
    const bool rot = gl->isAnimating();
    const bool rm = ui->tabModeSelector->currentIndex() == 1;
    const bool keep4D = !rm || ui->subTabImplicit->currentIndex() == 1;
    const QString running = t3 ? QStringLiteral("path3D") : t4 ? QStringLiteral("path4D")
                          : rot ? QStringLiteral("rotation") : QStringLiteral("none");

    // VISTE dei due path.
    auto viewName = [](int m) {
        return m == MainWindow::ModeTangential ? QStringLiteral("Tangent View") : QStringLiteral("Center View");
    };
    if (ui->pushView->text() != viewName(m_mw->m_pathViewMode4D))
        bad << QStringLiteral("vista 4D: tasto \"%1\", membro %2").arg(ui->pushView->text(), viewName(m_mw->m_pathViewMode4D));
    if (ui->pushView3D->text() != viewName(m_mw->m_pathViewMode3D))
        bad << QStringLiteral("vista 3D: tasto \"%1\", membro %2").arg(ui->pushView3D->text(), viewName(m_mw->m_pathViewMode3D));
    if (sv.pathMode4D != int(m_mw->m_pathViewMode4D) || sv.pathMode3D != int(m_mw->m_pathViewMode3D))
        bad << QStringLiteral("viste: il Save scriverebbe %1/%2").arg(sv.pathMode4D).arg(sv.pathMode3D);

    // VELOCITA' dei path: lo slider e' l'unica copia, il Save lo scrive.
    if (sv.speedPath3D != ui->speed3DSlider->value() || sv.speedPath4D != (keep4D ? ui->speed4DSlider->value() : 0))
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
    if (running != QLatin1String("none") && m_mw->m_lastCameraMotion != running)
        bad << QStringLiteral("gira %1 ma l'ultimo moto avviato e' \"%2\"").arg(running, m_mw->m_lastCameraMotion);
    if (running != QLatin1String("none") && sv.activeMotion != running)
        bad << QStringLiteral("gira %1 ma il Save scriverebbe activeMotion \"%2\"").arg(running, sv.activeMotion);
    if (!expectRunning.isNull() && running != expectRunning)
        bad << QStringLiteral("in corsa: %1, atteso %2").arg(running, expectRunning);

    // FOV.
    if (qAbs(m_mw->m_fov3D - ui->fovSliderMain->value()) > 0.5f || qAbs(m_mw->m_fov4D - ui->fovSliderMain->value()) > 0.5f)
        bad << QStringLiteral("FOV: slider %1, membri %2/%3").arg(ui->fovSliderMain->value()).arg(m_mw->m_fov3D).arg(m_mw->m_fov4D);
    if (qAbs(gl->cameraFov() - ui->fovSliderMain->value()) > 0.5f)
        bad << QStringLiteral("FOV: slider %1, proiezione del motore %2").arg(ui->fovSliderMain->value()).arg(gl->cameraFov());
    if (qAbs(sv.fov3D - m_mw->m_fov3D) > 0.01f || qAbs(sv.fov4D - m_mw->m_fov4D) > 0.01f)
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
    const bool ok = (s == scene && t == texture && a == sound);
    check(ok, QStringLiteral("%1 -> da salvare: %2%3")
                  .arg(step, names(scene, texture, sound),
                       ok ? QString() : QStringLiteral(" (l'app dice: %1; %2)")
                                            .arg(names(s, t, a), m_mw->unsavedKeys().join(QStringLiteral(", ")).left(300))));
}

void ScenarioTest::checkMotionDefaults(const QString &step)
{
    Ui::MainWindow *ui = m_mw->ui;
    GLWidget *gl = ui->glWidget;
    const LibraryItem sv = captureSave();
    QStringList bad;
    if ((m_mw->pathTimer && m_mw->pathTimer->isActive()) || (m_mw->pathTimer3D && m_mw->pathTimer3D->isActive())
        || gl->isAnimating())
        bad << QStringLiteral("un moto camera gira ancora");
    for (QLineEdit *l : { ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P, ui->lineAlpha_P, ui->lineBeta_P,
                          ui->lineGamma_P, ui->lineX_P3D, ui->lineY_P3D, ui->lineZ_P3D, ui->lineR_P3D })
        if (!l->text().trimmed().isEmpty()) { bad << QStringLiteral("campo path \"%1\" non vuoto").arg(l->objectName()); break; }
    if (m_mw->m_pathViewMode4D != MainWindow::ModeTangential || m_mw->m_pathViewMode3D != MainWindow::ModeTangential)
        bad << QStringLiteral("vista dei path rimasta Center (4D %1, 3D %2)")
                   .arg(int(m_mw->m_pathViewMode4D)).arg(int(m_mw->m_pathViewMode3D));
    if (ui->speed3DSlider->value() != 10 || ui->speed4DSlider->value() != 10)
        bad << QStringLiteral("velocita' dei path rimaste %1/%2").arg(ui->speed3DSlider->value()).arg(ui->speed4DSlider->value());
    const float speeds[] = { gl->getPrecessionSpeed(), gl->getNutationSpeed(), gl->getSpinSpeed(),
                             gl->getOmegaSpeed(), gl->getPhiSpeed(), gl->getPsiSpeed() };
    for (float v : speeds)
        if (qAbs(v) > 1e-4f) { bad << QStringLiteral("velocita' di rotazione rimaste nel motore"); break; }
    if (ui->fovSliderMain->value() != 45)
        bad << QStringLiteral("FOV rimasto %1").arg(ui->fovSliderMain->value());
    if (!m_mw->m_lastCameraMotion.isEmpty())
        bad << QStringLiteral("ultimo moto avviato rimasto \"%1\"").arg(m_mw->m_lastCameraMotion);
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
        const bool tabCS = ui->subTabImplicit->currentIndex() == 1;
        const QString active = tabCS ? fCS : f3D;
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
        // minimo >= massimo: nessun taglio), e il Save scrive quei valori.
        struct S { const char *name; QLineEdit *lo; QLineEdit *hi; float eLo, eHi, sLo, sHi; };
        const QVector3D mn = gl->spaceRangeMin(), mx = gl->spaceRangeMax();
        const QList<S> sp = {
            { "x", ui->lineXMin, ui->lineXMax, mn.x(), mx.x(), sv.xMin, sv.xMax },
            { "y", ui->lineYMin, ui->lineYMax, mn.y(), mx.y(), sv.yMin, sv.yMax },
            { "z", ui->lineZMin, ui->lineZMax, mn.z(), mx.z(), sv.zMin, sv.zMax } };
        for (const S &a : sp) {
            auto val = [this](QLineEdit *e, float def) {
                return e->text().trimmed().isEmpty() ? def : m_mw->parseMath(e->text());
            };
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
            // Lo STATO (m_eq) e' il testo del campo: il campo ne e' l'editor.
            if (m_mw->m_eq.*(f.member) != field)
                bad << QStringLiteral("%1: lo stato ha %2, il campo %3")
                           .arg(QString::fromLatin1(f.name), brief(m_mw->m_eq.*(f.member)), brief(field));
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
        if (!pendingEdit && m_mw->m_surfaceScriptApplied != m_mw->m_surfaceScriptText)
            bad << QStringLiteral("script: l'applicato e' diverso dallo scritto (%1 / %2)")
                       .arg(brief(m_mw->m_surfaceScriptApplied), brief(m_mw->m_surfaceScriptText));
    }

    if (!scriptEditorProblem().isEmpty()) bad << scriptEditorProblem();
    if (!rmFieldsProblem().isEmpty()) bad << rmFieldsProblem();

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
    // COSTANTE PIU' FINE DEL PASSO DELLO SLIDER (0.01): la fonte e' il campo.
    // Chi rileggeva lo slider vedeva 0.005 come 0 -- il fattore conforme del
    // flusso geodetico "200*A" risultava nullo e il Run dava errore.
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Costanti: valore piu' fine del passo dello slider =="));
    if (loadSurface(QStringLiteral("surfaces/Parametric/Geodesic Flow/R^3/Helicoid.json"))) {
        ui->lineConform->setPlainText(QStringLiteral("200*A"));  wait(400);
        setConstantByField(QStringLiteral("A"), QStringLiteral("0.005"));
        checkConstants(QStringLiteral("fattore conforme 200*A, A = 0.005 dal campo"), KV{ { "A", 0.005 } });
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
        if (!ui->radioSurface->isChecked()) bad << QStringLiteral("bersaglio ancora su Background");
        if (ui->chkBoxTexture->text() != QLatin1String("Texture"))
            bad << QStringLiteral("etichetta del checkbox \"%1\"").arg(ui->chkBoxTexture->text());
        if (ui->chkBoxTexture->isChecked()) bad << QStringLiteral("checkbox acceso");
        if (m_mw->m_surfaceTextureState) bad << QStringLiteral("intenzione accesa");
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
        if (m_mw->property("isTextureModified").toBool())
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
    // Reset della vista: riporta anche il FOV al default, e lo slider lo segue
    // (il motore tornava a 45 con lo slider fermo sul valore del record). Su un
    // record col FOV diverso dal default e' quindi un cambio della scena.
    if (loadRecord(QString::fromLatin1(kMultiMeshRecord))) {
        const int fov0 = ui->fovSliderMain->value();
        m_mw->onResetViewClicked();  wait(300);
        check(ui->fovSliderMain->value() == 45 && qAbs(ui->glWidget->cameraFov() - 45.0f) < 0.01f,
              QStringLiteral("Reset della vista -> FOV 45 sullo slider e nel motore (slider %1, motore %2)")
                  .arg(ui->fovSliderMain->value()).arg(ui->glWidget->cameraFov()));
        checkDirty(QStringLiteral("Reset della vista (FOV del record %1)").arg(fov0), fov0 != 45, false, false);
        checkMotion(QStringLiteral("dopo il Reset della vista"));
    }
    if (loadRecord(kPath3D)) {
        const int fov0 = ui->fovSliderMain->value();
        m_mw->onResetViewClicked();  wait(300);
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

    runScriptDockScenarios();

    finish();
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
        check(gl->getEngine()->isScriptModeActive() && m_mw->m_surfaceScriptApplied == s1,
              QStringLiteral("script con la costante A eseguito -> a schermo"));
        type(s2);
        ui->lineA->setText(QStringLiteral("0.9"));
        pressEnter(ui->lineA);
        check(gl->getEngine()->getScriptCodeGLSL() == glsl1 && m_mw->m_surfaceScriptApplied == s1,
              QStringLiteral("ritocco in sospeso, Invio su una costante -> a schermo resta lo script di prima"));
        setScriptMode(MainWindow::ScriptModeSurface);
        check(shows(s2) && runOn(), QStringLiteral("ritocco in sospeso, Invio su una costante -> il ritocco aspetta ancora il Run"));
        checkEquations(QStringLiteral("ritocco ancora in sospeso"), /*pendingEdit=*/true);
        if (m_mw->m_btnStart) { m_mw->m_btnStart->click();  wait(800); }
        check(gl->getEngine()->getScriptCodeGLSL() != glsl1 && m_mw->m_surfaceScriptApplied == s2,
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
        check(graphicsOf(m_mw->m_surfaceTextureCode, true) == kStripes4,
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
        check(graphicsOf(m_mw->m_bgTextureCode, true) == bg1, QStringLiteral("sfondo scritto ed eseguito -> applicato"));
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
        check(graphicsOf(m_mw->m_bgTextureCode, true) == bg1,
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
