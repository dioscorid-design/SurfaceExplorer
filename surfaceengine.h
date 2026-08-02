#ifndef SURFACEENGINE_H
#define SURFACEENGINE_H

#include "expressionparser.h"
#include "geodesiccalculator.h"
#include <rhi/qrhi.h>
#include <vector>
#include <algorithm>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <QVector2D>

// ==========================================================
// CORE DATA STRUCTURES
// ==========================================================
struct Vertex {
    QVector4D position;
    QVector4D normal;
    QVector2D texCoord;
};

struct CachedExpression {
    exprtk::expression<float> expr;
    bool isValid = false;
};

// ==========================================================
// MULTI-MESH: una superficie puo' essere composta da PIU' griglie indipendenti
// ==========================================================
// Ogni MeshPart e' una griglia parametrica autonoma, con dominio e risoluzione
// propri. Il motivo di esistere: cucendo N rami in un'unica griglia il
// generatore di indici collega SEMPRE j a j+1, quindi alla cucitura unisce
// l'ultima riga di un ramo alla prima del ramo SUCCESSIVO — una "lamina di
// giunzione" fra due superfici diverse, che va poi mascherata nel cutout con
// una banda di guardia (e lascia un solco circolare). Con griglie separate
// nessun quad puo' collegare due rami: il difetto sparisce alla radice.
//
// I vertici di TUTTE le parti vivono in un unico buffer contiguo (un solo
// upload alla GPU); ogni parte sa dove inizia. Gli indici sono LOCALI alla
// parte (partono da 0) e la draw call li rebasa con vertexOffset: cosi' una
// parte puo' essere disegnata senza toccare le altre.
struct MeshPart {
    // Dominio parametrico coperto da questa parte. Finisce negli uniform
    // u_min/u_max/v_min/v_max (per-draw): il cutout, che ricalcola il punto da
    // (u,v), continua a funzionare senza modifiche allo script.
    float uMin = 0.0f, uMax = 1.0f;
    float vMin = 0.0f, vMax = 1.0f;

    // Risoluzione EFFETTIVA di questa parte, cioe' quella con cui la griglia
    // viene generata. NON e' il valore dichiarato nello script: e' quello
    // riscalato dallo slider Steps (vedi resolveMeshParts).
    int numU = 100, numV = 100;

    // Risoluzione DICHIARATA dalla sezione //MESH_BEGIN, conservata a parte.
    // Serve come PROPORZIONE, non come tetto: lo slider Steps governa la
    // risoluzione (vedi la memoria slider-steps-governa-risoluzione-script), e i
    // valori dichiarati dicono soltanto il rapporto fra le parti e fra u e v
    // (es. "in v il doppio dei passi che in u", oppure "questa parte meta'
    // dell'altra"). Va tenuta separata da numU/numV perche' il riscalamento
    // deve partire SEMPRE dal valore originale: riscalare un valore gia'
    // riscalato farebbe derivare la risoluzione a ogni movimento dello slider.
    int declaredU = 0, declaredV = 0;   // 0 = non dichiarata

    // Posizione nei buffer condivisi (in elementi, non byte).
    int vertexOffset = 0;   // primo vertice nel buffer comune
    int vertexCount  = 0;   // (numU+1)*(numV+1)
    int indexOffset  = 0;   // primo indice nel buffer comune
    int indexCount   = 0;   // numU*numV*6

    // Chiusura rilevata su QUESTA parte (prima era globale sul dominio intero,
    // quindi non sapeva chiudere i singoli rami).
    bool uClosed = false;
    bool vClosed = false;

    // Indice progressivo, esposto allo script come uniform u_meshIndex per
    // distinguere il ramo (es. quale toro di Clifford si sta generando).
    int meshIndex = 0;

    // ==========================================================
    // ASPETTO PER-PARTE (colore, trasparenza, luce, solid/wireframe)
    // ==========================================================
    // Ogni parte puo' avere il proprio aspetto. La convenzione e' che un valore
    // NEGATIVO significa "eredita dallo stato globale del renderer": cosi' una
    // parte non configurata si disegna esattamente come prima, e i preset che
    // non dichiarano nulla restano identici (nessuna regressione).
    //
    // Colore e alpha finiscono nei campi omonimi del blocco UBO, che e' gia'
    // per-parte (dynamic offset): scriverli costa una assegnazione, non una
    // modifica agli shader.
    float colorR = -1.0f, colorG = -1.0f, colorB = -1.0f;
    float alpha = -1.0f;          // < 0 = eredita
    float lightIntensity = -1.0f; // < 0 = eredita

    // MODALITA' DI RENDERING PROPRIA (0 = Base, 1 = Phong, 2 = Wireframe).
    // Qui la convenzione "negativo = eredita" NON si puo' esprimere col valore
    // stesso, perche' 0 (Base) e' un valore LEGITTIMO che updateRenderState
    // invia davvero: serve un flag separato. Vedi la nota su questo in
    // GLWidget::setRenderMode.
    int  renderMode = 0;
    bool hasCustomRenderMode = false;

    // Densita' wireframe propria (passi di campionamento U/V; meno passi = piu'
    // linee). 0 = eredita dai wfStepU/wfStepV globali di GLWidget. Senza questi
    // due campi due mesh in wireframe non si possono differenziare, perche'
    // buildWireframe applicava un unico stride globale a tutte le parti.
    int  wfStepU = 0;
    int  wfStepV = 0;

    // TEXTURE PROCEDURALE PROPRIA (script GLSL della sola parte).
    // Vuoto = eredita dallo stato globale, cioe' la texture della superficie se
    // accesa e nessuna texture altrimenti: una parte non configurata si disegna
    // esattamente come prima e i preset esistenti restano invariati.
    // Il codice NON e' una risorsa GPU: viene compilato dentro l'UNICO fragment
    // shader come getCustomColor_<k>(), scelto a run time su u_meshIndex (vedi
    // GLWidget::createFragmentShaderSource). Percio' le texture per-mesh non
    // costano ne' binding ne' pipeline in piu': solo codice nello stesso shader.
    QString textureCode;
    // Acceso/spento PROPRIO della parte. Come per renderMode, il solo valore non
    // basta a esprimere "eredita": una parte puo' voler spegnere la texture
    // mentre il globale la tiene accesa, e "spento" non si distingue da "non
    // configurato". Serve percio' il flag esplicito.
    bool textureEnabled = false;
    bool hasCustomTexture = false;
    // COLORI DELLA TEXTURE PROPRI DELLA PARTE (u_col1/u_col2 dello script).
    // Sono per-parte perche' vivono nel blocco UBO: senza di essi due mesh con
    // texture diverse condividevano i due slot globali, e l'ultima applicata
    // riscriveva i colori di tutte le altre.
    // Negativo = eredita i colori globali della texture di superficie.
    float texCol1R = -1.0f, texCol1G = -1.0f, texCol1B = -1.0f;
    float texCol2R = -1.0f, texCol2G = -1.0f, texCol2B = -1.0f;
    bool hasCustomTexColors() const { return texCol1R >= 0.0f; }

    // TRASFORMAZIONE 2D PROPRIA DELLA PARTE (zoom / pan / rotazione della
    // texture, quelle che si regolano col mouse in vista 2D).
    // Come i colori, finiscono nel blocco UBO e quindi possono differire fra le
    // parti: senza, zoom e pan restavano i tre membri GLOBALI del widget e
    // ridimensionare la texture di una mesh le ridimensionava TUTTE, compresa
    // quella dell'ambito All.
    // texZoom < 0 = eredita la trasformazione globale.
    float texZoom = -1.0f;
    float texPanX = 0.0f, texPanY = 0.0f;
    float texRotation = 0.0f;
    bool hasCustomTexTransform() const { return texZoom >= 0.0f; }

    bool hasCustomColor() const { return colorR >= 0.0f; }
    // Texture EFFICACE di una parte in una superficie MULTI-MESH.
    // Una parte MAI CONFIGURATA (hasCustomTexture == false) resta SENZA texture:
    // NON eredita quella globale. E' la differenza con colore/alpha/luce, ed e'
    // deliberata: applicare una texture in ambito "All" con altre fasce gia'
    // texturizzate significa "questa e' la texture della superficie", non
    // "riempi anche le fasce che ho lasciato apposta in tinta unita". Ereditando,
    // una fascia lasciata volutamente nuda si texturizzava da sola.
    // NB: vale solo in multi-mesh. Il caso a mesh singola non passa di qui (vedi
    // il ramo uboPartCount <= 1 in GLWidget), quindi li' la texture globale
    // continua a governare la superficie come da sempre.
    bool effectiveTextureEnabledMulti() const {
        return hasCustomTexture && textureEnabled;
    }
    // Texture EFFICACE: la propria se dichiarata, altrimenti quella globale.
    // Usata dal DISPLAY dei controlli e dal caso a mesh singola.
    bool effectiveTextureEnabled(bool globalOn) const {
        return hasCustomTexture ? textureEnabled : globalOn;
    }
    // Modalita' EFFICACE: la propria se dichiarata, altrimenti quella globale.
    int  effectiveRenderMode(int globalMode) const {
        return hasCustomRenderMode ? renderMode : globalMode;
    }
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
    void setCustomMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, bool isUClosed, bool isVClosed);


    const std::vector<Vertex>& getVertices() const { return generatedVertices; }
    const std::vector<unsigned int>& getIndices() const { return generatedIndices; }

    int getNumU() const { return numU; }
    int getNumV() const { return numV; }
    bool isUClosed() const { return u_is_closed; }
    bool isVClosed() const { return v_is_closed; }
    bool isTwisted() const { return u_closes_twisted; }

    // ==========================================================
    // MULTI-MESH
    // ==========================================================
    // Le parti dichiarate dallo script con le sezioni //MESH_BEGIN..//MESH_END.
    // Se lo script non ne dichiara nessuna, computeMesh() sintetizza UNA parte
    // sul dominio corrente (uMin..uMax x vMin..vMax, numU x numV): il caso a
    // mesh singola resta identico a prima, stessi vertici e stessi indici.
    // Le sezioni //MESH_BEGIN portano DOMINIO e RISOLUZIONE, non l'aspetto:
    // colore, alpha, luce, modalita' e densita' wireframe sono scelte dell'utente
    // (spinbox del dock renderer) o del preset, e vanno PRESERVATE quando lo
    // script viene ri-estratto. Senza questo, ogni ri-parse azzerava l'aspetto
    // di tutte le parti: muovendo una costante che rigenera la geometria (es. lo
    // slider E dei tori di Hopf) le mesh tornavano tutte al colore globale.
    // L'aspetto si conserva per INDICE: se lo script dichiara piu' parti di
    // prima, quelle nuove partono con i valori di default (= eredita).
    void setMeshParts(const std::vector<MeshPart>& parts) {
        std::vector<MeshPart> next = parts;
        const int n = std::min((int)next.size(), (int)m_declaredParts.size());
        for (int k = 0; k < n; ++k) {
            const MeshPart &old = m_declaredParts[k];
            next[k].colorR = old.colorR;
            next[k].colorG = old.colorG;
            next[k].colorB = old.colorB;
            next[k].alpha = old.alpha;
            next[k].lightIntensity = old.lightIntensity;
            next[k].renderMode = old.renderMode;
            next[k].hasCustomRenderMode = old.hasCustomRenderMode;
            next[k].wfStepU = old.wfStepU;
            next[k].wfStepV = old.wfStepV;
            next[k].textureCode = old.textureCode;
            next[k].textureEnabled = old.textureEnabled;
            next[k].hasCustomTexture = old.hasCustomTexture;
            next[k].texCol1R = old.texCol1R;
            next[k].texCol1G = old.texCol1G;
            next[k].texCol1B = old.texCol1B;
            next[k].texCol2R = old.texCol2R;
            next[k].texCol2G = old.texCol2G;
            next[k].texCol2B = old.texCol2B;
            next[k].texZoom = old.texZoom;
            next[k].texPanX = old.texPanX;
            next[k].texPanY = old.texPanY;
            next[k].texRotation = old.texRotation;
        }
        m_declaredParts = std::move(next);
    }
    void clearMeshParts() { m_declaredParts.clear(); }
    const std::vector<MeshPart>& getMeshParts() const { return m_meshParts; }
    int getMeshPartCount() const { return (int)m_meshParts.size(); }

    // Aspetto di una parte modificabile DAL VIVO, senza rigenerare la griglia:
    // colore/alpha/luce finiscono nell'UBO, cioe'
    // nulla che dipenda dai vertici. Scrive su ENTRAMBE le liste, perche' le
    // dichiarate sopravvivono a un cambio di risoluzione o dominio mentre le
    // generate sono quelle che il render legge: se aggiornassi solo le seconde,
    // il primo computeMesh() riporterebbe l'aspetto ai valori dello script.
    MeshPart* mutableMeshPart(int index) {
        if (index < 0 || index >= (int)m_meshParts.size()) return nullptr;
        return &m_meshParts[index];
    }
    // Ricopia l'aspetto dalle parti generate a quelle dichiarate, cosi' una
    // modifica fatta dall'UI sopravvive alla prossima rigenerazione.
    void syncPartAppearance() {
        const int n = std::min((int)m_meshParts.size(), (int)m_declaredParts.size());
        for (int k = 0; k < n; ++k) {
            m_declaredParts[k].colorR = m_meshParts[k].colorR;
            m_declaredParts[k].colorG = m_meshParts[k].colorG;
            m_declaredParts[k].colorB = m_meshParts[k].colorB;
            m_declaredParts[k].alpha = m_meshParts[k].alpha;
            m_declaredParts[k].lightIntensity = m_meshParts[k].lightIntensity;
            m_declaredParts[k].renderMode = m_meshParts[k].renderMode;
            m_declaredParts[k].hasCustomRenderMode = m_meshParts[k].hasCustomRenderMode;
            m_declaredParts[k].wfStepU = m_meshParts[k].wfStepU;
            m_declaredParts[k].wfStepV = m_meshParts[k].wfStepV;
            m_declaredParts[k].textureCode = m_meshParts[k].textureCode;
            m_declaredParts[k].textureEnabled = m_meshParts[k].textureEnabled;
            m_declaredParts[k].hasCustomTexture = m_meshParts[k].hasCustomTexture;
            m_declaredParts[k].texCol1R = m_meshParts[k].texCol1R;
            m_declaredParts[k].texCol1G = m_meshParts[k].texCol1G;
            m_declaredParts[k].texCol1B = m_meshParts[k].texCol1B;
            m_declaredParts[k].texCol2R = m_meshParts[k].texCol2R;
            m_declaredParts[k].texCol2G = m_meshParts[k].texCol2G;
            m_declaredParts[k].texCol2B = m_meshParts[k].texCol2B;
            m_declaredParts[k].texZoom = m_meshParts[k].texZoom;
            m_declaredParts[k].texPanX = m_meshParts[k].texPanX;
            m_declaredParts[k].texPanY = m_meshParts[k].texPanY;
            m_declaredParts[k].texRotation = m_meshParts[k].texRotation;
        }
    }

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

    QVector<QVector<QVector4D>> computeGeodesicFlow(
        QRhi* rhi,
        const QString& eqX, const QString& eqY, const QString& eqZ, const QString& eqP, // <-- Aggiunto eqP
        const QString& eqU, const QString& eqV, const QString& eqW,
        const QString& eqDu, const QString& eqDv, const QString& eqDw, const QString& eqLambda,
        const QString& eqMetric, // corpo GLSL mat3(U,V,W); vuoto = metrica dall'embedding
        float uMin, float uMax, int numU,
        float vMin, float vMax, int numV,
        const QMap<QString, float>& constants,
        float currentT,
        QString* outErrorMsg = nullptr);

    void setTime(float t);

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

    bool isMeshValid() const;

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

    // Sezione opzionale //CUTOUT_BEGIN..//CUTOUT_END dello script (dock Script):
    // corpo di bool cutHere(float u, float v), iniettato nel fragment shader per
    // scartare (discard) le pareti interne nei punti di autointersezione. Vuoto =
    // nessun taglio (retrocompatibile con ogni preset che non la definisce).
    void setCutoutCodeGLSL(const QString& code) { m_cutoutCode = code; }
    QString getCutoutCodeGLSL() const { return m_cutoutCode; }

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

    // Parti dichiarate dallo script (input) e parti effettivamente generate con
    // gli offset nei buffer (output di generateParametricGrid). Sono distinte
    // perche' le dichiarate sopravvivono a un cambio di risoluzione o dominio,
    // mentre le generate vanno ricalcolate a ogni computeMesh().
    std::vector<MeshPart> m_declaredParts;
    std::vector<MeshPart> m_meshParts;

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
    QString m_cutoutCode;
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

    float m_varU_comp = 0.0f;
    float m_varV_comp = 0.0f;
    float m_varW_comp = 0.0f;

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
    // GEODESIC FLOW CACHE (GPU pipeline persistente)
    // ==========================================================
    GeodesicCalculator m_geoCalc;

    // ==========================================================
    // PRIVATE INTERNAL HELPERS
    // ==========================================================
    // --- Mesh Generation & Analysis ---
    void generateParametricGrid();
    void detectMeshClosure();
    // Chiusura di una singola parte: valuta le equazioni agli estremi del
    // sotto-dominio della parte, non del dominio globale.
    void detectPartClosure(MeshPart& part);
    // Costruisce l'elenco di parti da generare: quelle dichiarate dallo script,
    // oppure una parte sola sul dominio corrente (comportamento storico).
    std::vector<MeshPart> resolveMeshParts() const;

    // --- Expression Parsing ---
    void compileSingleExpr(const QString &eqStr, CachedExpression &target, exprtk::parser<float> &parser);
};

#endif // SURFACEENGINE_H
