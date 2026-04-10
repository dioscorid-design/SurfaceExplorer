#ifndef GLWIDGET_H
#define GLWIDGET_H

#include "surfaceengine.h"

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
    float padding[2];
};

class GLWidget : public QRhiWidget
{
    Q_OBJECT

public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget();

    QRhi* getRhi() const { return rhi(); }

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
    // EQUATIONS & MATHEMATICS
    // ==========================================================
    bool setParametricEquations(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    void setExplicitWEquation(const QString &eq);
    void setEquationConstants(float a, float b, float c, float d, float e, float f, float s);
    void setRangeU(float min, float max);
    void setRangeV(float min, float max);
    void setRangeW(float min, float max);
    void setResolution(int n);
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
    void setSurfaceScale(float s) { m_surfaceScale = s; update(); }
    bool rebuildShader();

    // ==========================================================
    // TEXTURES, SCRIPTS & BACKGROUND
    // ==========================================================
    void loadTextureFromFile(const QString &filename);
    void loadTextureFromImage(const QImage &img);
    void setTextureEnabled(bool enable);
    void setTextureColors(const QColor& c1, const QColor& c2);
    void resetTexture();
    void clearTexture();

    void setScriptCheck(bool enabled);
    void loadCustomShader(const QString &customCode);
    void setShaderTime(float t);

    void setBackgroundColor(const QColor &color);
    void setBackgroundTexture(const QString &path);
    void setBackgroundTextureEnabled(bool enabled);
    bool isBackgroundTextureEnabled() const { return m_useBackgroundTexture; }
    void loadBackgroundScript(const QString &scriptCode);

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

    void setCameraPosAndLookAt(const QVector3D& pos, float wValue);
    void setCameraPosAndDirection(const QVector3D& pos, const QVector3D& target, float wValue);
    void setCameraPosAndDirection3D(const QVector3D& pos, const QVector3D& target, float roll);
    void setCameraFrom4DVectors(const QVector4D &pos4D, const QVector4D &target4D, const QVector4D &up4D);

    void zoomCamera(float delta);
    void addCameraRotation(float dYaw, float dPitch);
    void addCameraRoll(float dRoll);
    void moveCameraFromScreenDelta(float dx, float dy);
    void resetTransformations();
    void virtualMove(MoveDir dir, bool slowMode);

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

    void setSlowMode(bool active) { m_slowModeActive = active; }
    bool isAnimating() const { return rotationTimer && rotationTimer->isActive(); }
    void pauseMotion();
    void resumeMotion();

    void startAnimationTimer();
    void stopAnimationTimer();
    void stopAllTimers();
    void resetTime();
    void resetSurfaceTime();
    void setSurfaceAnimating(bool animating);
    bool isSurfaceAnimating() const { return m_surfaceAnimating; }

    // ==========================================================
    // UTILITIES
    // ==========================================================
    int projectionMode = 0;
    QImage getFrameForVideo(int targetW = -1, int targetH = -1, bool useFbo = false);

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

private slots:
    void updateRotation();

private:
    // ==========================================================
    // CORE ARCHITECTURE
    // ==========================================================
    std::unique_ptr<SurfaceEngine> engine;
    std::unique_ptr<InputHandler> m_inputHandler;


    // ==========================================================
    // TEXTURE
    // ==========================================================
    QRhiTexture *m_surfaceTexture = nullptr;
    QImage m_pendingSurfaceImage;
    bool m_surfaceTextureNeedsUpload = false;

    QRhiTexture *m_backgroundTexture = nullptr;
    QImage m_pendingBackgroundImage;
    bool m_backgroundTextureNeedsUpload = false;
    QRhiBuffer *m_bgVbo = nullptr;
    QRhiGraphicsPipeline *m_bgPipeline = nullptr;
    QRhiShaderResourceBindings *m_bgBindings = nullptr;
    bool m_bgVboUploaded = false;
    QString m_customFragmentCode;
    QString m_bgScriptCode;


    // ==========================================================
    // RISORSE QRHI (Sostituiscono i vecchi QOpenGLBuffer e Shader)
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
    // MATHEMATICAL & GEOMETRY STATE
    // ==========================================================
    QString m_eqX, m_eqY, m_eqZ, m_eqW;
    bool meshNeedsUpdate = true;
    int m_indexCount = 0;
    int m_borderVertexCount = 0;
    int wfStepU = 4;
    int wfStepV = 4;
    float m_surfaceScale = 2.0f;

    // ==========================================================
    // RENDERING & TEXTURE STATE
    // ==========================================================
    int renderMode = 0;
    bool showBorders = false;
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

    bool m_slowModeActive = false;
    bool m_surfaceAnimating = false;
    float m_manualTime = 0.0f;
    float m_surfaceTimeOffset = 0.0f;

    float nutation = 0, precession = 0, spin = 0;
    float omega = 0, phi = 0, psi = 0;
    float nutationSpeed = 0, precessionSpeed = 0, spinSpeed = 0;
    float omegaSpeed = 0, phiSpeed = 0, psiSpeed = 0;

    QVector3D m_lastValidUp{0.0f, 1.0f, 0.0f};
    bool m_isFirstPathRun{true};

    // ==========================================================
    // PRIVATE HELPER METHODS
    // ==========================================================
    void buildBorderGeometry();
    void buildWireframeGeometry();

    void initBackgroundShader();
    void rebuildBackgroundShader(bool isTextureMode, const QString &customCode = "");

    QString createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    QString createFragmentShaderSource(const QString &customCode);
    void createDummyTexture();

    // Sostituisci la riga di prima con questa:
    QShader bakeShader(const QByteArray &source, QShader::Stage stage);

    QVector3D projectPoint4Dto3D(const QVector4D& point4D);

    void buildPipeline();
    QImage generateCheckerboard();

    // ==========================================================
    // UTILITIES
    // ==========================================================
    bool m_useFbo = false;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
};

#endif // GLWIDGET_H
