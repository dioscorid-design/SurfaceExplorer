#ifndef SYNTHESIZER_H
#define SYNTHESIZER_H

#include <QIODevice>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QAudioFormat>
#include <QTimer>
#include <QMutex>
#include <QByteArray>

// 1. Includiamo l'header principale di RHI
#include <rhi/qrhi.h>

// 2. Definiamo la struttura per i parametri da inviare alla GPU.
// NOTA: I buffer in RHI (e Vulkan/Metal) richiedono un allineamento a blocchi di 16 byte (vec4).
// time (4 byte) + sampleRate (4 byte) + padding (8 byte) = 16 byte esatti.
struct AudioUboData {
    int startSample;
    float sampleRate;
    float padding[2];
};

// 3. Rimuoviamo l'ereditarietà da QOpenGLExtraFunctions
class Synthesizer : public QIODevice {
    Q_OBJECT
public:
    explicit Synthesizer(QObject *parent = nullptr);
    ~Synthesizer() override;

    // Nuovo metodo per ricevere il motore RHI dal widget principale
    void setRhi(QRhi *rhi);

    bool updateScript(const QString &glslCode, bool isSimpleMath = false);
    QString lastError() const { return m_lastError; }

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
    // Rinominati per RHI
    void initializeRhiResources();
    void cleanupRhiResources();

    QAudioSink *m_audioSink;
    QAudioFormat m_format;

    QTimer *m_renderTimer;

    int m_currentSample = 0;
    int m_sampleRate;
    int m_chunkSize;

    QByteArray m_audioBuffer;
    QMutex m_bufferMutex;
    bool m_isScriptValid;
    QString m_lastError;

    // --- NUOVE VARIABILI RHI ---
    QRhi *m_rhi = nullptr; // Non possediamo noi l'RHI, ci viene solo prestato

    QRhiTexture *m_texture = nullptr;
    QRhiTextureRenderTarget *m_renderTarget = nullptr;
    QRhiRenderPassDescriptor *m_renderPassDesc = nullptr;

    QRhiGraphicsPipeline *m_pipeline = nullptr;
    QRhiBuffer *m_ubo = nullptr;
    QRhiShaderResourceBindings *m_bindings = nullptr;

    AudioUboData m_uboData;
};

#endif // SYNTHESIZER_H
