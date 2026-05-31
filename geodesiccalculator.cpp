#include "geodesiccalculator.h"
#include "glsltranslator.h"
#include <rhi/qshaderbaker.h>
#include <QFile>
#include <QTextStream>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <cmath>

// =============================================================================
// Cached: stato persistente della pipeline GPU.
//
// Forward-dichiarato nell'header e definito qui per non spargere include di
// QRhi negli altri file che vedono solo l'interfaccia pubblica.
// =============================================================================
struct GeodesicCalculator::Cached {
    QByteArray key;
    QRhi* rhi = nullptr;
    std::unique_ptr<QRhiBuffer> ssbo;          // griglia di output (storage)
    std::unique_ptr<QRhiBuffer> ubo;           // u_time (uniform, binding=1)
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    std::unique_ptr<QRhiComputePipeline> pipeline;
    QShader computeShader;                     // tenuto per eventuale diagnostica
    size_t bufferSize = 0;
    int numU = 0;
    int numV = 0;
    float uMin = 0, uMax = 0, vMin = 0, vMax = 0;
};

// =============================================================================
// Ciclo di vita
// =============================================================================
GeodesicCalculator::GeodesicCalculator() = default;
GeodesicCalculator::~GeodesicCalculator() = default;

void GeodesicCalculator::invalidateCache() {
    m_cached.reset();
}

// =============================================================================
// Chiave di cache
// -----------------------------------------------------------------------------
// SHA-1 su: equazioni (UTF-8) + range + numU/numV + costanti + puntatore QRhi.
// Il separatore 0x1F (Unit Separator) evita che due input adiacenti possano
// produrre la stessa concatenazione di un input più lungo (es. "ab"+"cd" vs
// "a"+"bcd").
// =============================================================================
QByteArray GeodesicCalculator::makeCacheKey(QRhi* rhi,
                                            const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
                                            const QString& eqU, const QString& eqV, const QString& eqW,
                                            const QString& eqDu, const QString& eqDv, const QString& eqDw,
                                            const QString& eqLambda,
                                            float uMin, float uMax, int numU,
                                            float vMin, float vMax, int numV,
                                            const QMap<QString, float>& constants) const
{
    QCryptographicHash h(QCryptographicHash::Sha1);

    auto feedStr = [&h](const QString& s) {
        h.addData(s.toUtf8());
        h.addData("\x1F", 1);
    };
    feedStr(eqX); feedStr(eqY); feedStr(eqZ); feedStr(eqP);
    feedStr(eqU); feedStr(eqV); feedStr(eqW);
    feedStr(eqDu); feedStr(eqDv); feedStr(eqDw);
    feedStr(eqLambda);

    auto feedBytes = [&h](const void* data, qsizetype n) {
        h.addData(QByteArray(reinterpret_cast<const char*>(data), int(n)));
    };
    feedBytes(&uMin, sizeof(uMin));
    feedBytes(&uMax, sizeof(uMax));
    feedBytes(&numU, sizeof(numU));
    feedBytes(&vMin, sizeof(vMin));
    feedBytes(&vMax, sizeof(vMax));
    feedBytes(&numV, sizeof(numV));

    // QMap itera ordinatamente per chiave: stessi input → stesso ordine → stesso hash.
    for (auto it = constants.constBegin(); it != constants.constEnd(); ++it) {
        h.addData(it.key().toUtf8());
        h.addData("=", 1);
        const float v = it.value();
        feedBytes(&v, sizeof(v));
        h.addData("\x1F", 1);
    }

    // Il puntatore QRhi fa parte della chiave: se il backend GPU cambia
    // (perdita contesto, switch device su mobile), le risorse non sono più
    // valide e ricostruiamo da capo.
    feedBytes(&rhi, sizeof(rhi));

    return h.result();
}

// =============================================================================
// Rebuild: la versione "vecchia" (~40 ms) di computeGeodesicFlow, modulo
// l'aggiunta dell'UBO di tempo.
// =============================================================================
bool GeodesicCalculator::rebuildPipeline(QRhi* rhi,
                                         const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
                                         const QString& eqU, const QString& eqV, const QString& eqW,
                                         const QString& eqDu, const QString& eqDv, const QString& eqDw,
                                         const QString& eqLambda,
                                         float uMin, float uMax, int numU,
                                         float vMin, float vMax, int numV,
                                         const QMap<QString, float>& constants,
                                         QString* outErrorMsg)
{
    // --- 1. Lettura sorgente shader e iniezione delle costanti/equazioni ---
    QFile file(":/shaders/geodesic.glsl");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outErrorMsg) *outErrorMsg = "Cannot open :/shaders/geodesic.glsl";
        return false;
    }
    QString source = QTextStream(&file).readAll();

    // Costanti dal JSON, iniettate subito sotto #version
    QString constantsGLSL = "\n// Costanti dal file JSON\n";
    for (auto it = constants.constBegin(); it != constants.constEnd(); ++it) {
        constantsGLSL += QString("const float %1 = %2;\n")
        .arg(it.key())
            .arg(it.value(), 0, 'f', 6);
    }
    source.replace("#version 450 core", "#version 450 core\n" + constantsGLSL);

    // Equazioni: passate verbatim al GlslTranslator, vuote → "0.0"
    auto sanitize = [](const QString& eq) {
        QString s = GlslTranslator::translateEquation(eq);
        return s.trimmed().isEmpty() ? "0.0" : s;
    };
    source.replace("/*%LAMBDA_EQ%*/", sanitize(eqLambda));
    source.replace("/*%X_EQ%*/", sanitize(eqX));
    source.replace("/*%Y_EQ%*/", sanitize(eqY));
    source.replace("/*%Z_EQ%*/", sanitize(eqZ));
    source.replace("/*%P_EQ%*/", sanitize(eqP));
    source.replace("/*%U_EQ%*/", sanitize(eqU));
    source.replace("/*%V_EQ%*/", sanitize(eqV));
    source.replace("/*%W_EQ%*/", sanitize(eqW));
    source.replace("/*%DU_EQ%*/", sanitize(eqDu));
    source.replace("/*%DV_EQ%*/", sanitize(eqDv));
    source.replace("/*%DW_EQ%*/", sanitize(eqDw));

    // --- 2. Bake su tutti i target ---
    QShaderBaker baker;
    baker.setSourceString(source.toUtf8(), QShader::ComputeStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({
        {QShader::SpirvShader, QShaderVersion(100)},
        {QShader::SpirvShader, QShaderVersion(130)},
        {QShader::GlslShader,  QShaderVersion(430)},
        {QShader::GlslShader,  QShaderVersion(460)},
        {QShader::GlslShader,  QShaderVersion(310, QShaderVersion::GlslEs)},
        {QShader::GlslShader,  QShaderVersion(320, QShaderVersion::GlslEs)},
        {QShader::MslShader,   QShaderVersion(20)},
        {QShader::HlslShader,  QShaderVersion(50)},
        {QShader::HlslShader,  QShaderVersion(60)}
    });

    QElapsedTimer timer; timer.start();
    QShader computeShader = baker.bake();

    if (!computeShader.isValid()) {
        if (outErrorMsg) *outErrorMsg = baker.errorMessage();
        return false;
    }

    // --- 3. Creazione risorse RHI persistenti ---
    int totalElements = (numU + 1) * (numV + 1);
    size_t bufferSize = totalElements * sizeof(QVector4D);

    auto cached = std::make_unique<Cached>();
    cached->rhi = rhi;
    cached->bufferSize = bufferSize;
    cached->numU = numU;
    cached->numV = numV;
    cached->uMin = uMin; cached->uMax = uMax;
    cached->vMin = vMin; cached->vMax = vMax;
    cached->computeShader = computeShader;

    cached->ssbo.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, bufferSize));
    if (!cached->ssbo->create()) {
        if (outErrorMsg) *outErrorMsg = "Cannot create SSBO.";
        return false;
    }

    // UBO per il tempo. std140 con un singolo float: serve un buffer ≥ 16 byte
    // (regola di alignment del blocco). Lo dichiariamo Dynamic perché lo
    // aggiorniamo a ogni frame.
    cached->ubo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    if (!cached->ubo->create()) {
        if (outErrorMsg) *outErrorMsg = "Cannot create UBO.";
        return false;
    }

    cached->srb.reset(rhi->newShaderResourceBindings());
    cached->srb->setBindings({
        QRhiShaderResourceBinding::bufferLoadStore(
            0, QRhiShaderResourceBinding::ComputeStage, cached->ssbo.get()),
        QRhiShaderResourceBinding::uniformBuffer(
            1, QRhiShaderResourceBinding::ComputeStage, cached->ubo.get())
    });
    if (!cached->srb->create()) {
        if (outErrorMsg) *outErrorMsg = "Cannot create SRB.";
        return false;
    }

    cached->pipeline.reset(rhi->newComputePipeline());
    cached->pipeline->setShaderResourceBindings(cached->srb.get());
    cached->pipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, computeShader));
    timer.restart();
    if (!cached->pipeline->create()) {
        if (outErrorMsg) *outErrorMsg = "Cannot create compute pipeline.";
        return false;
    }

    m_cached = std::move(cached);
    return true;
}

// =============================================================================
// Execute: la versione "veloce" (~1 ms) — aggiorna UBO, dispatch, readback,
// ricostruisce la griglia. Tutto il post-processing (truncate / dead-point
// detection) è identico alla versione originale.
// =============================================================================
QVector<QVector<QVector4D>> GeodesicCalculator::executeCached(float currentT) {
    if (!m_cached || !m_cached->rhi) return {};

    QRhi* rhi = m_cached->rhi;
    int numU = m_cached->numU;
    int numV = m_cached->numV;

    QRhiCommandBuffer* cb = nullptr;
    rhi->beginOffscreenFrame(&cb);

    // Update UBO con il nuovo t
    // Update UBO: layout std140 = { float time; float uMin,uMax,vMin,vMax; int numU,numV; }
    QRhiResourceUpdateBatch* rub = rhi->nextResourceUpdateBatch();
    rub->updateDynamicBuffer(m_cached->ubo.get(),  0, sizeof(float), &currentT);
    rub->updateDynamicBuffer(m_cached->ubo.get(),  4, sizeof(float), &m_cached->uMin);
    rub->updateDynamicBuffer(m_cached->ubo.get(),  8, sizeof(float), &m_cached->uMax);
    rub->updateDynamicBuffer(m_cached->ubo.get(), 12, sizeof(float), &m_cached->vMin);
    rub->updateDynamicBuffer(m_cached->ubo.get(), 16, sizeof(float), &m_cached->vMax);
    rub->updateDynamicBuffer(m_cached->ubo.get(), 20, sizeof(int),   &m_cached->numU);
    rub->updateDynamicBuffer(m_cached->ubo.get(), 24, sizeof(int),   &m_cached->numV);

    cb->beginComputePass(rub);
    cb->setComputePipeline(m_cached->pipeline.get());
    cb->setShaderResources();

    int workGroupsX = static_cast<int>(std::ceil(float(numU + 1) / 64.0f));
    cb->dispatch(workGroupsX, 1, 1);

    QRhiResourceUpdateBatch* readbackRub = rhi->nextResourceUpdateBatch();
    QRhiReadbackResult result;
    readbackRub->readBackBuffer(m_cached->ssbo.get(), 0, m_cached->bufferSize, &result);
    cb->endComputePass(readbackRub);

    rhi->endOffscreenFrame();
    QElapsedTimer t2; t2.start();
    rhi->finish();

    // --- Ricostruzione griglia (logica identica all'originale) ---
    QVector<QVector<QVector4D>> grid(numU + 1, QVector<QVector4D>(numV + 1));
    if (result.data.isEmpty()) {
        return {};
    }

    const QVector4D* rawData = reinterpret_cast<const QVector4D*>(result.data.constData());
    int stride = numV + 1;
    const float safeLim = 1000.0f;

    auto isBadPoint = [&](const QVector4D& v) -> bool {
        return std::isnan(v.x()) || std::isinf(v.x()) || std::abs(v.x()) > safeLim ||
               std::isnan(v.y()) || std::isinf(v.y()) || std::abs(v.y()) > safeLim ||
               std::isnan(v.z()) || std::isinf(v.z()) || std::abs(v.z()) > safeLim ||
               std::isnan(v.w()) || std::isinf(v.w()) || std::abs(v.w()) > safeLim;
    };

    int truncatedTrajectories = 0;
    int completelyDeadTrajectories = 0;

    for (int i = 0; i <= numU; ++i) {
        QVector4D lastGood(0.0f, 0.0f, 0.0f, 0.0f);
        bool foundGood = false;
        bool truncated = false;

        for (int j = 0; j <= numV; ++j) {
            QVector4D v = rawData[i * stride + j];

            if (truncated) {
                grid[i][j] = lastGood;
                continue;
            }

            if (isBadPoint(v)) {
                truncated = true;
                if (foundGood) grid[i][j] = lastGood;
                else           grid[i][j] = QVector4D(0,0,0,0);
            } else {
                grid[i][j] = v;
                lastGood = v;
                foundGood = true;
            }
        }

        if (truncated) {
            truncatedTrajectories++;
            if (!foundGood) completelyDeadTrajectories++;
        }
    }

    const int totalTrajectories = numU + 1;
    if (completelyDeadTrajectories == totalTrajectories) return {};

    constexpr float kMaxTruncatedFraction = 0.05f;
    const int maxAllowedTruncated = static_cast<int>(totalTrajectories * kMaxTruncatedFraction);
    if (truncatedTrajectories > maxAllowedTruncated) return {};

    return grid;
}

// =============================================================================
// Entrypoint pubblico: prova la cache, ricostruisce se serve, esegue.
// =============================================================================
QVector<QVector<QVector4D>> GeodesicCalculator::computeGeodesicFlow(
    QRhi* rhi,
    const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
    const QString& eqU, const QString& eqV, const QString& eqW,
    const QString& eqDu, const QString& eqDv, const QString& eqDw,
    const QString& eqLambda,
    float uMin, float uMax, int numU,
    float vMin, float vMax, int numV,
    const QMap<QString, float>& constants,
    float currentT,
    QString* outErrorMsg)
{
    if (!rhi) return {};

    const QByteArray key = makeCacheKey(rhi,
                                        eqX, eqY, eqZ, eqP, eqU, eqV, eqW, eqDu, eqDv, eqDw, eqLambda,
                                        uMin, uMax, numU, vMin, vMax, numV, constants);

    const bool hit = m_cached && m_cached->rhi == rhi && m_cached->key == key;
    if (!hit) {
        m_cached.reset();
        if (!rebuildPipeline(rhi,
                             eqX, eqY, eqZ, eqP, eqU, eqV, eqW, eqDu, eqDv, eqDw, eqLambda,
                             uMin, uMax, numU, vMin, vMax, numV, constants,
                             outErrorMsg)) {
            return {};
        }
        m_cached->key = key;
    }

    return executeCached(currentT);
}
