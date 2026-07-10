#include "geometrybuilder.h"
#include "surfaceengine.h"
#include <cmath>
#include <algorithm>

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
