#include "surfaceengine.h"
#include <cmath>
#include "exprtk.hpp"

SurfaceEngine::SurfaceEngine()
{
    // Limiti di default
    uMin = 0.0f; uMax = 6.28318f;
    vMin = 0.0f; vMax = 6.28318f;
    wMin = 0.0f; wMax = 1.0f;

    use4DLighting = false;
    m_useScriptMode = false;
    m_glslCode = "";

    u_is_closed = false;
    v_is_closed = false;
    u_closes_twisted = false;

    // Tabella Path
    m_pathSymbolTable.add_variable("t", m_varT);
    m_pathSymbolTable.add_variable("A", m_varA);
    m_pathSymbolTable.add_variable("B", m_varB);
    m_pathSymbolTable.add_variable("C", m_varC);
    m_pathSymbolTable.add_variable("D", m_varD);
    m_pathSymbolTable.add_variable("E", m_varE);
    m_pathSymbolTable.add_variable("F", m_varF);
    m_pathSymbolTable.add_variable("s", m_varS);
    m_pathSymbolTable.add_constants();

    // Tabella Surface
    m_surfaceSymbolTable.add_variable("u", m_varU);
    m_surfaceSymbolTable.add_variable("v", m_varV);
    m_surfaceSymbolTable.add_variable("w", m_varW);
    m_surfaceSymbolTable.add_variable("p", m_varP);
    m_surfaceSymbolTable.add_variable("A", m_varA);
    m_surfaceSymbolTable.add_variable("B", m_varB);
    m_surfaceSymbolTable.add_variable("C", m_varC);
    m_surfaceSymbolTable.add_variable("D", m_varD);
    m_surfaceSymbolTable.add_variable("E", m_varE);
    m_surfaceSymbolTable.add_variable("F", m_varF);
    m_surfaceSymbolTable.add_variable("s", m_varS);
    m_surfaceSymbolTable.add_variable("t", m_varT);
    m_surfaceSymbolTable.add_constants();

    // Generiamo subito la mesh di default
    computeMesh();
}


// ==========================================================
// CORE MESH & GENERATION
// ==========================================================

void SurfaceEngine::clear()
{
    generatedVertices.clear();
    generatedIndices.clear();
}

void SurfaceEngine::computeMesh()
{
    generatedVertices.clear();
    generatedIndices.clear();

    generateParametricGrid();
}

void SurfaceEngine::setResolution(int u, int v)
{
    numU = std::max(2, u);
    numV = std::max(2, v);
}


// ==========================================================
// EQUATIONS & CONSTRAINTS
// ==========================================================

void SurfaceEngine::setEquations(const QString &x, const QString &y, const QString &z, const QString &p)
{
    eqX = x; eqY = y; eqZ = z; eqP = p;

    exprtk::parser<float> parser;

    auto compileSurf = [&](const QString &s, CachedExpression &target) {
        target.isValid = false;
        if (s.trimmed().isEmpty()) return;
        target.expr.register_symbol_table(m_surfaceSymbolTable);
        if (parser.compile(s.toStdString(), target.expr)) {
            target.isValid = true;
        }
    };

    compileSurf(x, m_exprSurfX);
    compileSurf(y, m_exprSurfY);
    compileSurf(z, m_exprSurfZ);
}

void SurfaceEngine::setConstraintMode(ConstraintMode mode) {
    m_constraintMode = mode;
}

void SurfaceEngine::setExplicitU(const QString &eq) {
    m_explicitU = eq;
}

void SurfaceEngine::setExplicitV(const QString &eq) {
    m_explicitV = eq;
}

void SurfaceEngine::setExplicitW(const QString &eq) {
    m_explicitW = eq;
}

QString SurfaceEngine::getActiveExplicitEquation() const {
    switch(m_constraintMode) {
    case ConstraintU: return m_explicitU;
    case ConstraintV: return m_explicitV;
    default: return m_explicitW;
    }
}


// ==========================================================
// MATHEMATICAL RANGES & CONSTANTS
// ==========================================================

void SurfaceEngine::setRangeU(float min, float max)
{
    this->uMin = min;
    this->uMax = max;
}

void SurfaceEngine::setRangeV(float min, float max)
{
    this->vMin = min;
    this->vMax = max;
}

void SurfaceEngine::setRangeW(float min, float max)
{
    this->wMin = min;
    this->wMax = max;
}

void SurfaceEngine::setConstants(float a, float b, float c, float d, float e, float f, float s)
{
    valA = a; valB = b; valC = c;
    valD = d; valE = e; valF = f;
    valS = s;

    m_varA = a; m_varB = b; m_varC = c;
    m_varD = d; m_varE = e; m_varF = f;
    m_varS = s;
}


// ==========================================================
// PATHS 3D & 4D (EVALUATION)
// ==========================================================

bool SurfaceEngine::compilePathEquations(const QString &x, const QString &y, const QString &z, const QString &p,
                                         const QString &alpha, const QString &beta, const QString &gamma)
{
    exprtk::parser<float> parser;

    compileSingleExpr(x, m_exprPathX, parser);
    compileSingleExpr(y, m_exprPathY, parser);
    compileSingleExpr(z, m_exprPathZ, parser);
    compileSingleExpr(p, m_exprPathP, parser);

    compileSingleExpr(alpha, m_exprPathAlpha, parser);
    compileSingleExpr(beta, m_exprPathBeta, parser);
    compileSingleExpr(gamma, m_exprPathGamma, parser);

    pathValid = (m_exprPathX.isValid && m_exprPathY.isValid && m_exprPathZ.isValid);
    return pathValid;
}

QVector4D SurfaceEngine::evaluatePathPosition(float t)
{
    if (!pathValid) return QVector4D(0,0,4,0);

    m_varT = t;

    float x = m_exprPathX.isValid ? m_exprPathX.expr.value() : 0.0f;
    float y = m_exprPathY.isValid ? m_exprPathY.expr.value() : 0.0f;
    float z = m_exprPathZ.isValid ? m_exprPathZ.expr.value() : 0.0f;
    float p = m_exprPathP.isValid ? m_exprPathP.expr.value() : 0.0f;

    return QVector4D(x, y, z, p);
}

float SurfaceEngine::evaluatePathAlpha(float t)
{
    if (!m_exprPathAlpha.isValid) return 0.0f;
    m_varT = t;
    return m_exprPathAlpha.expr.value();
}

float SurfaceEngine::evaluatePathBeta(float t)
{
    if (!m_exprPathBeta.isValid) return 0.0f;
    m_varT = t;
    return m_exprPathBeta.expr.value();
}

float SurfaceEngine::evaluatePathGamma(float t)
{
    if (!m_exprPathGamma.isValid) return 0.0f;
    m_varT = t;
    return m_exprPathGamma.expr.value();
}

bool SurfaceEngine::compilePath3DEquations(const QString &x, const QString &y, const QString &z, const QString &r)
{
    exprtk::parser<float> parser;

    compileSingleExpr(x, m_exprPath3DX, parser);
    compileSingleExpr(y, m_exprPath3DY, parser);
    compileSingleExpr(z, m_exprPath3DZ, parser);
    compileSingleExpr(r, m_exprPath3DR, parser);

    path3DValid = (m_exprPath3DX.isValid && m_exprPath3DY.isValid && m_exprPath3DZ.isValid);
    return path3DValid;
}

QVector4D SurfaceEngine::evaluatePath3DPosition(float t)
{
    if (!path3DValid) return QVector4D(0,0,4,0);

    m_varT = t;

    float x = m_exprPath3DX.isValid ? m_exprPath3DX.expr.value() : 0.0f;
    float y = m_exprPath3DY.isValid ? m_exprPath3DY.expr.value() : 0.0f;
    float z = m_exprPath3DZ.isValid ? m_exprPath3DZ.expr.value() : 0.0f;
    float r = m_exprPath3DR.isValid ? m_exprPath3DR.expr.value() : 0.0f;

    return QVector4D(x, y, z, r);
}


// ==========================================================
// 4D LIGHTING STATE (Used by GLWidget)
// ==========================================================

void SurfaceEngine::set4DLighting(bool enable)
{
    use4DLighting = enable;
}


// ==========================================================
// SCRIPTING & SHADERS
// ==========================================================

void SurfaceEngine::setScriptMode(bool active) {
    m_useScriptMode = active;
}


// ==========================================================
// PRIVATE INTERNAL HELPERS
// ==========================================================

void SurfaceEngine::compileSingleExpr(const QString &eqStr, CachedExpression &target, exprtk::parser<float> &parser)
{
    target.isValid = false;
    if (eqStr.trimmed().isEmpty()) return;

    target.expr.register_symbol_table(m_pathSymbolTable);

    if (parser.compile(eqStr.toStdString(), target.expr)) {
        target.isValid = true;
    }
}

void SurfaceEngine::generateParametricGrid()
{
    // 1. Pulizia dei buffer
    generatedVertices.clear();
    generatedIndices.clear();

    int vertexCount = (numU + 1) * (numV + 1);
    generatedVertices.reserve(vertexCount);

    // 2. Definizione dei range dinamici in base alla modalità
    float start1, end1;
    float start2, end2;

    if (m_constraintMode == ConstraintU) {
        start1 = vMin; end1 = vMax;
        start2 = wMin; end2 = wMax;
    }
    else if (m_constraintMode == ConstraintV) {
        start1 = uMin; end1 = uMax;
        start2 = wMin; end2 = wMax;
    }
    else {
        start1 = uMin; end1 = uMax;
        start2 = vMin; end2 = vMax;
    }

    // 3. Generazione Dati
    for (int i = 0; i <= numU; ++i) {
        float val1 = start1 + (float)i / numU * (end1 - start1);

        for (int j = 0; j <= numV; ++j) {
            float val2 = start2 + (float)j / numV * (end2 - start2);

            QVector3D paramPos(val1, val2, 0.0f);

            Vertex vert;
            vert.position = paramPos;
            vert.normal = QVector4D(0.0f, 0.0f, 1.0f, 0.0f);
            vert.texCoord = QVector2D((float)i/numU, (float)j/numV);

            generatedVertices.push_back(vert);
        }
    }

    // 4. Generazione Indici (Triangoli)
    generatedIndices.reserve(numU * numV * 6);

    for (int i = 0; i < numU; ++i) {
        for (int j = 0; j < numV; ++j) {
            int p0 = i * (numV + 1) + j;
            int p1 = (i + 1) * (numV + 1) + j;
            int p2 = i * (numV + 1) + (j + 1);
            int p3 = (i + 1) * (numV + 1) + (j + 1);

            generatedIndices.push_back(p0);
            generatedIndices.push_back(p1);
            generatedIndices.push_back(p2);

            generatedIndices.push_back(p2);
            generatedIndices.push_back(p1);
            generatedIndices.push_back(p3);
        }
    }

    detectMeshClosure();
}

void SurfaceEngine::detectMeshClosure()
{
    if (m_useScriptMode) {
        u_is_closed = false; v_is_closed = false;
        return;
    }

    if (!m_exprSurfX.isValid || !m_exprSurfY.isValid || !m_exprSurfZ.isValid) {
        u_is_closed = false; v_is_closed = false;
        return;
    }

    float threshold = 0.001f;

    float start1, end1, start2, end2;

    if (m_constraintMode == ConstraintU) {
        start1 = vMin; end1 = vMax;
        start2 = wMin; end2 = wMax;
    } else if (m_constraintMode == ConstraintV) {
        start1 = uMin; end1 = uMax;
        start2 = wMin; end2 = wMax;
    } else {
        start1 = uMin; end1 = uMax;
        start2 = vMin; end2 = vMax;
    }

    auto evalPoint = [&](float p1, float p2) -> QVector3D {
        if (m_constraintMode == ConstraintU) {
            m_varV = p1; m_varW = p2; m_varU = 0;
        } else if (m_constraintMode == ConstraintV) {
            m_varU = p1; m_varW = p2; m_varV = 0;
        } else {
            m_varU = p1; m_varV = p2; m_varW = 0;
        }
        return QVector3D(m_exprSurfX.expr.value(), m_exprSurfY.expr.value(), m_exprSurfZ.expr.value());
    };

    float mid2 = (start2 + end2) * 0.5f;
    QVector3D pStart1 = evalPoint(start1, mid2);
    QVector3D pEnd1   = evalPoint(end1, mid2);
    u_is_closed = (pStart1.distanceToPoint(pEnd1) < threshold);

    float mid1 = (start1 + end1) * 0.5f;
    QVector3D pStart2 = evalPoint(mid1, start2);
    QVector3D pEnd2   = evalPoint(mid1, end2);
    v_is_closed = (pStart2.distanceToPoint(pEnd2) < threshold);
}
