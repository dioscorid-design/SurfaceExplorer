#ifndef GEOMETRYBUILDER_H
#define GEOMETRYBUILDER_H

#include <vector>
#include <QVector3D>

// Forward declaration per evitare include pesanti
class SurfaceEngine;

class GeometryBuilder
{
public:
    // Costruisce i dati raw per il buffer wireframe
   static std::vector<unsigned int> buildWireframe(const SurfaceEngine* engine, int stepU, int stepV);
};

#endif // GEOMETRYBUILDER_H
