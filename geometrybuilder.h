#ifndef GEOMETRYBUILDER_H
#define GEOMETRYBUILDER_H

#include <vector>
#include <QVector3D>

// Forward declaration per evitare include pesanti
class SurfaceEngine;

// Intervallo di indici occupato da una parte di mesh nel buffer wireframe.
// Serve al render: con piu' parti gli uniform del dominio cambiano per parte,
// quindi anche il wireframe va disegnato con una draw call per parte.
struct WireframeRange {
    int indexOffset = 0;
    int indexCount  = 0;
    int meshIndex   = 0;
};

class GeometryBuilder
{
public:
    // Costruisce i dati raw per il buffer wireframe.
    // outRanges (opzionale) riceve un intervallo per parte di mesh.
   static std::vector<unsigned int> buildWireframe(const SurfaceEngine* engine, int stepU, int stepV,
                                                   std::vector<WireframeRange>* outRanges = nullptr);
};

#endif // GEOMETRYBUILDER_H
