#include "geometrybuilder.h"
#include "surfaceengine.h"
#include <cmath>
#include <algorithm>

std::vector<unsigned int> GeometryBuilder::buildWireframe(const SurfaceEngine* engine, int stepU, int stepV,
                                                          std::vector<WireframeRange>* outRanges)
{
    std::vector<unsigned int> indices;
    if (outRanges) outRanges->clear();
    const std::vector<Vertex>& vertices = engine->getVertices();
    if (vertices.empty()) return indices;

    int sU = std::max(1, stepU);
    int sV = std::max(1, stepV);

    // Multi-mesh: ogni parte ha risoluzione (e quindi stride) propria, e i suoi
    // vertici stanno a un offset nel buffer condiviso. Un solo stride globale
    // collegherebbe punti di parti diverse.
    //
    // NB: questi indici sono ASSOLUTI, non locali come quelli della superficie:
    // il wireframe e' disegnato con una sola drawIndexed sul VBO condiviso,
    // senza vertexOffset (vedi il ramo renderMode==2 in glwidget.cpp), quindi
    // l'offset della parte va sommato qui.
    const std::vector<MeshPart>& parts = engine->getMeshParts();

    auto emitGrid = [&](int nU, int nV, int base) {
        int stride = nV + 1;

        auto addIndex = [&](int i, int j) {
            int index = base + i * stride + j;
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
    };

    auto emitPart = [&](int nU, int nV, int base, int meshIndex) {
        const int before = (int)indices.size();
        emitGrid(nU, nV, base);
        if (outRanges) {
            WireframeRange r;
            r.indexOffset = before;
            r.indexCount  = (int)indices.size() - before;
            r.meshIndex   = meshIndex;
            outRanges->push_back(r);
        }
    };

    if (parts.empty()) {
        // Mesh senza parti dichiarate (es. custom mesh caricata direttamente):
        // comportamento storico, una griglia sola che parte da 0.
        emitPart(engine->getNumU(), engine->getNumV(), 0, 0);
    } else {
        for (const MeshPart& p : parts) {
            emitPart(p.numU, p.numV, p.vertexOffset, p.meshIndex);
        }
    }

    return indices;
}
