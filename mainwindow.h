#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSet>
#include <QLabel>
#include <QColor>
#include <functional>
#include <QButtonGroup>
#include <QProgressBar>
#include <QFileSystemWatcher>
#include <QHash>

#include "glwidget.h"
#include "librarymanager.h"
#include "synthesizer.h"

class QLineEdit;
class QPushButton;
class QCheckBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QMenu;
class QMenuBar;
class VideoRecorder;
class LibraryMenuController;
class PresetSerializer;
class LibraryFileOperations;
class LibraryDragDropHandler;
class AudioController;
class QJsonObject;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class VideoRecorder;
    friend class LibraryMenuController;
    friend class PresetSerializer;
    friend class LibraryFileOperations;
    friend class LibraryDragDropHandler;
    friend class AudioController;
    friend class DesktopInputFilter;
    friend class MobileInputFilter;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    AudioController *m_audioController;


private slots:
    // ==========================================================
    // UI & STATE MANAGEMENT
    // ==========================================================
    void switchToMainMode();
    void switchTo3DMode();
    void switchTo4DMode();
    void update4DButtonState();
    void updateRenderState();
    // Slider trasparenza su campo implicito a prodotto: tooltip al caricamento (slider
    // abilitato), riabilitazione tornando a un campo sano. Il blocco+popup avviene al
    // TOCCO utente in onAlphaSliderMovedIllCheck. Vedi definizioni in .cpp.
    void syncImplicitAlphaSlider(bool isImplicitMode, bool newSurface = false);
    void onAlphaSliderMovedIllCheck(int value);
    // SOLO ANDROID: popup di avviso (NON blocca) al primo tocco dell'alpha su una
    // superficie implicita la cui trasparenza puo' degradare (Gyroid, script RM).
    void onAlphaSliderMovedWarnCheck();
    void applyModeDependentStepUI(bool isImplicit);
    void checkParametricDependency();
    void updateConstraintState();
    void updateConstantsUIState();
    void performMasterStop();
    void performEquationsStop();
    void applyStartSideEffects();

    // ==========================================================
    // RENDERING & VISUALS
    // ==========================================================
    void onColorTargetChanged();
    // Seleziona "Surface" come target colore a segnali bloccati, garantendo
    // l'esclusività cross-gruppo (deseleziona Color1/Color2, che vivono in un
    // QButtonGroup diverso dalla coppia Surface/Background). Sostituisce
    // il vecchio radioEditSurf->setChecked(true), che otteneva la deselezione
    // gratis dall'esclusività del singolo m_colorGroup a quattro.
    void selectSurfaceColorTarget();
    // Deseleziona un radio che appartiene a un QButtonGroup ESCLUSIVO. setChecked(false)
    // diretto è un no-op sull'unico bottone acceso di un gruppo esclusivo (Qt insiste a
    // tenerne uno selezionato): bisogna togliere temporaneamente l'esclusività. Serve per
    // l'esclusività MANUALE fra tripla (m_bgTargetGroup) e color slot (m_colorGroup).
    void uncheckInExclusiveGroup(QAbstractButton *btn);
    // Evidenzia nell'albero texture la voce corrispondente al codice attivo
    // (sfondo se radioBackground è acceso, altrimenti texture superficie).
    void syncTextureTreeSelection();
    void scheduleTextureGeneration();
    void handleTextureSelection(int index);
    // Riporta la trasparenza all'opacità piena (alpha=1). Va chiamata quando si
    // ricade su una superficie di DEFAULT (toro/sfera) per cui la trasparenza
    // del preset/superficie precedente non ha più senso: cambio tab o texture
    // incompatibile col modo corrente (RM su parametrica e viceversa).
    void resetTransparency();

    // ==========================================================
    // EQUATIONS & MATHEMATICS
    // ==========================================================
    bool updateULimits();
    bool updateVLimits();
    bool updateWLimits();

    // ==========================================================
    // ANIMATION, MOTION & TIMERS
    // ==========================================================
    void onStartClicked();
    void onStopClicked();
    void onResetViewClicked();
    void onNavTimerTick();
    void onDepartureClicked();
    void onPathTimerTick();
    void checkPathFields();
    void onDeparture3DClicked();
    void onPath3DTimerTick();
    void checkPath3DFields();
    // Camera dei path al tempo t: unica implementazione, condivisa tra i tick
    // live e il loop di registrazione (VideoRecorder passa il tempo virtuale
    // del frame). Il video deve mostrare cio' che mostrerebbe lo schermo:
    // niente copie locali di questa logica nel recorder.
    void applyPath4DCameraAt(float t);
    void applyPath3DCameraAt(float t);
    // Avanzamento del flusso geodetico di dtSeconds: unica implementazione,
    // condivisa tra il tick di m_geoAnimTimer e il loop di registrazione
    // (VideoRecorder passa il dt virtuale del frame). Converte il dt nella
    // stessa velocita' vista a schermo (0.015 unita' per tick nominale).
    bool advanceGeodesicFlowBy(double dtSeconds);
    // Compilazione delle equazioni path dai campi UI (pulizia input +
    // validazione + popup d'errore): unica implementazione, usata dal
    // Departure e dal commit live con Invio a path in corsa (nuova costante
    // o espressione entra subito, senza stop/ripartenza; i VALORI delle
    // costanti sono gia' live via symbol table per riferimento).
    bool compilePath4DFromFields();
    bool compilePath3DFromFields();
    // Mutua esclusivita' GO / Departure 3D / Departure 4D: attivando uno di questi
    // tre moti gli altri due si spengono. Questi helper fermano gli "altri" senza
    // duplicare la logica di pulizia UI. Ognuno e' un no-op se il suo moto e' fermo.
    void stopPathAnimations();   // ferma pathTimer (4D) e pathTimer3D (3D)
    void stopRotationMotion();   // ferma il moto GO (rotazioni superficie/4D)
    void onToggleViewClicked();    // toggle vista path 4D (pushView)
    void onToggleView3DClicked();  // toggle vista path 3D (pushView3D)
    // I pulsanti Tangent/Center View rispecchiano i rispettivi Departure:
    // attivi se il proprio path e' in corsa o i suoi campi sono compilati
    // (pre-selezione della vista, anche per il subentro con handoff mentre
    // l'altro path gira). Chiamato agli avvii/stop e a ogni modifica dei
    // campi path (via checkPathFields/checkPath3DFields).
    void updateViewButtonsEnabled();
    bool hasPath4DInput() const;   // >=2 campi path 4D compilati (gate Departure/View 4D)
    bool hasPath3DInput() const;   // >=2 campi path 3D compilati (gate Departure/View 3D)
    // Moto GO (rotazioni) davvero in corsa: timer attivo E tasto non su "GO".
    // UNICO lettore autorizzato del testo di btnStart_2 come stato — master
    // button e VideoRecorder passano da qui, mai confronti locali sul testo.
    bool isRotationMotionRunning() const;

    // ==========================================================
    // SCRIPTING ENGINE
    // ==========================================================
    void onToggleScriptMode();
    void onRunCurrentScript();
    void onRunScriptClicked();
    void runMetricScript(const QString& fullText);
    void exitMetricScriptMode();
    void onApplyTextureScriptClicked();
    void onRunRaymarchTextureClicked();
    void onRunSoundClicked();

    // ==========================================================
    // LIBRARY & WORKSPACE MANAGEMENT
    // ==========================================================
    void onExampleItemClicked(QTreeWidgetItem *item, int column);
    void applySurfaceExample(const LibraryItem &data);
    void applyMotionExample(const LibraryItem &data);
    void deleteSelectedExample();
    void onUndoDelete();
    void onAddRepositoryClicked(LibraryType type);
    void onCreateFolderClicked();
    void onSyncPresetsClicked();

    // ==========================================================
    // FILE I/O & CLIPBOARD
    // ==========================================================
    void saveSurfaceToFile(const QString &suggestedPath = QString());
    void onPasteExample(const QString &destDirOverride = QString());
    void onPasteTexture(const QString &destDirOverride = QString());
    void performCut(QTreeWidgetItem* item = nullptr);
    void performCopy(QTreeWidgetItem* targetItem = nullptr);
    void onSaveTextureClicked();
    void onSaveScriptClicked();
    void onSaveMotionClicked();

    // ==========================================================
    // AUDIO & MEDIA
    // ==========================================================
    void onSoundItemClicked(QTreeWidgetItem *item, int column);


private:
    // Estrae la sezione opzionale //CUTOUT_BEGIN..//CUTOUT_END da uno script
    // parametrico (dock Script): la traduce con GlslTranslator e la rimuove dal
    // testo restituito, cosi' NON finisce mai iniettata in getRawPosition() nel
    // vertex shader (romperebbe la compilazione: due "return" di tipo diverso
    // nella stessa funzione). Un solo punto per i due chiamanti che oggi
    // rialimentano lo script parametrico (onRunScriptClicked e la ripresa da
    // Master Start/dock Run): erano diventati due copie della stessa estrazione
    // e la seconda non toglieva mai il cutout -> errore di compilazione.
    QString extractCutoutSection(const QString &fullText, QString *outCutoutGlsl);

    // Estrae le sezioni ripetibili //MESH_BEGIN..//MESH_END (multi-mesh) e le
    // rimuove dal testo restituito, per lo stesso motivo del cutout: non devono
    // finire iniettate in getRawPosition(). Ogni sezione dichiara il dominio e
    // la risoluzione di UNA parte di mesh; lo script distingue il ramo con
    // u_meshIndex. Senza sezioni l'elenco resta vuoto = una mesh sola come
    // sempre. Va chiamata negli STESSI punti dell'estrazione del cutout,
    // altrimenti si ripete il bug "funziona da Run ma non da Master Start".
    QString extractMeshSections(const QString &fullText, std::vector<MeshPart> *outParts);

    // ==========================================================
    // CORE UI COMPONENTS
    // ==========================================================
    Ui::MainWindow *ui;
    QPushButton *m_btnStart;
    QPushButton *m_btnResetView;
    QPushButton *m_btnProjection;
    QPushButton *m_btnRec;
    QLabel *m_statusLabel;
    // Messaggio in sovrimpressione sulla scena (chiave "hintText" dei record).
    // Figlia del glWidget, autonascosta a timer: NON entra nei video esportati,
    // che vengono composti dal render offscreen.
    QLabel *m_hintOverlay = nullptr;
    QTimer *m_hintTimer = nullptr;
    // Messaggio del record attualmente caricato: non c'e' UI per editarlo, va
    // ricordato qui perche' un risalvataggio non lo perda.
    QString m_currentHintText;
    float   m_currentHintSeconds = 6.0f;

    // Costanti DISCRETE dichiarate dallo script con "A := int(min,max);".
    // Chiave = lettera maiuscola (A..F, S); assente = costante continua.
    // Al rilascio dello slider / Enter nel campo il valore scatta all'intero
    // piu' vicino dentro [min,max]. Vedi applyDiscreteConstants().
    struct DiscreteRange { int lo; int hi; };
    QHash<QString, DiscreteRange> m_discreteConsts;

    // Minimi CONTINUI dichiarati con "F := min(0.3);": la costante resta
    // frazionaria ma non scende sotto la soglia. Serve dove sotto un certo
    // valore la figura degenera (i tubi di Clifford collassano sull'asse).
    // Applicati insieme ai discreti, dagli stessi punti.
    QHash<QString, float> m_minConsts;
    QProgressBar *m_renderProgress;
    QButtonGroup *m_colorGroup;
    QButtonGroup *m_modeGroup;
    // Coppia esclusiva Surface / Background: scelta del target di editing (cosa
    // pilotano slider/texture/colore). radioSurface = superficie, radioBackground = sfondo.
    // La semantica: radioBackground->isChecked() == "edito lo sfondo". I color slot
    // (radioTexColor1/2) stanno in m_colorGroup a parte; l'esclusività fra i due gruppi
    // è manuale.
    QButtonGroup *m_bgTargetGroup;

    // Coppia esclusiva All / Mesh (ambito dell'aspetto: tutta la superficie
    // oppure la sola mesh scelta nello spinbox).
    // Serve un QButtonGroup ESPLICITO: l'esclusivita' automatica dei
    // QRadioButton vale solo fra fratelli con lo STESSO genitore, e nella .ui
    // radioMeshOne e' finito dentro il contenitore groupMeshOne (insieme a
    // spinMeshSel) mentre radioMeshAll e' rimasto in widgetMeshSel. Diversi
    // genitori = due gruppi da uno: si potevano selezionare e deselezionare
    // entrambi. Il gruppo li riunisce a prescindere da dove stanno nel layout,
    // quindi il riquadro attorno a "Mesh" si puo' spostare liberamente.
    QButtonGroup *m_meshScopeGroup = nullptr;

    // ==========================================================
    // MATHEMATICAL CONSTANTS & LIMITS
    // ==========================================================
    const float TWO_PI = 6.28318530718f;
    float uMin = 0.0f;
    float uMax = TWO_PI;
    float vMin = 0.0f;
    float vMax = TWO_PI;
    float wMin = 0.0f;
    float wMax = 0.1f;
    int m_lastParametricSteps = 100;
    int m_lastImplicitSteps = 400;
    double m_lastParametricS = 0.0;
    double m_lastImplicitS = 0.4;
    QTimer* m_stepsDebounce = nullptr;
    QTimer* m_meshDebounce = nullptr;
    bool m_constantPopupActive = false;
    // True mentre il popup di rallentamento e' a schermo: evita che segnali
    // performanceWarning gia' in coda aprano box sovrapposti (raffica).
    bool m_perfPopupActive = false;
    // True mentre una guardia trasparenza (forceOpaqueForHeavyRM) sta mostrando
    // il suo popup: fa scartare un performanceWarning gia' in coda (emesso sui
    // primi frame trasparenti prima dell'ack) che altrimenti aprirebbe il box del
    // watchdog SOPRA quello della guardia. Vedi il gestore di performanceWarning.
    bool m_transparencyGuardActive = false;
    QHash<QLineEdit*, float> m_lastValidConst;
    QTimer* m_geoAnimTimer = nullptr;   // timer del flusso geodetico (creato al primo updateGeodesicMesh)
    bool m_geodesicErrorPending = false;
    bool m_inGeoAnimTick = false;

    // ==========================================================
    // RENDERING & COLOR STATE
    // ==========================================================
    int m_savedRenderMode = 0;
    // True quando il ramo Texture del dock Library e' collassato+grigio perche'
    // siamo in surface-wireframe. Traccia la transizione: applichiamo il grigio
    // (o lo togliamo) solo quando lo stato cambia, non a ogni updateRenderState.
    bool m_textureLibraryGrayed = false;
    // Guardia "popup gia' mostrato / slider bloccato per campo a prodotto": il popup
    // compare UNA volta, al primo TOCCO dell'utente sullo slider su un campo a prodotto.
    // Resettata tornando a un campo sano (syncImplicitAlphaSlider). Vedi onAlphaSliderMovedIllCheck.
    bool m_implicitAlphaDisabled = false;
    // SOLO ANDROID. Guardia "avviso trasparenza gia' mostrato": il popup di avviso
    // (NON bloccante) compare UNA volta, al primo tocco dell'alpha su Gyroid / script
    // RM. Resettata per ogni nuova superficie (syncImplicitAlphaSlider newSurface=true).
    bool m_implicitWarnShown = false;
    // Guardia "conferma trasparenza-su-scena-pesante gia' accettata" (tutte le
    // piattaforme, MISURATA: scatta solo se renderingUnderHeavyLoad). Alzata solo
    // se l'utente sceglie "Apply anyway" — un "Keep it opaque" NON la alza, cosi'
    // un nuovo tentativo richiede di nuovo. Resettata per ogni nuova superficie
    // (syncImplicitAlphaSlider newSurface=true).
    bool m_alphaHeavyWarnShown = false;
    // Anti-rientranza del box di conferma: i valueChanged della PRESA ancora in
    // corso, consegnati nel loop annidato di exec(), non devono impilare altri box.
    bool m_alphaHeavyPopupActive = false;
    // "Keep it opaque" dato durante la presa corrente: i valueChanged residui dello
    // stesso gesto vengono riassorbiti a 100 SENZA riaprire il box (falso positivo:
    // finestra che permane/ricompare). Riarmata da una nuova presa (sliderPressed)
    // o dal cambio superficie (syncImplicitAlphaSlider).
    bool m_alphaHeavyDeclined = false;
    // SOLO MOBILE (no-op su desktop): dopo un apply RM riuscito, se il displacement
    // e' stato introdotto/cambiato mentre alpha<1 porta l'alpha a 1 (il costo arriva
    // CON la texture: la conferma misurata non puo' prevederlo). prevDisp = valore
    // di currentDisplacementCode() catturato PRIMA dell'apply.
    void guardTransparencyOnDisplacementApply(const QString &prevDisp);
    // SOLO MOBILE (no-op su desktop): guardia POST-LOAD, chiamata a fine
    // applyCommonData quando lo STATO FINALE caricato e' RM + alpha<1 +
    // displacement. Il load imposta alpha e displacement in modo PROGRAMMATICO
    // (m_settingAlphaProgrammatic), quindi ne' la conferma misurata ne' la
    // guardia interattiva scattano: e' la falla del "record con alpha<1 nel JSON".
    // Porta alpha a 1, avvisa, zittisce il watchdog (come la guardia interattiva).
    void guardTransparencyOnImplicitLoad();
    // Corpo condiviso dalle due guardie displacement: porta alpha a 1 (opaco),
    // zittisce il watchdog per questa scena (acknowledgePerformanceWarning) e
    // mostra `message`. Presuppone il contesto gia' verificato dal chiamante.
    void forceOpaqueForHeavyRM(const QString &message);
    // Alzata mentre impostiamo lo slider trasparenza DA CODICE (load preset,
    // resetTransparency): l'handler valueChanged distingue cosi' il set programmatico
    // dall'interazione utente e non fa scattare il blocco/popup sui load. Vedi setAlphaSliderProgrammatic.
    bool m_settingAlphaProgrammatic = false;
    int m_lightingMode4D = 0;
    float alphaValue = 1.0f;

    QColor m_currentSurfaceColor;
    QColor m_currentBackgroundColor;
    QColor m_texColor1 = Qt::white;
    QColor m_texColor2 = Qt::black;
    QColor m_bgTexColor1 = Qt::white;
    QColor m_bgTexColor2 = Qt::black;

    // ==========================================================
    // TEXTURE & SCRIPTING STATE
    // ==========================================================
    enum ScriptMode {
        ScriptModeSurface,
        ScriptModeTexture,
        ScriptModeSound
    };
    ScriptMode m_currentScriptMode = ScriptModeSurface;

    bool m_isCustomMode = false;
    bool m_isImageMode = false;
    bool m_surfaceTextureState = false;
    bool m_blockTextureGen = false;
    QString lastTextureFolder;
    QString m_currentTexturePath;
    QString m_currentTexturePresetPath;
    QString m_surfaceTextureCode;
    QString m_bgTextureCode;

    QString m_surfaceScriptText;
    // Corpo GLSL dello script metrico (direttive := rimosse, non tradotto).
    // Non vuoto = il flusso geodetico usa il tensore g_ij dello script invece
    // della metrica indotta dall'embedding X/Y/Z/P.
    QString m_metricScriptBody;
    // Caricamento preset: lo stato salvato (limiti, costanti, steps e
    // condizioni iniziali, eventualmente modificati dall'utente dopo il Run)
    // ha la precedenza sulle direttive := dello script metrico, che valgono
    // per intero solo al Run manuale; al load riempiono solo i campi vuoti.
    bool m_metricPresetLoad = false;
    // Firma dell'ultima combinazione metrica+condizioni per cui è già stato
    // mostrato l'avviso "costante ambigua": evita di ripeterlo a ogni frame di
    // animazione o a ogni tweak di slider con la stessa configurazione.
    QString m_lastAmbiguousConstSig;
    void checkMetricConstantAmbiguity();
    // Mappa di visualizzazione (embedding) di uno script metrico. Salva i campi
    // x/y/z/p in root solo se sono una mappa custom (non la carta identità);
    // applica al load reimpostando i campi. Vuoto = identità, non serializzato.
    void writeMetricDisplayMap(QJsonObject& root) const;
    bool metricDisplayMapIsCustom() const;
    QString m_surfaceTextureScriptText;
    QString m_bgTextureScriptText;
    QString m_soundScriptText;

    // ==========================================================
    // MOTION & PATHS
    // ==========================================================
    QTimer *navTimer;
    QSet<int> activeNavActions;
    // Tasti di spostamento a click dei dock 3D/4D (X±, Y±, left, right, roll, ...),
    // raccolti in connectNavButton per abilitarli/disabilitarli in blocco: durante
    // un path la telecamera segue il percorso e questi comandi non hanno senso.
    QVector<QPushButton*> m_navButtons;

    // Inizializzati a nullptr: updateProjectionButtonText() viene chiamata
    // alla riga ~2182 del costruttore,
    // ~700 righe PRIMA che pathTimer/pathTimer3D siano creati (~2875). Senza
    // l'inizializzatore il puntatore raw contiene spazzatura (non 0x0): il
    // guard "pathTimer &&" la considera valida e pathTimer->isActive() va in
    // EXC_BAD_ACCESS dentro QBindingStorage::registerDependency — crash
    // all'avvio, riprodotto SOLO su device iOS (il pattern di memoria dello
    // stack del costruttore capitava innocuo su desktop).
    QTimer *pathTimer = nullptr;
    float pathTimeT = 0.0f;

    QTimer *pathTimer3D = nullptr;
    float pathTimeT3D = 0.0f;

    float m_pathSpeed3D = 0.01f;
    float m_pathSpeed4D = 0.01f;

    // FOV dei due path, INDIPENDENTI (slider nel dock 3D e nel dock 4D).
    // Il FOV effettivo della proiezione e' applicato SOLO dentro
    // applyPath3D/4DCameraAt (quindi anche nei video, che passano di li');
    // fuori dalle path la proiezione resta al default 45 (lo zoom fuori
    // path ha gia' i suoi comandi, e un reset non deve rimpicciolire la
    // superficie). Persistiti come "fov3D"/"fov4D" (legacy: "cameraFov").
    float m_fov3D = 45.0f;
    float m_fov4D = 45.0f;

    // Orientamento 4D (omega/phi/psi) della superficie catturato all'avvio del
    // path 4D: il tick applica le compensazioni -gamma/-beta RELATIVE a questa
    // base, cosi' l'orientamento accumulato dal moto GO non viene azzerato a ogni
    // Departure. Solo il PRIMO Departure 4D della sessione parte da neutro
    // (m_path4DStartedOnce), come gia' avviene per il path 3D.
    float m_pathBaseOmega = 0.0f;
    float m_pathBasePhi   = 0.0f;
    float m_pathBasePsi   = 0.0f;
    bool  m_path4DStartedOnce = false;

    // Il PRIMO Departure della sessione (3D o 4D) azzera la rotazione spaziale di
    // default (neutralizeDefaultRotationForPath); dai successivi si conserva
    // l'orientamento accumulato (es. dal moto GO), senza reset nel passaggio da
    // una modalita' all'altra (GO <-> Departure 3D <-> Departure 4D).
    bool  m_anyPathStartedOnce = false;

    enum CameraPathMode {
        ModeTangential,
        ModeCentered
    };
    CameraPathMode m_pathViewMode4D;     // modalita' vista del path 4D (pushView)
    CameraPathMode m_pathViewMode3D;   // modalita' vista del path 3D (pushView3D)
    // Ultimo moto camera avviato ("rotation" | "path4D" | "path3D", "" = mai):
    // con rotazioni e path entrambi compilati, applyStartSideEffects riavvia
    // SOLO questo (la vecchia cascata faceva vincere sempre il path 3D). Al
    // load di un record viene impostato dalla chiave JSON "activeMotion".
    QString m_lastCameraMotion;

    bool m_masterStopped = false;
    // L'utente ha fermato il suono ESPLICITAMENTE (tasto Stop Sound): in tal caso
    // applyStartSideEffects() non deve riaccenderlo a ogni re-commit di equazione/
    // texture (Enter ad animazione attiva passa per onStartClicked). Si riarma solo
    // su un vero master Start. Stesso pattern di m_masterStopped.
    bool m_userStoppedSound = false;

    // Stessa famiglia di m_userStoppedSound, per gli altri moduli: l'utente ha
    // fermato ESPLICITAMENTE il clock (Stop del dock texture/script o del dock
    // Equations). Senza questi flag lo stop viveva solo nel GLWidget e ogni
    // ricalcolo globale (applyAnimationState) lo sovrascriveva: con path,
    // rotazioni o t-motion in corso, accendere lo sfondo o togglare la checkbox
    // Texture faceva RIPARTIRE la texture (o la geometria) fermata a mano.
    // Si riarmano su Run esplicito del proprio modulo, load di preset/record
    // e master Start (il master governa tutti i moduli).
    bool m_userStoppedTexClock  = false;
    bool m_userStoppedBgClock   = false;
    bool m_userStoppedGeomClock = false;

    // Moto CAMERA (path 4D/3D o rotazioni GO) fermato ESPLICITAMENTE: STOP su
    // Departure, pausa del GO o master STOP. Senza questo flag un commit di
    // equazione (Enter -> onStartClicked -> applyStartSideEffects) riavviava
    // m_lastCameraMotion pur con tutto fermo a mano. Si riarma su master Start,
    // avvio esplicito di un moto camera e load di preset/record (applyCommonData).
    bool m_userStoppedCameraMotion = false;

    // Run del dock Equations (tab Parametric) senza animazione (nessun 't'):
    // dopo aver applicato la modifica grafica il tasto va DISABILITATO finché le
    // equazioni non vengono modificate di nuovo. true = già applicato, niente da
    // rieseguire. Per le equazioni animate il tasto resta Run/Stop e questo flag
    // non lo tocca (vedi updateMasterButtonState). Parte da true: all'avvio la
    // superficie di default è già renderizzata, quindi non c'è nulla da applicare.
    bool m_parametricApplied = true;

    // Stessa logica per il Run del tab Ray Marching (btnImplicit): senza 't'
    // nell'EQUAZIONE implicita (il displacement è del modulo texture, non conta)
    // il tasto si disabilita dopo l'applicazione finché l'equazione non cambia.
    // Parte da true: all'avvio la sfera implicita di default è già renderizzata.
    bool m_implicitApplied = true;

    // Stessa logica one-shot per il Run della TEXTURE Ray Marching (btnTextureCode,
    // alimentato da lineTexture + lineVariations): senza 't' negli script il tasto
    // si disabilita dopo l'applicazione finché uno dei due script non cambia. Con
    // animazione resta Run/Stop. Se entrambi i campi sono vuoti il tasto è
    // disabilitato a prescindere (vedi updateMasterButtonState). Parte da true:
    // all'avvio non c'è texture da applicare.
    bool m_rmTextureApplied = true;

    // false durante la costruzione di MainWindow, true alla fine. Scrivere le
    // equazioni di default in costruzione emette textChanged, che invocherebbe
    // updateMasterButtonState() quando sotto-oggetti come m_audioController
    // (QMediaPlayer interno) non sono ancora pronti -> crash in Release. La
    // guardia in updateMasterButtonState() salta finché la UI non è completa.
    bool m_uiReady = false;

    // ==========================================================
    // LIBRARY & FILE SYSTEM
    // ==========================================================
    LibraryManager m_libraryManager;
    LibraryMenuController* m_menuController;
    PresetSerializer *m_presetSerializer;
    LibraryFileOperations *m_fileOps;
    LibraryDragDropHandler *m_dragDropHandler;

    QList<DeletionBackup> m_undoStack;
    QStringList m_cutFilePaths;
    QStringList m_cutTexturePaths;
    bool m_isCopyOperation = false;
    bool m_libraryInitialized = false;

    QFileSystemWatcher *m_fsWatcher = nullptr;
    QTimer *m_fsSyncTimer = nullptr;

    // ==========================================================
    // MEDIA & RECORDING
    // ==========================================================
    VideoRecorder *m_videoRecorder;

    bool m_isRecording = false;
    bool m_stopRecordingRequested = false;
    bool m_isProcessingVideo = false;
    QString m_recFolder;

    // ==========================================================
    // PRIVATE HELPER METHODS
    // ==========================================================

    // --- Data & Initialization ---
    void setupDefaultFolders();
    void connectSidePanels();
    void connectNavButton(QPushButton *btn, int action);
    // Abilita/disabilita in blocco i tasti di spostamento dei dock 3D/4D e blocca i
    // comandi mouse 3D (rotazione/zoom) del glWidget mentre un path e' attivo.
    void setNavControlsEnabled(bool enabled);

    // --- Library & File I/O ---
    void syncResourcesToFolder(const QString &resourcePath, const QString &diskPath, bool forceRestore = false, int *overwriteState = nullptr);
    void refreshRepositories();
    void refreshAndSelectPreset(QTreeWidget *tree, const QString &path);
    void updateWatcherPaths();
    void copyPath(QString src, QString dst);
    QTreeWidgetItem* getCurrentLibraryItem();
    // Collassa e ingrigisce (grayed=true) o ripristina (false) il ramo Texture del
    // dock Library. Usato per riflettere che in surface-wireframe la texture non e'
    // applicabile. Idempotente sul colore; il collasso e' one-shot (non riespande).
    void setTextureLibraryGrayed(bool grayed);
    void applyCommonData(const LibraryItem &data);
    QString presetsRootPath() const;
    bool resolveNeedsCopy(const QString& src, const QString& dst,
                          bool forceRestore, bool isDeleted, int* overwriteState);

    // --- Parsing, Strings & Scripts ---
    float parseMath(const QString &text, bool *ok = nullptr);
    float parseUIConstant(const QString &exprStr, float A, float B, float C, float D, float E, float F, float S, bool* ok = nullptr);
    struct CascadeConstants { float a, b, c, d, e, f, s; };
    CascadeConstants resolveCascadeConstants(bool restoreTextOnNegative);
    QString composeEquation(const QString &eq, const QString &uDef, const QString &vDef, const QString &wDef);
    void parseAndApplyScriptParams(const QString &scriptCode, bool restartAudio = true,
                                   bool onlyFillEmptyLimits = false);
    bool hasTimeVariable(const QString& code);
    QString extractAndResolveImagePath(const QString& scriptCode);
    QString extractAudioDirectives(const QString& fullText);
    static QString cleanCodeForComparison(QString str);
    // Decide se un item della libreria texture e' quello attivo. Unica sede del
    // confronto: lo usano sia syncTextureTreeSelection sia la sincronizzazione
    // al load di un record.
    static bool textureItemMatchesCode(const LibraryItem &texItem, const QString &activeCode,
                                       const QString &cleanedActiveCode);

    // --- UI State & Graphics ---
    // Mostra un messaggio in sovrimpressione sulla scena per 'seconds' secondi
    // (testo vuoto = nasconde subito).
    void showSceneHint(const QString &text, float seconds);
    void hideSceneHint();
    void repositionSceneHint();
    // Porta le costanti dichiarate discrete ("A := int(min,max);") all'intero
    // piu' vicino nel loro range. Chiamata al RILASCIO dello slider e all'Enter
    // nel campo, mai durante il trascinamento (renderebbe lo slider a scatti
    // mentre lo si muove). Ritorna true se ha modificato qualcosa.
    bool applyDiscreteConstants();
    void updateLayoutForMode(int mode);
    void setupSpeedControl(QPushButton* btnPlus, QPushButton* btnMinus, QLabel* label, std::function<void(float)> setter);
    void updateProjectionButtonText();
    void updateScriptButtonText();
    void updateTextureUIState(bool isTextureOn, bool resetColorTargetToFirst = false);
    bool activeTextureUsesColors() const;
    // Granulari: la texture attiva referenzia u_col1 / u_col2 (token = "u_col1"/"u_col2").
    // Servono per abilitare i due picker INDIPENDENTEMENTE: una texture che usa solo
    // u_col1 (es. "Xor") non deve lasciare attivo il picker di col2, che non avrebbe
    // effetto. activeTextureUsesColors() resta la OR delle due (true se almeno una).
    bool activeTextureUsesColorToken(const QString &token) const;
    // true se la texture attiva (superficie/sfondo, parametrico o ray marching)
    // ha del codice salvabile: rispecchia la selezione dei campi di
    // PresetSerializer::saveTexture(). Pilota l'abilitazione del tasto Save.
    bool hasSavableTexture() const;
    void updateFlatPreviewButton();
    void updateMasterButtonState();
    void applyAnimationState(bool animated, bool dockOnly = false);
    bool hasAnyRotationSpeed() const;
    void generateTexture();
    void applyDefaultCheckerShader();
    // FOV UNICO (slider nel dock renderer, sotto Light). Unico punto che imposta
    // il campo visivo: allinea slider + etichetta e applica SEMPRE il valore
    // alla proiezione, senza dipendere da quale moto stia guidando la camera.
    // Sostituisce applyPathFov3D/applyPathFov4D, che agivano solo con la
    // rispettiva path in corsa (da fermo il FOV era inerte e non correggibile,
    // e i due slider si contendevano lo stesso m_cameraFov).
    void applyCameraFov(float deg);

    // ASPETTO PER-MESH (spinbox nel dock renderer). updateMeshSelectorRange va
    // chiamata dopo ogni rigenerazione della mesh: con una superficie a mesh
    // singola il massimo resta 0 e lo spinbox mostra solo "All", quindi la
    // funzionalita' resta invisibile dove non serve.
    void updateMeshSelectorRange();
    void syncAppearanceControlsToActiveMesh();
    // Porta i radio Base/Phong/Wireframe sulla modalita' indicata, a segnali
    // bloccati (sono un DISPLAY: non devono scrivere nulla).
    void syncRenderRadiosTo(int mode);
    // COMANDO: l'utente ha cliccato un radio Base/Phong/Wireframe. E' l'unico
    // punto che puo' scrivere una modalita' PROPRIA sulla mesh selezionata; con
    // "All" agisce sul globale come da sempre. Tenuto separato dal DISPLAY (i
    // radio mossi da syncAppearanceControlsToActiveMesh) perche' e' proprio la
    // confusione fra i due ruoli che rendeva instabile il wireframe per-mesh.
    void onUserRenderModeChosen();
    // Vera mentre syncAppearanceControlsToActiveMesh sta muovendo i controlli:
    // impedisce che i segnali di quei widget la facciano rientrare.
    bool m_syncingMeshControls = false;
    // Aspetto per-mesh letto da un preset: al momento di applyCommonData le parti
    // non esistono ancora, quindi resta in sospeso qui e viene riversato dopo la
    // rigenerazione della griglia.
    std::vector<MeshPart> m_pendingMeshParts;
    // Ambito All/Mesh letto dal preset, applicato quando le parti esistono.
    // m_meshScopePending lo rende un'azione UNA-TANTUM: il punto di applicazione
    // e' agganciato a meshPartsChanged, che scatta a ogni rigenerazione della
    // griglia, e senza il consumo l'ambito veniva riportato allo stato del
    // preset a ogni cambio di costante.
    bool m_pendingMeshScopeAll = false;
    bool m_meshScopePending = false;
    void applyPendingMeshAppearance();
    // Ripristina l'ambito All/Mesh salvato col preset. Chiamata da
    // applyPendingMeshAppearance PRIMA dell'early-return, cosi' vale anche per i
    // preset che non portano aspetto per-mesh.
    void applyPendingMeshScope();
    void toggleProjection();
    bool applyBackgroundTextureIfNeeded();

    // --- Geometry & Geodesic Flow ---
    void snapshotActiveEquations();
    void restoreActiveEquations(const QStringList &saved);
    QStringList readActiveEquations() const;
    void commitUiFieldsDuringMotion();
    void commitFieldsOnEnter();
    // Invio su un campo path (chiamata dai filtri tastiera desktop/mobile,
    // che CONSUMANO il Return: returnPressed non arriva mai ai QLineEdit):
    // a moto attivo ricompila le equazioni del path del campo al volo.
    void commitPathFieldOnEnter(const QString& fieldName);
    bool updateGeodesicMesh();
    void checkAndTriggerMeshUpdate();
    void stopGeodesicAnimation();
    bool isGeodesicMotionActive() const;
    bool hasGeodesicText() const;
    bool geodesicFieldsAreFinite(const QStringList& exprs, float uMin, float uMax, float vMin, float vMax, float A, float B, float C, float D, float E, float F, float S);

    // --- Inline Math Helper ---
    static float det3x3(float a1, float a2, float a3,
                        float b1, float b2, float b3,
                        float c1, float c2, float c3)
    {
        return a1 * (b2 * c3 - b3 * c2) -
               a2 * (b1 * c3 - b3 * c1) +
               a3 * (b1 * c2 - b2 * c1);
    }


protected:
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
};

#endif // MAINWINDOW_H
