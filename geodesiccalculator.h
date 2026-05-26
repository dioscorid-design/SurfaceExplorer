#ifndef GEODESICCALCULATOR_H
#define GEODESICCALCULATOR_H

#include <QString>
#include <QVector>
#include <QVector4D>
#include <QByteArray>
#include <QMap>
#include <rhi/qrhi.h>
#include <memory>

// =============================================================================
// GeodesicCalculator
// -----------------------------------------------------------------------------
// Calcola il flusso geodetico per via di un compute shader.
//
// La classe è instanziabile (non più una collezione di metodi statici come
// la versione precedente) perché mantiene una CACHE della pipeline GPU. La
// compilazione dello shader (QShaderBaker::bake() con 9 varianti target)
// costa ~40 ms; durante un'animazione a 60 fps con la variabile tempo `t`,
// ribaccare a ogni frame produce il rallentamento osservato.
//
// La cache è indicizzata da una chiave SHA-1 calcolata su tutti gli input
// "lenti" (equazioni, costanti, limiti, numU/numV, puntatore QRhi). Il
// parametro `currentT` è invece "veloce": viene scritto in un UBO a ogni
// chiamata, senza ricompilare nulla.
//
// Comportamento:
//   - Cache hit  : aggiorna UBO + dispatch + readback (~1 ms).
//   - Cache miss : ribuilda tutto come prima (~40 ms), poi esegue.
//   - rhi nullo  : ritorna grid vuota.
//
// La cache è automaticamente invalidata quando QUALUNQUE input lento cambia,
// incluso il puntatore QRhi (utile per perdita di contesto su mobile).
// =============================================================================

class GeodesicCalculator {
public:
    GeodesicCalculator();
    ~GeodesicCalculator();

    // Non copiabile né movibile: possiede risorse GPU.
    GeodesicCalculator(const GeodesicCalculator&) = delete;
    GeodesicCalculator& operator=(const GeodesicCalculator&) = delete;

    // Esegue (o ricostruisce + esegue) il flusso geodetico.
    // `currentT` è l'unico parametro che NON entra nella chiave di cache: viene
    // scritto come uniform nello shader. Gli altri parametri, se cambiano,
    // forzano una ricompilazione.
    QVector<QVector<QVector4D>> computeGeodesicFlow(
        QRhi* rhi,
        const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
        const QString& eqU, const QString& eqV, const QString& eqW,
        const QString& eqDu, const QString& eqDv, const QString& eqDw,
        const QString& eqLambda,
        float uMin, float uMax, int numU,
        float vMin, float vMax, int numV,
        const QMap<QString, float>& constants,
        float currentT,
        QString* outErrorMsg = nullptr);

    // Libera esplicitamente le risorse GPU della cache.
    // Chiamabile su Stop dell'animazione, perdita di contesto, shutdown.
    // Non strettamente necessario: la cache muore con l'oggetto.
    void invalidateCache();

private:
    // Pimpl per non far entrare i tipi QRhi* nell'header (riduce il rebuild
    // cost dei file che includono solo questa intestazione).
    struct Cached;
    std::unique_ptr<Cached> m_cached;

    QByteArray makeCacheKey(QRhi* rhi,
                            const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
                            const QString& eqU, const QString& eqV, const QString& eqW,
                            const QString& eqDu, const QString& eqDv, const QString& eqDw,
                            const QString& eqLambda,
                            float uMin, float uMax, int numU,
                            float vMin, float vMax, int numV,
                            const QMap<QString, float>& constants) const;

    bool rebuildPipeline(QRhi* rhi,
                         const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
                         const QString& eqU, const QString& eqV, const QString& eqW,
                         const QString& eqDu, const QString& eqDv, const QString& eqDw,
                         const QString& eqLambda,
                         float uMin, float uMax, int numU,
                         float vMin, float vMax, int numV,
                         const QMap<QString, float>& constants,
                         QString* outErrorMsg);

    QVector<QVector<QVector4D>> executeCached(float currentT);
};

#endif // GEODESICCALCULATOR_H
