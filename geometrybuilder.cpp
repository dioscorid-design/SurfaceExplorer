#include "geometrybuilder.h"
#include "surfaceengine.h"
#include <cmath>
#include <algorithm>

std::vector<QVector3D> GeometryBuilder::buildBorders(const SurfaceEngine* engine)
{
    std::vector<QVector3D> borderLines;

    // 1. Recupera il vettore UNICO dei vertici
    const std::vector<Vertex>& vertices = engine->getVertices();
    if (vertices.empty()) return borderLines;

    int nU = engine->getNumU();
    int nV = engine->getNumV();
    // Larghezza della riga nella griglia piatta (numero di colonne + 1)
    int stride = nV + 1;

    float thresholdSq = 5.0f * 5.0f;

    // Helper per leggere il vertice (i, j) dal vettore piatto
    auto getPos = [&](int i, int j) -> QVector3D {
        int index = i * stride + j;
        if (index >= 0 && index < (int)vertices.size()) {
            return vertices[index].position;
        }
        return QVector3D(0,0,0);
    };

    auto addSegment = [&](const QVector3D& p1, const QVector3D& p2) {
        // Ignora punti "infiniti" o non validi (NaN)
        if (std::isnan(p1.x()) || std::isnan(p2.x())) return;
        // Ignora segmenti troppo lunghi (salti di discontinuità)
        if ((p1 - p2).lengthSquared() > thresholdSq) return;
        borderLines.push_back(p1);
        borderLines.push_back(p2);
    };

    // Bordi lungo U (Linee orizzontali)
    if (!engine->isVClosed()) {
        for (int i = 0; i < nU; ++i) {
            // Lato superiore (j=0) e inferiore (j=nV)
            QVector3D p0_curr = getPos(i, 0);
            QVector3D p0_next = getPos(i + 1, 0);
            addSegment(p0_curr, p0_next);

            QVector3D p1_curr = getPos(i, nV);
            QVector3D p1_next = getPos(i + 1, nV);
            addSegment(p1_curr, p1_next);
        }
    }

    // Bordi lungo V (Linee verticali)
    if (!engine->isUClosed()) {
        for (int j = 0; j < nV; ++j) {
            // Lato sinistro (i=0) e destro (i=nU)
            QVector3D p0_curr = getPos(0, j);
            QVector3D p0_next = getPos(0, j + 1);
            addSegment(p0_curr, p0_next);

            QVector3D p1_curr = getPos(nU, j);
            QVector3D p1_next = getPos(nU, j + 1);
            addSegment(p1_curr, p1_next);
        }
    }

    return borderLines;
}

std::vector<float> GeometryBuilder::buildWireframe(const SurfaceEngine* engine, int stepU, int stepV)
{
    std::vector<float> data;
    const std::vector<Vertex>& vertices = engine->getVertices();

    if (vertices.empty()) return data;

    int nU = engine->getNumU();
    int nV = engine->getNumV();
    int stride = nV + 1;

    int sU = std::max(1, stepU);
    int sV = std::max(1, stepV);

    auto addPoint = [&](int i, int j) {
        int index = i * stride + j;
        if (index >= 0 && index < (int)vertices.size()) {
            const Vertex& v = vertices[index];
            // Posizione
            data.push_back(v.position.x());
            data.push_back(v.position.y());
            data.push_back(v.position.z());

            // Normale (da Vector4D a 3D per visualizzazione)
            data.push_back(v.normal.x());
            data.push_back(v.normal.y());
            data.push_back(v.normal.z());
        }
    };

    // Linee lungo U
    for (int i = 0; i <= nU; i += sU) {
        for (int j = 0; j < nV; ++j) {
            addPoint(i, j);
            addPoint(i, j + 1);
        }
    }

    // Linee lungo V
    for (int j = 0; j <= nV; j += sV) {
        for (int i = 0; i < nU; ++i) {
            addPoint(i, j);
            addPoint(i + 1, j);
        }
    }
    return data;
}
