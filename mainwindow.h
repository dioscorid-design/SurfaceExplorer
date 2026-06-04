#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSet>
#include <QLabel>
#include <QColor>
#include <functional>
#include <QButtonGroup>
#include <QProgressBar>
#include <QDebug>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimedia/QAudioOutput>
#include <QFileSystemWatcher>
#include <QVector>
#include <QVector4D>

#include "glwidget.h"
#include "librarymanager.h"
#include "synthesizer.h"

class QLineEdit;
class QPushButton;
class QCheckBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QMenu;
class QMenuBar;
class VideoRecorder;
class LibraryMenuController;
class PresetSerializer;
class LibraryFileOperations;
class LibraryDragDropHandler;
class AudioController;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class VideoRecorder;
    friend class LibraryMenuController;
    friend class PresetSerializer;
    friend class LibraryFileOperations;
    friend class LibraryDragDropHandler;
    friend class AudioController;
    friend class DesktopInputFilter;
    friend class MobileInputFilter;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    AudioController *m_audioController;


private slots:
    // ==========================================================
    // UI & STATE MANAGEMENT
    // ==========================================================
    void switchToMainMode();
    void switchTo3DMode();
    void switchTo4DMode();
    void update4DButtonState();
    void updateRenderState();
    void checkParametricDependency();
    void updateConstraintState();
    void updateConstantsUIState();
    void performMasterStop();
    void applyStartSideEffects();

    // ==========================================================
    // RENDERING & VISUALS
    // ==========================================================
    void onColorTargetChanged();
    void scheduleTextureGeneration();
    void handleTextureSelection(int index);

    // ==========================================================
    // EQUATIONS & MATHEMATICS
    // ==========================================================
    void updateULimits();
    void updateVLimits();
    void updateWLimits();

    // ==========================================================
    // ANIMATION, MOTION & TIMERS
    // ==========================================================
    void onStartClicked();
    void onStopClicked();
    void onResetViewClicked();
    void onNavTimerTick();
    void onDepartureClicked();
    void onPathTimerTick();
    void checkPathFields();
    void onDeparture3DClicked();
    void onPath3DTimerTick();
    void checkPath3DFields();
    void onToggleViewClicked();

    // ==========================================================
    // SCRIPTING ENGINE
    // ==========================================================
    void onToggleScriptMode();
    void onRunCurrentScript();
    void onRunScriptClicked();
    void onApplyTextureScriptClicked();
    void onRunRaymarchTextureClicked();
    void onRunSoundClicked();

    // ==========================================================
    // LIBRARY & WORKSPACE MANAGEMENT
    // ==========================================================
    void onExampleItemClicked(QTreeWidgetItem *item, int column);
    void applySurfaceExample(const LibraryItem &data);
    void applyMotionExample(const LibraryItem &data);
    void deleteSelectedExample();
    void onUndoDelete();
    void onAddRepositoryClicked(LibraryType type);
    void onCreateFolderClicked();
    void onSyncPresetsClicked();

    // ==========================================================
    // FILE I/O & CLIPBOARD
    // ==========================================================
    void saveSurfaceToFile(const QString &suggestedPath = QString());
    void onPasteExample();
    void onPasteTexture();
    void performCut(QTreeWidgetItem* item = nullptr);
    void performCopy(QTreeWidgetItem* targetItem = nullptr);
    void onSaveTexJsonClicked();
    void onSaveTextureClicked();
    void onSaveScriptClicked();
    void onSaveMotionClicked();

    // ==========================================================
    // AUDIO & MEDIA
    // ==========================================================
    void onSoundItemClicked(QTreeWidgetItem *item, int column);


private:
    // ==========================================================
    // CORE UI COMPONENTS
    // ==========================================================
    Ui::MainWindow *ui;
    QPushButton *m_btnStart;
    QPushButton *m_btnResetView;
    QPushButton *m_btnProjection;
    QPushButton *m_btnRec;
    QLabel *m_statusLabel;
    QLabel* m_topMessageBar;
    QTimer* m_topMessageTimer;
    QProgressBar *m_renderProgress;
    QButtonGroup *m_colorGroup;
    QButtonGroup *m_modeGroup;

    // ==========================================================
    // MATHEMATICAL CONSTANTS & LIMITS
    // ==========================================================
    const float TWO_PI = 6.28318530718f;
    float uMin = 0.0f;
    float uMax = TWO_PI;
    float vMin = 0.0f;
    float vMax = TWO_PI;
    float wMin = 0.0f;
    float wMax = 0.1f;
    int m_lastParametricSteps = 100;
    int m_lastImplicitSteps = 400;
    double m_lastParametricS = 0.0;
    double m_lastImplicitS = 0.4;
    QTimer* m_stepsDebounce = nullptr;
    QTimer* m_meshDebounce = nullptr;

    // ==========================================================
    // RENDERING & COLOR STATE
    // ==========================================================
    int m_savedRenderMode = 0;
    int m_lightingMode4D = 0;
    float alphaValue = 1.0f;

    QColor m_currentSurfaceColor;
    QColor m_currentBorderColor;
    QColor m_currentBackgroundColor;
    QColor m_texColor1 = Qt::white;
    QColor m_texColor2 = Qt::black;
    QColor m_bgTexColor1 = Qt::white;
    QColor m_bgTexColor2 = Qt::black;

    // ==========================================================
    // TEXTURE & SCRIPTING STATE
    // ==========================================================
    enum ScriptMode {
        ScriptModeSurface,
        ScriptModeTexture,
        ScriptModeSound
    };
    ScriptMode m_currentScriptMode = ScriptModeSurface;

    bool m_isCustomMode = false;
    bool m_isImageMode = false;
    bool m_surfaceTextureState = false;
    bool m_blockTextureGen = false;

    QString lastTextureFolder;
    QString m_currentTexturePath;
    QString m_currentTexturePresetPath;
    QString m_surfaceTextureCode;
    QString m_bgTextureCode;

    QString m_surfaceScriptText;
    QString m_surfaceTextureScriptText;
    QString m_bgTextureScriptText;
    QString m_soundScriptText;

    // ==========================================================
    // MOTION & PATHS
    // ==========================================================
    QTimer *navTimer;
    QSet<int> activeNavActions;

    QTimer *pathTimer;
    float pathTimeT = 0.0f;

    QTimer *pathTimer3D;
    float pathTimeT3D = 0.0f;

    float m_pathSpeed3D = 0.01f;
    float m_pathSpeed4D = 0.01f;

    enum CameraPathMode {
        ModeTangential,
        ModeCentered
    };
    CameraPathMode m_pathMode;

    bool m_masterStopped = false;

    // ==========================================================
    // LIBRARY & FILE SYSTEM
    // ==========================================================
    LibraryManager m_libraryManager;
    LibraryMenuController* m_menuController;
    PresetSerializer *m_presetSerializer;
    LibraryFileOperations *m_fileOps;
    LibraryDragDropHandler *m_dragDropHandler;

    QList<DeletionBackup> m_undoStack;
    QStringList m_cutFilePaths;
    QStringList m_cutTexturePaths;
    bool m_isCopyOperation = false;
    bool m_libraryInitialized = false;

    QFileSystemWatcher *m_fsWatcher = nullptr;
    QTimer *m_fsSyncTimer = nullptr;

    // ==========================================================
    // MEDIA & RECORDING
    // ==========================================================
    VideoRecorder *m_videoRecorder;

    bool m_isRecording = false;
    bool m_stopRecordingRequested = false;
    bool m_isProcessingVideo = false;
    QString m_recFolder;

    // ==========================================================
    // PRIVATE HELPER METHODS
    // ==========================================================

    // --- Data & Initialization ---
    void setupDefaultFolders();
    void connectSidePanels();
    void connectNavButton(QPushButton *btn, int action);

    // --- Library & File I/O ---
    void syncResourcesToFolder(const QString &resourcePath, const QString &diskPath, bool forceRestore = false, int *overwriteState = nullptr);
    void refreshRepositories();
    void updateWatcherPaths();
    void copyPath(QString src, QString dst);
    void saveTextureConfig(const QString &savePath);
    QTreeWidgetItem* getCurrentLibraryItem();
    void applyCommonData(const LibraryItem &data);
    QString presetsRootPath() const;
    bool resolveNeedsCopy(const QString& src, const QString& dst,
                          bool forceRestore, bool isDeleted, int* overwriteState);

    // --- Parsing, Strings & Scripts ---
    float parseMath(const QString &text, bool *ok = nullptr);
    float parseUIConstant(const QString &exprStr, float A, float B, float C, float D, float E, float F, float S);
    QString composeEquation(const QString &eq, const QString &uDef, const QString &vDef, const QString &wDef);
    void parseAndApplyScriptParams(const QString &scriptCode, bool restartAudio = true);
    bool hasTimeVariable(const QString& code);
    QString extractAndResolveImagePath(const QString& scriptCode);
    QString extractAudioDirectives(const QString& fullText);
    static QString cleanCodeForComparison(QString str);

    // --- UI State & Graphics ---
    void updateLayoutForMode(int mode);
    void setupSpeedControl(QPushButton* btnPlus, QPushButton* btnMinus, QLabel* label, std::function<void(float)> setter);
    void updateProjectionButtonText();
    void updateScriptButtonText();
    void updateTextureUIState(bool isTextureOn);
    void updateFlatPreviewButton();
    void updateMasterButtonState();
    void applyAnimationState(bool animated);
    void generateTexture();
    void toggleProjection();
    void showTopMessage(const QString& msg, bool isError = true);
    void hideTopMessage();
    bool applyBackgroundTextureIfNeeded();

    // --- Geometry & Geodesic Flow ---
    void snapshotActiveEquations();
    void restoreActiveEquations(const QStringList &saved);
    QStringList readActiveEquations() const;
    void commitUiFieldsDuringMotion();
    bool updateGeodesicMesh();
    void checkAndTriggerMeshUpdate();
    void stopGeodesicAnimation();
    bool isGeodesicMotionActive() const;
    bool hasGeodesicText() const;

    bool m_geodesicErrorPending = false;

    // --- Inline Math Helper ---
    static float det3x3(float a1, float a2, float a3,
                        float b1, float b2, float b3,
                        float c1, float c2, float c3)
    {
        return a1 * (b2 * c3 - b3 * c2) -
               a2 * (b1 * c3 - b3 * c1) +
               a3 * (b1 * c2 - b2 * c1);
    }


protected:
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
};

#endif // MAINWINDOW_H
