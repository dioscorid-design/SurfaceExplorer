#ifndef SURFACEOBJECT_H
#define SURFACEOBJECT_H

// UN OGGETTO DELLA SCENA (2.0): una superficie col suo motore, il suo aspetto,
// le sue risorse GPU, la sua posa coi suoi moti e i suoi orologi. Sono i dati
// che fino alla 1.4 stavano in GLWidget accanto a quelli della VISTA
// (camera, proiezione e osservatore 4D, sfondo, luce comune, timer, recorder),
// che restano li'. La modalita' Superficie e' una scena con un oggetto solo.
//
// Tappa 1a: solo i DATI, spostati cosi' come erano (i commenti sono quelli
// originali, coi nomi senza il prefisso m_). Le funzioni che li usano sono
// ancora in GLWidget, che li raggiunge da m_obj.

#include "surfaceengine.h"
#include "geometrybuilder.h"   // WireframeRange

#include <rhi/qrhi.h>
#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMap>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <memory>
#include <vector>

class QTimer;

struct SurfaceObject
{
    // Parametrico o Ray Marching: ogni oggetto ha il suo.
    enum EngineMode {
        ModeParametric = 0,
        ModeImplicit   = 1
    };
    // Il bordo, acceso, si vedrebbe? Vedi GLWidget::borderAvailability.
    enum class BorderAvailability { Unknown, Visible, None };
    // Densita' di default del wireframe del bordo (GLWidget::BorderStyle).
    static constexpr int kBorderWireAlongDefault = 8;
    static constexpr int kBorderWireAroundDefault = 2;

    // ==========================================================
    // MOTORE E MODALITA'
    // ==========================================================
    std::unique_ptr<SurfaceEngine> engine;
    EngineMode engineMode = ModeParametric;
    // Campo implicito "mal condizionato": equazioni a PRODOTTO di piu' fattori
    // (es. preset "Chain" = -(f1*f2*...*f6)) hanno un range dinamico enorme
    // (~1e9) e cambiano segno per PARITA' del prodotto anche lontano da ogni
    // superficie reale. Il ramo trasparente (marchNextLayer) rileva le facce col
    // cambio di segno GREZZO del campo, quindi aggancia questi crossing FANTASMA
    // e il compositing esce con alpha~0 -> la superficie SPARISCE con alpha<1.
    // Il ramo OPACO usa d=val/gradLen e non ne soffre. Quando questo flag e' true
    // forziamo alpha=1.0 (fallback a opaco) e la UI disabilita lo slider alpha.
    // Rilevato empiricamente campionando il campo su CPU in setImplicitEquation:
    // NON e' un'euristica sulla stringa (un legittimo (x)*(y) non lo attiva).
    bool implicitIllConditioned = false;
    // SOLO ANDROID: avviso (non blocco) per la trasparenza che potrebbe degradare.
    // Sempre false su desktop/iOS. Vedi implicitTransparencyMayDegrade().
    bool implicitTransparencyWarn = false;

    // ==========================================================
    // GEOMETRIA (equazioni applicate, griglia, dominio delle texCoord)
    // ==========================================================
    QString eqX, eqY, eqZ, eqW;
    bool meshNeedsUpdate = true;
    int indexCount = 0;
    int wfStepU = 4;
    int wfStepV = 4;
    int raySteps = 100;
    float surfaceScale = 2.0f;
    bool isCustomMesh = false;

    // DOMINIO DI RIFERIMENTO delle coordinate texture della mesh geodetica.
    // Le texCoord sono la frazione di QUESTO dominio, non dell'intervallo
    // corrente: e' cio' che tiene la texture ancorata alla geometria quando si
    // restringe l'intervallo (la si vede TAGLIATA, non compressa). Fissato al
    // primo calcolo dopo un load/reset (uvRefValid=false) e tenuto fermo
    // finche' non se ne carica un altro, cosi' il preset conserva l'aspetto con
    // cui e' stato salvato.
    bool  uvRefValid = false;
    float uvRefUMin = 0.0f, uvRefUSpan = 1.0f;
    float uvRefVMin = 0.0f, uvRefVSpan = 1.0f;

    // ==========================================================
    // EQUAZIONI IMPLICITE (Ray Marching)
    // ==========================================================
    QString eqImplicitF = "x*x + y*y + z*z - 1.0";
    // Equazione a 4 variabili (x,y,z,p) del sotto-tab Cross Section. Stato
    // separato da eqImplicitF: i due sotto-tab (3D / Cross Section) non si
    // sovrascrivono a vicenda. p e' la quarta coordinata del punto nel
    // riferimento della superficie, ricavata applicando la rotazione 4D INVERSA
    // al punto (x,y,z,0) della camera — vedi createImplicitFragmentShader().
    // Default: 3-toro T^3 (toro-di-tori, S=x^2+y^2+z^2+A^2, M=S+p^2+B^2-C^2),
    // A=raggio esterno, B=raggio intermedio, C=raggio tubo. A p=0 NON si
    // riduce al toro 2D standard: e' proprio l'oggetto 4D nel suo riferimento.
    // DEVE combaciare con kT3Equation in MainWindow::loadCrossSectionDefaultSurface
    // (fattore di scala compreso): sono due copie della stessa equazione, e una
    // che resta indietro e' un default incoerente con cio' che l'utente vede.
    QString eqCrossSectionF =
        "0.5*(((x*x+y*y+z*z+p*p+A*A+B*B-C*C)^2 + 4*(A*A-B*B)*(x*x+y*y) - 4*B*B*(z*z+A*A))^2 "
        "- 16*A*A*(x*x+y*y)*(x*x+y*y+z*z+p*p+A*A-B*B-C*C)^2)";
    // Quale delle due equazioni sopra e' quella attiva nello shader compilato.
    bool implicitUsesCrossSection = false;

    // ==========================================================
    // COSTANTI DEL FLUSSO GEODETICO
    // ==========================================================
    // Il flusso geodetico si calcola in geodesiccalculator + compute shader:
    // qui resta solo la mappa delle costanti (GLWidget::setConstants).
    QMap<QString, float> constants;

    // ==========================================================
    // UNIFORM PROPRI CHE NON CAMBIANO A OGNI FRAME
    // ==========================================================
    // Li scrivono i setter (costanti A..F e S, dominio u/v, taglio x/y/z del
    // Ray Marching) e prepareObjectUniforms li copia nel blocco a ogni frame.
    // Fino alla tappa 1c stavano solo in GLWidget::m_uboData, la copia di
    // lavoro degli uniform, che pero' e' una per tutti gli oggetti: le costanti
    // di uno avrebbero sovrascritto quelle dell'altro.
    QVector4D mathParams;    // A, B, C, S
    QVector4D mathParams2;   // D, E, F
    float uMin = 0.0f, uMax = 0.0f, vMin = 0.0f, vMax = 0.0f;
    float xMin = -10.0f, xMax = 10.0f, yMin = -10.0f, yMax = 10.0f, zMin = -10.0f, zMax = 10.0f;

    // ==========================================================
    // ASPETTO (resa, colore, luce propria, guscio, bordo)
    // ==========================================================
    int renderMode = 0;
    // Indice della mesh su cui agiscono i controlli di aspetto; -1 = "All"
    // (stato globale). Vedi setActiveMeshPart.
    int activeMeshPart = -1;
    bool meshAppearanceBypass = false;
    // Vero in ambito "All": l'aspetto per-parte e' SOSPESO (ignorato dal render,
    // non cancellato), cosi' la superficie si disegna come una sola e i valori
    // per-mesh restano disponibili per quando si torna su "Mesh".
    bool meshAppearanceUniform = false;
    // True quando lo script ray marching definisce la direttiva Inner:=
    // (seconda superficie opaca interna, es. orizzonte di Kerr). Settato in
    // createImplicitFragmentShader() e inviato alla GPU via u_dummyZero.y.
    bool raymarchHasInner = false;
    bool textureEnabled = false;
    bool isSpecularEnabled = false;
    float alpha = 0.5f;
    float red = 1, green = 1, blue = 1;
    float lightIntensity = 1.0f;
    // 0.005 = il guscio storico della sfera unitaria (dove |grad| = 2).
    float shellThickness = 0.005f;
    // MARCHER: false = sphere tracing storico. Default conservativo, cosi' ogni
    // superficie che non chiede esplicitamente l'ibrido si disegna come sempre
    // (i record 3D esistenti, che con l'ibrido mostravano difetti sui bordi).
    // Il T^3 del Cross Section lo accende da loadCrossSectionDefaultSurface, e i
    // preset lo portano nella chiave "hybridMarcher".
    bool hybridMarcher = false;
    float tubeRadius = 0.1f;
    float borderRadius = 0.0f;
    QColor borderColor = QColor::fromRgbF(1.0f, 1.0f, 1.0f);
    int borderRenderMode = 0;
    int borderWireAlong = kBorderWireAlongDefault;
    int borderWireAround = kBorderWireAroundDefault;
    QString borderTexCode;
    bool borderTexEnabled = false;
    QColor borderTexCol1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
    QColor borderTexCol2 = QColor(Qt::black);
    float borderTexZoom = 1.0f;
    QVector2D borderTexPan = QVector2D(0.0f, 0.0f);
    float borderTexRotation = 0.0f;
    QString borderTexLibName;
    QString borderTexHint;
    float borderTexHintSeconds = 6.0f;
    bool borderTexAnimating = false;
    float borderTimeTex = 0.0f;
    float texRed1 = 1.0f, texGreen1 = 1.0f, texBlue1 = 1.0f;
    float texRed2 = 0.0f, texGreen2 = 0.0f, texBlue2 = 0.0f;

    // ==========================================================
    // TEXTURE DI SUPERFICIE E IMMAGINI DELLE FASCE
    // ==========================================================
    QRhiTexture *surfaceTexture = nullptr;
    // Percorso dell'immagine dell'utente in surfaceTexture (loadTextureFromFile);
    // vuoto se non ce n'e'. NON basta guardare surfaceTexture != nullptr:
    // MainWindow::generateTexture() ci mette anche la scacchiera di default,
    // disegnata su CPU, che e' un fallback e non un'immagine. Non vuoto governa
    // u_noImage, cioe' se gli script che campionano iChannel0 cadono sulla
    // scacchiera procedurale (colori vivi da u_col1/u_col2) o leggono la
    // texture. Era un bool: il percorso dice anche QUALE immagine e' a schermo
    // (vedi surfaceImagePath).
    QString surfaceImagePath;
    // IMMAGINI PROPRIE DELLE FASCE (multi-mesh, ambito Mesh). L'immagine di una
    // fascia e' quella nominata dal tag //IMG: nel suo script (textureCode),
    // come per la texture di superficie. Ogni fascia e' gia' una draw call col
    // suo blocco UBO: per darle un'immagine sua basta legarle, allo slot 1, la
    // sua texture al posto di quella della superficie -- stesso layout, nessun
    // campo nuovo nel blocco uniforme, nessuna pipeline in piu'.
    // Una texture per PERCORSO: la stessa immagine su piu' fasce si carica una
    // volta sola. Si creano e si distruggono nel frame (syncPartImages).
    struct PartImage {
        QRhiTexture *tex = nullptr;
        QRhiShaderResourceBindings *srb = nullptr;
        bool failed = false;   // file illeggibile: non si riprova a ogni frame
    };
    QHash<QString, PartImage> partImages;
    QImage pendingSurfaceImage;
    bool surfaceTextureNeedsUpload = false;
    // Richiesta di scarico della texture di superficie dalla GPU: la distruzione
    // dell'oggetto RHI va fatta DENTRO il render loop (non sul thread GUI), come
    // per l'upload. Senza questo, una texture caricata resta residente e ricompare
    // stantia cambiando superficie o riaccendendo il checkbox.
    bool surfaceTextureNeedsClear = false;
    QString customFragmentCode;
    QString textureCode;
    QString displacementCode;

    // ==========================================================
    // TRASFORMAZIONE 2D DELLA TEXTURE
    // ==========================================================
    // TRASFORMAZIONE 2D GLOBALE della texture di superficie (ambito "All", e
    // valore ereditato dalle parti che non ne hanno una propria).
    // Vive separata dai tre membri qui sopra perche' quelli fanno DUE lavori:
    // sono il buffer che il mouse muove in vista 2D, e finivano anche in
    // uboData come stato globale. Manipolando la texture di UNA mesh li si
    // sovrascriveva, e la trasformazione globale -- insieme a quella di ogni
    // fascia che la eredita -- veniva trascinata dietro.
    float globalTexZoom = 1.0f;
    float globalTexRotation = 0.0f;
    QVector2D globalTexPan;

    // ==========================================================
    // POSA E ROTAZIONI (i moti dell'oggetto)
    // ==========================================================
    QMatrix4x4 model;
    // POSIZIONE nella scena: il modello e' traslazione * rotazione, quindi
    // l'oggetto ruota attorno al proprio centro. Zero = come fino alla 1.4.
    QVector3D position;
    // FINESTRA DI COMPARSA (2.0, timeline, primo passo): l'oggetto e' in scena
    // fra windowIn e windowOut secondi dell'orologio di scena (GLWidget::
    // sceneTime). 0 = dall'inizio / fino alla fine. Fuori non si disegna e i
    // suoi moti restano fermi. L'ambiente e' sempre in scena.
    float windowIn = 0.0f;
    float windowOut = 0.0f;
    // Il TEMPO DEL SUO PATH (SurfaceEngine::compileObjectPath): un orologio
    // suo, che avanza mentre l'oggetto e' in scena, ha un path e pathRunning
    // (il master lo spegne e lo riaccende); si azzera col RESET (suo o della
    // scena).
    float pathTime = 0.0f;
    bool pathRunning = true;
    // SCALA uniforme dell'oggetto (2.0): modello = traslazione * rotazione *
    // scala. 1 = come fino alla 1.4. La cambiano rotella, pinch, Back/Forward
    // con l'oggetto selezionato (GLWidget::zoomCamera) e il dock Objects.
    float scale = 1.0f;
    QQuaternion rotationQuat;
    // true se l'utente ha ruotato la superficie a mano (mouse/touch) dopo l'ultima
    // rotazione "di default" (setRotationQuat). Governa neutralizeDefaultRotationForPath.
    bool userRotatedManually = false;
    float nutation = 0, precession = 0, spin = 0;
    float omega = 0, phi = 0, psi = 0;
    float nutationSpeed = 0, precessionSpeed = 0, spinSpeed = 0;
    float omegaSpeed = 0, phiSpeed = 0, psiSpeed = 0;
    // Il GO dell'oggetto: le sue rotazioni avanzano (alle velocita' qui sopra)
    // a ogni tick di rotationTimer, che gira finche' almeno un oggetto ruota.
    bool rotating = false;

    // ==========================================================
    // OROLOGI (geometria e texture; lo sfondo e' della vista)
    // ==========================================================
    // Ciascuno E' il tempo che il suo modulo mostra (piu' kClockOrigin), e lo
    // avanza solo GLWidget::advanceClocksBy (vedi il commento a m_timeBg).
    bool surfaceAnimating = false;
    float timeGeom = 0.0f;
    float timeTex = 0.0f;
    bool texAnimating = false;

    // ==========================================================
    // RISORSE QRHI DELLA SUPERFICIE
    // ==========================================================
    QRhiBuffer *vbo = nullptr;
    QRhiBuffer *ibo = nullptr;
    QRhiBuffer *ubo = nullptr;
    QRhiShaderResourceBindings *bindings = nullptr;
    QRhiBuffer *wireframeIbo = nullptr;
    // Bordo: buffer a parte, riempiti insieme alla mesh (vedi
    // SurfaceEngine::generateBorder).
    QRhiBuffer *borderVbo = nullptr;
    QRhiBuffer *borderIbo = nullptr;
    QRhiBuffer *borderWireIbo = nullptr;
    bool borderUploaded = false;
    QRhiGraphicsPipeline *wireframePipeline = nullptr;
    std::vector<unsigned int> wireframeIndices;
    bool wireframeNeedsUpdate = true;
    int wireframeIndexCount = 0;
    // Un intervallo per parte di mesh: il wireframe condivide il VBO della
    // superficie (indici assoluti) ma gli uniform del dominio sono per-parte.
    std::vector<WireframeRange> wireframeRanges;
    QRhiGraphicsPipeline *pipelineOpaque = nullptr;
    QRhiGraphicsPipeline *pipelineTranspBack = nullptr;
    QRhiGraphicsPipeline *pipelineTranspFront = nullptr;
    // BORDO: pipeline proprie, opaca e a linee, con lo stato di pipelineOpaque
    // e wireframePipeline ma col fragment del bordo (HAS_BORDER): solo lui
    // contiene il discard dei tratti coperti. Nel fragment della superficie un
    // discard, anche mai eseguito, spegne lo scarto anticipato dei frammenti
    // nascosti sulle GPU a tessere (HSR di Apple, FPK di Mali, LRZ di Adreno):
    // gli strati opachi coperti si colorano lo stesso. Nulle col bordo spento;
    // se il loro fragment non compila, il bordo si disegna con le pipeline della
    // superficie, senza il taglio dei tratti coperti.
    QRhiGraphicsPipeline *borderPipelineOpaque = nullptr;
    QRhiGraphicsPipeline *borderPipelineWire = nullptr;

    // ==========================================================
    // MULTI-MESH: UBO AD ARRAY + DYNAMIC OFFSET (vedi GLWidget)
    // ==========================================================
    // uboBlockStride e' allineato con QRhi::ubufAlignment(): NON e'
    // sizeof(UboData) (su Metal/Vulkan l'allineamento minimo puo' essere 256).
    quint32 uboBlockStride = 0;
    int uboBlockCapacity = 0;   // quante parti entrano nel buffer attuale
    // Binding con dynamic offset per la superficie multi-parte. Restano separati
    // da bindings: quello serve ai percorsi che disegnano a offset 0 (sfondo
    // flat, ray marching) e non deve cambiare comportamento.
    QRhiShaderResourceBindings *bindingsDyn = nullptr;
    QRhiTexture *bindingsDynTexture = nullptr;  // texture con cui e' stato creato

    // ==========================================================
    // TUBI: SEZIONE TRASPORTATA (vedi GLWidget::createTubeFrameResources)
    // ==========================================================
    QRhiTexture *tubeFrameTex = nullptr;
    QRhiSampler *tubeFrameSampler = nullptr;
    // Lavoro del compute e stato fra un frame e l'altro: per ogni anello
    // tangente, direzione locale e sezione mostrata al frame prima; per ogni
    // tubo partenza e angolo di chiusura; in coda i campioni fitti di ogni
    // tubo (tangente e svolta cumulata). Lo stato si azzera quando
    // la pipeline viene rifatta (curva nuova) o cambiano tubi e anelli
    // (tubeFrameLayout): tubeFrameStateReset, consumato al frame dopo.
    QRhiBuffer *tubeFrameWork = nullptr;
    bool tubeFrameStateReset = true;
    quint64 tubeFrameLayout = 0;
    // Passo dell'ultimo frame, da advanceClocksBy (live o REC): da qui lo
    // smorzamento degli scatti della sezione (u_tubeFrameBlend).
    float tubeFrameDt = 0.0f;
    QRhiShaderResourceBindings *tubeFrameSrb = nullptr;
    QRhiComputePipeline *tubeFramePipeline = nullptr;

    // ==========================================================
    // BORDO: LATI DELLA MESH CUSTOM (vedi GLWidget::setBorderEdges)
    // ==========================================================
    QRhiTexture *borderEdgeTex = nullptr;
    QRhiSampler *borderEdgeSampler = nullptr;
    std::vector<float> borderEdgeData;   // larghezza borderEdgeWidth, kBorderEdgeRows righe
    int borderEdgeWidth = 0;
    bool borderEdgeDirty = false;

    // ==========================================================
    // BORDO: LATI COPERTI (vedi GLWidget::buildSharedEdgePipeline)
    // ==========================================================
    QRhiTexture *sharedEdgeTex = nullptr;
    QRhiSampler *sharedEdgeSampler = nullptr;
    QRhiBuffer *sharedEdgeParts = nullptr;
    QRhiBuffer *sharedEdgeSamples = nullptr;
    QRhiBuffer *sharedEdgeTiles = nullptr;
    // I flag grezzi della terza passata, uno per campione dei lati: la quarta
    // li ripulisce (tratti corti) e scrive sharedEdgeTex.
    QRhiBuffer *sharedEdgeRaw = nullptr;
    std::vector<QVector4D> sharedEdgeTable;   // l'ultima tabella caricata
    QByteArray sharedEdgeKey;                 // la geometria dell'ultima passata
    QRhiShaderResourceBindings *sharedEdgeSrb = nullptr;
    QRhiComputePipeline *sharedEdgePipeline = nullptr;
    // Il vertex da cui costruire il compute (vuoto se non si puo': tubo, mesh
    // custom, bordo spento) e se va ancora costruito.
    QString sharedEdgeSource;
    bool sharedEdgeSourceDirty = false;
    // Solo per gli scenari: spegne la passata, per confrontare col test del vertex.
    bool sharedEdgesOff = false;
    // Solo per gli scenari (border-audit): nel prossimo frame con la passata
    // attiva si rileggono dalla GPU i flag dei lati coperti (RGBA8,
    // kSharedEdgeSamples + 1 colonne, riga = mesh * 4 + lato) e i campioni
    // delle parti (kSharedEdgePartVec4 vec4 per parte). Ogni lettura finita
    // incrementa sharedEdgeReadbacks.
    bool sharedEdgeReadbackRequested = false;
    QRhiReadbackResult sharedEdgeFlagsResult;
    QRhiReadbackResult sharedEdgeSamplesResult;
    int sharedEdgeReadbacks = 0;

    // ==========================================================
    // BORDO: SONDA (vedi GLWidget::borderAvailability)
    // ==========================================================
    // SONDA (vedi borderAvailability), a bordo spento e acceso: la geometria
    // (tabella delle parti, costanti, vincoli; il tempo no) dell'ultima sonda,
    // quella in attesa che si fermi (borderProbeTimer: caricamento, slider
    // trascinato) e, a timer scaduto, il via per il prossimo frame. La lettura
    // dei flag e' asincrona: finishBorderProbe la scarta se nel frattempo il
    // vertex e' cambiato (borderProbeSerial).
    QByteArray borderProbeKey;
    QByteArray borderProbePending;
    QTimer *borderProbeTimer = nullptr;
    bool borderProbeArmed = false;
    bool borderProbeReading = false;
    int borderProbeSerial = 0;
    int borderProbeReadSerial = 0;
    std::vector<int> borderProbeMeshes;   // indice di mesh di ogni parte, alla lettura
    // Su una superficie animata la sonda guarda piu' istanti di t in un colpo
    // solo (vedi render): letture e arrivi.
    static constexpr int kBorderProbeInstants = 8;
    static constexpr float kBorderProbeStep = 0.83f;   // passo in t, non una frazione tonda di pi
    QRhiReadbackResult borderProbeResults[kBorderProbeInstants];
    int borderProbeInstantsRead = 1;
    int borderProbeArrived = 0;
    // Superficie animata che si apre in qualche istante ma ora e' chiusa: si
    // guarda l'istante corrente ogni kBorderProbeWatchMs finche' non si apre,
    // poi Border resta acceso (scelta dell'utente, 10-17).
    static constexpr int kBorderProbeWatchMs = 250;
    bool borderProbeWatch = false;
    QElapsedTimer borderProbeWatchClock;
    BorderAvailability borderAvailability = BorderAvailability::Unknown;

    // ==========================================================
    // RAY MARCHING
    // ==========================================================
    QRhiGraphicsPipeline *pipelineImplicit = nullptr;
    // La stessa pipeline SENZA scrittura della profondita', per il ray marching
    // trasparente (vedi implicitPipelineFor): costruita al primo uso dagli
    // shader di pipelineImplicit, e rifatta quando questa cambia
    // (implicitPipelineGen e' la generazione da cui viene).
    QRhiGraphicsPipeline *pipelineImplicitSeeThrough = nullptr;
    QShader implicitVs, implicitFs;
    int implicitPipelineGen = 0;
    int seeThroughGen = -1;
    QRhiShaderResourceBindings *bindingsImplicit = nullptr;

};

#endif // SURFACEOBJECT_H
