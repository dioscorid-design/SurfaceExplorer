#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QTimer>
#include <memory>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QElapsedTimer>
#include <QQuaternion>

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
// --- MOBILE (Android & iOS) ---
// Usiamo OpenGL ES 3.0+ tramite ExtraFunctions
#include <QOpenGLExtraFunctions>
#define NATIVE_GL_FUNCTIONS QOpenGLExtraFunctions
#else
// --- DESKTOP (Mac, Windows, Linux) ---
// Usiamo OpenGL 4.1 Core Profile
#include <QOpenGLFunctions_4_1_Core>
#define NATIVE_GL_FUNCTIONS QOpenGLFunctions_4_1_Core
#endif

class InputHandler;
class SurfaceEngine;
class TextureManager;

class GLWidget : public QOpenGLWidget, protected NATIVE_GL_FUNCTIONS
{
    Q_OBJECT

public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget();

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
    QImage getFrameForVideo();

signals:
    void rotationChanged();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
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
    std::unique_ptr<TextureManager> m_texManager;
    std::unique_ptr<InputHandler> m_inputHandler;

    // ==========================================================
    // OPENGL BUFFERS & SHADERS
    // ==========================================================
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    QOpenGLBuffer m_ibo;
    QOpenGLBuffer m_borderVbo;
    QOpenGLBuffer m_wireframeVbo;
    QOpenGLShaderProgram *m_program = nullptr;

    QOpenGLVertexArrayObject m_bgVao;
    QOpenGLBuffer m_bgVbo;
    QOpenGLShaderProgram *m_bgProgram = nullptr;
    QOpenGLTexture *m_bgTexture = nullptr;
    GLuint m_dummyTex = 0;

    // ==========================================================
    // SHADER UNIFORM LOCATIONS
    // ==========================================================
    int m_posAttr = 0, m_normAttr = 0, m_texAttr = 0;
    int m_matrixUniform = 0, m_modelViewUniform = 0, m_modelUniform = 0;
    int m_colorUniform = 0, m_alphaUniform = 0, m_useTextureUniform = 0;
    int m_textureUniform = 0, m_specularUniform = 0, m_lightIntensityUniform = -1;
    int m_uMinUniform = -1, m_uMaxUniform = -1, m_vMinUniform = -1, m_vMaxUniform = -1;
    int m_mathParamsUniform = -1, m_mathParams2Uniform = -1, m_dummyUniform = -1;
    int m_lightingModeUniform = -1, m_observerPosUniform = -1;
    int m_cameraPosUniform = -1, m_cameraPos4DUniform = -1, m_hasExplicitWUniform = -1;
    int m_omegaUniform = -1, m_phiUniform = -1, m_psiUniform = -1;
    int m_projModeUniform = -1, m_isFlatUniform = -1, m_renderModeUniform = 0;
    int m_zoomUniform = 0, m_centerUniform = 0, m_rotationUniform = -1;
    int m_col1Uniform = 0, m_col2Uniform = 0;
    int m_iTimeUniform = -1, m_iResolutionUniform = -1, m_iMouseUniform = -1;

    // ==========================================================
    // MATHEMATICAL & GEOMETRY STATE
    // ==========================================================
    QString m_eqX, m_eqY, m_eqZ, m_eqW;
    bool meshNeedsUpdate = true;
    int m_borderVertexCount = 0;
    int m_wireframeVertexCount = 0;
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
    float m_gpuZoom = 1.0f;
    QVector2D m_gpuCenter = QVector2D(-0.748f, 0.1f);

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
    void uploadGeometry();
    void buildBorderGeometry();
    void buildWireframeGeometry();
    void uploadQuadGeometry();

    void initBackgroundShader();
    void rebuildBackgroundShader(bool isTextureMode, const QString &customCode = "");
    void drawBackground();

    QString createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    QString createFragmentShaderSource(const QString &customCode);
    void fetchUniformLocations();
    void createDummyTexture();

    QVector3D projectPoint4Dto3D(const QVector4D& point4D);
};

#endif // GLWIDGET_H
