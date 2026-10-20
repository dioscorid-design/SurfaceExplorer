#ifndef CLOCKTEST_H
#define CLOCKTEST_H

#include <QObject>
#include <QStringList>

class MainWindow;

// TEST DEGLI OROLOGI DI ANIMAZIONE (t di geometria, texture, sfondo, fasce),
// dal vivo e in registrazione video. E' la prova di non-regressione chiesta
// per il refactoring che ha tolto la base comune m_manualTime: nel video il
// tempo del recorder entrava due volte (velocita' doppia, partenza da zero) e
// ogni stop/start dell'orologio faceva saltare avanti le animazioni.
//
// Scena: un record con la geometria animata in t, poi texture FERMATA a mano e
// sfondo animato (lo scenario indicato per questo refactoring). Verifica:
//   - dal vivo gli orologi accesi avanzano col tempo reale, quelli spenti no;
//   - stop/start dell'orologio (come il Save Record) non fa saltare nulla;
//   - in REC ogni frame avanza di esattamente 1/fps a partire dal tempo che lo
//     schermo mostrava (letto dall'UBO dopo il render del frame), i moduli
//     fermi restano fermi, e il tempo reale che passa non conta;
//   - dopo il REC lo schermo riprende da dove il video e' arrivato;
//   - resetTime riporta all'origine;
//   - le fasce di un record multi-mesh seguono ciascuna il proprio flag.
// In REC usa le STESSE chiamate del recorder (setClocksDrivenByRecorder,
// advanceClocksBy, getFrameForVideo): la logica sta in GLWidget, non qui.
//
//   SurfaceExplorer --clock-test <radice preset> <cartella uscita>
//
// Scrive clock-report.txt; esce con 0 se tutte le verifiche passano, 1 altrimenti.
class ClockTest : public QObject
{
    Q_OBJECT
public:
    static bool requested(const QStringList &args);
    static void start(MainWindow *mw, const QStringList &args);

private:
    ClockTest(MainWindow *mw, const QString &root, const QString &outDir);
    void run();
    bool loadRecord(const QString &rel);
    void wait(int ms);
    void check(bool ok, const QString &what);
    void finish();

    MainWindow *m_mw;
    QString m_root;
    QString m_outDir;
    QStringList m_lines;
    int m_failures = 0;
};

#endif // CLOCKTEST_H
