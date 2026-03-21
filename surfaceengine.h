#ifndef SURFACEENGINE_H
#define SURFACEENGINE_H

#include "expressionparser.h"
#include <vector>
#include <QString>
#include <QVector3D>
#include <QVector4D>
#include <QVector2D>

// ==========================================================
// CORE DATA STRUCTURES
// ==========================================================
struct Vertex {
    QVector3D position;
    QVector4D normal;
    QVector2D texCoord;
};

struct CachedExpression {
    exprtk::expression<float> expr;
    bool isValid = false;
};

class SurfaceEngine
{
public:
    SurfaceEngine();

    // ==========================================================
    // ENUMS
    // ==========================================================
    enum ConstraintMode { ConstraintW, ConstraintU, ConstraintV };

    // ==========================================================
    // CORE MESH & GENERATION
    // ==========================================================
    void clear();
    void computeMesh();
    void setResolution(int u, int v);

    const std::vector<Vertex>& getVertices() const { return generatedVertices; }
    const std::vector<unsigned int>& getIndices() const { return generatedIndices; }

    int getNumU() const { return numU; }
    int getNumV() const { return numV; }
    bool isUClosed() const { return u_is_closed; }
    bool isVClosed() const { return v_is_closed; }
    bool isTwisted() const { return u_closes_twisted; }

    // ==========================================================
    // EQUATIONS & CONSTRAINTS
    // ==========================================================
    void setEquations(const QString &x, const QString &y, const QString &z, const QString &p);

    void setConstraintMode(ConstraintMode mode);
    ConstraintMode getConstraintMode() const { return m_constraintMode; }

    void setExplicitU(const QString &eq);
    void setExplicitV(const QString &eq);
    void setExplicitW(const QString &eq);

    QString getExplicitU() const { return m_explicitU; }
    QString getExplicitV() const { return m_explicitV; }
    QString getExplicitW() const { return m_explicitW; }
    QString getActiveExplicitEquation() const;

    // ==========================================================
    // MATHEMATICAL RANGES & CONSTANTS
    // ==========================================================
    void setRangeU(float min, float max);
    void setRangeV(float min, float max);
    void setRangeW(float min, float max);
    void setConstants(float a, float b, float c, float d, float e, float f, float s);

    float getUMin() const { return uMin; }
    float getUMax() const { return uMax; }
    float getVMin() const { return vMin; }
    float getVMax() const { return vMax; }
    float getWMin() const { return wMin; }
    float getWMax() const { return wMax; }

    float getValA() const { return valA; }
    float getValB() const { return valB; }
    float getValC() const { return valC; }
    float getValD() const { return valD; }
    float getValE() const { return valE; }
    float getValF() const { return valF; }
    float getValS() const { return valS; }

    // ==========================================================
    // PATHS 3D & 4D (EVALUATION)
    // ==========================================================
    bool compilePathEquations(const QString &x, const QString &y, const QString &z, const QString &p,
                              const QString &alpha, const QString &beta, const QString &gamma);
    QVector4D evaluatePathPosition(float t);
    float evaluatePathAlpha(float t);
    float evaluatePathBeta(float t);
    float evaluatePathGamma(float t);

    bool compilePath3DEquations(const QString &x, const QString &y, const QString &z, const QString &r);
    QVector4D evaluatePath3DPosition(float t); // Restituisce (x, y, z, roll)

    // ==========================================================
    // 4D LIGHTING STATE (Used by GLWidget)
    // ==========================================================
    void set4DLighting(bool enable);
    bool is4DLightingEnabled() const { return use4DLighting; }

    // ==========================================================
    // SCRIPTING & SHADERS
    // ==========================================================
    void setScriptMode(bool active);
    bool isScriptModeActive() const { return m_useScriptMode; }

    void setScriptCodeGLSL(const QString& code) { m_glslCode = code; }
    QString getScriptCodeGLSL() const { return m_glslCode; }

private:

    // ==========================================================
    // MESH DATA
    // ==========================================================
    std::vector<Vertex> generatedVertices;
    std::vector<unsigned int> generatedIndices;
    int numU = 100, numV = 100;
    bool u_is_closed = false;
    bool v_is_closed = false;
    bool u_closes_twisted = false;

    // ==========================================================
    // EQUATIONS STATE
    // ==========================================================
    QString eqX, eqY, eqZ, eqP;
    QString m_explicitU, m_explicitV, m_explicitW;
    ConstraintMode m_constraintMode = ConstraintW;

    QString pathEqX, pathEqY, pathEqZ, pathEqP;
    QString pathEqAlpha, pathEqBeta, pathEqGamma;
    bool pathValid = false;

    QString pathEqX3D, pathEqY3D, pathEqZ3D, pathEqR3D;
    bool path3DValid = false;

    // ==========================================================
    // MATHEMATICAL VARIABLES
    // ==========================================================
    float valA = 1.0f, valB = 1.0f, valC = 1.0f;
    float valD = 1.0f, valE = 1.0f, valF = 1.0f;
    float valS = 1.0f;
    float uMin, uMax, vMin, vMax, wMin, wMax;

    bool use4DLighting = false;

    // ==========================================================
    // SCRIPTING STATE
    // ==========================================================
    QString m_glslCode;
    bool m_useScriptMode = false;

    // ==========================================================
    // EXPRTK PARSER ENVIRONMENT (Simboli Mappati in Memoria)
    // ==========================================================
    exprtk::symbol_table<float> m_pathSymbolTable;
    exprtk::symbol_table<float> m_surfaceSymbolTable;

    // Variabili fisiche registrate nella tabella (ExprTk punta qui)
    float m_varU = 0.0f;
    float m_varV = 0.0f;
    float m_varW = 0.0f;
    float m_varP = 0.0f;
    float m_varT = 0.0f;

    float m_varA = 1.0f, m_varB = 1.0f, m_varC = 1.0f;
    float m_varD = 1.0f, m_varE = 1.0f, m_varF = 1.0f;
    float m_varS = 0.0f;

    // ==========================================================
    // CACHED COMPILED EXPRESSIONS
    // ==========================================================
    CachedExpression m_exprSurfX, m_exprSurfY, m_exprSurfZ;
    CachedExpression m_exprPathX, m_exprPathY, m_exprPathZ, m_exprPathP;
    CachedExpression m_exprPathAlpha, m_exprPathBeta, m_exprPathGamma;
    CachedExpression m_exprPath3DX, m_exprPath3DY, m_exprPath3DZ, m_exprPath3DR;

    // ==========================================================
    // PRIVATE INTERNAL HELPERS
    // ==========================================================
    void compileSingleExpr(const QString &eqStr, CachedExpression &target, exprtk::parser<float> &parser);
    void generateParametricGrid();
    void detectMeshClosure();
};

#endif // SURFACEENGINE_H
