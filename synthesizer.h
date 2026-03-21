#ifndef SYNTHESIZER_H
#define SYNTHESIZER_H

#include <QIODevice>
#include <QAudioSink>
#include <QAudioFormat>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLExtraFunctions>
#include <QOpenGLVertexArrayObject> // FIX 1: Inclusione del VAO
#include <QTimer>
#include <QMutex>
#include <QByteArray>

class Synthesizer : public QIODevice, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    explicit Synthesizer(QObject *parent = nullptr);
    ~Synthesizer() override;

    bool updateScript(const QString &glslCode, bool isSimpleMath = false);

    void start();
    void stop();
    bool saveToRawFile(const QString &filename, int durationSeconds);

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }

private slots:
    void renderAudioChunk();

private:
    void initializeOpenGL();
    void cleanupOpenGL();

    QAudioSink *m_audioSink;
    QAudioFormat m_format;

    QOpenGLContext *m_context;
    QOffscreenSurface *m_surface;
    QOpenGLFramebufferObject *m_fbo;
    QOpenGLShaderProgram *m_shader;
    QOpenGLVertexArrayObject *m_vao; // FIX 2: Aggiunto oggetto VAO

    QTimer *m_renderTimer;

    float m_time;
    int m_sampleRate;
    int m_chunkSize;

    QByteArray m_audioBuffer;
    QMutex m_bufferMutex;
    bool m_isScriptValid;
};

#endif // SYNTHESIZER_H
