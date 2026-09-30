#ifndef SCENARIOTEST_H
#define SCENARIOTEST_H

#include <QColor>
#include <QObject>
#include <QStringList>

#include "librarymanager.h"

class MainWindow;

// TEST DEGLI SCENARI D'USO: preme i controlli veri (checkbox, radio Base/Phong/
// Wireframe, Surface/Background) come farebbe l'utente e dopo ogni gesto
// verifica che le copie dello stato della scena siano coerenti fra loro e con
// cio' che scriverebbe il Save. E' la rete per la tappa 3 dello "stato unico
// della scena": il test di andata e ritorno copre load e save, non i gesti, ed
// e' nei gesti che lo stato duplicato diverge.
//
// Regole verificate sull'ACCENSIONE della texture di superficie:
//   - l'intenzione (m_surfaceTextureState) cambia SOLO col checkbox della
//     superficie, mai coi cambi di modalita' o di bersaglio;
//   - il motore la segue: acceso = intenzione e non wireframe;
//   - il checkbox la mostra: spento e disabilitato in wireframe, altrimenti
//     uguale all'intenzione (quando si edita la superficie);
//   - il Save scrive l'intenzione, in qualunque modalita' si salvi.
// E sui COLORI u_col1/u_col2 (vedi checkTexColors) e sul DISPLACEMENT
// (checkDisplacement).
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
    // Come un click sulla voce della Library Textures (handleTextureSelection),
    // cercata per percorso nella libreria dell'app.
    bool selectTexture(const QString &rel);
    void wait(int ms);
    void check(bool ok, const QString &what);
    // Le quattro copie dell'accensione dopo il gesto `step`, con l'intenzione
    // attesa. Una riga per regola violata.
    void checkTextureEnabled(const QString &step, bool expectedIntent);
    // I colori u_col1/u_col2 della texture di superficie dopo il gesto `step`:
    // i picker mostrano i colori EFFICACI del bersaglio (la fascia selezionata
    // se ne ha di propri, altrimenti i due slot globali che eredita), gli
    // slider mostrano quello dello slot scelto, il Save scrive i globali.
    // `global1/2`, se validi, sono i colori globali attesi; `shown1/2` quelli
    // efficaci attesi sul bersaglio (se non validi: uguali ai globali attesi).
    void checkTexColors(const QString &step, const QColor &global1 = QColor(),
                        const QColor &global2 = QColor(), const QColor &shown1 = QColor(),
                        const QColor &shown2 = QColor());
    // Il DISPLACEMENT (rilievi, solo Ray Marching) dopo il gesto `step`: il
    // campo e il motore dicono la stessa cosa -- salvo `pendingEdit`, un campo
    // modificato a mano e non ancora eseguito -- e il Save scrive il campo.
    // `expected`, se non nullo, e' il codice atteso nel campo.
    void checkDisplacement(const QString &step, const QString &expected = QString(),
                           bool pendingEdit = false);
    // Il codice di displacement di un preset della libreria (come lo legge l'app).
    QString presetDisplacement(const QString &rel, LibraryType type);
    // Come l'utente: slot Colore 1/2 col suo radio, poi i tre slider.
    void setTexColorBySliders(bool slot2, const QColor &c);
    void finish();

    MainWindow *m_mw;
    QString m_root;
    QString m_outDir;
    QString m_record;          // record in scena (per la cattura del Save)
    QStringList m_lines;
    int m_failures = 0;
};

#endif // SCENARIOTEST_H
