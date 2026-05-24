#include "geodesiccalculator.h"
#include "glsltranslator.h"
#include <rhi/qshaderbaker.h>
#include <QFile>
#include <QTextStream>
#include <memory>

QVector<QVector<QVector4D>> GeodesicCalculator::computeGeodesicFlow(
    QRhi* rhi,
    const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
    const QString& eqU, const QString& eqV, const QString& eqW,
    const QString& eqDu, const QString& eqDv, const QString& eqDw,
    const QString& eqLambda, float uMin, float uMax, int numU,
    float vMin, float vMax, int numV,
    const QMap<QString, float>& constants,
    QString* outErrorMsg)
{
    if (!rhi) return QVector<QVector<QVector4D>>();

    // 1. Carica e prepara il codice GLSL
    QFile file(":/shaders/geodesic.glsl");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString source = QTextStream(&file).readAll();

    // --- NUOVO BLOCCO: Iniezione dinamica delle costanti ---
    QString constantsGLSL = "\n// Costanti dal file JSON\n";
    for (auto it = constants.constBegin(); it != constants.constEnd(); ++it) {
        constantsGLSL += QString("const float %1 = %2;\n")
        .arg(it.key())
            .arg(it.value(), 0, 'f', 6);
    }

    // Inseriamo le costanti subito sotto la direttiva #version
    source.replace("#version 450 core", "#version 450 core\n" + constantsGLSL);
    // --------------------------------------------------------

    // Sostituiamo le costanti matematiche (assicurati di formattarle come float, es. "1.0")
    source.replace("/*%U_MIN%*/", QString::number(uMin, 'f', 6));
    source.replace("/*%U_MAX%*/", QString::number(uMax, 'f', 6));
    source.replace("/*%V_MIN%*/", QString::number(vMin, 'f', 6));
    source.replace("/*%V_MAX%*/", QString::number(vMax, 'f', 6));
    source.replace("/*%NUM_U%*/", QString::number(numU));
    source.replace("/*%NUM_V%*/", QString::number(numV));

    // Rimpiazziamo le equazioni sfruttando il tuo traduttore GLSL
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

    // 2. Compilazione dinamica via QShaderBaker
    QShaderBaker baker;
    baker.setSourceString(source.toUtf8(), QShader::ComputeStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});

    baker.setGeneratedShaders({
        // Vulkan (SPIR-V)
        {QShader::SpirvShader, QShaderVersion(100)}, // SPIR-V 1.0 (Compatibilità base)
        {QShader::SpirvShader, QShaderVersion(130)}, // SPIR-V 1.3 (Supporto funzioni avanzate Vulkan 1.1+)

        // OpenGL Desktop
        {QShader::GlslShader, QShaderVersion(430)},  // GLSL 4.30 (Minimo assoluto per Compute Shaders)
        {QShader::GlslShader, QShaderVersion(460)},  // GLSL 4.60 (Ultima versione, per driver recenti)

        // OpenGL ES (Mobile: Android / iOS / WebGL)
        {QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs)}, // Minimo per Compute su Mobile
        {QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs)},

        // Metal (Apple: macOS / iOS / iPadOS)
        {QShader::MslShader, QShaderVersion(20)},    // MSL 2.0 (Standard per Apple Silicon/dispositivi recenti)

        // DirectX (Windows)
        {QShader::HlslShader, QShaderVersion(50)},   // HLSL Shader Model 5.0 (DirectX 11, minimo per Compute)
        {QShader::HlslShader, QShaderVersion(60)}    // HLSL Shader Model 6.0 (DirectX 12)
    });

    QShader computeShader = baker.bake();

    // --- GESTIONE DEGLI ERRORI DI COMPILAZIONE ---
    if (!computeShader.isValid()) {
        if (outErrorMsg) {
            *outErrorMsg = baker.errorMessage(); // Passa l'errore alla UI
        }
        return {}; // Ferma l'esecuzione e restituisce una griglia vuota
    }

    // 3. Creazione Buffers e Pipeline RHI
    int totalElements = (numU + 1) * (numV + 1);
    size_t bufferSize = totalElements * sizeof(QVector4D);

    // Usiamo std::unique_ptr per pulizia automatica
    std::unique_ptr<QRhiBuffer> ssbo(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, bufferSize));
    ssbo->create();

    std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
    srb->setBindings({
        QRhiShaderResourceBinding::bufferLoadStore(0, QRhiShaderResourceBinding::ComputeStage, ssbo.get())
    });
    srb->create();

    std::unique_ptr<QRhiComputePipeline> pipeline(rhi->newComputePipeline());
    pipeline->setShaderResourceBindings(srb.get());
    pipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, computeShader));
    pipeline->create();

    // 4. Esecuzione e Readback Sincrono
    QRhiCommandBuffer *cb;
    rhi->beginOffscreenFrame(&cb);

    QRhiResourceUpdateBatch *rub = rhi->nextResourceUpdateBatch();
    cb->beginComputePass(rub);
    cb->setComputePipeline(pipeline.get());
    cb->setShaderResources();

    // Dispatch: Gruppi da 64 thread. Calcoliamo quanti workgroup ci servono.
    int workGroupsX = std::ceil(float(numU + 1) / 64.0f);
    cb->dispatch(workGroupsX, 1, 1);

    QRhiResourceUpdateBatch *readbackRub = rhi->nextResourceUpdateBatch();
    QRhiReadbackResult result;

    // Richiediamo i dati indietro.
    readbackRub->readBackBuffer(ssbo.get(), 0, bufferSize, &result);
    cb->endComputePass(readbackRub);

    rhi->endOffscreenFrame();

    // BLOCCO FORZATO DELLA CPU: Aspettiamo che la GPU finisca tutto
    rhi->finish();

    // 5. Ricostruzione dell'output nel formato originale
    QVector<QVector<QVector4D>> grid(numU + 1, QVector<QVector4D>(numV + 1));
    if (result.data.isEmpty()) {
        if (outErrorMsg) *outErrorMsg = "GPU readback returned no data.";
        return QVector<QVector<QVector4D>>();
    }

    const QVector4D* rawData = reinterpret_cast<const QVector4D*>(result.data.constData());
    int stride = numV + 1;

    // Soglia: alziamo a 1000 (la vecchia 50 era troppo stretta per alcune superfici).
    // Si potrebbe in futuro derivarla dalle dimensioni della scena.
    const float safeLim = 1000.0f;

    auto isBadPoint = [&](const QVector4D& v) -> bool {
        return std::isnan(v.x()) || std::isinf(v.x()) || std::abs(v.x()) > safeLim ||
               std::isnan(v.y()) || std::isinf(v.y()) || std::abs(v.y()) > safeLim ||
               std::isnan(v.z()) || std::isinf(v.z()) || std::abs(v.z()) > safeLim ||
               std::isnan(v.w()) || std::isinf(v.w()) || std::abs(v.w()) > safeLim;
    };

    int truncatedTrajectories = 0;
    int completelyDeadTrajectories = 0;

    // Per-traiettoria: ogni riga i è una geodetica indipendente.
    // Se una geodetica incontra un punto cattivo, la "congeliamo" sull'ultimo
    // punto valido invece di scartare tutto. Visivamente la geodetica si ferma
    // lì, ma le altre 99 vengono comunque rese.
    for (int i = 0; i <= numU; ++i) {
        QVector4D lastGood(0.0f, 0.0f, 0.0f, 0.0f);
        bool foundGood = false;
        bool truncated = false;

        for (int j = 0; j <= numV; ++j) {
            QVector4D v = rawData[i * stride + j];

            if (truncated) {
                // Dopo il troncamento, riempiamo con l'ultimo punto valido
                // così la mesh non ha "buchi" topologici.
                grid[i][j] = lastGood;
                continue;
            }

            if (isBadPoint(v)) {
                truncated = true;
                if (foundGood) {
                    grid[i][j] = lastGood;        // termina sul punto buono
                } else {
                    grid[i][j] = QVector4D(0,0,0,0); // geodetica nata morta
                }
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

    if (completelyDeadTrajectories == totalTrajectories) {
        return {};
    }

    constexpr float kMaxTruncatedFraction = 0.05f;
    const int maxAllowedTruncated = static_cast<int>(totalTrajectories * kMaxTruncatedFraction);
    if (truncatedTrajectories > maxAllowedTruncated) {
        return {};
    }

    return grid;
}
