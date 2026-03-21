#include "synthesizer.h"
#include <QMediaDevices>
#include <QRegularExpression>
#include <QDebug>
#include <QFile>
#include <QDataStream>

Synthesizer::Synthesizer(QObject *parent)
    : QIODevice(parent), m_audioSink(nullptr), m_context(nullptr),
    m_surface(nullptr), m_fbo(nullptr), m_shader(nullptr), m_vao(nullptr), m_isScriptValid(false)
{
    m_sampleRate = 44100;
    m_chunkSize = 4096;
    m_time = 0.0f;

    m_format.setSampleRate(m_sampleRate);
    m_format.setChannelCount(2);
    m_format.setSampleFormat(QAudioFormat::Float);

    m_renderTimer = new QTimer(this);
    connect(m_renderTimer, &QTimer::timeout, this, &Synthesizer::renderAudioChunk);

    initializeOpenGL();
}

Synthesizer::~Synthesizer() {
    stop();
    cleanupOpenGL();
}

void Synthesizer::initializeOpenGL() {
    m_context = new QOpenGLContext(this);

    // FIX 3: Richiediamo esplicitamente un contesto OpenGL 3.3 Core (obbligatorio per #version 330)
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    m_context->setFormat(format);
    m_context->create();

    m_surface = new QOffscreenSurface();
    m_surface->setFormat(m_context->format());
    m_surface->create();

    m_context->makeCurrent(m_surface);
    initializeOpenGLFunctions();

    // FIX 4: Creiamo e leghiamo il VAO obbligatorio
    m_vao = new QOpenGLVertexArrayObject();
    m_vao->create();

    QOpenGLFramebufferObjectFormat fboFormat;
    fboFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    fboFormat.setInternalTextureFormat(GL_RGBA32F);
    m_fbo = new QOpenGLFramebufferObject(m_chunkSize, 1, fboFormat);

    m_context->doneCurrent();
}

void Synthesizer::cleanupOpenGL() {
    if (m_context) {
        m_context->makeCurrent(m_surface);
        if (m_vao) { m_vao->destroy(); delete m_vao; m_vao = nullptr; }
        if (m_fbo) { delete m_fbo; m_fbo = nullptr; }
        if (m_shader) { delete m_shader; m_shader = nullptr; }
        m_context->doneCurrent();
        delete m_surface; m_surface = nullptr;
        delete m_context; m_context = nullptr;
    }
}

bool Synthesizer::updateScript(const QString &code, bool isSimpleMath) {
    if (code.trimmed().isEmpty()) return false;

    m_context->makeCurrent(m_surface);
    if (m_shader) { delete m_shader; m_shader = nullptr; }
    m_shader = new QOpenGLShaderProgram();

    const char* vsSrc = R"(
        #version 330 core
        void main() {
            float x = -1.0 + float((gl_VertexID & 1) << 2);
            float y = -1.0 + float((gl_VertexID & 2) << 1);
            gl_Position = vec4(x, y, 0.0, 1.0);
        }
    )";

    QString fsSrc = R"(
        #version 330 core
        out vec4 fragColor;
        uniform float u_time;
        uniform float u_sampleRate;

        #define PI 3.14159265359
        #define pi 3.14159265359
    )";

    if (isSimpleMath) {
        fsSrc += "vec2 mainSound(int samp, float t) { return vec2(" + code + "); }\n";
    } else {
        fsSrc += code + "\n";
    }

    fsSrc += R"(
        void main() {
            float offset = floor(gl_FragCoord.x);
            float t = u_time + (offset / u_sampleRate);
            int samp = int(t * u_sampleRate);

            vec2 audio = mainSound(samp, t);
            audio = clamp(audio, -1.0, 1.0);
            fragColor = vec4(audio.x, audio.y, 0.0, 1.0);
        }
    )";

    if (!m_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, vsSrc) ||
        !m_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, fsSrc) ||
        !m_shader->link()) {
        qDebug() << "Errore compilazione GLSL Audio:\n" << m_shader->log();
        m_isScriptValid = false;
        m_context->doneCurrent();
        return false;
    }

    m_isScriptValid = true;
    m_context->doneCurrent();
    return true;
}

void Synthesizer::start() {
    if (isOpen()) close();
    open(QIODevice::ReadOnly);

    m_time = 0.0f;
    m_audioBuffer.clear();

    renderAudioChunk(); renderAudioChunk();

    if (m_audioSink) { m_audioSink->stop(); delete m_audioSink; }
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_format, this);
    m_audioSink->setVolume(1.0);
    m_audioSink->start(this);

    m_renderTimer->start(50);
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

void Synthesizer::renderAudioChunk() {
    if (!m_isScriptValid || !m_context) return;

    m_bufferMutex.lock();
    int currentBytes = m_audioBuffer.size();
    m_bufferMutex.unlock();
    if (currentBytes > m_sampleRate * sizeof(float) * 2 * 2) { return; }

    m_context->makeCurrent(m_surface);
    m_fbo->bind();
    glViewport(0, 0, m_chunkSize, 1);

    m_shader->bind();
    m_shader->setUniformValue("u_time", m_time);
    m_shader->setUniformValue("u_sampleRate", (float)m_sampleRate);

    // FIX 5: Bindi-amo il VAO prima di disegnare! È lui l'"interruttore" che accende l'output.
    if (m_vao) m_vao->bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (m_vao) m_vao->release();

    std::vector<float> pixels(m_chunkSize * 4);
    glReadPixels(0, 0, m_chunkSize, 1, GL_RGBA, GL_FLOAT, pixels.data());

    m_fbo->release();
    m_context->doneCurrent();

    QByteArray newSamples;
    newSamples.resize(m_chunkSize * 2 * sizeof(float));
    float *outData = reinterpret_cast<float*>(newSamples.data());

    for (int i = 0; i < m_chunkSize; ++i) {
        outData[i*2]     = pixels[i*4];     // Left
        outData[i*2 + 1] = pixels[i*4 + 1]; // Right
    }

    m_bufferMutex.lock();
    m_audioBuffer.append(newSamples);
    m_bufferMutex.unlock();

    m_time += (float)m_chunkSize / (float)m_sampleRate;
}

qint64 Synthesizer::bytesAvailable() const {
    return m_audioSink ? (m_audioSink->bytesFree() + QIODevice::bytesAvailable()) : 0;
}

qint64 Synthesizer::readData(char *data, qint64 maxlen) {
    QMutexLocker locker(&m_bufferMutex);

    qint64 bytesToRead = qMin(maxlen, (qint64)m_audioBuffer.size());

    if (bytesToRead > 0) {
        memcpy(data, m_audioBuffer.constData(), bytesToRead);
        m_audioBuffer.remove(0, bytesToRead);
    } else {
        memset(data, 0, maxlen);
        bytesToRead = maxlen;
    }

    return bytesToRead;
}

qint64 Synthesizer::writeData(const char *data, qint64 len) {
    Q_UNUSED(data); Q_UNUSED(len);
    return 0;
}

bool Synthesizer::saveToRawFile(const QString &filename, int durationSeconds) {
    if (!m_isScriptValid || !m_context || durationSeconds <= 0) return false;

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) return false;

    m_context->makeCurrent(m_surface);
    m_fbo->bind();
    glViewport(0, 0, m_chunkSize, 1);
    m_shader->bind();

    float t = 0.0f;
    float timeIncrement = (float)m_chunkSize / (float)m_sampleRate;

    // Calcoliamo quanti cicli (chunk) servono per riempire tutta la durata del video
    int totalChunks = (durationSeconds * m_sampleRate) / m_chunkSize + 1;

    std::vector<float> pixels(m_chunkSize * 4);
    QByteArray chunkData;
    chunkData.resize(m_chunkSize * 2 * sizeof(float)); // Left + Right Float a 32 bit
    float *outData = reinterpret_cast<float*>(chunkData.data());

    // Loop super-veloce senza aspettare il tempo reale
    for (int c = 0; c < totalChunks; ++c) {
        m_shader->setUniformValue("u_time", t);
        m_shader->setUniformValue("u_sampleRate", (float)m_sampleRate);

        if (m_vao) m_vao->bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (m_vao) m_vao->release();

        glReadPixels(0, 0, m_chunkSize, 1, GL_RGBA, GL_FLOAT, pixels.data());

        // Conversione da RGBA (X,Y) ai canali Stereo (Left, Right)
        for (int i = 0; i < m_chunkSize; ++i) {
            outData[i*2]     = pixels[i*4];     // Left
            outData[i*2 + 1] = pixels[i*4 + 1]; // Right
        }

        file.write(chunkData);
        t += timeIncrement;
    }

    m_fbo->release();
    m_context->doneCurrent();
    file.close();

    return true;
}
