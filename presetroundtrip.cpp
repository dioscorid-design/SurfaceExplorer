#include "presetroundtrip.h"

#include <random>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "presetserializer.h"
#include "librarymanager.h"
#include "audiocontroller.h"
#include "inputvalidator.h"
#include "shadercompilelog.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

// Una differenza fra due JSON, su una chiave FOGLIA ("texture/col1",
// "meshParts[0]/texZoom") o sul sotto-albero intero se manca da una parte.
struct Diff {
    enum Kind { Changed, Lost, Added };
    QString key;
    Kind kind;
    QString before;     // valore nel riferimento (file originale / passaggio A)
    QString after;      // valore ricostruito
    bool whitespaceOnly = false;
};

QString kindName(Diff::Kind k)
{
    switch (k) {
    case Diff::Changed: return QStringLiteral("cambiata");
    case Diff::Lost:    return QStringLiteral("persa");
    case Diff::Added:   return QStringLiteral("aggiunta");
    }
    return {};
}

QString shortValue(const QJsonValue &v)
{
    QString s;
    if (v.isString()) {
        s = QLatin1Char('"') + v.toString() + QLatin1Char('"');
    } else if (v.isObject()) {
        s = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    } else if (v.isArray()) {
        s = QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    } else if (v.isDouble()) {
        s = QString::number(v.toDouble(), 'g', 10);
    } else if (v.isBool()) {
        s = v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    } else if (v.isNull()) {
        s = QStringLiteral("null");
    }
    s.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    if (s.size() > 110) s = s.left(107) + QStringLiteral("...");
    return s;
}

// I numeri del motore sono float: 0.4 torna 0.4000000059604645. Tolleranza
// relativa ampia abbastanza per il giro float -> double, stretta abbastanza da
// vedere un valore troncato a 2 decimali.
bool sameNumber(double a, double b)
{
    return std::fabs(a - b) <= 1e-5 * std::max({1.0, std::fabs(a), std::fabs(b)});
}

void diffValues(const QString &key, const QJsonValue &ref, const QJsonValue &got,
                QList<Diff> &out)
{
    if (ref.isUndefined() && got.isUndefined()) return;
    if (ref.isUndefined()) { out.append({key, Diff::Added, QString(), shortValue(got)}); return; }
    if (got.isUndefined()) { out.append({key, Diff::Lost, shortValue(ref), QString()}); return; }

    if (ref.isObject() && got.isObject()) {
        const QJsonObject a = ref.toObject(), b = got.toObject();
        QStringList keys = a.keys();
        for (const QString &k : b.keys())
            if (!a.contains(k)) keys.append(k);
        for (const QString &k : keys)
            diffValues(key.isEmpty() ? k : key + QLatin1Char('/') + k, a.value(k), b.value(k), out);
        return;
    }
    if (ref.isArray() && got.isArray()) {
        const QJsonArray a = ref.toArray(), b = got.toArray();
        for (int i = 0; i < std::max(a.size(), b.size()); ++i) {
            diffValues(key + QStringLiteral("[%1]").arg(i),
                       i < a.size() ? a.at(i) : QJsonValue(QJsonValue::Undefined),
                       i < b.size() ? b.at(i) : QJsonValue(QJsonValue::Undefined), out);
        }
        return;
    }
    if (ref.isDouble() && got.isDouble()) {
        if (!sameNumber(ref.toDouble(), got.toDouble()))
            out.append({key, Diff::Changed, shortValue(ref), shortValue(got)});
        return;
    }
    if (ref.isString() && got.isString()) {
        if (ref.toString() != got.toString()) {
            Diff d{key, Diff::Changed, shortValue(ref), shortValue(got)};
            d.whitespaceOnly = ref.toString().simplified() == got.toString().simplified();
            out.append(d);
        }
        return;
    }
    if (ref != got)
        out.append({key, Diff::Changed, shortValue(ref), shortValue(got)});
}

QList<Diff> diffJson(const QJsonObject &ref, const QJsonObject &got, const QSet<QString> &moving)
{
    QList<Diff> all;
    diffValues(QString(), ref, got, all);
    QList<Diff> out;
    for (const Diff &d : all)
        if (!moving.contains(d.key)) out.append(d);
    return out;
}

QJsonObject readJsonFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeJsonFile(const QString &path, const QJsonObject &o)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson());
}

// Chiavi che il Save non riscrive perche' nessun codice le legge piu' (funzione
// del bordo rimossa; sceneLocked mai letta). Perderle non cambia la scena.
const QStringList kObsoleteKeys = {
    QStringLiteral("showBorder"),
    QStringLiteral("colors/bordColor"),
    QStringLiteral("background/sceneLocked"),
};

// L'AUDIO che viaggia dentro un codice di texture/sfondo: blocchi
// SOUND_BEGIN..SOUND_END e righe //MUSIC:. audioPieces ne da' i brani in forma
// canonica (senza marcatori, il file per nome: il load risolve i percorsi di
// altre macchine sulla libreria locale); graphicsPart cio' che resta.
const QRegularExpression &soundBlockRe()
{
    static const QRegularExpression re(R"(//\s*SOUND_BEGIN(.*?)//\s*SOUND_END\n?)",
                                       QRegularExpression::DotMatchesEverythingOption
                                           | QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression &soundMarkerRe()
{
    static const QRegularExpression re(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)",
                                       QRegularExpression::MultilineOption
                                           | QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression &musicLineRe()
{
    static const QRegularExpression re(R"(^\s*//MUSIC:\s*(.*)$\n?)", QRegularExpression::MultilineOption);
    return re;
}

QString graphicsPart(QString code)
{
    while (code.contains(soundBlockRe())) code.remove(soundBlockRe());
    code.remove(soundMarkerRe());
    code.remove(musicLineRe());
    return code.simplified();
}

QStringList audioPieces(const QStringList &codes)
{
    QStringList pieces;
    auto add = [&pieces](const QString &p) { if (!p.isEmpty() && !pieces.contains(p)) pieces << p; };
    for (const QString &code : codes) {
        auto music = musicLineRe().globalMatch(code);
        while (music.hasNext())
            add(QStringLiteral("file:") + QFileInfo(music.next().captured(1).trimmed()).fileName());
        auto block = soundBlockRe().globalMatch(code);
        while (block.hasNext()) {
            QString inner = block.next().captured(1);
            inner.remove(soundMarkerRe());
            add(inner.simplified());
        }
    }
    pieces.sort();
    return pieces;
}

QString argValue(const QStringList &args, const QString &name)
{
    const int i = args.indexOf(name);
    return (i >= 0 && i + 1 < args.size()) ? args.at(i + 1) : QString();
}

} // namespace

bool PresetRoundTrip::requested(const QStringList &args)
{
    return args.contains(QStringLiteral("--roundtrip-test"));
}

void PresetRoundTrip::start(MainWindow *mw, const QStringList &args)
{
    const int i = args.indexOf(QStringLiteral("--roundtrip-test"));
    const QString root = args.value(i + 1);
    const QString out  = args.value(i + 2);
    if (root.isEmpty() || out.isEmpty() || !QDir(root).exists()) {
        qCritical("uso: SurfaceExplorer --roundtrip-test <radice preset> <cartella uscita> "
                  "[--filter <testo>] [--settle <ms>] [--single-pass] [--no-shuffle] "
                  "[--shuffle-seed <n>]");
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(2); });
        return;
    }
    bool ok = false;
    int settle = argValue(args, QStringLiteral("--settle")).toInt(&ok);
    if (!ok || settle <= 0) settle = 600;

    auto *t = new PresetRoundTrip(mw, QDir(root).absolutePath(), QDir(out).absolutePath(),
                                  argValue(args, QStringLiteral("--filter")), settle,
                                  args.contains(QStringLiteral("--single-pass")),
                                  args.contains(QStringLiteral("--data-only")));
    if (args.contains(QStringLiteral("--no-shuffle"))) t->m_shufflePass = false;
    const uint seed = argValue(args, QStringLiteral("--shuffle-seed")).toUInt(&ok);
    if (ok) t->m_shuffleSeed = seed;
    // Si parte a finestra aperta e avvio concluso (superficie di default,
    // lettura della libreria): il primo preset non deve contendere con quelli.
    QTimer::singleShot(2000, t, &PresetRoundTrip::run);
}

PresetRoundTrip::PresetRoundTrip(MainWindow *mw, const QString &root, const QString &outDir,
                                 const QString &filter, int settleMs, bool singlePass,
                                 bool dataOnly)
    : QObject(mw), m_mw(mw), m_root(root), m_outDir(outDir), m_filter(filter),
      m_settleMs(settleMs), m_singlePass(singlePass || dataOnly), m_dataOnly(dataOnly)
{
    QDir().mkpath(m_outDir);
    m_logFile.setFileName(m_outDir + QStringLiteral("/progress.log"));
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        m_logStream.setDevice(&m_logFile);

    // I popup modali (immagine o suono mancante, errori di shader) aprono un
    // exec() che fermerebbe il test: si annotano e si chiudono. Anche quelli
    // dell'avvio, prima che il test parta.
    m_modalWatcher = new QTimer(this);
    connect(m_modalWatcher, &QTimer::timeout, this, &PresetRoundTrip::closeModalDialogs);
    m_modalWatcher->start(100);

    // Gli errori di compilazione degli shader non aprono sempre un popup: si
    // leggono dal log (vedi shadercompilelog.h).
    ShaderCompileLog::install();
}

void PresetRoundTrip::log(const QString &line)
{
    // Riga per riga e subito su disco: se un preset fa cadere l'app, l'ultima
    // riga del log dice quale.
    if (m_logStream.device()) {
        m_logStream << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss "))
                    << line << '\n';
        m_logStream.flush();
    }
    qInfo().noquote() << "[roundtrip]" << line;
}

void PresetRoundTrip::closeModalDialogs()
{
    QWidget *w = QApplication::activeModalWidget();
    if (!w) return;
    // Solo i popup di ERRORE (icona Critical: limiti, shader, equazioni)
    // contano come difetto del caricamento. Gli avvisi (immagine o suono
    // mancante, costanti condivise fra superficie e texture) sono voluti e si
    // elencano soltanto.
    QString desc = QStringLiteral("[avviso] ") + w->windowTitle();
    if (auto *mb = qobject_cast<QMessageBox *>(w)) {
        if (mb->icon() == QMessageBox::Critical)
            desc = QStringLiteral("[errore] ") + w->windowTitle();
        desc += QStringLiteral(": ") + mb->text();
        if (!mb->informativeText().isEmpty())
            desc += QStringLiteral(" / ") + mb->informativeText();
    }
    desc = desc.simplified();
    if (m_currentDialogs) m_currentDialogs->append(desc);
    else log(QStringLiteral("popup fuori da un caricamento: ") + desc);

    // Reject = Annulla: nessun popup deve poter scrivere o cambiare scelta.
    if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
    else w->close();
}

void PresetRoundTrip::wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void PresetRoundTrip::collect()
{
    const struct { const char *dir; bool isRecord; } branches[] = {
        { "surfaces", false }, { "records", true },
    };
    for (const auto &b : branches) {
        const QString base = m_root + QLatin1Char('/') + QLatin1String(b.dir);
        QStringList files;
        QDirIterator it(base, { QStringLiteral("*.json") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.next());
        files.sort(Qt::CaseInsensitive);
        for (const QString &f : files) {
            const QString rel = QDir(m_root).relativeFilePath(f);
            // Piu' filtri separati da '|': basta che uno combaci. Serve a
            // riprodurre una sequenza precisa ("Kerr|Wormhole").
            if (!m_filter.isEmpty()) {
                bool match = false;
                for (const QString &f : m_filter.split(QLatin1Char('|'), Qt::SkipEmptyParts))
                    if (rel.contains(f, Qt::CaseInsensitive)) match = true;
                if (!match) continue;
            }
            m_entries.append({ f, rel, b.isRecord });
        }
    }
}

QJsonObject PresetRoundTrip::captureJson(const Entry &e)
{
    // Stesso nome che scriverebbe un Save sopra lo stesso file.
    const QString name = QFileInfo(e.path).baseName();
    PresetSerializer *ser = m_mw->m_presetSerializer;
    if (!e.isRecord) return ser->buildSurfaceJson(name);

    // Stesse letture di saveMotion per "activeMotion".
    PresetSerializer::MotionRunState run;
    run.rotating = m_mw->ui->glWidget->isAnimating();
    run.path4D   = m_mw->pathRunning(CameraPaths::Path4D);
    run.path3D   = m_mw->pathRunning(CameraPaths::Path3D);
    return ser->buildMotionJson(name, run, /*includeSound*/ true);
}

PresetRoundTrip::Capture PresetRoundTrip::loadAndCapture(const Entry &e)
{
    // IL WATCHDOG DELLA GPU non e' un difetto del preset. A macchina carica
    // (altri programmi, o ore di test di fila) puo' fermare l'animazione
    // durante il load e portare la trasparenza a opaco: la scena catturata non
    // e' piu' quella del preset, e il Save risultava "diverso" e "dipendente
    // dall'ordine" (colors/alpha 0.61 -> 1 su Glass 3-Torus) senza che il
    // codice c'entrasse -- lo stesso binario, pochi minuti dopo, dava il giro
    // pulito. Come nel test degli scenari, il load si ripete; quante volte e'
    // successo lo dice il report.
    Capture c;
    for (int attempt = 0; attempt < 3; ++attempt) {
        c = loadAndCaptureOnce(e);
        bool watchdog = false;
        for (const QString &d : c.dialogs)
            if (d.contains(QLatin1String("slowing down"))) watchdog = true;
        if (!watchdog) break;
        ++m_watchdogReloads;
        log(QStringLiteral("watchdog della GPU durante il load di %1: %2")
                .arg(e.rel, attempt < 2 ? QStringLiteral("ricaricato") : QStringLiteral("terzo tentativo, lo tengo")));
        wait(1500);
    }
    return c;
}

PresetRoundTrip::Capture PresetRoundTrip::loadAndCaptureOnce(const Entry &e)
{
    Capture c;
    m_currentDialogs = &c.dialogs;
    const quint64 errorsBefore = InputValidator::errorCount();

    // Stesso parser dell'albero della libreria (LibraryManager::loadFromDirectory).
    LibraryManager lm;
    const LibraryItem item = lm.parseJson(e.path, e.isRecord ? LibraryType::Motion
                                                             : LibraryType::Surface);
    if (item.name.isEmpty()) {
        c.dialogs.append(QStringLiteral("[errore] NON CARICABILE: la libreria non lo mostrerebbe"));
        m_currentDialogs = nullptr;
        return c;
    }

    // Stesse funzioni del click nell'albero (onExampleItemClicked), senza la
    // conferma "vuoi salvare?" e la selezione degli alberi, che non toccano la scena.
    if (e.isRecord) m_mw->applyMotionExample(item);
    else            m_mw->applySurfaceExample(item);

    wait(m_settleMs);
    c.json = captureJson(e);
    c.sceneDiff = withoutDerivations(diffScene(m_mw->sceneFromItem(item, e.isRecord), m_mw->m_scene),
                                     m_mw);
    const QPair<QString, QLineEdit *> constantFields[] = {
        { QStringLiteral("A"), m_mw->ui->lineA }, { QStringLiteral("B"), m_mw->ui->lineB },
        { QStringLiteral("C"), m_mw->ui->lineC }, { QStringLiteral("D"), m_mw->ui->lineD },
        { QStringLiteral("E"), m_mw->ui->lineE }, { QStringLiteral("F"), m_mw->ui->lineF },
        { QStringLiteral("S"), m_mw->ui->lineS },
    };
    for (const auto &f : constantFields)
        if (!f.second->isEnabled()) c.unusedConstants.insert(f.first);
    // Campi limite VUOTI: l'app li svuota quando la variabile non serve (u, v o
    // w che le equazioni non usano, composizione, Ray Marching), e il Save
    // scrive 0 al posto del valore -- ormai senza significato -- del file.
    {
        Ui::MainWindow *ui = m_mw->ui;
        const QList<QPair<QString, QLineEdit *>> limits = {
            { QStringLiteral("uMin"), ui->uMinEdit }, { QStringLiteral("uMax"), ui->uMaxEdit },
            { QStringLiteral("vMin"), ui->vMinEdit }, { QStringLiteral("vMax"), ui->vMaxEdit },
            { QStringLiteral("wMin"), ui->wMinEdit }, { QStringLiteral("wMax"), ui->wMaxEdit } };
        for (const auto &l : limits)
            if (l.second->text().trimmed().isEmpty()) c.emptyLimits.insert(l.first);
    }
    wait(250);
    const QJsonObject later = captureJson(e);
    for (const Diff &d : diffJson(c.json, later, {}))
        c.moving.insert(d.key);
    // A caricamento finito e assestato la scena dev'essere PULITA.
    c.unsavedAfterLoad = m_mw->unsavedKeys();

    // Il suono di un record ripartirebbe a ogni caricamento: si ferma dopo la
    // cattura (il JSON lo porta comunque, includeSound = true).
    if (m_mw->m_audioController) m_mw->m_audioController->stopAll();

    c.errors = InputValidator::errorCount() - errorsBefore;
    // Dall'ultimo caricamento a qui: cio' che un load lascia in sospeso
    // (Run differito) arriva entro l'assestamento, cioe' qui.
    for (const ShaderCompileLog::Entry &s : ShaderCompileLog::take()) {
        if (s.counts) c.shaderErrors.append(s.text);
        else          c.shaderExcused.append(QStringLiteral("%1  [non conta: %2]").arg(s.text, s.excuse));
    }
    m_currentDialogs = nullptr;
    return c;
}

QStringList PresetRoundTrip::withoutDerivations(const QStringList &diff, MainWindow *mw)
{
    Ui::MainWindow *ui = mw->ui;
    const QHash<QString, QWidget *> fieldOf = {
        { QStringLiteral("constants.a"), ui->lineA }, { QStringLiteral("constants.b"), ui->lineB },
        { QStringLiteral("constants.c"), ui->lineC }, { QStringLiteral("constants.d"), ui->lineD },
        { QStringLiteral("constants.e"), ui->lineE }, { QStringLiteral("constants.f"), ui->lineF },
        { QStringLiteral("constants.s"), ui->lineS },
        { QStringLiteral("lim.uMin"), ui->uMinEdit }, { QStringLiteral("lim.uMax"), ui->uMaxEdit },
        { QStringLiteral("lim.vMin"), ui->vMinEdit }, { QStringLiteral("lim.vMax"), ui->vMaxEdit },
        { QStringLiteral("lim.wMin"), ui->wMinEdit }, { QStringLiteral("lim.wMax"), ui->wMaxEdit } };
    QStringList kept;
    for (const QString &line : diff) {
        const QStringList f = line.split(QLatin1Char('|'));
        const QString key = f.value(0), got = f.value(2);
        QWidget *w = fieldOf.value(key);
        const bool off = w && !w->isEnabled();
        if (key.startsWith(QLatin1String("constants.")) && off
            && got == (key == QLatin1String("constants.s") ? QLatin1String("0") : QLatin1String("1")))
            continue;
        if (key.startsWith(QLatin1String("lim.")) && off && got.isEmpty()) continue;
        if (key == QLatin1String("lastCameraMotion") && f.value(1).isEmpty()) continue;
        kept.append(line);
    }
    return kept;
}

QStringList PresetRoundTrip::diffScene(const MainWindow::SceneState &want, const MainWindow::SceneState &got)
{
    QStringList out;
    auto cmp = [&out](const char *name, const QString &w, const QString &g) {
        if (w != g) out.append(QStringLiteral("%1|%2|%3").arg(QString::fromLatin1(name), w.left(50), g.left(50)));
    };
    auto cmpInt = [&cmp](const char *name, int w, int g) { cmp(name, QString::number(w), QString::number(g)); };
#define SE_CMP(path) cmp(#path, want.path, got.path)
    SE_CMP(eq.x); SE_CMP(eq.y); SE_CMP(eq.z); SE_CMP(eq.p);
    SE_CMP(eq.u); SE_CMP(eq.v); SE_CMP(eq.w);
    SE_CMP(eq.explicitU); SE_CMP(eq.explicitV); SE_CMP(eq.explicitW);
    SE_CMP(eq.geoU); SE_CMP(eq.geoV); SE_CMP(eq.geoW);
    SE_CMP(eq.geoDU); SE_CMP(eq.geoDV); SE_CMP(eq.geoDW); SE_CMP(eq.conform);
    SE_CMP(rm.equation); SE_CMP(rm.crossSection); SE_CMP(rm.displacement);
    SE_CMP(constants.a); SE_CMP(constants.b); SE_CMP(constants.c); SE_CMP(constants.d);
    SE_CMP(constants.e); SE_CMP(constants.f); SE_CMP(constants.s);
    SE_CMP(lim.uMin); SE_CMP(lim.uMax); SE_CMP(lim.vMin); SE_CMP(lim.vMax);
    SE_CMP(lim.wMin); SE_CMP(lim.wMax); SE_CMP(lim.xMin); SE_CMP(lim.xMax);
    SE_CMP(lim.yMin); SE_CMP(lim.yMax); SE_CMP(lim.zMin); SE_CMP(lim.zMax);
    SE_CMP(path.x); SE_CMP(path.y); SE_CMP(path.z); SE_CMP(path.p);
    SE_CMP(path.alpha); SE_CMP(path.beta); SE_CMP(path.gamma);
    SE_CMP(path.x3D); SE_CMP(path.y3D); SE_CMP(path.z3D); SE_CMP(path.roll3D);
    SE_CMP(lastCameraMotion);
    SE_CMP(surfaceScriptText); SE_CMP(surfaceScriptApplied);
    SE_CMP(surfaceTextureScriptText); SE_CMP(surfaceTextureCode); SE_CMP(rm.texture);
    SE_CMP(bgTextureScriptText); SE_CMP(bgTextureCode); SE_CMP(soundScriptText);
    SE_CMP(textureLibName); SE_CMP(bgTextureLibName); SE_CMP(soundLibName);
    SE_CMP(tube.x); SE_CMP(tube.y); SE_CMP(tube.z); SE_CMP(tube.p); SE_CMP(tube.thickness);
#undef SE_CMP
    cmpInt("surfaceTextureState", want.surfaceTextureState, got.surfaceTextureState);
    // Domini delle costanti, in ordine di lettera.
    auto domains = [](const MainWindow::SceneState &s) {
        QStringList out;
        for (const QString &k : s.discreteConsts.keys())
            out << QStringLiteral("%1:int(%2,%3)").arg(k).arg(s.discreteConsts.value(k).lo)
                                                  .arg(s.discreteConsts.value(k).hi);
        for (const QString &k : s.minConsts.keys())
            out << QStringLiteral("%1:min(%2)").arg(k).arg(s.minConsts.value(k));
        out.sort();
        return out.join(QLatin1Char(' '));
    };
    cmp("constantDomains", domains(want), domains(got));
    cmp("bgColor", want.bgColor.name(), got.bgColor.name());
    cmp("bgTexColor1", want.bgTexColor1.name(), got.bgTexColor1.name());
    cmp("bgTexColor2", want.bgTexColor2.name(), got.bgTexColor2.name());
    cmp("fov", QString::number(want.fov), QString::number(got.fov));
    cmpInt("steps", want.steps, got.steps);
    cmpInt("implicitMode", want.implicitMode, got.implicitMode);
    cmpInt("crossSectionTab", want.crossSectionTab, got.crossSectionTab);
    cmpInt("tubesTab", want.tubesTab, got.tubesTab);
    cmpInt("implicitShell", want.implicitShell, got.implicitShell);
    cmpInt("renderMode", want.renderMode, got.renderMode);
    cmpInt("pathViewMode4D", want.pathViewMode4D, got.pathViewMode4D);
    cmpInt("pathViewMode3D", want.pathViewMode3D, got.pathViewMode3D);
    cmpInt("pathSpeed3D", want.pathSpeed3D, got.pathSpeed3D);
    cmpInt("pathSpeed4D", want.pathSpeed4D, got.pathSpeed4D);
    return out;
}

void PresetRoundTrip::run()
{
    collect();
    log(QStringLiteral("radice %1, %2 preset, settle %3 ms%4")
            .arg(m_root).arg(m_entries.size()).arg(m_settleMs)
            .arg(m_singlePass ? QStringLiteral(", un solo passaggio") : QString()));

    const int n = m_entries.size();
    QString previous;
    for (int i = 0; i < n; ++i) {
        const Entry &e = m_entries.at(i);
        log(QStringLiteral("A %1/%2 %3").arg(i + 1).arg(n).arg(e.rel));
        Capture c;
        if (m_dataOnly) {
            // Solo dati: lo stesso parser dell'albero e la stessa scrittura del
            // Save, senza passare dall'app.
            LibraryManager lm;
            const LibraryItem item = lm.parseJson(e.path, e.isRecord ? LibraryType::Motion
                                                                     : LibraryType::Surface);
            if (!item.name.isEmpty()) c.json = LibraryManager::toJson(item);
            else c.dialogs.append(QStringLiteral("[errore] NON CARICABILE: la libreria non lo mostrerebbe"));
        } else {
            c = loadAndCapture(e);
        }
        c.previous = previous;
        previous = e.rel;
        writeJsonFile(m_outDir + QStringLiteral("/passA/") + e.rel, c.json);
        m_passA.insert(e.rel, c);
    }
    if (!m_singlePass) {
        for (int i = n - 1; i >= 0; --i) {
            const Entry &e = m_entries.at(i);
            log(QStringLiteral("B %1/%2 %3").arg(n - i).arg(n).arg(e.rel));
            Capture c = loadAndCapture(e);
            c.previous = previous;
            previous = e.rel;
            writeJsonFile(m_outDir + QStringLiteral("/passB/") + e.rel, c.json);
            m_passB.insert(e.rel, c);
        }
    }
    if (!m_singlePass && m_shufflePass) {
        // PASSAGGIO C, in ordine RIMESCOLATO. A e B provano solo le coppie di
        // preset vicine in ordine alfabetico, nei due versi: lo stato che
        // sopravvive fra due preset mai adiacenti resta invisibile (visto col
        // Cross Section ereditato dai record da script, uscito solo con un
        // filtro che li aveva messi vicini). Il seme e' fisso: l'ordine e'
        // sempre lo stesso, quindi un difetto trovato si ritrova; --shuffle-seed
        // ne prova un altro. Fisher-Yates a mano: std::shuffle non garantisce
        // lo stesso risultato fra librerie diverse.
        QList<int> order;
        for (int i = 0; i < n; ++i) order.append(i);
        std::mt19937 rng(m_shuffleSeed);
        for (int i = n - 1; i > 0; --i)
            order.swapItemsAt(i, int(rng() % uint(i + 1)));
        for (int k = 0; k < n; ++k) {
            const Entry &e = m_entries.at(order.at(k));
            log(QStringLiteral("C %1/%2 %3").arg(k + 1).arg(n).arg(e.rel));
            Capture c = loadAndCapture(e);
            c.previous = previous;
            previous = e.rel;
            writeJsonFile(m_outDir + QStringLiteral("/passC/") + e.rel, c.json);
            m_passC.insert(e.rel, c);
        }
    }
    writeReport();
}

// La chiave e' la foto di un MOTO in corso al Save (path o rotazione)? Allora
// il suo valore dipende dall'istante, non dal preset: non conta ne' contro il
// file ne' fra i due passaggi.
QString PresetRoundTrip::drivenByMotion(const QString &key, const QJsonObject &saved)
{
    // Path in moto al Save: la camera 3D la muove il path a ogni tick (e con
    // il path 4D anche angoli e osservatore 4D), e al reload il path la
    // ricalcola dal primo tick. Il valore nel file e' la foto di un istante:
    // Clifford Labyrinth salvava yaw +90 o -90 a seconda del lato del path.
    const QString motion = saved.value(QStringLiteral("activeMotion")).toString();
    const bool path4D = motion == QLatin1String("path4D");
    if ((path4D || motion == QLatin1String("path3D"))
        && key.startsWith(QLatin1String("camera3D/")))
        return QStringLiteral("camera guidata dal path");
    if (path4D && (key.startsWith(QLatin1String("angles/"))
                   || key == QLatin1String("observer4D")))
        return QStringLiteral("4D guidato dal path");
    // Rotazioni in corso: orientamento e angoli 4D sono la foto di un moto.
    // Il confronto fra due catture a 250 ms non basta a riconoscerlo quando
    // la rotazione e' molto lenta (Hyperbolic Mobius Band).
    if (motion == QLatin1String("rotation")
        && (key.startsWith(QLatin1String("camera3D/rot_")) || key.startsWith(QLatin1String("angles/"))))
        return QStringLiteral("rotazione in corso");
    return QString();
}

QString PresetRoundTrip::excusedBecause(const QString &key, int kind, bool whitespaceOnly,
                                       const QJsonObject &file, const Capture &c)
{
    if (kind == Diff::Added)
        return QStringLiteral("chiave assente nel file");      // formato vecchio
    if (whitespaceOnly)
        return QStringLiteral("solo spazi/a capo");            // il load fa trimmed()
    if (kind == Diff::Lost && kObsoleteKeys.contains(key))
        return QStringLiteral("chiave obsoleta");
    if (key.startsWith(QLatin1String("constants/")) && c.unusedConstants.contains(key.mid(10)))
        return QStringLiteral("costante non usata");
    // Composizione e vincoli di un preset con SCRIPT: il load li svuota (lo
    // script da' la geometria, il motore li azzera), quindi un valore nel file
    // e' un residuo che il Save ripulisce.
    if (!c.json.value(QStringLiteral("scriptCode")).toString().isEmpty()) {
        static const QStringList comp = {
            QStringLiteral("equations/defU"), QStringLiteral("equations/defV"),
            QStringLiteral("equations/defW"), QStringLiteral("equations/explicitU"),
            QStringLiteral("equations/explicitV"), QStringLiteral("equations/explicitW"),
        };
        if (comp.contains(key)) return QStringLiteral("composizione ignorata con uno script");
    }
    {
        const QString moved = drivenByMotion(key, c.json);
        if (!moved.isEmpty()) return moved;
    }
    // Ray Marching fuori dal Cross Section: angoli e velocita' 4D non hanno
    // effetto e il Save li azzera di proposito (vedi keep4DAngles in
    // buildMotionJson).
    {
        const QJsonObject &saved = c.json;
        const bool rm3D = saved.value(QStringLiteral("isImplicitMode")).toBool()
                          && !saved.value(QStringLiteral("implicitUsesCrossSection")).toBool();
        static const QStringList fourD = {
            QStringLiteral("angles/omega"), QStringLiteral("angles/phi"), QStringLiteral("angles/psi"),
            QStringLiteral("speeds/omega"), QStringLiteral("speeds/phi"), QStringLiteral("speeds/psi"),
            QStringLiteral("speeds/path4D"),
        };
        if (rm3D && fourD.contains(key))
            return QStringLiteral("4D azzerato in Ray Marching 3D");
    }
    // Sfondo SPENTO nel file: codice, ancora e messaggio non contano -- lo
    // sfondo spento non ha texture (MainWindow::forgetBackgroundTexture), e un
    // residuo nel file era un difetto del Save vecchio (il percorso
    // dell'immagine sopravviveva allo spegnimento).
    if (!file.value(QStringLiteral("background")).toObject().value(QStringLiteral("enabled")).toBool()) {
        static const QStringList bgTex = {
            QStringLiteral("background/code"), QStringLiteral("background/libName"),
            QStringLiteral("background/hintText"), QStringLiteral("background/hintSeconds"),
        };
        if (bgTex.contains(key)) return QStringLiteral("sfondo spento: niente texture");
    }
    // Il nome del preset lo decide il FILE: il Save scrive il nome del file,
    // qualunque cosa dica la chiave (es. apostrofo tipografico nel "name").
    if (kind == Diff::Changed && key == QLatin1String("name"))
        return QStringLiteral("il nome segue il file");
    // Messaggio VUOTO nel file: il Save omette i messaggi vuoti (e la loro
    // durata), che al load valgono "nessun messaggio" comunque.
    if (kind == Diff::Lost) {
        const QJsonValue v = file.value(key);
        if (v.isString() && v.toString().isEmpty())
            return QStringLiteral("vuoto nel file");
        if (key.endsWith(QLatin1String("Seconds"))) {
            const QString textKey = key.left(key.size() - 7) + QStringLiteral("Text");
            if (file.value(textKey).toString().isEmpty())
                return QStringLiteral("durata di un messaggio vuoto");
        }
    }
    // discreteConstants con voci che non sono [lo, hi] (es. {"A": true}): il load
    // le scarta (parseDiscreteConstants), quindi non c'e' nulla da riscrivere.
    if (kind == Diff::Lost && key == QLatin1String("discreteConstants")) {
        const QJsonObject disc = file.value(key).toObject();
        bool anyValid = false;
        for (auto it = disc.constBegin(); it != disc.constEnd(); ++it)
            if (it.value().toArray().size() == 2) anyValid = true;
        if (!anyValid) return QStringLiteral("voci malformate, ignorate dal load");
    }
    // Codice di texture o di sfondo che cambia nella sola parte AUDIO: la scena
    // ha un solo brano, che il Save scrive una volta nel codice della texture,
    // coi marcatori ripuliti (i Save vecchi li raddoppiavano, o mettevano lo
    // stesso brano anche nello sfondo) e il percorso del file risolto sulla
    // libreria locale. Conta che la grafica dello slot sia la stessa e che i
    // brani del record siano gli stessi.
    if (kind == Diff::Changed
        && (key == QLatin1String("texture/code") || key == QLatin1String("background/code"))) {
        auto code = [](const QJsonObject &o, const QString &slot) {
            return o.value(slot).toObject().value(QStringLiteral("code")).toString();
        };
        auto pieces = [&code](const QJsonObject &o) {
            return audioPieces({ o.value(QStringLiteral("scriptCode")).toString(),
                                 code(o, QStringLiteral("texture")),
                                 code(o, QStringLiteral("background")) });
        };
        const QString slot = key.section(QLatin1Char('/'), 0, 0);
        if (graphicsPart(code(file, slot)) == graphicsPart(code(c.json, slot))
            && pieces(file) == pieces(c.json))
            return QStringLiteral("solo audio: marcatori o percorso ripuliti");
    }
    // Immagine di una FASCIA (tag //IMG: nel suo script): il file porta il
    // percorso del dispositivo che l'ha salvato, il load lo ritrova per nome
    // nella libreria locale e il Save scrive il percorso trovato -- come gia'
    // fa per l'immagine della superficie. Conta che il file sia lo stesso e
    // che il resto dello script non cambi.
    if (kind == Diff::Changed && key.startsWith(QLatin1String("meshParts["))
        && key.endsWith(QLatin1String("]/texCode"))) {
        const int idx = key.mid(10, key.indexOf(QLatin1Char(']')) - 10).toInt();
        auto codeOf = [idx](const QJsonObject &o) {
            return o.value(QStringLiteral("meshParts")).toArray().at(idx).toObject()
                    .value(QStringLiteral("texCode")).toString();
        };
        static const QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)",
                                              QRegularExpression::MultilineOption);
        const QString was = codeOf(file), now = codeOf(c.json);
        const QString wasImg = QFileInfo(imgRe.match(was).captured(1).trimmed()).fileName();
        const QString nowImg = QFileInfo(imgRe.match(now).captured(1).trimmed()).fileName();
        QString wasRest = was, nowRest = now;
        wasRest.remove(imgRe);
        nowRest.remove(imgRe);
        if (!wasImg.isEmpty() && wasImg == nowImg && wasRest == nowRest)
            return QStringLiteral("immagine della fascia ritrovata nella libreria locale");
    }
    // Fattore conforme VUOTO nel file: vale 1.0 (metrica piatta), che il load
    // scrive nel campo e il Save riporta.
    if (key == QLatin1String("geodesic/conform")
        && file.value(QStringLiteral("geodesic")).toObject().value(QStringLiteral("conform")).toString().trimmed().isEmpty()
        && c.json.value(QStringLiteral("geodesic")).toObject().value(QStringLiteral("conform")).toString().trimmed()
               == QLatin1String("1.0"))
        return QStringLiteral("fattore conforme vuoto = 1.0");
    // Posa 4D PIATTA (tre angoli a 0) su una superficie con la quarta
    // coordinata: il load la sposta di 0.01 per non partire dalla proiezione
    // degenere (anti-glitch di applySurfaceExample), e il Save scrive la posa
    // che si vede.
    if (key.startsWith(QLatin1String("angles/"))) {
        const QJsonObject fa = file.value(QStringLiteral("angles")).toObject();
        // La quarta coordinata: della curva, se la scena e' un tubo.
        const QString eqKey = c.json.contains(QStringLiteral("tube")) ? QStringLiteral("tube")
                                                                       : QStringLiteral("equations");
        const QString p = c.json.value(eqKey).toObject().value(QStringLiteral("p")).toString().trimmed();
        const bool flat = fa.value(QStringLiteral("omega")).toDouble() == 0.0
                          && fa.value(QStringLiteral("phi")).toDouble() == 0.0
                          && fa.value(QStringLiteral("psi")).toDouble() == 0.0;
        const double got = c.json.value(QStringLiteral("angles")).toObject().value(key.mid(7)).toDouble();
        const bool surface4D = !p.isEmpty() && p != QLatin1String("0") && p != QLatin1String("0.0");
        if (flat && surface4D && std::abs(got - 0.01) < 1e-6)
            return QStringLiteral("posa 4D piatta spostata di 0.01 (anti-glitch)");
    }
    // Trasparenza di un preset in WIREFRAME: il wireframe e' opaco (il ramo
    // wireframe di updateRenderState chiama resetTransparency), quindi un'alpha
    // < 1 nel file e' un residuo che il Save riporta a 1.
    if (key == QLatin1String("colors/alpha")
        && c.json.value(QStringLiteral("renderMode")).toInt() == 2
        && c.json.value(QStringLiteral("colors")).toObject().value(QStringLiteral("alpha")).toDouble() == 1.0)
        return QStringLiteral("wireframe: sempre opaco");
    // Limite di una variabile che la scena non usa: campo vuoto, il Save scrive 0.
    if (key.startsWith(QLatin1String("limits/")) && c.emptyLimits.contains(key.mid(7))
        && c.json.value(QStringLiteral("limits")).toObject().value(key.mid(7)).toDouble() == 0.0)
        return QStringLiteral("variabile non usata: campo limite vuoto");
    // Limiti 0/0 nel file = "nessun dominio": vale quello dichiarato dallo script
    // (u_min := ...), che il Save poi scrive. Se invece arrivasse dal preset
    // precedente, lo mostrerebbe il confronto fra i due passaggi.
    if (key.startsWith(QLatin1String("limits/")) && key.size() == 11) {    // limits/uMin
        const QJsonObject lim = file.value(QStringLiteral("limits")).toObject();
        const QString axis = key.mid(7, 1);
        if (lim.value(axis + QStringLiteral("Min")).toDouble() == 0.0
            && lim.value(axis + QStringLiteral("Max")).toDouble() == 0.0)
            return QStringLiteral("limiti 0/0 nel file, dominio dallo script");
    }
    return {};
}

void PresetRoundTrip::writeReport()
{
    struct KeyStat { int count = 0; QStringList examples; };
    QMap<QString, KeyStat> saveByKey, orderByKey, movingByKey;
    QStringList details, popups;
    int identical = 0, saveChanged = 0, orderDependent = 0, notLoadable = 0, withPopups = 0;
    int dirtyAfterLoad = 0;
    QStringList phantom;
    int withShaderErrors = 0;
    QStringList shaderLines;
    QMap<QString, KeyStat> sceneByKey;
    int sceneMismatch = 0;

    auto bump = [](QMap<QString, KeyStat> &m, const QString &key, const QString &rel) {
        KeyStat &s = m[key];
        ++s.count;
        if (s.examples.size() < 3) s.examples.append(rel);
    };
    auto line = [](const Diff &d, const QString &excuse = QString()) {
        QString s = QStringLiteral("    %1 %2").arg(kindName(d.kind), d.key);
        if (d.kind == Diff::Changed) s += QStringLiteral(": %1 -> %2").arg(d.before, d.after);
        else if (d.kind == Diff::Lost) s += QStringLiteral(" (era %1)").arg(d.before);
        else s += QStringLiteral(" = %1").arg(d.after);
        if (!excuse.isEmpty()) s += QStringLiteral("  [non conta: %1]").arg(excuse);
        return s;
    };

    for (const Entry &e : m_entries) {
        const Capture &a = m_passA[e.rel];
        const Capture *bp = m_passB.contains(e.rel) ? &m_passB[e.rel] : nullptr;
        const Capture *cp = m_passC.contains(e.rel) ? &m_passC[e.rel] : nullptr;
        // Scena prevista dal file contro scena dopo il load, in TUTTI i
        // passaggi: una scelta che sopravvive dal preset di prima si vede solo
        // dopo certi preset (sotto-tab e Shell ereditati da un record Ray
        // Marching: solo nel passaggio rimescolato). Il preset conta una volta.
        {
            bool off = false;
            const QPair<QChar, const Capture *> passes[] = {
                { QLatin1Char('A'), &a }, { QLatin1Char('B'), bp }, { QLatin1Char('C'), cp } };
            for (const auto &pc : passes) {
                if (!pc.second) continue;
                for (const QString &d : pc.second->sceneDiff) {
                    off = true;
                    const QStringList f = d.split(QLatin1Char('|'));
                    KeyStat &st = sceneByKey[f.value(0)];
                    ++st.count;
                    if (st.examples.size() < 3)
                        st.examples.append(QStringLiteral("%1 [previsto '%2', reale '%3', %4 dopo %5]")
                                               .arg(QFileInfo(e.rel).completeBaseName(), f.value(1), f.value(2))
                                               .arg(pc.first)
                                               .arg(pc.second->previous.isEmpty()
                                                        ? QStringLiteral("l'avvio")
                                                        : QFileInfo(pc.second->previous).completeBaseName()));
                }
            }
            if (off) ++sceneMismatch;
        }
        // Un popup durante un caricamento e' sempre un difetto (il load si
        // ferma a meta' finche' l'utente non lo chiude): conta, in tutti e due
        // i passaggi, con il predecessore che serve a riprodurlo.
        bool popped = false;
        for (const Capture *c : { &a, bp, cp }) {
            if (!c) continue;
            for (const QString &p : c->dialogs) {
                popups.append(QStringLiteral("%1 (dopo %2): %3")
                                  .arg(e.rel, c->previous.isEmpty() ? QStringLiteral("l'avvio")
                                                                    : c->previous, p));
                if (!p.startsWith(QLatin1String("[avviso]"))) popped = true;
            }
        }
        if (popped) ++withPopups;

        // Shader che non compilano (vedi shadercompilelog.h), in qualunque
        // passaggio: conta il preset una volta, si elencano tutti.
        {
            bool broken = false;
            for (const Capture *c : { &a, bp, cp }) {
                if (!c) continue;
                const QString after = c->previous.isEmpty() ? QStringLiteral("l'avvio") : c->previous;
                for (const QString &s : c->shaderErrors) {
                    shaderLines.append(QStringLiteral("%1 (dopo %2): %3").arg(e.rel, after, s));
                    broken = true;
                }
                for (const QString &s : c->shaderExcused)
                    shaderLines.append(QStringLiteral("%1 (dopo %2): %3").arg(e.rel, after, s));
            }
            if (broken) ++withShaderErrors;
        }
        if (a.json.isEmpty()) { ++notLoadable; continue; }

        // Scena "da salvare" senza alcun gesto, in uno qualunque dei passaggi.
        {
            bool dirty = false;
            for (const Capture *c : { &a, bp, cp }) {
                if (!c || c->unsavedAfterLoad.isEmpty()) continue;
                dirty = true;
                phantom.append(QStringLiteral("%1 (dopo %2): %3")
                                   .arg(e.rel, c->previous.isEmpty() ? QStringLiteral("l'avvio") : c->previous,
                                        c->unsavedAfterLoad.join(QStringLiteral(", ")).left(400)));
            }
            if (dirty) ++dirtyAfterLoad;
        }

        QSet<QString> moving = a.moving;
        const Capture *b = m_passB.contains(e.rel) ? &m_passB[e.rel] : nullptr;
        if (b) moving.unite(b->moving);
        if (cp) moving.unite(cp->moving);
        for (const QString &k : moving) bump(movingByKey, k, e.rel);

        // Non tutto cio' che il Save cambia rispetto al file e' un difetto: chiavi
        // nate dopo il preset, spazi tolti dal load, chiavi obsolete, costanti
        // che nessun codice usa (vedi excusedBecause). Si elencano con la
        // ragione, ma non contano. Fra i due passaggi invece conta tutto: e'
        // stato del preset precedente, mai legittimo.
        const QJsonObject file = readJsonFile(e.path);
        const QList<Diff> save = diffJson(file, a.json, moving);
        // ...tranne le chiavi che sono la foto di un moto in corso: quanti tick
        // passano prima della cattura dipende dal carico della macchina, non dal
        // preset di prima. Visto su Rotations/Torus Knot: un tick di differenza
        // negli angoli 4D fra i due passaggi, in un giro rallentato (e la
        // rotazione era troppo lenta perche' le due catture a 250 ms la vedessero).
        // Contro B (ordine inverso) e contro C (ordine rimescolato).
        auto orderDiffs = [&](const Capture *other) {
            QList<Diff> out;
            if (!other) return out;
            for (const Diff &d : diffJson(a.json, other->json, moving))
                if (drivenByMotion(d.key, a.json).isEmpty()) out.append(d);
            return out;
        };
        const QList<Diff> orderB = orderDiffs(b);
        const QList<Diff> orderC = orderDiffs(cp);
        // Per i totali: una chiave che dipende dall'ordine conta una volta sola.
        QList<Diff> order = orderB;
        for (const Diff &d : orderC) {
            bool seen = false;
            for (const Diff &x : orderB) if (x.key == d.key && x.kind == d.kind) { seen = true; break; }
            if (!seen) order.append(d);
        }

        bool saveDiffers = false;
        QStringList saveLines;
        for (const Diff &d : save) {
            const QString excuse = excusedBecause(d.key, d.kind, d.whitespaceOnly, file, a);
            bump(saveByKey, kindName(d.kind) + QLatin1Char(' ') + d.key
                                + (excuse.isEmpty() ? QString()
                                                    : QStringLiteral("  [non conta: %1]").arg(excuse)),
                 e.rel);
            if (excuse.isEmpty()) saveDiffers = true;
            saveLines.append(line(d, excuse));
        }
        for (const Diff &d : order) bump(orderByKey, kindName(d.kind) + QLatin1Char(' ') + d.key, e.rel);

        if (saveDiffers) ++saveChanged;
        if (!order.isEmpty()) ++orderDependent;
        if (!saveDiffers && order.isEmpty()) ++identical;

        if (save.isEmpty() && order.isEmpty() && a.errors == 0) continue;
        details.append(QStringLiteral("[%1]").arg(e.rel));
        if (a.errors) details.append(QStringLiteral("  errori segnalati al caricamento: %1").arg(a.errors));
        if (!save.isEmpty()) {
            details.append(QStringLiteral("  Save subito dopo il Load:"));
            details.append(saveLines);
        }
        // Col predecessore di ciascun passaggio: e' cio' che serve a riprodurre
        // il caso a mano ("apri X, poi questo").
        auto after = [](const Capture &c) {
            return c.previous.isEmpty() ? QStringLiteral("l'avvio") : c.previous;
        };
        if (!orderB.isEmpty()) {
            details.append(QStringLiteral("  Dipende dal preset caricato prima (A dopo %1, B dopo %2):")
                               .arg(after(a), after(*b)));
            for (const Diff &d : orderB) details.append(line(d));
        }
        if (!orderC.isEmpty()) {
            details.append(QStringLiteral("  Dipende dal preset caricato prima (A dopo %1, C dopo %2):")
                               .arg(after(a), after(*cp)));
            for (const Diff &d : orderC) details.append(line(d));
        }
    }

    auto summary = [](const QMap<QString, KeyStat> &m) {
        QList<QPair<QString, KeyStat>> rows;
        for (auto it = m.cbegin(); it != m.cend(); ++it) rows.append({ it.key(), it.value() });
        std::sort(rows.begin(), rows.end(), [](const auto &x, const auto &y) {
            return x.second.count != y.second.count ? x.second.count > y.second.count
                                                    : x.first < y.first;
        });
        QStringList out;
        for (const auto &r : rows)
            out.append(QStringLiteral("  %1  %2   (es. %3)")
                           .arg(r.second.count, 4).arg(r.first, r.second.examples.join(QStringLiteral(", "))));
        if (out.isEmpty()) out.append(QStringLiteral("  nessuna"));
        return out;
    };

    const int n = m_entries.size();
    QStringList rep;
    rep << (m_dataOnly ? QStringLiteral("TEST DI ANDATA E RITORNO DEI PRESET -- SOLO DATI (parseJson -> toJson)")
                       : QStringLiteral("TEST DI ANDATA E RITORNO DEI PRESET"))
        << QStringLiteral("radice: %1").arg(m_root)
        << QStringLiteral("preset: %1   settle: %2 ms   passaggi: %3%4")
               .arg(n).arg(m_settleMs)
               .arg(m_singlePass ? QStringLiteral("1")
                    : m_passC.isEmpty() ? QStringLiteral("2")
                                        : QStringLiteral("3 (C rimescolato, seme %1)").arg(m_shuffleSeed))
               .arg(m_filter.isEmpty() ? QString() : QStringLiteral("   filtro: ") + m_filter)
        << QString()
        << QStringLiteral("Identici (Save = file, e nessuna dipendenza dal precedente): %1 su %2").arg(identical).arg(n - notLoadable)
        << QStringLiteral("Il Save cambia o perde qualcosa:                             %1").arg(saveChanged)
        << QStringLiteral("Dipendono dal preset caricato prima:                         %1").arg(orderDependent)
        << QStringLiteral("Non caricabili:                                              %1").arg(notLoadable)
        << QStringLiteral("Con un popup di errore al caricamento:                       %1").arg(withPopups)
        << QStringLiteral("Risultano da salvare appena caricati (popup fantasma):       %1").arg(dirtyAfterLoad)
        << QStringLiteral("Con uno shader che non compila:                              %1").arg(withShaderErrors)
        << QStringLiteral("Scena dopo il load diversa da quella del file:              %1").arg(sceneMismatch)
        << QStringLiteral("Ricaricati perche' il watchdog della GPU li aveva fermati:   %1%2").arg(m_watchdogReloads)
               .arg(m_watchdogReloads > 0 ? QStringLiteral("   (macchina carica: non e' un difetto)") : QString())
        << QString()
        << QStringLiteral("== SCENA PREVISTA DAL FILE vs SCENA DOPO IL LOAD, per campo (tutti i passaggi) ==")
        << summary(sceneByKey)
        << QString()
        << QStringLiteral("== DIPENDONO DAL PRESET PRECEDENTE, per chiave (numero di preset) ==")
        << summary(orderByKey)
        << QString()
        << QStringLiteral("== IL SAVE CAMBIA RISPETTO AL FILE, per chiave (le voci [non conta] sono attese) ==")
        << summary(saveByKey)
        << QString()
        << QStringLiteral("== CHIAVI IN MOTO, escluse dal confronto (cambiano fra due catture a 250 ms) ==")
        << summary(movingByKey)
        << QString()
        << QStringLiteral("== POPUP DURANTE IL CARICAMENTO (chiusi dal test) ==");
    if (popups.isEmpty()) rep << QStringLiteral("  nessuno");
    for (const QString &p : popups) rep << QStringLiteral("  ") + p;
    rep << QString() << QStringLiteral("== SHADER CHE NON COMPILANO (dal log; le voci [non conta] sono attese) ==");
    if (shaderLines.isEmpty()) rep << QStringLiteral("  nessuno");
    for (const QString &s : shaderLines) rep << QStringLiteral("  ") + s;
    rep << QString() << QStringLiteral("== DA SALVARE APPENA CARICATI (chiavi che farebbero uscire l'avviso) ==");
    if (phantom.isEmpty()) rep << QStringLiteral("  nessuno");
    for (const QString &p : phantom) rep << QStringLiteral("  ") + p;
    rep << QString() << QStringLiteral("== DETTAGLIO PER PRESET ==") << details;

    QFile f(m_outDir + QStringLiteral("/report.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(rep.join(QLatin1Char('\n')).toUtf8() + '\n');

    const bool pass = (saveChanged == 0 && orderDependent == 0 && notLoadable == 0
                       && withPopups == 0 && dirtyAfterLoad == 0 && withShaderErrors == 0
                       && sceneMismatch == 0);
    log(QStringLiteral("fine: %1 identici su %2, Save diverso %3, dipendenti dall'ordine %4, "
                       "con popup %5, con shader non compilati %6. Report: %7")
            .arg(identical).arg(n - notLoadable).arg(saveChanged).arg(orderDependent)
            .arg(withPopups).arg(withShaderErrors).arg(f.fileName()));
    m_modalWatcher->stop();
    QCoreApplication::exit(pass ? 0 : 1);
}
