#include "glwidget.h"
#include "geometrybuilder.h"
#include "inputhandler.h"
#include "surfaceengine.h"
#include "glsltranslator.h"
#include "expressionparser.h"

#include <QTimer>
#include <cmath>
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
    showBorders = false;
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
    rotationTimer->setInterval(16);
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

void GLWidget::initialize(QRhiCommandBuffer *cb)
{
    if (m_ubo) {
        return;
    }

    // --- 1. CREAZIONE UBO (Uniform Buffer Object) ---
    // Alloca memoria dinamica per la tua struct UboData
    m_ubo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UboData));
    m_ubo->create();

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

    // VBO per i bordi (100.000 vertici sono sufficienti per i perimetri)
    m_borderVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 100000 * sizeof(Vertex));
    m_borderVbo->create();

    // UBO indipendente per poter colorare il bordo diversamente dalla mesh principale
    m_borderUbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UboData));
    m_borderUbo->create();

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

    m_borderBindings = rhi()->newShaderResourceBindings();
    m_borderBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
                                                 QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                 m_borderUbo),
        QRhiShaderResourceBinding::sampledTexture(1,
                                                  QRhiShaderResourceBinding::FragmentStage,
                                                  m_dummyTexture, m_sampler)
    });
    m_borderBindings->create();

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
            if (m_frameClock.isValid() && !justResumed) {
                float dtMs = (float)m_frameClock.nsecsElapsed() / 1.0e6f;
                // Un dtMs enorme (> ~2 s) durante l'animazione NON e' un frame lento:
                // e' un buco da inattivita' (timer in pausa, finestra nascosta). Lo
                // scartiamo del tutto invece di clamparlo-e-mediarlo nell'EMA.
                if (dtMs <= 2000.0f) {
                    // EMA: reattiva ma immune ai picchi isolati di un singolo frame.
                    m_avgFrameMs = 0.85f * m_avgFrameMs + 0.15f * dtMs;

                    // Soglia per piattaforma: su Android il watchdog del driver GPU
                    // puo' uccidere l'app prima dell'avviso, quindi avvisiamo PRIMA
                    // (2 fps); su desktop, dove non c'e' kill, scendiamo a ~1.5 fps.
#if defined(Q_OS_ANDROID)
                    constexpr float kSlowFrameMs = 500.0f;  // ~2 fps
#else
                    constexpr float kSlowFrameMs = 667.0f;  // ~1.5 fps
#endif
                    constexpr float kSlowDwellMs = 600.0f;  // sostenuto per >0.6 s

                    if (m_avgFrameMs > kSlowFrameMs) {
                        m_slowAccumMs += dtMs;
                        // Riarmo per PEGGIORAMENTO: compare la prima volta (level==0)
                        // e poi solo se la media raddoppia rispetto a quando e'
                        // apparso (+100%). Non tormenta ai cali lievi ma riavvisa se
                        // la situazione degrada. m_perfWarnLevelMs==0 = armato.
                        const bool firstTime = (m_perfWarnLevelMs <= 0.0f);
                        const bool worsened  = (m_avgFrameMs > m_perfWarnLevelMs * 2.0f);
                        if (m_slowAccumMs >= kSlowDwellMs && (firstTime || worsened)) {
                            m_perfWarnLevelMs = m_avgFrameMs;  // livello mostrato
                            emit performanceWarning();
                        }
                    } else {
                        m_slowAccumMs = 0.0f;
                        // Tornati sopra soglia (fluidi): riarmiamo, cosi' un successivo
                        // rallentamento (anche dopo un cambio di preset, senza passare
                        // dallo stop) fa ricomparire l'avviso.
                        m_perfWarnLevelMs = 0.0f;
                    }
                } // chiude: if (dtMs <= 2000.0f)
            }
            m_wasAnimating = true;
        } else {
            // Animazione ferma: azzeriamo gli accumulatori e riarmiamo.
            m_slowAccumMs = 0.0f;
            m_avgFrameMs = 16.0f;
            m_perfWarnLevelMs = 0.0f;
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
            float halfHeight = camDist * std::tan(45.0f * 0.5f * M_PI / 180.0f);
            float halfWidth = halfHeight * aspect;
            m_projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
        } else {
            m_projection.perspective(45.0f, aspect, nearPlane, farPlane);
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
    if (useVirtualTimeVar.isValid() && useVirtualTimeVar.toBool()) {
        // Se stiamo registrando, forziamo tutti gli orologi al tempo del recorder
        float vTime = property("virtual_time").toFloat();
        m_timeGeom = vTime;
        m_timeTex  = vTime;
        m_timeBg   = vTime;
    } else {
        // App in uso normale: ogni orologio avanza solo se la sua parte è "attiva"
        // Se corrono insieme, avanzano dello stesso identico 'dt', restando in sincrono!
        if (m_surfaceAnimating) m_timeGeom += dt;
        if (m_texAnimating)     m_timeTex  += dt;
        if (m_bgAnimating)      m_timeBg   += dt;
    }

    // 3. INVIO DEI DATI ALLA GPU
    m_uboData.time = m_manualTime + m_timeGeom;

    // Usiamo la coordinata X di dummyZero per inviare il tempo specifico della Texture
    m_uboData.dummyZero.setX(m_manualTime + m_timeTex);

    // .y = flag "seconda superficie interna" (Inner:= nello script ray marching).
    // .x resta l'orologio texture; .y/.z/.w erano liberi (azzerati a inizio frame).
    m_uboData.dummyZero.setY(m_raymarchHasInner ? 1.0f : 0.0f);
    m_uboData.projMode = projectionMode;
    m_uboData.renderMode = renderMode;
    m_uboData.lightingMode = is4DActive() ? m_lightingMode4D : 0;
    m_uboData.useSpecular = m_isSpecularEnabled ? 1 : 0;

    m_uboData.isFlat = m_isFlatView ? 1 : 0;
    m_uboData.zoom = m_flatZoom;
    m_uboData.center = m_flatPan;
    m_uboData.rotation = m_flatRotation;

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

    // Aggiornamento Buffer Principale
    resourceUpdates->updateDynamicBuffer(m_ubo, 0, sizeof(UboData), &m_uboData);

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
            bgUboData.time = m_manualTime + m_timeBg;
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

        if (borderNeedsUpdate && m_borderVertexCount > 0) {
            int vSize = m_borderVertexCount * sizeof(Vertex);
            if (m_borderVbo->size() < vSize) {
                m_borderVbo->destroy(); delete m_borderVbo;
                m_borderVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5);
                m_borderVbo->create();
            }
            resourceUpdates->updateDynamicBuffer(m_borderVbo, 0, vSize, m_borderVertices.data());
            borderNeedsUpdate = false;
        }

        if (showBorders && m_borderUbo) {
            UboData borderUboData = m_uboData;
            borderUboData.color = QVector3D(bordRed, bordGreen, bordBlue);
            borderUboData.useTexture = 0;
            borderUboData.useSpecular = 0;
            resourceUpdates->updateDynamicBuffer(m_borderUbo, 0, sizeof(UboData), &borderUboData);
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
                cb->setShaderResources(m_bindings);
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

            if (renderMode == 2) {
                // Wireframe
                if (m_wireframePipeline && m_wireframeIndexCount > 0) {
                    cb->setGraphicsPipeline(m_wireframePipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    cb->setShaderResources(m_bindings);
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbo, 0);
                    cb->setVertexInput(0, 1, &vbufBinding, m_wireframeIbo, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(m_wireframeIndexCount);
                }
            } else {
                // Solido
                if (m_indexCount > 0 && m_vbo && m_ibo) {
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbo, 0);
                    if (alpha < 0.99f) {
                        // Disegna solo se entrambe le pipeline di trasparenza sono valide.
                        // Dopo un errore di compilazione la pipeline pu\u00f2 essere nulla:
                        // passarla a Metal causa EXC_BAD_ACCESS.
                        if (m_pipelineTranspBack && m_pipelineTranspFront) {
                            cb->setGraphicsPipeline(m_pipelineTranspBack);
                            cb->setShaderResources(m_bindings);
                            cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                            cb->drawIndexed(m_indexCount);
                            cb->setGraphicsPipeline(m_pipelineTranspFront);
                            cb->setShaderResources(m_bindings);
                            cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                            cb->drawIndexed(m_indexCount);
                        }
                    } else {
                        if (m_pipelineOpaque) {
                            cb->setGraphicsPipeline(m_pipelineOpaque);
                            cb->setShaderResources(m_bindings);
                            cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                            cb->drawIndexed(m_indexCount);
                        }
                    }
                }
            }

            // Bordi 3D
            if (showBorders && m_borderPipeline && m_borderVertexCount > 0) {
                cb->setGraphicsPipeline(m_borderPipeline);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(m_borderBindings);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_borderVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(m_borderVertexCount);
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
    if (m_ubo) {
        delete m_ubo;
        m_ubo = nullptr;
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
    if (m_borderVbo) {
        delete m_borderVbo;
        m_borderVbo = nullptr;
    }
    if (m_borderUbo) {
        delete m_borderUbo;
        m_borderUbo = nullptr;
    }
    if (m_borderPipeline) {
        delete m_borderPipeline;
        m_borderPipeline = nullptr;
    }
    if (m_borderBindings) {
        delete m_borderBindings;
        m_borderBindings = nullptr;
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
    float speedMult3D = 2.0f;
    float speedMult4D = 0.05f;

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
// campo su una griglia CPU (exprtk) e misura il RANGE DINAMICO. Un prodotto di piu'
// fattori esplode a ~1e13, mentre ogni superficie a espressione singola resta molto
// sotto (<=1e8 anche a grado 6). Se max|f| sfonda la soglia -> il ramo trasparente
// aggancerebbe crossing FANTASMA (cambi di segno per parita' del prodotto, lontani da
// ogni superficie reale) e la superficie sparirebbe con alpha<1: settiamo
// m_implicitIllConditioned = true cosi' il render forza alpha=1.0 e la UI disabilita
// lo slider. Vedi m_implicitIllConditioned in glwidget.h per il razionale completo.
void GLWidget::detectImplicitConditioning(const QString &eqF)
{
    m_implicitIllConditioned = false;
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
    // una griglia grossa (12^3) li mancherebbe e sottostimerebbe max|f|. 40^3 = 64k
    // valutazioni exprtk: eseguito UNA volta per cambio-equazione, costo trascurabile.
    const int N = 40;
    double maxAbs = 0.0;
    for (int i = 0; i < N; ++i) {
        x = bxMin + (bxMax - bxMin) * (i + 0.5) / N;
        for (int j = 0; j < N; ++j) {
            y = byMin + (byMax - byMin) * (j + 0.5) / N;
            for (int k = 0; k < N; ++k) {
                z = bzMin + (bzMax - bzMin) * (k + 0.5) / N;
                double v = parser.value();
                if (!std::isfinite(v)) continue;
                double av = std::abs(v);
                if (av > maxAbs) maxAbs = av;
            }
        }
    }

    // Soglia empirica sul RANGE DINAMICO del campo grezzo. Verificato campionando su
    // griglia fitta [-10,10]^3: un prodotto di piu' fattori quadratici (es. "Chain",
    // 6 tori) esplode a ~1e13..1e14, mentre QUALSIASI superficie a espressione singola
    // di grado ragionevole resta molto sotto: sfera/toro ~1e2, gyroide ~1, quartica
    // ~1e5, genus-2 ~1e6, "cuore" grado 6 ~1e8. La soglia 1e10 lascia un margine di
    // ~2 ordini di grandezza sopra la peggiore superficie legittima testata e ~4 sotto
    // il "Chain": separa in modo netto i campi a prodotto senza falsi positivi.
    // (Niente gate su segni misti: i tubi sottili possono non essere "bucati" e darebbero
    //  falsi negativi proprio sul "Chain" — vedi cronologia analisi.)
    const double ILL_THRESHOLD = 1.0e10;
    m_implicitIllConditioned = (maxAbs > ILL_THRESHOLD);

    if (m_implicitIllConditioned) {
        qDebug() << "[implicit] campo mal condizionato (prodotto): max|f| =" << maxAbs
                 << "-> trasparenza disabilitata (fallback opaco)";
    }
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
    buildBorderGeometry();

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
    buildBorderGeometry();

    // 2. Diciamo al Render Pass che i dati sono pronti per essere spediti alla GPU
    meshNeedsUpdate = true;

    // 3. Forza il ridisegno
    update();
}

void GLWidget::resetVisuals()
{
    engine->clear();
    m_lightingMode4D = 0;

    m_borderVertexCount = 0;
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

        // Azzera i parametri della superficie
        m_flatZoom = 1.0f;
        m_flatPan = QVector2D(0.0f, 0.0f);
        m_flatRotation = 0.0f;

        update();
    }
}

void GLWidget::setProjectionMode(int mode) {
    projectionMode = mode;           // Serve ancora alla CPU per projectPoint4Dto3D
    m_uboData.projMode = mode;       // Sincronizza con la GPU
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setRenderMode(int mode) {
    this->renderMode = mode;
    update();
}

void GLWidget::setShowBorders(bool enable) {
    showBorders = enable;            // Serve alla CPU per sapere se generare la geometria
    update();
}

void GLWidget::setColor(float r, float g, float b) {
    this->red = r;
    this->green = g;
    this->blue = b;
    // Rimuovi l'assegnazione diretta a m_uboData qui, lo fa già il render()
    update();
}

void GLWidget::setBorderColor(float r, float g, float b) {
    bordRed = r; bordGreen = g; bordBlue = b;
    update();
}

void GLWidget::setAlpha(float a) {
    this->alpha = a;
    update();
}

void GLWidget::setSpecularEnabled(bool enabled) {
    m_isSpecularEnabled = enabled;
    update();
}

void GLWidget::setLightIntensity(float intensity) {
    this->m_lightIntensity = intensity;
    update();
}

void GLWidget::increaseWireframeUDensity() {
    if (wfStepV > STEP_MIN) wfStepV--;
    buildWireframeGeometry();
    update();
}

void GLWidget::decreaseWireframeUDensity() {
    if (wfStepV < STEP_MAX) wfStepV++;
    buildWireframeGeometry();
    update();
}

void GLWidget::increaseWireframeVDensity() {
    if (wfStepU > STEP_MIN) wfStepU--;
    buildWireframeGeometry();
    update();
}

void GLWidget::decreaseWireframeVDensity() {
    if (wfStepU < STEP_MAX) wfStepU++;
    buildWireframeGeometry();
    update();
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
    update();
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

void GLWidget::setFlatView(bool active) {
    m_isFlatView = active;

    if (!m_isFlatView) {
        buildBorderGeometry();
        buildWireframeGeometry();
    }

    meshNeedsUpdate = true;
    update();
}

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

void GLWidget::setCameraPosAndDirection3D(const QVector3D& pos, const QVector3D& targetPoint, float roll)
{
    m_cameraPos = pos;
    m_pathTarget = targetPoint;
    m_isPathFollowing = true;

    // 1. RIPRISTINA L'UP VECTOR ORIGINALE
    m_pathUp = QVector3D(0.0f, 0.0f, 1.0f);

    // 2. IL VERO FIX PER RHI: INVERTI IL ROLLIO
    m_pathRoll = qRadiansToDegrees(roll);

    update();
}

void GLWidget::setCameraFrom4DVectors(const QVector4D &pos4D, const QVector4D &target4D, const QVector4D &up4D)
{
    // 1. GESTIONE OSSERVATORE
    QVector4D safeObserverPos = pos4D;
    safeObserverPos.setW(pos4D.w() + 5.0f);

    m_observerPos = safeObserverPos;

    // Aggiorniamo l'engine per coerenza nei calcoli
    m_cameraPos4D = pos4D;

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

    m_pathUp = finalUp3D;

    // 6. APPLICAZIONE
    m_cameraPos = pos3D;
    m_pathTarget = target3D;
    m_isPathFollowing = true;
    m_pathRoll = 0.0f;

    m_view.setToIdentity();
    m_view.lookAt(pos3D, target3D, finalUp3D);

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
    if (meshNeedsUpdate) updateSurfaceData();

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

void GLWidget::buildBorderGeometry() {
    // 1. Generiamo i dati grezzi sulla CPU
    // CAMBIATO DA QVector3D a QVector4D
    std::vector<QVector4D> data = GeometryBuilder::buildBorders(engine.get());

    m_borderVertices.clear();
    m_borderVertices.reserve(data.size());

    // 2. Li convertiamo nel formato Vertex compatibile con l'input layout di RHI
    // CAMBIATO DA QVector3D a QVector4D
    for (const QVector4D& pos : data) {
        Vertex v;
        // Ora sia v.position (che abbiamo cambiato in surfaceengine.h)
        // sia 'pos' sono QVector4D, quindi l'assegnazione funzionerà perfettamente!
        v.position = pos;

        // Usiamo QVector4D per rispettare la struttura del Vertex!
        // (La normale non serve per le linee, ma va riempita per non inviare "spazzatura" alla VRAM)
        v.normal = QVector4D(0.0f, 0.0f, 1.0f, 0.0f);

        v.texCoord = QVector2D(0.0f, 0.0f);
        m_borderVertices.push_back(v);
    }

    m_borderVertexCount = (int)m_borderVertices.size();
    borderNeedsUpdate = true; // Segnaliamo al render loop che i dati sono pronti
}

void GLWidget::buildWireframeGeometry() {
    m_wireframeIndices = GeometryBuilder::buildWireframe(engine.get(), wfStepU, wfStepV);
    m_wireframeIndexCount = m_wireframeIndices.size();
    wireframeNeedsUpdate = true;

    if (m_wireframeIndexCount == 0) {
        qWarning() << "ATTENZIONE: Nessun indice wireframe generato! (I commenti in updateSurfaceData sono stati tolti?)";
    }
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

QString GLWidget::createFragmentShaderSource(const QString &customLogic)
{
    QString fragmentTemplate = loadShaderSource(":/shaders/surface.frag");
    QString commonCode = loadShaderSource(":/shaders/common.glsl");

    QRegularExpression versionRegex("^\\s*#version\\s+[0-9]+(\\s+es|\\s+core)?\\s*\n?");
    fragmentTemplate.remove(versionRegex);

    QString header = "#version 450\n";

    // safePow (pow sicura per basi negative) è in common.glsl, iniettato qui sotto.
    QString fullSource = header + "\n" + commonCode + "\n" + fragmentTemplate;

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

    // DICHIARAZIONE FONDAMENTALE DELLA TEXTURE PER RHI
    QString samplerDecl = "layout(binding=1) uniform sampler2D tex;\n";

    if (customLogic.isEmpty()) {
        codeToInject = samplerDecl +
                       "vec3 getCustomColor(vec2 in_uv) {\n" +
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

        codeToInject = samplerDecl + stHelpers + safeLogic + "\n"
                                                             "vec3 getCustomColor(vec2 in_uv) {\n"
                       + stLocalHelpers + initVars +
                       "    vec2 _st_coord = uv * iResolution.xy;\n"
                       "    _st_fragCoord = vec4(_st_coord, 0.0, 1.0);\n"
                       "    vec4 fragColor_out;\n"
                       "    mainImage(fragColor_out, _st_coord);\n"
                       "    return fragColor_out.rgb;\n"
                       "}\n";
    }
    else if (safeLogic.contains("getCustomColor")) {
        QString extHelpers;

        extHelpers += "#define iResolution vec3(1.0, 1.0, 1.0)\n"
                      "#define iTime ubuf.u_dummyZero.x\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";
        codeToInject = samplerDecl + extHelpers + safeLogic;
    }
    else {
        if (!safeLogic.contains("return")) {
            safeLogic += "\n    return vec3(u, v, 0.2); // Fallback\n";
        }
        codeToInject = samplerDecl +
                       "vec3 getCustomColor(vec2 in_uv) {\n"
                       + helpers +
                       "    float u = uv.x;\n"
                       "    float v = uv.y;\n"
                       + safeLogic + "\n"
                                     "}\n";
    }

    fullSource.replace("%CUSTOM_CODE%", codeToInject);

    fullSource.replace("vec4 tex = texture(textureSampler, v_texCoord);",
                       "vec4 tex = vec4(getCustomColor(v_texCoord), 1.0);");
    fullSource.replace("vec3 texColor = texture(textureSampler, v_texCoord).rgb;",
                       "vec3 texColor = getCustomColor(v_texCoord);");

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
        clearPipe(m_borderPipeline);
        return;
    }

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

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
    m_pipelineOpaque->setShaderResourceBindings(m_bindings);
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
    m_pipelineTranspBack->setShaderResourceBindings(m_bindings);
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
    m_pipelineTranspFront->setShaderResourceBindings(m_bindings);
    m_pipelineTranspFront->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipelineTranspFront->create()) {
        qWarning() << "buildPipeline: create() transpBack fallita.";
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
    m_wireframePipeline->setShaderResourceBindings(m_bindings);
    m_wireframePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_wireframePipeline->create()) {
        qWarning() << "buildPipeline: create() transpBack fallita.";
        delete m_wireframePipeline; m_wireframePipeline = nullptr;
    }

    // --- PIPELINE BORDER ---
    if (m_borderPipeline) {
        delete m_borderPipeline;
        m_borderPipeline = nullptr;
    }
    m_borderPipeline = rhi()->newGraphicsPipeline();
    m_borderPipeline->setTopology(QRhiGraphicsPipeline::Lines); // Topologia a linee sparse
    m_borderPipeline->setVertexInputLayout(inputLayout);
    m_borderPipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_borderPipeline->setSampleCount(renderTarget()->sampleCount());
    m_borderPipeline->setDepthTest(true);
    m_borderPipeline->setDepthWrite(true);
    m_borderPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_borderPipeline->setDepthBias(-3);
    m_borderPipeline->setSlopeScaledDepthBias(-3.0f);
    m_borderPipeline->setTargetBlends({ blend });
    m_borderPipeline->setShaderResourceBindings(m_borderBindings); // <-- Binding esclusivo
    m_borderPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_borderPipeline->create()) {
        qWarning() << "buildPipeline: create() transpBack fallita.";
        delete m_borderPipeline; m_borderPipeline = nullptr;
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
