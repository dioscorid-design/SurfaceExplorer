#ifndef SCENARIOTEST_H
#define SCENARIOTEST_H

#include <QObject>
#include <QStringList>

class MainWindow;

// TEST DEGLI SCENARI D'USO: preme i controlli veri (checkbox, radio Base/Phong/
// Wireframe, Surface/Background) come farebbe l'utente e dopo ogni gesto
// verifica che le copie dello stato della scena siano coerenti fra loro e con
// cio' che scriverebbe il Save. E' la rete per la tappa 3 dello "stato unico
// della scena": il test di andata e ritorno copre load e save, non i gesti, ed
// e' nei gesti che lo stato duplicato diverge.
//
// Regole verificate, per ora sull'ACCENSIONE della texture di superficie:
//   - l'intenzione (m_surfaceTextureState) cambia SOLO col checkbox della
//     superficie, mai coi cambi di modalita' o di bersaglio;
//   - il motore la segue: acceso = intenzione e non wireframe;
//   - il checkbox la mostra: spento e disabilitato in wireframe, altrimenti
//     uguale all'intenzione (quando si edita la superficie);
//   - il Save scrive l'intenzione, in qualunque modalita' si salvi.
//
//   SurfaceExplorer --scenario-test <radice preset> <cartella uscita>
//
// Scrive scenario-report.txt; esce con 0 se tutte le verifiche passano.
class ScenarioTest : public QObject
{
    Q_OBJECT
public:
    static bool requested(const QStringList &args);
    static void start(MainWindow *mw, const QStringList &args);

private:
    ScenarioTest(MainWindow *mw, const QString &root, const QString &outDir);
    void run();
    bool loadRecord(const QString &rel);
    void wait(int ms);
    void check(bool ok, const QString &what);
    // Le quattro copie dell'accensione dopo il gesto `step`, con l'intenzione
    // attesa. Una riga per regola violata.
    void checkTextureEnabled(const QString &step, bool expectedIntent);
    void finish();

    MainWindow *m_mw;
    QString m_root;
    QString m_outDir;
    QString m_record;          // record in scena (per la cattura del Save)
    QStringList m_lines;
    int m_failures = 0;
};

#endif // SCENARIOTEST_H
