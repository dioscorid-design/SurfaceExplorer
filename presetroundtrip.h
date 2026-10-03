#ifndef PRESETROUNDTRIP_H
#define PRESETROUNDTRIP_H

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QFile>

class MainWindow;
class QTimer;

// TEST DI ANDATA E RITORNO DEI PRESET (tappa 1 del refactoring "stato unico
// della scena"). Carica ogni superficie e ogni record di una libreria con lo
// stesso percorso di un click nell'albero, ricostruisce il JSON con lo stesso
// codice di Save (PresetSerializer::build*Json) e lo confronta:
//
//   1. col file originale: cio' che un Save subito dopo il Load cambierebbe;
//   2. con un secondo passaggio in ordine INVERSO: ogni preset viene caricato
//      dopo un predecessore diverso, quindi una differenza fra i due passaggi
//      e' stato del preset precedente sopravvissuto al caricamento
//      ("carica A poi B" != "carica B");
//   3. con un terzo passaggio in ordine RIMESCOLATO (seme fisso, quindi sempre
//      lo stesso): i primi due provano solo le coppie vicine in ordine
//      alfabetico, questo ne prova altre.
//
// I valori che avanzano col tempo (rotazioni, path in corsa) si riconoscono da
// soli: il JSON si cattura due volte a distanza di qualche centinaio di ms e le
// chiavi che cambiano fra le due catture escono dal confronto stretto.
//
// Non scrive nulla nella libreria: legge i preset e mette report e JSON
// ricostruiti nella cartella di uscita. Si lancia solo da riga di comando:
//
//   SurfaceExplorer --roundtrip-test <radice preset> <cartella uscita>
//                   [--filter <testo>[|<testo>...]] [--settle <ms>] [--single-pass]
//                   [--data-only] [--no-shuffle] [--shuffle-seed <n>]
//
// La radice e' quella che contiene surfaces/ e records/. Esce con codice 0 se
// tutti i preset tornano identici, 1 altrimenti.
class PresetRoundTrip : public QObject
{
    Q_OBJECT
public:
    static bool requested(const QStringList &args);
    static void start(MainWindow *mw, const QStringList &args);

private:
    struct Entry {
        QString path;       // assoluto
        QString rel;        // relativo alla radice, per report e file di uscita
        bool isRecord = false;
    };
    struct Capture {
        QJsonObject json;
        QSet<QString> moving;   // chiavi cambiate fra le due catture
        QStringList dialogs;    // popup chiusi dal test durante il caricamento
        QString previous;       // preset caricato subito prima (per riprodurre)
        quint64 errors = 0;     // errori segnalati da InputValidator
        // Costanti col campo spento dopo il caricamento: per l'app non le usa
        // nessun codice (updateConstantsUIState), e le riporta al default.
        QSet<QString> unusedConstants;
        QSet<QString> emptyLimits;      // "uMin", "wMax"...: campi limite vuoti alla cattura
        // Chiavi che l'avviso "vuoi salvare?" difenderebbe a fine cattura, senza
        // che nessuno abbia toccato nulla (MainWindow::unsavedKeys): devono
        // essere zero, o al preset successivo uscirebbe un popup fantasma.
        QStringList unsavedAfterLoad;
        // Errori di compilazione degli shader comparsi nel log durante il
        // caricamento (vedi shadercompilelog.h): quelli che contano, e quelli
        // attesi, elencati con la loro ragione.
        QStringList shaderErrors;
        QStringList shaderExcused;
    };

    // Perche' una differenza fra file e Save NON conta; vuota = conta.
    static QString drivenByMotion(const QString &key, const QJsonObject &saved);
    static QString excusedBecause(const QString &key, int kind, bool whitespaceOnly,
                                  const QJsonObject &file, const Capture &c);

    PresetRoundTrip(MainWindow *mw, const QString &root, const QString &outDir,
                    const QString &filter, int settleMs, bool singlePass, bool dataOnly);

    void run();
    void collect();
    Capture loadAndCapture(const Entry &e);
    // Un solo tentativo di load e cattura (vedi loadAndCapture).
    Capture loadAndCaptureOnce(const Entry &e);
    // Load ripetuti perche' il watchdog della GPU aveva fermato la scena.
    int m_watchdogReloads = 0;
    QJsonObject captureJson(const Entry &e);
    void wait(int ms);
    void closeModalDialogs();
    void writeReport();
    void log(const QString &line);

    MainWindow *m_mw;
    QString m_root;
    QString m_outDir;
    QString m_filter;
    int m_settleMs;
    bool m_singlePass;
    // --data-only: niente app, solo file -> parseJson -> toJson -> confronto.
    // Trova in pochi secondi cio' che il parser legge ma il Save non scrive (o
    // viceversa), senza il rumore dei caricamenti; non vede i bug del load.
    bool m_dataOnly;

    QList<Entry> m_entries;
    QHash<QString, Capture> m_passA;    // chiave: Entry::rel
    QHash<QString, Capture> m_passB;
    // Terzo passaggio, in ordine rimescolato a seme fisso (vedi run()). Spento
    // da --single-pass, --data-only e --no-shuffle.
    QHash<QString, Capture> m_passC;
    bool m_shufflePass = true;
    uint m_shuffleSeed = 20261001;
    QStringList *m_currentDialogs = nullptr;
    QTimer *m_modalWatcher = nullptr;
    QFile m_logFile;
    QTextStream m_logStream;
};

#endif // PRESETROUNDTRIP_H
