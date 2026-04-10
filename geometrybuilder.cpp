#include "geometrybuilder.h"
#include "surfaceengine.h"
#include <cmath>
#include <algorithm>

std::vector<QVector3D> GeometryBuilder::buildBorders(const SurfaceEngine* engine)
{
    std::vector<QVector3D> borderLines;

    const std::vector<Vertex>& vertices = engine->getVertices();
    if (vertices.empty()) return borderLines;

    int nU = engine->getNumU();
    int nV = engine->getNumV();
    int stride = nV + 1;

    // Helper per leggere il vertice (i, j) dalla griglia parametrica (U, V, 0)
    auto getPos = [&](int i, int j) -> QVector3D {
        int index = i * stride + j;
        if (index >= 0 && index < (int)vertices.size()) {
            return vertices[index].position;
        }
        return QVector3D(0,0,0);
    };

    // Bordi lungo U (Linee orizzontali: Lato superiore e inferiore)
    if (!engine->isVClosed()) {
        for (int i = 0; i < nU; ++i) {
            borderLines.push_back(getPos(i, 0));
            borderLines.push_back(getPos(i + 1, 0));

            borderLines.push_back(getPos(i, nV));
            borderLines.push_back(getPos(i + 1, nV));
        }
    }

    // Bordi lungo V (Linee verticali: Lato sinistro e destro)
    if (!engine->isUClosed()) {
        for (int j = 0; j < nV; ++j) {
            borderLines.push_back(getPos(0, j));
            borderLines.push_back(getPos(0, j + 1));

            borderLines.push_back(getPos(nU, j));
            borderLines.push_back(getPos(nU, j + 1));
        }
    }

    return borderLines;
}

std::vector<unsigned int> GeometryBuilder::buildWireframe(const SurfaceEngine* engine, int stepU, int stepV)
{
    std::vector<unsigned int> indices;
    const std::vector<Vertex>& vertices = engine->getVertices();
    if (vertices.empty()) return indices;

    int nU = engine->getNumU();
    int nV = engine->getNumV();
    int stride = nV + 1;
    int sU = std::max(1, stepU);
    int sV = std::max(1, stepV);

    auto addIndex = [&](int i, int j) {
        int index = i * stride + j;
        if (index >= 0 && index < (int)vertices.size()) {
            indices.push_back(index);
        }
    };

    // Linee lungo U
    for (int i = 0; i <= nU; i += sU) {
        for (int j = 0; j < nV; ++j) {
            addIndex(i, j);
            addIndex(i, j + 1);
        }
    }
    // Linee lungo V
    for (int j = 0; j <= nV; j += sV) {
        for (int i = 0; i < nU; ++i) {
            addIndex(i, j);
            addIndex(i + 1, j);
        }
    }
    return indices;
}
