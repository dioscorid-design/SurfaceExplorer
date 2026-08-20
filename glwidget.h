#ifndef GLWIDGET_H
#define GLWIDGET_H

#include "surfaceengine.h"
#include "geometrybuilder.h"   // WireframeRange (intervalli per parte di mesh)

#include <QWidget>
#include <QRhiWidget>
#include <private/qrhi_p.h>
#include <rhi/qshader.h>
#include <rhi/qshaderbaker.h>
#include <QTimer>
#include <memory>
#include <functional>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QQuaternion>

class InputHandler;
class SurfaceEngine;

struct UboData {
    float mvpMatrix[16];
    float mvMatrix[16];
    float mMatrix[16];
    QVector4D dummyZero;
    QVector4D observerPos;
    QVector4D cameraPos4D;
    QVector4D mathParams;
    QVector4D mathParams2;
    QVector3D color;
    float alpha;
    QVector3D col1;
    float lightIntensity;
    QVector3D col2;
    float zoom;
    QVector2D center;
    float rotation;
    float omega;
    float phi;
    float psi;
    float time;
    int projMode;
    int lightingMode;
    int renderMode;
    int isFlat;
    int useTexture;
    int useSpecular;
    float u_min;
    float u_max;
    float v_min;
    float v_max;
    int hasExplicitW;
    int u_raySteps;
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    float z_min;
    float z_max;
    // MULTI-MESH: indice della parte in corso di disegno, esposto allo script
    // come u_meshIndex per distinguere il ramo (es. quale toro di Clifford).
    // E' float e non int per essere usabile direttamente nelle espressioni dello
    // script senza conversioni. Vale 0 per le superfici a mesh singola.
    float u_meshIndex;
    // 1 = nessuna immagine caricata nello slot sampler (c'e' solo la texture
    // "tappabuchi"). Gli script che campionano iChannel0 -- gli "Animated
    // Images" -- usano questo flag per cadere su una SCACCHIERA PROCEDURALE
    // mix(u_col1,u_col2) invece dei pixel del fallback: cosi' i picker Color1/2
    // agiscono davvero. Con un'immagine caricata vale 0 e non cambia nulla.
    // Occupa il primo slot dell'ex _pad0 (la coda di riserva), quindi il blocco
    // non cresce e gli offset precedenti restano tutti invariati.
    int u_noImage;
    // Coda di riserva. Gli offset di questa struct combaciano con il blocco
    // SceneUBO degli shader: verificato con `qsb --dump` che u_min=372,
    // z_max=416, u_meshIndex=420 su entrambi i lati (blocco shader = 424 byte).
    // Lo spazio fra i blocchi NON e' sizeof(UboData): il passo e' m_uboBlockStride,
    // ricavato da QRhi::ubufAlignment() (256 su Metal/Vulkan), quindi ogni blocco
    // e' comunque allineato come richiede l'API.
    float _pad0[2];
};

class GLWidget : public QRhiWidget
{
    Q_OBJECT

public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget();

    QRhi* getRhi() { return rhi(); }

    // ==========================================================
    // ENUMS & CONSTANTS
    // ==========================================================
    static const int Ortho4D = 0;
    static const int Perspective4D = 1;
    static const int PerspectiveWide4D = 2;

    enum MoveDir {
        MoveForward, MoveBack, MoveLeft, MoveRight,
        MoveUp, MoveDown,
        RollLeft, RollRight,
        ObsMoveXPos, ObsMoveXNeg,
        ObsMoveYPos, ObsMoveYNeg,
        ObsMoveZPos, ObsMoveZNeg,
        ObsMovePPos, ObsMovePNeg,
        RotOmegaPos, RotOmegaNeg,
        RotPhiPos,   RotPhiNeg,
        RotPsiPos,   RotPsiNeg
    };


    // ==========================================================
    // ENGINE MODES
    // ==========================================================
    enum EngineMode {
        ModeParametric = 0,
        ModeImplicit   = 1
    };

    void setEngineMode(EngineMode mode);
    EngineMode getEngineMode() const { return m_engineMode; }

    // true se il campo implicito corrente e' a prodotto/mal condizionato (es. "Chain"):
    // con alpha<1 sparisce. Su TUTTE le piattaforme la UI disabilita lo slider e il
    // render forza opaco. Vedi m_implicitIllConditioned.
    bool isImplicitIllConditioned() const { return m_implicitIllConditioned; }
    // Azzera il flag per gli script impliciti (GLSL grezzo non valutabile su CPU):
    // il ramo di load che applica lo script via setScriptCodeGLSL non passa da
    // validateAndApplyImplicitScript, che lo azzererebbe. Vedi il chiamante.
    void clearImplicitIllConditioned() { m_implicitIllConditioned = false; }

    // SOLO ANDROID: true se la trasparenza di questa superficie implicita potrebbe
    // degradare (superficie triplamente periodica come Gyroid, o QUALSIASI script RM
    // non valutabile su CPU) per via del budget facce ridotto su Android. A differenza
    // di isImplicitIllConditioned NON blocca e NON forza opaco: lo slider resta usabile,
    // la UI mostra solo un popup di avviso al primo tocco. Sempre false su desktop/iOS.
    bool implicitTransparencyMayDegrade() const { return m_implicitTransparencyWarn; }
    void setImplicitTransparencyWarn(bool w) { m_implicitTransparencyWarn = w; }


    // ==========================================================
    // EQUATIONS & MATHEMATICS
    // ==========================================================
    bool setParametricEquations(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    void setImplicitEquation(const QString &eqF);
    void setEquationConstants(float a, float b, float c, float d, float e, float f, float s);
    void setRangeU(float min, float max);
    void setRangeV(float min, float max);
    void setRangeW(float min, float max);
    void setRangeX(float min, float max);
    void setRangeY(float min, float max);
    void setRangeZ(float min, float max);
    void setResolution(int n);
    void setRaySteps(int steps);
    bool setCustomMesh(const QVector<QVector<QVector4D>>& grid, bool tolerateTruncated = false);
    const QMap<QString, float>& getConstantsMap() const { return m_constants; }

    SurfaceEngine* getEngine() const { return engine.get(); }

    // Campo implicito attualmente compilato nel ray marcher.
    QString implicitEquation() const { return m_eqImplicitF; }


    // ==========================================================
    // RENDERING & VISUALS
    // ==========================================================
    void updateSurfaceData();
    // SCENA VUOTA: svuota la geometria e la sua ombra wireframe, senza
    // ricalcolare nulla. updateSurfaceData() non va bene qui -- chiama
    // computeMesh(), che rigenererebbe la griglia parametrica dalle equazioni.
    // Il wireframe va azzerato INSIEME alla mesh e non prima: e' costruito da
    // engine (buildWireframe), quindi un buildWireframeGeometry() chiamato
    // mentre la mesh vecchia e' ancora in pancia -- come fa
    // resetWireframeDensity() piu' a monte nel reset -- lo ripopola con le
    // linee della superficie che stiamo buttando via, e a schermo restano.
    void clearSceneGeometry() {
        if (engine) engine->clear();
        m_wireframeIndices.clear();
        m_wireframeRanges.clear();
        m_wireframeIndexCount = 0;
        meshNeedsUpdate = true;
        wireframeNeedsUpdate = true;
        update();
    }
    void resetVisuals();
    void setProjectionMode(int mode);
    // ASPETTO PER-MESH: se una parte e' selezionata con lo spinbox del dock
    // renderer, setColor/setAlpha/setLightIntensity scrivono su QUELLA parte
    // invece che sullo stato globale (vedi applyToActiveMeshPart).
    //
    // I SETTER DI GLWIDGET SONO DI DUE FAMIGLIE, con contratti OPPOSTI. Sbagliare
    // famiglia e' la radice di un'intera serie di bug (il colore del preset che
    // finiva dentro una fascia, il wireframe di una fascia che spegneva la
    // texture di tutta la superficie, i colori di una texture per-mesh addosso a
    // quella globale). Il prefisso nel nome dice a quale appartengono:
    //
    //   setColor / setAlpha / setLightIntensity
    //       DIROTTANO sulla parte attiva. Per scrivere davvero il globale (stato
    //       del preset, reset automatici del motore) serve il BYPASS.
    //   setGlobal* (setGlobalRenderMode / setGlobalTextureEnabled /
    //   setGlobalTextureColors)
    //       scrivono SEMPRE lo stato globale, qualunque mesh sia selezionata.
    //       Chi sta agendo su una fascia NON deve chiamarli: esistono i
    //       setActiveMesh* corrispondenti.
    //   setActiveMesh* (setActiveMeshRenderMode / setActiveMeshTexture /
    //   setActiveMeshTexColors / setActiveMeshWireframeDensity)
    //       scrivono SOLO sulla parte; tornano false in "All", dove il chiamante
    //       deve ricadere sul setGlobal* corrispondente.
    void setActiveMeshPart(int index);
    // Sospende temporaneamente il dirottamento sulla mesh attiva: i reset
    // AUTOMATICI del motore (es. il ripristino di alpha/luce quando si entra in
    // wireframe) devono agire sullo stato GLOBALE, non essere scambiati per una
    // scelta dell'utente su quella parte. Vedi MeshAppearanceBypass.
    void setMeshAppearanceBypass(bool on) { m_meshAppearanceBypass = on; }
    bool meshAppearanceBypass() const { return m_meshAppearanceBypass; }
    // AMBITO "ALL": la superficie si comporta come UNA SOLA. L'aspetto proprio
    // delle parti viene IGNORATO dal render (non cancellato), quindi colore,
    // trasparenza, luce e wireframe globali valgono per tutte. Tornando su
    // "Mesh" le differenze ricompaiono: i valori sono rimasti in MeshPart.
    // NB: ricostruisce la geometria delle linee, perche' anche le DENSITA'
    // per-parte vengono sospese (sono indici, non uniform).
    // NB2: ricompila anche lo SHADER, perche' in "All" le texture per-mesh sono
    // sospese e il dispatcher su u_meshIndex non va nemmeno generato (vedi
    // createFragmentShaderSource). Definita nel .cpp: rebuildShader non e'
    // visibile a questo punto dell'header.
    void setMeshAppearanceUniform(bool on);
    bool meshAppearanceUniform() const { return m_meshAppearanceUniform; }
    int activeMeshPart() const { return m_activeMeshPart; }
    int meshPartCount() const { return engine ? engine->getMeshPartCount() : 0; }

    void setGlobalRenderMode(int mode);
    // Modalita' di rendering GLOBALE (0=Base, 1=Phong, 2=Wireframe). E' la fonte
    // di verita' per cio' che una parte "eredita": i radio dell'interfaccia NON
    // lo sono, perche' mostrano la modalita' della mesh selezionata.
    int globalRenderMode() const { return renderMode; }
    // Colore GLOBALE della superficie, come globalRenderMode: e' quello che una
    // parte SENZA colore proprio sta mostrando, e quindi quello che i controlli
    // devono visualizzare quando si seleziona una mesh che eredita.
    void globalColor(float &r, float &g, float &b) const { r = red; g = green; b = blue; }
    // Trasparenza e luce GLOBALI, stessa logica: sono i valori in vigore in
    // ambito "All" e quelli che una parte senza valore proprio eredita.
    float globalAlpha() const { return alpha; }
    float globalLightIntensity() const { return m_lightIntensity; }
    // COLORI u_col1/u_col2 della texture di SUPERFICIE. Sono i due slot globali,
    // che il ramo per-mesh non tocca mai: restano quindi la fonte di verita' per
    // ripristinare il DISPLAY tornando in ambito "All" dopo essere passati da una
    // fascia (che sovrascrive i membri m_texColor1/2 di MainWindow coi propri).
    QColor globalTexColor1() const { return QColor::fromRgbF(texRed1, texGreen1, texBlue1); }
    QColor globalTexColor2() const { return QColor::fromRgbF(texRed2, texGreen2, texBlue2); }
    // COMANDO esplicito dell'utente sulla mesh selezionata: e' l'UNICA via che
    // scrive una modalita' PROPRIA su una parte. setGlobalRenderMode() (chiamata da
    // updateRenderState a ogni cambio tab/proiezione/load) resta invece sempre
    // GLOBALE: e' la separazione fra display e comando che rende stabile il
    // wireframe per-mesh. Con "All" selezionato ricade sul globale.
    void setActiveMeshRenderMode(int mode);

    // TEXTURE PROCEDURALE PER-MESH. Ritornano true se hanno scritto su una
    // parte; false in "All", dove il chiamante deve applicare al globale.
    bool setActiveMeshTexture(const QString &code, bool enabled);
    // Spegne/riaccende la texture della parte CONSERVANDO lo script: lo spegnimento
    // NON deve passare da setActiveMeshTexture, che e' la via di COMANDO e
    // riscriverebbe textureCode forzando hasCustomTexture=true (rimetterebbe "in
    // vita" la texture che si sta togliendo). Usato dal wireframe, che la texture
    // non la disegna, e dal checkbox. True se ha cambiato qualcosa.
    bool setActiveMeshTextureEnabled(bool on);
    // COLORI u_col1/u_col2 della parte selezionata. Stesso contratto degli altri
    // setActiveMesh*: true se ha scritto su una parte, false in "All", dove il
    // chiamante deve usare setGlobalTextureColors (che e' SEMPRE globale).
    // Senza questa via, muovere gli slider colore su una fascia texturizzata
    // passava da setGlobalTextureColors e riscriveva i due slot GLOBALI: cambiava i
    // colori della texture di superficie e di ogni altra fascia che li eredita.
    bool setActiveMeshTexColors(const QColor &c1, const QColor &c2);
    // Zoom/pan/rotazione 2D della parte attiva, da FUORI la vista 2D: la usa chi
    // APPLICA una texture per darle l'inquadratura del suo preset, come il ramo
    // globale fa con setFlatZoom/setFlatPan/setFlatRotation. True se ha scritto
    // su una parte, false in "All".
    bool setActiveMeshTexTransform(float zoom, const QVector2D &pan, float rotation);
    // OROLOGIO TEXTURE PER-MESH (Stop/Run della sola parte selezionata).
    // Contratto degli altri setActiveMesh*: true se ha scritto su una parte,
    // false in "All", dove decide il chiamante.
    bool setActiveMeshTextureAnimating(bool animating);
    // Scrive l'orologio su TUTTE le parti (ambito "All" e master Start/Stop).
    // ADOZIONE ESPLICITA: nessuna parte eredita il clock globale, o ne
    // erediterebbe anche il freeze. Vedi la nota su timeTex in MeshPart.
    void setAllMeshTexturesAnimating(bool animating);
    // Stato dell'orologio della parte attiva: e' cio' che i tasti Run/Stop
    // mostrano in ambito "Mesh".
    bool isActiveMeshTextureAnimating() const;
    // C'e' almeno una parte con texture propria, accesa e in movimento?
    // Il master button la considera attivita' da fermare/riavviare.
    bool anyMeshTextureAnimating() const;
    QString activeMeshTextureCode() const;
    // Come sopra, ma vuoto se la texture della parte e' SPENTA (lo script resta
    // conservato). E' cio' che l'editor deve mostrare: vedi la nota nel .cpp.
    QString activeMeshEffectiveTextureCode() const;
    // Texture EFFICACE della parte attiva (propria E accesa): e' cio' che il
    // render disegna, quindi decide se gli slider colore editano u_col1/u_col2
    // o la tinta della superficie, e se il tasto 2D ha qualcosa da mostrare.
    bool activeMeshTextureActive() const;
    // Torna a ereditare dal globale (voce "Inherit" del selettore).
    void clearActiveMeshRenderMode();
    // Modalita' EFFICACE della parte selezionata (globale se "All"/eredita):
    // e' cio' che i radio devono MOSTRARE.
    int  activeMeshEffectiveRenderMode() const;
    // Congela nelle parti che EREDITANO la modalita' globale attuale, rendendola
    // loro (hasCustomRenderMode = true) senza cambiare cio' che si vede adesso.
    // Serve prima di cambiare il globale dall'ambito "All": senza questo passo le
    // parti che ereditano seguirebbero il nuovo globale e l'aspetto misto
    // impostato per-mesh andrebbe perso. Chi ha gia' una modalita' propria non
    // viene toccato. Vedi la nota in MainWindow::onUserRenderModeChosen.
    void pinInheritedRenderModes();
    // Densita' wireframe della parte selezionata (0,0 = eredita dal globale).
    void setActiveMeshWireframeDensity(int uStep, int vStep);
    void activeMeshWireframeDensity(int &uStep, int &vStep) const;
    void setColor(float r, float g, float b);
    void setAlpha(float a);
    void setSpecularEnabled(bool enabled);
    void setLightIntensity(float intensity);
    void increaseWireframeUDensity();
    void decreaseWireframeUDensity();
    void increaseWireframeVDensity();
    void decreaseWireframeVDensity();
    // Riporta la densità wireframe (wfStepU/V) al valore di default. Va azzerata a ogni
    // nuova superficie (cambio tab / caricamento senza densità salvata) per non ereditare
    // quella della precedente. I preset che HANNO la densità salvata la ripristinano
    // invece via setWireframeDensity dopo il load (vedi applyCommonData).
    void resetWireframeDensity();
    // Densità wireframe corrente (passi di campionamento U/V). Serializzata nei preset
    // per riprodurre a schermo lo stesso numero di linee al reload.
    int getWireframeUStep() const { return wfStepU; }
    int getWireframeVStep() const { return wfStepV; }
    void setWireframeDensity(int uStep, int vStep);
    // Ricostruisce la geometria delle linee. Serve dopo aver scritto densita'
    // per-parte (che cambiano gli indici, non un uniform), p.es. al load di un
    // preset. Innocua se la mesh non e' ancora pronta.
    void rebuildWireframeGeometry() { buildWireframeGeometry(); update(); }
    // Chiamato dal popup di rallentamento quando l'utente sceglie "Keep going":
    // sopprime ogni ulteriore avviso finche' l'animazione non si ferma/riparte.
    void acknowledgePerformanceWarning() { m_perfWarnDismissed = true; }
    // Riarma il watchdog dopo che era stato zittito senza passare da rebuildShader
    // (es. la guardia displacement lo ha zittito disattivando la trasparenza; se
    // l'utente riabbassa lo slider vogliamo che torni a vigilare). Azzera anche il
    // livello mostrato cosi' un nuovo rallentamento riavvisa da capo.
    void rearmPerformanceWarning() { m_perfWarnDismissed = false; m_perfWarnLevelMs = 0.0f; }
    // true se un'animazione e' in corso e l'EMA del watchdog misura GIA' un
    // carico pesante (~sotto i 6-7 fps). Usato dalla UI per chiedere conferma
    // PRIMA di attivare effetti che moltiplicano il costo per pixel (ramo
    // trasparente RM: ~4-12x l'opaco -> dal carico pesante si passerebbe
    // dritti al collasso, che il watchdog fermerebbe solo DOPO il magenta).
    // A riposo m_avgFrameMs vale 16 -> false: mai popup su scene fluide o ferme.
    bool renderingUnderHeavyLoad() const { return m_avgFrameMs > 150.0f; }
    // Displacement RM attualmente applicato: la UI lo legge PRIMA di un apply per
    // capire se lo sta introducendo/cambiando (guardia trasparenza mobile: il
    // displacement gira dentro map() e col ramo trasparente il costo esplode).
    QString currentDisplacementCode() const { return m_displacementCode; }
    float getSurfaceScale() const { return m_surfaceScale; }
    void rebuildShader();


    // ==========================================================
    // TEXTURES, SCRIPTS & BACKGROUND
    // ==========================================================
    void loadTextureFromFile(const QString &filename);
    void loadTextureFromImage(const QImage &img);
    void setGlobalTextureEnabled(bool enable);
    void setGlobalTextureColors(const QColor& c1, const QColor& c2);
    void clearTexture();

    void setScriptCheck(bool enabled);
    bool loadCustomShader(const QString &customCode);
    void setShaderTime(float t);

    void setBackgroundColor(const QColor &color);
    void setBackgroundTexture(const QString &path);
    void setBackgroundTextureEnabled(bool enabled);
    bool isBackgroundTextureEnabled() const { return m_useBackgroundTexture; }
    // Stato GLOBALE della texture di superficie (quello che le parti senza
    // texture propria ereditano).
    bool isTextureEnabled() const { return m_textureEnabled; }
    void loadBackgroundScript(const QString &scriptCode);

    void setTextureCode(const QString& code);
    void setDisplacementCode(const QString& code);

    bool validateAndApplyTextureDisplacement(const QString &texCode, const QString &dispCode);


    // ==========================================================
    // 2D FLAT VIEW
    // ==========================================================
    void setFlatView(bool active);
    bool isFlatView() const { return m_isFlatView; }
    // Passando da Superficie (0) a Sfondo (1) si abbandona l'editing della
    // texture della parte attiva: va fissata prima, o resta nei membri globali.
    // Tornando su Superficie si ricarica quella della parte.
    void setFlatViewTarget(int target) {
        if (target == m_flatViewTarget) { update(); return; }
        commitFlatTransformToActivePart();
        m_flatViewTarget = target;
        loadFlatTransformFromActivePart();
        update();
    }
    float getFlatZoom() const;
    void setFlatZoom(float z);
    float getFlatRotation() const;
    void setFlatRotation(float angle);
    void addFlatRotation(float angle);
    void rotateFlat90();
    QVector2D getFlatPan() const;
    void setFlatPan(float x, float y);

    // TRASFORMAZIONE 2D GLOBALE della texture di superficie, per la PERSISTENZA.
    // I getFlat*() qui sopra espongono il buffer di lavoro della vista 2D (lo usa
    // inputhandler per il trascinamento incrementale), che durante l'editing di
    // una fascia contiene l'inquadratura di QUELLA. Il preset deve invece salvare
    // quella della superficie, o riaprendolo l'ultima fascia toccata imporrebbe
    // il proprio zoom a tutte le altre.
    float globalTexZoom() const { return m_globalTexZoom; }
    float globalTexRotation() const { return m_globalTexRotation; }
    QVector2D globalTexPan() const { return m_globalTexPan; }
    void setGlobalTexTransform(float zoom, const QVector2D &pan, float rotation) {
        m_globalTexZoom = zoom;
        m_globalTexPan = pan;
        m_globalTexRotation = rotation;
        // In ambito "All" il buffer di lavoro E' la trasformazione globale:
        // tenerli allineati evita che il load di un preset lasci la vista 2D
        // sull'inquadratura precedente.
        if (m_activeMeshPart < 0) {
            m_flatZoom = zoom;
            m_flatPan = pan;
            m_flatRotation = rotation;
        }
        update();
    }

    // Trasferimento della trasformazione 2D (zoom/pan/rotazione della texture)
    // fra i membri globali e la parte di mesh attiva. Sono il perno del
    // per-mesh: senza il commit la trasformazione resta globale e finisce nel
    // blocco UBO di TUTTE le parti (e dell'ambito All).
    void commitFlatTransformToActivePart();
    void loadFlatTransformFromActivePart();


    // ==========================================================
    // CAMERA 3D & 4D STATE
    // ==========================================================
    void set4DLighting(bool enable);
    void setLightingMode4D(int mode);
    bool is4DActive() const;

    void setRotation4D(float o, float p, float ps);
    float getOmega() const { return omega; }
    float getPhi() const { return phi; }
    float getPsi() const { return psi; }

    void setCameraPosAndDirection3D(const QVector3D& pos, const QVector3D& target, float roll);
    void setCameraFrom4DVectors(const QVector4D &pos4D, const QVector4D &target4D, const QVector4D &up4D);
    // Handoff camera tra path 3D<->4D: cattura la camera CORRENTE e per i primi
    // tick del nuovo path fonde (smoothstep) la vista catturata con quella del
    // path, evitando il teletrasporto al cambio. Chiamato dai Departure quando
    // l'ALTRO path era attivo al click.
    void beginPathHandoff();

    void zoomCamera(float delta);
    void addCameraRotation(float dYaw, float dPitch);
    void resetTransformations();
    void virtualMove(MoveDir dir, float speed3D, float speed4D);

    QVector3D getCameraPos() const { return m_cameraPos; }
    float getCameraYaw() const { return m_cameraYaw; }
    float getCameraPitch() const { return m_cameraPitch; }
    float getCameraRoll() const { return m_cameraRoll; }

    // YAW/PITCH EFFETTIVI, cioe' quelli che descrivono DAVVERO la direzione di
    // vista in questo istante.
    //
    // Servono al SALVATAGGIO. In modalita' path la vista non passa da
    // m_cameraYaw/m_cameraPitch: paintGL usa lookAt(m_cameraPos, m_pathTarget),
    // e quei due campi restano al valore che avevano prima -- tipicamente 0.
    // Salvare i campi grezzi produceva quindi un record con la posizione giusta
    // e la direzione sbagliata: al ricaricamento, con il path non riavviato, la
    // vista ricadeva su front=(0,0,-1) e il soggetto usciva dall'inquadratura.
    // MISURATO sul record "Calabi-Yau Orbit": camera in (2.19, 0.20, 1.89) con
    // yaw=pitch=0 -- 130.8 gradi di scarto dalla direzione che inquadra
    // l'origine, soggetto spostato a sinistra e in parte fuori campo.
    //
    // ATTENZIONE: m_isPathFollowing e' la MODALITA', non "il path sta
    // animando", e PERSISTE dopo lo stop del path (la spegne solo
    // resetTransformations). E' proprio questo a rendere il caso frequente:
    // basta aver usato un path, fermarlo, avviare le rotazioni e salvare.
    float getEffectiveCameraYaw() const;
    float getEffectiveCameraPitch() const;
    void setCameraPos(const QVector3D& pos) { m_cameraPos = pos; meshNeedsUpdate = true; update(); }
    void setCameraYaw(float y) { m_cameraYaw = y; meshNeedsUpdate = true; update(); }
    void setCameraPitch(float p) { m_cameraPitch = p; meshNeedsUpdate = true; update(); }
    void setCameraRoll(float r) { m_cameraRoll = r; meshNeedsUpdate = true; update(); }
    // Attiva/disattiva la soppressione del watchdog di performance durante
    // l'esportazione video (vedi m_isRecording). Chiamato dal VideoRecorder.
    // Al ritorno live (on=false) riarma lo STATO DI MISURA del watchdog per non
    // scambiare il transitorio di ripristino export per un rallentamento reale.
    void setRecordingActive(bool on);
    QQuaternion getRotationQuat() const { return m_rotationQuat; }
    // setRotationQuat imposta la rotazione "di default" (load preset / stato iniziale):
    // azzera m_userRotatedManually perche' questa NON e' una rotazione dell'utente.
    void setRotationQuat(const QQuaternion& q) { m_rotationQuat = q; m_userRotatedManually = false; meshNeedsUpdate = true; update(); }
    // All'avvio di un path: se l'orientamento corrente e' quello di default del
    // preset/avvio (l'utente non ha ruotato a mano), lo riporta a neutro; se invece
    // l'utente ha ruotato col mouse, lo conserva.
    void neutralizeDefaultRotationForPath() {
        if (!m_userRotatedManually) { m_rotationQuat = QQuaternion(); meshNeedsUpdate = true; update(); }
    }
    // Marcato dalla rotazione manuale (mouse/touch) via InputHandler.
    void markUserRotated() { m_userRotatedManually = true; }
    float getObserverPos4D() const { return m_observerPos.w(); }
    void setObserverPos4D(float pos) { m_observerPos.setW(pos); m_cameraPos4D.setW(pos); meshNeedsUpdate = true; update(); }


    // ==========================================================
    // ANIMATION & MOTION CONTROL
    // ==========================================================
    void addObjectRotation(float dPrecession, float dNutation, float dSpin);
    // Avanza rotazioni oggetto (prec/nut/spin) e 4D (omega/phi/psi) di
    // dtSeconds di tempo d'animazione, alle velocita' correnti. Unica
    // implementazione: la usa il tick live (updateRotation, dt=16ms) e il
    // loop di registrazione (dt del frame virtuale) — niente copie della
    // cinematica nel recorder (vedi CLAUDE.md).
    void advanceRotationsBy(float dtSeconds);
    // Clock esterno (registrazione): con il flag attivo i tick live NON
    // avanzano i moti — li avanza il loop del recorder col tempo virtuale del
    // frame. Lo STATO pero' resta vero (rotationTimer attivo, isAnimating()
    // sincero), cosi' bottoni, mutua esclusivita' e handler dei moti
    // funzionano normalmente anche durante il REC.
    void setExternalClockActive(bool on) { m_externalClockActive = on; }
    void setNutationSpeed(float v) { nutationSpeed = v; }
    void setPrecessionSpeed(float v) { precessionSpeed = v; }
    void setSpinSpeed(float v) { spinSpeed = v; }
    void setOmegaSpeed(float v) { omegaSpeed = v; }
    void setPhiSpeed(float v) { phiSpeed = v; }
    void setPsiSpeed(float v) { psiSpeed = v; }

    float getNutationSpeed() const { return nutationSpeed; }
    float getPrecessionSpeed() const { return precessionSpeed; }
    float getSpinSpeed() const { return spinSpeed; }
    float getOmegaSpeed() const { return omegaSpeed; }
    float getPhiSpeed() const { return phiSpeed; }
    float getPsiSpeed() const { return psiSpeed; }

    bool isAnimating() const { return rotationTimer && rotationTimer->isActive(); }
    void pauseMotion();
    void resumeMotion();

    // "Il path sta ANIMANDO". Distinto da m_isPathFollowing, che e' la MODALITA'
    // CAMERA (tangent vs center, render() riga ~298) e deve restare attiva anche
    // dopo lo stop perche' l'utente non perda l'orientamento tangente. Questo flag
    // serve SOLO al watchdog di performance per sapere se c'e' rendering continuo;
    // MainWindow lo spegne allo stop del path. Vedi render().
    void setPathAnimating(bool animating) {
        m_pathAnimating = animating;
        // Stop del path a handoff in corso: il blend va annullato, altrimenti
        // un Departure successivo SENZA subentro riprenderebbe la scivolata
        // da una vista catturata ormai stantia.
        if (!animating) m_pathHandoffActive = false;
    }
    bool isPathAnimating() const { return m_pathAnimating; }

    void startAnimationTimer();
    void stopAnimationTimer();
    void stopAllTimers();
    void resetTime();
    void setSurfaceAnimating(bool animating);
    bool isSurfaceAnimating() const { return m_surfaceAnimating; }

    void setBackgroundTextureAnimating(bool animating);
    void setSurfaceTextureAnimating(bool animating);
    bool isBackgroundTextureAnimating() const { return m_bgAnimating; }
    bool isSurfaceTextureAnimating() const { return m_texAnimating; }

    // Registrazione video: fotografa/ripristina il tempo mostrato dai moduli
    // col clock FERMO, che non devono seguire il tempo virtuale del recorder
    // (vedi ramo use_virtual_time in render). begin va chiamata PRIMA di
    // attivare use_virtual_time e di toccare m_manualTime; end quando torna false.
    void beginVirtualTimeFreeze();
    void endVirtualTimeFreeze();


    // ==========================================================
    // UTILITIES
    // ==========================================================
    int projectionMode = 0;
    // FOV verticale (gradi) dell'obiettivo 3D (slider Fov del dock Path 3D).
    // Letto in render() -> vale identico per tick live e registrazione.
    void setCameraFov(float deg);
    float cameraFov() const { return m_cameraFov; }
    QImage getFrameForVideo(int targetW = -1, int targetH = -1, bool useFbo = false);
    // Cattura a risoluzione PIENA (vero "FBO"): fissa la dimensione in pixel del
    // color buffer offscreen di QRhiWidget alla risoluzione di export, così la
    // scena viene renderizzata NATIVAMENTE a quella risoluzione e grabFramebuffer()
    // restituisce pixel nitidi senza upscale (fix wireframe sfocato nei video).
    // begin va chiamata una volta prima del loop di registrazione, end alla fine.
    void beginHiResCapture(int w, int h);
    void endHiResCapture();
    QString getShaderError() const { return m_lastCompilationError; }
    bool validateAndApplyParametricShader(const QString &customLogic);
    bool validateAndApplyImplicitShader(const QString &eqF, const QString &texCode, const QString &dispCode);
      bool validateAndApplyImplicitScript(const QString &scriptCodeGLSL);
    bool validateAndApplyBackgroundShader(const QString &scriptCode);
    bool validateAndApplyParametricScript(const QString &scriptCodeGLSL);


signals:
    void rotationChanged();
    // Il fragment shader non compila e le pipeline sono state azzerate: a schermo
    // la superficie SPARISCE. Senza questo segnale l'unica traccia era un
    // qWarning sulla console, invisibile all'utente, che vedeva solo il vuoto.
    // Il caso tipico sono due texture per-mesh i cui script dichiarano lo stesso
    // simbolo (vedi la rinomina in buildTextureFunction): il generatore copre le
    // forme note, ma uno script NUOVO puo' sempre introdurne una non prevista.
    void shaderCompilationFailed(const QString &error);
    // Un'immagine di texture non si e' potuta CARICARE: il file esiste ed e'
    // leggibile (chi chiama lo verifica prima), ma Qt non ne ricava pixel —
    // formato non supportato o file corrotto. Senza questo segnale
    // loadTextureFromFile usciva in SILENZIO, lasciando a schermo la texture
    // precedente: all'utente sembrava che il preset avesse una texture
    // "sbagliata" o "di default", senza alcun indizio del perche'.
    void textureImageLoadFailed(const QString &path);
    // Emesso dopo ogni rigenerazione della griglia, quando le parti di mesh
    // possono essere cambiate (nuovo script, nuove sezioni //MESH_BEGIN).
    // MainWindow lo usa per riallineare il selettore di mesh del dock renderer e
    // per riversare l'aspetto per-mesh di un preset appena caricato. E' un
    // segnale e non una chiamata diretta perche' updateSurfaceData() ha otto
    // chiamanti sparsi (script, equazioni, cambio tab, load...): agganciarsi al
    // punto in cui le parti NASCONO copre tutti i percorsi, incluso quello degli
    // script, che non passa da checkAndTriggerMeshUpdate.
    void meshPartsChanged();
    // Emesso quando il throughput di rendering resta basso (animazione che
    // rallenta sensibilmente) abbastanza a lungo da segnalare un carico GPU
    // eccessivo. MainWindow lo intercetta per avvisare l'utente. Vedi la logica
    // di misura intervallo-frame in render().
    void performanceWarning();
    // L'UTENTE ha mosso la scena col mouse: trascinamento (rilascio) o rotella.
    // Distinto da rotationChanged(), che viene emesso a ogni frame anche dal moto
    // AUTOMATICO (advanceRotationsBy) e quindi non distingue chi ha mosso cosa.
    // MainWindow lo usa per marcare il lavoro non salvato.
    void userMovedView();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

public slots:
    void rebuildBackgroundShader(bool isTextureMode, const QString &customCode = "");

    // ==========================================================
    // GEODESIC FLOW CALCULATIONS
    // ==========================================================

    void setConstants(const QMap<QString, float>& constants) { m_constants = constants; }

private slots:
    void updateRotation();

private:
    // ==========================================================
    // CORE ARCHITECTURE
    // ==========================================================
    std::unique_ptr<SurfaceEngine> engine;
    std::unique_ptr<InputHandler> m_inputHandler;


    // ==========================================================
    // ENGINE STATE & REFACTORING
    // ==========================================================
    EngineMode m_engineMode = ModeParametric;

    // Campo implicito "mal condizionato": equazioni a PRODOTTO di piu' fattori
    // (es. preset "Chain" = -(f1*f2*...*f6)) hanno un range dinamico enorme
    // (~1e9) e cambiano segno per PARITA' del prodotto anche lontano da ogni
    // superficie reale. Il ramo trasparente (marchNextLayer) rileva le facce col
    // cambio di segno GREZZO del campo, quindi aggancia questi crossing FANTASMA
    // e il compositing esce con alpha~0 -> la superficie SPARISCE con alpha<1.
    // Il ramo OPACO usa d=val/gradLen e non ne soffre. Quando questo flag e' true
    // forziamo alpha=1.0 (fallback a opaco) e la UI disabilita lo slider alpha.
    // Rilevato empiricamente campionando il campo su CPU in setImplicitEquation:
    // NON e' un'euristica sulla stringa (un legittimo (x)*(y) non lo attiva).
    bool m_implicitIllConditioned = false;
    // SOLO ANDROID: avviso (non blocco) per la trasparenza che potrebbe degradare.
    // Sempre false su desktop/iOS. Vedi implicitTransparencyMayDegrade().
    bool m_implicitTransparencyWarn = false;
    void detectImplicitConditioning(const QString &eqF);

    // ==========================================================
    // TEXTURE
    // ==========================================================
    QRhiTexture *m_surfaceTexture = nullptr;
    // true SOLO se m_surfaceTexture contiene un'immagine caricata dall'utente
    // (loadTextureFromFile). NON basta guardare m_surfaceTexture != nullptr:
    // MainWindow::generateTexture() ci mette anche la scacchiera di default,
    // disegnata su CPU, che e' un fallback e non un'immagine. Governa u_noImage,
    // cioe' se gli script che campionano iChannel0 cadono sulla scacchiera
    // procedurale (colori vivi da u_col1/u_col2) o leggono la texture.
    bool m_hasUserImage = false;
    QImage m_pendingSurfaceImage;
    bool m_surfaceTextureNeedsUpload = false;
    // Richiesta di scarico della texture di superficie dalla GPU: la distruzione
    // dell'oggetto RHI va fatta DENTRO il render loop (non sul thread GUI), come
    // per l'upload. Senza questo, una texture caricata resta residente e ricompare
    // stantia cambiando superficie o riaccendendo il checkbox.
    bool m_surfaceTextureNeedsClear = false;

    QRhiTexture *m_backgroundTexture = nullptr;
    QImage m_pendingBackgroundImage;
    bool m_backgroundTextureNeedsUpload = false;
    QRhiBuffer *m_bgVbo = nullptr;
    QRhiGraphicsPipeline *m_bgPipeline = nullptr;
    QRhiShaderResourceBindings *m_bgBindings = nullptr;
    bool m_bgVboUploaded = false;
    QString m_customFragmentCode;
    QString m_bgScriptCode;

    QString m_textureCode;
    QString m_displacementCode;


    // ==========================================================
    // RISORSE QRHI
    // ==========================================================
    QRhiBuffer *m_vbo = nullptr;
    QRhiBuffer *m_ibo = nullptr;
    QRhiBuffer *m_ubo = nullptr;

    // Avviso di shader non compilato gia' emesso: impedisce la RAFFICA di popup.
    // buildPipeline puo' fallire a ogni frame, e il popup desktop e' modale
    // (event loop annidato), quindi ogni segnale in coda ne aprirebbe un altro
    // sopra il precedente. Si riarma appena uno shader torna valido.
    bool m_shaderErrorReported = false;
    QRhiTexture *m_dummyTexture = nullptr;
    QRhiSampler *m_sampler = nullptr;

    QRhiShaderResourceBindings *m_bindings = nullptr;

    QRhiBuffer *m_wireframeIbo = nullptr;
    QRhiGraphicsPipeline *m_wireframePipeline = nullptr;
    std::vector<unsigned int> m_wireframeIndices;
    bool wireframeNeedsUpdate = true;
    int m_wireframeIndexCount = 0;
    // Un intervallo per parte di mesh: il wireframe condivide il VBO della
    // superficie (indici assoluti) ma gli uniform del dominio sono per-parte.
    std::vector<WireframeRange> m_wireframeRanges;

    QRhiGraphicsPipeline *m_pipelineOpaque = nullptr;
    QRhiGraphicsPipeline *m_pipelineTranspBack = nullptr;
    QRhiGraphicsPipeline *m_pipelineTranspFront = nullptr;

    QRhiBuffer *m_bgUbo = nullptr;

    UboData m_uboData;

    // ==========================================================
    // MULTI-MESH: UBO ad array + dynamic offset
    // ==========================================================
    // I limiti u_min/u_max/v_min/v_max e u_meshIndex sono per-parte, ma sono
    // uniform: servono N blocchi nello stesso buffer, uno per parte, e ogni draw
    // call ne seleziona uno con un dynamic offset. Alternativa scartata: N
    // updateDynamicBuffer sullo stesso blocco, che serializzerebbe i draw.
    //
    // m_uboBlockStride e' allineato con QRhi::ubufAlignment(): NON e'
    // sizeof(UboData) (su Metal/Vulkan l'allineamento minimo puo' essere 256).
    quint32 m_uboBlockStride = 0;
    int m_uboBlockCapacity = 0;   // quante parti entrano nel buffer attuale
    // Binding con dynamic offset per la superficie multi-parte. Restano separati
    // da m_bindings: quello serve ai percorsi che disegnano a offset 0 (sfondo
    // flat, ray marching) e non deve cambiare comportamento.
    QRhiShaderResourceBindings *m_bindingsDyn = nullptr;
    QRhiTexture *m_bindingsDynTexture = nullptr;  // texture con cui e' stato creato
    void ensureDynamicBindings(QRhiTexture *tex);
    bool ensureUboCapacity(int partCount);


    // ==========================================================
    // RISORSE QRHI IMPLICIT (RAY MARCHING)
    // ==========================================================
    void buildImplicitPipeline();
    QRhiGraphicsPipeline *m_pipelineImplicit = nullptr;
    QRhiShaderResourceBindings *m_bindingsImplicit = nullptr;


    // ==========================================================
    // MATHEMATICAL & GEOMETRY STATE
    // ==========================================================
    QString m_eqX, m_eqY, m_eqZ, m_eqW;
    bool meshNeedsUpdate = true;
    int m_indexCount = 0;
    int wfStepU = 4;
    int wfStepV = 4;
    int m_raySteps = 100;
    float m_surfaceScale = 2.0f;
    bool m_isCustomMesh = false;


    // ==========================================================
    // IMPLICIT EQUATIONS STATE
    // ==========================================================
    QString m_eqImplicitF = "x*x + y*y + z*z - 1.0";

    QString createImplicitFragmentShader();
    QString createBackgroundFragmentShader(bool isTextureMode, const QString &customCode);


    // ==========================================================
    // GEODESIC FLOW STATE
    // ==========================================================
    QString m_eqLambda = "1.0";
    QString m_initU = "u", m_initV = "0", m_initW = "0";
    QString m_eqDu = "0", m_eqDv = "1", m_eqDw = "0";
    QMap<QString, float> m_constants;

    int m_numU_geo = 100;
    int m_numV_geo = 200;
    float m_vMin_geo = -5.0f;
    float m_vMax_geo = 5.0f;


    // ==========================================================
    // RENDERING & TEXTURE STATE
    // ==========================================================
    int renderMode = 0;
    // Indice della mesh su cui agiscono i controlli di aspetto; -1 = "All"
    // (stato globale). Vedi setActiveMeshPart.
    int m_activeMeshPart = -1;
    bool m_meshAppearanceBypass = false;
    // Vero in ambito "All": l'aspetto per-parte e' SOSPESO (ignorato dal render,
    // non cancellato), cosi' la superficie si disegna come una sola e i valori
    // per-mesh restano disponibili per quando si torna su "Mesh".
    bool m_meshAppearanceUniform = false;
    bool applyToActiveMeshPart(const std::function<void(MeshPart&)> &fn);
    // Modalita' con cui una parte va DISEGNATA: la sua se dichiarata, ma in
    // ambito "All" (uniform) vince sempre il globale. Punto unico, cosi' render
    // e partizione wireframe/solido non possono divergere.
    int effectivePartRenderMode(const MeshPart &p) const {
        return m_meshAppearanceUniform ? renderMode : p.effectiveRenderMode(renderMode);
    }
    // Sposta di 'delta' il passo wireframe della parte selezionata. Ritorna
    // false se non c'e' una parte attiva: il chiamante ricade sul globale.
    bool adjustActiveWireframeStep(bool isU, int delta);
    // True quando lo script ray marching definisce la direttiva Inner:=
    // (seconda superficie opaca interna, es. orizzonte di Kerr). Settato in
    // createImplicitFragmentShader() e inviato alla GPU via u_dummyZero.y.
    bool m_raymarchHasInner = false;
    bool m_textureEnabled = false;
    bool m_isSpecularEnabled = false;
    float alpha = 0.5f;
    float red = 1, green = 1, blue = 1;
    float m_lightIntensity = 1.0f;

    float texRed1 = 1.0f, texGreen1 = 1.0f, texBlue1 = 1.0f;
    float texRed2 = 0.0f, texGreen2 = 0.0f, texBlue2 = 0.0f;

    bool m_useBackgroundTexture = false;
    bool m_bgIsScript = false;
    QVector3D m_bgColor = QVector3D(0.3f, 0.3f, 0.3f);
    int m_lightingMode4D = 0;


    // ==========================================================
    // 2D / FLAT VIEW STATE
    // ==========================================================
    bool m_isFlatView = false;
    // true tra beginHiResCapture/endHiResCapture: il color buffer è già fissato
    // alla risoluzione di export, quindi getFrameForVideo NON deve riscalare.
    bool m_hiResCapture = false;
    // true durante l'esportazione video: il rendering e' frame-by-frame (con
    // cattura + scrittura su disco tra i frame), quindi gli intervalli sono
    // naturalmente lentissimi -> il watchdog di performance darebbe un FALSO
    // avviso "il rendering rallenta". Lo si disattiva mentre e' true.
    bool m_isRecording = false;
    int m_flatViewTarget = 0;
    // BUFFER DI LAVORO della vista 2D: e' cio' che il mouse muove e cio' che il
    // fragment legge per la parte in editing. NON e' la trasformazione globale:
    // vedi m_globalTexZoom/Pan/Rotation qui sotto.
    float m_flatZoom = 1.0f;
    float m_flatRotation = 0.0f;
    QVector2D m_flatPan;
    // TRASFORMAZIONE 2D GLOBALE della texture di superficie (ambito "All", e
    // valore ereditato dalle parti che non ne hanno una propria).
    // Vive separata dai tre membri qui sopra perche' quelli fanno DUE lavori:
    // sono il buffer che il mouse muove in vista 2D, e finivano anche in
    // m_uboData come stato globale. Manipolando la texture di UNA mesh li si
    // sovrascriveva, e la trasformazione globale -- insieme a quella di ogni
    // fascia che la eredita -- veniva trascinata dietro.
    float m_globalTexZoom = 1.0f;
    float m_globalTexRotation = 0.0f;
    QVector2D m_globalTexPan;


    // ==========================================================
    // CAMERA & TRANSFORMATIONS STATE
    // ==========================================================
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;
    QQuaternion m_rotationQuat;
    // true se l'utente ha ruotato la superficie a mano (mouse/touch) dopo l'ultima
    // rotazione "di default" (setRotationQuat). Governa neutralizeDefaultRotationForPath.
    bool m_userRotatedManually = false;

    QVector3D m_cameraPos;
    QVector4D m_cameraPos4D = QVector4D(0.0f, 0.0f, 4.0f, 4.0f);
    QVector4D m_observerPos = QVector4D(0.0f, 0.0f, 0.0f, 4.0f);

    float m_cameraYaw;
    float m_cameraPitch;
    float m_cameraRoll = 0.0f;
    float m_cameraFov = 45.0f; // gradi, vedi setCameraFov()
    bool m_externalClockActive = false; // vedi setExternalClockActive()

    bool m_isPathFollowing = false;   // MODALITA' camera tangent (persiste dopo stop)
    bool m_pathAnimating = false;     // path in animazione (solo per il watchdog)
    QVector3D m_pathTarget;
    QVector3D m_pathUp;
    float m_pathRoll = 0.0f;

    // --- Handoff camera al cambio path 3D<->4D (vedi beginPathHandoff) ---
    float advancePathHandoff();      // avanza il blend, torna il fattore smoothstep
    bool m_pathHandoffActive = false;
    float m_pathHandoffK = 0.0f;      // 0..1, avanzato a ogni tick del nuovo path
    QVector3D m_handoffPos;
    QVector3D m_handoffTarget;
    QVector3D m_handoffUp;
    float m_handoffRoll = 0.0f;
    QVector4D m_handoffObserver;
    QVector4D m_handoffCam4D;


    // ==========================================================
    // ANIMATION & MOTION STATE
    // ==========================================================
    // Periodo del tick di rotationTimer: UNICA fonte, usata sia dal
    // setInterval sia dalla conversione tempo->tick di advanceRotationsBy.
    // Mai duplicarlo come letterale: se i due valori divergono, live e video
    // ruotano a velocita' diverse (vedi CLAUDE.md).
    static constexpr int kRotationTickMs = 16;
    QTimer* rotationTimer;
    QTimer* m_animTimer = nullptr;
    QElapsedTimer m_elapsedTimer;
    QElapsedTimer m_surfaceTimer;

    // --- Watchdog di performance (avviso da rallentamento) ---
    // Misura l'intervallo TRA frame consecutivi (non la durata di render(): in
    // QRhiWidget render() registra solo i comandi, la GPU li esegue dopo, quindi
    // il throughput reale si legge dal ritmo dei frame, non dall'encoding CPU).
    QElapsedTimer m_frameClock;          // delta dall'ultimo frame
    QElapsedTimer m_perfGraceClock;      // finestra di grazia dopo l'export: entro
                                         // kPerfGraceMs il watchdog scarta le misure
                                         // (il transitorio di ripristino, di durata
                                         // variabile su mobile, non e' carico GPU)
    float m_avgFrameMs = 16.0f;          // media mobile (EMA) dell'intervallo, ms
    float m_slowAccumMs = 0.0f;          // tempo accumulato sotto soglia
    int   m_slowFrameRun = 0;            // campioni lenti (dt > kSlowFrameMs) consecutivi:
                                         // con l'EMA asimmetrica (salita 0.35) un singolo
                                         // picco fin quasi a 2 s porta la media oltre
                                         // soglia da solo, quindi l'immunita' ai picchi
                                         // isolati la garantisce QUESTO contatore
                                         // (avviso solo con >= kSlowRunToWarn di fila)
    int   m_hugeFrameRun = 0;            // frame consecutivi > kHugeFrameMs (>2 s): un
                                         // frame isolato = buco da inattivita' (scartato),
                                         // una SEQUENZA = collasso GPU reale (es. iPad a
                                         // 0.2 fps con RM+trasparenza) -> va segnalata
    float m_perfWarnLevelMs = 0.0f;      // livello del primo avviso (0 = armato per la
                                         // scena corrente); riarmato solo da rebuildShader()
    bool m_perfWarnDismissed = false;    // gia' avvisato per QUESTA scena: sopprime ogni
                                         // ulteriore avviso finche' non cambiano le
                                         // impostazioni (riarmo SOLO in rebuildShader(),
                                         // NON su stop/riavvio moti ne' su oscillazione)
    bool m_wasAnimating = false;         // stato anim. al frame precedente: per saltare
                                         // il primo frame dopo lo start (transitorio)

    bool m_surfaceAnimating = false;
    float m_manualTime = 0.0f;

    float nutation = 0, precession = 0, spin = 0;
    float omega = 0, phi = 0, psi = 0;
    float nutationSpeed = 0, precessionSpeed = 0, spinSpeed = 0;
    float omegaSpeed = 0, phiSpeed = 0, psiSpeed = 0;

    QVector3D m_lastValidUp{0.0f, 1.0f, 0.0f};
    bool m_isFirstPathRun{true};

    float m_lastRealTime = 0.0f;
    float m_timeGeom = 0.0f;
    float m_timeTex = 0.0f;
    float m_timeBg = 0.0f;

    // Tempi congelati per la registrazione video (beginVirtualTimeFreeze):
    // il tempo TOTALE (m_manualTime + m_time*) mostrato quando e' partito il REC.
    // In registrazione i moduli fermi restano inchiodati a questi valori.
    bool  m_vtFreezeValid = false;
    float m_vtFrozenGeom = 0.0f;
    float m_vtFrozenTex  = 0.0f;
    float m_vtFrozenBg   = 0.0f;

    bool m_bgAnimating = false;
    bool m_texAnimating = false;


    // ==========================================================
    // MEMORIA DI STATO DELLA VISTA (Parametrica vs Implicita)
    // ==========================================================
    struct ViewState {
        QVector3D cameraPos = QVector3D(0.0f, 0.0f, 4.0f);
        float cameraYaw = 0.0f;
        float cameraPitch = 0.0f;
        float cameraRoll = 0.0f;
        QQuaternion rotationQuat;
        float flatZoom = 1.0f;
        float flatRotation = 0.0f;
        QVector2D flatPan = QVector2D(0.0f, 0.0f);
        // Anche la trasformazione 2D GLOBALE e' per-vista, come il buffer di
        // lavoro qui sopra: senza, passando da Parametric a Ray Marching
        // l'inquadratura globale dell'una restava applicata all'altra.
        float globalTexZoom = 1.0f;
        float globalTexRotation = 0.0f;
        QVector2D globalTexPan = QVector2D(0.0f, 0.0f);
    };

    // Array con i 2 slot di memoria (0 = Parametrico, 1 = Implicito)
    ViewState m_viewStates[2];

    // ==========================================================
    // PRIVATE HELPER METHODS
    // ==========================================================
    // --- Geometry & Mesh Builders ---
    void buildWireframeGeometry();

    // --- Shader Generation & Compilation ---
    QString createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    QString createFragmentShaderSource(const QString &customCode);
    // Genera UNA funzione texture procedurale col nome dato. Chiamata piu' volte
    // nello stesso shader per le texture per-mesh (getCustomColor_<k>).
    QString buildTextureFunction(const QString &customLogic, const QString &funcName);
    QString generateGlslHelperVars(const QString& sourceCode);
    QShader bakeShader(const QByteArray &source, QShader::Stage stage);

    // --- Pipeline & Resource Initialization ---
    void buildPipeline();
    void initBackgroundShader();

    // --- Texture Utilities ---
    // cb: necessario per sottomettere l'upload della scacchiera di fallback.
    void createDummyTexture(QRhiCommandBuffer *cb);

    // --- Math & Projections ---
    QVector3D projectPoint4Dto3D(const QVector4D& point4D);


    // ==========================================================
    // UTILITIES
    // ==========================================================
    bool m_useFbo = false;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    QString m_lastCompilationError;
};

#endif // GLWIDGET_H
