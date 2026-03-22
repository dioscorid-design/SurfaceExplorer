#include "glwidget.h"
#include "geometrybuilder.h"
#include "inputhandler.h"
#include "surfaceengine.h"
#include "texturemanager.h"
#include "glsltranslator.h"

#include <QTimer>
#include <cmath>
#include <QtMath>
#include <QMouseEvent>
#include <QDebug>
#include <algorithm>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

#define STEP_MIN 1
#define STEP_MAX 50

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
    : QOpenGLWidget(parent),
    m_vbo(QOpenGLBuffer::VertexBuffer),
    m_ibo(QOpenGLBuffer::IndexBuffer),
    m_bgVbo(QOpenGLBuffer::VertexBuffer)
{
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(3, 0);
#elif defined(Q_OS_MAC)
    // macOS è fermo a OpenGL 4.1
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
#else
    // Windows e Linux: Richiediamo l'ultima versione (4.6)
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CoreProfile);
#endif

    setFormat(format);

    m_inputHandler = std::make_unique<InputHandler>(this);

    m_flatPan = QVector2D(0.0f, 0.0f);
    m_flatZoom = 1.0f;
    m_flatRotation = 0.0f;

    m_rotationQuat = QQuaternion();

    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_AcceptTouchEvents);

    engine = std::make_unique<SurfaceEngine>();

    rotationTimer = new QTimer(this);
    rotationTimer->setInterval(16);
    connect(rotationTimer, &QTimer::timeout, this, &GLWidget::updateRotation);
    rotationTimer->stop();

    m_textureEnabled = false;

    showBorders = false;
    nutationSpeed = precessionSpeed = spinSpeed = 0.0f;
    omegaSpeed = phiSpeed = psiSpeed = 0.0f;
    precession = 0.0f; nutation = 0.0f; spin = 0.0f;
    red = green = blue = 0.5f; alpha = 1.0f;

    setFocusPolicy(Qt::StrongFocus);

    m_cameraPos = QVector3D(0.0f, 0.0f, 4.0f);
    m_cameraYaw = 0.0f;
    m_cameraPitch = 0.0f;
    m_cameraRoll = 0.0f;

    m_pathTarget = QVector3D(0.0f, 0.0f, 0.0f);
    m_pathUp = QVector3D(0.0f, 0.0f, 1.0f);

    m_texManager = std::make_unique<TextureManager>();

    // Configura il timer per l'animazione Shadertoy
    m_animTimer = new QTimer(this);
    // 16 ms ≈ 60 FPS. Se va ancora a scatti, prova 33 ms (30 FPS)
    m_animTimer->setInterval(33);
    connect(m_animTimer, &QTimer::timeout, this, QOverload<>::of(&GLWidget::update));
}

GLWidget::~GLWidget()
{
    if (m_vbo.isCreated()) m_vbo.destroy();
    if (m_ibo.isCreated()) m_ibo.destroy();
}


// ==========================================================
// PROTECTED
// ==========================================================

void GLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    m_vao.create();
    m_vao.bind();

    m_texManager->init();

    // --- FIX MAC M4: Creare la texture dummy ---
    createDummyTexture();
    // -------------------------------------------

    setColor(0.2f, 0.8f, 0.2f); // Verde
    setTextureEnabled(false);

    m_vbo.create(); m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_ibo.create(); m_ibo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    if(m_eqX.isEmpty()) {
        m_eqX = "sin(u)"; m_eqY = "cos(u)"; m_eqZ = "v"; m_eqW = "0";
        engine->setEquations(m_eqX, m_eqY, m_eqZ, m_eqW);
    }

    rebuildShader();

    m_borderVbo.create();
    m_borderVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    m_wireframeVbo.create();
    m_wireframeVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    glEnable(GL_DEPTH_TEST);

    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);

    m_vao.release();
    initBackgroundShader();
}

void GLWidget::resizeGL(int w, int h)
{
    qreal ratio = devicePixelRatio();
    glViewport(0, 0, w * ratio, h * ratio);

    m_projection.setToIdentity();
    m_projection.perspective(45.0f, GLfloat(w) / h, 0.1f, 1000.0f);
}

void GLWidget::paintGL()
{
    // 1. PULIZIA
    glClearColor(m_bgColor.x(), m_bgColor.y(), m_bgColor.z(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    bool forceBgDraw = (m_isFlatView && m_flatViewTarget == 1);

    if (m_useBackgroundTexture && (m_bgTexture || m_bgIsScript)) {
        drawBackground();
        // Pulisci il depth buffer dopo lo sfondo affinché la superficie 3D venga disegnata sopra
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    if (m_isFlatView && m_flatViewTarget == 1) {
        return;
    }

    if (!m_program) return;
    if (meshNeedsUpdate) updateSurfaceData();
    if (!m_isFlatView && engine->getIndices().empty()) return;

    m_program->bind();

    // 1. Tempo per le Texture (Sempre in esecuzione se la texture è animata)
    float texTimeToUse = (!m_animTimer->isActive()) ? m_manualTime : (float)m_elapsedTimer.elapsed() / 1000.0f;

    // 2. Tempo per la Deformazione (Controllato solo dal tasto START)
    float surfTimeToUse = m_surfaceTimeOffset;
    if (m_surfaceAnimating) {
        surfTimeToUse += (float)m_surfaceTimer.elapsed() / 1000.0f;
    }

    // Modalità Esportazione Video: forza la sincronizzazione perfetta per entrambi
    if (property("use_virtual_time").toBool()) {
        float vt = property("virtual_time").toFloat();
        texTimeToUse = vt;
        surfTimeToUse = vt;
    }

    // Passa i tempi separati agli shader
    m_program->setUniformValue("u_time", surfTimeToUse);
    m_program->setUniformValue("t", surfTimeToUse);
    m_program->setUniformValue("iTime", texTimeToUse);

    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

    // 2. CALCOLO MATRICI
    m_gpuZoom = -std::log2(m_flatZoom);
    m_gpuCenter = m_flatPan;

    m_projection.setToIdentity();
    m_view.setToIdentity();
    m_model.setToIdentity();
    m_model.scale(m_surfaceScale);

    if (m_isFlatView) {
        float aspect = (float)width() / (float)height();
        m_projection.ortho(-aspect, aspect, -1.0f, 1.0f, -1.0f, 10.0f);
        m_view.lookAt(QVector3D(0,0,1), QVector3D(0,0,0), QVector3D(0,1,0));
        m_view.rotate(m_flatRotation, 0.0f, 0.0f, 1.0f);
    } else {
        m_projection.perspective(45.0f, GLfloat(width()) / height(), 0.1f, 1000.0f);

        if (m_isPathFollowing) {
            QMatrix4x4 baseView;

            baseView.lookAt(m_cameraPos, m_pathTarget, m_pathUp);

            // 2. Gestione Rollio (Inclinazione testa)
            if (std::abs(m_pathRoll) > 0.001f) {
                // Conversione: Math Engine (Radianti) -> OpenGL Qt (Gradi)
                float rollDeg = qRadiansToDegrees(m_pathRoll);

                // Creiamo una matrice che ruota attorno all'asse Z (l'asse di profondità nella vista)
                QMatrix4x4 rollMat;
                rollMat.rotate(rollDeg, 0.0f, 0.0f, 1.0f);

                // 3. APPLICAZIONE CORRETTA: Roll * View
                m_view = rollMat * baseView;
            } else {
                m_view = baseView;
            }
        } else {
            // Modalità Navigazione Manuale Standard
            m_view.translate(0, 0, -4.0);
            m_view.rotate(m_cameraRoll, 0.0f, 0.0f, 1.0f);
            m_view.rotate(m_cameraPitch, 1.0f, 0.0f, 0.0f);
            m_view.rotate(m_cameraYaw,   0.0f, 1.0f, 0.0f);
            m_view.translate(-m_cameraPos);
        }

        m_model.rotate(m_rotationQuat);
    }

    QMatrix4x4 modelView = m_view * m_model;
    QMatrix4x4 mvp = m_projection * modelView;

    // 3. INVIO MATRICI (Nomi Nuovi)
    m_program->setUniformValue(m_matrixUniform, mvp);

    if (m_isFlatUniform != -1) {
        m_program->setUniformValue(m_isFlatUniform, m_isFlatView);
    }

    m_program->setUniformValue(m_modelViewUniform, modelView);
    m_program->setUniformValue(m_modelUniform, m_model);

    // 4. INVIO DUMMY (Essenziale per Mac M4)
    m_program->setUniformValue(m_dummyUniform, QVector4D(0.0f, 0.0f, 0.0f, 0.0f));

    // 5. INVIO PARAMETRI A, B, C (Il punto che mancava!)
    // Creiamo il pacchetto prendendo i valori dall'engine
    float valA = engine->getValA();
    float valB = engine->getValB();
    float valC = engine->getValC();
    float valD = engine->getValD();
    float valE = engine->getValE();
    float valF = engine->getValF();
    float valS = engine->getValS();
    QVector4D paramsVector(valA, valB, valC, valS);

    m_program->setUniformValue(m_mathParamsUniform, paramsVector);
    m_program->setUniformValue(m_mathParams2Uniform, QVector4D(valD, valE, valF, 0.0f));

    // --- 6. ALTRI PARAMETRI (MODIFICATO PER GESTIRE W) ---

    // Variabili per i limiti da inviare allo shader
    float shaderMin1, shaderMax1, shaderMin2, shaderMax2;

    SurfaceEngine::ConstraintMode mode = engine->getConstraintMode();

    if (mode == SurfaceEngine::ConstraintU) {
        // CASO: u = f(v, w)
        shaderMin1 = engine->getVMin();
        shaderMax1 = engine->getVMax();
        // Parametro 2 diventa W
        shaderMin2 = engine->getWMin();
        shaderMax2 = engine->getWMax();
    }
    else if (mode == SurfaceEngine::ConstraintV) {
        // CASO: v = g(u, w)
        // Parametro 1 diventa U
        shaderMin1 = engine->getUMin();
        shaderMax1 = engine->getUMax();
        // Parametro 2 diventa W
        shaderMin2 = engine->getWMin();
        shaderMax2 = engine->getWMax();
    }
    else {
        // CASO STANDARD: w = h(u, v) (o nessun vincolo 4D)
        // Parametro 1 è U
        shaderMin1 = engine->getUMin();
        shaderMax1 = engine->getUMax();
        // Parametro 2 è V
        shaderMin2 = engine->getVMin();
        shaderMax2 = engine->getVMax();
    }

    // Ora inviamo i valori alle uniform esistenti.
    // m_uMinUniform è la "scatola" per il primo parametro (indipendentemente che sia U o V)
    m_program->setUniformValue(m_uMinUniform, shaderMin1);
    m_program->setUniformValue(m_uMaxUniform, shaderMax1);

    // m_vMinUniform è la "scatola" per il secondo parametro (indipendentemente che sia V o W)
    m_program->setUniformValue(m_vMinUniform, shaderMin2);
    m_program->setUniformValue(m_vMaxUniform, shaderMax2);

    m_program->setUniformValue(m_omegaUniform, getOmega());
    m_program->setUniformValue(m_phiUniform, getPhi());
    m_program->setUniformValue(m_psiUniform, getPsi());

    m_program->setUniformValue(m_projModeUniform, projectionMode);

    // Render Mode & Colors
    m_program->setUniformValue(m_zoomUniform, m_gpuZoom);
    m_program->setUniformValue(m_centerUniform, m_gpuCenter);

    if (m_isFlatView) {
        m_program->setUniformValue(m_rotationUniform, 0.0f);
    } else {
        m_program->setUniformValue(m_rotationUniform, m_flatRotation);
    }

    m_program->setUniformValue(m_col1Uniform, QVector3D(texRed1, texGreen1, texBlue1));
    m_program->setUniformValue(m_col2Uniform, QVector3D(texRed2, texGreen2, texBlue2));

    m_program->setUniformValue(m_specularUniform, m_isSpecularEnabled);
    m_program->setUniformValue(m_alphaUniform, alpha);

    if (m_lightIntensityUniform != -1) {
        m_program->setUniformValue(m_lightIntensityUniform, m_lightIntensity);
    }

    if (m_hasExplicitWUniform != -1) {
        bool hasW = !engine->getExplicitW().isEmpty();
        m_program->setUniformValue(m_hasExplicitWUniform, hasW);
    }

    if (m_lightingModeUniform != -1) {
        // Se is4DActive() è vero (tasto premuto o flag attivo), mandiamo la modalità
        // Altrimenti mandiamo 0 (luce standard)
        int modeToSend = 0;

        // is4DActive() controlla se l'illuminazione 4D è abilitata nell'engine
        if (is4DActive()) {
            modeToSend = m_lightingMode4D; // 1 = Radial, 2 = Observer
        }

        m_program->setUniformValue(m_lightingModeUniform, modeToSend);
    }

    if (m_observerPosUniform != -1) {
        // Inviamo la posizione dell'osservatore (che si muove coi tasti)
        m_program->setUniformValue(m_observerPosUniform, m_observerPos);
    }

    if (m_cameraPosUniform != -1) {
        // Calcola posizione camera nello spazio mondo
        QVector3D camWorldPos = m_cameraPos;

        // Se non siamo in path following, aggiungi l'offset di default
        if (!m_isPathFollowing) {
            // La camera ha un offset di (0, 0, -4) in view space
            // che diventa (0, 0, 4) in world space per la default view
            QMatrix4x4 viewInv = m_view.inverted();
            camWorldPos = (viewInv * QVector4D(0, 0, 0, 1)).toVector3D();
        }

        m_program->setUniformValue(m_cameraPosUniform, camWorldPos);
    }

    if (m_cameraPos4DUniform != -1) {
        m_program->setUniformValue(m_cameraPos4DUniform, m_cameraPos4D);
    }

    if (m_iTimeUniform != -1) {
        // iTime: Secondi trascorsi dall'avvio dello shader
        float timeSec = m_elapsedTimer.elapsed() / 1000.0f;
        m_program->setUniformValue(m_iTimeUniform, timeSec);
    }

    if (m_iResolutionUniform != -1) {
        // iResolution: Larghezza, Altezza, Aspect Ratio
        // Nota: Shadertoy usa pixel reali. Qui usiamo la dimensione del widget.
        m_program->setUniformValue(m_iResolutionUniform, QVector3D(width(), height(), 1.0f));
    }

    // Mouse (Placeholder semplice: 0,0 se non implementi il tracking completo)
    if (m_iMouseUniform != -1) {
        m_program->setUniformValue(m_iMouseUniform, QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
    }

    // 7. DISEGNO (Draw Calls)
    if (renderMode != 2) { // SOLIDO
        m_vbo.bind(); m_ibo.bind();
        quintptr offset = 0;
        m_program->enableAttributeArray(0); m_program->setAttributeBuffer(0, GL_FLOAT, offset, 3, sizeof(Vertex)); offset += sizeof(QVector3D);
        m_program->enableAttributeArray(1); m_program->setAttributeBuffer(1, GL_FLOAT, offset, 4, sizeof(Vertex)); offset += sizeof(QVector4D);
        m_program->enableAttributeArray(2); m_program->setAttributeBuffer(2, GL_FLOAT, offset, 2, sizeof(Vertex));

        // Texture Logic
        glActiveTexture(GL_TEXTURE0);
        if (m_textureEnabled && m_texManager) {
            m_texManager->bind(0);
            m_program->setUniformValue(m_useTextureUniform, true);
            m_program->setUniformValue(m_colorUniform, QVector3D(1, 1, 1));
        } else {
            glBindTexture(GL_TEXTURE_2D, m_dummyTex);
            m_program->setUniformValue(m_useTextureUniform, false);
            m_program->setUniformValue(m_colorUniform, QVector3D(red, green, blue));
        }
        m_program->setUniformValue(m_textureUniform, 0);
        m_program->setUniformValue(m_renderModeUniform, renderMode);

        int indexCount = m_isFlatView ? 6 : (int)engine->getIndices().size();

        if (showBorders || renderMode == 2) {
            glEnable(GL_POLYGON_OFFSET_FILL);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
            glPolygonOffset(1.0f, 1.0f); // Offset più delicato su mobile
#else
            glPolygonOffset(2.0f, 2.0f); // Standard Desktop
#endif
        } else {
            // Se disegniamo solo la superficie solida, non serve offset!
            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        if (alpha < 0.99f) {
            glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDepthMask(GL_FALSE); glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT); glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
            glCullFace(GL_BACK);  glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
            glDisable(GL_CULL_FACE); glDepthMask(GL_TRUE);
        } else {
            glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE);
            if (!m_textureEnabled) m_program->setUniformValue(m_alphaUniform, 1.0f);
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        }
        m_ibo.release(); m_vbo.release();
    }

    // 8. WIREFRAME & BORDERS (Se necessari)
    if (renderMode == 2 && !m_isFlatView && m_wireframeVertexCount > 0) {
        m_wireframeVbo.bind();
        m_program->enableAttributeArray(0); m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
        m_program->enableAttributeArray(1); m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
        m_program->disableAttributeArray(2);
        m_program->setUniformValue(m_renderModeUniform, 2);
        m_program->setUniformValue(m_useTextureUniform, false);
        m_program->setUniformValue(m_colorUniform, QVector3D(red, green, blue));
        m_program->setUniformValue(m_alphaUniform, 1.0f);
        glLineWidth(1.5f); glDrawArrays(GL_LINES, 0, m_wireframeVertexCount);
        m_wireframeVbo.release();
    }

    if (showBorders && m_borderVertexCount > 0 && !m_isFlatView) {
        m_borderVbo.bind();
        m_program->enableAttributeArray(0); m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 0);
        m_program->disableAttributeArray(1); m_program->disableAttributeArray(2);
        m_program->setUniformValue(m_renderModeUniform, 5);
        m_program->setUniformValue(m_useTextureUniform, false);
        m_program->setUniformValue(m_colorUniform, QVector3D(bordRed, bordGreen, bordBlue));
        m_program->setUniformValue(m_alphaUniform, 1.0f);
        glDisable(GL_POLYGON_OFFSET_FILL); glLineWidth(3.0f);
        glLineWidth(8.0f);
        glDrawArrays(GL_LINES, 0, m_borderVertexCount);
        glLineWidth(1.0f);
        m_borderVbo.release();
    }

    if (m_textureEnabled) m_texManager->release();
    m_program->release();
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
    if (m_inputHandler->handleTouch(e)) {
        return true;
    }

    return QOpenGLWidget::event(e);
}

void GLWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_inputHandler) {
        m_inputHandler->handleMouseRelease(event);
    }

    QOpenGLWidget::mouseReleaseEvent(event);
}


// ==========================================================
// PRIVATE SLOT
// ==========================================================

void GLWidget::updateRotation() {
    float timeFactor = m_slowModeActive ? 0.1f : 1.0f;

    float speedMult3D = 2.0f;
    float speedMult4D = 0.05f;

    float dPrec = precessionSpeed * speedMult3D * timeFactor;
    float dNut  = nutationSpeed   * speedMult3D * timeFactor;
    float dSpin = spinSpeed       * speedMult3D * timeFactor;

    if (std::abs(dPrec) > 0.0001f || std::abs(dNut) > 0.0001f || std::abs(dSpin) > 0.0001f) {
        addObjectRotation(dPrec, dNut, dSpin);
    }
    // ------------- Rotazioni 4D -----------
    omega += omegaSpeed * speedMult4D * timeFactor;
    phi   += phiSpeed   * speedMult4D * timeFactor;
    psi   += psiSpeed   * speedMult4D * timeFactor;

    // Controllo aggiornamento mesh (Invariato)
    if (std::abs(omegaSpeed) > 0.001f || std::abs(phiSpeed) > 0.001f || std::abs(psiSpeed) > 0.001f ||
        std::abs(nutationSpeed) > 0.001f || std::abs(precessionSpeed) > 0.001f || std::abs(spinSpeed) > 0.001f) {
        meshNeedsUpdate = true;
    }

    emit rotationChanged();
    update();
}


// ==========================================================
// EQUATIONS & MATHEMATICS
// ==========================================================

bool GLWidget::setParametricEquations(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq)
{
    // Backup delle vecchie equazioni in caso di errore
    QString oldX = m_eqX, oldY = m_eqY, oldZ = m_eqZ, oldW = m_eqW;

    // 1. Memorizza le stringhe
    m_eqX = xEq;
    m_eqY = yEq;
    m_eqZ = zEq;
    m_eqW = wEq;

    // 2. Passale anche all'engine
    engine->setEquations(xEq, yEq, zEq, wEq);

    if (!isValid()) {
        meshNeedsUpdate = true;
        return true;
    }

    // 3. Ricompila lo shader con le nuove formule!
    makeCurrent();
    bool success = rebuildShader();
    doneCurrent();

    // 4. Se fallisce davvero (per un errore dell'utente), ripristina e blocca
    if (!success) {
        m_eqX = oldX; m_eqY = oldY; m_eqZ = oldZ; m_eqW = oldW;
        engine->setEquations(oldX, oldY, oldZ, oldW);
        return false;
    }

    // 5. Tutto ok: aggiorna e disegna
    meshNeedsUpdate = true;
    update();

    return true;
}

void GLWidget::setExplicitWEquation(const QString &eq) {
    engine->setExplicitW(eq);
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setEquationConstants(float a, float b, float c, float d, float e, float f, float s) {
    engine->setConstants(a, b, c, d, e, f, s);
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setRangeU(float min, float max)
{
    if (engine) engine->setRangeU(min, max);
    meshNeedsUpdate = true;
}

void GLWidget::setRangeV(float min, float max)
{
    if (engine) engine->setRangeV(min, max);
    meshNeedsUpdate = true;
}

void GLWidget::setRangeW(float min, float max)
{
    if (engine) engine->setRangeW(min, max);
    meshNeedsUpdate = true;
}

void GLWidget::setResolution(int n) {

    engine->setResolution(n, n);
    meshNeedsUpdate = true;
}


// ==========================================================
// RENDERING & VISUALS
// ==========================================================

void GLWidget::updateSurfaceData()
{
    engine->computeMesh();


    if (m_isFlatView) {
        meshNeedsUpdate = false;
        return;
    }

    m_vao.bind();
    uploadGeometry(); // Carica la mesh solida
    m_vao.release();

    buildWireframeGeometry();
    buildBorderGeometry();

    meshNeedsUpdate = false;
    update();
}

void GLWidget::resetVisuals()
{
    makeCurrent(); // ESSENZIALE: previene la corruzione della memoria OpenGL

    engine->clear();
    m_lightingMode4D = 1;

    meshNeedsUpdate = false;
    uploadGeometry();
    m_borderVertexCount = 0;
    m_wireframeVertexCount = 0;

    if (m_animTimer->isActive()) {
        m_animTimer->stop();
    }

    // --- ELIMINA LA VECCHIA TEXTURE E RICARICA IL DEFAULT ---
    if (m_bgTexture) {
        delete m_bgTexture;
        m_bgTexture = nullptr;
    }

    QImage defaultImg(":/background.png");
    if (!defaultImg.isNull()) {
        m_bgTexture = new QOpenGLTexture(defaultImg.flipped(Qt::Vertical));
        m_bgTexture->setMinificationFilter(QOpenGLTexture::Linear);
        m_bgTexture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_bgTexture->setWrapMode(QOpenGLTexture::MirroredRepeat);
    }
    // --------------------------------------------------------

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

    doneCurrent(); // Rilascia il contesto
    update();
}

void GLWidget::setProjectionMode(int mode) {
    projectionMode = mode;
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setRenderMode(int mode) {
    renderMode = mode;
    update();
}

void GLWidget::setShowBorders(bool enable) {
    showBorders = enable;
    update();
}

void GLWidget::setColor(float r, float g, float b) {
    red = r; green = g; blue = b;
    update();
}

void GLWidget::setBorderColor(float r, float g, float b) {
    bordRed = r; bordGreen = g; bordBlue = b;
    update();
}

void GLWidget::setAlpha(float a) {
    alpha = a;
    update();
}

void GLWidget::setSpecularEnabled(bool enabled) {
    m_isSpecularEnabled = enabled;
    update();
}

void GLWidget::setLightIntensity(float intensity) {
    m_lightIntensity = intensity;
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

bool GLWidget::rebuildShader()
{
    // 1. Genera Codice
    QString vertexSource = createVertexShaderSource(m_eqX, m_eqY, m_eqZ, m_eqW);
    // Passando stringa vuota, genera il fragment shader di default
    QString fragSource = createFragmentShaderSource("");

    // 2. Compila e Linka
    QOpenGLShaderProgram *newProgram = new QOpenGLShaderProgram(this);
    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)) {
        delete newProgram; return false;
    }
    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragSource)) {
        qDebug() << "Fragment Error:" << newProgram->log(); delete newProgram; return false;
    }
    if (!newProgram->link()) {
        qDebug() << "Link Error:" << newProgram->log(); delete newProgram; return false;
    }

    // 3. Scambio Programmi e Fetch Uniforms
    if (m_program) delete m_program;
    m_program = newProgram;

    fetchUniformLocations();

    return true;
}


// ==========================================================
// TEXTURES, SCRIPTS & BACKGROUND
// ==========================================================

void GLWidget::loadTextureFromFile(const QString &f) {
    m_texManager->loadFromFile(f);
    update();
}

void GLWidget::loadTextureFromImage(const QImage &img) {
    makeCurrent();

    if (m_texManager) {
        m_texManager->loadFromImage(img);
    }
    doneCurrent();
    update();
}

void GLWidget::setTextureEnabled(bool enable) {
    m_textureEnabled = enable;
    update();
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

void GLWidget::resetTexture() {
    makeCurrent();
    if (m_texManager) {
        m_texManager->init();
    }
    doneCurrent();
    update();
}

void GLWidget::clearTexture() {
    makeCurrent();

    // 1. Diciamo al tuo gestore personalizzato di azzerarsi
    if (m_texManager) {
        m_texManager->init();
    }

    // 2. Forza la scheda video a "sputare" la vecchia immagine dallo slot
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_dummyTex);

    doneCurrent();
    update();
}

void GLWidget::setScriptCheck(bool enabled) {
    engine->setScriptMode(enabled);
    meshNeedsUpdate = true;
}

void GLWidget::loadCustomShader(const QString &customCode)
{
    makeCurrent();

    // 1. Prepara logica Vertex (usa le equazioni correnti dell'engine)
    QString vertexSource = createVertexShaderSource(m_eqX, m_eqY, m_eqZ, m_eqW);

    // 2. Prepara Logica Fragment Custom
    QString commonLogic = loadShaderSource(":/shaders/common.glsl");

    if (commonLogic.isEmpty()) {
        commonLogic = "float u = raw_uv.x; float v = raw_uv.y;";
        qDebug() << "WARNING: common.glsl not found/empty in loadCustomShader. Using fallback.";
    }

    QString codeBody = customCode.trimmed();
    QString finalCustomLogic;

    if (codeBody.contains("void mainImage") || codeBody.contains("mainImage(")) {
        // --- SHADERTOY MODE ---
        QString shadertoyUniforms =
            "uniform float iTime;\n"
            "uniform vec3 iResolution;\n"
            "uniform vec4 iMouse;\n"
            "#define iChannel0 textureSampler\n"
            "#define iChannel1 textureSampler\n";

        QString adapter =
            "\nvec3 getCustomColor(vec2 raw_uv) {\n"
            "    " + commonLogic + "\n" // Include zoom/pan logic
                            "    vec2 fragCoord = uv * iResolution.xy;\n"
                            "    vec4 fragColor;\n"
                            "    mainImage(fragColor, fragCoord);\n"
                            "    return fragColor.rgb;\n"
                            "}\n";

        finalCustomLogic = shadertoyUniforms + "\n" + codeBody + "\n" + adapter;
        m_elapsedTimer.restart();
    } else {
        // --- SIMPLE GLSL MODE ---
        if (codeBody.isEmpty()) codeBody = "return vec3(1.0, 0.0, 1.0);";

        if (!codeBody.contains("return") && !codeBody.contains(";")) {
            codeBody = "return " + codeBody + ";";
        }

        finalCustomLogic =
            "vec3 getCustomColor(vec2 raw_uv) {\n" +
            commonLogic + "\n" + // Qui vengono definite 'u' e 'v'
            codeBody + "\n}";
    }

    // 3. Genera Fragment Source Completo
    QString fragSource = createFragmentShaderSource(finalCustomLogic);

    // 4. Compila e Linka
    QOpenGLShaderProgram *newProgram = new QOpenGLShaderProgram(this);

    // Vertex
    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)) {
        qDebug() << "Custom Vertex Error:" << newProgram->log();
        delete newProgram;
        doneCurrent();
        return;
    }

    // Fragment
    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragSource)) {
        qDebug() << "Custom Fragment Error:" << newProgram->log();
        delete newProgram;
        doneCurrent();
        return;
    }

    // Link
    if (!newProgram->link()) {
        qDebug() << "Custom Link Error:" << newProgram->log();
        delete newProgram;
        doneCurrent();
        return;
    }

    // 5. Scambio e Fetch
    if (m_program) delete m_program;
    m_program = newProgram;

    fetchUniformLocations();

    // Setup stato UI
    setRenderMode(11); // Modalità Custom
    setTextureEnabled(true);

    if (!m_animTimer->isActive()) m_animTimer->start();

    doneCurrent();

    update();
}

void GLWidget::setShaderTime(float t) {
    m_manualTime = t;
}

void GLWidget::setBackgroundColor(const QColor &c) {
    m_bgColor = QVector3D(c.redF(), c.greenF(), c.blueF());
    update();
}

void GLWidget::setBackgroundTexture(const QString &path) {
    makeCurrent();

    if (m_bgTexture) {
        delete m_bgTexture;
        m_bgTexture = nullptr;
    }

    // Carica la texture
    QImage img(path);
    if (!img.isNull()) {
        m_bgTexture = new QOpenGLTexture(img.flipped(Qt::Vertical));
        m_bgTexture->setMinificationFilter(QOpenGLTexture::Linear);
        m_bgTexture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_bgTexture->setWrapMode(QOpenGLTexture::MirroredRepeat);
    }

    if (m_bgIsScript) {
        rebuildBackgroundShader(true); // True = Texture Mode
    }

    doneCurrent();
    update();
}

void GLWidget::setBackgroundTextureEnabled(bool enabled) {
    m_useBackgroundTexture = enabled;
    update();
}

void GLWidget::loadBackgroundScript(const QString &scriptCode) {
    m_elapsedTimer.restart();
    if (m_animTimer && !m_animTimer->isActive()) {
        m_animTimer->start();
    }

    rebuildBackgroundShader(false, scriptCode);

    setBackgroundTextureEnabled(true);
}


// ==========================================================
// 2D FLAT VIEW
// ==========================================================

void GLWidget::setFlatView(bool active) {
    m_isFlatView = active;

    if (m_isFlatView) {
        uploadQuadGeometry();
    } else {
        uploadGeometry();
        buildBorderGeometry();
        buildWireframeGeometry();
    }
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
        setProperty("bg_zoom", std::clamp(z, 0.01f, 50.0f));
    } else { // 0 = Superficie
        m_flatZoom = std::clamp(z, 0.01f, 50.0f);
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
    meshNeedsUpdate = true;

    update();
}

void GLWidget::setCameraPosAndLookAt(const QVector3D& pos, float wValue)
{
    m_cameraPos = pos;
    if (std::abs(m_observerPos.w() - wValue) > 0.001f) {
        m_observerPos.setW(wValue);
        meshNeedsUpdate = true;
    }

    update();
}

void GLWidget::setCameraPosAndDirection(const QVector3D& pos, const QVector3D& targetPoint, float wValue)
{
    // 1. Aggiorna la Camera 3D (i tuoi "occhi")
    m_cameraPos = pos;
    m_pathTarget = targetPoint;
    m_isPathFollowing = true;
    m_pathUp = QVector3D(0.0f, 0.0f, 1.0f);

    // 2. SINCRONIZZAZIONE TOTALE 4D
    QVector4D newObsPos(pos.x(), pos.y(), pos.z(), wValue);

    // 3. Invio dati se cambiati
    if ((m_observerPos - newObsPos).lengthSquared() > 0.000001f) {
        m_observerPos = newObsPos;

        // Segnala che i dati sono cambiati (per aggiornare gli uniform nel paintGL)
        meshNeedsUpdate = true;
    }

    update();
}

void GLWidget::setCameraPosAndDirection3D(const QVector3D& pos, const QVector3D& targetPoint, float roll)
{
    m_cameraPos = pos;
    m_pathTarget = targetPoint;
    m_isPathFollowing = true;
    m_pathUp = QVector3D(0.0f, 0.0f, 1.0f);

    // Imposta il rollio richiesto dal path 3D
    m_pathRoll = roll;

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

    meshNeedsUpdate = true;
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

    update();
}

void GLWidget::addCameraRoll(float dRoll) {
    m_cameraRoll += dRoll;
    update();
}

void GLWidget::moveCameraFromScreenDelta(float dx, float dy) {
    float radYaw = m_cameraYaw * M_PI / 180.0f;
    QVector3D front(sin(radYaw), 0, -cos(radYaw));
    QVector3D right(cos(radYaw), 0, sin(radYaw));

    float speed = 0.01f;

    m_cameraPos += right * (dx * speed);
    m_cameraPos -= front * (dy * speed);

    update();
}

void GLWidget::resetTransformations()
{
    m_isPathFollowing = false;
    pauseMotion();
    resetSurfaceTime();

    precession = 0.0f;
    nutation = 0.0f;
    spin = 0.0f;

    // Manteniamo il piccolo offset per evitare lo Z-Fighting
    omega = 0.1f;
    phi = 0.1f;
    psi = 0.1f;

    if (engine) {
        // Passing empty strings forces the engine to set isValid = false
        engine->compilePathEquations("", "", "", "", "", "", "");
        engine->compilePath3DEquations("", "", "", "");
    }

    nutationSpeed = 0.0f;
    precessionSpeed = 0.0f;
    spinSpeed = 0.0f;
    omegaSpeed = 0.0f;
    phiSpeed = 0.0f;
    psiSpeed = 0.0f;

    m_cameraPos = QVector3D(0.0f, 0.0f, 4.0f);
    m_cameraYaw = 0.0f;
    m_cameraPitch = 0.0f;
    m_cameraRoll = 0.0f;

    m_observerPos = QVector4D(0.0f, 0.0f, 0.0f, 4.0f);
    m_cameraPos4D = QVector4D(0.0f, 0.0f, 0.0f, 4.0f);

    m_pathUp = QVector3D(0.0f, 0.0f, 1.0f);
    m_flatPan = QVector2D(0.0f, 0.0f);
    m_flatZoom = 1.0f;
    m_flatRotation = 0.0f;

    setProperty("bg_zoom", 1.0f);
    setProperty("bg_pan", QVector2D(0.0f, 0.0f));
    setProperty("bg_rot", 0.0f);

    m_rotationQuat = QQuaternion();

    m_isFirstPathRun = true;
    m_lastValidUp = QVector3D(0.0f, 1.0f, 0.0f);

    // Forza aggiornamento geometria
    meshNeedsUpdate = true;

    update();
}

void GLWidget::virtualMove(MoveDir dir, bool slowMode)
{
    float speed = slowMode ? 0.01f : 0.05f;
    float rollSpeed = slowMode ? 0.05f : 0.2f;
    float rotSpeed = slowMode ? 0.005f : 0.01f;
    float obsSpeed = slowMode ? 0.01f : 0.05f;


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

    if (updateNeeded) {
        makeCurrent();
        updateSurfaceData();
        doneCurrent();
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
    m_elapsedTimer.restart();
    update(); //
}

void GLWidget::resetSurfaceTime() {
    m_surfaceTimeOffset = 0.00001f;
    m_surfaceAnimating = false;
}

void GLWidget::setSurfaceAnimating(bool animating) {
    if (animating == m_surfaceAnimating) return;
    m_surfaceAnimating = animating;

    if (animating) {
        m_surfaceTimer.restart();
        if (!m_animTimer->isActive()) m_animTimer->start();
    } else {
        m_surfaceTimeOffset += (float)m_surfaceTimer.elapsed() / 1000.0f;
    }
}


// ==========================================================
// UTILITIES
// ==========================================================

QImage GLWidget::getFrameForVideo() {
    // 1. BLOCCO DEL CONTESTO
    makeCurrent();

    // 2. AGGIORNAMENTO GEOMETRIA
    if (meshNeedsUpdate) {
        updateSurfaceData();
    }

    // 3. DISEGNO FORZATO
    paintGL();

    // 4. CATTURA
    QImage img = grabFramebuffer();

    // 5. RILASCIO
    doneCurrent();

    return img;
}


// ==========================================================
// PRIVATE HELPER METHODS
// ==========================================================

void GLWidget::uploadGeometry()
{
    if (!m_program) return;

    const auto& vertices = engine->getVertices();
    const auto& indices = engine->getIndices();

    if (vertices.empty() || indices.empty()) return;

    m_program->bind();

    m_vbo.bind();
    m_vbo.allocate(vertices.data(), int(vertices.size() * sizeof(Vertex)));

    m_ibo.bind();
    m_ibo.allocate(indices.data(), int(indices.size() * sizeof(unsigned int)));

    quintptr offset = 0;

    // Vertex (Location 0 nello shader moderno)
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, offset, 3, sizeof(Vertex));
    offset += sizeof(QVector3D);

    // Normal (Location 1) - Anche se lo shader calcola le normali, passiamo il placeholder
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, offset, 4, sizeof(Vertex));
    offset += sizeof(QVector4D);

    // TexCoord (Location 2)
    m_program->enableAttributeArray(2);
    m_program->setAttributeBuffer(2, GL_FLOAT, offset, 2, sizeof(Vertex));
}

void GLWidget::buildBorderGeometry() {
    std::vector<QVector3D> data = GeometryBuilder::buildBorders(engine.get());
    m_borderVertexCount = (int)data.size();

    if (m_borderVertexCount > 0) {
        m_borderVbo.bind();
        m_borderVbo.allocate(data.data(), m_borderVertexCount * sizeof(QVector3D));
        m_borderVbo.release();
    }
}

void GLWidget::buildWireframeGeometry() {
    std::vector<float> data = GeometryBuilder::buildWireframe(engine.get(), wfStepU, wfStepV);

    m_wireframeVertexCount = (int)data.size() / 6;

    if (m_wireframeVertexCount > 0) {
        m_wireframeVbo.bind();
        m_wireframeVbo.allocate(data.data(), (int)data.size() * sizeof(float));
        m_wireframeVbo.release();
    }
}

void GLWidget::uploadQuadGeometry() {
    float data[] = {
        // X, Y, Z,            NX, NY, NZ, NW,       U, V
        -1.0f, -1.0f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f,   0.0f, 0.0f, // Basso-Sx
        1.0f, -1.0f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f,   1.0f, 0.0f, // Basso-Dx
        -1.0f,  1.0f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f,   0.0f, 1.0f, // Alto-Sx
        1.0f,  1.0f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f,   1.0f, 1.0f  // Alto-Dx
    };

    unsigned int indices[] = { 0, 1, 2, 2, 1, 3 };

    m_vbo.bind();
    m_vbo.allocate(data, sizeof(data));
    m_vbo.release();

    m_ibo.bind();
    m_ibo.allocate(indices, sizeof(indices));
    m_ibo.release();

    m_borderVertexCount = 0;
    m_wireframeVertexCount = 0;
}

void GLWidget::initBackgroundShader() {
    makeCurrent();

    // 1. Crea Geometria Quad a tutto schermo (X, Y, U, V)
    float bgVertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
        1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
        1.0f,  1.0f,  1.0f, 1.0f,
    };

    m_bgVao.create();
    m_bgVao.bind();

    m_bgVbo.create();
    m_bgVbo.bind();
    m_bgVbo.allocate(bgVertices, sizeof(bgVertices));

    // 2. CREA LO SHADER "INTELLIGENTE" (Supporta Zoom e Pan)
    rebuildBackgroundShader(true, "");

    makeCurrent();

    // 3. Setup Attributi (0=Pos, 1=UV)
    m_bgProgram->bind();
    m_bgProgram->enableAttributeArray(0);
    m_bgProgram->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_bgProgram->enableAttributeArray(1);
    m_bgProgram->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    m_bgVao.release();
    m_bgProgram->release();

    // 4. TEXTURE DI DEFAULT BACKGROUND
    QImage defaultImg(":/background.png");

    m_bgTexture = new QOpenGLTexture(defaultImg.flipped(Qt::Vertical));
    m_bgTexture->setMinificationFilter(QOpenGLTexture::Linear);
    m_bgTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_bgTexture->setWrapMode(QOpenGLTexture::MirroredRepeat);

    m_bgIsScript = false;

    doneCurrent();
}

void GLWidget::rebuildBackgroundShader(bool isTextureMode, const QString &customCode) {
    makeCurrent();

    if (m_bgProgram) {
        delete m_bgProgram;
        m_bgProgram = nullptr;
    }

    m_bgProgram = new QOpenGLShaderProgram(this);

    // --- DEFINIZIONE DINAMICA VERSIONE GLSL ---
    QString glslVersion;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    glslVersion = "#version 300 es\n";
#elif defined(Q_OS_MAC)
    glslVersion = "#version 410 core\n";
#else
    glslVersion = "#version 460 core\n";
#endif
    // ------------------------------------------

    // 1. Vertex Shader
    const char *vsrc =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position = vec4(aPos, 0.999, 1.0); uv = aTexCoord; }";

    m_bgProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc);

    // 2. Fragment Shader
    QString fsrc;
    fsrc += "#version 330 core\n";
    fsrc += "out vec4 FragColor;\n";
    fsrc += "in vec2 uv;\n";

    // --- UNIFORMS STANDARD ---
    fsrc += "uniform float iTime;\n";
    fsrc += "uniform vec3 iResolution;\n";
    fsrc += "uniform vec4 iMouse;\n";
    fsrc += "uniform sampler2D bgTexture;\n";

    // --- UNIFORMS QT ---
    fsrc += "uniform vec3 u_col1;\n";
    fsrc += "uniform vec3 u_col2;\n";
    fsrc += "uniform bool u_isFlat;\n";
    fsrc += "uniform float u_zoom;\n";
    fsrc += "uniform float u_rotation;\n";
    fsrc += "uniform vec2 u_center;\n";
    fsrc += "uniform vec4 u_mathParams;\n";
    fsrc += "uniform vec4 u_mathParams2;\n";

    if (isTextureMode) {
        // Carichiamo la logica di zoom/pan
        QString commonLogic = loadShaderSource(":/shaders/common.glsl");
        if(commonLogic.isEmpty()) commonLogic = "float u = raw_uv.x; float v = raw_uv.y;";

        // Funzione per applicare la trasformazione
        fsrc += "vec2 getTransformedUV(vec2 raw_uv) {\n";
        fsrc += commonLogic;
        fsrc += "\n    return uv;\n"; // Ritorna le uv modificate da common.glsl
        fsrc += "}\n";

        fsrc += "void main() {\n";
        // 1. Correggiamo le proporzioni dello schermo (Aspect Ratio)
        fsrc += "    vec2 corrected_uv = uv;\n";
        fsrc += "    float aspect = iResolution.x / iResolution.y;\n";
        fsrc += "    corrected_uv.x = (corrected_uv.x - 0.5) * aspect + 0.5;\n";

        // 2. Applichiamo Zoom, Pan e Rotazione (mouse)
        fsrc += "    vec2 final_uv = getTransformedUV(corrected_uv);\n";

        // 3. Leggiamo l'immagine
        fsrc += "    FragColor = texture(bgTexture, final_uv);\n";
        fsrc += "}";
    } else {
        // FUNZIONI NOISE (Copiati da surface.vert/frag)
        fsrc += R"(
            float sys_hash(float n) { return fract(sin(n) * 1e4); }
            float sys_hash(vec2 p) { return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }
            float sys_noise(float x) {
                float i = floor(x);
                float f = fract(x);
                float u = f * f * (3.0 - 2.0 * f);
                return mix(sys_hash(i), sys_hash(i + 1.0), u);
            }
            float sys_noise(vec2 x) {
                vec2 i = floor(x);
                vec2 f = fract(x);
                float a = sys_hash(i);
                float b = sys_hash(i + vec2(1.0, 0.0));
                float c = sys_hash(i + vec2(0.0, 1.0));
                float d = sys_hash(i + vec2(1.0, 1.0));
                vec2 u = f * f * (3.0 - 2.0 * f);
                return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
            }
        )";

        QString commonLogic = loadShaderSource(":/shaders/common.glsl");
        if(commonLogic.isEmpty()) commonLogic = "float u = raw_uv.x; float v = raw_uv.y;";

        // --- ADAPTER SHADERTOY ---
        if (customCode.contains("void mainImage") || customCode.contains("mainImage(")) {
            fsrc += "#define iChannel0 bgTexture\n";
            fsrc += "#define iChannel1 bgTexture\n"; // <--- MANCAVA QUESTO!
            fsrc += "#define iChannel2 bgTexture\n"; // Aggiungiamo anche questi
            fsrc += "#define iChannel3 bgTexture\n"; // per sicurezza futura

            fsrc += customCode + "\n";

            // Inietta commonLogic per ereditare Zoom, Pan e Rotazione
            fsrc += "vec3 getCustomColor(vec2 raw_uv) {\n";
            fsrc += commonLogic;
            fsrc += "\n    vec4 fragColor;\n";
            fsrc += "    vec2 fragCoord = uv * iResolution.xy;\n";
            fsrc += "    mainImage(fragColor, fragCoord);\n";
            fsrc += "    return fragColor.rgb;\n";
            fsrc += "}\n";

            fsrc += "void main() {\n";
            fsrc += "    FragColor = vec4(getCustomColor(uv), 1.0);\n";
            fsrc += "}";
        }
        else {
            // --- MODO STANDARD (Simple Script) ---
            fsrc += "vec3 getCustomColor(vec2 raw_uv) {\n";
            fsrc += commonLogic;
            fsrc += "\n";
            fsrc += customCode;
            fsrc += "\n}\n";

            fsrc += "void main() {\n";
            // FIX ASPECT RATIO: Adatta l'asse X in base allo schermo
            fsrc += "    vec2 corrected_uv = uv;\n";
            fsrc += "    float aspect = iResolution.x / iResolution.y;\n";
            fsrc += "    corrected_uv.x = (corrected_uv.x - 0.5) * aspect + 0.5;\n";

            fsrc += "    vec3 col = getCustomColor(corrected_uv);\n";
            fsrc += "    FragColor = vec4(col, 1.0);\n";
            fsrc += "}";
        }
    }

    if (!m_bgProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc)) {
        qDebug() << "Background Shader Error:" << m_bgProgram->log();
    }

    m_bgProgram->link();
    m_bgIsScript = !isTextureMode;
    doneCurrent();
    update();
}

void GLWidget::drawBackground() {
    if (!m_bgProgram) return;

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    m_bgProgram->bind();
    m_bgVao.bind();

    m_bgProgram->setUniformValue("iResolution", QVector3D(width(), height(), 1.0f));

    // Usiamo le proprietà dinamiche di Qt. Se non esistono, diamo valori di default (1.0, 0.0)
    float bgZoom = property("bg_zoom").isValid() ? property("bg_zoom").toFloat() : 1.0f;
    QVector2D bgPan = property("bg_pan").isValid() ? property("bg_pan").value<QVector2D>() : QVector2D(0.0f, 0.0f);
    float bgRot = property("bg_rot").isValid() ? property("bg_rot").toFloat() : 0.0f;

    m_bgProgram->setUniformValue("u_zoom", -std::log2(bgZoom));
    m_bgProgram->setUniformValue("u_center", bgPan);
    m_bgProgram->setUniformValue("u_rotation", bgRot);

    if (m_bgIsScript) {
        float timeSec;
        if (property("use_virtual_time").toBool()) {
            timeSec = property("virtual_time").toFloat();
        } else {
            timeSec = (float)m_elapsedTimer.elapsed() / 1000.0f;
        }

        m_bgProgram->setUniformValue("iTime", timeSec);
        m_bgProgram->setUniformValue("iMouse", QVector4D(0,0,0,0));

        QVector3D col1 = property("bg_col1").isValid() ? property("bg_col1").value<QVector3D>() : QVector3D(0.0f, 0.0f, 0.1f);
        QVector3D col2 = property("bg_col2").isValid() ? property("bg_col2").value<QVector3D>() : QVector3D(0.0f, 0.0f, 0.0f);

        m_bgProgram->setUniformValue("u_col1", col1);
        m_bgProgram->setUniformValue("u_col2", col2);
        m_bgProgram->setUniformValue("u_isFlat", m_isFlatView);

        float valA = engine->getValA();
        float valB = engine->getValB();
        float valC = engine->getValC();
        float valD = engine->getValD();
        float valE = engine->getValE();
        float valF = engine->getValF();
        float valS = engine->getValS();
        m_bgProgram->setUniformValue("u_mathParams", QVector4D(valA, valB, valC, valS));
        m_bgProgram->setUniformValue("u_mathParams2", QVector4D(valD, valE, valF, 0.0f));

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_dummyTex);
        m_bgProgram->setUniformValue("bgTexture", 1);
    }
    else if (m_bgTexture) {
        glActiveTexture(GL_TEXTURE1);
        m_bgTexture->bind(1);
        m_bgProgram->setUniformValue("bgTexture", 1);
    }
    else {
        // Se l'immagine manca, forza la dummy texture per non leggere la superficie
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_dummyTex);
        m_bgProgram->setUniformValue("bgTexture", 1);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (m_bgTexture && !m_bgIsScript) {
        m_bgTexture->release(1);
    }

    m_bgVao.release();
    m_bgProgram->release();

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

QString GLWidget::createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq)
{
    // 1. Carica il template
    QString vertexTemplate = loadShaderSource(":/shaders/surface.vert");

    // 2. RIMUOVE QUALSIASI INTESTAZIONE ESISTENTE
    QRegularExpression headerCleanup("^\\s*(#version|precision).*\n", QRegularExpression::MultilineOption);
    vertexTemplate.remove(headerCleanup);
    vertexTemplate.remove(QRegularExpression("^\\s*#ifdef GL_ES[\\s\\S]*?#endif", QRegularExpression::MultilineOption));

    QString header;

    // 3. INTESTAZIONE PULITA E FORZATA
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Android/iOS: GLES 3.0 ESplicito
    header = "#version 300 es\n"
             "precision highp float;\n"
             "precision highp int;\n";
#elif defined(Q_OS_MAC)
    header = "#version 410 core\n";
#else
    header = "#version 460 core\n";
#endif

    QString safePowDef = R"(
    float safe_pow(float x, float y) {
        if (x >= 0.0) return pow(x, y);
        if (mod(y, 2.0) == 0.0) return pow(-x, y); // u^2 diventa positivo
        if (mod(y, 1.0) == 0.0) return -pow(-x, y); // u^3 diventa negativo
        return 0.0;
    }
    )";

    QString source = header + "\n" + safePowDef + "\n" + vertexTemplate;

    auto sanitizeEq = [](const QString &s) {
        // 1. Traduce u^2 in safe_pow(u, 2.0) e pi in 3.1415
        QString translated = GlslTranslator::translateEquation(s);

        // 2. Traduce la 't' nel tempo reale della GPU!
        translated.replace(QRegularExpression("\\bt\\b"), "u_time");

        // 3. Wrap di sicurezza finale
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

        QString injectedVars = "    float t = u_time;\n"
                               "    float A = u_mathParams.x;\n"
                               "    float B = u_mathParams.y;\n"
                               "    float C = u_mathParams.z;\n"
                               "    float s = u_mathParams.w;\n"
                               "    float S = u_mathParams.w;\n"
                               "    float D = u_mathParams2.x;\n"
                               "    float E = u_mathParams2.y;\n"
                               "    float F = u_mathParams2.z;\n";

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

    QString params = "float A=u_mathParams.x; "
                     "float B=u_mathParams.y; "
                     "float C=u_mathParams.z; "
                     "float _valS=u_mathParams.w; "
                     "float D=u_mathParams2.x; "
                     "float E=u_mathParams2.y; "
                     "float F=u_mathParams2.z; "
                     "float s=_valS; "
                     "float S=_valS; "
                     "float t=u_time; ";

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

    return source;
}

QString GLWidget::createFragmentShaderSource(const QString &customLogic)
{
    // 1. Carica Template
    QString fragmentTemplate = loadShaderSource(":/shaders/surface.frag");

    QRegularExpression versionRegex("^\\s*#version\\s+[0-9]+(\\s+es|\\s+core)?\\s*\n?");
    fragmentTemplate.remove(versionRegex);

    // 2. Header Versione (Cross-Platform)
    QString header;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    header = "#version 300 es\nprecision highp float;\n";
#elif defined(Q_OS_MAC)
    header = "#version 410 core\n";
#else
    header = "#version 460 core\n";
#endif

    QString fullSource = header + fragmentTemplate;

    QString safePowLogic = R"(
    float safe_pow(float x, float y) {
        if (x >= 0.0) return pow(x, y);
        if (mod(y, 2.0) == 0.0) return pow(-x, y);
        if (mod(y, 1.0) == 0.0) return -pow(-x, y);
        return 0.0;
    }
    )";

    // 3. Inserimento Logica Custom
    QString codeToInject = customLogic;
    if (codeToInject.isEmpty()) {
        QString commonLogic = loadShaderSource(":/shaders/common.glsl");
        codeToInject = "vec3 getCustomColor(vec2 raw_uv) {\n" +
                       commonLogic +
                       "\n    return texture(textureSampler, uv).rgb;\n}";
    }

    codeToInject = safePowLogic + "\n" + codeToInject;

    fullSource.replace("%CUSTOM_CODE%", codeToInject);

    // 4. Patch Compatibilità (Texture vs Funzione)
    fullSource.replace("vec4 tex = texture(textureSampler, v_texCoord);",
                       "vec4 tex = vec4(getCustomColor(v_texCoord), 1.0);");
    fullSource.replace("vec3 texColor = texture(textureSampler, v_texCoord).rgb;",
                       "vec3 texColor = getCustomColor(v_texCoord);");

    return fullSource;
}

void GLWidget::fetchUniformLocations()
{
    if (!m_program) return;

    // Matrici
    m_matrixUniform     = m_program->uniformLocation("u_mvpMatrix");
    m_modelViewUniform  = m_program->uniformLocation("u_mvMatrix");
    m_modelUniform      = m_program->uniformLocation("u_mMatrix");

    // Colore e Texture
    m_colorUniform          = m_program->uniformLocation("color");
    m_alphaUniform          = m_program->uniformLocation("alpha");
    m_useTextureUniform     = m_program->uniformLocation("useTexture");
    m_textureUniform        = m_program->uniformLocation("textureSampler");
    m_specularUniform       = m_program->uniformLocation("useSpecular");
    m_lightIntensityUniform = m_program->uniformLocation("u_lightIntensity");

    // Limiti UV / Matematica
    m_uMinUniform        = m_program->uniformLocation("u_min");
    m_uMaxUniform        = m_program->uniformLocation("u_max");
    m_vMinUniform        = m_program->uniformLocation("v_min");
    m_vMaxUniform        = m_program->uniformLocation("v_max");
    m_mathParamsUniform  = m_program->uniformLocation("u_mathParams");
    m_mathParams2Uniform = m_program->uniformLocation("u_mathParams2");
    m_dummyUniform       = m_program->uniformLocation("u_dummyZero");

    // 4D e Illuminazione
    m_lightingModeUniform = m_program->uniformLocation("u_lightingMode");
    m_observerPosUniform  = m_program->uniformLocation("u_observerPos");
    m_cameraPosUniform    = m_program->uniformLocation("u_cameraPos");
    m_cameraPos4DUniform  = m_program->uniformLocation("u_cameraPos4D");
    m_hasExplicitWUniform = m_program->uniformLocation("u_hasExplicitW");

    // Rotazioni
    m_omegaUniform    = m_program->uniformLocation("u_omega");
    m_phiUniform      = m_program->uniformLocation("u_phi");
    m_psiUniform      = m_program->uniformLocation("u_psi");

    // Modi di visualizzazione
    m_projModeUniform   = m_program->uniformLocation("u_projMode");
    m_isFlatUniform     = m_program->uniformLocation("u_isFlat");
    m_renderModeUniform = m_program->uniformLocation("u_renderMode");

    // Texture Procedurali (Zoom/Pan)
    m_zoomUniform     = m_program->uniformLocation("u_zoom");
    m_centerUniform   = m_program->uniformLocation("u_center");
    m_rotationUniform = m_program->uniformLocation("u_rotation");
    m_col1Uniform     = m_program->uniformLocation("u_col1");
    m_col2Uniform     = m_program->uniformLocation("u_col2");

    // Shadertoy Uniforms (Gestite sempre, anche se non usate)
    m_iTimeUniform       = m_program->uniformLocation("iTime");
    m_iResolutionUniform = m_program->uniformLocation("iResolution");
    m_iMouseUniform      = m_program->uniformLocation("iMouse");
}

void GLWidget::createDummyTexture() {
    glGenTextures(1, &m_dummyTex);
    glBindTexture(GL_TEXTURE_2D, m_dummyTex);

    unsigned char blackPixel[] = { 0, 0, 0, 255 };

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blackPixel);

    // Parametri minimi per farla funzionare
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);
}

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
