#include "synthesizer.h"
#include <QMediaDevices>
#include <QDebug>
#include <QFile>
#include <rhi/qshaderbaker.h>
#include <QMutexLocker>
#include <QColor>
#include <QSize>
#include <QCoreApplication>
#include <rhi/qshader.h>
#include <cstring>

Synthesizer::Synthesizer(QObject *parent)
    : QIODevice(parent), m_audioSink(nullptr), m_isScriptValid(false), m_rhi(nullptr)
{
    m_sampleRate = 44100;
    m_chunkSize = 4096;
    m_currentSample = 0;

    m_format.setSampleRate(m_sampleRate);
    m_format.setChannelCount(2);
    m_format.setSampleFormat(QAudioFormat::Float);

    m_renderTimer = new QTimer(this);
    connect(m_renderTimer, &QTimer::timeout, this, &Synthesizer::renderAudioChunk);

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        stop();
        if (m_rhi) {
            cleanupRhiResources();
            m_rhi = nullptr; // Segnala al distruttore che le risorse sono già salve
        }
    });
}

Synthesizer::~Synthesizer() {
    stop();
    if (m_rhi) {
        cleanupRhiResources();
    }
}

// --- GESTIONE RHI ---

void Synthesizer::setRhi(QRhi *rhi) {
    if (m_rhi == rhi) return;
    m_rhi = rhi;
    if (m_rhi) {
        initializeRhiResources();
    }
}

void Synthesizer::initializeRhiResources() {
    if (!m_rhi) return;

    cleanupRhiResources();

    // 1. Texture RGBA Float per i campioni audio
    m_texture = m_rhi->newTexture(QRhiTexture::RGBA32F, QSize(m_chunkSize, 1), 1,
                                  QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    m_texture->create();

    // 2. Render Target Offscreen
    m_renderTarget = m_rhi->newTextureRenderTarget({ m_texture });
    m_renderPassDesc = m_renderTarget->newCompatibleRenderPassDescriptor();
    m_renderTarget->setRenderPassDescriptor(m_renderPassDesc);
    m_renderTarget->create();

    // 3. UBO per Tempo e SampleRate
    int uboSize = sizeof(AudioUboData);
    if (uboSize < 16) uboSize = 16;
    m_ubo = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uboSize);
    m_ubo->create();

    // 4. Bindings
    m_bindings = m_rhi->newShaderResourceBindings();
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_ubo)
    });
    m_bindings->create();
}

void Synthesizer::cleanupRhiResources() {
    if (m_pipeline) { delete m_pipeline; m_pipeline = nullptr; }
    if (m_bindings) { delete m_bindings; m_bindings = nullptr; }
    if (m_ubo) { delete m_ubo; m_ubo = nullptr; }
    if (m_renderTarget) { delete m_renderTarget; m_renderTarget = nullptr; }
    if (m_renderPassDesc) { delete m_renderPassDesc; m_renderPassDesc = nullptr; }
    if (m_texture) { delete m_texture; m_texture = nullptr; }
}

bool Synthesizer::updateScript(const QString &glslCode, bool isSimpleMath) {
    if (!m_rhi) {
        qWarning() << "Errore: RHI non impostato nel Synthesizer!";
        return false;
    }

    m_isScriptValid = false;
    m_lastError.clear();

    // --- 1. CARICHIAMO LA LIBRERIA COMUNE ---
    QString commonCode = "";
    QFile file(":/shaders/common.glsl");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        commonCode = file.readAll();
        file.close();
    }

    // Fallback: se common.glsl non si carica, forniamo le costanti base
    // (PI/pi/TAU/tau/e) che common.glsl avrebbe normalmente fornito,
    // così il fragment shader rimane comunque compilabile.
    QString fallbackConsts;
    if (commonCode.trimmed().isEmpty()) {
        fallbackConsts = R"(
        const float PI  = 3.14159265359;
        const float pi  = 3.14159265359;
        const float TAU = 6.28318530718;
        const float tau = 6.28318530718;
        const float e   = 2.71828182846;
        )";
    }

    // --- 2. INTESTAZIONE DINAMICA ---
    QString header = "#version 450\n";

    // --- 3. COSTRUIAMO IL FRAGMENT SHADER AUDIO ---
    QString fsSrc = header + R"(
    layout(std140, binding = 0) uniform AudioUBO {
        int val_startSample;     // Nome univoco per la struttura
        float val_sampleRate;    // Nome univoco per la struttura
        vec2 padding;
    } ubuf;

    // Esposizione per gli script esterni (es. Cross-Galactic Ocean)
    #define u_startSample ubuf.val_startSample
    #define u_sampleRate  ubuf.val_sampleRate

    layout(location = 0) out vec4 fragColor;
    )" + commonCode + "\n" + fallbackConsts + R"(

    // Costante TWO_PI (non presente in common.glsl, ma usata da alcuni script audio)
    const float TWO_PI = 6.28318530717958647692;

    )" + glslCode + R"(

    void main() {
        // Calcolo esatto del tempo (usando i nomi univoci, niente macro loop!)
        int offset = int(gl_FragCoord.x);
        int samp = ubuf.val_startSample + offset;
        float t = float(samp) / ubuf.val_sampleRate;

        // Retrocompatibilità
        float u_time = t;

        // Esegue la funzione custom
        vec2 audio = mainSound(samp, t);

        // Filtro Anti-Distorsione (Protegge dai crash del buffer dovuti a NaN)
        if (isnan(audio.x) || isinf(audio.x)) audio.x = 0.0;
        if (isnan(audio.y) || isinf(audio.y)) audio.y = 0.0;

        fragColor = vec4(clamp(audio, -1.0, 1.0), 0.0, 1.0);
    })";

    // --- 4. COSTRUIAMO IL VERTEX SHADER AUDIO ---
    QString vsSrc = header + R"(
    void main() {
        float x = -1.0 + float((gl_VertexIndex & 1) << 2);
        float y = -1.0 + float((gl_VertexIndex & 2) << 1);
        gl_Position = vec4(x, y, 0.0, 1.0);
    })";

    auto bakeShader = [this](const QByteArray &source, QShader::Stage stage) -> QShader {
        QShaderBaker baker;
        baker.setSourceString(source, stage);
        baker.setGeneratedShaderVariants({QShader::StandardShader});

        QList<QShaderBaker::GeneratedShader> targets;
        targets.append({QShader::SpirvShader, QShaderVersion(100)});
        targets.append({QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs)});
        targets.append({QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs)});
        targets.append({QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs)});

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        targets.append({QShader::GlslShader, QShaderVersion(410)});
        targets.append({QShader::GlslShader, QShaderVersion(460)});
        targets.append({QShader::HlslShader, QShaderVersion(50)});
#endif
#if defined(Q_OS_APPLE)
        targets.append({QShader::MslShader, QShaderVersion(12)});
#endif

        baker.setGeneratedShaders(targets);

        QShader shader = baker.bake();
        if (!shader.isValid()) {
            qWarning() << "Audio shader compilation error:" << baker.errorMessage();
            if (m_lastError.isEmpty()) m_lastError = baker.errorMessage();
        }
        return shader;
    };

    QShader vs = bakeShader(vsSrc.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSrc.toUtf8(), QShader::FragmentStage);

    if (!vs.isValid() || !fs.isValid()) {
        qWarning() << "Errore compilazione shader audio";
        return false;
    }

    if (m_pipeline) delete m_pipeline;
    m_pipeline = m_rhi->newGraphicsPipeline();

    m_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    m_pipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipeline->setRenderPassDescriptor(m_renderPassDesc);
    m_pipeline->setShaderResourceBindings(m_bindings);
    m_pipeline->create();

    m_isScriptValid = true;
    return true;
}

void Synthesizer::renderAudioChunk() {
    if (!m_isScriptValid || !m_rhi || !m_pipeline) return;

    // Genera chunk finché il buffer non è pieno
    while (true) {
        m_bufferMutex.lock();
        int currentBytes = m_audioBuffer.size();
        m_bufferMutex.unlock();

        // Se il buffer ha più di 2 secondi, fermati e aspetta il prossimo tick
        if (currentBytes > m_sampleRate * sizeof(float) * 2 * 2) break;

        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) break;

        QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();

        // 1. Carica il campione esatto (nessuna perdita di precisione!)
        m_uboData.startSample = m_currentSample;
        m_uboData.sampleRate = (float)m_sampleRate;
        rub->updateDynamicBuffer(m_ubo, 0, sizeof(AudioUboData), &m_uboData);

        // 2. Passata di Rendering
        cb->beginPass(m_renderTarget, Qt::black, { 1.0f, 0 }, rub);
        cb->setGraphicsPipeline(m_pipeline);
        cb->setViewport(QRhiViewport(0, 0, m_chunkSize, 1));
        cb->setShaderResources(m_bindings);
        cb->draw(3);
        cb->endPass();

        // 3. Estrazione Pixel
        QRhiReadbackResult readResult;
        QRhiResourceUpdateBatch *readRub = m_rhi->nextResourceUpdateBatch();
        QRhiReadbackDescription readDesc(m_texture);
        readRub->readBackTexture(readDesc, &readResult);
        cb->resourceUpdate(readRub);

        m_rhi->endOffscreenFrame();
        m_rhi->finish(); // Sincronizza

        if (!readResult.data.isEmpty()) {
            QByteArray newSamples;
            newSamples.resize(m_chunkSize * 2 * sizeof(float));
            float *outData = reinterpret_cast<float*>(newSamples.data());

            const float *pixels = reinterpret_cast<const float*>(readResult.data.constData());

            for (int i = 0; i < m_chunkSize; ++i) {
                outData[i*2]     = pixels[i*4];     // Left
                outData[i*2 + 1] = pixels[i*4 + 1]; // Right
            }

            m_bufferMutex.lock();
            m_audioBuffer.append(newSamples);
            m_bufferMutex.unlock();

            // Aggiorna usando un intero assoluto, zero difetti di arrotondamento
            m_currentSample += m_chunkSize;
        }
    }
}

// --- FUNZIONI STANDARD (Avvio, Stop, Lettura) ---

void Synthesizer::start() {
    if (!m_isScriptValid) return;
    open(QIODevice::ReadOnly);
    m_currentSample = 0;
    m_audioBuffer.clear();
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_format, this);
    m_audioSink->start(this);
    m_renderTimer->start(10);
}

void Synthesizer::stop() {
    m_renderTimer->stop();
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    close();
}

bool Synthesizer::saveToRawFile(const QString &filename, int durationSeconds) {
    if (!m_isScriptValid || !m_rhi || !m_pipeline) {
        qWarning() << "Synthesizer non pronto per il render offline.";
        return false;
    }

    QFile outFile(filename);
    if (!outFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Impossibile aprire il file per il salvataggio audio RAW:" << filename;
        return false;
    }

    int totalSamplesToRender = durationSeconds * m_sampleRate;
    int offlineSampleOffset = 0; // Usiamo un contatore separato per non rompere il tempo reale

    while (offlineSampleOffset < totalSamplesToRender) {
        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
            break;
        }

        QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();

        // 1. Carica il campione esatto
        m_uboData.startSample = offlineSampleOffset;
        m_uboData.sampleRate = (float)m_sampleRate;
        rub->updateDynamicBuffer(m_ubo, 0, sizeof(AudioUboData), &m_uboData);

        // 2. Render Pass
        cb->beginPass(m_renderTarget, Qt::black, { 1.0f, 0 }, rub);
        cb->setGraphicsPipeline(m_pipeline);
        cb->setViewport(QRhiViewport(0, 0, m_chunkSize, 1));
        cb->setShaderResources(m_bindings);
        cb->draw(3);
        cb->endPass();

        // 3. Estrazione Pixel
        QRhiReadbackResult readResult;
        QRhiResourceUpdateBatch *readRub = m_rhi->nextResourceUpdateBatch();
        QRhiReadbackDescription readDesc(m_texture);
        readRub->readBackTexture(readDesc, &readResult);
        cb->resourceUpdate(readRub);

        m_rhi->endOffscreenFrame();
        m_rhi->finish(); // Sincronizza lettura dalla GPU

        if (!readResult.data.isEmpty()) {
            // Calcola quanti campioni servono davvero (per non eccedere la durata)
            int samplesRemaining = totalSamplesToRender - offlineSampleOffset;
            int samplesToProcess = qMin(m_chunkSize, samplesRemaining);

            QByteArray rawChunk;
            rawChunk.resize(samplesToProcess * 2 * sizeof(float));
            float *outData = reinterpret_cast<float*>(rawChunk.data());

            const float *pixels = reinterpret_cast<const float*>(readResult.data.constData());

            for (int i = 0; i < samplesToProcess; ++i) {
                outData[i*2]     = pixels[i*4];     // Canale Sinistro (R)
                outData[i*2 + 1] = pixels[i*4 + 1]; // Canale Destro (G)
            }

            outFile.write(rawChunk);
            offlineSampleOffset += samplesToProcess;
        } else {
            qWarning() << "Errore nella lettura dei pixel dal RHI durante il bouncing audio.";
            break;
        }
    }

    outFile.close();
    return true;
}

qint64 Synthesizer::readData(char *data, qint64 maxlen) {
    QMutexLocker locker(&m_bufferMutex);
    qint64 bytesToRead = qMin(maxlen, (qint64)m_audioBuffer.size());
    if (bytesToRead > 0) {
        memcpy(data, m_audioBuffer.constData(), bytesToRead);
        m_audioBuffer.remove(0, bytesToRead);
    }
    return bytesToRead;
}

qint64 Synthesizer::writeData(const char *data, qint64 len) {
    return 0; // Sola lettura
}

qint64 Synthesizer::bytesAvailable() const {
    return m_audioBuffer.size() + QIODevice::bytesAvailable();
}
