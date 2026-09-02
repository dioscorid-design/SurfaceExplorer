#ifndef LIBRARYMANAGER_H
#define LIBRARYMANAGER_H

#include "surfaceengine.h"
#include <QString>
#include <QList>
#include <QHash>
#include <QPair>
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
    // GEODESIC FLOW
    // ==========================================================
    QString geoU0, geoV0, geoW0;
    QString geoDU, geoDV, geoDW;
    QString geoConform;

    // Mappa di visualizzazione (embedding) di uno script metrico: x/y/z/p custom
    // (non carta identità). hasMetricMap=false => identità, niente override.
    bool hasMetricMap = false;
    QString metricMapX, metricMapY, metricMapZ, metricMapP;

    // ==========================================================
    // IMPLICIT EQUATIONS
    // ==========================================================
    bool isImplicitMode = false;
    QString implicitEq;

    float xMin = -1.0f, xMax = 1.0f;
    float yMin = -1.0f, yMax = 1.0f;
    float zMin = -1.0f, zMax = 1.0f;

    // ==========================================================
    // MATHEMATICAL CONSTANTS & LIMITS
    // ==========================================================
    int steps = 100;
    float uMin = 0.0f, uMax = 0.0f;
    float vMin = 0.0f, vMax = 0.0f;
    float wMin = 0.0f, wMax = 0.0f;
    // Forma TESTUALE dei limiti, quando l'utente ha scritto un'espressione
    // ("2*A", "pi/B"). Vuota sui record vecchi e su quelli con limiti numerici:
    // in tal caso vale il float qui sopra. Le chiavi numeriche uMin/uMax/...
    // restano sempre scritte, cosi' i lettori precedenti continuano a leggere
    // un preset nuovo senza vedere formule che non saprebbero valutare.
    QString uMinExpr, uMaxExpr;
    QString vMinExpr, vMaxExpr;
    QString wMinExpr, wMaxExpr;
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

    // Messaggio in sovrimpressione mostrato al caricamento del record (chiavi
    // JSON "hintText"/"hintSeconds"). Vuoto = nessun messaggio.
    QString hintText;
    float   hintSeconds = 6.0f;

    // Secondo messaggio, quello della TEXTURE della scena (chiavi
    // "textureHintText"/"textureHintSeconds"), usato dai soli record: la scena
    // che catturano ha due sorgenti di costanti indipendenti -- la superficie e
    // la texture -- e con un campo solo il messaggio dell'una scacciava quello
    // dell'altra. I due si mostrano insieme (MainWindow::composedHintText).
    // Vuoto = la texture non ha nulla da dire.
    QString textureHintText;
    float   textureHintSeconds = 6.0f;

    // Costanti discrete dichiarate dal PRESET (chiave JSON "discreteConstants",
    // es. {"A": [2,6]}): la costante assume solo valori interi nel range e il
    // campo scatta all'intero piu' vicino. Equivale a "A := int(2,6);" nello
    // script, ma vale anche per i preset che lo script non ce l'hanno (equazioni
    // scritte nel dock Equations). Vuota = tutte le costanti continue.
    QHash<QString, QPair<int,int>> discreteConstants;

    // ==========================================================
    // TEXTURES & BACKGROUNDS
    // ==========================================================
    bool textureEnabled = false;
    bool isTextureCustom = false;
    QString textureCode;

    bool bgTextureEnabled = false;
    QString bgTextureCode;
    QString displacementCode;

    // Texture transformations
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    float rotation = 0.0f;

    // ==========================================================
    // COLORS & APPEARANCE
    // ==========================================================
    bool isImage = false;
    // Path dell'IMMAGINE vera (PNG/JPG). Per una texture-immagine salvata come
    // JSON, filePath e' il .json mentre imagePath e' l'immagine estratta dal tag
    // //IMG:. Per un file immagine caricato direttamente coincide con filePath.
    QString imagePath;
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
    // FOV 3D (gradi). 45 = default storico: i JSON senza chiave lo ripristinano.
    // cameraFov = chiave legacy (unico FOV); fov3D/fov4D = FOV indipendenti dei
    // due path (i JSON vecchi li ereditano da cameraFov in fase di load).
    float cameraFov = 45.0f;
    float fov3D = 45.0f;
    float fov4D = 45.0f;

    // Densità wireframe (passi campionamento U/V). hasWireframe=false => il preset non
    // la contiene (formato vecchio): il load usa il default (resetWireframeDensity).
    bool hasWireframe = false;
    int wireframeUStep = 4;
    int wireframeVStep = 4;

    // ASPETTO PER-MESH (multi-mesh): un elemento per parte, nell'ordine in cui
    // lo script le dichiara. Assente nei preset che non lo personalizzano, e in
    // quel caso ogni parte eredita dallo stato globale (comportamento storico).
    // I valori negativi dentro MeshPart significano "eredita", quindi qui si
    // riempiono solo i campi realmente presenti nel JSON.
    std::vector<MeshPart> meshParts;
    // Ambito All/Mesh al salvataggio (chiave "meshScopeAll"). Assente nei preset
    // vecchi: li' si apre in "Mesh" se il preset porta un aspetto per-mesh,
    // com'e' sempre stato.
    bool meshScopeAll = false;

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

    // Lookup per percorso file (univoco), robusto al disallineamento degli indici
    // posizionali: dopo un refresh (fsWatcher) la lista m_surfaces viene ricostruita
    // e l'ordine può cambiare, ma i vecchi nodi albero conservano l'indice vecchio.
    // Il tooltip del nodo porta sempre il filePath, quindi cerchiamo per quello.
    // Ritorna nullptr se non trovato.
    const LibraryItem* getSurfaceByPath(const QString &filePath) const;

    // Stesso ruolo di getSurfaceByPath per il ramo RECORD. Mancava, quindi il
    // click su un record risolveva ancora per indice posizionale: dopo un
    // refresh (salvataggio o modifica da Finder) la lista m_motions e'
    // ricostruita e l'ordine puo' cambiare, ma il nodo dell'albero conserva
    // l'indice VECCHIO -> si caricava un record diverso da quello cliccato.
    const LibraryItem* getMotionByPath(const QString &filePath) const;

    // PRESET SALTATI perche' non materializzati su questo disco (segnaposto di
    // iCloud o di un altro file provider): leggerli bloccherebbe il thread della
    // UI a tempo indeterminato. Vedi isDatalessFile in librarymanager.cpp.
    // Serve a MainWindow per dire quanti e quali, invece di lasciare una
    // libreria incompleta e inspiegata. Azzerata da clear().
    const QStringList& skippedDataless() const { return m_skippedDataless; }

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

    QStringList m_skippedDataless;   // vedi skippedDataless()

    // ==========================================================
    // PRIVATE HELPERS
    // ==========================================================
    QTreeWidgetItem* getOrCreateSubCategory(QTreeWidget* tree, QTreeWidgetItem* parent, const QString& name);
    LibraryItem parseJson(const QString &filePath, LibraryType type);
};

#endif // LIBRARYMANAGER_H
