#include "presetroundtrip.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "presetserializer.h"
#include "librarymanager.h"
#include "audiocontroller.h"
#include "inputvalidator.h"

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
                  "[--filter <testo>] [--settle <ms>] [--single-pass]");
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
    run.path4D   = m_mw->pathTimer && m_mw->pathTimer->isActive();
    run.path3D   = m_mw->pathTimer3D && m_mw->pathTimer3D->isActive();
    return ser->buildMotionJson(name, run, /*includeSound*/ true);
}

PresetRoundTrip::Capture PresetRoundTrip::loadAndCapture(const Entry &e)
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
    const QPair<QString, QLineEdit *> constantFields[] = {
        { QStringLiteral("A"), m_mw->ui->lineA }, { QStringLiteral("B"), m_mw->ui->lineB },
        { QStringLiteral("C"), m_mw->ui->lineC }, { QStringLiteral("D"), m_mw->ui->lineD },
        { QStringLiteral("E"), m_mw->ui->lineE }, { QStringLiteral("F"), m_mw->ui->lineF },
        { QStringLiteral("S"), m_mw->ui->lineS },
    };
    for (const auto &f : constantFields)
        if (!f.second->isEnabled()) c.unusedConstants.insert(f.first);
    wait(250);
    const QJsonObject later = captureJson(e);
    for (const Diff &d : diffJson(c.json, later, {}))
        c.moving.insert(d.key);

    // Il suono di un record ripartirebbe a ogni caricamento: si ferma dopo la
    // cattura (il JSON lo porta comunque, includeSound = true).
    if (m_mw->m_audioController) m_mw->m_audioController->stopAll();

    c.errors = InputValidator::errorCount() - errorsBefore;
    m_currentDialogs = nullptr;
    return c;
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
    writeReport();
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
    // Path in moto al Save: la camera 3D la muove il path a ogni tick (e con
    // il path 4D anche angoli e osservatore 4D), e al reload il path la
    // ricalcola dal primo tick. Il valore nel file e' la foto di un istante:
    // Clifford Labyrinth salvava yaw +90 o -90 a seconda del lato del path.
    {
        const QString motion = c.json.value(QStringLiteral("activeMotion")).toString();
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
        // Un popup durante un caricamento e' sempre un difetto (il load si
        // ferma a meta' finche' l'utente non lo chiude): conta, in tutti e due
        // i passaggi, con il predecessore che serve a riprodurlo.
        bool popped = false;
        const Capture *bp = m_passB.contains(e.rel) ? &m_passB[e.rel] : nullptr;
        for (const Capture *c : { &a, bp }) {
            if (!c) continue;
            for (const QString &p : c->dialogs) {
                popups.append(QStringLiteral("%1 (dopo %2): %3")
                                  .arg(e.rel, c->previous.isEmpty() ? QStringLiteral("l'avvio")
                                                                    : c->previous, p));
                if (!p.startsWith(QLatin1String("[avviso]"))) popped = true;
            }
        }
        if (popped) ++withPopups;
        if (a.json.isEmpty()) { ++notLoadable; continue; }

        QSet<QString> moving = a.moving;
        const Capture *b = m_passB.contains(e.rel) ? &m_passB[e.rel] : nullptr;
        if (b) moving.unite(b->moving);
        for (const QString &k : moving) bump(movingByKey, k, e.rel);

        // Non tutto cio' che il Save cambia rispetto al file e' un difetto: chiavi
        // nate dopo il preset, spazi tolti dal load, chiavi obsolete, costanti
        // che nessun codice usa (vedi excusedBecause). Si elencano con la
        // ragione, ma non contano. Fra i due passaggi invece conta tutto: e'
        // stato del preset precedente, mai legittimo.
        const QJsonObject file = readJsonFile(e.path);
        const QList<Diff> save = diffJson(file, a.json, moving);
        const QList<Diff> order = b ? diffJson(a.json, b->json, moving) : QList<Diff>();

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
        if (!order.isEmpty()) {
            details.append(QStringLiteral("  Dipende dal preset caricato prima (passaggio A -> B):"));
            for (const Diff &d : order) details.append(line(d));
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
               .arg(n).arg(m_settleMs).arg(m_singlePass ? 1 : 2)
               .arg(m_filter.isEmpty() ? QString() : QStringLiteral("   filtro: ") + m_filter)
        << QString()
        << QStringLiteral("Identici (Save = file, e nessuna dipendenza dal precedente): %1 su %2").arg(identical).arg(n - notLoadable)
        << QStringLiteral("Il Save cambia o perde qualcosa:                             %1").arg(saveChanged)
        << QStringLiteral("Dipendono dal preset caricato prima:                         %1").arg(orderDependent)
        << QStringLiteral("Non caricabili:                                              %1").arg(notLoadable)
        << QStringLiteral("Con un popup di errore al caricamento:                       %1").arg(withPopups)
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
    rep << QString() << QStringLiteral("== DETTAGLIO PER PRESET ==") << details;

    QFile f(m_outDir + QStringLiteral("/report.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(rep.join(QLatin1Char('\n')).toUtf8() + '\n');

    const bool pass = (saveChanged == 0 && orderDependent == 0 && notLoadable == 0
                       && withPopups == 0);
    log(QStringLiteral("fine: %1 identici su %2, Save diverso %3, dipendenti dall'ordine %4, "
                       "con popup %5. Report: %6")
            .arg(identical).arg(n - notLoadable).arg(saveChanged).arg(orderDependent)
            .arg(withPopups).arg(f.fileName()));
    m_modalWatcher->stop();
    QCoreApplication::exit(pass ? 0 : 1);
}
