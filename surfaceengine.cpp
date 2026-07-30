#include "surfaceengine.h"
#include "geodesiccalculator.h"

#include <cmath>
#include <algorithm>
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
    m_surfaceSymbolTable.add_variable("U", m_varU_comp);
    m_surfaceSymbolTable.add_variable("V", m_varV_comp);
    m_surfaceSymbolTable.add_variable("W", m_varW_comp);
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
    m_meshParts.clear();
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

void SurfaceEngine::setCustomMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, bool isUClosed, bool isVClosed)
{
    generatedVertices = vertices;
    generatedIndices = indices;

    // Ora rispettiamo la vera topologia della mesh geodetica
    u_is_closed = isUClosed;
    v_is_closed = isVClosed;

    // Mesh custom (flusso geodetico): una parte sola che copre tutto il buffer.
    // Senza questo il loop di draw non troverebbe parti e non disegnerebbe
    // nulla. I vertici qui sono POSIZIONI, non parametri (#define CUSTOM_MESH),
    // quindi il dominio non serve a ricalcolare punti: teniamo quello corrente.
    m_meshParts.clear();
    MeshPart part;
    part.uMin = uMin; part.uMax = uMax;
    part.vMin = vMin; part.vMax = vMax;
    part.numU = numU; part.numV = numV;
    part.vertexOffset = 0;
    part.vertexCount  = (int)generatedVertices.size();
    part.indexOffset  = 0;
    part.indexCount   = (int)generatedIndices.size();
    part.uClosed = isUClosed;
    part.vClosed = isVClosed;
    part.meshIndex = 0;
    m_meshParts.push_back(part);
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
QVector<QVector<QVector4D>> SurfaceEngine::computeGeodesicFlow(
    QRhi* rhi,
    const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP,
    const QString& eqU, const QString& eqV, const QString& eqW,
    const QString& eqDu, const QString& eqDv, const QString& eqDw, const QString& eqLambda,
    const QString& eqMetric,
    float uMin, float uMax, int numU,
    float vMin, float vMax, int numV,
    const QMap<QString, float>& constants,
    float currentT,
    QString* outErrorMsg)
{
    return m_geoCalc.computeGeodesicFlow(
        rhi,
        eqX, eqY, eqZ, eqP, eqU, eqV, eqW, eqDu, eqDv, eqDw, eqLambda, eqMetric,
        uMin, uMax, numU, vMin, vMax, numV, constants,
        currentT,
        outErrorMsg);
}

void SurfaceEngine::setTime(float t) {
    m_varT = t;
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

bool SurfaceEngine::isMeshValid() const {
    if (generatedVertices.empty()) return false;

    float limit = 100000.0f;

    for (const Vertex& v : generatedVertices) {
        if (std::isnan(v.position.x()) || std::isinf(v.position.x()) || std::abs(v.position.x()) > limit ||
            std::isnan(v.position.y()) || std::isinf(v.position.y()) || std::abs(v.position.y()) > limit ||
            std::isnan(v.position.z()) || std::isinf(v.position.z()) || std::abs(v.position.z()) > limit ||
            std::isnan(v.position.w()) || std::isinf(v.position.w()) || std::abs(v.position.w()) > limit) {

            return false;
        }
    }

    return true;
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

// --- Mesh Generation & Analysis ---

std::vector<MeshPart> SurfaceEngine::resolveMeshParts() const
{
    // Nessuna sezione //MESH_BEGIN nello script: una parte sola sul dominio
    // corrente. Coincide esattamente con la griglia storica, quindi ogni preset
    // che non dichiara parti genera gli stessi vertici e gli stessi indici.
    if (m_declaredParts.empty()) {
        MeshPart single;

        // Gli assi della griglia dipendono dal vincolo attivo, come prima.
        if (m_constraintMode == ConstraintU) {
            single.uMin = vMin; single.uMax = vMax;
            single.vMin = wMin; single.vMax = wMax;
        } else if (m_constraintMode == ConstraintV) {
            single.uMin = uMin; single.uMax = uMax;
            single.vMin = wMin; single.vMax = wMax;
        } else {
            single.uMin = uMin; single.uMax = uMax;
            single.vMin = vMin; single.vMax = vMax;
        }

        single.numU = numU;
        single.numV = numV;
        single.meshIndex = 0;
        return { single };
    }

    // Parti dichiarate dallo script.
    //
    // LO SLIDER STEPS GOVERNA LA RISOLUZIONE (vedi la memoria
    // slider-steps-governa-risoluzione-script: la direttiva `steps :=` e' inerte
    // e comanda solo lo slider). I passi dichiarati nelle sezioni //MESH_BEGIN
    // valgono quindi come PROPORZIONE, non come tetto.
    //
    // Prima qui c'era `min(dichiarato, numU)`, che rendeva il valore dichiarato
    // un tetto assoluto: con "u: 0, TAU, 100" lo slider agiva solo fino a 100 e
    // oltre non cambiava piu' nulla. Era un bug: le superfici multi-mesh
    // ignoravano gran parte della corsa dello slider.
    //
    // Ora si riscala: la parte con piu' passi dichiarati prende esattamente il
    // valore dello slider, le altre restano in proporzione. Cosi'
    //   - i rapporti voluti dallo script (fra parti, e fra u e v) sono rispettati
    //   - la corsa INTERA dello slider produce effetto
    //   - il costo totale resta governato dallo slider, come per le mesh singole.
    std::vector<MeshPart> parts = m_declaredParts;

    // Riferimento = massimo dichiarato su tutte le parti e su entrambi gli assi.
    int declMax = 0;
    for (const MeshPart& p : parts) {
        declMax = std::max(declMax, std::max(p.declaredU, p.declaredV));
    }

    for (int k = 0; k < (int)parts.size(); ++k) {
        MeshPart& p = parts[k];
        p.meshIndex = k;

        // Una parte senza risoluzione dichiarata segue lo slider su entrambi gli
        // assi (comportamento naturale: "come la superficie intera").
        if (declMax <= 0) {
            p.numU = std::max(1, numU);
            p.numV = std::max(1, numV);
            continue;
        }

        const int dU = (p.declaredU > 0) ? p.declaredU : declMax;
        const int dV = (p.declaredV > 0) ? p.declaredV : declMax;

        // Riscalamento proporzionale sul valore ORIGINALE dichiarato: parte
        // sempre da lì, quindi muovere lo slider avanti e indietro riporta
        // esattamente agli stessi valori (nessuna deriva).
        // I due assi usano lo stesso riferimento (numU e numV coincidono: lo
        // slider Steps e' unico), ma restano campi distinti per non legarli.
        p.numU = std::max(1, (int)std::lround((double)dU * (double)numU / (double)declMax));
        p.numV = std::max(1, (int)std::lround((double)dV * (double)numV / (double)declMax));
    }
    return parts;
}

void SurfaceEngine::generateParametricGrid()
{
    // 1. Pulizia dei buffer
    generatedVertices.clear();
    generatedIndices.clear();
    m_meshParts.clear();

    m_meshParts = resolveMeshParts();

    // Dimensiona una volta i buffer condivisi: i vertici di TUTTE le parti
    // vivono contigui, cosi' la GPU riceve un solo upload e ogni parte viene
    // disegnata con un offset (vedi MeshPart in surfaceengine.h).
    int totalVerts = 0, totalIdx = 0;
    for (const MeshPart& p : m_meshParts) {
        totalVerts += (p.numU + 1) * (p.numV + 1);
        totalIdx   += p.numU * p.numV * 6;
    }
    generatedVertices.reserve(totalVerts);
    generatedIndices.reserve(totalIdx);

    // 2. Generazione, una parte per volta
    for (MeshPart& part : m_meshParts) {
        const int pNumU = part.numU;
        const int pNumV = part.numV;

        part.vertexOffset = (int)generatedVertices.size();
        part.indexOffset  = (int)generatedIndices.size();

        const float start1 = part.uMin, end1 = part.uMax;
        const float start2 = part.vMin, end2 = part.vMax;

        // 2a. Vertici. position.xy porta i PARAMETRI (non le coordinate): il
        // vertex shader ne ricava la posizione con getRawPosition(u,v).
        for (int i = 0; i <= pNumU; ++i) {
            float val1 = start1 + (float)i / pNumU * (end1 - start1);

            for (int j = 0; j <= pNumV; ++j) {
                float val2 = start2 + (float)j / pNumV * (end2 - start2);

                QVector3D paramPos(val1, val2, 0.0f);

                Vertex vert;
                vert.position = QVector4D(paramPos, 1.0f);
                vert.normal = QVector4D(0.0f, 0.0f, 1.0f, 0.0f);
                // texCoord resta normalizzata 0..1 SULLA PARTE: il fragment
                // ricostruisce (u,v) interpolando gli uniform u_min/u_max
                // (v_texCoord), che sono per-draw e valgono il sotto-dominio di
                // questa parte. Usare coordinate globali sfalserebbe il cutout.
                vert.texCoord = QVector2D((float)i/pNumU, (float)j/pNumV);

                generatedVertices.push_back(vert);
            }
        }

        // 2b. Indici, LOCALI alla parte (0..vertexCount-1). Il quad fra j e j+1
        // non puo' piu' scavalcare su un'altra parte: il loop e' confinato alla
        // griglia corrente. E' qui che sparisce la lamina di giunzione.
        for (int i = 0; i < pNumU; ++i) {
            for (int j = 0; j < pNumV; ++j) {
                int p0 = i * (pNumV + 1) + j;
                int p1 = (i + 1) * (pNumV + 1) + j;
                int p2 = i * (pNumV + 1) + (j + 1);
                int p3 = (i + 1) * (pNumV + 1) + (j + 1);

                generatedIndices.push_back(p0);
                generatedIndices.push_back(p1);
                generatedIndices.push_back(p2);

                generatedIndices.push_back(p2);
                generatedIndices.push_back(p1);
                generatedIndices.push_back(p3);
            }
        }

        part.vertexCount = (int)generatedVertices.size() - part.vertexOffset;
        part.indexCount  = (int)generatedIndices.size()  - part.indexOffset;
    }

    detectMeshClosure();
}

// Chiusura di UNA parte: confronta gli estremi del sotto-dominio della parte.
// Prima il test era solo globale, quindi con N rami cuciti in una griglia sola
// non poteva sapere se il singolo ramo si chiudeva: e' il motivo per cui
// u_is_closed/v_is_closed "non aiutavano" nel caso multi-ramo.
void SurfaceEngine::detectPartClosure(MeshPart& part)
{
    part.uClosed = false;
    part.vClosed = false;

    if (m_useScriptMode) return;
    if (!m_exprSurfX.isValid || !m_exprSurfY.isValid || !m_exprSurfZ.isValid) return;

    const float threshold = 0.001f;

    const float start1 = part.uMin, end1 = part.uMax;
    const float start2 = part.vMin, end2 = part.vMax;

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
    part.uClosed = (pStart1.distanceToPoint(pEnd1) < threshold);

    float mid1 = (start1 + end1) * 0.5f;
    QVector3D pStart2 = evalPoint(mid1, start2);
    QVector3D pEnd2   = evalPoint(mid1, end2);
    part.vClosed = (pStart2.distanceToPoint(pEnd2) < threshold);
}

void SurfaceEngine::detectMeshClosure()
{
    for (MeshPart& part : m_meshParts) {
        detectPartClosure(part);
    }

    // I flag globali restano quelli della PRIMA parte: nel caso a mesh singola
    // (che e' ogni preset senza //MESH_BEGIN) sono identici a prima. Con piu'
    // parti la chiusura e' una proprieta' per-parte e va letta da getMeshParts().
    if (!m_meshParts.empty()) {
        u_is_closed = m_meshParts[0].uClosed;
        v_is_closed = m_meshParts[0].vClosed;
    } else {
        u_is_closed = false;
        v_is_closed = false;
    }
}

 // --- Expression Parsing ---

void SurfaceEngine::compileSingleExpr(const QString &eqStr, CachedExpression &target, exprtk::parser<float> &parser)
{
    target.isValid = false;
    if (eqStr.trimmed().isEmpty()) return;

    target.expr.register_symbol_table(m_pathSymbolTable);

    if (parser.compile(eqStr.toStdString(), target.expr)) {
        target.isValid = true;
    }
}




