#include "glwidget.h"
#include "geometrybuilder.h"
#include "inputhandler.h"
#include "surfaceengine.h"
#include "glsltranslator.h"
#include "expressionparser.h"

#include <QTimer>
#include <cmath>
#include <vector>
#include <limits>
#include <QtMath>
#include <QMouseEvent>
#include <algorithm>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QPainter>
#include <QLinearGradient>
#include <QDebug>
#include <cstddef>
#include <cstring>

// Diagnostica del "triangolo-artefatto" geodetico: 1 = ispeziona la mesh FINALE
// (vertices+indices effettivamente inviati alla GPU) in setCustomMesh e logga i
// triangoli con un lato spropositato rispetto alla mediana + il numero di
// sentinelle (0,0,0,0) intercettate. Puramente osservativo, NON altera la mesh.
// Servì a localizzare il triangolo (vertice grid[0][0]=(0,0,0) triangolato su
// Mali, intermittente). Lasciato a 0; rimettere a 1 se ricompare un artefatto.
#define GEO_DIAG_TRIANGLE 0
#include <rhi/qrhi.h>

#define STEP_MIN 1
#define STEP_MAX 50
// Densità wireframe di default (deve coincidere con l'init di wfStepU/wfStepV in glwidget.h).
#define STEP_DEF 4

// MULTI-MESH: blocchi UBO pre-allocati all'avvio. Le superfici a mesh singola ne
// usano 1; il buffer cresce da solo se uno script dichiara piu' parti.
static const int kInitialUboBlocks = 8;

QString loadShaderSource(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Errore caricamento shader:" << path;
        return "";
    }
    QTextStream in(&file);
    return in.readAll();
}

GLWidget::GLWidget(QWidget *parent)
    : QRhiWidget(parent)
{
    m_uboData = UboData();

    m_uboData.x_min = -10.0f;
    m_uboData.x_max =  10.0f;
    m_uboData.y_min = -10.0f;
    m_uboData.y_max =  10.0f;
    m_uboData.z_min = -10.0f;
    m_uboData.z_max =  10.0f;

    // --- 1. SETUP LOGICA APPLICATIVA E INPUT ---
    m_inputHandler = std::make_unique<InputHandler>(this);
    engine = std::make_unique<SurfaceEngine>();

    // --- 2. INIZIALIZZAZIONE VARIABILI DI STATO ---
    m_flatPan = QVector2D(0.0f, 0.0f);
    m_flatZoom = 1.0f;
    m_flatRotation = 0.0f;
    m_rotationQuat = QQuaternion();

    m_elapsedTimer.start();
    m_surfaceTimer.start();

    m_surfaceAnimating = false;
    m_manualTime = 0.00001f;

    m_textureEnabled = false;
    nutationSpeed = precessionSpeed = spinSpeed = 0.0f;
    omegaSpeed = phiSpeed = psiSpeed = 0.0f;
    precession = 0.0f; nutation = 0.0f; spin = 0.0f;
    red = green = blue = 0.5f; alpha = 1.0f;

    m_cameraPos = QVector3D(0.0f, 0.0f, 4.0f);
    m_cameraYaw = 0.0f;
    m_cameraPitch = 0.0f;
    m_cameraRoll = 0.0f;

    m_pathTarget = QVector3D(0.0f, 0.0f, 0.0f);
    m_pathUp = QVector3D(0.0f, 0.0f, 1.0f);

    // --- 3. ATTRIBUTI DEL WIDGET ---
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);

    // --- 4. TIMERS ---
    rotationTimer = new QTimer(this);
    rotationTimer->setInterval(kRotationTickMs);
    connect(rotationTimer, &QTimer::timeout, this, &GLWidget::updateRotation);
    rotationTimer->stop();

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(33); // ~30 FPS

    // In RHI, chiamare update() farà scattare automaticamente l'evento render()
    connect(m_animTimer, &QTimer::timeout, this, QOverload<>::of(&GLWidget::update));
}

GLWidget::~GLWidget()
{
}


// ==========================================================
// PROTECTED
// ==========================================================

// ==========================================================
// MULTI-MESH: gestione dell'UBO ad array
// ==========================================================

// Garantisce che l'UBO contenga almeno partCount blocchi. Ritorna true se il
// buffer e' stato (ri)creato: in quel caso i binding che lo referenziano puntano
// a memoria liberata e vanno riagganciati (stessa trappola gia' documentata per
// m_surfaceTexture nel render).
bool GLWidget::ensureUboCapacity(int partCount)
{
    if (!rhi()) return false;

    const int wanted = std::max(1, partCount);
    if (m_ubo && wanted <= m_uboBlockCapacity) return false;

    // Cresce con margine per non ricreare il buffer a ogni parte in piu'.
    const int newCap = std::max(wanted, m_uboBlockCapacity * 2);

    if (m_ubo) {
        m_ubo->destroy();
        delete m_ubo;
        m_ubo = nullptr;
    }

    m_ubo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                             m_uboBlockStride * newCap);
    m_ubo->create();
    m_uboBlockCapacity = newCap;

    // Riaggancia i binding al NUOVO buffer senza distruggere l'oggetto SRB: le
    // pipeline ne tengono il puntatore, quindi un delete+new le lascerebbe con
    // un riferimento pendente. Il layout non cambia, cambia solo la risorsa.
    if (m_bindingsDyn) {
        m_bindingsDyn->setBindings({
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_ubo, sizeof(UboData)),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                m_bindingsDynTexture ? m_bindingsDynTexture : m_dummyTexture, m_sampler)
        });
        m_bindingsDyn->updateResources();
    }

    // Anche i binding statici (sfondo flat, ray marching) puntavano al vecchio UBO.
    if (m_bindings) {
        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                m_surfaceTexture ? m_surfaceTexture : m_dummyTexture, m_sampler)
        });
        m_bindings->updateResources();
    }

    return true;
}

// Binding della superficie con dynamic offset sul blocco UBO. La dimensione
// dichiarata e' sizeof(UboData) (la finestra visibile su un blocco), non lo
// stride fra blocchi.
void GLWidget::ensureDynamicBindings(QRhiTexture *tex)
{
    if (!rhi() || !m_ubo) return;
    if (!tex) tex = m_dummyTexture;
    if (!tex || !m_sampler) return;

    // Niente da fare se il binding esiste gia' con la stessa texture.
    if (m_bindingsDyn && m_bindingsDynTexture == tex) return;

    const bool isNew = (m_bindingsDyn == nullptr);
    if (isNew) {
        m_bindingsDyn = rhi()->newShaderResourceBindings();
    }

    m_bindingsDyn->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubo, sizeof(UboData)),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, tex, m_sampler)
    });

    if (isNew) {
        m_bindingsDyn->create();
    } else {
        // IMPORTANTE: updateResources() e non create(). Le pipeline tengono il
        // PUNTATORE a questo oggetto: ricrearlo (delete + new) le lascerebbe con
        // un puntatore pendente e obbligherebbe a ricostruirle tutte. Qui il
        // layout non cambia (stessi tipi/binding), solo la risorsa allo slot 1.
        m_bindingsDyn->updateResources();
    }

    m_bindingsDynTexture = tex;
}

void GLWidget::initialize(QRhiCommandBuffer *cb)
{
    if (m_ubo) {
        return;
    }

    // --- 1. CREAZIONE UBO (Uniform Buffer Object) ---
    // MULTI-MESH: l'UBO non contiene piu' un blocco solo ma un ARRAY di blocchi,
    // uno per parte di mesh (i limiti u_min/u_max/v_min/v_max e u_meshIndex sono
    // per-parte). Ogni draw call seleziona il suo blocco con un dynamic offset.
    // Lo stride e' allineato secondo l'API grafica: ubufAlignment() vale 256 su
    // Metal/Vulkan, quindi NON si puo' usare sizeof(UboData) come passo.
    m_uboBlockStride = rhi()->ubufAlignment();
    while (m_uboBlockStride < sizeof(UboData)) {
        m_uboBlockStride += rhi()->ubufAlignment();
    }
    ensureUboCapacity(kInitialUboBlocks);

    // --- 2. CREAZIONE VBO E IBO ---
    // In RHI è meglio pre-allocare un buffer grande per evitare di ricrearlo a ogni frame.
    // 500.000 vertici sono un limite di sicurezza abbondante per la tua griglia.
    int maxVertexSize = 500000 * sizeof(Vertex);
    int maxIndexSize = 1000000 * sizeof(unsigned int);

    m_vbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, maxVertexSize);
    m_vbo->create();

    m_ibo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, maxIndexSize);
    m_ibo->create();

    m_wireframeIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, maxIndexSize);
    m_wireframeIbo->create();

    // --- 3. CREAZIONE TEXTURE E BINDINGS ---
    // A. Creiamo la texture "tappabuchi"
    createDummyTexture();

    // B. Colleghiamo l'UBO al binding 0 e la TEXTURE al binding 1
    m_bindings = rhi()->newShaderResourceBindings();
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
                                                 QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                 m_ubo),
        QRhiShaderResourceBinding::sampledTexture(1,
                                                  QRhiShaderResourceBinding::FragmentStage,
                                                  m_dummyTexture, m_sampler)
    });
    m_bindings->create();

    // C. Binding della superficie con dynamic offset (multi-mesh). Va creato
    // PRIMA di buildPipeline(): le pipeline parametriche si costruiscono su
    // questo layout, e il layout dichiarato dalla pipeline deve combaciare con
    // quello passato a setShaderResources al draw.
    ensureDynamicBindings(m_dummyTexture);

    // Inizializzazioni di base dell'engine che avevi prima
    if (m_eqX.isEmpty()) {
        m_eqX = "sin(u)"; m_eqY = "cos(u)"; m_eqZ = "v"; m_eqW = "0";
        engine->setEquations(m_eqX, m_eqY, m_eqZ, m_eqW);
    }

    buildPipeline();
}

void GLWidget::render(QRhiCommandBuffer *cb)
{
    // ==========================================================
    // WATCHDOG DI PERFORMANCE (avviso da rallentamento)
    // ==========================================================
    // Misuriamo l'intervallo tra frame consecutivi: quando un'animazione e'
    // attiva ma il throughput resta sotto ~13 fps abbastanza a lungo, il carico
    // GPU e' eccessivo e avvisiamo l'utente (una sola volta). Non modifichiamo la
    // qualita' (l'adaptive su raySteps fu provata e rimossa perche' peggiorava):
    // qui solo avviso. Si misura SOLO con animazione attiva, altrimenti gli
    // intervalli non rappresentano un throughput continuo.
    {
        // Durante l'esportazione video il rendering e' frame-by-frame (cattura +
        // scrittura su disco tra i frame): gli intervalli sono lentissimi per
        // costruzione, non per carico GPU. Saltiamo il watchdog per non dare un
        // falso avviso "il rendering rallenta" durante la registrazione.
        const bool animating = !m_isRecording &&
                               (isAnimating() || m_surfaceAnimating || m_pathAnimating);
        if (animating) {
            // Il PRIMO frame dopo l'avvio (o ri-avvio) dell'animazione non e' una
            // misura valida: l'intervallo dal frame precedente include il tempo da
            // FERMI (click di start, pausa UI), non il costo GPU. Misurarlo dava un
            // dtMs enorme che inquinava l'EMA e faceva scattare un FALSO avviso
            // proprio allo start/stop di preset scorrevoli. Lo saltiamo.
            const bool justResumed = !m_wasAnimating;
            // Finestra di grazia post-export: entro kPerfGraceMs dal ritorno live
            // (m_perfGraceClock avviato in setRecordingActive(false)) scartiamo le
            // misure — il transitorio di ripristino puo' durare piu' frame lenti
            // su mobile e NON e' carico GPU. Scaduta la finestra, la fermiamo
            // (invalidate) cosi' non ricade mai piu' in questo ramo.
            constexpr qint64 kPerfGraceMs = 500;
            const bool inGrace = m_perfGraceClock.isValid() &&
                                 m_perfGraceClock.elapsed() < kPerfGraceMs;
            if (!inGrace && m_perfGraceClock.isValid()) m_perfGraceClock.invalidate();
            if (m_frameClock.isValid() && !justResumed && !inGrace) {
                float dtMs = (float)m_frameClock.nsecsElapsed() / 1.0e6f;

                // Soglia per piattaforma: su Android il watchdog del driver GPU
                // puo' uccidere l'app prima dell'avviso, quindi avvisiamo PRIMA
                // (2 fps); su desktop, dove non c'e' kill, scendiamo a ~1.5 fps.
#if defined(Q_OS_ANDROID)
                constexpr float kSlowFrameMs = 500.0f;  // ~2 fps
#else
                constexpr float kSlowFrameMs = 667.0f;  // ~1.5 fps
#endif
                constexpr float kSlowDwellMs = 600.0f;  // sostenuto per >0.6 s
                constexpr int   kSlowRunToWarn = 2;     // campioni lenti consecutivi minimi
                                                        // per avvisare: e' il guard che
                                                        // conserva l'immunita' ai picchi
                                                        // isolati con l'EMA asimmetrica
                // Oltre questo, un dtMs NON e' piu' classificabile come frame lento
                // vs. buco da inattivita' guardando il solo valore: serve il contesto
                // (isolato vs. sequenza). Vedi ramo "frame enorme" sotto.
                constexpr float kHugeFrameMs   = 2000.0f;
                constexpr int   kHugeRunToWarn = 3;      // N frame enormi consecutivi
                                                         // = collasso reale, non un buco.
                                                         // 3 (non 2): il ritorno da
                                                         // app-in-background produce 1-2
                                                         // frame enormi (ricompilazione/
                                                         // riscaldamento), non 3+ di fila.

                if (dtMs <= kHugeFrameMs) {
                    m_hugeFrameRun = 0;  // frame "normale": interrompe una sequenza enorme
                    m_slowFrameRun = (dtMs > kSlowFrameMs) ? m_slowFrameRun + 1 : 0;

                    // EMA ASIMMETRICA: in salita (campione sopra soglia) peso 0.35,
                    // cosi' un rallentamento vero viene agganciato in 2-3 frame e
                    // l'avviso precede (o quasi) gli artefatti da budget GPU
                    // (magenta/tremolii), invece di arrivare dopo ~5 s di media
                    // pigra; in discesa resta 0.15 (recupero morbido). ATTENZIONE:
                    // con 0.35 un SINGOLO picco fin quasi a 2 s porta l'EMA oltre
                    // soglia da solo (0.35*2000 = 700 > kSlowFrameMs): l'immunita'
                    // ai picchi isolati non la da' piu' l'aritmetica dell'EMA
                    // (0.15*2000 = 300 la garantiva), la da' m_slowFrameRun nella
                    // condizione di avviso (>= kSlowRunToWarn campioni lenti DI FILA).
                    const float emaW = (dtMs > kSlowFrameMs) ? 0.35f : 0.15f;
                    m_avgFrameMs = (1.0f - emaW) * m_avgFrameMs + emaW * dtMs;

                    if (m_avgFrameMs > kSlowFrameMs) {
                        m_slowAccumMs += dtMs;
                        // UN SOLO avviso per scena: dopo il primo, m_perfWarnDismissed
                        // (settato dal connect via acknowledgePerformanceWarning) zittisce
                        // TUTTO — anche un ulteriore peggioramento — finche' non cambiano
                        // le impostazioni. Il riarmo avviene SOLO al reale cambio
                        // scena/effetti, in rebuildShader() (azzera dismissed e level).
                        // Ne' lo stop/riavvio dei moti ne' l'oscillazione attorno alla
                        // soglia riarmano: e' cio' che elimina il vecchio popup "random".
                        // firstTime resta la condizione del PRIMO avviso di una scena.
                        const bool firstTime = (m_perfWarnLevelMs <= 0.0f);
                        if (!m_perfWarnDismissed && m_slowAccumMs >= kSlowDwellMs
                            && m_slowFrameRun >= kSlowRunToWarn && firstTime) {
                            m_perfWarnLevelMs = m_avgFrameMs;  // livello mostrato
                            emit performanceWarning();
                        }
                    } else {
                        // Tornati sopra soglia (fluidi): svuotiamo solo l'accumulo
                        // di dwell. NON azzeriamo m_perfWarnLevelMs qui: una scena
                        // che OSCILLA intorno alla soglia (fluida->lenta->fluida)
                        // non deve riavvisare a ogni ricaduta. Il guard
                        // !m_perfWarnDismissed sopra gia' zittisce dopo il primo
                        // avviso; il livello si riarma solo al reale cambio scena
                        // (rebuildShader azzera level e dismissed insieme).
                        m_slowAccumMs = 0.0f;
                    }
                } else {
                    // dtMs ENORME (> 2 s). Un frame isolato di questa entita' e' un
                    // buco da inattivita' (timer in pausa, finestra nascosta) e va
                    // scartato — NON inquiniamo l'EMA con 5000 ms. MA una SEQUENZA di
                    // frame enormi consecutivi NON e' inattivita': e' un collasso GPU
                    // reale (caso iPad: RM + trasparenza a ~0.2 fps, ogni frame ~5 s).
                    // Su desktop lo stesso record rende frame < 2 s e cade nel ramo
                    // sopra, quindi li' l'avviso scattava gia'; su iPad TUTTI i frame
                    // sforavano i 2 s e venivano scartati -> il watchdog restava muto.
                    // Contiamo i consecutivi: al kHugeRunToWarn-esimo, e' collasso.
                    // Un frame enorme e' comunque un campione LENTO: alimenta anche
                    // m_slowFrameRun, cosi' le sequenze miste (1.8 s, 2.5 s, 1.8 s...)
                    // non azzerano l'evidenza del ramo EMA a ogni sforamento dei 2 s.
                    ++m_slowFrameRun;
                    if (++m_hugeFrameRun >= kHugeRunToWarn) {
                        const bool firstTime = (m_perfWarnLevelMs <= 0.0f);
                        if (!m_perfWarnDismissed && firstTime) {
                            m_perfWarnLevelMs = dtMs;  // livello indicativo (frame enorme)
                            emit performanceWarning();
                        }
                    }
                } // chiude: if (dtMs <= kHugeFrameMs) / else
            }
            m_wasAnimating = true;
        } else {
            // Animazione ferma: azzeriamo SOLO gli accumulatori di misura, cosi'
            // la prossima animazione riparte da uno stato pulito. NON riarmiamo
            // m_perfWarnDismissed: fermare/riavviare la STESSA scena (o l'export
            // che ferma+riavvia i timer) non deve far ricomparire il popup. Il
            // riarmo avviene solo su reale cambio scena/effetti, in
            // rebuildShader(). ("Un avviso per scena finche' non cambiano le
            // impostazioni.")
            m_slowAccumMs = 0.0f;
            m_avgFrameMs = 16.0f;
            m_perfWarnLevelMs = 0.0f;
            m_slowFrameRun = 0;
            m_hugeFrameRun = 0;
            m_wasAnimating = false;
        }
        m_frameClock.restart();
    }

    // ==========================================================
    // PARTE COMUNE (SEMPRE IN ESECUZIONE)
    // ==========================================================
    QRhiResourceUpdateBatch *resourceUpdates = rhi()->nextResourceUpdateBatch();

    // --- CALCOLO MATRICI E TELECAMERA ---
    QSize outputSize = renderTarget()->pixelSize();
    float aspect = (float)outputSize.width() / (float)(outputSize.height() > 0 ? outputSize.height() : 1);

    QMatrix4x4 mvp;
    QMatrix4x4 mv;

    if (m_isFlatView) {
        m_projection.setToIdentity();
        m_view.setToIdentity();
        m_model.setToIdentity();
        mvp = rhi()->clipSpaceCorrMatrix();
        mv.setToIdentity();
    } else {
        m_projection.setToIdentity();
        // Piani near/far scalati con la distanza della camera. Un far fisso a
        // 100 tagliava la parte lontana di oggetti grandi quando si rimpicciolisce
        // (la camera arretra oltre i 100). Scalando NEAR e FAR insieme il rapporto
        // far/near resta costante (~10000, come l'originale 0.01:100), quindi la
        // precisione del depth buffer non peggiora (niente z-fighting/appiattimento)
        // e a camDist=4 si riottengono esattamente i valori storici.
        float camDist = m_cameraPos.length();
        if (camDist < 0.1f) camDist = 4.0f;
        const float nearPlane = camDist * 0.0025f;
        const float farPlane  = camDist * 25.0f;
        if (projectionMode == Ortho4D) {
            float halfHeight = camDist * std::tan(m_cameraFov * 0.5f * M_PI / 180.0f);
            float halfWidth = halfHeight * aspect;
            // NEAR SIMMETRICO IN ORTOGONALE (-far invece di nearPlane).
            // In prospettiva la camera e' un punto e il cono si stringe verso di
            // lei: cio' che sta prima del near e' poco o fuori campo. In
            // ortogonale i raggi sono PARALLELI e il volume e' un box a sezione
            // costante: tutto cio' che sta prima del near viene tagliato in
            // pieno. Con una camera vicina alla superficie (i record di path la
            // salvano DENTRO l'oggetto: Klein3D Racing a distanza 1.06) meta'
            // figura ha z negativo e spariva, mentre lo stesso preset caricato
            // dal ramo Surface (camera a 4.0) si vedeva intero.
            // Estendendo il box all'indietro fino a -farPlane la camera si
            // comporta come una lastra: nessun taglio, da qualunque distanza.
            //
            // COSTO SUL DEPTH BUFFER: in ortogonale z_ndc e' LINEARE in z_eye,
            // quindi conta solo l'AMPIEZZA (far-near), non il rapporto far/near
            // che governa il caso prospettico citato qui sopra. Raddoppiando
            // l'ampiezza la risoluzione si dimezza: a camDist=4 si passa da
            // 6.0e-06 a 1.2e-05 unita' per livello su depth a 24 bit. Margine
            // ampio; il ramo prospettico resta invariato.
            m_projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -farPlane, farPlane);
        } else {
            m_projection.perspective(m_cameraFov, aspect, nearPlane, farPlane);
        }

        m_view.setToIdentity();
        if (m_isPathFollowing) {
            m_view.lookAt(m_cameraPos, m_pathTarget, m_pathUp);
            if (m_pathRoll != 0.0f) {
                QMatrix4x4 rollMat;
                rollMat.rotate(m_pathRoll, 0.0f, 0.0f, 1.0f);
                m_view = rollMat * m_view;
            }
        } else {
            float radYaw = m_cameraYaw * M_PI / 180.0f;
            float radPitch = m_cameraPitch * M_PI / 180.0f;
            QVector3D front(std::sin(radYaw)*std::cos(radPitch), std::sin(radPitch), -std::cos(radYaw)*std::cos(radPitch));
            QMatrix4x4 rollMat;
            rollMat.rotate(m_cameraRoll, 0.0f, 0.0f, 1.0f);
            QVector3D up = rollMat.map(QVector3D(0.0f, 1.0f, 0.0f));
            m_view.lookAt(m_cameraPos, m_cameraPos + front, up);
        }

        m_model.setToIdentity();
        m_model.rotate(m_rotationQuat);

        mvp = rhi()->clipSpaceCorrMatrix() * m_projection * m_view * m_model;
        mv = m_view * m_model;
    }

    // --- 3. COMPILAZIONE UNIFORM BUFFER (UBO) ---
    memcpy(m_uboData.mvpMatrix, mvp.constData(), 64);
    memcpy(m_uboData.mvMatrix, mv.constData(), 64);
    memcpy(m_uboData.mMatrix, m_model.constData(), 64);

    m_uboData.dummyZero = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    m_uboData.observerPos = m_observerPos;
    m_uboData.cameraPos4D = m_cameraPos4D;
    m_uboData.omega = omega;
    m_uboData.phi = phi;
    m_uboData.psi = psi;

    // 1. CALCOLO DEL DELTA TIME (dt) GLOBALE
    float currentRealTime = (float)m_surfaceTimer.elapsed() / 1000.0f;
    float dt = currentRealTime - m_lastRealTime;
    if (dt < 0) dt = 0; // Protezione al riavvio del timer
    m_lastRealTime = currentRealTime;

    // 2. GESTIONE TEMPO: LIVE vs VIDEO RECORDER
    QVariant useVirtualTimeVar = property("use_virtual_time");
    const bool useVirtualTime = useVirtualTimeVar.isValid() && useVirtualTimeVar.toBool();
    if (useVirtualTime) {
        // Se stiamo registrando, il tempo del recorder guida SOLO i moduli col
        // clock attivo: un modulo FERMATO dall'utente resta congelato al tempo
        // fotografato da beginVirtualTimeFreeze (vedi sotto), come a schermo.
        // Prima vTime era forzato incondizionatamente su tutti e tre: nel video
        // la texture (o la geometria) ferma ripartiva ad animarsi.
        float vTime = property("virtual_time").toFloat();
        if (m_surfaceAnimating) m_timeGeom = vTime;
        if (m_texAnimating)     m_timeTex  = vTime;
        if (m_bgAnimating)      m_timeBg   = vTime;
    } else {
        // App in uso normale: ogni orologio avanza solo se la sua parte è "attiva"
        // Se corrono insieme, avanzano dello stesso identico 'dt', restando in sincrono!
        if (m_surfaceAnimating) m_timeGeom += dt;
        if (m_texAnimating)     m_timeTex  += dt;
        if (m_bgAnimating)      m_timeBg   += dt;
    }

    // 3. INVIO DEI DATI ALLA GPU
    // In registrazione m_manualTime avanza per TUTTI (setShaderTime dal loop del
    // recorder): per i moduli fermi va neutralizzato col tempo totale congelato,
    // altrimenti animerebbero comunque via m_manualTime.
    const bool vtFreezeGeom = useVirtualTime && m_vtFreezeValid && !m_surfaceAnimating;
    const bool vtFreezeTex  = useVirtualTime && m_vtFreezeValid && !m_texAnimating;
    const bool vtFreezeBg   = useVirtualTime && m_vtFreezeValid && !m_bgAnimating;
    m_uboData.time = vtFreezeGeom ? m_vtFrozenGeom : m_manualTime + m_timeGeom;

    // Usiamo la coordinata X di dummyZero per inviare il tempo specifico della Texture
    m_uboData.dummyZero.setX(vtFreezeTex ? m_vtFrozenTex : m_manualTime + m_timeTex);

    // .y = flag "seconda superficie interna" (Inner:= nello script ray marching).
    // .x resta l'orologio texture; .y/.z/.w erano liberi (azzerati a inizio frame).
    m_uboData.dummyZero.setY(m_raymarchHasInner ? 1.0f : 0.0f);
    m_uboData.projMode = projectionMode;
    m_uboData.renderMode = renderMode;
    m_uboData.lightingMode = is4DActive() ? m_lightingMode4D : 0;
    m_uboData.useSpecular = m_isSpecularEnabled ? 1 : 0;

    m_uboData.isFlat = m_isFlatView ? 1 : 0;
    // TRASFORMAZIONE 2D GLOBALE (non il buffer di lavoro della vista 2D): e' la
    // base di OGNI blocco parte, quindi ci va lo stato globale, o l'inquadratura
    // che il mouse sta dando alla fascia in editing si propagherebbe a tutte le
    // fasce che ereditano e all'ambito "All".
    // In vista 2D sulla SUPERFICIE fa eccezione cio' che si sta editando: li' il
    // fragment deve seguire il mouse.
    //  - con una FASCIA selezionata ci pensa il ramo editingThisPart del loop
    //    per-parte piu' sotto, che scrive il buffer nel blocco di quella parte;
    //  - con una mesh SOLA (nessuna parte dichiarata) quel loop non esiste;
    //  - in ambito "ALL" (nessuna parte attiva) nessuna parte e' "in editing",
    //    e il quad 2D disegna col blocco 0.
    // Negli ultimi due casi il buffer va messo QUI, o trascinare non muove nulla.
    const bool flatEditingGlobal = m_isFlatView && m_flatViewTarget == 0
                                   && (!engine || engine->getMeshPartCount() <= 1
                                       || m_activeMeshPart < 0);
    m_uboData.zoom     = flatEditingGlobal ? m_flatZoom     : m_globalTexZoom;
    m_uboData.center   = flatEditingGlobal ? m_flatPan      : m_globalTexPan;
    m_uboData.rotation = flatEditingGlobal ? m_flatRotation : m_globalTexRotation;

    if (engine) {
        m_uboData.hasExplicitW = (!engine->getActiveExplicitEquation().isEmpty()) ? 1 : 0;
    } else {
        m_uboData.hasExplicitW = 0;
    }

    m_uboData.u_raySteps = m_raySteps;

    if (m_textureEnabled && m_engineMode == ModeParametric) {
        m_uboData.color = QVector3D(1.0f, 1.0f, 1.0f);
    } else {
        m_uboData.color = QVector3D(red, green, blue);
    }

    // FALLBACK ANTI-SPARIZIONE: su un campo implicito mal condizionato (prodotto,
    // es. "Chain") il ramo trasparente aggancia crossing fantasma e la superficie
    // sparirebbe con alpha<1. Forziamo opaco: il cammino opaco (d=val/gradLen) e'
    // affidabile. La UI disabilita comunque lo slider, ma questo e' l'ultimo baluardo
    // indipendente da come alpha e' stato impostato (slider/preset/script).
    m_uboData.alpha = (m_engineMode == ModeImplicit && m_implicitIllConditioned) ? 1.0f : alpha;
    m_uboData.lightIntensity = m_lightIntensity;

    m_uboData.col1 = QVector3D(texRed1, texGreen1, texBlue1);
    m_uboData.col2 = QVector3D(texRed2, texGreen2, texBlue2);
    m_uboData.useTexture = m_textureEnabled ? 1 : 0;

    // ==========================================================
    // AGGIORNAMENTO BUFFER PRINCIPALE (multi-mesh)
    // ==========================================================
    // Scriviamo un blocco UBO per parte di mesh: tutto identico tranne i limiti
    // del dominio (u_min/u_max/v_min/v_max) e u_meshIndex, che sono per-parte.
    // Il blocco 0 resta quello "globale": lo usano lo sfondo in flat view e ogni
    // draw che non appartiene a una parte, quindi il caso a mesh singola scrive
    // esattamente cio' che scriveva prima.
    const std::vector<MeshPart> &uboParts = engine->getMeshParts();
    const int uboPartCount = (int)uboParts.size();

    // Cresce l'UBO se lo script ha dichiarato piu' parti della capienza attuale.
    // ensureUboCapacity riaggancia i binding in place (senza distruggerli),
    // quindi le pipeline restano valide e non serve ricostruirle.
    ensureUboCapacity(std::max(1, uboPartCount));
    ensureDynamicBindings(m_surfaceTexture ? m_surfaceTexture : m_dummyTexture);

    if (uboPartCount <= 1) {
        // Mesh singola: un blocco solo, come da sempre.
        resourceUpdates->updateDynamicBuffer(m_ubo, 0, sizeof(UboData), &m_uboData);
    } else {
        for (int k = 0; k < uboPartCount; ++k) {
            const MeshPart &mp = uboParts[k];
            UboData partUbo = m_uboData;
            partUbo.u_min = mp.uMin;
            partUbo.u_max = mp.uMax;
            partUbo.v_min = mp.vMin;
            partUbo.v_max = mp.vMax;
            partUbo.u_meshIndex = (float)mp.meshIndex;

            // ASPETTO PER-PARTE. Un valore negativo significa "eredita dallo
            // stato globale", che e' gia' in partUbo perche' copiato da
            // m_uboData: percio' una parte non configurata resta identica a
            // prima e nessun preset esistente cambia.
            // Il colore NON si applica con una texture attiva: li' il motore
            // forza deliberatamente ubuf.color a bianco per non sporcare le
            // texture che portano gia' il proprio colore (vedi il ramo
            // m_textureEnabled sopra), e scriverci sopra lo vanificherebbe.
            // AMBITO "ALL" (m_meshAppearanceUniform): l'aspetto proprio viene
            // IGNORATO e resta valido solo il globale, gia' in partUbo. La
            // superficie si disegna come una sola; i valori per-parte non
            // vengono toccati, quindi tornando su "Mesh" ricompaiono.
            if (!m_meshAppearanceUniform) {
                // Questa parte finira' texturizzata? Serve PRIMA del colore: una
                // parte SENZA texture deve riprendersi la propria tinta, mentre
                // una texturizzata vuole il bianco neutro (finalRGB = color * tex).
                const bool partTextured = mp.effectiveTextureEnabledMulti();

                // COLORE DELLA PARTE. Il ramo globale poco sopra forza
                // m_uboData.color a BIANCO quando la texture globale e' accesa,
                // e ogni blocco parte da quella copia: una fascia senza texture
                // si ritrovava quindi bianca invece che del proprio colore
                // (o di quello globale). Il colore va soppresso solo se e'
                // QUESTA parte ad essere texturizzata, non perche' lo e' la
                // superficie.
                if (!partTextured) {
                    // Tinta piena: la propria se ce l'ha, altrimenti quella
                    // globale REALE (red/green/blue), non il bianco neutro che
                    // m_uboData porta quando la texture globale e' accesa.
                    if (mp.hasCustomColor())
                        partUbo.color = QVector3D(mp.colorR, mp.colorG, mp.colorB);
                    else if (m_textureEnabled && m_engineMode == ModeParametric)
                        partUbo.color = QVector3D(red, green, blue);
                }
                if (mp.alpha >= 0.0f)
                    partUbo.alpha = mp.alpha;
                if (mp.lightIntensity >= 0.0f)
                    partUbo.lightIntensity = mp.lightIntensity;

                // TEXTURE PROPRIA DELLA PARTE. Il codice e' gia' compilato nello
                // shader come getCustomColor_<k> (vedi createFragmentShaderSource):
                // qui basta accendere l'interruttore per questa parte, perche' il
                // dispatcher sceglie la funzione da u_meshIndex.
                //
                // TEXTURE EFFICACE DELLA PARTE. Si scrive SEMPRE, anche quando
                // la parte non ne ha una propria: in multi-mesh una fascia mai
                // configurata deve restare SENZA texture, non ereditare quella
                // globale (vedi effectiveTextureEnabledMulti). Prima qui si
                // entrava solo con hasCustomTexture e le altre si tenevano lo
                // useTexture globale copiato da m_uboData: applicando una
                // texture in "All" le fasce lasciate apposta nude se la
                // prendevano addosso.
                partUbo.useTexture = partTextured ? 1 : 0;

                if (mp.hasCustomTexture) {

                    // COLORE BASE A BIANCO SULLA PARTE TEXTURIZZATA.
                    // Il fragment compone finalRGB = ubuf.color * texture: il
                    // colore va quindi neutralizzato, o la texture esce
                    // MOLTIPLICATA per la tinta della superficie (con un colore
                    // scuro il risultato e' nero, e con la scacchiera meta'
                    // celle si annullano -> la mesh sembra sparire).
                    // Il ramo globale lo fa gia' piu' sopra, ma solo quando e'
                    // m_textureEnabled GLOBALE ad essere acceso: una texture
                    // per-mesh non passa di li'.
                    // NB: si scrive DOPO il colore proprio della parte, cosi'
                    // vince su di esso; il colore resta in MeshPart e ricompare
                    // spegnendo la texture.
                    // Stessa condizione usata per useTexture e per il colore
                    // pieno qui sopra: il bianco neutro e la texture accesa
                    // devono essere la STESSA decisione, o si torna al caso
                    // "parte bianca senza texture".
                    if (partTextured)
                        partUbo.color = QVector3D(1.0f, 1.0f, 1.0f);

                    // COLORI u_col1/u_col2 DELLA PARTE. Vivono nel blocco UBO,
                    // quindi possono essere diversi per ogni mesh: senza questo
                    // le parti condividevano i due slot globali e l'ultima
                    // texture applicata riscriveva i colori di TUTTE le altre.
                    if (mp.hasCustomTexColors()) {
                        partUbo.col1 = QVector3D(mp.texCol1R, mp.texCol1G, mp.texCol1B);
                        partUbo.col2 = QVector3D(mp.texCol2R, mp.texCol2G, mp.texCol2B);
                    }

                }

                // ZOOM / PAN / ROTAZIONE 2D PROPRI DELLA PARTE. Stessa ragione
                // dei colori: sono campi del blocco UBO, quindi per-parte.
                // Senza, la trasformazione fatta col mouse in vista 2D su UNA
                // texture veniva applicata a tutte.
                // FUORI dal blocco hasCustomTexture: la trasformazione e' una
                // proprieta' della PARTE, non dello script. Due fasce con LA
                // STESSA texture devono poterla mostrare a scale diverse, e una
                // fascia che eredita la texture globale deve avere comunque la
                // propria inquadratura, altrimenti resta agganciata allo zoom
                // globale e si ridimensiona insieme alle altre.
                // In vista 2D (m_isFlatView) si edita a schermo intero la
                // texture della SOLA parte attiva: quella deve seguire i membri
                // globali che il mouse muove (o trascinare non muoverebbe
                // nulla), mentre tutte le ALTRE restano ferme sulla propria.
                const bool editingThisPart = m_isFlatView && m_flatViewTarget == 0
                                             && (k == m_activeMeshPart);
                if (editingThisPart) {
                    // BUFFER DI LAVORO, esplicito. Prima qui si lasciava stare,
                    // contando sul fatto che partUbo (copia di m_uboData) gia'
                    // portasse i membri che il mouse muove. Non e' piu' vero: in
                    // m_uboData ora c'e' la trasformazione GLOBALE persistente,
                    // che il mouse non tocca -- e la texture in editing restava
                    // immobile al trascinamento.
                    partUbo.zoom = m_flatZoom;
                    partUbo.center = m_flatPan;
                    partUbo.rotation = m_flatRotation;
                } else if (mp.hasCustomTexTransform()) {
                    partUbo.zoom = mp.texZoom;
                    partUbo.center = QVector2D(mp.texPanX, mp.texPanY);
                    partUbo.rotation = mp.texRotation;
                }
            }

            // MODALITA' PER-PARTE. Lo shader decide dal solo ubuf.u_renderMode
            // se disegnare a colore piatto (wireframe/bordi) o illuminato:
            // senza questa riga una mesh in wireframe con globale Phong veniva
            // illuminata, e una mesh solida con globale wireframe usciva piatta.
            partUbo.renderMode = effectivePartRenderMode(mp);

            resourceUpdates->updateDynamicBuffer(m_ubo, k * m_uboBlockStride,
                                                 sizeof(UboData), &partUbo);
        }
    }

    // =========================================================================
    // SCARICO TEXTURE (Superficie) — distruzione RHI nel contesto del frame.
    // Eseguito PRIMA dell'upload: se nello stesso giro arriva una nuova immagine,
    // il blocco di upload sotto ricrea comunque m_surfaceTexture.
    // =========================================================================
    if (m_surfaceTextureNeedsClear) {
        if (m_surfaceTexture) {
            m_surfaceTexture->destroy();
            delete m_surfaceTexture;
            m_surfaceTexture = nullptr;

            // Il binding allo slot 1 puntava a m_surfaceTexture appena distrutta:
            // ricolleghiamolo a m_dummyTexture (come all'init), altrimenti un frame
            // disegnato senza re-upload leggerebbe memoria liberata.
            if (m_bindings && m_dummyTexture) {
                m_bindings->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo),
                    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_dummyTexture, m_sampler)
                });
                m_bindings->create();
            }
            // Stesso problema sui binding con dynamic offset usati dalla
            // superficie: puntavano alla texture appena distrutta.
            ensureDynamicBindings(m_dummyTexture);
        }
        m_surfaceTextureNeedsClear = false;
    }

    // =========================================================================
    // GESTIONE COMUNE UPLOAD TEXTURE (Superficie)
    // =========================================================================
    if (m_surfaceTextureNeedsUpload && !m_pendingSurfaceImage.isNull()) {
        if (m_surfaceTexture) {
            m_surfaceTexture->destroy();
            delete m_surfaceTexture;
        }
        m_surfaceTexture = rhi()->newTexture(QRhiTexture::RGBA8, m_pendingSurfaceImage.size(), 1);
        m_surfaceTexture->create();

        QRhiTextureSubresourceUploadDescription subresDesc(m_pendingSurfaceImage.constBits(), m_pendingSurfaceImage.sizeInBytes());
        QRhiTextureUploadEntry entry(0, 0, subresDesc);
        QRhiTextureUploadDescription uploadDesc({ entry });
        resourceUpdates->uploadTexture(m_surfaceTexture, uploadDesc);

        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_surfaceTexture, m_sampler)
        });
        m_bindings->create();

        // La superficie disegna dai binding con dynamic offset: senza questo
        // aggiornamento la nuova texture non comparirebbe (resterebbe la dummy).
        ensureDynamicBindings(m_surfaceTexture);

        m_surfaceTextureNeedsUpload = false;
    }

    // =========================================================================
    // GESTIONE COMUNE SFONDO 2D (Texture Upload, UBO, VBO)
    // Questo serve sia alla Parametrica che al Ray Marching!
    // =========================================================================
    if (m_backgroundTextureNeedsUpload && !m_pendingBackgroundImage.isNull()) {
        if (!m_bgBindings) initBackgroundShader();

        if (m_backgroundTexture) {
            m_backgroundTexture->destroy();
            delete m_backgroundTexture;
        }

        m_backgroundTexture = rhi()->newTexture(QRhiTexture::RGBA8, m_pendingBackgroundImage.size(), 1);
        m_backgroundTexture->create();

        QRhiTextureSubresourceUploadDescription bgSubresDesc(m_pendingBackgroundImage.constBits(), m_pendingBackgroundImage.sizeInBytes());
        QRhiTextureUploadEntry bgEntry(0, 0, bgSubresDesc);
        QRhiTextureUploadDescription bgUploadDesc({ bgEntry });
        resourceUpdates->uploadTexture(m_backgroundTexture, bgUploadDesc);

        m_bgBindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_bgUbo),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_backgroundTexture, m_sampler)
        });
        m_bgBindings->create();

        m_backgroundTextureNeedsUpload = false;
    }

    if (m_useBackgroundTexture || m_isFlatView || m_engineMode == ModeImplicit) {
        if (!m_bgBindings) initBackgroundShader();

        if (m_bgUbo) {
            UboData bgUboData = m_uboData;
            bgUboData.time = vtFreezeBg ? m_vtFrozenBg : m_manualTime + m_timeBg;
            // Lo sfondo è sempre opaco: lo slider trasparenza del renderer agisce
            // sulla superficie (m_uboData.alpha), non deve sbiadire la texture di
            // background fondendola col clearColor (pipeline bg con blend SrcAlpha).
            bgUboData.alpha = 1.0f;

            QVariant c1 = property("bg_col1");
            QVariant c2 = property("bg_col2");
            if (c1.isValid()) bgUboData.col1 = c1.value<QVector3D>();
            if (c2.isValid()) bgUboData.col2 = c2.value<QVector3D>();

            QVariant z = property("bg_zoom");
            QVariant p = property("bg_pan");
            QVariant r = property("bg_rot");
            if (z.isValid()) bgUboData.zoom = z.toFloat();
            if (p.isValid()) bgUboData.center = p.value<QVector2D>();
            if (r.isValid()) bgUboData.rotation = r.toFloat();

            resourceUpdates->updateDynamicBuffer(m_bgUbo, 0, sizeof(UboData), &bgUboData);
        }

        if (!m_bgVboUploaded && m_bgVbo) {
            Vertex quadVertices[6] = {
                { QVector4D(-1.0f, -1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(0.0f, 1.0f) },
                { QVector4D( 1.0f, -1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(1.0f, 1.0f) },
                { QVector4D(-1.0f,  1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(0.0f, 0.0f) },
                { QVector4D(-1.0f,  1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(0.0f, 0.0f) },
                { QVector4D( 1.0f, -1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(1.0f, 1.0f) },
                { QVector4D( 1.0f,  1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(1.0f, 0.0f) }
            };
            resourceUpdates->updateDynamicBuffer(m_bgVbo, 0, sizeof(quadVertices), quadVertices);
            m_bgVboUploaded = true;
        }
    }


    if (m_engineMode == ModeParametric) {

        // ==========================================
        // 1. MODALITÀ PARAMETRICA
        // ==========================================
        if (!m_pipelineOpaque) {
            buildPipeline();
        }

        if (meshNeedsUpdate) {
            const auto& vertices = engine->getVertices();
            const auto& indices = engine->getIndices();

            if (vertices.empty() || indices.empty()) {
                qWarning() << "ATTENZIONE: Stai inviando una mesh vuota alla GPU!";
            } else {
                int vSize = vertices.size() * sizeof(Vertex);
                int iSize = indices.size() * sizeof(unsigned int);

                if (m_vbo->size() < vSize) {
                    m_vbo->destroy(); delete m_vbo;
                    m_vbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5);
                    m_vbo->create();
                }

                if (m_ibo->size() < iSize) {
                    m_ibo->destroy(); delete m_ibo;
                    m_ibo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                    m_ibo->create();
                }

                resourceUpdates->updateDynamicBuffer(m_vbo, 0, vSize, vertices.data());
                resourceUpdates->updateDynamicBuffer(m_ibo, 0, iSize, indices.data());
                m_indexCount = indices.size();
            }
            meshNeedsUpdate = false;
        }

        if (wireframeNeedsUpdate && !m_wireframeIndices.empty()) {
            int iSize = m_wireframeIndexCount * sizeof(unsigned int);
            if (m_wireframeIbo->size() < iSize) {
                m_wireframeIbo->destroy(); delete m_wireframeIbo;
                m_wireframeIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                m_wireframeIbo->create();
            }
            resourceUpdates->updateDynamicBuffer(m_wireframeIbo, 0, iSize, m_wireframeIndices.data());
            wireframeNeedsUpdate = false;
        }

        // --- RENDER PASS ---
        QColor clearColor = QColor::fromRgbF(m_bgColor.x(), m_bgColor.y(), m_bgColor.z());
        cb->beginPass(renderTarget(), clearColor, { 1.0f, 0 }, resourceUpdates);

        if (m_isFlatView) {
            if (m_flatViewTarget == 1 && m_bgPipeline && m_bgVbo) {
                cb->setGraphicsPipeline(m_bgPipeline);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(m_bgBindings);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            } else if (m_pipelineOpaque && m_bgVbo) {
                cb->setGraphicsPipeline(m_pipelineOpaque);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                // m_pipelineOpaque e' costruita sul layout con dynamic offset:
                // anche questo draw (quad di anteprima, nessuna parte di mesh)
                // deve passare un offset.
                // MULTI-MESH: la vista 2D edita la texture della mesh SELEZIONATA,
                // quindi deve leggere il blocco UBO di QUELLA parte. Col blocco 0
                // fisso, u_meshIndex valeva sempre 0 e il dispatcher mostrava la
                // texture della mesh 1 mentre il dock script mostrava quella
                // selezionata: stesso indice, due texture diverse a schermo.
                // L'indice va CLAMPATO agli stessi limiti che usa
                // commitFlatTransformToActivePart, o si disegna un blocco mentre
                // il mouse ne scrive un altro e il trascinamento sembra non
                // funzionare (il "a tratti" storico: capita quando l'indice
                // attivo sopravvive a una rigenerazione con meno parti).
                // Nota: con uboPartCount <= 1 e' scritto SOLO il blocco 0, quindi
                // qualunque altro offset leggerebbe un blocco mai aggiornato.
                const std::vector<MeshPart> &flatParts = engine->getMeshParts();
                const int flatPart = (flatParts.size() > 1 && m_activeMeshPart >= 0
                                      && m_activeMeshPart < (int)flatParts.size())
                                     ? m_activeMeshPart : 0;
                const QRhiCommandBuffer::DynamicOffset dynOfs0(0, flatPart * m_uboBlockStride);
                cb->setShaderResources(m_bindingsDyn ? m_bindingsDyn : m_bindings, 1, &dynOfs0);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            }
        } else {
            if (m_useBackgroundTexture) {
                if (!m_bgPipeline) rebuildBackgroundShader(!m_bgIsScript, m_bgScriptCode);
                if (m_bgPipeline && m_bgVbo) {
                    cb->setGraphicsPipeline(m_bgPipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    cb->setShaderResources(m_bgBindings);
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
            }

            // MULTI-MESH: una draw call per parte, ognuna col proprio blocco UBO
            // (dominio + u_meshIndex) selezionato via dynamic offset. Con una
            // parte sola il loop fa un solo giro sul blocco 0: identico a prima.
            QRhiShaderResourceBindings *srb = m_bindingsDyn ? m_bindingsDyn : m_bindings;
            const std::vector<MeshPart> &parts = engine->getMeshParts();

            // Una parte e' in wireframe se la sua modalita' EFFICACE e' 2.
            // Con parti che ereditano tutte, questo coincide col vecchio test
            // globale renderMode == 2.
            auto partIsWireframe = [&](const MeshPart &p) {
                return effectivePartRenderMode(p) == 2;
            };
            bool anyWireframe = (renderMode == 2);
            bool anySolid     = (renderMode != 2);
            if (parts.size() > 1) {
                anyWireframe = false;
                anySolid     = false;
                for (const MeshPart &p : parts) {
                    if (partIsWireframe(p)) anyWireframe = true;
                    else                    anySolid = true;
                }
            }

            // ORDINE: prima il SOLIDO, poi il WIREFRAME.
            // La pipeline wireframe ha depthWrite = true (le linee devono
            // occludersi fra loro), quindi disegnandola per PRIMA riempiva il
            // depth buffer e le mesh solide TRASPARENTI disegnate dopo venivano
            // scartate dal depth test: a mesh wireframe presente, una mesh
            // trasparente dietro spariva del tutto. Il solido opaco scrive
            // comunque depth, quindi il wireframe disegnato dopo resta
            // correttamente occluso da cio' che gli sta davanti.
            if (anySolid) {
                // Solido
                if (m_indexCount > 0 && m_vbo && m_ibo) {
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbo, 0);

                    // Disegna una parte con la pipeline corrente. Gli indici sono
                    // LOCALI alla parte, quindi il rebase sui vertici passa da
                    // vertexOffset di drawIndexed; l'offset nell'index buffer e'
                    // in byte.
                    auto drawPart = [&](const MeshPart &p, quint32 ubOffset) {
                        if (p.indexCount <= 0) return;
                        const QRhiCommandBuffer::DynamicOffset ofs(0, ubOffset);
                        cb->setShaderResources(m_bindingsDyn ? m_bindingsDyn : m_bindings, 1, &ofs);
                        cb->setVertexInput(0, 1, &vbufBinding, m_ibo,
                                           p.indexOffset * sizeof(unsigned int),
                                           QRhiCommandBuffer::IndexUInt32);
                        cb->drawIndexed(p.indexCount, 1, 0, p.vertexOffset);
                    };

                    // Mesh singola (o mesh custom senza parti): percorso storico,
                    // un solo drawIndexed su tutto il buffer col blocco UBO 0.
                    auto drawWhole = [&]() {
                        const QRhiCommandBuffer::DynamicOffset ofs(0, 0);
                        cb->setShaderResources(m_bindingsDyn ? m_bindingsDyn : m_bindings, 1, &ofs);
                        cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                        cb->drawIndexed(m_indexCount);
                    };

                    const bool multi = (parts.size() > 1);

                    // Alpha EFFICACE di una parte: la sua, se dichiarata,
                    // altrimenti quella globale. Decide se va nel gruppo
                    // trasparente o in quello opaco.
                    // In ambito "All" l'alpha per-parte e' sospeso: vale il
                    // globale per tutte, o una mesh con alpha proprio finirebbe
                    // nel gruppo sbagliato e la superficie non si comporterebbe
                    // come una sola.
                    auto partAlpha = [&](const MeshPart &p) {
                        if (m_meshAppearanceUniform) return alpha;
                        return (p.alpha >= 0.0f) ? p.alpha : alpha;
                    };
                    if (multi) {
                        // Le parti in wireframe sono gia' state disegnate dal
                        // ramo sopra: qui vanno saltate, o comparirebbero anche
                        // come solido (doppio disegno).
                        std::vector<const MeshPart*> solidParts;
                        solidParts.reserve(parts.size());
                        for (const MeshPart &p : parts)
                            if (!partIsWireframe(p)) solidParts.push_back(&p);

                        // ORDINE: prima tutte le parti OPACHE (scrivono depth),
                        // poi le trasparenti; e fra queste TUTTE le facce
                        // posteriori prima di QUALSIASI faccia anteriore. Fare
                        // back+front parte per parte fonderebbe la parte k+1
                        // sopra il fronte della parte k, invertendo la
                        // profondita' fra i rami (il fronte non scrive depth).
                        if (m_pipelineOpaque) {
                            cb->setGraphicsPipeline(m_pipelineOpaque);
                            for (const MeshPart *p : solidParts)
                                if (partAlpha(*p) >= 0.99f)
                                    drawPart(*p, p->meshIndex * m_uboBlockStride);
                        }
                        // TRASPARENTI ORDINATE PER PROFONDITA'.
                        // Il blending non e' commutativo e questo ramo non scrive
                        // depth: disegnandole nell'ordine delle PARTI vinceva
                        // l'ultima anche se stava dietro, e con tori annidati uno
                        // interno sembrava coprire quello esterno.
                        // La distanza si stima sul CENTROIDE dei vertici della
                        // parte, portato in spazio vista: e' un'approssimazione
                        // (due superfici compenetranti non hanno un ordine
                        // corretto) ma copre il caso dei rami annidati o
                        // affiancati, che e' quello dei preset multi-mesh.
                        std::vector<const MeshPart*> transp;
                        transp.reserve(solidParts.size());
                        for (const MeshPart *p : solidParts)
                            if (partAlpha(*p) < 0.99f) transp.push_back(p);

                        if (transp.size() > 1) {
                            const std::vector<Vertex> &verts = engine->getVertices();
                            const QMatrix4x4 mvNow = m_view * m_model;
                            auto depthOf = [&](const MeshPart *p) -> float {
                                if (p->vertexCount <= 0) return 0.0f;
                                QVector3D c(0.0f, 0.0f, 0.0f);
                                const int step = std::max(1, p->vertexCount / 64);
                                int n = 0;
                                for (int i = 0; i < p->vertexCount; i += step) {
                                    const int idx = p->vertexOffset + i;
                                    if (idx < 0 || idx >= (int)verts.size()) continue;
                                    c += verts[idx].position.toVector3D();
                                    ++n;
                                }
                                if (n == 0) return 0.0f;
                                c /= float(n);
                                // La camera guarda lungo -z: piu' NEGATIVO = piu' lontano.
                                return mvNow.map(c).z();
                            };
                            std::stable_sort(transp.begin(), transp.end(),
                                             [&](const MeshPart *a, const MeshPart *b) {
                                                 return depthOf(a) < depthOf(b);
                                             });
                        }

                        // Disegna solo se entrambe le pipeline di trasparenza sono
                        // valide: dopo un errore di compilazione una puo' essere
                        // nulla, e passarla a Metal causa EXC_BAD_ACCESS.
                        if (m_pipelineTranspBack && m_pipelineTranspFront) {
                            cb->setGraphicsPipeline(m_pipelineTranspBack);
                            for (const MeshPart *p : transp)
                                drawPart(*p, p->meshIndex * m_uboBlockStride);

                            cb->setGraphicsPipeline(m_pipelineTranspFront);
                            for (const MeshPart *p : transp)
                                drawPart(*p, p->meshIndex * m_uboBlockStride);
                        }
                    } else if (alpha < 0.99f) {
                        if (m_pipelineTranspBack && m_pipelineTranspFront) {
                            cb->setGraphicsPipeline(m_pipelineTranspBack);
                            drawWhole();
                            cb->setGraphicsPipeline(m_pipelineTranspFront);
                            drawWhole();
                        }
                    } else {
                        if (m_pipelineOpaque) {
                            cb->setGraphicsPipeline(m_pipelineOpaque);
                            drawWhole();
                        }
                    }
                }
            }

            // WIREFRAME, dopo il solido (vedi la nota sull'ordine sopra).
            if (anyWireframe) {
                if (m_wireframePipeline && m_wireframeIndexCount > 0) {
                    cb->setGraphicsPipeline(m_wireframePipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbo, 0);

                    if (m_wireframeRanges.size() <= 1) {
                        const QRhiCommandBuffer::DynamicOffset ofs(0, 0);
                        cb->setShaderResources(srb, 1, &ofs);
                        cb->setVertexInput(0, 1, &vbufBinding, m_wireframeIbo, 0, QRhiCommandBuffer::IndexUInt32);
                        cb->drawIndexed(m_wireframeIndexCount);
                    } else {
                        for (const WireframeRange &r : m_wireframeRanges) {
                            if (r.indexCount <= 0) continue;
                            // Salta le parti che NON sono in wireframe: i loro
                            // indici esistono comunque nel buffer (buildWireframe
                            // genera un range per ogni parte), ma vanno disegnate
                            // dal ramo solido.
                            if (r.meshIndex >= 0 && r.meshIndex < (int)parts.size()
                                && !partIsWireframe(parts[r.meshIndex])) continue;
                            const QRhiCommandBuffer::DynamicOffset ofs(0, r.meshIndex * m_uboBlockStride);
                            cb->setShaderResources(srb, 1, &ofs);
                            // Gli indici del wireframe sono ASSOLUTI: l'offset
                            // della parte si applica all'index buffer (in byte),
                            // non ai vertici.
                            cb->setVertexInput(0, 1, &vbufBinding, m_wireframeIbo,
                                               r.indexOffset * sizeof(unsigned int),
                                               QRhiCommandBuffer::IndexUInt32);
                            cb->drawIndexed(r.indexCount);
                        }
                    }
                }
            }

        }

        cb->endPass();

    } else if (m_engineMode == ModeImplicit) {
        // ==========================================
        // MODALITÀ IMPLICITA (RAY MARCHING)
        // ==========================================
        if (!m_pipelineImplicit) buildImplicitPipeline();

        QColor clearColor = QColor::fromRgbF(m_bgColor.x(), m_bgColor.y(), m_bgColor.z());
        cb->beginPass(renderTarget(), clearColor, { 1.0f, 0 }, resourceUpdates);

        if (m_isFlatView) {
            if (m_flatViewTarget == 1 && m_bgPipeline && m_bgVbo) {
                cb->setGraphicsPipeline(m_bgPipeline);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(m_bgBindings);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            }
        } else {
            if (m_useBackgroundTexture) {
                if (!m_bgPipeline) rebuildBackgroundShader(!m_bgIsScript, m_bgScriptCode);
                if (m_bgPipeline && m_bgVbo) {
                    cb->setGraphicsPipeline(m_bgPipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    cb->setShaderResources(m_bgBindings);
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
            }

            if (m_pipelineImplicit && m_bgVbo) {
                cb->setGraphicsPipeline(m_pipelineImplicit);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(m_bindings);

                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            }
        }

        cb->endPass();
    }
}

void GLWidget::releaseResources()
{
    if (m_dummyTexture) {
        m_dummyTexture->destroy();
        delete m_dummyTexture;
        m_dummyTexture = nullptr;
    }
    if (m_sampler) {
        m_sampler->destroy();
        delete m_sampler;
        m_sampler = nullptr;
    }
    if (m_bindings) {
        delete m_bindings;
        m_bindings = nullptr;
    }
    if (m_bindingsDyn) {
        delete m_bindingsDyn;
        m_bindingsDyn = nullptr;
        m_bindingsDynTexture = nullptr;
    }
    if (m_ubo) {
        delete m_ubo;
        m_ubo = nullptr;
        m_uboBlockCapacity = 0;
    }
    if (m_vbo) {
        delete m_vbo;
        m_vbo = nullptr;
    }
    if (m_ibo) {
        delete m_ibo;
        m_ibo = nullptr;
    }
    if (m_wireframeIbo) {
        delete m_wireframeIbo;
        m_wireframeIbo = nullptr;
    }
    if (m_wireframePipeline) {
        delete m_wireframePipeline;
        m_wireframePipeline = nullptr;
    }
    if (m_surfaceTexture) {
        m_surfaceTexture->destroy();
        delete m_surfaceTexture;
        m_surfaceTexture = nullptr;
    }
    if (m_bgVbo) {
        delete m_bgVbo;
        m_bgVbo = nullptr;
    }
    if (m_bgPipeline) {
        delete m_bgPipeline;
        m_bgPipeline = nullptr;
    }
    if (m_bgBindings) {
        delete m_bgBindings;
        m_bgBindings = nullptr;
    }
    if (m_backgroundTexture) {
        m_backgroundTexture->destroy();
        delete m_backgroundTexture;
        m_backgroundTexture = nullptr;
    }
    if (m_pipelineOpaque) {
        delete m_pipelineOpaque;
        m_pipelineOpaque = nullptr;
    }
    if (m_pipelineTranspBack) {
        delete m_pipelineTranspBack;
        m_pipelineTranspBack = nullptr;
    }
    if (m_pipelineTranspFront) {
        delete m_pipelineTranspFront;
        m_pipelineTranspFront = nullptr;
    }
    if (m_bgUbo) {
        delete m_bgUbo;
        m_bgUbo = nullptr;
    }
    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    if (m_bindingsImplicit) {
        delete m_bindingsImplicit;
        m_bindingsImplicit = nullptr;
    }
}

void GLWidget::mousePressEvent(QMouseEvent *event) {
    m_inputHandler->handleMousePress(event);
}

void GLWidget::mouseMoveEvent(QMouseEvent *event) {
    m_inputHandler->handleMouseMove(event);
}

void GLWidget::wheelEvent(QWheelEvent *event)
{
    if (m_inputHandler) {
        m_inputHandler->handleWheel(event);
    }

    event->accept();

    update();
}

bool GLWidget::event(QEvent *e)
{
    if (m_inputHandler && m_inputHandler->handleTouch(e)) {
        return true;
    }

    // Usiamo QRhiWidget invece di QOpenGLWidget
    return QRhiWidget::event(e);
}

void GLWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_inputHandler) {
        m_inputHandler->handleMouseRelease(event);
    }

    // Usiamo QRhiWidget invece di QOpenGLWidget
    return QRhiWidget::mouseReleaseEvent(event);
}


//===========================================================
// PUBLIC SLOTS
//===========================================================

void GLWidget::rebuildBackgroundShader(bool isTextureMode, const QString &customCode) {
    if (!m_bgVbo) initBackgroundShader();

    if (m_bgPipeline) {
        delete m_bgPipeline;
        m_bgPipeline = nullptr;
    }

    m_bgPipeline = rhi()->newGraphicsPipeline();
    m_bgPipeline->setTopology(QRhiGraphicsPipeline::Triangles);

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal) },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });
    m_bgPipeline->setVertexInputLayout(inputLayout);

    QString header = "#version 450\n";

    QString vsSource = header +
                       "layout(location=0) in vec3 position;\n"
                       "layout(location=1) in vec4 normal;\n"
                       "layout(location=2) in vec2 texCoord;\n"
                       "layout(location=0) out vec2 v_texCoord;\n"
                       "void main() {\n"
                       "  v_texCoord = vec2(texCoord.x, 1.0 - texCoord.y);\n"
                       "  gl_Position = vec4(position.xy, 0.999, 1.0);\n"
                       "}\n";


    QString fsSource = createBackgroundFragmentShader(isTextureMode, customCode);

    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    m_bgPipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_bgPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_bgPipeline->setSampleCount(renderTarget()->sampleCount());
    m_bgPipeline->setDepthTest(false);
    m_bgPipeline->setDepthWrite(false);
    m_bgPipeline->setCullMode(QRhiGraphicsPipeline::None);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_bgPipeline->setTargetBlends({ blend });
    m_bgPipeline->setShaderResourceBindings(m_bgBindings);
    m_bgPipeline->create();
}

// ==========================================================
// PRIVATE SLOTS
// ==========================================================

void GLWidget::updateRotation() {
    // Clock esterno attivo (registrazione): il tick live e' un no-op, ad
    // avanzare le rotazioni e' il loop del recorder (advanceRotationsBy col
    // dt virtuale del frame). Il timer resta attivo apposta: e' lo STATO.
    if (m_externalClockActive) return;

    advanceRotationsBy(kRotationTickMs / 1000.0f);

    // NB: la rotazione 4D NON cambia i dati CPU della mesh: la rotazione XW/YW/ZW
    // e la proiezione 4D->3D avvengono nel VERTEX SHADER leggendo omega/phi/psi
    // dall'UBO, che render() ricarica a OGNI frame (updateDynamicBuffer(m_ubo)).
    // Settare meshNeedsUpdate qui forzava un re-upload INTEGRALE di VBO+IBO a ogni
    // frame con dati IDENTICI: spreco puro che su iOS/Metal (memoria stretta +
    // watchdog jetsam) accumula buffer transitori e fa killare l'app dopo qualche
    // decina di secondi, specie su superfici dense (steps alti) in wireframe.
    // Basta l'update() qui sotto: ridisegna col nuovo omega senza toccare la mesh.

    emit rotationChanged();
    update();
}

void GLWidget::advanceRotationsBy(float dtSeconds) {
    // L'unita' di avanzamento e' il tick live di rotationTimer: le velocita'
    // dei dock sono tarate su quello. Convertire dtSeconds in tick
    // equivalenti garantisce che il recorder, passando il dt del suo frame
    // virtuale (1/fps), avanzi esattamente alla velocita' vista a schermo.
    float ticks = dtSeconds / (kRotationTickMs / 1000.0f);

    float speedMult3D = 2.0f * ticks;
    float speedMult4D = 0.05f * ticks;

    float dPrec = precessionSpeed * speedMult3D;
    float dNut  = nutationSpeed   * speedMult3D;
    float dSpin = spinSpeed       * speedMult3D;

    if (std::abs(dPrec) > 0.0001f || std::abs(dNut) > 0.0001f || std::abs(dSpin) > 0.0001f) {
        addObjectRotation(dPrec, dNut, dSpin);
    }

    // ------------- Rotazioni 4D -----------
    omega += omegaSpeed * speedMult4D;
    phi   += phiSpeed   * speedMult4D;
    psi   += psiSpeed   * speedMult4D;
}


// ==========================================================
// ENGINE MODES
// ==========================================================

void GLWidget::setEngineMode(EngineMode mode)
{
    if (m_engineMode == mode) return;

    // 1. SALVA lo stato del "Banco di Lavoro" nello slot che stiamo abbandonando
    int oldIndex = (m_engineMode == ModeParametric) ? 0 : 1;
    m_viewStates[oldIndex].cameraPos    = m_cameraPos;
    m_viewStates[oldIndex].cameraYaw    = m_cameraYaw;
    m_viewStates[oldIndex].cameraPitch  = m_cameraPitch;
    m_viewStates[oldIndex].cameraRoll   = m_cameraRoll;
    m_viewStates[oldIndex].rotationQuat = m_rotationQuat;
    m_viewStates[oldIndex].flatZoom     = m_flatZoom;
    m_viewStates[oldIndex].flatPan      = m_flatPan;
    m_viewStates[oldIndex].flatRotation = m_flatRotation;
    m_viewStates[oldIndex].globalTexZoom     = m_globalTexZoom;
    m_viewStates[oldIndex].globalTexPan      = m_globalTexPan;
    m_viewStates[oldIndex].globalTexRotation = m_globalTexRotation;

    // 2. Cambia la modalità attiva
    m_engineMode = mode;

    // 3. RIPRISTINA i dati nel "Banco di Lavoro" dallo slot in cui stiamo entrando
    int newIndex = (m_engineMode == ModeParametric) ? 0 : 1;
    m_cameraPos    = m_viewStates[newIndex].cameraPos;
    m_cameraYaw    = m_viewStates[newIndex].cameraYaw;
    m_cameraPitch  = m_viewStates[newIndex].cameraPitch;
    m_cameraRoll   = m_viewStates[newIndex].cameraRoll;
    m_rotationQuat = m_viewStates[newIndex].rotationQuat;
    m_flatZoom     = m_viewStates[newIndex].flatZoom;
    m_flatPan      = m_viewStates[newIndex].flatPan;
    m_flatRotation = m_viewStates[newIndex].flatRotation;
    m_globalTexZoom     = m_viewStates[newIndex].globalTexZoom;
    m_globalTexPan      = m_viewStates[newIndex].globalTexPan;
    m_globalTexRotation = m_viewStates[newIndex].globalTexRotation;

    update();
}


// ==========================================================
// EQUATIONS & MATHEMATICS
// ==========================================================

bool GLWidget::setParametricEquations(const QString &xEq, const QString &yEq,
                                      const QString &zEq, const QString &wEq)
{
    m_isCustomMesh = false;
    QString oldX = m_eqX, oldY = m_eqY, oldZ = m_eqZ, oldW = m_eqW;

    // 1. DRY RUN del vertex con le NUOVE equazioni prima di toccare lo stato
    QString vsSource = createVertexShaderSource(xEq, yEq, zEq, wEq);
    QShaderBaker baker;
    baker.setSourceString(vsSource.toUtf8(), QShader::VertexStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });
    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = "VERTEX (parametric): " + baker.errorMessage();
        return false;  // Non tocchiamo nulla, l'UI riceve false e mostra il popup
    }

    // 2. Solo ora applichiamo
    m_eqX = xEq; m_eqY = yEq; m_eqZ = zEq; m_eqW = wEq;
    engine->setEquations(xEq, yEq, zEq, wEq);
    rebuildShader();
    meshNeedsUpdate = true;
    update();
    return true;
}

void GLWidget::setImplicitEquation(const QString &eqF)
{
    if (m_eqImplicitF == eqF) return;

    m_eqImplicitF = eqF;
    detectImplicitConditioning(eqF);

    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    update();
}

// Rileva EMPIRICAMENTE se il campo implicito e' "mal condizionato" (prodotto di
// piu' fattori, es. preset "Chain"). Non e' un'euristica sulla stringa: valuta il
// campo su una griglia CPU (exprtk) e misura il RANGE DINAMICO *SULLA SUPERFICIE*.
//
// La chiave e' misurare |f| SOLO vicino a f=0 (celle adiacenti a un cambio di segno),
// NON su tutto il box. Un polinomio a espressione singola di grado alto (es. "Filament
// Cube", grado ~12) esplode nel vuoto lontano dalle sue radici (~1e12) pur restando
// SANO sulla superficie (~1): misurando il max globale sarebbe un FALSO POSITIVO. Un
// prodotto di piu' fattori (es. "Chain", 6 tori) invece resta enorme ANCHE vicino ai
// suoi crossing (~1e8), perche' ogni crossing porta con se' il prodotto degli altri
// fattori grandi. La banda near-surface separa i due con ~6 ordini di margine.
//
// Se max|f| near-surface sfonda la soglia -> il ramo trasparente aggancerebbe crossing
// FANTASMA (cambi di segno per parita' del prodotto, con salti di scala enormi) e la
// superficie sparirebbe con alpha<1: settiamo m_implicitIllConditioned = true cosi' il
// render forza alpha=1.0 e la UI disabilita lo slider. Vedi m_implicitIllConditioned in
// glwidget.h per il razionale completo.
void GLWidget::detectImplicitConditioning(const QString &eqF)
{
    m_implicitIllConditioned = false;
    m_implicitTransparencyWarn = false;   // SOLO Android; ricalcolato sotto
    if (eqF.trimmed().isEmpty()) return;

    // I chiamanti passano l'equazione in due forme: gia' sottratta "(LHS) - (RHS)"
    // oppure grezza "LHS = RHS". exprtk NON compila l'uguaglianza come espressione di
    // valore, quindi normalizziamo "A = B" -> "(A) - (B)" (altrimenti compile fallisce
    // e usciremmo con un falso negativo proprio sul "Chain").
    QString expr = eqF.trimmed();
    int eqPos = expr.indexOf('=');
    if (eqPos >= 0) {
        QString lhs = expr.left(eqPos).trimmed();
        QString rhs = expr.mid(eqPos + 1).trimmed();
        if (rhs.isEmpty()) rhs = "0.0";
        expr = QString("(%1) - (%2)").arg(lhs, rhs);
    }

    // Coordinate e costanti collegate per riferimento (exprtk richiede lvalue vivi).
    double x = 0.0, y = 0.0, z = 0.0, tVar = 0.0;
    double A = m_uboData.mathParams.x(), B = m_uboData.mathParams.y();
    double C = m_uboData.mathParams.z(), S = m_uboData.mathParams.w();
    double D = m_uboData.mathParams2.x(), E = m_uboData.mathParams2.y();
    double F = m_uboData.mathParams2.z();

    ExpressionParser parser;
    // Il campo implicito e' in x,y,z (+ eventuale t e costanti A..F,S). Aggiungiamo
    // solo questi simboli: setupConstants collega A..F,S e crea la symbol_table.
    parser.setupConstants(A, B, C, D, E, F, S);
    parser.addCustomVariable("x", x);
    parser.addCustomVariable("y", y);
    parser.addCustomVariable("z", z);
    parser.addCustomVariable("t", tVar);
    if (!parser.compile(expr)) {
        // se non compila lato CPU non blocchiamo nulla: lo shader la gestisce a modo suo
        return;
    }

    // Campioniamo dentro un box RAGIONEVOLE dove vive la geometria: intersezione tra
    // il bounding box UI e [-10,10]^3 (i clip box di default sono +/-1000, campionarli
    // tutti misurerebbe solo il vuoto lontano).
    const float clampR = 10.0f;
    float bxMin = std::max(m_uboData.x_min, -clampR), bxMax = std::min(m_uboData.x_max, clampR);
    float byMin = std::max(m_uboData.y_min, -clampR), byMax = std::min(m_uboData.y_max, clampR);
    float bzMin = std::max(m_uboData.z_min, -clampR), bzMax = std::min(m_uboData.z_max, clampR);
    if (bxMax <= bxMin) { bxMin = -clampR; bxMax = clampR; }
    if (byMax <= byMin) { byMin = -clampR; byMax = clampR; }
    if (bzMax <= bzMin) { bzMin = -clampR; bzMax = clampR; }

    // Griglia FITTA (40^3): i tubi dei tori sono sottili (raggio ~sqrt(0.4)~0.63);
    // una griglia grossa (12^3) li mancherebbe e non troverebbe i crossing. 40^3 = 64k
    // valutazioni exprtk: eseguito UNA volta per cambio-equazione, costo trascurabile.
    // Materializziamo l'intero campo in un buffer per poter confrontare i VICINI.
    const int N = 40;
    std::vector<double> vals(static_cast<size_t>(N) * N * N);
    auto idx = [N](int i, int j, int k) {
        return (static_cast<size_t>(i) * N + j) * N + k;
    };
    for (int i = 0; i < N; ++i) {
        x = bxMin + (bxMax - bxMin) * (i + 0.5) / N;
        for (int j = 0; j < N; ++j) {
            y = byMin + (byMax - byMin) * (j + 0.5) / N;
            for (int k = 0; k < N; ++k) {
                z = bzMin + (bzMax - bzMin) * (k + 0.5) / N;
                double v = parser.value();
                vals[idx(i, j, k)] = std::isfinite(v) ? v : std::numeric_limits<double>::quiet_NaN();
            }
        }
    }

    // Misuriamo max|f| SOLO nella banda near-surface: celle che hanno almeno un vicino
    // (6-connesso) di segno opposto. E' qui che il ramo trasparente aggancia i crossing;
    // e' qui che un campo a prodotto tradisce la sua scala enorme, mentre un polinomio
    // grado-alto sano (che esplode solo nel vuoto lontano) resta O(1).
    double maxNear = 0.0;
    auto opposite = [](double a, double b) {
        return std::isfinite(a) && std::isfinite(b) && ((a < 0.0) != (b < 0.0)) && a != 0.0 && b != 0.0;
    };
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < N; ++k) {
                double v = vals[idx(i, j, k)];
                if (!std::isfinite(v)) continue;
                bool onSurface =
                    (i + 1 < N && opposite(v, vals[idx(i + 1, j, k)])) ||
                    (i - 1 >= 0 && opposite(v, vals[idx(i - 1, j, k)])) ||
                    (j + 1 < N && opposite(v, vals[idx(i, j + 1, k)])) ||
                    (j - 1 >= 0 && opposite(v, vals[idx(i, j - 1, k)])) ||
                    (k + 1 < N && opposite(v, vals[idx(i, j, k + 1)])) ||
                    (k - 1 >= 0 && opposite(v, vals[idx(i, j, k - 1)]));
                if (onSurface) {
                    double av = std::abs(v);
                    if (av > maxNear) maxNear = av;
                }
            }

    // Soglia empirica sul RANGE DINAMICO del campo NEAR-SURFACE. Verificato valutando
    // TUTTI i preset impliciti Ray Marching su griglia 40^3: il "Chain" (prodotto di 6
    // tori) resta a ~1e8 anche sui suoi crossing, mentre OGNI superficie a espressione
    // singola sta sotto ~20 (peggiori: KleinIso ~21, DuplinCyclides ~14) — incluso il
    // "Filament Cube" grado ~12 che vale ~1 sulla superficie pur esplodendo a ~1e12 nel
    // vuoto lontano (era il falso positivo della vecchia soglia globale 1e10). La soglia
    // 1e5 lascia ~3.7 ordini di margine su ENTRAMBI i lati (sopra il peggior legittimo,
    // sotto il "Chain"): separazione netta e robusta.
    const double ILL_THRESHOLD = 1.0e5;
    m_implicitIllConditioned = (maxNear > ILL_THRESHOLD);

    if (m_implicitIllConditioned) {
        qDebug() << "[implicit] campo mal condizionato (prodotto): max|f| near-surface ="
                 << maxNear << "-> trasparenza disabilitata (fallback opaco)";
    }

#if defined(Q_OS_ANDROID)
    // TROPPE FACCE PER RAGGIO — SOLO ANDROID (desktop/iOS: questo blocco non esiste
    // nemmeno, #if compilato via). Il ramo trasparente RM compone al massimo MAX_FACES
    // gusci per pixel; su Android MAX_FACES/MAX_LAYER_STEPS sono ridotti (4/600 vs
    // 8/2000 desktop) come rete anti-timeout-GPU. Le superfici triplamente periodiche
    // (Gyroid, Lidinoid, Holes...) attraversano MOLTE facce lungo un raggio e sforano
    // quel budget: con alpha<1 su Android la superficie si TAGLIA (facce oltre MAX_FACES
    // non composte). Non potendo alzare i limiti senza reintrodurre il fault GPU,
    // AVVISIAMO l'utente: alziamo m_implicitTransparencyWarn -> lo slider resta USABILE
    // (nessun blocco, nessun fallback opaco) e la UI mostra un popup di avviso al primo
    // tocco dell'alpha. Diverso dal "Chain" (m_implicitIllConditioned), che invece
    // sparisce del tutto e resta bloccato+opaco su tutte le piattaforme.
    //
    // STIMA DELLE FACCE: contiamo i cambi di segno del campo lungo 7 raggi che
    // attraversano il CENTRO del box (3 assi + 4 diagonali dello spazio), a passo fine
    // 0.05 come il marcher. La griglia 40^3 sopra e' troppo grossa (celle ~0.5) per
    // contare le facce di un campo con periodo ~2*PI, e i raggi assiali sottostimano
    // (i raggi reali della camera lontana tagliano in diagonale): i raggi centrali
    // colgono il caso peggiore. Taratura su tutti i preset impliciti RM: le periodiche
    // problematiche danno 13-26 crossing, tutte le legittime <=6 (Chmutov 6, Blobs 4,
    // sfere/tori/quadriche <=4). Soglia 8 (= 2 * MAX_FACES Android) nel mezzo, con
    // margine su entrambi i lati. Solo per superfici a EQUAZIONE (parser CPU); gli
    // script RM sono avvisati preventivamente altrove (non valutabili su CPU).
    if (!m_implicitIllConditioned) {
        const int    ANDROID_MAX_FACES = 4;                 // deve combaciare col %MAX_FACES% Android
        const int    CROSSINGS_LIMIT   = 2 * ANDROID_MAX_FACES;
        const double rayStep = 0.05;
        const double cx = 0.5 * (bxMin + bxMax);
        const double cy = 0.5 * (byMin + byMax);
        const double cz = 0.5 * (bzMin + bzMax);
        const double hx = 0.5 * (bxMax - bxMin);
        const double hy = 0.5 * (byMax - byMin);
        const double hz = 0.5 * (bzMax - bzMin);
        const double dirs[7][3] = {
            {1,0,0}, {0,1,0}, {0,0,1},
            {1,1,1}, {1,-1,1}, {1,1,-1}, {-1,1,1}
        };
        int maxCrossings = 0;
        for (int r = 0; r < 7 && maxCrossings <= CROSSINGS_LIMIT; ++r) {
            double dx = dirs[r][0], dy = dirs[r][1], dz = dirs[r][2];
            double dlen = std::sqrt(dx*dx + dy*dy + dz*dz);
            dx /= dlen; dy /= dlen; dz /= dlen;
            // Estensione del raggio: mezza-diagonale del box, in entrambi i versi.
            double L = std::sqrt(hx*hx + hy*hy + hz*hz);
            int crossings = 0;
            double prev = 0.0; bool havePrev = false;
            for (double s = -L; s <= L; s += rayStep) {
                x = cx + dx * s; y = cy + dy * s; z = cz + dz * s;
                if (std::abs(x - cx) > hx || std::abs(y - cy) > hy || std::abs(z - cz) > hz)
                    continue; // fuori dal box di taglio
                double v = parser.value();
                if (!std::isfinite(v)) { havePrev = false; continue; }
                if (havePrev && ((prev < 0.0) != (v < 0.0)) && prev != 0.0 && v != 0.0)
                    ++crossings;
                prev = v; havePrev = true;
            }
            if (crossings > maxCrossings) maxCrossings = crossings;
        }
        if (maxCrossings > CROSSINGS_LIMIT) {
            m_implicitTransparencyWarn = true;
            qDebug() << "[implicit] Android: troppe facce per raggio (crossings ="
                     << maxCrossings << "> " << CROSSINGS_LIMIT
                     << ") -> avviso trasparenza (slider resta usabile)";
        }
    }
#endif
}

void GLWidget::setEquationConstants(float a, float b, float c, float d, float e, float f, float s) {
    engine->setConstants(a, b, c, d, e, f, s);
    // Salviamo nel pacchetto UBO!
    m_uboData.mathParams = QVector4D(a, b, c, s);
    m_uboData.mathParams2 = QVector4D(d, e, f, 0.0f);
    meshNeedsUpdate = true;

    m_constants.clear();
    m_constants["A"] = a;
    m_constants["B"] = b;
    m_constants["C"] = c;
    m_constants["D"] = d;
    m_constants["E"] = e;
    m_constants["F"] = f;
    m_constants["S"] = s;

    update();
}

void GLWidget::setRangeU(float min, float max) {
    if (engine) engine->setRangeU(min, max);
    m_uboData.u_min = min;
    m_uboData.u_max = max;
    meshNeedsUpdate = true;
}

void GLWidget::setRangeV(float min, float max) {
    if (engine) engine->setRangeV(min, max);
    m_uboData.v_min = min;
    m_uboData.v_max = max;
    meshNeedsUpdate = true;
}

void GLWidget::setRangeW(float min, float max)
{
    if (engine) engine->setRangeW(min, max);
    meshNeedsUpdate = true;
}

void GLWidget::setRangeX(float min, float max) {
    m_uboData.x_min = min;
    m_uboData.x_max = max;
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setRangeY(float min, float max) {
    m_uboData.y_min = min;
    m_uboData.y_max = max;
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setRangeZ(float min, float max) {
    m_uboData.z_min = min;
    m_uboData.z_max = max;
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setResolution(int n) {

    engine->setResolution(n, n);
    meshNeedsUpdate = true;
}

void GLWidget::setRaySteps(int steps) {
    m_raySteps = steps; // Salva il valore nello stato del Widget
    update();           // Richiede un nuovo fotogramma
}

bool GLWidget::setCustomMesh(const QVector<QVector<QVector4D>>& grid, bool tolerateTruncated)
{
    bool wasCustom = m_isCustomMesh; // Salviamo lo stato precedente
    m_isCustomMesh = true;

    if (grid.isEmpty() || grid[0].isEmpty()) return false;

    int numU = grid.size() - 1;
    int numV = grid[0].size() - 1;

    // =====================================================================
    // 1. CONTROLLO MATEMATICO GENERALE (Anticipazione Errori e Artefatti)
    // =====================================================================
    int deadTrajectories = 0;

    // Soglia coerente con safeLim in GeodesicCalculator, scalata in base alla scena.
    const float limitThreshold = 999.0f * m_surfaceScale / 2.0f;

    // NUOVO: Limite di salto. Se un singolo step schizza oltre 20 unità,
    // l'equazione differenziale sta esplodendo generando artefatti.
    const float jumpThreshold = 20.0f;

    for (int i = 0; i <= numU; ++i) {
        bool isDead = false;

        // A. Punto "nato morto" (il calcolatore ha intercettato un NaN al primo step)
        if (grid[i][0].lengthSquared() < 1e-8f && grid[i][1].lengthSquared() < 1e-8f) {
            isDead = true;
        } else {
            for (int j = 0; j <= numV; ++j) {
                // B. Controllo sul limite spaziale assoluto (Anticipato a 250)
                if (std::abs(grid[i][j].x()) > limitThreshold ||
                    std::abs(grid[i][j].y()) > limitThreshold ||
                    std::abs(grid[i][j].z()) > limitThreshold) {
                    isDead = true;
                    break;
                }

                // C. NUOVO: Controllo sull'esplosione della derivata (Jump Check)
                if (j > 0) {
                    float distSq = (grid[i][j] - grid[i][j-1]).lengthSquared();
                    if (distSq > (jumpThreshold * jumpThreshold)) {
                        isDead = true;
                        break;
                    }
                }
            }
        }
        if (isDead) deadTrajectories++;
    }

    // Blocca la mesh se più del 5% delle traiettorie sono collassate.
    // Con la metrica da script (tolerateTruncated) i troncamenti sono
    // fisiologici (singolarità, blow-up di coordinate): si rigetta solo se
    // sono collassate tutte le traiettorie, come nel GeodesicCalculator.
    if (tolerateTruncated ? (deadTrajectories > numU)
                          : (deadTrajectories > (numU * 0.05f))) {
        return false;
    }

    // =====================================================================
    // 1.5 MARCATURA DEI VERTICI "CONGELATI" (tail di truncate del calculator)
    // =====================================================================
    // Il GeodesicCalculator congela una traiettoria sull'ultimo punto valido
    // quando incontra un NaN. Quei punti hanno (grid[i][j] == grid[i][j-1])
    // esatto. Vanno esclusi dal rendering per non generare quad degeneri.
    //
    // Inoltre il calculator usa QVector4D(0,0,0,0) ESATTO come sentinella per un
    // punto "nato morto" (traiettoria che crasha al primo step, prima di avere un
    // ultimo-punto-buono a cui ripiegare: vedi scanDirection in
    // geodesiccalculator.cpp). Su alcune GPU (Mali) una singola traiettoria può
    // crashare così mentre su altre (Adreno) no: il vertice resta a (0,0,0,0),
    // NON è uguale al suo vicino (quindi sfugge al test di "congelato" sopra) e
    // viene triangolato verso i vicini sani, generando la scheggia che attraversa
    // la superficie (confermato: triangolo grid[0][0]=(0,0,0) -> grid[1][0],
    // grid[0][1], lato 2.17 vs mediana 0.04). Un vertice geodetico legittimo non
    // cade mai esattamente su tutte e 4 le coordinate nulle, quindi possiamo
    // trattare (0,0,0,0) esatto come la sentinella che è ed escluderlo dai quad.
    std::vector<bool> isFrozen((numU + 1) * (numV + 1), false);
    std::vector<bool> isDeadOrigin((numU + 1) * (numV + 1), false);
    for (int i = 0; i <= numU; ++i) {
        for (int j = 0; j <= numV; ++j) {
            const int idx = i * (numV + 1) + j;
            if (grid[i][j] == QVector4D(0.0f, 0.0f, 0.0f, 0.0f)) {
                isDeadOrigin[idx] = true;        // sentinella punto-morto: SEMPRE da escludere
                continue;
            }
            if (j >= 1 && (grid[i][j] - grid[i][j-1]).lengthSquared() < 1e-12f) {
                isFrozen[idx] = true;            // coda congelata di un troncamento
            }
        }
    }

#if GEO_DIAG_TRIANGLE
    // Quante sentinelle punto-morto sono state intercettate. Il fenomeno è
    // INTERMITTENTE (la stessa traiettoria su Mali a volte crasha a volte no, anche
    // a parità di binario): se questo numero oscilla tra 0 e >0 tra un caricamento
    // e l'altro è la conferma del non-determinismo a monte — e che la rete a valle
    // scatta quando serve. A regime (assenza di artefatto) il triangolo NON dipende
    // più da questo valore: con sentinella i quad sono esclusi, senza non ci sono.
    {
        int deadCount = 0;
        for (bool b : isDeadOrigin) if (b) deadCount++;
        qWarning("[GEO_TRI] dead-origin sentinels intercepted=%d", deadCount);
    }
#endif

    // =====================================================================
    // 2. COSTRUZIONE GEOMETRIA
    // =====================================================================
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    // Costruzione Vertici e Normali
    for (int i = 0; i <= numU; ++i) {
        for (int j = 0; j <= numV; ++j) {
            Vertex v;
            v.position = grid[i][j];

            auto p = [&](int x, int y) { return grid[x][y].toVector3D(); };
            QVector3D du, dv;

            if (i > 0 && i < numU) {
                du = grid[i+1][j].toVector3D() - grid[i-1][j].toVector3D();
            } else {
                if ((grid[numU][j] - grid[0][j]).lengthSquared() < 1e-5f) {
                    du = grid[1][j].toVector3D() - grid[numU-1][j].toVector3D();
                } else {
                    if (i == 0) du = grid[1][j].toVector3D() - grid[0][j].toVector3D();
                    else        du = grid[numU][j].toVector3D() - grid[numU-1][j].toVector3D();
                }
            }

            if (j > 0 && j < numV) {
                dv = grid[i][j+1].toVector3D() - grid[i][j-1].toVector3D();
            } else {
                if ((grid[i][numV] - grid[i][0]).lengthSquared() < 1e-5f) {
                    dv = grid[i][1].toVector3D() - grid[i][numV-1].toVector3D();
                } else {
                    if (j == 0) dv = grid[i][1].toVector3D() - grid[i][0].toVector3D();
                    else        dv = grid[i][numV].toVector3D() - grid[i][numV-1].toVector3D();
                }
            }

            // Normalizza i vettori in modo sicuro prima del prodotto vettoriale
            QVector3D safeDu = du.normalized();
            QVector3D safeDv = dv.normalized();

            // Calcola la normale (la sua lunghezza ora dipenderà solo dall'angolo tra du e dv)
            QVector3D normal = QVector3D::crossProduct(safeDu, safeDv);

            // Threshold per vertici degeneri / singolarità matematiche
            if (normal.lengthSquared() < 1e-5f) {
                if (j > 0) {
                    normal = vertices.back().normal.toVector3D();
                } else {
                    normal = QVector3D(0.0f, 0.0f, 1.0f);
                }
            } else {
                normal.normalize();
            }

            if (j > 0) {
                QVector3D prevNormal = vertices.back().normal.toVector3D();
                if (QVector3D::dotProduct(normal, prevNormal) < 0.0f) {
                    normal = -normal;
                }
            }

            v.normal = QVector4D(normal, 0.0f);
            v.texCoord = QVector2D((float)i / numU, (float)j / numV);
            vertices.push_back(v);
        }
    }

    // Costruzione Indici (Triangoli)
    for (int i = 0; i < numU; ++i) {
        for (int j = 0; j < numV; ++j) {
            int p0 = i * (numV + 1) + j;
            int p1 = (i + 1) * (numV + 1) + j;
            int p2 = i * (numV + 1) + (j + 1);
            int p3 = (i + 1) * (numV + 1) + (j + 1);

            // Sentinella punto-morto (0,0,0,0): basta UN vertice del quad per
            // saltarlo. È un punto inventato (traiettoria crashata al primo step),
            // non un vertice reale: triangolarlo verso i vicini sani produce la
            // scheggia che attraversa la superficie. Soglia >=1 (a differenza dei
            // frozen) perché qui NON c'è il rischio di bucare la mesh su un
            // artefatto di arrotondamento: o il punto è la sentinella esatta, o no.
            if (isDeadOrigin[p0] || isDeadOrigin[p1] || isDeadOrigin[p2] || isDeadOrigin[p3])
                continue;

            // Salta il quad se almeno 2 dei 4 vertici sono "frozen".
            // Tollerare 1 frozen evita di bucare la mesh per artefatti
            // di arrotondamento isolati; saltare a >=2 elimina i quad
            // dentro la coda congelata e sul confine con essa.
            int frozenCount = (isFrozen[p0] ? 1 : 0) +
                              (isFrozen[p1] ? 1 : 0) +
                              (isFrozen[p2] ? 1 : 0) +
                              (isFrozen[p3] ? 1 : 0);
            if (frozenCount >= 2) continue;

            indices.push_back(p0);
            indices.push_back(p1);
            indices.push_back(p2);

            indices.push_back(p2);
            indices.push_back(p1);
            indices.push_back(p3);
        }
    }

    // RILEVAMENTO TOPOLOGICO DELLA CHIUSURA (Cilindro o Möbius)
    bool isUClosed = true;
    bool isVClosed = true;
    float threshold = 1e-4f; // Tolleranza per l'errore del calcolo geodetico numerico

    // Controllo Chiusura su U
    for (int j = 0; j <= numV; ++j) {
        float distNormal = (grid[0][j] - grid[numU][j]).lengthSquared();
        float distTwisted = (grid[0][j] - grid[numU][numV - j]).lengthSquared();

        if (distNormal > threshold && distTwisted > threshold) {
            isUClosed = false;
            break;
        }
    }

    // Controllo Chiusura su V
    for (int i = 0; i <= numU; ++i) {
        float distNormal = (grid[i][0] - grid[i][numV]).lengthSquared();
        float distTwisted = (grid[i][0] - grid[numU - i][numV]).lengthSquared();

        if (distNormal > threshold && distTwisted > threshold) {
            isVClosed = false;
            break;
        }
    }

#if GEO_DIAG_TRIANGLE
    // --- Diagnostica triangolo-artefatto sulla MESH FINALE --------------------
    // Esamina i triangoli realmente inviati alla GPU. Un artefatto = un triangolo
    // con un lato enormemente più lungo della mediana di tutti i lati (una
    // scheggia che attraversa la superficie). Logghiamo i peggiori con indici e
    // posizioni 3D dei 3 vertici, così si risale al vertice colpevole e al perché
    // è finito nella mesh. Su Android → logcat (qWarning, priorità W).
    {
        auto vpos = [&](unsigned int idx) {
            return vertices[idx].position.toVector3D();
        };
        QVector<float> edges;
        edges.reserve(indices.size());
        auto triLongestEdge = [&](unsigned int a, unsigned int b, unsigned int c) {
            const QVector3D pa = vpos(a), pb = vpos(b), pc = vpos(c);
            return std::max({ (pa-pb).length(), (pb-pc).length(), (pc-pa).length() });
        };
        for (size_t k = 0; k + 2 < indices.size(); k += 3)
            edges.push_back(triLongestEdge(indices[k], indices[k+1], indices[k+2]));

        if (!edges.isEmpty()) {
            QVector<float> s = edges; std::sort(s.begin(), s.end());
            const float median = s[s.size()/2];
            const float maxEdge = s.last();
            const float thr = std::max(median * 20.0f, 1e-4f);
            qWarning("[GEO_TRI] tris=%lld median edge=%.4f  maxEdge=%.4f  thr=%.4f",
                     static_cast<long long>(edges.size()), median, maxEdge, thr);
            int reported = 0;
            for (size_t k = 0; k + 2 < indices.size() && reported < 12; k += 3) {
                const unsigned int a=indices[k], b=indices[k+1], c=indices[k+2];
                if (triLongestEdge(a,b,c) > thr) {
                    const QVector3D pa=vpos(a), pb=vpos(b), pc=vpos(c);
                    qWarning("[GEO_TRI] BIG tri longest=%.3f  idx(%u,%u,%u) -> (%d,%d)(%d,%d)(%d,%d)  A=(%.3f,%.3f,%.3f) B=(%.3f,%.3f,%.3f) C=(%.3f,%.3f,%.3f)",
                             triLongestEdge(a,b,c), a,b,c,
                             a/(numV+1), a%(numV+1), b/(numV+1), b%(numV+1), c/(numV+1), c%(numV+1),
                             pa.x(),pa.y(),pa.z(), pb.x(),pb.y(),pb.z(), pc.x(),pc.y(),pc.z());
                    reported++;
                }
            }
            if (reported == 0)
                qWarning("[GEO_TRI] no oversized triangle (all edges <= %.4f)", thr);
        }
    }
#endif

    // 3. Invia i dati al SurfaceEngine
    engine->setCustomMesh(vertices, indices, isUClosed, isVClosed);

    buildWireframeGeometry();

    meshNeedsUpdate = true;

    if (!wasCustom) {
        rebuildShader();
    }

    update();
    return true; // Costruzione completata con successo!
}


// ==========================================================
// RENDERING & VISUALS
// ==========================================================

void GLWidget::updateSurfaceData()
{
    // 1. Calcola i nuovi vertici sulla CPU usando il tuo Engine
    engine->computeMesh();

    if (m_isFlatView) {
        meshNeedsUpdate = false;
        return;
    }

    buildWireframeGeometry();

    // 2. Diciamo al Render Pass che i dati sono pronti per essere spediti alla GPU
    meshNeedsUpdate = true;

    // Le parti possono essere cambiate di numero o essere state rigenerate:
    // avvisa la UI (selettore di mesh + aspetto per-mesh in sospeso).
    emit meshPartsChanged();

    // 3. Forza il ridisegno
    update();
}

void GLWidget::resetVisuals()
{
    engine->clear();
    m_lightingMode4D = 0;

    m_wireframeIndexCount = 0;

    meshNeedsUpdate = true; // Diciamo a RHI di svuotare i buffer al prossimo render()

    if (m_animTimer->isActive()) {
        m_animTimer->stop();
    }

    QImage defaultImg(":/background.png");
    if (!defaultImg.isNull()) {

        m_useBackgroundTexture = false; // La manteniamo disattivata

        // Se lo sfondo precedente usava uno Script, forziamo il ripristino
        if (m_bgIsScript) {
            rebuildBackgroundShader(true, "");
        }
        m_bgIsScript = false;

        // Azzera i parametri dinamici dello sfondo
        setProperty("bg_zoom", 1.0f);
        setProperty("bg_pan", QVector2D(0.0f, 0.0f));
        setProperty("bg_rot", 0.0f);

        // Azzera i parametri della superficie: sia il buffer di lavoro sia la
        // trasformazione GLOBALE, o quest'ultima sopravviverebbe allo
        // spegnimento della texture e tornerebbe applicata alla successiva.
        m_flatZoom = 1.0f;
        m_flatPan = QVector2D(0.0f, 0.0f);
        m_flatRotation = 0.0f;
        m_globalTexZoom = 1.0f;
        m_globalTexPan = QVector2D(0.0f, 0.0f);
        m_globalTexRotation = 0.0f;

        update();
    }
}

void GLWidget::setProjectionMode(int mode) {
    projectionMode = mode;           // Serve ancora alla CPU per projectPoint4Dto3D
    m_uboData.projMode = mode;       // Sincronizza con la GPU
    meshNeedsUpdate = true;
    update();
}

// ==========================================================
// ASPETTO PER-MESH
// ==========================================================
// m_activeMeshPart e' l'indice scelto con lo spinbox del dock renderer:
// -1 (voce "All") = i controlli agiscono sullo stato GLOBALE, come da sempre;
// >= 0 = agiscono solo su quella parte, che smette di ereditare.
// Le superfici a mesh singola non ne risentono: lo spinbox resta su "All" e
// nessuna parte dichiara un aspetto proprio, quindi il percorso e' invariato.
void GLWidget::setActiveMeshPart(int index) {
    const int n = engine ? engine->getMeshPartCount() : 0;
    const int next = (index >= 0 && index < n) ? index : -1;
    if (next == m_activeMeshPart) { update(); return; }

    // CAMBIO DI MESH MENTRE SI EDITA IN VISTA 2D: zoom/pan/rotazione correnti
    // appartengono ancora alla parte che si sta lasciando. Vanno fissati PRIMA
    // di spostare l'indice, o resterebbero nei membri globali e verrebbero
    // riscritti sulla parte NUOVA (il ridimensionamento della mesh 1 si
    // ritrovava sulla mesh 2, e da li' su tutte).
    if (m_isFlatView) commitFlatTransformToActivePart();

    m_activeMeshPart = next;

    // ...e la vista 2D deve ora mostrare l'inquadratura della parte entrante.
    if (m_isFlatView) loadFlatTransformFromActivePart();

    update();
}

// Applica una modifica di aspetto: alla parte attiva se ce n'e' una, altrimenti
// allo stato globale. Ritorna true se ha scritto su una parte, cosi' i setter
// sanno se devono anche aggiornare il proprio membro globale.
bool GLWidget::applyToActiveMeshPart(const std::function<void(MeshPart&)> &fn) {
    // Bypass: il chiamante sta eseguendo un reset automatico (non un comando
    // dell'utente), quindi deve finire sullo stato globale anche se una mesh e'
    // selezionata. Senza questo, entrando in wireframe il reset di alpha/luce
    // veniva scritto sulla parte attiva, che smetteva di ereditare.
    if (m_meshAppearanceBypass) return false;
    if (m_activeMeshPart < 0 || !engine) return false;
    MeshPart *p = engine->mutableMeshPart(m_activeMeshPart);
    if (!p) return false;
    fn(*p);
    // L'aspetto va ricopiato sulle parti DICHIARATE, o la prossima
    // rigenerazione della griglia lo riporterebbe ai valori dello script.
    engine->syncPartAppearance();
    update();
    return true;
}

// SEPARAZIONE DISPLAY / COMANDO (il punto che rende stabile il wireframe
// per-mesh). setRenderMode e' la via da cui passa updateRenderState, che gira a
// ogni cambio tab, cambio proiezione, load e a ogni toggled dei radio: qui NON
// si scrive mai su una parte, si scrive SOLO lo stato globale. I radio restano
// un DISPLAY della mesh selezionata; la sorgente di una modalita' per-parte e'
// soltanto setActiveMeshRenderMode, chiamata dal gestore dei radio quando e'
// l'utente a muoverli.
//
// Il tentativo precedente falliva proprio qui: facendo scrivere questa funzione
// sulla parte attiva, ogni updateRenderState riapplicava il valore a un
// destinatario che dipendeva dallo stato del momento, e il wireframe si
// propagava a tutte le parti che ereditano.
void GLWidget::setRenderMode(int mode) {
    this->renderMode = mode;
    update();
}

void GLWidget::setMeshAppearanceUniform(bool on) {
    if (m_meshAppearanceUniform == on) return;
    m_meshAppearanceUniform = on;
    // Le densita' wireframe per-parte sono INDICI, non uniform: sospenderle
    // richiede di ricostruire la geometria delle linee.
    buildWireframeGeometry();
    // Le texture per-mesh vivono nel CODICE del fragment shader: in "All" il
    // dispatcher non deve esistere affatto, in "Mesh" si', quindi il passaggio
    // fra i due ambiti e' un cambio di sorgente e va ricompilato.
    rebuildShader();
    update();
}

// TEXTURE PROCEDURALE DELLA PARTE ATTIVA. Come setActiveMeshRenderMode, e' la
// via di COMANDO: ci passa solo l'utente che preme Run con una mesh selezionata.
// In "All" (nessuna parte attiva) ricade sul comportamento di sempre, cioe'
// texture globale della superficie: lo decide il chiamante, qui torniamo false.
// rebuildShader e' necessario perche' il codice della texture vive DENTRO il
// fragment shader (getCustomColor_<k>), non in una risorsa: cambiare lo script
// di una mesh significa ricompilare, esattamente come per la texture globale.
bool GLWidget::setActiveMeshTexture(const QString &code, bool enabled) {
    const bool onPart = applyToActiveMeshPart([&](MeshPart &p){
        p.textureCode = code;
        p.textureEnabled = enabled;
        p.hasCustomTexture = true;
        // I colori correnti diventano PROPRI della parte: altrimenti restano
        // condivisi con le altre mesh e la texture applicata dopo riscrive i
        // colori di quelle di prima.
        // Solo se la parte non ne ha GIA' di propri: chi la configura puo'
        // averli appena scritti con setActiveMeshTexColors, e quelli devono
        // vincere sui due slot globali (che appartengono alla superficie).
        if (!p.hasCustomTexColors()) {
            p.texCol1R = texRed1; p.texCol1G = texGreen1; p.texCol1B = texBlue1;
            p.texCol2R = texRed2; p.texCol2G = texGreen2; p.texCol2B = texBlue2;
        }
    });
    if (onPart) rebuildShader();
    return onPart;
}

// Colori u_col1/u_col2 PROPRI della parte attiva. Non tocca i due slot globali
// (texRed1..texBlue2), che appartengono alla texture di SUPERFICIE.
// Nessun rebuildShader: i colori vivono nel blocco UBO, non nel codice.
bool GLWidget::setActiveMeshTexColors(const QColor &c1, const QColor &c2) {
    return applyToActiveMeshPart([&](MeshPart &p){
        p.texCol1R = c1.redF();   p.texCol1G = c1.greenF();   p.texCol1B = c1.blueF();
        p.texCol2R = c2.redF();   p.texCol2G = c2.greenF();   p.texCol2B = c2.blueF();
    });
}

// Spegne la texture propria della parte attiva e la fa tornare a EREDITARE dal
// globale (hasCustomTexture = false), che e' diverso da "texture spenta".
bool GLWidget::clearActiveMeshTexture() {
    const bool onPart = applyToActiveMeshPart([](MeshPart &p){
        p.textureCode.clear();
        p.textureEnabled = false;
        p.hasCustomTexture = false;
    });
    if (onPart) rebuildShader();
    return onPart;
}

// Codice della texture della parte attiva, per il DISPLAY nell'editor.
// Vuoto se non ne ha una propria o se siamo in "All".
QString GLWidget::activeMeshTextureCode() const {
    if (m_activeMeshPart < 0 || !engine) return QString();
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return QString();
    const MeshPart &p = parts[m_activeMeshPart];
    return p.hasCustomTexture ? p.textureCode : QString();
}

bool GLWidget::activeMeshHasOwnTexture() const {
    if (m_activeMeshPart < 0 || !engine) return false;
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return false;
    return parts[m_activeMeshPart].hasCustomTexture;
}

void GLWidget::setActiveMeshRenderMode(int mode) {
    // Nessuna parte selezionata ("All") o bypass attivo: e' una scelta globale.
    if (!applyToActiveMeshPart([mode](MeshPart &p){
            p.renderMode = mode;
            p.hasCustomRenderMode = true;
        })) {
        this->renderMode = mode;
        update();
    }
    // La densita' wireframe dipende da quali parti sono in wireframe e con che
    // stride: la geometria va ricostruita.
    buildWireframeGeometry();
    update();
}

void GLWidget::clearActiveMeshRenderMode() {
    applyToActiveMeshPart([](MeshPart &p){
        p.hasCustomRenderMode = false;
        p.renderMode = 0;
    });
    buildWireframeGeometry();
    update();
}

// Rende PROPRIA di ogni parte la modalita' che sta ereditando dal globale. Non
// cambia nulla di visibile: a ogni parte si scrive esattamente il valore che
// effectiveRenderMode gia' restituiva per lei.
// A cosa serve: dall'ambito "All" un click sui radio cambia il GLOBALE, e tutte
// le parti senza modalita' propria lo seguono. Il preset "Hopf Tori Mesh Colors"
// ha globale = Phong e solo due mesh con wireframe proprio: mettendo tutto in
// wireframe da "All" le altre ereditavano il 2 e, tornando su "Mesh", la
// superficie era interamente wireframe. I dati per-mesh non erano stati persi
// (le due mesh avevano ancora il loro 2): era la BASE ereditata a essersi
// spostata sotto di loro. Congelandola prima, l'aspetto misto sopravvive.
void GLWidget::pinInheritedRenderModes() {
    if (!engine) return;
    const int n = engine->getMeshPartCount();
    if (n <= 1) return;              // mesh singola: nessun aspetto per-parte in gioco
    for (int k = 0; k < n; ++k) {
        MeshPart *p = engine->mutableMeshPart(k);
        if (!p || p->hasCustomRenderMode) continue;   // chi ha gia' la sua non si tocca
        p->renderMode = renderMode;                   // = quello che stava ereditando
        p->hasCustomRenderMode = true;
    }
    // Come in applyToActiveMeshPart: senza questo la prossima rigenerazione
    // della griglia riporterebbe le parti ai valori dello script.
    engine->syncPartAppearance();
}

int GLWidget::activeMeshEffectiveRenderMode() const {
    if (m_activeMeshPart < 0 || !engine) return renderMode;
    const auto &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return renderMode;
    return parts[m_activeMeshPart].effectiveRenderMode(renderMode);
}

void GLWidget::setActiveMeshWireframeDensity(int uStep, int vStep) {
    // 0 = eredita; altrimenti clamp come i tasti +/- globali.
    const int u = (uStep <= 0) ? 0 : std::clamp(uStep, STEP_MIN, STEP_MAX);
    const int v = (vStep <= 0) ? 0 : std::clamp(vStep, STEP_MIN, STEP_MAX);
    if (!applyToActiveMeshPart([u,v](MeshPart &p){ p.wfStepU = u; p.wfStepV = v; })) {
        // "All": e' la densita' globale, percorso storico.
        setWireframeDensity(u > 0 ? u : wfStepU, v > 0 ? v : wfStepV);
        return;
    }
    buildWireframeGeometry();
    update();
}

void GLWidget::activeMeshWireframeDensity(int &uStep, int &vStep) const {
    uStep = 0; vStep = 0;
    if (m_activeMeshPart < 0 || !engine) return;
    const auto &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return;
    uStep = parts[m_activeMeshPart].wfStepU;
    vStep = parts[m_activeMeshPart].wfStepV;
}

void GLWidget::setColor(float r, float g, float b) {
    if (applyToActiveMeshPart([r,g,b](MeshPart &p){ p.colorR=r; p.colorG=g; p.colorB=b; })) return;
    this->red = r;
    this->green = g;
    this->blue = b;
    // Rimuovi l'assegnazione diretta a m_uboData qui, lo fa già il render()
    update();
}

void GLWidget::setAlpha(float a) {
    if (applyToActiveMeshPart([a](MeshPart &p){ p.alpha = a; })) return;
    this->alpha = a;
    update();
}

void GLWidget::setSpecularEnabled(bool enabled) {
    m_isSpecularEnabled = enabled;
    update();
}

void GLWidget::setLightIntensity(float intensity) {
    if (applyToActiveMeshPart([intensity](MeshPart &p){ p.lightIntensity = intensity; })) return;
    this->m_lightIntensity = intensity;
    update();
}

// I tasti +/- della densita' agiscono sulla mesh SELEZIONATA se ce n'e' una,
// altrimenti sullo stato globale (voce "All"): stesso schema di colore/alpha.
// La parte parte dal valore che stava gia' usando (il proprio se dichiarato,
// altrimenti il globale), cosi' il primo click non fa un salto.
// NB: lo scambio U<->V e' storico e va conservato (il tasto "U" muove wfStepV).
bool GLWidget::adjustActiveWireframeStep(bool isU, int delta) {
    MeshPart *p = nullptr;
    if (!m_meshAppearanceBypass && m_activeMeshPart >= 0 && engine)
        p = engine->mutableMeshPart(m_activeMeshPart);
    if (!p) return false;

    int &partStep = isU ? p->wfStepV : p->wfStepU;
    const int fallback = isU ? wfStepV : wfStepU;
    const int cur = (partStep > 0) ? partStep : fallback;
    partStep = std::clamp(cur + delta, STEP_MIN, STEP_MAX);

    engine->syncPartAppearance();
    buildWireframeGeometry();
    update();
    return true;
}

void GLWidget::increaseWireframeUDensity() {
    if (adjustActiveWireframeStep(true, -1)) return;
    if (wfStepV > STEP_MIN) wfStepV--;
    buildWireframeGeometry();
    update();
}

void GLWidget::decreaseWireframeUDensity() {
    if (adjustActiveWireframeStep(true, +1)) return;
    if (wfStepV < STEP_MAX) wfStepV++;
    buildWireframeGeometry();
    update();
}

void GLWidget::increaseWireframeVDensity() {
    if (adjustActiveWireframeStep(false, -1)) return;
    if (wfStepU > STEP_MIN) wfStepU--;
    buildWireframeGeometry();
    update();
}

void GLWidget::decreaseWireframeVDensity() {
    if (adjustActiveWireframeStep(false, +1)) return;
    if (wfStepU < STEP_MAX) wfStepU++;
    buildWireframeGeometry();
    update();
}

void GLWidget::setWireframeDensity(int uStep, int vStep) {
    // Clamp ai limiti dei tasti +/- (STEP_MIN..STEP_MAX): un preset con valori fuori
    // range o corrotto non deve produrre una geometria degenere.
    wfStepU = std::clamp(uStep, STEP_MIN, STEP_MAX);
    wfStepV = std::clamp(vStep, STEP_MIN, STEP_MAX);
    // Ricostruiamo solo se c'è un engine valido: al load la mesh potrebbe non essere
    // ancora pronta, nel qual caso basta aver impostato wfStepU/V (la geometria sarà
    // costruita con questi valori quando la superficie viene caricata). Vedi resetWireframeDensity.
    if (engine) {
        buildWireframeGeometry();
        update();
    }
}

void GLWidget::resetWireframeDensity() {
    if (wfStepU == STEP_DEF && wfStepV == STEP_DEF) return; // già al default
    wfStepU = STEP_DEF;
    wfStepV = STEP_DEF;
    // Ricostruiamo la geometria wireframe solo se c'è un engine valido: chiamato anche
    // su cambio superficie/tab, quando la nuova mesh potrebbe non essere ancora pronta.
    // In quel caso basta aver riportato wfStepU/V al default: la geometria sarà costruita
    // con questi valori quando la superficie viene caricata.
    if (engine) {
        buildWireframeGeometry();
        update();
    }
}

void GLWidget::rebuildShader()
{
    if (m_pipelineOpaque) {
        delete m_pipelineOpaque;
        m_pipelineOpaque = nullptr;
    }

    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }

    meshNeedsUpdate = true;

    // Riarmo del watchdog di performance: rebuildShader() e' l'imbuto di OGNI
    // cambio che altera il peso GPU della scena (nuovo preset/superficie/record,
    // Run script, toggle trasparenza/RM, parametri pesanti come steps). "Un
    // avviso per scena, finche' non cambiano le impostazioni": qui le
    // impostazioni sono cambiate, quindi un futuro rallentamento potra'
    // riavvisare. NON riarmato dallo stop/riavvio dei moti (vedi ramo !animating
    // in render(), da cui il reset del flag e' stato tolto apposta).
    m_perfWarnDismissed = false;
    m_perfWarnLevelMs = 0.0f;
    // Misura FRESCA per la nuova scena: senza questo azzeramento l'EMA della
    // scena PRECEDENTE sopravviveva al cambio di preset e faceva scattare a
    // vuoto la conferma trasparenza (renderingUnderHeavyLoad) al primo tocco
    // dell'alpha su un preset appena caricato. Se la scena nuova e' davvero
    // pesante l'EMA risale in 2-3 frame (peso 0.35 in salita).
    m_avgFrameMs = 16.0f;
    m_slowAccumMs = 0.0f;
    m_slowFrameRun = 0;
    m_hugeFrameRun = 0;

    update();
}

void GLWidget::setRecordingActive(bool on)
{
    m_isRecording = on;
    if (!on) {
        // Ritorno dall'export: il transitorio di ripristino (endHiResCapture,
        // resize dell'FBO alla dimensione a schermo, eventuale ricompilazione)
        // produce alcuni frame lenti che NON sono carico GPU reale. Su desktop
        // sono trascurabili, su mobile bastano a sforare il dwell e a far
        // ricomparire il popup SUBITO DOPO la registrazione (fuori tempo massimo).
        // Riarmiamo lo stato di MISURA e apriamo una finestra di grazia: entro
        // kPerfGraceMs il watchdog scarta ogni misura, cosi' l'INTERO transitorio
        // di ripristino (di durata variabile su mobile: puo' durare piu' frame,
        // non solo il primo) non viene scambiato per un rallentamento reale. NON
        // tocchiamo m_perfWarnDismissed: se avevamo gia' avvisato per questa scena
        // PRIMA di registrare, resta zitto.
        m_avgFrameMs = 16.0f;
        m_slowAccumMs = 0.0f;
        m_perfWarnLevelMs = 0.0f;
        m_slowFrameRun = 0;
        m_hugeFrameRun = 0;
        m_wasAnimating = false;      // -> justResumed=true: salta comunque il 1o frame
        m_perfGraceClock.start();    // apre la finestra di grazia post-export
    }
}


// ==========================================================
// TEXTURES, SCRIPTS & BACKGROUND
// ==========================================================

void GLWidget::loadTextureFromFile(const QString &f) {
    QImage img(f);
    if (!img.isNull()) {
        loadTextureFromImage(img);
    }
}

void GLWidget::loadTextureFromImage(const QImage &img) {
    if (img.isNull()) return;

    // 1. RHI richiede formati precisi (RGBA8888)
    m_pendingSurfaceImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));

    // 2. Alziamo la bandierina per il Render Loop
    m_surfaceTextureNeedsUpload = true;

    rebuildShader();
    update();
}

void GLWidget::setTextureEnabled(bool enable) {
    if (m_textureEnabled != enable) {
        m_textureEnabled = enable;

        if (m_engineMode == ModeImplicit && m_pipelineImplicit) {
            m_pipelineImplicit->destroy();
            delete m_pipelineImplicit;
            m_pipelineImplicit = nullptr;
        }

        update();
    }
}

void GLWidget::setTextureColors(const QColor& c1, const QColor& c2)
{
    texRed1 = c1.redF();
    texGreen1 = c1.greenF();
    texBlue1 = c1.blueF();

    texRed2 = c2.redF();
    texGreen2 = c2.greenF();
    texBlue2 = c2.blueF();

    update();
}

void GLWidget::clearTexture() {
    // Svuota l'immagine in attesa
    m_pendingSurfaceImage = QImage();
    m_surfaceTextureNeedsUpload = false;

    // Distruzione effettiva dell'oggetto RHI rinviata al render loop: svuotare solo
    // m_pendingSurfaceImage NON bastava (m_surfaceTexture restava residente sulla GPU
    // e riaffiorava cambiando superficie / riaccendendo la texture). Il flag forza lo
    // scarico al prossimo frame, nel contesto RHI corretto.
    m_surfaceTextureNeedsClear = true;

    // Al prossimo frame tornerà tutto nero/colore base se disattiviamo m_textureEnabled
    setTextureEnabled(false);
    update();
}

void GLWidget::setScriptCheck(bool enabled) {
    engine->setScriptMode(enabled);
    meshNeedsUpdate = true;
}

bool GLWidget::loadCustomShader(const QString &customCode)
{
    if (!validateAndApplyParametricShader(customCode)) {
        qWarning() << "loadCustomShader: codice non valido, non applicato:"
                   << m_lastCompilationError;
        return false;
    }
    return true;
}

void GLWidget::setShaderTime(float t) {
    m_manualTime = t;
}

void GLWidget::setCameraFov(float deg) {
    // Clamp: sotto ~20° l'immagine è un teleobiettivo inutilizzabile negli
    // interni, sopra ~110° la prospettiva rettilinea degenera ai bordi.
    m_cameraFov = qBound(20.0f, deg, 110.0f);
    update();
}

void GLWidget::setBackgroundColor(const QColor &c) {
    m_bgColor = QVector3D(c.redF(), c.greenF(), c.blueF());
    update();
}


void GLWidget::setBackgroundTexture(const QString &path) {
    QImage img;

    // 1. Tenta il percorso letterale esatto fornito (es. "background.png")
    img.load(path);

    // 2. Tenta la risorsa interna Qt (utile se in futuro lo metti nel .qrc)
    if (img.isNull() && !path.startsWith(":/")) {
        img.load(":/" + path);
    }

    // 3. Tenta la cartella in cui si trova l'eseguibile (.exe o AppBundle)
    if (img.isNull()) {
        img.load(QCoreApplication::applicationDirPath() + "/" + path);
    }

    // 4. SALVAVITA: Se l'immagine è persa, generiamo un gradiente visibile!
    if (img.isNull()) {
        qWarning() << "Warning: Failed to load background image:" << path << "- Generating fallback.";
        img = QImage(512, 512, QImage::Format_RGBA8888);
        QPainter p(&img);
        QLinearGradient grad(0, 0, 0, 512);
        grad.setColorAt(0, QColor("#1e1e1e")); // Grigio scuro in alto
        grad.setColorAt(1, QColor("#4a4a4a")); // Grigio più chiaro in basso
        p.fillRect(0, 0, 512, 512, grad);
        p.end();
    }

    // A questo punto 'img' ha SEMPRE dei pixel validi da mostrare a Vulkan
    m_pendingBackgroundImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));

    m_backgroundTextureNeedsUpload = true;
    m_bgIsScript = false;
    rebuildBackgroundShader(true, "");

    update();
}

void GLWidget::setBackgroundTextureEnabled(bool enabled) {
    m_useBackgroundTexture = enabled;

    if (enabled) {
        // 1. Se dall'avvio del programma non abbiamo NESSUNA immagine in memoria,
        // carichiamo quella di default (nella working directory).
        if (m_pendingBackgroundImage.isNull() && !m_backgroundTexture) {
            setBackgroundTexture("background.png");
        }
        // 2. Altrimenti, l'immagine c'è già (es. l'ultima usata). Dobbiamo solo
        // assicurarci che la pipeline Vulkan sia pronta per disegnarla.
        else {
            if (!m_bgPipeline) {
                // Ricostruisce lo shader in base allo stato in cui l'avevamo lasciato
                rebuildBackgroundShader(!m_bgIsScript, m_bgScriptCode);
            }
            update();
        }
    } else {
        // Se stiamo spegnendo lo sfondo, basta aggiornare lo schermo
        update();
    }
}

void GLWidget::loadBackgroundScript(const QString &scriptCode) {
    m_bgScriptCode = scriptCode; // Memorizza il codice
    m_bgIsScript = true;         // Alziamo la bandierina dello script

    m_useBackgroundTexture = true;

    // Ricostruiamo la pipeline Vulkan
    rebuildBackgroundShader(false, m_bgScriptCode);

    // Diciamo allo schermo di aggiornarsi
    update();
}

void GLWidget::setTextureCode(const QString& code) {
    if (m_textureCode == code) return;

    QString oldTex = m_textureCode;
    m_textureCode = code;

    QString fsSource = createImplicitFragmentShader();
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        // Rollback silenzioso
        m_textureCode = oldTex;
        return;
    }

    if (m_pipelineImplicit) {
        m_pipelineImplicit->destroy();
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    update();
}

void GLWidget::setDisplacementCode(const QString& code) {
    if (m_displacementCode == code) return;

    QString oldDisp = m_displacementCode;
    m_displacementCode = code;

    QString fsSource = createImplicitFragmentShader();
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_displacementCode = oldDisp;
        return;
    }

    if (m_pipelineImplicit) {
        m_pipelineImplicit->destroy();
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    update();
}

// ==========================================================
// 2D FLAT VIEW
// ==========================================================

// Fissa sul destinatario corrente zoom/pan/rotazione 2D.
// Va chiamata a OGNI modifica, non solo all'uscita dalla vista 2D: finche' la
// trasformazione vive solo nel buffer di lavoro (m_flatZoom/m_flatPan/
// m_flatRotation) appartiene alla vista, non a cio' che si sta editando.
// Scrivendola subito nella parte, hasCustomTexTransform() diventa vero e il ramo
// per-parte del loop UBO la rimette al suo posto.
// Il destinatario e' la parte attiva, o lo stato GLOBALE in ambito "All": sono i
// due casi che il render sa disegnare (vedi flatEditingGlobal e il clamp del
// quad 2D), e vanno tenuti allineati o il mouse scrive dove nessuno legge.
// NB: NON si richiede hasCustomTexture. Due mesh possono avere LA STESSA texture
// e volerla a scale diverse: la trasformazione e' una proprieta' della parte, non
// dello script. Con quel requisito una fascia senza texture propria restava
// agganciata allo zoom globale e si ridimensionava insieme alle altre.
// Solo per la texture di SUPERFICIE (m_flatViewTarget 0): il target 1 e' lo
// sfondo, che ha i propri bg_zoom/bg_pan/bg_rot ed e' unico.
void GLWidget::commitFlatTransformToActivePart() {
    if (m_flatViewTarget != 0) return;

    // AMBITO "ALL" (nessuna parte selezionata): si sta regolando la texture
    // della SUPERFICIE, quindi la trasformazione appartiene allo stato globale.
    // Senza questo ramo, in "All" il mouse muoveva solo il buffer di lavoro e
    // l'inquadratura si perdeva uscendo dalla vista 2D.
    if (m_activeMeshPart < 0) {
        m_globalTexZoom = m_flatZoom;
        m_globalTexPan = m_flatPan;
        m_globalTexRotation = m_flatRotation;
        return;
    }

    if (!engine) return;
    MeshPart *p = engine->mutableMeshPart(m_activeMeshPart);
    if (!p) return;
    p->texZoom = m_flatZoom;
    p->texPanX = m_flatPan.x();
    p->texPanY = m_flatPan.y();
    p->texRotation = m_flatRotation;
    engine->syncPartAppearance();
}

// Carica nei membri globali la trasformazione della parte attiva, cosi' la vista
// 2D parte dall'inquadratura di QUELLA mesh e non da quella dell'ultima toccata.
// Una parte che non ne ha ancora una eredita quella corrente (non si azzera:
// sarebbe un salto visivo a ogni cambio di fascia).
void GLWidget::loadFlatTransformFromActivePart() {
    if (m_flatViewTarget != 0) return;

    // AMBITO "ALL": il buffer di lavoro parte dalla trasformazione GLOBALE, che
    // e' quella che si sta per regolare.
    if (m_activeMeshPart < 0) {
        m_flatZoom = m_globalTexZoom;
        m_flatPan = m_globalTexPan;
        m_flatRotation = m_globalTexRotation;
        return;
    }

    if (!engine) return;
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return;
    const MeshPart &p = parts[m_activeMeshPart];
    // Una parte senza trasformazione propria parte da quella GLOBALE, che e'
    // esattamente cio' che sta ereditando e disegnando: cosi' il primo
    // trascinamento non fa un salto.
    if (p.hasCustomTexTransform()) {
        m_flatZoom = p.texZoom;
        m_flatPan = QVector2D(p.texPanX, p.texPanY);
        m_flatRotation = p.texRotation;
    } else {
        m_flatZoom = m_globalTexZoom;
        m_flatPan = m_globalTexPan;
        m_flatRotation = m_globalTexRotation;
    }
}

void GLWidget::setFlatView(bool active) {
    const bool wasFlat = m_isFlatView;
    m_isFlatView = active;

    if (!m_isFlatView) {
        // USCITA DALLA VISTA 2D: fissa sulla parte quanto regolato col mouse.
        if (wasFlat) commitFlatTransformToActivePart();
        buildWireframeGeometry();
    }
    else {
        // INGRESSO in vista 2D: si edita la texture della parte selezionata,
        // quindi si riparte dalla SUA trasformazione, non da quella globale
        // (altrimenti la texture apparirebbe con lo zoom di un'altra mesh).
        loadFlatTransformFromActivePart();
    }

    meshNeedsUpdate = true;
    update();
}

// NB: i getter espongono il BUFFER DI LAVORO, non lo stato globale. Li usa
// inputhandler per il trascinamento incrementale (legge, somma, riscrive) e
// devono quindi riflettere cio' che il mouse sta muovendo sul destinatario
// corrente. Per la persistenza esistono i globalTex*() (vedi glwidget.h), che
// salvano l'inquadratura della texture di SUPERFICIE.
float GLWidget::getFlatZoom() const {
    if (m_flatViewTarget == 1) {
        return property("bg_zoom").isValid() ? property("bg_zoom").toFloat() : 1.0f;
    }
    return m_flatZoom;
}

void GLWidget::setFlatZoom(float z) {
    if (m_flatViewTarget == 1) { // 1 = Sfondo
        setProperty("bg_zoom", std::clamp(z, 0.001f, 1000.0f));
    } else { // 0 = Superficie
        m_flatZoom = std::clamp(z, 0.001f, 1000.0f);
        commitFlatTransformToActivePart();
    }
    update();
}

float GLWidget::getFlatRotation() const {
    if (m_flatViewTarget == 1) {
        return property("bg_rot").isValid() ? property("bg_rot").toFloat() : 0.0f;
    }
    return m_flatRotation;
}

void GLWidget::setFlatRotation(float angle) {
    if (m_flatViewTarget == 1) {
        setProperty("bg_rot", angle);
    } else {
        m_flatRotation = angle;
        commitFlatTransformToActivePart();
    }
    update();
}

void GLWidget::addFlatRotation(float angle) {
    setFlatRotation(getFlatRotation() + angle);
}

void GLWidget::rotateFlat90() {
    if (m_flatViewTarget == 1) { // Sfondo
        float current = property("bg_rot").isValid() ? property("bg_rot").toFloat() : 0.0f;
        setProperty("bg_rot", current + 90.0f);
    } else { // Superficie
        m_flatRotation += 90.0f;
        commitFlatTransformToActivePart();
    }
    update();
}

QVector2D GLWidget::getFlatPan() const {
    if (m_flatViewTarget == 1) {
        return property("bg_pan").isValid() ? property("bg_pan").value<QVector2D>() : QVector2D(0.0f, 0.0f);
    }
    return m_flatPan;
}

void GLWidget::setFlatPan(float x, float y) {
    if (m_flatViewTarget == 1) {
        setProperty("bg_pan", QVector2D(x, y));
    } else {
        m_flatPan = QVector2D(x, y);
        commitFlatTransformToActivePart();
    }
    update();
}


// ==========================================================
// CAMERA 3D & 4D STATE
// ==========================================================

void GLWidget::set4DLighting(bool enable) {

    engine->set4DLighting(enable);
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setLightingMode4D(int mode) {

    if (mode == 0) {
        m_lightingMode4D = 1; // Radial
    } else if (mode == 1) {
        m_lightingMode4D = 2; // Observer
    } else {
        m_lightingMode4D = 3; // Slice
    }

    meshNeedsUpdate = true;
    update();
}

bool GLWidget::is4DActive() const {
    return engine && engine->is4DLightingEnabled();
}

void GLWidget::setRotation4D(float o, float p, float ps) {
    omega = o; phi = p; psi = ps;
    update();
}

void GLWidget::beginPathHandoff()
{
    m_handoffPos = m_cameraPos;
    if (m_isPathFollowing) {
        m_handoffTarget = m_pathTarget;
        m_handoffUp     = m_pathUp;
        m_handoffRoll   = m_pathRoll;
    } else {
        // Camera libera: ricostruiamo target/up con lo stesso modello del render()
        float radYaw = m_cameraYaw * M_PI / 180.0f;
        float radPitch = m_cameraPitch * M_PI / 180.0f;
        QVector3D front(std::sin(radYaw) * std::cos(radPitch),
                        std::sin(radPitch),
                        -std::cos(radYaw) * std::cos(radPitch));
        QMatrix4x4 rollMat;
        rollMat.rotate(m_cameraRoll, 0.0f, 0.0f, 1.0f);
        m_handoffTarget = m_cameraPos + front;
        m_handoffUp     = rollMat.map(QVector3D(0.0f, 1.0f, 0.0f));
        m_handoffRoll   = 0.0f;   // il roll libero e' gia' dentro l'up
    }
    m_handoffObserver = m_observerPos;
    m_handoffCam4D    = m_cameraPos4D;
    m_pathHandoffK = 0.0f;
    m_pathHandoffActive = true;
}

float GLWidget::advancePathHandoff()
{
    m_pathHandoffK += 0.025f;          // ~1.2 s a un tick ogni 30 ms
    if (m_pathHandoffK >= 1.0f) {
        m_pathHandoffK = 1.0f;
        m_pathHandoffActive = false;
    }
    float k = m_pathHandoffK;
    return k * k * (3.0f - 2.0f * k);  // smoothstep
}

void GLWidget::setCameraPosAndDirection3D(const QVector3D& pos, const QVector3D& targetPoint, float roll)
{
    QVector3D finalPos = pos;
    QVector3D finalTarget = targetPoint;
    // 1. RIPRISTINA L'UP VECTOR ORIGINALE
    QVector3D finalUp(0.0f, 0.0f, 1.0f);
    // 2. IL VERO FIX PER RHI: INVERTI IL ROLLIO
    float rollDeg = qRadiansToDegrees(roll);

    // Handoff dal path 4D: nei primi tick la camera scivola dalla vista
    // catturata al click a quella del path, invece di teletrasportarsi.
    if (m_pathHandoffActive) {
        float k = advancePathHandoff();
        finalPos    = m_handoffPos    * (1.0f - k) + finalPos    * k;
        finalTarget = m_handoffTarget * (1.0f - k) + finalTarget * k;
        finalUp     = m_handoffUp     * (1.0f - k) + finalUp     * k;
        if (finalUp.lengthSquared() > 1e-6f) finalUp.normalize();
        else finalUp = QVector3D(0.0f, 0.0f, 1.0f);
        rollDeg     = m_handoffRoll * (1.0f - k) + rollDeg * k;
    }

    m_cameraPos = finalPos;
    m_pathTarget = finalTarget;
    m_isPathFollowing = true;
    m_pathUp = finalUp;
    m_pathRoll = rollDeg;

    update();
}

void GLWidget::setCameraFrom4DVectors(const QVector4D &pos4D, const QVector4D &target4D, const QVector4D &up4D)
{
    // 1. GESTIONE OSSERVATORE
    QVector4D safeObserverPos = pos4D;
    safeObserverPos.setW(pos4D.w() + 5.0f);

    // (assegnati in fondo: durante l'handoff dal path 3D anche osservatore e
    // camera 4D vengono fusi, altrimenti la proiezione 4D->3D della superficie
    // scatterebbe pur con la camera in blend)

    // 2. PROIEZIONE DELLA POSIZIONE
    QVector3D pos3D = projectPoint4Dto3D(pos4D);
    QVector3D target3D = projectPoint4Dto3D(target4D);

    // 3. CALCOLO VETTORE UP "VISIVO"
    QVector4D upTip4D = pos4D + up4D;
    QVector3D upTip3D = projectPoint4Dto3D(upTip4D);

    // Il vettore UP 3D è la differenza tra punta e base
    QVector3D finalUp3D = (upTip3D - pos3D);

    // 4. GESTIONE SINGOLARITÀ
    if (finalUp3D.lengthSquared() < 0.0001f) {
        finalUp3D = m_pathUp;
    } else {
        finalUp3D.normalize();
    }

    // 5. ANTI-FLIP VISIVO
    if (m_isFirstPathRun) {
        m_lastValidUp = finalUp3D;
        m_isFirstPathRun = false;
    } else {
        if (QVector3D::dotProduct(finalUp3D, m_lastValidUp) < 0.0f) {
            finalUp3D = -finalUp3D;
        }
        m_lastValidUp = finalUp3D;
    }

    // 5b. HANDOFF dal path 3D: scivolata dalla vista catturata al click a
    // quella del path (l'anti-flip sopra resta sul vettore puro del path).
    QVector3D dispUp = finalUp3D;
    float rollDeg = 0.0f;
    QVector4D newObserver = safeObserverPos;
    QVector4D newCam4D = pos4D;
    if (m_pathHandoffActive) {
        float k = advancePathHandoff();
        pos3D    = m_handoffPos    * (1.0f - k) + pos3D    * k;
        target3D = m_handoffTarget * (1.0f - k) + target3D * k;
        dispUp   = m_handoffUp     * (1.0f - k) + dispUp   * k;
        if (dispUp.lengthSquared() > 1e-6f) dispUp.normalize();
        else dispUp = finalUp3D;
        rollDeg     = m_handoffRoll * (1.0f - k);   // il 4D lavora a roll 0
        newObserver = m_handoffObserver * (1.0f - k) + safeObserverPos * k;
        newCam4D    = m_handoffCam4D    * (1.0f - k) + pos4D           * k;
    }

    m_pathUp = dispUp;
    m_observerPos = newObserver;
    m_cameraPos4D = newCam4D;

    // 6. APPLICAZIONE
    m_cameraPos = pos3D;
    m_pathTarget = target3D;
    m_isPathFollowing = true;
    m_pathRoll = rollDeg;

    m_view.setToIdentity();
    m_view.lookAt(pos3D, target3D, dispUp);

    update();
}

void GLWidget::zoomCamera(float delta) {
    // Calcoliamo il vettore "Avanti" completo
    float radYaw = m_cameraYaw * M_PI / 180.0f;
    float radPitch = m_cameraPitch * M_PI / 180.0f;

    // Vettore direzione sguardo
    QVector3D front;
    front.setX(std::sin(radYaw) * std::cos(radPitch));
    front.setY(std::sin(radPitch));
    front.setZ(-std::cos(radYaw) * std::cos(radPitch));

    // Spostiamo la camera lungo questo vettore
    m_cameraPos += front * delta;

    update();
}

void GLWidget::addCameraRotation(float dYaw, float dPitch) {
    m_cameraYaw += dYaw;
    m_cameraPitch += dPitch;

    if (m_cameraPitch > 89.0f) m_cameraPitch = 89.0f;
    if (m_cameraPitch < -89.0f) m_cameraPitch = -89.0f;

  //  update();
}

void GLWidget::resetTransformations()
{
    m_isPathFollowing = false;

    precession = 0.0f;
    nutation = 0.0f;
    spin = 0.0f;

    // Manteniamo un offset sicuro per evitare lo Z-Fighting in 4D.
    // Deve combaciare col valore usato all'avvio e nel guard di onStartClicked
    // (0.0001): un 0.1 introdurrebbe una rotazione XW/YW/ZW reale che, proiettata
    // 4D->3D, deforma in modo asimmetrico le superfici con componente P costruite
    // dopo un giro nei tab (Parametric->Ray Marching->Parametric).
    omega = 0.0001f;
    phi = 0.0001f;
    psi = 0.0001f;

    // =======================================================
    // FIX 1: SALVAVITA ZOOM 3D (Risolve l'effetto "Gigante")
    // =======================================================
    float current3DZoom = m_cameraPos.length();
    // Se la telecamera era dentro la figura (distanza < 2.5), la tiriamo indietro a 4.0
    if (std::isnan(current3DZoom) || std::isinf(current3DZoom) || current3DZoom < 2.5f) {
        current3DZoom = 4.0f;
    }
    m_cameraPos = QVector3D(0.0f, 0.0f, current3DZoom);

    m_cameraYaw = 0.0f;
    m_cameraPitch = 0.0f;
    m_cameraRoll = 0.0f;

    // FOV al default: il reset della vista annulla anche lo "zoom" del FOV di
    // un path appena abbandonato (senza questo, dopo un path a FOV alto la
    // superficie resettata apparirebbe rimpicciolita). Se un path e' ANCORA in
    // corsa, il suo primo tick riapplica subito il proprio FOV (stesso schema
    // della posa): il default vale solo per la vista libera.
    m_cameraFov = 45.0f;

    // =======================================================
    // FIX 2: SALVAVITA PROIEZIONE 4D
    // =======================================================
    // Leggiamo la distanza dall'Osservatore fisso, NON dalla telecamera mobile!
    float current4DZoom = m_observerPos.w();
    if (std::isnan(current4DZoom) || std::isinf(current4DZoom) || std::abs(current4DZoom) < 1.5f) {
        current4DZoom = 4.0f;
    }

    m_observerPos = QVector4D(0.0f, 0.0f, 0.0f, current4DZoom);
    m_cameraPos4D = QVector4D(0.0f, 0.0f, 0.0f, current4DZoom);

    // Reset dei vettori di mira
    m_pathTarget = QVector3D(0.0f, 0.0f, 0.0f);
    m_pathUp = QVector3D(0.0f, 1.0f, 0.0f);
    m_lastValidUp = QVector3D(0.0f, 1.0f, 0.0f);
    m_isFirstPathRun = true;

    m_flatPan = QVector2D(0.0f, 0.0f);
    setProperty("bg_pan", QVector2D(0.0f, 0.0f));

    m_rotationQuat = QQuaternion();

    // Azzera le memorie di backup
    m_viewStates[0] = ViewState();
    m_viewStates[1] = ViewState();

    meshNeedsUpdate = true;
    update();
}

void GLWidget::virtualMove(MoveDir dir, float speed3D, float speed4D)
{
    // speed3D e speed4D arrivano dagli slider e vanno da 0.001 a 0.1.
    // Li moltiplichiamo per dei "moltiplicatori base" per avere una buona sensibilità
    float speed     = speed3D * 5.0f;   // Spostamento 3D standard (Avanti, Indietro...)
    float rollSpeed = speed3D * 10.0f;  // Rollio della telecamera 3D
    float rotSpeed  = speed4D * 2.0f;   // Rotazioni 4D (Omega, Phi, Psi)
    float obsSpeed  = speed4D * 5.0f;   // Spostamento 4D lineare (X+, P-, ecc.)

    bool updateNeeded = true;

    float radYaw = m_cameraYaw * M_PI / 180.0f;
    QVector3D front(sin(radYaw), 0, -cos(radYaw));
    QVector3D right(cos(radYaw), 0, sin(radYaw));

    switch (dir) {
    case MoveForward: m_cameraPos += front * speed; break;
    case MoveBack:    m_cameraPos -= front * speed; break;
    case MoveLeft:    m_cameraPos -= right * speed; break;
    case MoveRight:   m_cameraPos += right * speed; break;
    case MoveUp:      m_cameraPos.setY(m_cameraPos.y() + speed); break;
    case MoveDown:    m_cameraPos.setY(m_cameraPos.y() - speed); break;
    case RollLeft:    m_cameraRoll += rollSpeed; break;
    case RollRight:   m_cameraRoll -= rollSpeed; break;

    case ObsMoveXPos:
        m_cameraPos4D.setX(m_cameraPos4D.x() + obsSpeed);
        m_observerPos.setX(m_observerPos.x() + obsSpeed);
        break;
    case ObsMoveXNeg:
        m_cameraPos4D.setX(m_cameraPos4D.x() - obsSpeed);
        m_observerPos.setX(m_observerPos.x() - obsSpeed);
        break;

    case ObsMoveYPos:
        m_cameraPos4D.setY(m_cameraPos4D.y() + obsSpeed);
        m_observerPos.setY(m_observerPos.y() + obsSpeed);
        break;
    case ObsMoveYNeg:
        m_cameraPos4D.setY(m_cameraPos4D.y() - obsSpeed);
        m_observerPos.setY(m_observerPos.y() - obsSpeed);
        break;

    case ObsMoveZPos:
        m_cameraPos4D.setZ(m_cameraPos4D.z() + obsSpeed);
        m_observerPos.setZ(m_observerPos.z() + obsSpeed);
        break;
    case ObsMoveZNeg:
        m_cameraPos4D.setZ(m_cameraPos4D.z() - obsSpeed);
        m_observerPos.setZ(m_observerPos.z() - obsSpeed);
        break;

    case ObsMovePPos:
        m_cameraPos4D.setW(m_cameraPos4D.w() + obsSpeed);
        m_observerPos.setW(m_observerPos.w() + obsSpeed);
        break;
    case ObsMovePNeg:
        m_cameraPos4D.setW(m_cameraPos4D.w() - obsSpeed);
        m_observerPos.setW(m_observerPos.w() - obsSpeed);
        break;

    case RotOmegaPos:
        omega += rotSpeed;
        break;
    case RotOmegaNeg:
        omega -= rotSpeed;
        break;

    case RotPhiPos:
        phi += rotSpeed;
        break;
    case RotPhiNeg:
        phi -= rotSpeed;
        break;

    case RotPsiPos:
        psi += rotSpeed;
        break;
    case RotPsiNeg:
        psi -= rotSpeed;
        break;

    default:
        updateNeeded = false;
        break;
    }

    update();
}


// ==========================================================
// ANIMATION & MOTION CONTROL
// ==========================================================

void GLWidget::addObjectRotation(float dPrecession, float dNutation, float dSpin)
{
    // --- ROTAZIONE BASATA SUGLI ASSI DELLO SCHERMO (Trackball Style) ---

    // 1. Mouse Destra/Sinistra (dPrecession) -> Ruota attorno all'asse Y verticale
    QQuaternion yRot = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, dPrecession);

    // 2. Mouse Su/Giù (dNutation) -> Ruota attorno all'asse X orizzontale
    QQuaternion xRot = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, dNutation);

    // 3. Spin (dSpin) -> Ruota attorno all'asse Z (profondità/vista)
    QQuaternion zRot = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, dSpin);

    // 4. Applica le rotazioni: Ordine: Y * X * Z * VecchiaRotazione
    // Questo garantisce che la rotazione avvenga rispetto al punto di vista dell'osservatore
    m_rotationQuat = yRot * xRot * zRot * m_rotationQuat;

    // Normalizza per evitare errori di arrotondamento nel tempo
    m_rotationQuat.normalize();

    emit rotationChanged();
    update();
}

void GLWidget::pauseMotion() {
    if (rotationTimer) rotationTimer->stop();
}

void GLWidget::resumeMotion() {
    if (rotationTimer) rotationTimer->start();
}

void GLWidget::startAnimationTimer() {
    if (m_animTimer && !m_animTimer->isActive()) {
        m_animTimer->start();
        m_elapsedTimer.restart();
    }
}

void GLWidget::stopAnimationTimer() {
    // Se il timer sta girando, lo fermiamo
    if (m_animTimer && m_animTimer->isActive()) {
        m_manualTime = (float)m_elapsedTimer.elapsed() / 1000.0f;
        m_animTimer->stop();
    }
}

void GLWidget::stopAllTimers() {
    // Ferma la rotazione fisica
    if (rotationTimer->isActive()) rotationTimer->stop();

    // Ferma l'animazione shader (Shadertoy)
    if (m_animTimer->isActive()) m_animTimer->stop();
}

void GLWidget::resetTime() {
    m_manualTime = 0.00001f;
    m_surfaceTimer.restart();
    m_lastRealTime = 0.0f;
    m_timeGeom = 0.0f;
    m_timeTex = 0.0f;
    m_timeBg = 0.0f;
    update();
}

void GLWidget::setSurfaceAnimating(bool animating) {
    m_surfaceAnimating = animating;
    if (animating && m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();   // azzera la base del dt: niente salti
        m_animTimer->start();
    }
}

void GLWidget::setBackgroundTextureAnimating(bool animating)
{
    m_bgAnimating = animating;
    if (animating && m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();
        m_animTimer->start();
    }
}

void GLWidget::setSurfaceTextureAnimating(bool animating) {
    m_texAnimating = animating;
    if (animating && m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();   // azzera la base del dt: niente salti
        m_animTimer->start();        // <-- avvia anche se il flag era già true
    }
}

// Chiamata dal VideoRecorder PRIMA di attivare use_virtual_time e di toccare
// m_manualTime (setShaderTime): fotografa il tempo TOTALE attualmente mostrato
// da ogni modulo. Durante la registrazione i moduli col clock fermo restano
// inchiodati a questi valori (vedi render, ramo use_virtual_time).
void GLWidget::beginVirtualTimeFreeze() {
    m_vtFrozenGeom = m_manualTime + m_timeGeom;
    m_vtFrozenTex  = m_manualTime + m_timeTex;
    m_vtFrozenBg   = m_manualTime + m_timeBg;
    m_vtFreezeValid = true;
}

// Chiamata a fine registrazione (use_virtual_time torna false): ricompone i
// m_time* dei moduli fermi cosi' che (m_manualTime + m_time*) torni ESATTAMENTE
// al valore congelato — il modulo riprende dallo stesso identico frame di prima
// del REC, invece di saltare al tempo raggiunto dal recorder.
void GLWidget::endVirtualTimeFreeze() {
    if (!m_vtFreezeValid) return;
    if (!m_surfaceAnimating) m_timeGeom = m_vtFrozenGeom - m_manualTime;
    if (!m_texAnimating)     m_timeTex  = m_vtFrozenTex  - m_manualTime;
    if (!m_bgAnimating)      m_timeBg   = m_vtFrozenBg   - m_manualTime;
    m_vtFreezeValid = false;
}


// ==========================================================
// UTILITIES
// ==========================================================

void GLWidget::beginHiResCapture(int w, int h) {
    if (w <= 0 || h <= 0) return;
    m_hiResCapture = true;
    // Fissa il color buffer offscreen di QRhiWidget alla risoluzione di export:
    // d'ora in poi render() disegna NATIVAMENTE a w x h (renderTarget()->pixelSize()
    // diventa questa) e grabFramebuffer() restituisce pixel nitidi a quella misura.
    setFixedColorBufferSize(QSize(w, h));
    // La riconfigurazione del render target avviene al prossimo grab/render:
    // grabFramebuffer() in getFrameForVideo fa un render sincrono e raccoglie la
    // nuova dimensione. Niente processEvents (ignorato su iOS).
}

void GLWidget::endHiResCapture() {
    if (!m_hiResCapture) return;
    m_hiResCapture = false;
    // Torna a seguire la dimensione a schermo del widget (QSize() = dinamica).
    setFixedColorBufferSize(QSize());
    this->repaint();
}

QImage GLWidget::getFrameForVideo(int targetW, int targetH, bool useFbo) {
    // Mesh CUSTOM (flusso geodetico): i vertici correnti del motore sono la
    // griglia appena caricata da setCustomMesh (che alza meshNeedsUpdate per
    // il re-upload, fatto da render() durante il grab). computeMesh() qui la
    // SOVRASCRIVEREBBE con la valutazione parametrica delle equazioni — per
    // gli script metrici la display map, cioe' una lamina piatta nel video.
    // Il ricalcolo CPU pre-grab serve solo alla mesh parametrica.
    if (meshNeedsUpdate && !m_isCustomMesh) updateSurfaceData();

    // 1. Catturiamo il frame. grabFramebuffer() (QRhiWidget) esegue da sé un render
    //    SINCRONO e restituisce i PIXEL REALI del color buffer: non serve repaint()
    //    né processEvents (quest'ultimo con ExcludeUserInputEvents è IGNORATO su iOS
    //    e spammava il log, oltre a non forzare nulla). In modalità hi-res capture
    //    (FBO) il buffer è già fissato alla risoluzione di export -> nessun upscale.
    QImage img = this->grabFramebuffer();

    // Fallback anti-crash: se la cattura RHI fallisce, prova il vecchio grab().
    if (img.isNull()) {
        img = this->grab().toImage();
    }
    if (img.isNull()) {
        img = QImage(targetW > 0 ? targetW : 1920,
                     targetH > 0 ? targetH : 1080,
                     QImage::Format_RGBA8888);
        img.fill(Qt::black);
        return img;
    }

    // 3. In hi-res capture il buffer è GIÀ alla risoluzione di export: NON riscalare
    //    (eviterebbe la nitidezza appena guadagnata). Negli altri casi adatta al
    //    target con SmoothTransformation (di norma un downscale nitido).
    if (!m_hiResCapture && targetW > 0 && targetH > 0 &&
            (img.width() != targetW || img.height() != targetH)) {
        img = img.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    return img;
}

bool GLWidget::validateAndApplyParametricShader(const QString &customLogic)
{
    // Backup dei valori correnti per ripristino
    QString oldCustom = m_customFragmentCode;
    QString oldX = m_eqX, oldY = m_eqY, oldZ = m_eqZ, oldW = m_eqW;

    // 1. DRY RUN FRAGMENT
    QString fsSource = createFragmentShaderSource(customLogic);
    {
        QShaderBaker baker;
        baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });
        QShader shader = baker.bake();
        if (!shader.isValid()) {
            m_lastCompilationError = "FRAGMENT: " + baker.errorMessage();
            return false;
        }
    }

    // 2. DRY RUN VERTEX (NUOVO!) - testa le equazioni x/y/z/w correnti
    QString vsSource = createVertexShaderSource(m_eqX, m_eqY, m_eqZ, m_eqW);
    {
        QShaderBaker baker;
        baker.setSourceString(vsSource.toUtf8(), QShader::VertexStage);
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });
        QShader shader = baker.bake();
        if (!shader.isValid()) {
            m_lastCompilationError = "VERTEX: " + baker.errorMessage();
            return false;
        }
    }

    // 3. APPLICA
    m_customFragmentCode = customLogic;
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyImplicitShader(const QString &eqF, const QString &texCode, const QString &dispCode)
{
    // Salvataggio di emergenza dei vecchi parametri funzionanti
    QString oldEq = m_eqImplicitF;
    QString oldTex = m_textureCode;
    QString oldDisp = m_displacementCode;

    // Prepariamo i nuovi parametri per generare lo shader
    m_eqImplicitF = eqF;
    m_textureCode = texCode;
    m_displacementCode = dispCode;

    QString fsSource = createImplicitFragmentShader();

    // DRY RUN
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = baker.errorMessage();

        // RIPRISTINO: Se la compilazione fallisce, rimettiamo tutto a posto!
        m_eqImplicitF = oldEq;
        m_textureCode = oldTex;
        m_displacementCode = oldDisp;
        return false;
    }

    // Se compila, la pipeline prosegue sicura. Questo percorso setta m_eqImplicitF
    // direttamente (bypassa setImplicitEquation), quindi rileviamo qui il
    // condizionamento del campo per il fallback trasparenza (vedi m_implicitIllConditioned).
    detectImplicitConditioning(eqF);
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyTextureDisplacement(const QString &texCode, const QString &dispCode)
{
    // Salvataggio di emergenza
    QString oldTex = m_textureCode;
    QString oldDisp = m_displacementCode;

    // Prepariamo i nuovi parametri per generare lo shader. m_eqImplicitF
    // resta invariato — è già valido (per i record script-mode è settato da
    // validateAndApplyImplicitScript).
    m_textureCode = texCode;
    m_displacementCode = dispCode;

    QString fsSource = createImplicitFragmentShader();

    // DRY RUN
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = baker.errorMessage();
        // RIPRISTINO
        m_textureCode = oldTex;
        m_displacementCode = oldDisp;
        return false;
    }

    // Se compila, la pipeline prosegue
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyBackgroundShader(const QString &scriptCode)
{
    // Genera la stessa stringa fsSource che produrrebbe rebuildBackgroundShader
    QString fsSource = createBackgroundFragmentShader(false, scriptCode);

    // DRY RUN con un baker locale: a differenza di bakeShader (la funzione del
    // widget che genera multi-target SPIR-V/GLSL/HLSL/MSL), qui generiamo solo
    // SPIR-V. Questo rende isValid() affidabile: se SPIR-V fallisce, sappiamo
    // che lo shader è rotto e blocchiamo prima di toccare la pipeline.
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = baker.errorMessage();
        return false;
    }

    // OK: applichiamo davvero
    m_bgScriptCode = scriptCode;
    m_bgIsScript = true;
    m_useBackgroundTexture = true;
    rebuildBackgroundShader(false, scriptCode);
    update();
    return true;
}

bool GLWidget::validateAndApplyParametricScript(const QString &scriptCodeGLSL)
{
    // Salva lo stato attuale dello script per ripristinarlo se fallisce
    QString oldScript = engine->getScriptCodeGLSL();
    QString oldCutout = engine->getCutoutCodeGLSL();
    bool oldScriptMode = engine->isScriptModeActive();

    // Applica temporaneamente per testare
    engine->setScriptCodeGLSL(scriptCodeGLSL);
    engine->setScriptMode(true);

    // Genera il vertex shader con lo script applicato
    QString vsSource = createVertexShaderSource("0", "0", "0", "0");

    // DRY RUN: solo SPIR-V con baker locale (vedi pattern background)
    QShaderBaker baker;
    baker.setSourceString(vsSource.toUtf8(), QShader::VertexStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = "VERTEX (script): " + baker.errorMessage();
        // RIPRISTINO: rimettiamo lo stato di prima
        engine->setScriptCodeGLSL(oldScript);
        engine->setCutoutCodeGLSL(oldCutout);
        engine->setScriptMode(oldScriptMode);
        return false;
    }

    // DRY RUN FRAGMENT: valida anche l'eventuale sezione CUTOUT (già impostata
    // nell'engine da onRunScriptClicked prima di chiamare questa funzione).
    QString fsSource = createFragmentShaderSource(m_customFragmentCode);
    QShaderBaker fragBaker;
    fragBaker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    fragBaker.setGeneratedShaderVariants({QShader::StandardShader});
    fragBaker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader fragShader = fragBaker.bake();
    if (!fragShader.isValid()) {
        m_lastCompilationError = "FRAGMENT (cutout): " + fragBaker.errorMessage();
        engine->setScriptCodeGLSL(oldScript);
        engine->setCutoutCodeGLSL(oldCutout);
        engine->setScriptMode(oldScriptMode);
        return false;
    }

    // OK: lo script è valido. Lo lasciamo applicato e schedula il rebuild.
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyImplicitScript(const QString &scriptCodeGLSL)
{
    QString oldScript     = engine->getScriptCodeGLSL();
    bool    oldScriptMode = engine->isScriptModeActive();

    engine->setScriptCodeGLSL(scriptCodeGLSL);
    engine->setScriptMode(true);

    // createImplicitFragmentShader() inietta lo scriptCodeGLSL appena impostato
    QString fsSource = createImplicitFragmentShader();

    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = "FRAGMENT (script): " + baker.errorMessage();
        engine->setScriptCodeGLSL(oldScript);   // rollback
        engine->setScriptMode(oldScriptMode);
        return false;
    }

    // Le superfici da script usano GLSL grezzo (non un campo algebrico x,y,z che il
    // parser CPU possa valutare): la rilevazione "campo a prodotto" non si applica.
    // Azzeriamo il flag cosi' non resta stantio da un preset "Chain" caricato prima
    // (l'autore dello script e' responsabile della propria trasparenza).
    m_implicitIllConditioned = false;
    rebuildShader();
    return true;
}


// ==========================================================
// RISORSE QRHI IMPLICIT (RAY MARCHING)
// ==========================================================

void GLWidget::buildImplicitPipeline()
{
    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }

    // 1. Assicuriamoci che il VBO dello schermo intero esista (sfruttiamo quello del background!)
    if (!m_bgVbo) {
        initBackgroundShader();
    }

    m_pipelineImplicit = rhi()->newGraphicsPipeline();
    m_pipelineImplicit->setTopology(QRhiGraphicsPipeline::Triangles);

    // 2. Diciamo alla GPU come leggere i vertici del quad
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal) },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });
    m_pipelineImplicit->setVertexInputLayout(inputLayout);

    // 3. VERTEX SHADER (Adattato da raymarch.vert per QRhi)
    QString vsSource = "#version 450\n"
                       "layout(location=0) in vec3 position;\n"
                       "layout(location=1) in vec4 normal;\n"
                       "layout(location=2) in vec2 texCoord;\n"
                       "layout(location=0) out vec2 fragCoord;\n"
                       "void main() {\n"
                       "  // Passiamo le coordinate al Fragment Shader (da 0 a 1)\n"
                       "  fragCoord = texCoord;\n"
                       "  // Disegniamo il quadrato che copre tutto lo schermo\n"
                       "  gl_Position = vec4(position.xy, 0.999, 1.0);\n"
                       "}\n";

    // 4. FRAGMENT SHADER: Generiamo lo shader di Ray Marching con l'equazione corrente!
   QString fsSource = createImplicitFragmentShader();

    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    if (!vs.isValid() || !fs.isValid()) {
        m_lastCompilationError = "Implicit pipeline: invalid shader stage.";
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
        return;
    }

    m_pipelineImplicit->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineImplicit->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    m_pipelineImplicit->setSampleCount(renderTarget()->sampleCount());

    // Per il Ray Marching, la profondità si calcola via matematica, non via Z-Buffer nativo
    m_pipelineImplicit->setDepthTest(false);
    m_pipelineImplicit->setDepthWrite(false);
    m_pipelineImplicit->setCullMode(QRhiGraphicsPipeline::None);

    // --- TRASPARENZA ---
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_pipelineImplicit->setTargetBlends({ blend });

    // 5. Usiamo temporaneamente i bindings standard (che contengono il tuo UBO con la telecamera)
    m_pipelineImplicit->setShaderResourceBindings(m_bindings);

    if (!m_pipelineImplicit->create()) {
        m_lastCompilationError = "Implicit pipeline: create() failed.";
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
        return;
    }
}


// ==========================================================
// IMPLICIT EQUATIONS STATE
// ==========================================================

QString GLWidget::createImplicitFragmentShader()
{
    QString safeEqF = GlslTranslator::translateEquation(m_eqImplicitF);

    // Per garantire la compatibilità con script stile Shadertoy:
    safeEqF.replace(QRegularExpression("\\biTime\\b"), "t");

    // Fallback di sicurezza se la stringa è vuota
    if (safeEqF.trimmed().isEmpty()) safeEqF = "x*x + y*y + z*z - 1.0";

    QString templateSource = loadShaderSource(":/shaders/raymarch_template.txt");
    templateSource.remove(QRegularExpression("^\\s*#version\\s+450\\s*\n?"));

    // MAX_FACES del ramo trasparente (raymarch_template.txt): ogni faccia costa 3
    // marchNextLayer (loop di marcia completi) per pixel. Su Android (GPU Adreno) a 8
    // facce, con un campo SDF pesante e alpha<1, il frame supera il timeout di rendering
    // -> GPU fault -> l'app viene terminata (verificato su Galaxy Tab S9 con N-Tours
    // trasparente). Riduciamo a 3 SOLO su mobile: la trasparenza resta (entrata+uscita+
    // un secondo guscio per le concavita'), ma il costo cala ~60%. Desktop invariato (8).
    // RETE DI SICUREZZA TRASPARENZA — MOBILE (desktop INVARIATO).
    // Il ramo trasparente costa MAX_FACES x 3 chiamate a marchNextLayer per pixel, e
    // ogni marchNextLayer e' un loop fino a MAX_LAYER_STEPS con 4 map() a passo. Su
    // mobile (anche superfici fisse RM trasparenti) questo supera il timeout GPU su
    // pannelli ad alta risoluzione (es. Galaxy Tab S9 Adreno) -> fault -> app killed.
    // Riduciamo ENTRAMBI i moltiplicatori; il ramo OPACO (una sola marchField, cappata
    // a u_raySteps) resta a piena qualita'. Android e' il caso peggiore osservato
    // (schermo grande + watchdog severo) -> piu' aggressivo; iOS un gradino sopra.
    // Desktop usa i valori pieni (8 facce, 2000 passi).
#if defined(Q_OS_ANDROID)
    templateSource.replace("%MAX_FACES%", "4");          // Android: 4 facce (era 8)
    templateSource.replace("%MAX_LAYER_STEPS%", "600");  // Android: tetto passi/strato (era 2000)
    templateSource.replace("%BISECT_ITERS%", "24");      // Android: bisezione piena
#elif defined(Q_OS_IOS)
    templateSource.replace("%MAX_FACES%", "4");           // iOS: 4 facce (era 8)
    // iOS: tetto passi/strato a 2000 (TEST). Ridurlo a 1000 NON ha eliminato il
    // glitch residuo in casi estremi su iPhone (non iPad), quindi quel tetto non
    // era il collo di bottiglia: i raggi del caso estremo escono prima di 1000.
    // Riportato a 2000 per isolare la vera causa altrove (probabilmente MAX_FACES
    // o il costo del campo map() con texture 3D + animazione insieme).
    templateSource.replace("%MAX_LAYER_STEPS%", "2000");
    templateSource.replace("%BISECT_ITERS%", "24");       // iOS: bisezione invariata
#else
    templateSource.replace("%MAX_FACES%", "8");
    templateSource.replace("%MAX_LAYER_STEPS%", "2000");
    templateSource.replace("%BISECT_ITERS%", "24");
#endif

    // safePow (pow sicura per basi negative) vive in common.glsl, iniettato qui sotto.
    QString commonCode = loadShaderSource(":/shaders/common.glsl");

    // Libreria solver (solveKerrR, kerr*SDF, ...): serve agli script ray marching
    // che usano la direttiva Inner:= (es. ergosfera di Kerr). Le sue funzioni sono
    // self-contained (prendono M/a come parametri, non leggono ubuf), quindi
    // possono precedere la dichiarazione dell'UBO nel template senza problemi.
    QString implicitLib = loadShaderSource(":/shaders/implicit.glsl");

    QString finalSource = "#version 450\n\n" + commonCode + "\n"
                          + implicitLib + "\n" + templateSource;

    // Variabili iniettate, condivise tra superficie esterna e interna.
    QString injectedVars = "    float t = ubuf.u_time;\n"
                           "    float iTime = ubuf.u_time;\n"
                           "    float A = ubuf.u_mathParams.x;\n"
                           "    float B = ubuf.u_mathParams.y;\n"
                           "    float C = ubuf.u_mathParams.z;\n"
                           "    float s = ubuf.u_mathParams.w;\n"
                           "    float S = ubuf.u_mathParams.w;\n"
                           "    float D = ubuf.u_mathParams2.x;\n"
                           "    float E = ubuf.u_mathParams2.y;\n"
                           "    float F = ubuf.u_mathParams2.z;\n"
                           "    float x = p.x; float y = p.y; float z = p.z;\n";

    // Default: nessuna seconda superficie. Lo stub ritorna 1e9 (mai colpito) e
    // il flag resta false, così il main() segue il cammino storico opaco.
    m_raymarchHasInner = false;
    QString innerExpr = "1e9";

    // --- INIZIO NUOVA LOGICA SCRIPT MULTI-RIGA ---
    if (engine->isScriptModeActive()) {
        QString customCode = engine->getScriptCodeGLSL();

        // --- DIRETTIVA //INNER: (seconda superficie opaca interna) ---
        // Espressione SDF del campo interno; il resto dello script è la
        // superficie esterna. È una DIRETTIVA-COMMENTO (inizia con //): cruciale
        // perché il filtro direttive di mainwindow SCARTA ogni riga con ":=",
        // quindi una "Inner:=" sparirebbe prima di arrivare qui. Con "//INNER:"
        // (niente :=) la riga sopravvive intatta nel glslCode e la estraiamo noi.
        // ANCORATA A INIZIO RIGA (^ + MultilineOption).
        QRegularExpression innerRe("^[ \\t]*//[ \\t]*INNER[ \\t]*:[ \\t]*([^\\n;]+);?",
                                   QRegularExpression::CaseInsensitiveOption
                                   | QRegularExpression::MultilineOption);
        QRegularExpressionMatch innerMatch = innerRe.match(customCode);
        if (innerMatch.hasMatch()) {
            innerExpr = innerMatch.captured(1).trimmed();
            customCode.remove(innerMatch.capturedStart(), innerMatch.capturedLength());
            m_raymarchHasInner = true;
        }

        // Creiamo una funzione indipendente con il codice custom (esterna)
        QString newFunction = "float getCustomImplicit(vec3 p) {\n" + injectedVars + customCode + "\n}\n\n";

        // 1. Dichiariamo la nostra funzione custom ESATTAMENTE prima della funzione map standard
        finalSource.replace("float map(vec3 p) {", newFunction + "float map(vec3 p) {");

        // 2. Sostituiamo il placeholder in map() delegando il calcolo alla nostra funzione
        finalSource.replace("%IMPLICIT_EQ%", "getCustomImplicit(p)");

    } else {
        // Modalità classica: UI LineEdit (Singola Riga)
        finalSource.replace("%IMPLICIT_EQ%", safeEqF);
    }
    // --- FINE NUOVA LOGICA ---

    // Iniettiamo l'espressione del campo interno (o lo stub 1e9). Le variabili
    // x,y,z,A,B,... sono già dichiarate nel corpo di mapInner() nel template.
    finalSource.replace("%INNER_MAP%", innerExpr);

    if (m_textureEnabled) {
        QString texCodeRemapped = m_textureCode;
        texCodeRemapped.replace("ubuf.u_time", "ubuf.u_dummyZero.x");
        QString texWrap = "{\n  float t = ubuf.u_dummyZero.x;\n  float iTime = ubuf.u_dummyZero.x;\n"
                          + texCodeRemapped + "\n}\n";
        finalSource.replace("%TEXTURE_CODE%", texWrap);

        // Il displacement appartiene al MODULO TEXTURE: il suo 't' deve leggere
        // l'orologio texture (dummyZero.x), non quello geometria (u_time del map()).
        // Lo avvolgiamo in uno scope che fa SHADOW del 't' esterno, come il colore
        // texture qui sopra. Senza questo, un 't' nudo nel displacement leggeva il
        // 'float t = ubuf.u_time' del template (orologio geometria), creando
        // l'interazione incrociata col dock Equations. Il blocco { } ridichiara
        // solo 't'/'iTime' (shadow): d_surf, pModel, p restano quelli esterni e
        // le assegnazioni del displacement (d_surf -= ...) li modificano davvero.
        QString dispRemapped = m_displacementCode;
        dispRemapped.replace("ubuf.u_time", "ubuf.u_dummyZero.x");   // STESSO orologio della texture
        QString dispWrap = "{\n  float t = ubuf.u_dummyZero.x;\n  float iTime = ubuf.u_dummyZero.x;\n"
                           + dispRemapped + "\n}\n";
        finalSource.replace("%DISPLACEMENT_CODE%", dispWrap);
    } else {
        finalSource.replace("%TEXTURE_CODE%", "");
        finalSource.replace("%DISPLACEMENT_CODE%", "");
    }

    return finalSource;
}

QString GLWidget::createBackgroundFragmentShader(bool isTextureMode, const QString &customCode)
{
    QString fsSource;
    QString header = "#version 450\n";

    QString uboBlock = "layout(std140, binding=0) uniform SceneUBO {\n"
                       "    mat4 u_mvpMatrix; mat4 u_mvMatrix; mat4 u_mMatrix;\n"
                       "    vec4 u_dummyZero; vec4 u_observerPos; vec4 u_cameraPos4D; vec4 u_mathParams; vec4 u_mathParams2;\n"
                       "    vec3 color; float alpha;\n"
                       "    vec3 u_col1; float u_lightIntensity;\n"
                       "    vec3 u_col2; float u_zoom;\n"
                       "    vec2 u_center; float u_rotation; float u_omega;\n"
                       "    float u_phi; float u_psi; float u_time; int u_projMode;\n"
                       "    int u_lightingMode; int u_renderMode; int u_isFlat; int useTexture;\n"
                       "    int useSpecular; float u_min; float u_max; float v_min;\n"
                       "    float v_max; int u_hasExplicitW; float x_min; float x_max;\n"
                       "    float y_min; float y_max; float z_min; float z_max;\n"
                       "} ubuf;\n";

    if (!customCode.isEmpty() && customCode.contains("#version")) {
        fsSource = customCode;
    }
    else if (isTextureMode) {
        fsSource = header +
                   "layout(binding=1) uniform sampler2D tex;\n"
                   + uboBlock +
                   "layout(location=0) in vec2 v_texCoord;\n"
                   "layout(location=0) out vec4 fragColor;\n"
                   "void main() {\n"
                   "  float rad = radians(ubuf.u_rotation);\n"
                   "  float c = cos(rad); float s = sin(rad);\n"
                   "  vec2 centered = v_texCoord - 0.5;\n"
                   "  vec2 rot = vec2(centered.x * c - centered.y * s, centered.x * s + centered.y * c) + 0.5;\n"
                   "  float scale = 1.0 / ubuf.u_zoom;\n"
                   "  vec2 shift = ubuf.u_center * 0.5;\n"
                   "  vec2 uv = (rot - 0.5) * scale + 0.5 + shift;\n"
                   "  fragColor = texture(tex, uv);\n"
                   "}\n";
    } else {
        QString commonCode = loadShaderSource(":/shaders/common.glsl");

        QString safeCode = customCode;
        safeCode.remove(QRegularExpression("#ifdef GL_ES[\\s\\S]*?#endif"));
        safeCode.remove(QRegularExpression("precision\\s+(highp|mediump|lowp)\\s+float\\s*;"));
        safeCode.remove(QRegularExpression("//SOUND_BEGIN.*?//SOUND_END", QRegularExpression::DotMatchesEverythingOption));

        // FIX: Rinominiamo gl_FragColor per non collidere con i parametri di Shadertoy!
        safeCode.replace("gl_FragColor", "out_FragColor");

        QString helpers = "    float _rad = radians(ubuf.u_rotation);\n"
                          "    float _c = cos(_rad); float _s = sin(_rad);\n"
                          "    vec2 _centered = in_uv - 0.5;\n"
                          "    vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;\n"
                          "    float _scale = 1.0 / ubuf.u_zoom;\n"
                          "    vec2 _shift = ubuf.u_center * 0.5;\n"
                          "    vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;\n"
                          "    bool u_isFlat = (ubuf.u_isFlat != 0);\n";

        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+t\\b"))) helpers += "    float t = ubuf.u_time;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+A\\b"))) helpers += "    float A = ubuf.u_mathParams.x;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+B\\b"))) helpers += "    float B = ubuf.u_mathParams.y;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+C\\b"))) helpers += "    float C = ubuf.u_mathParams.z;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+S\\b"))) helpers += "    float S = ubuf.u_mathParams.w;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+s\\b"))) helpers += "    float s = ubuf.u_mathParams.w;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+D\\b"))) helpers += "    float D = ubuf.u_mathParams2.x;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+E\\b"))) helpers += "    float E = ubuf.u_mathParams2.y;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+F\\b"))) helpers += "    float F = ubuf.u_mathParams2.z;\n";
        if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) helpers += "    vec3 u_col1 = ubuf.u_col1;\n";
        if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) helpers += "    vec3 u_col2 = ubuf.u_col2;\n";

        QString dynamicBody;

        if (safeCode.contains("mainImage")) {
            // Molti shader Shadertoy ignorano il parametro 'fragCoord' di mainImage e
            // leggono direttamente gl_FragCoord (posizione del pixel sullo schermo).
            // gl_FragCoord scavalca la nostra trasformazione (zoom/pan/rotazione di
            // u_center/u_zoom/u_rotation), così la texture di SFONDO non rispondeva ai
            // comandi (a differenza della stessa texture su superficie, dove il fix
            // c'e' gia' in createFragmentShaderSource). Lo rimappiamo su una globale
            // _st_fragCoord impostata al fragCoord TRASFORMATO prima di mainImage.
            safeCode.replace(QRegularExpression("\\bgl_FragCoord\\b"), "_st_fragCoord");

            // FIX: Vere variabili globali invece di macro!
            QString stHelpers = "vec3 iResolution;\n"
                                "float iTime;\n"
                                "float iTimeDelta;\n"
                                "int iFrame;\n"
                                "vec4 iMouse;\n"
                                "vec4 iDate;\n"
                                "vec4 _st_fragCoord;\n"
                                "#define iChannel0 tex\n"
                                "#define iChannel1 tex\n"
                                "#define iChannel2 tex\n"
                                "#define iChannel3 tex\n";

            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) stHelpers += "vec3 u_col1;\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) stHelpers += "vec3 u_col2;\n";

            // Le inizializziamo dinamicamente all'interno della funzione
            QString initVars = "    iResolution = vec3(1024.0, 1024.0, 1.0);\n"
                               "    iTime = ubuf.u_time;\n"
                               "    iTimeDelta = 0.016;\n"
                               "    iFrame = int(ubuf.u_time * 60.0);\n"
                               "    iMouse = vec4(0.0);\n"
                               "    iDate = vec4(0.0);\n";

            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) initVars += "    u_col1 = ubuf.u_col1;\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) initVars += "    u_col2 = ubuf.u_col2;\n";

            // Le helpers locali ridichiarano u_col1/u_col2: rimuovile per non fare
            // shadowing delle globali assegnate in initVars (vedi stesso accorgimento
            // in createFragmentShaderSource), altrimenti gli slider colore non agiscono.
            QString stLocalHelpers = helpers;
            stLocalHelpers.remove("    vec3 u_col1 = ubuf.u_col1;\n");
            stLocalHelpers.remove("    vec3 u_col2 = ubuf.u_col2;\n");

            dynamicBody = stHelpers + safeCode + "\n"
                                                 "vec3 getCustomColor(vec2 in_uv) {\n"
                          + stLocalHelpers + initVars +
                          "    vec2 _st_coord = uv * iResolution.xy;\n"
                          "    _st_fragCoord = vec4(_st_coord, 0.0, 1.0);\n"
                          "    vec4 fragColor_out;\n"
                          "    mainImage(fragColor_out, _st_coord);\n"
                          "    return fragColor_out.rgb;\n"
                          "}\n"
                          "void main() {\n"
                          "  out_FragColor = vec4(getCustomColor(v_texCoord), ubuf.alpha);\n"
                          "}\n";
        }
        else if (safeCode.contains("void main()")) {
            QString extHelpers = "#define iResolution vec3(1.0, 1.0, 1.0)\n#define iTime ubuf.u_time\n";
            extHelpers += "#define A ubuf.u_mathParams.x\n#define B ubuf.u_mathParams.y\n#define C ubuf.u_mathParams.z\n#define S ubuf.u_mathParams.w\n";
            extHelpers += "#define D ubuf.u_mathParams2.x\n#define E ubuf.u_mathParams2.y\n#define F ubuf.u_mathParams2.z\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";
            dynamicBody = extHelpers + safeCode + "\n";
        }
        else if (safeCode.contains("getCustomColor")) {
            // Lo snippet definisce GIA' la propria getCustomColor(vec2 in_uv) — e' il
            // formato dei preset texture parametrica/superficie. Senza questo ramo
            // ricadeva nel fallback che lo ri-avvolgeva in un'altra getCustomColor,
            // producendo una funzione annidata (illegale in GLSL) -> "unexpected
            // LEFT_BRACE". Lo iniettiamo cosi' com'e' (come fa createFragmentShaderSource),
            // mappando iTime/u_col1/u_col2, e gli forniamo noi la void main() perche'
            // nel background non preesiste.
            QString extHelpers = "#define iResolution vec3(1.0, 1.0, 1.0)\n#define iTime ubuf.u_time\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";
            dynamicBody = extHelpers + safeCode + "\n"
                          "void main() {\n"
                          "  out_FragColor = vec4(getCustomColor(v_texCoord), ubuf.alpha);\n"
                          "}\n";
        }
        else {
            if (!safeCode.contains("return")) {
                safeCode += "\n    return vec3(u, v, 0.2);\n";
            }
            dynamicBody = "vec3 getCustomColor(vec2 in_uv) {\n"
                          + helpers +
                          "    float u = uv.x;\n"
                          "    float v = uv.y;\n"
                          + safeCode + "\n"
                                       "}\n"
                                       "void main() {\n"
                                       "  out_FragColor = vec4(getCustomColor(v_texCoord), ubuf.alpha);\n"
                                       "}\n";
        }

        // OUTPUT GLOBALE RINOMINATO A "out_FragColor"
        fsSource = "#version 450\n"
                   "layout(location=0) in vec2 v_texCoord;\n"
                   "layout(location=0) out vec4 out_FragColor;\n"
                   "layout(binding=1) uniform sampler2D tex;\n"
                   + uboBlock + "\n" + commonCode + "\n" + dynamicBody;
    }

    return fsSource;
}


// ==========================================================
// PRIVATE HELPER METHODS
// ==========================================================

// --- Geometry & Mesh Builders ---

void GLWidget::buildWireframeGeometry() {
    // Zero indici è legittimo quando l'engine non ha ancora vertici (reset/init/
    // flat-view): la mesh arriva subito dopo e il wireframe si rigenera. Nessun
    // warning: era una diagnostica residua di un vecchio bug, ormai solo rumore.
    m_wireframeIndices = GeometryBuilder::buildWireframe(engine.get(), wfStepU, wfStepV,
                                                         &m_wireframeRanges,
                                                         m_meshAppearanceUniform);
    m_wireframeIndexCount = m_wireframeIndices.size();
    wireframeNeedsUpdate = true;
}

 // --- Shader Generation & Compilation ---

QString GLWidget::createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq)
{
    // 1. Carica il template
    QString vertexTemplate = loadShaderSource(":/shaders/surface.vert");
    QString commonCode = loadShaderSource(":/shaders/common.glsl");
    // Libreria di solver per variabili implicite (Kerr, Kruskal, ...): la stessa
    // iniettata nel flusso geodetico. Resa disponibile anche alle superfici
    // PARAMETRICHE così i campi x/y/z possono chiamare kerrRadius/kerrEmbedZ ecc.
    // (es. imbuto equatoriale di Kerr come superficie di rivoluzione).
    QString implicitCode = loadShaderSource(":/shaders/implicit.glsl");

    // 2. RIMUOVE QUALSIASI INTESTAZIONE ESISTENTE
    QRegularExpression headerCleanup("^\\s*(#version|precision).*\n", QRegularExpression::MultilineOption);
    vertexTemplate.remove(headerCleanup);
    vertexTemplate.remove(QRegularExpression("^\\s*#ifdef GL_ES[\\s\\S]*?#endif", QRegularExpression::MultilineOption));

    QString header = "#version 450\n";

    // safePow (pow sicura per basi negative) è definita in common.glsl, iniettato qui
    // sotto prima del template: il traduttore riscrive pow(->safePow( nelle equazioni.
    QString source = header + "\n" + commonCode + "\n" + implicitCode + "\n" + vertexTemplate;

    auto sanitizeEq = [](const QString &s) {
        // 1. Traduce potenze e costanti (pi, e)
        QString translated = GlslTranslator::translateEquation(s);

        // 2. Wrap di sicurezza finale (Niente più "replace" manuali del tempo!)
        if (translated.trimmed().isEmpty()) return QString("0.0");
        return "float(" + translated + ")";
    };

    // --- SOSTITUZIONE LOGICA ---
    if (engine->isScriptModeActive()) {
        QString customCode = engine->getScriptCodeGLSL();
        if (customCode.trimmed().isEmpty()) customCode = "return vec4(0.0, 0.0, 0.0, 0.0);";

        // Pulizia interi anche nello script custom
        QRegularExpression regInt("(?<=[^a-zA-Z0-9_.]|^)([0-9]+)(?=[^a-zA-Z0-9_.]|$)");
        customCode.replace(regInt, "\\1.0");

        QRegularExpression regEx("vec4 getRawPosition\\(float u, float v, float w\\)\\s*\\{[\\s\\S]*?\\}");

        // INIEZIONE VARIABILI DI SUPPORTO
        QString injectedVars = generateGlslHelperVars(customCode);

        QString newFunction = "vec4 getRawPosition(float u, float v, float w) {\n" +
                              injectedVars +
                              customCode +
                              "\n}";

        source.replace(regEx, newFunction);

        // Pulisci placeholder non usati
        source.replace("%X_EQ%", "0.0");
        source.replace("%Y_EQ%", "0.0");
        source.replace("%Z_EQ%", "0.0");
        source.replace("%W_EQ%", "0.0");

    } else {
        // Parametric Mode: Applica sanitizzazione a ogni equazione
        source.replace("%X_EQ%", sanitizeEq(xEq));
        source.replace("%Y_EQ%", sanitizeEq(yEq));
        source.replace("%Z_EQ%", sanitizeEq(zEq));
        source.replace("%W_EQ%", sanitizeEq(wEq));
    }

    // --- GESTIONE VINCOLI ---
    SurfaceEngine::ConstraintMode mode = engine->getConstraintMode();
    QString explicitEq = engine->getActiveExplicitEquation();
    if (explicitEq.trimmed().isEmpty()) explicitEq = "0.0";

    // Applica sanitize anche al vincolo
    QString explicitEqSafe = sanitizeEq(explicitEq);

    // PARAMETRI SOLO MAIUSCOLI (a e b minuscoli sotto rimangono perché sono le coordinate U,V della funzione)
    QString params = "float A=ubuf.u_mathParams.x; "
                     "float B=ubuf.u_mathParams.y; "
                     "float C=ubuf.u_mathParams.z; "
                     "float S=ubuf.u_mathParams.w; float s=S; "
                     "float D=ubuf.u_mathParams2.x; "
                     "float E=ubuf.u_mathParams2.y; "
                     "float F=ubuf.u_mathParams2.z; "
                     "float t=ubuf.u_time; ";

    QString explicitBody;
    if (mode == SurfaceEngine::ConstraintU) {
        explicitBody = params + "float v=a; float w=b; return (" + explicitEqSafe + ");";
    } else if (mode == SurfaceEngine::ConstraintV) {
        explicitBody = params + "float u=a; float w=b; return (" + explicitEqSafe + ");";
    } else {
        explicitBody = params + "float u=a; float v=b; return (" + explicitEqSafe + ");";
    }

    source.replace("%EXPLICIT_BODY%", explicitBody);
    source.replace("%CONSTRAINT_MODE%", QString::number((int)mode));

    if (m_isCustomMesh) {
        source.replace("#version 450", "#version 450\n#define CUSTOM_MESH\n");
    }

    return source;
}

// Corpo di UNA funzione texture procedurale, col nome che le viene dato.
// Estratta da createFragmentShaderSource per poterla generare piu' volte nello
// stesso shader (una per mesh con texture propria, vedi il dispatcher li'):
// duplicare i tre rami di iniezione (Shadertoy / getCustomColor esterno / corpo
// semplice) avrebbe creato due copie destinate a divergere, che in questo
// progetto e' la famiglia di bug piu' ricorrente.
// funcName e' il nome da generare; con "getCustomColor" il risultato e'
// byte-identico a quello che la funzione produceva prima di questa estrazione.
QString GLWidget::buildTextureFunction(const QString &customLogic, const QString &funcName)
{
    QString safeLogic = customLogic;
    safeLogic.remove(QRegularExpression("//SOUND_BEGIN.*?//SOUND_END", QRegularExpression::DotMatchesEverythingOption));
    safeLogic.remove(QRegularExpression("#ifdef GL_ES[\\s\\S]*?#endif"));
    safeLogic.remove(QRegularExpression("precision\\s+(highp|mediump|lowp)\\s+float\\s*;"));
    safeLogic.replace("gl_FragColor", "fragColor");

    QString helpers = "    float _rad = radians(ubuf.u_rotation);\n"
                      "    float _c = cos(_rad); float _s = sin(_rad);\n"
                      "    vec2 _centered = in_uv - 0.5;\n"
                      "    vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;\n"
                      "    float _scale = 1.0 / ubuf.u_zoom;\n"
                      "    vec2 _shift = ubuf.u_center * 0.5;\n"
                      "    vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;\n"
                      "    bool u_isFlat = (ubuf.u_isFlat != 0);\n";

    helpers += generateGlslHelperVars(safeLogic);
    helpers.replace("ubuf.u_time", "ubuf.u_dummyZero.x");

    if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) helpers += "    vec3 u_col1 = ubuf.u_col1;\n";
    if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) helpers += "    vec3 u_col2 = ubuf.u_col2;\n";

    QString codeToInject;

    // NB: la dichiarazione del sampler NON sta qui. E' una uniform di modulo e
    // va emessa UNA volta sola: generandola per funzione, uno shader con piu'
    // texture per-mesh la ridichiarerebbe N volte ("redefinition"). La emette
    // createFragmentShaderSource prima di tutte le funzioni.
    if (customLogic.isEmpty()) {
        codeToInject = "vec3 " + funcName + "(vec2 in_uv) {\n" +
                       helpers +
                       "\n    return texture(tex, uv).rgb;\n}";
    }
    else if (safeLogic.contains("mainImage")) {
        // Supporto Shadertoy

        // Molti shader Shadertoy ignorano il parametro 'fragCoord' di mainImage e
        // leggono direttamente gl_FragCoord (la posizione del pixel sullo schermo).
        // gl_FragCoord scavalca la nostra trasformazione flat (zoom/pan/rotazione),
        // così la texture in vista 2D non rispondeva al mouse. La rimappiamo su una
        // variabile globale che impostiamo al fragCoord trasformato prima di mainImage.
        safeLogic.replace(QRegularExpression("\\bgl_FragCoord\\b"), "_st_fragCoord");

        QString stHelpers = "vec3 iResolution;\n"
                            "float iTime;\n"
                            "float iTimeDelta;\n"
                            "int iFrame;\n"
                            "vec4 iMouse;\n"
                            "vec4 iDate;\n"
                            "vec4 _st_fragCoord;\n"
                            "#define iChannel0 tex\n"
                            "#define iChannel1 tex\n"
                            "#define iChannel2 tex\n"
                            "#define iChannel3 tex\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) stHelpers += "vec3 u_col1;\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) stHelpers += "vec3 u_col2;\n";

        QString initVars = "    iResolution = vec3(1024.0, 1024.0, 1.0);\n"
                           "    iTime = ubuf.u_dummyZero.x;\n"               // <- SOLO l'orologio della texture!
                           "    iTimeDelta = 0.016;\n"
                           "    iFrame = int(ubuf.u_dummyZero.x * 60.0);\n"  // <- Anche iFrame usa l'orologio texture
                           "    iMouse = vec4(0.0);\n"
                           "    iDate = vec4(0.0);\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) initVars += "    u_col1 = ubuf.u_col1;\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) initVars += "    u_col2 = ubuf.u_col2;\n";

        // Nel ramo Shadertoy u_col1/u_col2 sono GLOBALI (lette da mainImage) e
        // vengono assegnate in initVars. Le dichiarazioni LOCALI aggiunte da
        // `helpers` farebbero shadowing: mainImage leggerebbe le globali rimaste
        // a vec3(0) e gli slider colore non avrebbero effetto. Le rimuoviamo qui.
        QString stLocalHelpers = helpers;
        stLocalHelpers.remove("    vec3 u_col1 = ubuf.u_col1;\n");
        stLocalHelpers.remove("    vec3 u_col2 = ubuf.u_col2;\n");

        QString stBody = stHelpers + safeLogic + "\n"
                         "vec3 " + funcName + "(vec2 in_uv) {\n"
                         + stLocalHelpers + initVars +
                         "    vec2 _st_coord = uv * iResolution.xy;\n"
                         "    _st_fragCoord = vec4(_st_coord, 0.0, 1.0);\n"
                         "    vec4 fragColor_out;\n"
                         "    mainImage(fragColor_out, _st_coord);\n"
                         "    return fragColor_out.rgb;\n"
                         "}\n";

        // NOMI DI MODULO RESI UNICI PER FUNZIONE.
        // Questo ramo dichiara a livello GLOBALE iResolution, iTime, iFrame,
        // mainImage, _st_fragCoord...: con due mesh Shadertoy nello stesso
        // shader quei simboli collidono ("redefinition") e la compilazione
        // fallisce -> resta in piedi lo shader precedente, cioe' la texture
        // nuova non compare e la vecchia sembra "congelarsi".
        // Con funcName di default (texture globale) NON si rinomina nulla: il
        // sorgente resta byte-identico a prima.
        if (funcName != QLatin1String("getCustomColor")) {
            // funcName e' "getCustomColor_<k>": il suffisso e' gia' "_<k>".
            // NB: non anteporre un altro '_', o si otterrebbe "__<k>": i doppi
            // underscore sono RISERVATI in GLSL e il compilatore puo' rifiutarli.
            const QString sfx = QString(funcName).remove("getCustomColor");
            static const char *kSyms[] = {
                "iResolution", "iTime", "iTimeDelta", "iFrame", "iMouse",
                "iDate", "_st_fragCoord", "mainImage", "u_col1", "u_col2"
            };
            for (const char *s : kSyms) {
                // (?<!\.) esclude i CAMPI DELL'UBO: "ubuf.u_col1" e' un membro
                // del blocco uniform e rinominarlo dà "no such field in
                // structure 'ubuf'". Va rinominata solo la GLOBALE omonima che
                // questo ramo dichiara, non l'accesso qualificato che la
                // inizializza.
                stBody.replace(QRegularExpression(QString("(?<!\\.)\\b%1\\b").arg(s)),
                               QString(s) + sfx);
            }
            // Le #define iChannelN puntano al sampler `tex`, che e' unico e NON
            // va rinominato: il replace sopra le ha gia' lasciate intatte
            // (iChannel0..3 non sono nell'elenco), ma i loro nomi vanno resi
            // unici come gli altri, o la seconda definizione collide.
            for (int c = 0; c < 4; ++c) {
                const QString ch = QString("iChannel%1").arg(c);
                stBody.replace(QRegularExpression("\\b" + ch + "\\b"), ch + sfx);
            }
        }
        codeToInject = stBody;
    }
    else if (safeLogic.contains("getCustomColor")) {
        QString extHelpers;

        extHelpers += "#define iResolution vec3(1.0, 1.0, 1.0)\n"
                      "#define iTime ubuf.u_dummyZero.x\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";
        // Qui il nome della funzione lo scrive l'UTENTE dentro lo script
        // ("getCustomColor" letterale): per una texture per-mesh va rinominato,
        // o le N definizioni collidono.
        if (funcName != QLatin1String("getCustomColor"))
            safeLogic.replace(QRegularExpression("\\bgetCustomColor\\b"), funcName);
        codeToInject = extHelpers + safeLogic;
    }
    else {
        if (!safeLogic.contains("return")) {
            safeLogic += "\n    return vec3(u, v, 0.2); // Fallback\n";
        }
        codeToInject = "vec3 " + funcName + "(vec2 in_uv) {\n"
                       + helpers +
                       "    float u = uv.x;\n"
                       "    float v = uv.y;\n"
                       + safeLogic + "\n"
                                     "}\n";
    }

    return codeToInject;
}

QString GLWidget::createFragmentShaderSource(const QString &customLogic)
{
    QString fragmentTemplate = loadShaderSource(":/shaders/surface.frag");
    QString commonCode = loadShaderSource(":/shaders/common.glsl");

    QRegularExpression versionRegex("^\\s*#version\\s+[0-9]+(\\s+es|\\s+core)?\\s*\n?");
    fragmentTemplate.remove(versionRegex);

    QString header = "#version 450\n";

    // safePow (pow sicura per basi negative) è in common.glsl, iniettato qui sotto.
    QString fullSource = header + "\n" + commonCode + "\n" + fragmentTemplate;

    // DICHIARAZIONE FONDAMENTALE DELLA TEXTURE PER RHI. Emessa QUI, una volta
    // sola, prima di tutte le funzioni texture: e' una uniform di modulo e
    // ridichiararla per funzione sarebbe un errore di compilazione.
    QString codeToInject = "layout(binding=1) uniform sampler2D tex;\n";

    // Texture GLOBALE della superficie: la funzione che il frag chiama sempre,
    // identica a prima di questa modifica.
    //
    // ECCEZIONE, con texture per-mesh in gioco: se la superficie non ha MAI
    // avuto una texture globale, customLogic e' vuoto e il ramo di default di
    // buildTextureFunction genera `texture(tex, uv)`, cioe' campiona il
    // sampler. Con nessuna immagine caricata li' c'e' m_dummyTexture, che e'
    // 1x1 e non viene mai riempita -> NERO. Le mesh senza texture propria
    // cadono su questa funzione e uscivano nere.
    // In quel caso la neutralizziamo a bianco: bianco e' l'elemento neutro
    // della composizione (finalRGB = ubuf.color * texture), quindi la parte si
    // disegna col proprio colore, che e' esattamente "nessuna texture".
    // AMBITO "ALL": la superficie si comporta come UNA SOLA, quindi vale solo
    // la texture GLOBALE e le texture per-mesh sono SOSPESE (non cancellate:
    // restano nelle MeshPart e tornano passando a "Mesh", come gia' avviene per
    // colore/alpha/luce/wireframe). Percio' in All non si genera alcun
    // dispatcher e ogni parte usa getCustomColor.
    const bool perMeshTextures = !m_meshAppearanceUniform;

    QString globalLogic = customLogic;
    if (globalLogic.trimmed().isEmpty() && engine && perMeshTextures) {
        for (const MeshPart &mp : engine->getMeshParts()) {
            if (!mp.textureCode.trimmed().isEmpty()) {
                globalLogic = QStringLiteral("return vec3(1.0);");
                break;
            }
        }
    }
    codeToInject += buildTextureFunction(globalLogic, "getCustomColor");

    // ==========================================================
    // TEXTURE PER-MESH (procedurali)
    // ==========================================================
    // Ogni parte con codice proprio ottiene la sua getCustomColor_<k> nello
    // STESSO shader, e un dispatcher sceglie su u_meshIndex. Non servono ne'
    // binding ne' pipeline aggiuntive: le mesh gia' si distinguono per blocco
    // UBO (dynamic offset), e u_meshIndex e' costante su tutto il draw di una
    // parte, quindi il branch e' uniforme e non divergente.
    // Le parti SENZA codice proprio cadono sul default, cioe' la texture
    // globale: una superficie che non usa la feature genera esattamente lo
    // shader di prima, dispatcher incluso ma con un solo ramo.
    QString dispatch;
    if (engine && perMeshTextures) {
        const std::vector<MeshPart> &tparts = engine->getMeshParts();
        for (size_t k = 0; k < tparts.size(); ++k) {
            const QString code = tparts[k].textureCode.trimmed();
            if (code.isEmpty()) continue;
            const QString fn = QString("getCustomColor_%1").arg(k);
            codeToInject += "\n" + buildTextureFunction(tparts[k].textureCode, fn);
            // Confronto su float: u_meshIndex e' un indice piccolo esatto in
            // float32, ma restiamo sulla soglia 0.5 come il resto del progetto.
            dispatch += QString("    if (abs(ubuf.u_meshIndex - %1.0) < 0.5) return %2(in_uv);\n")
                            .arg(k).arg(fn);
        }
    }

    if (!dispatch.isEmpty()) {
        codeToInject += "\nvec3 getMeshColor(vec2 in_uv) {\n"
                        + dispatch +
                        "    return getCustomColor(in_uv);\n"
                        "}\n";
    }

    // Il frag chiama getMeshColor SOLO se esiste almeno una texture per-mesh;
    // altrimenti resta la chiamata diretta di sempre.
    const QString entryPoint = dispatch.isEmpty() ? QStringLiteral("getCustomColor")
                                                  : QStringLiteral("getMeshColor");

    fullSource.replace("%CUSTOM_CODE%", codeToInject);

    // PUNTO D'INGRESSO DELLA TEXTURE NEL TEMPLATE.
    // Il template chiama getCustomColor() direttamente (le vecchie
    // `texture(textureSampler, ...)` non esistono piu' nel .frag): quando ci
    // sono texture per-mesh dobbiamo dirottare quelle chiamate sul dispatcher.
    // Si sostituisce SOLO la chiamata dentro main(), non la definizione delle
    // funzioni appena generate, che vivono in codeToInject ed e' gia' stato
    // inserito: il replace mirato sulle due forme letterali presenti nel
    // template lascia intatte le definizioni.
    if (entryPoint != QLatin1String("getCustomColor")) {
        fullSource.replace("vec3 texColor = getCustomColor(v_texCoord);",
                           "vec3 texColor = " + entryPoint + "(v_texCoord);");
        fullSource.replace("vec4 tex = vec4(getCustomColor(v_texCoord), 1.0);",
                           "vec4 tex = vec4(" + entryPoint + "(v_texCoord), 1.0);");
    }

    // Taglio pareti interne (sezione //CUTOUT_BEGIN..//CUTOUT_END dello script
    // dock Script): vuoto = nessun taglio. HAS_CUTOUT è iniettato SOLO quando
    // serve davvero, così il branch discard in surface.frag (sotto #ifdef) non
    // esiste nemmeno per le superfici senza cutout: costo zero, non solo "sempre
    // false" (misurato: senza questo #ifdef erano comunque 4 istruzioni scalari
    // extra per pixel su OGNI superficie parametrica, anche senza cutout).
    QString cutoutBody = engine ? engine->getCutoutCodeGLSL() : QString();
    QString cutoutCode;
    bool hasCutout = !cutoutBody.trimmed().isEmpty();
    if (!hasCutout) {
        cutoutCode = "bool cutHere(float u, float v) { return false; }";
    } else {
        QString cutoutHelpers = generateGlslHelperVars(cutoutBody);
        cutoutCode = "bool cutHere(float u, float v) {\n" + cutoutHelpers + cutoutBody + "\n}";
    }
    fullSource.replace("%CUTOUT_CODE%", cutoutCode);

    if (hasCutout) {
        fullSource.replace("#version 450", "#version 450\n#define HAS_CUTOUT\n");
    }

    if (m_isCustomMesh) {
        fullSource.replace("#version 450", "#version 450\n#define CUSTOM_MESH\n");
    }

    return fullSource;
}

QString GLWidget::generateGlslHelperVars(const QString& sourceCode) {
    QString vars;
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+t\\b"))) vars += "    float t = ubuf.u_time;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+A\\b"))) vars += "    float A = ubuf.u_mathParams.x;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+B\\b"))) vars += "    float B = ubuf.u_mathParams.y;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+C\\b"))) vars += "    float C = ubuf.u_mathParams.z;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+S\\b"))) vars += "    float S = ubuf.u_mathParams.w;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+s\\b"))) vars += "    float s = ubuf.u_mathParams.w;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+D\\b"))) vars += "    float D = ubuf.u_mathParams2.x;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+E\\b"))) vars += "    float E = ubuf.u_mathParams2.y;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+F\\b"))) vars += "    float F = ubuf.u_mathParams2.z;\n";
    // MULTI-MESH: indice della parte in corso di disegno, per distinguere il ramo
    // (0 se la superficie e' a mesh singola). Nome esposto allo script: mesh.
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+mesh\\b"))) vars += "    float mesh = ubuf.u_meshIndex;\n";
    return vars;
}

QShader GLWidget::bakeShader(const QByteArray &source, QShader::Stage stage)
{
    QShaderBaker baker;
    baker.setSourceString(source, stage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});

    QList<QShaderBaker::GeneratedShader> targets;

    // Target base per Mobile e Desktop (Vulkan e OpenGL ES)
    targets.append({QShader::SpirvShader, QShaderVersion(100)});
    targets.append({QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs)});
    targets.append({QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs)});
    targets.append({QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs)});

    // Compila DirectX e OpenGL Desktop SOLO se NON siamo su smartphone
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    targets.append({QShader::GlslShader, QShaderVersion(410)});
    targets.append({QShader::GlslShader, QShaderVersion(460)});
    targets.append({QShader::HlslShader, QShaderVersion(50)});
#endif

    // Compila Metal SOLO su sistemi Apple (macOS/iOS)
#if defined(Q_OS_APPLE)
    targets.append({QShader::MslShader,  QShaderVersion(12)});
#endif

    baker.setGeneratedShaders(targets);

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        qWarning() << "SHADER ERROR (" << (stage == QShader::VertexStage ? "VERT" : "FRAG") << "):" << baker.errorMessage();
    }
    return shader;
}

// --- Pipeline & Resource Initialization ---

void GLWidget::buildPipeline() {
    // Pulisce preventivamente se esiste già
    if (m_pipelineOpaque) {
        delete m_pipelineOpaque;
        m_pipelineOpaque = nullptr;
    }

    // --- Layout, shader e blend (variabili locali, usate da tutte le pipeline) ---
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float4, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal)   },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });

    QString vsSource = createVertexShaderSource(m_eqX, m_eqY, m_eqZ, m_eqW);
    QString fsSource = createFragmentShaderSource(m_customFragmentCode);
    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    if (!vs.isValid() || !fs.isValid()) {
        qWarning() << "buildPipeline: shader non valido, pipeline non costruite.";
        auto clearPipe = [](QRhiGraphicsPipeline *&p){ if (p) { delete p; p = nullptr; } };
        clearPipe(m_pipelineOpaque);
        clearPipe(m_pipelineTranspBack);
        clearPipe(m_pipelineTranspFront);
        clearPipe(m_wireframePipeline);
        return;
    }

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    // MULTI-MESH: le pipeline della superficie devono essere costruite sul
    // layout con DYNAMIC OFFSET, perche' e' quello che ricevono al draw (un
    // blocco UBO per parte di mesh). Il layout dichiarato alla pipeline e quello
    // passato a setShaderResources devono corrispondere, altrimenti il
    // comportamento dipende dall'API grafica.
    // Nota: le pipeline dello sfondo (m_bgPipeline) e del ray marching restano
    // sul binding statico m_bindings, che non ha parti.
    ensureDynamicBindings(m_surfaceTexture ? m_surfaceTexture : m_dummyTexture);
    QRhiShaderResourceBindings *surfaceBindings = m_bindingsDyn ? m_bindingsDyn : m_bindings;

    // --- 1. PIPELINE OPACO (Klein perfetta, Bordi nascosti) ---
    m_pipelineOpaque = rhi()->newGraphicsPipeline();
    m_pipelineOpaque->setVertexInputLayout(inputLayout);
    m_pipelineOpaque->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineOpaque->setSampleCount(renderTarget()->sampleCount());
    m_pipelineOpaque->setDepthTest(true);
    m_pipelineOpaque->setDepthWrite(true); // SCRITTURA ATTIVA
    m_pipelineOpaque->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_pipelineOpaque->setCullMode(QRhiGraphicsPipeline::None); // DISEGNA TUTTO INSIEME
    m_pipelineOpaque->setTargetBlends({ blend });
    m_pipelineOpaque->setShaderResourceBindings(surfaceBindings);
    m_pipelineOpaque->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipelineOpaque->create()) {
        qWarning() << "buildPipeline: create() opaque fallita.";
        delete m_pipelineOpaque; m_pipelineOpaque = nullptr;
    }

    // --- 2. PIPELINE TRASPARENTE: FACCE POSTERIORI (Passata 1) ---
    m_pipelineTranspBack = rhi()->newGraphicsPipeline();
    m_pipelineTranspBack->setVertexInputLayout(inputLayout);
    m_pipelineTranspBack->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineTranspBack->setSampleCount(renderTarget()->sampleCount());
    m_pipelineTranspBack->setDepthTest(true);
    m_pipelineTranspBack->setDepthWrite(false); // SCRITTURA SPENTA
    m_pipelineTranspBack->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_pipelineTranspBack->setCullMode(QRhiGraphicsPipeline::Front); // NASCONDE IL FRONTE
    m_pipelineTranspBack->setTargetBlends({ blend });
    m_pipelineTranspBack->setShaderResourceBindings(surfaceBindings);
    m_pipelineTranspBack->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipelineTranspBack->create()) {
        qWarning() << "buildPipeline: create() transpBack fallita.";
        delete m_pipelineTranspBack; m_pipelineTranspBack = nullptr;
    }

    // --- 3. PIPELINE TRASPARENTE: FACCE ANTERIORI (Passata 2) ---
    m_pipelineTranspFront = rhi()->newGraphicsPipeline();
    m_pipelineTranspFront->setVertexInputLayout(inputLayout);
    m_pipelineTranspFront->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineTranspFront->setSampleCount(renderTarget()->sampleCount());
    m_pipelineTranspFront->setDepthTest(true);
    m_pipelineTranspFront->setDepthWrite(false); // SCRITTURA SPENTA
    m_pipelineTranspFront->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_pipelineTranspFront->setCullMode(QRhiGraphicsPipeline::Back); // NASCONDE IL RETRO
    m_pipelineTranspFront->setTargetBlends({ blend });
    m_pipelineTranspFront->setShaderResourceBindings(surfaceBindings);
    m_pipelineTranspFront->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipelineTranspFront->create()) {
        qWarning() << "buildPipeline: create() transpFront fallita.";
        delete m_pipelineTranspFront; m_pipelineTranspFront = nullptr;
    }

    // --- PIPELINE WIREFRAME ---
    if (m_wireframePipeline) {
        delete m_wireframePipeline;
        m_wireframePipeline = nullptr;
    }
    m_wireframePipeline = rhi()->newGraphicsPipeline();
    m_wireframePipeline->setTopology(QRhiGraphicsPipeline::Lines);

    // Assegna gli stessi identici layout e shader della mesh principale
    m_wireframePipeline->setVertexInputLayout(inputLayout);
    m_wireframePipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_wireframePipeline->setSampleCount(renderTarget()->sampleCount());
    m_wireframePipeline->setDepthTest(true);
    m_wireframePipeline->setDepthWrite(true);
    m_wireframePipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_wireframePipeline->setTargetBlends({ blend });
    m_wireframePipeline->setShaderResourceBindings(surfaceBindings);
    m_wireframePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_wireframePipeline->create()) {
        qWarning() << "buildPipeline: create() wireframe fallita.";
        delete m_wireframePipeline; m_wireframePipeline = nullptr;
    }
}

void GLWidget::initBackgroundShader() {
    if (m_bgVbo) return;

    m_bgVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 6 * sizeof(Vertex));
    m_bgVbo->create();

    if (!m_bgUbo) {
        m_bgUbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UboData));
        m_bgUbo->create();
    }

    m_bgBindings = rhi()->newShaderResourceBindings();
    m_bgBindings->setBindings({
        // CORREZIONE: Usa m_bgUbo invece di m_ubo!
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_bgUbo),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_dummyTexture, m_sampler)
    });
    m_bgBindings->create();
}

// --- Texture Utilities ---

void GLWidget::createDummyTexture() {
    if (!rhi()) return;

    // Crea un'immagine minuscola (1x1 pixel) per tappare il buco
    m_dummyTexture = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
    m_dummyTexture->create();

    // Crea le regole di lettura per l'immagine
    m_sampler = rhi()->newSampler(QRhiSampler::Linear,
                                  QRhiSampler::Linear,
                                  QRhiSampler::None,
                                  QRhiSampler::Repeat,
                                  QRhiSampler::Repeat);
    m_sampler->create();
}

// --- Math & Projections ---

QVector3D GLWidget::projectPoint4Dto3D(const QVector4D& p) {
    // 1. Ortogonale Pura
    if (projectionMode == 0) {
        return p.toVector3D();
    }
    // 2. Stereografica Pura (Polo fissato sull'asse W)
    else if (projectionMode == 2) {
        float r = p.length();
        if (r < 0.0001f) return QVector3D(0, 0, 0);

        // 1. Curviamo lo spazio normalizzando il punto sulla sfera 4D
        QVector4D pNorm = p / r;

        // 2. Proiettiamo dal "Polo Nord" (W = 1.0)
        float denom = 1.0f - pNorm.w();
        if (denom < 0.05f) denom = 0.05f; // Evita esplosioni matematiche al polo

        // 3. Ripristiniamo la scala (r) ammorbidita (* 0.5) per il 3D
        return pNorm.toVector3D() * (r / denom);
    }

    // 3. Prospettiva Centrale (Camera Pinhole 4D)
    float distW = m_observerPos.w() - p.w();
    if (std::abs(distW) < 0.01f) {
        return p.toVector3D();
    }

    float wFactor = m_observerPos.w() / distW;
    float x = m_observerPos.x() + (p.x() - m_observerPos.x()) * wFactor;
    float y = m_observerPos.y() + (p.y() - m_observerPos.y()) * wFactor;
    float z = m_observerPos.z() + (p.z() - m_observerPos.z()) * wFactor;

    return QVector3D(x, y, z);
}
