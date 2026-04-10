#include "glwidget.h"
#include "geometrybuilder.h"
#include "inputhandler.h"
#include "surfaceengine.h"
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
#include <QCoreApplication>
#include <QPainter>
#include <QLinearGradient>
#include <cstddef>
#include <cstring>
#include <rhi/qrhi.h>

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
    : QRhiWidget(parent)
{
    m_uboData = UboData();

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
    m_surfaceTimeOffset = 0.00001f;

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
    // Se la pipeline esiste già, non dobbiamo reinizializzare nulla
    if (m_pipelineOpaque) {
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

void GLWidget::render(QRhiCommandBuffer *cb)
{
    if (!m_pipelineOpaque) {
        buildPipeline();
    }

    QRhiResourceUpdateBatch *resourceUpdates = rhi()->nextResourceUpdateBatch();

    if (m_surfaceTextureNeedsUpload && !m_pendingSurfaceImage.isNull()) {

        // 1. Distruggiamo la vecchia texture se esisteva
        if (m_surfaceTexture) {
            m_surfaceTexture->destroy();
            delete m_surfaceTexture;
        }

        // 2. Creiamo il nuovo oggetto Texture in RHI
        m_surfaceTexture = rhi()->newTexture(QRhiTexture::RGBA8, m_pendingSurfaceImage.size(), 1);
        m_surfaceTexture->create();

        // 3. Istruiamo RHI per copiare i byte dell'immagine nella Texture VRAM
        QRhiTextureSubresourceUploadDescription subresDesc(m_pendingSurfaceImage.constBits(), m_pendingSurfaceImage.sizeInBytes());
        QRhiTextureUploadEntry entry(0, 0, subresDesc);
        QRhiTextureUploadDescription uploadDesc({ entry });
        resourceUpdates->uploadTexture(m_surfaceTexture, uploadDesc);

        // 4. AGGIORNAMENTO DEL BINDING:
        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_surfaceTexture, m_sampler)
        });
        m_bindings->create(); // Ricrea la struttura interna dei binding

        // Abbassiamo la bandierina
        m_surfaceTextureNeedsUpload = false;
    }

    // --- 1. CARICAMENTO GEOMETRIA (VBO / IBO) ---
    if (meshNeedsUpdate) {
        const auto& vertices = engine->getVertices();
        const auto& indices = engine->getIndices();

        if (vertices.empty() || indices.empty()) {
            qWarning() << "ATTENZIONE: Stai inviando una mesh vuota alla GPU!";
        } else {
            int vSize = vertices.size() * sizeof(Vertex);
            int iSize = indices.size() * sizeof(unsigned int);

            // AUTO-RESIZE DEL VBO
            if (m_vbo->size() < vSize) {
                m_vbo->destroy();
                delete m_vbo;
                m_vbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5); // +50% di margine
                m_vbo->create();
            }

            // AUTO-RESIZE DELL'IBO
            if (m_ibo->size() < iSize) {
                m_ibo->destroy();
                delete m_ibo;
                m_ibo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                m_ibo->create();
            }

            resourceUpdates->updateDynamicBuffer(m_vbo, 0, vSize, vertices.data());
            resourceUpdates->updateDynamicBuffer(m_ibo, 0, iSize, indices.data());

            m_indexCount = indices.size();
        }
        meshNeedsUpdate = false;
    }

    // +++ BLOCCO BORDI 1: Caricamento Vertici +++
    if (borderNeedsUpdate && m_borderVertexCount > 0) {
        int vSize = m_borderVertexCount * sizeof(Vertex);

        // AUTO-RESIZE DEL BORDER VBO
        if (m_borderVbo->size() < vSize) {
            m_borderVbo->destroy();
            delete m_borderVbo;
            m_borderVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5);
            m_borderVbo->create();
        }

        resourceUpdates->updateDynamicBuffer(m_borderVbo, 0, vSize, m_borderVertices.data());
        borderNeedsUpdate = false;
    }
    // ++++++++++++++++++++++++++++++++++++++++++++

    // --- 2. CALCOLO MATRICI E TELECAMERA ---
    QSize outputSize = renderTarget()->pixelSize();
    float aspect = (float)outputSize.width() / (float)(outputSize.height() > 0 ? outputSize.height() : 1);

    QMatrix4x4 mvp;
    QMatrix4x4 mv;

    if (m_isFlatView) {
        // === MODALITÀ 2D PIATTA ===
        // Neutralizziamo le matrici 3D (Camera, Prospettiva, Rotazione).
        // Il quadrato così riempirà esattamente lo schermo intero (da -1 a +1).
        m_projection.setToIdentity();
        m_view.setToIdentity();
        m_model.setToIdentity();

        // rhi()->clipSpaceCorrMatrix() è vitale per allineare le API Vulkan/OpenGL
        mvp = rhi()->clipSpaceCorrMatrix();
        mv.setToIdentity();
    } else {
        // === MODALITÀ 3D NORMALE ===
        m_projection.setToIdentity();
        m_projection.perspective(45.0f, aspect, 0.01f, 100.0f);

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
            QVector3D up = rollMat * QVector3D(0.0f, 1.0f, 0.0f);

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

    float currentTime = m_manualTime + m_surfaceTimeOffset;
    if (m_surfaceAnimating) {
        currentTime += (float)m_surfaceTimer.elapsed() / 1000.0f;
    }

    m_uboData.time = currentTime;
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

    if (m_textureEnabled) {
        m_uboData.color = QVector3D(1.0f, 1.0f, 1.0f);
    } else {
        m_uboData.color = QVector3D(red, green, blue);
    }

    m_uboData.alpha = alpha;
    m_uboData.lightIntensity = m_lightIntensity;

    m_uboData.col1 = QVector3D(texRed1, texGreen1, texBlue1);
    m_uboData.col2 = QVector3D(texRed2, texGreen2, texBlue2);
    m_uboData.useTexture = m_textureEnabled ? 1 : 0;

    // Aggiornamento Buffer Principale
    resourceUpdates->updateDynamicBuffer(m_ubo, 0, sizeof(UboData), &m_uboData);

    // +++ BLOCCO BORDI 2: UBO Separato per il colore dei bordi +++
    if (showBorders && m_borderUbo) {
        UboData borderUboData = m_uboData;
        borderUboData.color = QVector3D(bordRed, bordGreen, bordBlue); // Usa i colori del bordo
        borderUboData.useTexture = 0;                                  // Niente texture sulle linee
        borderUboData.useSpecular = 0;                                 // Niente luce speculare

        resourceUpdates->updateDynamicBuffer(m_borderUbo, 0, sizeof(UboData), &borderUboData);
    }
    // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    if (wireframeNeedsUpdate && !m_wireframeIndices.empty()) {
        int iSize = m_wireframeIndexCount * sizeof(unsigned int);

        // AUTO-RESIZE DEL WIREFRAME IBO
        if (m_wireframeIbo->size() < iSize) {
            m_wireframeIbo->destroy();
            delete m_wireframeIbo;
            m_wireframeIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
            m_wireframeIbo->create();
        }

        resourceUpdates->updateDynamicBuffer(m_wireframeIbo, 0, iSize, m_wireframeIndices.data());
        wireframeNeedsUpdate = false;
    }

    // --- 4. RENDER PASS (Disegno effettivo) ---
    QColor clearColor = QColor::fromRgbF(m_bgColor.x(), m_bgColor.y(), m_bgColor.z());

    // +++ CARICAMENTO VRAM TEXTURE BACKGROUND +++
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

        // Collega l'immagine allo shader del background
        m_bgBindings->setBindings({
            // CORREZIONE: Usa m_bgUbo invece di m_ubo!
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_bgUbo),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_backgroundTexture, m_sampler)
        });
        m_bgBindings->create();

        m_backgroundTextureNeedsUpload = false;
    }

    // +++ CARICAMENTO DATI UBO E VBO PER QUADRATO 2D +++
    if (m_useBackgroundTexture || m_isFlatView) {
        if (!m_bgBindings) initBackgroundShader();

        // Aggiorna UBO Sfondo
        if (m_bgUbo) {
            UboData bgUboData = m_uboData;
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

        // Prepariamo 6 vertici per formare due triangoli completi (TriangleList)
        if (!m_bgVboUploaded && m_bgVbo) {
            Vertex quadVertices[6] = {
                { QVector3D(-1.0f, -1.0f, 0.99f), QVector4D(), QVector2D(0.0f, 1.0f) }, // Bottom-Left
                { QVector3D( 1.0f, -1.0f, 0.99f), QVector4D(), QVector2D(1.0f, 1.0f) }, // Bottom-Right
                { QVector3D(-1.0f,  1.0f, 0.99f), QVector4D(), QVector2D(0.0f, 0.0f) }, // Top-Left

                { QVector3D(-1.0f,  1.0f, 0.99f), QVector4D(), QVector2D(0.0f, 0.0f) }, // Top-Left
                { QVector3D( 1.0f, -1.0f, 0.99f), QVector4D(), QVector2D(1.0f, 1.0f) }, // Bottom-Right
                { QVector3D( 1.0f,  1.0f, 0.99f), QVector4D(), QVector2D(1.0f, 0.0f) }  // Top-Right
            };
            resourceUpdates->updateDynamicBuffer(m_bgVbo, 0, sizeof(quadVertices), quadVertices);
            m_bgVboUploaded = true;
        }
    }

    cb->beginPass(renderTarget(), clearColor, { 1.0f, 0 }, resourceUpdates);

    if (m_isFlatView) {
        // ==========================================
        // MODALITÀ 2D PURA: NIENTE MESH 3D
        // ==========================================
        if (m_flatViewTarget == 1 && m_bgPipeline && m_bgVbo) {
            // Disegna SOLO lo Sfondo 2D
            cb->setGraphicsPipeline(m_bgPipeline);
            cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
            cb->setShaderResources(m_bgBindings);
            const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(6);
        } else if (m_pipelineOpaque && m_bgVbo) {
            // Disegna SOLO la Superficie 2D
            cb->setGraphicsPipeline(m_pipelineOpaque);
            cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
            cb->setShaderResources(m_bindings);

            // Usiamo il quadrato piatto (m_bgVbo) ma con la pipeline e texture della superficie
            const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(6);
        }
    } else {
        // ==========================================
        // MODALITÀ 3D: SFONDO + MODELLO 3D
        // ==========================================

        // 1. Disegna il quadrato di Sfondo (Dietro a tutto)
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

        // 2. Disegna la Mesh 3D Principale
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
            if (m_indexCount > 0) {
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                const QRhiCommandBuffer::VertexInput vbufBinding(m_vbo, 0);

                if (alpha < 0.99f) {
                    // Modalità Trasparente
                    cb->setGraphicsPipeline(m_pipelineTranspBack);
                    cb->setShaderResources(m_bindings);
                    cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(m_indexCount);

                    cb->setGraphicsPipeline(m_pipelineTranspFront);
                    cb->setShaderResources(m_bindings);
                    cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(m_indexCount);
                } else {
                    // Modalità Opaca Standard
                    cb->setGraphicsPipeline(m_pipelineOpaque);
                    cb->setShaderResources(m_bindings);
                    cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(m_indexCount);
                }
            }
        }

        // 3. Disegna i Bordi 3D
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

    // In RHI non serve controllare isValid() o usare makeCurrent()!
    // Chiamiamo semplicemente la nostra funzione che segnala di distruggere la pipeline.
    bool success = rebuildShader();

    // 3. Se fallisce davvero (per un errore dell'utente), ripristina e blocca
    if (!success) {
        m_eqX = oldX; m_eqY = oldY; m_eqZ = oldZ; m_eqW = oldW;
        engine->setEquations(oldX, oldY, oldZ, oldW);
        return false;
    }

    // 4. Tutto ok: la pipeline verrà ricreata al prossimo frame.
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
    // Salviamo nel pacchetto UBO!
    m_uboData.mathParams = QVector4D(a, b, c, s);
    m_uboData.mathParams2 = QVector4D(d, e, f, 0.0f);
    meshNeedsUpdate = true;
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

void GLWidget::setResolution(int n) {

    engine->setResolution(n, n);
    meshNeedsUpdate = true;
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

bool GLWidget::rebuildShader()
{
    // --- VERSIONE RHI ---
    // Distruggendo la pipeline, diciamo a RHI di ricrearla
    // (con le nuove equazioni) al prossimo frame in initialize()
    if (m_pipelineOpaque) {
        delete m_pipelineOpaque;
        m_pipelineOpaque = nullptr;
    }

    meshNeedsUpdate = true;
    update();

    return true;
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    m_pendingSurfaceImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));
#else
    m_pendingSurfaceImage = img.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
#endif

    // 2. Alziamo la bandierina per il Render Loop
    m_surfaceTextureNeedsUpload = true;
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
    // Genera la scacchiera e la carica in RHI
    loadTextureFromImage(generateCheckerboard());
    update();
}

void GLWidget::clearTexture() {
    // Svuota l'immagine in attesa
    m_pendingSurfaceImage = QImage();

    // Al prossimo frame tornerà tutto nero/colore base se disattiviamo m_textureEnabled
    setTextureEnabled(false);
    update();
}

void GLWidget::setScriptCheck(bool enabled) {
    engine->setScriptMode(enabled);
    meshNeedsUpdate = true;
}

void GLWidget::loadCustomShader(const QString &customCode)
{
    m_customFragmentCode = customCode; // Memorizza il codice
    rebuildShader(); // Dice a RHI di ricompilare usando la nuova stringa
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    m_pendingBackgroundImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));
#else
    m_pendingBackgroundImage = img.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
#endif

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
        updateSurfaceData();
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

QImage GLWidget::getFrameForVideo(int targetW, int targetH, bool useFbo) {
    if (meshNeedsUpdate) updateSurfaceData();

    // 1. Forziamo il sistema a "disegnare" fisicamente il nuovo fotogramma
    // prima di scattare la foto, mettendo in pausa il ciclo per un millisecondo.
    this->repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // 2. Catturiamo il widget aggiornato così come appare sullo schermo
    QImage img = this->grab().toImage();

    // Se la cattura è fallita, restituiamo un'immagine nera come fallback anti-crash
    if (img.isNull()) {
        img = QImage(targetW > 0 ? targetW : 1920,
                     targetH > 0 ? targetH : 1080,
                     QImage::Format_RGBA8888);
        img.fill(Qt::black);
        return img;
    }

    // 3. Upscaling se le dimensioni richieste non coincidono con quelle dello schermo
    if (targetW > 0 && targetH > 0 && (img.width() != targetW || img.height() != targetH)) {
        img = img.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    return img;
}


// ==========================================================
// PRIVATE HELPER METHODS
// ==========================================================

void GLWidget::buildBorderGeometry() {
    // 1. Generiamo i dati grezzi sulla CPU
    std::vector<QVector3D> data = GeometryBuilder::buildBorders(engine.get());

    m_borderVertices.clear();
    m_borderVertices.reserve(data.size());

    // 2. Li convertiamo nel formato Vertex compatibile con l'input layout di RHI
    for (const QVector3D& pos : data) {
        Vertex v;
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

    // UBO Block sincronizzato matematicamente col C++ e con surface.frag
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
                       "    float v_max; int u_hasExplicitW; vec2 padding;\n"
                       "} ubuf;\n";

    QString fsSource;
    if (isTextureMode) {
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
        if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) helpers += "    vec3 u_col1 = ubuf.u_col1;\n";
        if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) helpers += "    vec3 u_col2 = ubuf.u_col2;\n";

        QString dynamicBody;

        if (safeCode.contains("mainImage")) {
            // FIX: Vere variabili globali invece di macro!
            QString stHelpers = "vec3 iResolution;\n"
                                "float iTime;\n"
                                "float iTimeDelta;\n"
                                "int iFrame;\n"
                                "vec4 iMouse;\n"
                                "vec4 iDate;\n"
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

            dynamicBody = stHelpers + safeCode + "\n"
                                                 "vec3 getCustomColor(vec2 in_uv) {\n"
                          + helpers + initVars +
                          "    vec4 fragColor_out;\n"
                          "    mainImage(fragColor_out, uv * iResolution.xy);\n"
                          "    return fragColor_out.rgb;\n"
                          "}\n"
                          "void main() {\n"
                          "  out_FragColor = vec4(getCustomColor(v_texCoord), ubuf.alpha);\n"
                          "}\n";
        }
        else if (safeCode.contains("void main()")) {
            QString extHelpers = "#define iResolution vec3(1.0, 1.0, 1.0)\n#define iTime ubuf.u_time\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";
            dynamicBody = extHelpers + safeCode + "\n";
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

    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    m_bgPipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_bgPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
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

QString GLWidget::createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq)
{
    // 1. Carica il template
    QString vertexTemplate = loadShaderSource(":/shaders/surface.vert");
    QString commonCode = loadShaderSource(":/shaders/common.glsl");

    // 2. RIMUOVE QUALSIASI INTESTAZIONE ESISTENTE
    QRegularExpression headerCleanup("^\\s*(#version|precision).*\n", QRegularExpression::MultilineOption);
    vertexTemplate.remove(headerCleanup);
    vertexTemplate.remove(QRegularExpression("^\\s*#ifdef GL_ES[\\s\\S]*?#endif", QRegularExpression::MultilineOption));

    QString header = "#version 450\n";

    QString safePowDef = R"(
    float safe_pow(float x, float y) {
        return sign(x) * pow(abs(x), y);
    }
    )";

    QString source = header + "\n" + safePowDef + "\n" + commonCode + "\n" + vertexTemplate;

    auto sanitizeEq = [](const QString &s) {
        // 1. Traduce u^2 in safe_pow(u, 2.0) e pi in 3.1415
        QString translated = GlslTranslator::translateEquation(s);

        // 2. FIX RHI: Il traduttore legacy converte 't' in 'u_time'.
        // In RHI dobbiamo reindirizzarlo obbligatoriamente al blocco UBO!
        translated.replace(QRegularExpression("\\bu_time\\b"), "ubuf.u_time");

        // Manteniamo anche il replace di 't' nel caso il traduttore non lo faccia
        translated.replace(QRegularExpression("\\bt\\b"), "ubuf.u_time");

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

        QString injectedVars = "    float t = ubuf.u_time;\n"
                               "    float A = ubuf.u_mathParams.x;\n"
                               "    float B = ubuf.u_mathParams.y;\n"
                               "    float C = ubuf.u_mathParams.z;\n"
                               "    float s = ubuf.u_mathParams.w;\n"
                               "    float S = ubuf.u_mathParams.w;\n"
                               "    float D = ubuf.u_mathParams2.x;\n"
                               "    float E = ubuf.u_mathParams2.y;\n"
                               "    float F = ubuf.u_mathParams2.z;\n";

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

    QString params = "float A=ubuf.u_mathParams.x; "
                     "float B=ubuf.u_mathParams.y; "
                     "float C=ubuf.u_mathParams.z; "
                     "float _valS=ubuf.u_mathParams.w; "
                     "float D=ubuf.u_mathParams2.x; "
                     "float E=ubuf.u_mathParams2.y; "
                     "float F=ubuf.u_mathParams2.z; "
                     "float s=_valS; "
                     "float S=_valS; "
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

    return source;
}
QString GLWidget::createFragmentShaderSource(const QString &customLogic)
{
    QString fragmentTemplate = loadShaderSource(":/shaders/surface.frag");
    QString commonCode = loadShaderSource(":/shaders/common.glsl");

    QRegularExpression versionRegex("^\\s*#version\\s+[0-9]+(\\s+es|\\s+core)?\\s*\n?");
    fragmentTemplate.remove(versionRegex);

    QString header = "#version 450\n";

    QString safePowLogic = R"(
    float safe_pow(float x, float y) {
        return sign(x) * pow(abs(x), y);
    }
    )";

    QString fullSource = header + "\n" + safePowLogic + "\n" + commonCode + "\n" + fragmentTemplate;

    QString safeLogic = customLogic;
    safeLogic.remove(QRegularExpression("//SOUND_BEGIN.*?//SOUND_END", QRegularExpression::DotMatchesEverythingOption));
    safeLogic.remove(QRegularExpression("#ifdef GL_ES[\\s\\S]*?#endif"));
    safeLogic.remove(QRegularExpression("precision\\s+(highp|mediump|lowp)\\s+float\\s*;"));
    safeLogic.replace("gl_FragColor", "fragColor");
    safeLogic.replace(QRegularExpression("\\bu_time\\b"), "ubuf.u_time");

    QString helpers = "    float _rad = radians(ubuf.u_rotation);\n"
                      "    float _c = cos(_rad); float _s = sin(_rad);\n"
                      "    vec2 _centered = in_uv - 0.5;\n"
                      "    vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;\n"
                      "    float _scale = 1.0 / ubuf.u_zoom;\n"
                      "    vec2 _shift = ubuf.u_center * 0.5;\n"
                      "    vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;\n"
                      "    bool u_isFlat = (ubuf.u_isFlat != 0);\n";

    if (!safeLogic.contains(QRegularExpression("\\bfloat\\s+t\\b"))) helpers += "    float t = ubuf.u_time;\n";
    if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) helpers += "    vec3 u_col1 = ubuf.u_col1;\n";
    if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) helpers += "    vec3 u_col2 = ubuf.u_col2;\n";

    QString codeToInject;

    // DICHIARAZIONE FONDAMENTALE DELLA TEXTURE PER RHI
    QString samplerDecl = "layout(binding=1) uniform sampler2D tex;\n";

    if (customLogic.isEmpty()) {
        codeToInject = samplerDecl +
                       "vec3 getCustomColor(vec2 in_uv) {\n" +
                       helpers +
                       "\n    return texture(tex, uv).rgb;\n}"; // <--- Modificato in tex
    }
    else if (safeLogic.contains("mainImage")) {
        // FIX: Vere variabili globali
        QString stHelpers = "vec3 iResolution;\n"
                            "float iTime;\n"
                            "float iTimeDelta;\n"
                            "int iFrame;\n"
                            "vec4 iMouse;\n"
                            "vec4 iDate;\n"
                            "#define iChannel0 tex\n"
                            "#define iChannel1 tex\n"
                            "#define iChannel2 tex\n"
                            "#define iChannel3 tex\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) stHelpers += "vec3 u_col1;\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) stHelpers += "vec3 u_col2;\n";

        QString initVars = "    iResolution = vec3(1024.0, 1024.0, 1.0);\n"
                           "    iTime = ubuf.u_time;\n"
                           "    iTimeDelta = 0.016;\n"
                           "    iFrame = int(ubuf.u_time * 60.0);\n"
                           "    iMouse = vec4(0.0);\n"
                           "    iDate = vec4(0.0);\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) initVars += "    u_col1 = ubuf.u_col1;\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) initVars += "    u_col2 = ubuf.u_col2;\n";

        codeToInject = samplerDecl + stHelpers + safeLogic + "\n"
                                                             "vec3 getCustomColor(vec2 in_uv) {\n"
                       + helpers + initVars +
                       "    vec4 fragColor_out;\n"
                       "    mainImage(fragColor_out, uv * iResolution.xy);\n"
                       "    return fragColor_out.rgb;\n"
                       "}\n";
    }
    else if (safeLogic.contains("getCustomColor")) {
        QString extHelpers;
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

    // Queste due righe puoi anche cancellarle, appartenevano a un vecchio surface.frag
    // e ora non servono più, ma lasciarle non fa danni se le hai ancora.
    fullSource.replace("vec4 tex = texture(textureSampler, v_texCoord);",
                       "vec4 tex = vec4(getCustomColor(v_texCoord), 1.0);");
    fullSource.replace("vec3 texColor = texture(textureSampler, v_texCoord).rgb;",
                       "vec3 texColor = getCustomColor(v_texCoord);");

    return fullSource;
}

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

void GLWidget::buildPipeline() {
    // Pulisce preventivamente se esiste già
    if (m_pipelineOpaque) {
        delete m_pipelineOpaque;
        m_pipelineOpaque = nullptr;
    }

    m_pipelineOpaque = rhi()->newGraphicsPipeline();

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal) },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });
    m_pipelineOpaque->setVertexInputLayout(inputLayout);

    QString vsSource = createVertexShaderSource(m_eqX, m_eqY, m_eqZ, m_eqW);
    QString fsSource = createFragmentShaderSource(m_customFragmentCode);

    qDebug() << "\n=== SURFACE FRAGMENT SHADER ===";
    qDebug().noquote() << fsSource;

    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);

    qDebug() << "\n=== SURFACE FRAGMENT SHADER ===";
    qDebug().noquote() << fsSource;

    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    m_pipelineOpaque->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs }
    });

    // --- 1. SETUP COMUNE (Depth, Blend, ecc.) ---
    m_pipelineOpaque->setSampleCount(renderTarget()->sampleCount());
    m_pipelineOpaque->setDepthTest(true);

    // 1. SCRITTURA PROFONDITA' ATTIVA (Nasconde i bordi dietro e risolve gli incroci)
    m_pipelineOpaque->setDepthWrite(true);

    m_pipelineOpaque->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);

    // 2. NESSUN CULLING
    m_pipelineOpaque->setCullMode(QRhiGraphicsPipeline::None);

    // Preparazione del Blend (uguale per tutti, anche se per l'opaco non servirà)
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
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
    m_pipelineOpaque->create();

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
    m_pipelineTranspBack->create();

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
    m_pipelineTranspFront->create();

    // --- PIPELINE WIREFRAME ---
    if (m_wireframePipeline) {
        delete m_wireframePipeline;
        m_wireframePipeline = nullptr;
    }
    m_wireframePipeline = rhi()->newGraphicsPipeline();
    m_wireframePipeline->setTopology(QRhiGraphicsPipeline::Lines); // <-- LA MAGIA È QUI

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

    m_wireframePipeline->create();

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
    m_borderPipeline->setTargetBlends({ blend });
    m_borderPipeline->setShaderResourceBindings(m_borderBindings); // <-- Binding esclusivo
    m_borderPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    m_borderPipeline->create();
}

QImage GLWidget::generateCheckerboard() {
    int s = 512;
    QImage img(s, s, QImage::Format_RGBA8888);
    int step = 16;
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            if (((x / step) + (y / step)) % 2 == 0) {
                img.setPixelColor(x, y, QColor(0, 255, 0)); // Verde
            } else {
                img.setPixelColor(x, y, Qt::black); // Nero
            }
        }
    }
    return img;
}
