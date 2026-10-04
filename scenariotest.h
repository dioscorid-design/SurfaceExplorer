#ifndef SCENARIOTEST_H
#define SCENARIOTEST_H

#include <QColor>
#include <QMap>
#include <QObject>
#include <QStringList>

#include "librarymanager.h"

class MainWindow;
class QWidget;

// TEST DEGLI SCENARI D'USO: preme i controlli veri (checkbox, radio Base/Phong/
// Wireframe, Surface/Background) come farebbe l'utente e dopo ogni gesto
// verifica che le copie dello stato della scena siano coerenti fra loro e con
// cio' che scriverebbe il Save. E' la rete per la tappa 3 dello "stato unico
// della scena": il test di andata e ritorno copre load e save, non i gesti, ed
// e' nei gesti che lo stato duplicato diverge.
//
// Regole verificate sull'ACCENSIONE della texture di superficie:
//   - l'intenzione (m_scene.surfaceTextureState) cambia SOLO col checkbox della
//     superficie, mai coi cambi di modalita' o di bersaglio;
//   - il motore la segue: acceso = intenzione e non wireframe;
//   - il checkbox la mostra: spento e disabilitato in wireframe, altrimenti
//     uguale all'intenzione (quando si edita la superficie);
//   - il Save scrive l'intenzione, in qualunque modalita' si salvi.
// E sui COLORI u_col1/u_col2 (vedi checkTexColors), sul DISPLACEMENT
// (checkDisplacement), sul CODICE della texture (checkTextureCode), sulla sua
// IMMAGINE (checkSurfaceImage) e sull'immagine di SFONDO (checkBackground).
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
    // Come il load di una superficie della Library (applySurfaceExample).
    bool loadSurface(const QString &rel);
    // Il click vero sulla voce della Library Textures (segnale itemClicked
    // dell'albero, quindi onExampleItemClicked), cercata per percorso nella
    // libreria dell'app.
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
    // Lo SFONDO dopo il gesto `step`: il Save scrive cio' che e' a schermo.
    // Spento: niente codice ne' ancora, e in GPU la default (riaccendendo si
    // vede quella). Acceso con un'immagine: il tag //IMG: del Save e' l'immagine
    // caricata nel motore. `expectedImage`, se non nullo, e' il nome del file
    // atteso a schermo ("" = la default).
    void checkBackground(const QString &step, const QString &expectedImage = QString());
    // I comandi della SUPERFICIE nel dock RENDERER dopo il gesto `step`: in
    // Background spenti (modo di resa, densita', trasparenza, luce, Headlight),
    // FOV acceso; su Surface di nuovo secondo le regole normali
    // (Wireframe solo in parametrico, densita' solo in wireframe, luce spenta in
    // wireframe, Headlight solo in Ray Marching).
    void checkSurfaceControls(const QString &step);
    // Il CODICE della texture di superficie dopo il gesto `step`. Ray Marching:
    // il campo lineTexture e il motore dicono la stessa cosa, salvo
    // `pendingEdit` (campo modificato a mano, non ancora eseguito), e il Save
    // scrive il campo. Parametrico: lo script (l'editor se mostra la texture,
    // altrimenti il suo slot) e' quello applicato salvo `pendingEdit`, il motore
    // compila l'applicato, il Save scrive lo script qualunque modulo mostri il
    // dock; campo e motore Ray Marching vuoti. Si confronta la parte GRAFICA:
    // senza audio e, in parametrico, senza tag //IMG:. `expected`, se non
    // nullo, e' il codice atteso.
    void checkTextureCode(const QString &step, const QString &expected = QString(),
                          bool pendingEdit = false);
    // L'IMMAGINE della texture di superficie dopo il gesto `step`: quella in
    // GPU (GLWidget::surfaceImagePath) e' quella del tag //IMG: dello script
    // (campo lineTexture in Ray Marching, editor o slot in parametrico) e del
    // Save. Texture spenta: niente immagine ne' in GPU ne' nel Save.
    // `expectedImage`, se non nullo, e' il file atteso in GPU ("" = nessuna).
    void checkSurfaceImage(const QString &step, const QString &expectedImage = QString());
    // Le EQUAZIONI dopo il gesto `step`. Ray Marching: sotto-tab, ramo del
    // motore e Save d'accordo su 3D/Cross Section; il motore compila il campo
    // attivo; il Save scrive i due campi. Parametrico: lo snapshot dell'ultimo
    // Run e' il testo dei campi X/Y/Z/P, il Save scrive i campi, il dominio
    // u/v del motore e' quello dei campi limite, e le due copie dello script di
    // superficie coincidono. `pendingEdit`: campi modificati e non eseguiti.
    void checkEquations(const QString &step, bool pendingEdit = false);
    // PATH E MOTI dopo il gesto `step`. Viste dei due path: tasto = membro =
    // Save. Velocita' dei path: slider = Save. Velocita' delle
    // rotazioni: etichetta = motore = Save. Tasti e timer: Departure/GO dicono
    // STOP se e solo se il loro moto gira, mai due moti camera insieme, il moto
    // "ultimo avviato" e' quello in corsa. FOV: slider = membri = Save. Path
    // applicato: a path in corsa il motore valuta le equazioni dei campi, salvo
    // `pendingEdit` (campi modificati e non confermati). `expectRunning`, se non
    // nullo, e' il moto camera atteso in corsa ("path4D", "path3D", "rotation",
    // "none").
    void checkMotion(const QString &step, const QString &expectRunning = QString(),
                     bool pendingEdit = false);
    // LAVORO NON SALVATO dopo il gesto `step`: cio' che l'avviso "vuoi
    // salvare?" difenderebbe adesso -- la scena, il modulo texture, il modulo
    // suono (vedi MainWindow::confirmDiscardUnsaved).
    void checkDirty(const QString &step, bool scene, bool texture, bool sound);
    // La scena senza moti: quello che devono lasciare NEW, il cambio di
    // modalita' e il load di una superficie dopo un record in movimento.
    void checkMotionDefaults(const QString &step);
    // Le COSTANTI A..F/S dopo il gesto `step`: il campo di testo e' la fonte
    // (puo' essere un'espressione a cascata), lo slider lo mostra, il motore usa
    // quel valore, il Save lo scrive. Una costante spenta (nessun modulo la usa)
    // sta al valore neutro (1; S: 0). `expected`: valori attesi per lettera.
    void checkConstants(const QString &step,
                        const QMap<QString, double> &expected = QMap<QString, double>());
    // Come l'utente: lo slider (trascina e rilascia) o il campo (testo e Invio).
    void setConstantBySlider(const QString &letter, double value);
    void setConstantByField(const QString &letter, const QString &text);
    // Il codice di displacement di un preset della libreria (come lo legge l'app).
    QString presetDisplacement(const QString &rel, LibraryType type);
    // La parte grafica del codice texture di un preset (vedi checkTextureCode).
    QString presetTextureCode(const QString &rel, LibraryType type, bool dropImage);
    // Cio' che scriverebbe il Save del record in scena.
    LibraryItem captureSave();
    // Come un click sulla voce della Library Sounds, cercata per percorso.
    bool selectSound(const QString &rel);
    // Porta il dock Script sul modulo `mode` (MainWindow::ScriptMode) col suo tasto.
    void setScriptMode(int mode);
    // Il tasto NEW, rispondendo "Don't save" alla conferma.
    void pressNew();
    // Il tasto Invio dentro un campo (applica equazioni, limiti, costanti).
    void pressEnter(QWidget *w);
    // Testo DIGITATO in un QLineEdit (non scritto dal programma).
    void typeInField(class QLineEdit *l, const QString &text);
    // Applica una modifica alle equazioni parametriche come fa l'utente: Invio
    // nel campo se l'animazione e' in corso, tasto Run del dock se e' ferma.
    void applyEquationEdit(QWidget *field);
    // Come l'utente: slot Colore 1/2 col suo radio, poi i tre slider.
    void setTexColorBySliders(bool slot2, const QColor &c);
    // DOCK SCRIPT: l'editor e' un solo widget per piu' testi (script della
    // superficie, texture di superficie, di una fascia, di sfondo, suono). Cio'
    // che e' scritto e non eseguito sopravvive ai cambi di modulo, di bersaglio
    // e di fascia, resta eseguibile (tasto Run acceso) e non passa per applicato.
    // Si lancia anche da solo: --scenario-only script-dock.
    void runScriptDockScenarios();
    // BERSAGLIO DEL DOCK RENDERER (Surface / Background) e checkbox Texture: il
    // checkbox e' la VISTA della texture del bersaglio (sfondo, fascia o
    // superficie) e nessuno lo legge per sapere se la texture di SUPERFICIE e'
    // accesa. Col bersaglio su Background e lo sfondo spento, master Start e
    // Run riavviano comunque la texture della superficie.
    // Da sola: --scenario-only texture-target.
    void runTextureTargetScenarios();
    // IMMAGINI SULLE SINGOLE MESH (multi-mesh, ambito Mesh): un'immagine scelta
    // in Library con una fascia selezionata va su QUELLA fascia, ferma, e la
    // superficie non cambia; fasce diverse tengono immagini diverse; uno script
    // di "Animated Images" anima l'immagine della fascia, una procedurale che
    // non campiona nulla la toglie; in ambito All sono sospese; i record
    // salvati prima (tag della fascia = immagine della superficie) non cambiano.
    // Da sola: --scenario-only mesh-image.
    void runMeshImageScenarios();
    // Load di record la cui texture si compilava male: motore = applicata =
    // script (checkTextureCode). Kerr Spin Animated porta un blocco audio coi
    // marcatori spaziati ("// SOUND_BEGIN"), Oloid un vincolo in W che al
    // momento del load il vertex non conosce ancora.
    // Da sola: --scenario-only record-texture.
    void runRecordTextureScenarios();
    // Library: aprire e chiudere la cartella del preset caricato (doppio clic
    // vero sulla riga; tap mobile simulato) non gli toglie l'evidenziazione, e
    // il clic su una cartella non la toglie agli altri alberi.
    // Da sola: --scenario-only library-folder.
    void runLibraryFolderScenarios();
    // Library: copia e incolla di una CARTELLA, in una cartella temporanea (la
    // libreria non si tocca): accanto a se stessa -> _copy1 col contenuto; in
    // una cartella il cui nome la contiene come prefisso (Paths -> Paths2);
    // dentro una sua sottocartella -> rifiutata.
    // Da sola: --scenario-only library-paste.
    void runLibraryPasteScenarios();
    // "" se checkbox ed etichetta dicono il bersaglio e lo stato della sua
    // texture, altrimenti la differenza.
    QString textureCheckboxProblem() const;
    // Steps: slider, campo, motore (risoluzione della mesh in parametrico, Ray
    // Steps in Ray Marching) e Save dicono lo stesso numero. Vuoto se si'.
    QString stepsProblem();
    // Luce: slider = motore (unica copia, globale) = Save. Vuoto se si'.
    QString lightProblem();
    // Il bersaglio Surface/Background (MainWindow::m_editTarget) e le sue viste
    // -- i due radio, il bersaglio della vista 2D nel motore -- dicono la
    // stessa cosa. Vuoto se si'.
    QString editTargetProblem() const;
    // Base/Phong/Wireframe (parametrico): la modalita' globale nello stato
    // (m_scene.renderMode) e nel motore coincidono, e i radio mostrano quella
    // della fascia selezionata o, in ambito All, la globale. Vuoto se si'.
    QString renderModeProblem() const;
    // Shell/Solid e Fast/Precise (Ray Marching): radio = stato = motore.
    QString implicitRenderProblem() const;
    // Ambito All/Mesh: radio = stato; su una multi-mesh parametrica, in All
    // nessuna fascia attiva e aspetto uniforme nel motore, in Mesh no.
    QString meshScopeProblem() const;
    // Modalita' Parametric / Implicit: stato, linguetta e motore coincidono.
    QString modeProblem() const;
    // "" se l'editor del dock Script mostra lo slot che il dock indica (e' la
    // sua vista), altrimenti la descrizione della differenza. La controllano
    // le verifiche piu' frequenti (codice della texture, equazioni).
    QString scriptEditorProblem() const;
    // Lo stesso per i quattro campi Ray Marching e il loro stato (m_scene.rm).
    QString rmFieldsProblem() const;
    // Lo stesso per i campi a una riga: limiti u/v/w, taglio x/y/z del Ray
    // Marching, path 4D e 3D (m_scene.lim, m_scene.path).
    QString lineFieldsProblem() const;
    void finish();

    MainWindow *m_mw;
    QString m_root;
    QString m_outDir;
    QString m_record;          // record in scena (per la cattura del Save)
    QString m_only;            // --scenario-only <nome>: una sola sezione
    QStringList m_lines;
    // Campi di testo scritti a segnali VIVI mentre un load o un reset li
    // riempie (MainWindow::m_populatingFields): ogni emissione fa girare i
    // gestori pensati per la digitazione. "campo (sezione)", senza doppioni.
    QStringList m_liveWritesDuringLoad;
    // Errori di compilazione degli shader comparsi nel log (shadercompilelog.h),
    // con la sezione in cui sono avvenuti: quelli che contano e quelli attesi.
    QStringList m_shaderErrors;
    QStringList m_shaderExcused;
    void collectShaderErrors();
    int m_failures = 0;
    // Durante una scelta in Library il popup "lavoro non salvato" riceve
    // "Don't save" invece di Annulla (vedi il costruttore).
    bool m_discardOnPrompt = false;
    // Il watchdog della GPU ha fermato l'animazione durante un caricamento
    // (macchina sotto carico): il load si ripete, o i gesti seguenti partono
    // da una scena ferma che non e' quella in prova.
    bool m_watchdogFired = false;
    // Popup chiusi dal test (errori e avvisi; non il "Don't save" ne' il
    // watchdog): per verificare che un gesto NON ne apra.
    int m_popupsClosed = 0;
};

#endif // SCENARIOTEST_H
