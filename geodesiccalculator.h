#ifndef GEODESICCALCULATOR_H
#define GEODESICCALCULATOR_H

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include "exprtk.hpp"
#include <rhi/qrhi.h>

class GeodesicCalculator {
public:
    static QVector<QVector<QVector4D>> computeGeodesicFlow(
        QRhi* rhi, // <-- NUOVO: Necessario per lanciare il Compute Shader
        const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
        const QString& eqU, const QString& eqV, const QString& eqW,
        const QString& eqDu, const QString& eqDv, const QString& eqDw,
        const QString& eqLambda, float uMin, float uMax, int numU,
        float vMin, float vMax, int numV,
        const QMap<QString, float>& constants,
        QString* outErrorMsg = nullptr);
};

#endif // GEODESICCALCULATOR_H
