#ifndef LIBRARYMANAGER_H
#define LIBRARYMANAGER_H

#include <QString>
#include <QList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDir>

// ==========================================================
// ENUMS
// ==========================================================
enum class LibraryType {
    Surface,
    Texture,
    Motion,
    Sound
};

// ==========================================================
// DATA STRUCTURES
// ==========================================================
struct LibraryItem {
    // ==========================================================
    // CORE PROPERTIES
    // ==========================================================
    QString name;
    QString filePath;
    LibraryType type;

    // ==========================================================
    // EQUATIONS & CONSTRAINTS
    // ==========================================================
    QString x, y, z, w;
    QString explicitU, explicitV, explicitW;
    QString defU, defV, defW;

    // ==========================================================
    // MATHEMATICAL CONSTANTS & LIMITS
    // ==========================================================
    int steps = 100;
    float uMin = 0.0f, uMax = 0.0f;
    float vMin = 0.0f, vMax = 0.0f;
    float wMin = 0.0f, wMax = 0.0f;
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f, e = 0.0f, f = 0.0f, s = 0.0f;

    // ==========================================================
    // CAMERA & 3D/4D TRANSFORMATIONS
    // ==========================================================
    bool hasCamera3D = false;
    float camX = 0.0f, camY = 0.0f, camZ = 4.0f;
    float camYaw = 0.0f, camPitch = 0.0f, camRoll = 0.0f;
    float rotW = 1.0f, rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;

    // ==========================================================
    // MOTION, ANGLES & PATHS
    // ==========================================================
    float omega = 0.0f, phi = 0.0f, psi = 0.0f;
    float speedNut = 0.0f, speedPrec = 0.0f, speedSpin = 0.0f;
    float speedOmega = 0.0f, speedPhi = 0.0f, speedPsi = 0.0f;

    bool  restoreAngles = false;
    float startOmega = 0.0f, startPhi = 0.0f, startPsi = 0.0f;

    QString path3D_x, path3D_y, path3D_z, path3D_roll;
    QString path4D_x, path4D_y, path4D_z, path4D_w;
    QString path4D_alpha, path4D_beta, path4D_gamma;

    // ==========================================================
    // TEXTURES & BACKGROUNDS
    // ==========================================================
    bool textureEnabled = false;
    bool isTextureCustom = false;
    QString textureCode;

    bool bgTextureEnabled = false;
    QString bgTextureCode;

    // Texture transformations
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    float rotation = 0.0f;

    // ==========================================================
    // COLORS & APPEARANCE
    // ==========================================================
    bool isImage = false;
    bool hasCustomColors = false;

    QString bgColor;
    QString color1;
    QString color2;
    QString texColor1;
    QString texColor2;

    // ==========================================================
    // SCRIPTING
    // ==========================================================
    bool isScript = false;
    QString scriptCode;

    // ==========================================================
    // RENDERING & LIGHTING STATE
    // ==========================================================
    int renderMode = 0;
    int projectionMode = 1;
    bool showBorder = true;

    bool hasLightingState = false;
    bool use4DLighting = false;
    int lightingMode = -1;

    float alpha = 1.0f;
    float lightIntensity = 1.0f;
};

struct DeletionBackup {
    bool isValid = false;
    LibraryItem data;
    QString originalPath;
    QString backupPath;
};

// ==========================================================
// MANAGER CLASS
// ==========================================================
class LibraryManager
{
public:
    LibraryManager();

    // ==========================================================
    // PUBLIC API
    // ==========================================================
    void clear();
    void loadFromDirectory(const QString &dirPath, QTreeWidget *tree, LibraryType type);

    // ==========================================================
    // GETTERS
    // ==========================================================
    const LibraryItem& getSurface(int index) const;
    const LibraryItem& getTexture(int index) const;
    const LibraryItem& getMotion(int index) const;
    const LibraryItem& getSound(int index) const;

    // ==========================================================
    // FILE OPERATIONS & BACKUP
    // ==========================================================
    DeletionBackup softDelete(int index, LibraryType type);
    bool restore(const DeletionBackup &backup);
    bool moveFile(const QString &oldPath, const QString &newFolder);

private:
    // ==========================================================
    // INTERNAL DATA STORAGE
    // ==========================================================
    QList<LibraryItem> m_surfaces;
    QList<LibraryItem> m_textures;
    QList<LibraryItem> m_motions;
    QList<LibraryItem> m_sounds;

    // ==========================================================
    // PRIVATE HELPERS
    // ==========================================================
    QTreeWidgetItem* getOrCreateSubCategory(QTreeWidget* tree, QTreeWidgetItem* parent, const QString& name);
    LibraryItem parseJson(const QString &filePath, LibraryType type);
};

#endif // LIBRARYMANAGER_H
