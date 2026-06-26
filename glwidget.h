#ifndef GLWIDGET_H
#define GLWIDGET_H

#include "surfaceengine.h"

#include <QWidget>
#include <QRhiWidget>
#include <private/qrhi_p.h>
#include <rhi/qshader.h>
#include <rhi/qshaderbaker.h>
#include <QTimer>
#include <memory>
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


    // ==========================================================
    // RENDERING & VISUALS
    // ==========================================================
    void updateSurfaceData();
    void resetVisuals();
    void setProjectionMode(int mode);
    void setRenderMode(int mode);
    void setShowBorders(bool enable);
    void setColor(float r, float g, float b);
    void setBorderColor(float r, float g, float b);
    void setAlpha(float a);
    void setSpecularEnabled(bool enabled);
    void setLightIntensity(float intensity);
    void increaseWireframeUDensity();
    void decreaseWireframeUDensity();
    void increaseWireframeVDensity();
    void decreaseWireframeVDensity();
    float getSurfaceScale() const { return m_surfaceScale; }
    void rebuildShader();


    // ==========================================================
    // TEXTURES, SCRIPTS & BACKGROUND
    // ==========================================================
    void loadTextureFromFile(const QString &filename);
    void loadTextureFromImage(const QImage &img);
    void setTextureEnabled(bool enable);
    void setTextureColors(const QColor& c1, const QColor& c2);
    void clearTexture();

    void setScriptCheck(bool enabled);
    bool loadCustomShader(const QString &customCode);
    void setShaderTime(float t);

    void setBackgroundColor(const QColor &color);
    void setBackgroundTexture(const QString &path);
    void setBackgroundTextureEnabled(bool enabled);
    bool isBackgroundTextureEnabled() const { return m_useBackgroundTexture; }
    void loadBackgroundScript(const QString &scriptCode);

    void setTextureCode(const QString& code);
    void setDisplacementCode(const QString& code);

    bool validateAndApplyTextureDisplacement(const QString &texCode, const QString &dispCode);


    // ==========================================================
    // 2D FLAT VIEW
    // ==========================================================
    void setFlatView(bool active);
    bool isFlatView() const { return m_isFlatView; }
    void setFlatViewTarget(int target) { m_flatViewTarget = target; update(); }
    float getFlatZoom() const;
    void setFlatZoom(float z);
    float getFlatRotation() const;
    void setFlatRotation(float angle);
    void addFlatRotation(float angle);
    void rotateFlat90();
    QVector2D getFlatPan() const;
    void setFlatPan(float x, float y);


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

    void zoomCamera(float delta);
    void addCameraRotation(float dYaw, float dPitch);
    void resetTransformations();
    void virtualMove(MoveDir dir, float speed3D, float speed4D);

    QVector3D getCameraPos() const { return m_cameraPos; }
    float getCameraYaw() const { return m_cameraYaw; }
    float getCameraPitch() const { return m_cameraPitch; }
    float getCameraRoll() const { return m_cameraRoll; }
    void setCameraPos(const QVector3D& pos) { m_cameraPos = pos; meshNeedsUpdate = true; update(); }
    void setCameraYaw(float y) { m_cameraYaw = y; meshNeedsUpdate = true; update(); }
    void setCameraPitch(float p) { m_cameraPitch = p; meshNeedsUpdate = true; update(); }
    void setCameraRoll(float r) { m_cameraRoll = r; meshNeedsUpdate = true; update(); }
    QQuaternion getRotationQuat() const { return m_rotationQuat; }
    void setRotationQuat(const QQuaternion& q) { m_rotationQuat = q; meshNeedsUpdate = true; update(); }
    float getObserverPos4D() const { return m_observerPos.w(); }
    void setObserverPos4D(float pos) { m_observerPos.setW(pos); m_cameraPos4D.setW(pos); meshNeedsUpdate = true; update(); }


    // ==========================================================
    // ANIMATION & MOTION CONTROL
    // ==========================================================
    void addObjectRotation(float dPrecession, float dNutation, float dSpin);
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


    // ==========================================================
    // UTILITIES
    // ==========================================================
    int projectionMode = 0;
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

    // ==========================================================
    // TEXTURE
    // ==========================================================
    QRhiTexture *m_surfaceTexture = nullptr;
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

    QRhiTexture *m_dummyTexture = nullptr;
    QRhiSampler *m_sampler = nullptr;

    QRhiShaderResourceBindings *m_bindings = nullptr;

    QRhiBuffer *m_wireframeIbo = nullptr;
    QRhiGraphicsPipeline *m_wireframePipeline = nullptr;
    std::vector<unsigned int> m_wireframeIndices;
    bool wireframeNeedsUpdate = true;
    int m_wireframeIndexCount = 0;

    std::vector<Vertex> m_borderVertices;
    QRhiBuffer *m_borderVbo = nullptr;
    QRhiBuffer *m_borderUbo = nullptr;
    QRhiShaderResourceBindings *m_borderBindings = nullptr;
    QRhiGraphicsPipeline *m_borderPipeline = nullptr;
    bool borderNeedsUpdate = true;

    QRhiGraphicsPipeline *m_pipelineOpaque = nullptr;
    QRhiGraphicsPipeline *m_pipelineTranspBack = nullptr;
    QRhiGraphicsPipeline *m_pipelineTranspFront = nullptr;

    QRhiBuffer *m_bgUbo = nullptr;

    UboData m_uboData;


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
    int m_borderVertexCount = 0;
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
    bool showBorders = false;
    // True quando lo script ray marching definisce la direttiva Inner:=
    // (seconda superficie opaca interna, es. orizzonte di Kerr). Settato in
    // createImplicitFragmentShader() e inviato alla GPU via u_dummyZero.y.
    bool m_raymarchHasInner = false;
    bool m_textureEnabled = false;
    bool m_isSpecularEnabled = false;
    float alpha = 0.5f;
    float red = 1, green = 1, blue = 1;
    float bordRed = 1, bordGreen = 1, bordBlue = 0;
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
    int m_flatViewTarget = 0;
    float m_flatZoom = 1.0f;
    float m_flatRotation = 0.0f;
    QVector2D m_flatPan;


    // ==========================================================
    // CAMERA & TRANSFORMATIONS STATE
    // ==========================================================
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;
    QQuaternion m_rotationQuat;

    QVector3D m_cameraPos;
    QVector4D m_cameraPos4D = QVector4D(0.0f, 0.0f, 4.0f, 4.0f);
    QVector4D m_observerPos = QVector4D(0.0f, 0.0f, 0.0f, 4.0f);

    float m_cameraYaw;
    float m_cameraPitch;
    float m_cameraRoll = 0.0f;

    bool m_isPathFollowing = false;
    QVector3D m_pathTarget;
    QVector3D m_pathUp;
    float m_pathRoll = 0.0f;


    // ==========================================================
    // ANIMATION & MOTION STATE
    // ==========================================================
    QTimer* rotationTimer;
    QTimer* m_animTimer = nullptr;
    QElapsedTimer m_elapsedTimer;
    QElapsedTimer m_surfaceTimer;

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
    };

    // Array con i 2 slot di memoria (0 = Parametrico, 1 = Implicito)
    ViewState m_viewStates[2];

    // ==========================================================
    // PRIVATE HELPER METHODS
    // ==========================================================
    // --- Geometry & Mesh Builders ---
    void buildBorderGeometry();
    void buildWireframeGeometry();

    // --- Shader Generation & Compilation ---
    QString createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    QString createFragmentShaderSource(const QString &customCode);
    QString generateGlslHelperVars(const QString& sourceCode);
    QShader bakeShader(const QByteArray &source, QShader::Stage stage);

    // --- Pipeline & Resource Initialization ---
    void buildPipeline();
    void initBackgroundShader();

    // --- Texture Utilities ---
    void createDummyTexture();

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
