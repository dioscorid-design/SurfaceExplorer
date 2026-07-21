#include "librarymanager.h"
#include <QDirIterator>
#include <QUrl>
#include <QDebug>
#include <QSettings>
#include <QColor>
#include <QRegularExpression>

LibraryManager::LibraryManager() {}

void LibraryManager::clear()
{
    m_surfaces.clear();
    m_textures.clear();
    m_motions.clear();
    m_sounds.clear();
}

const LibraryItem& LibraryManager::getSurface(int index) const {
    if (index >= 0 && index < m_surfaces.size()) return m_surfaces[index];
    static LibraryItem dummy; return dummy;
}

const LibraryItem* LibraryManager::getSurfaceByPath(const QString &filePath) const {
    for (const LibraryItem &it : m_surfaces) {
        if (it.filePath == filePath) return &it;
    }
    return nullptr;
}

const LibraryItem& LibraryManager::getTexture(int index) const {
    if (index >= 0 && index < m_textures.size()) return m_textures[index];
    static LibraryItem dummy; return dummy;
}

const LibraryItem& LibraryManager::getMotion(int index) const {
    if (index >= 0 && index < m_motions.size()) return m_motions[index];
    static LibraryItem dummy; return dummy;
}

const LibraryItem& LibraryManager::getSound(int index) const {
    if (index >= 0 && index < m_sounds.size()) return m_sounds[index];
    static LibraryItem dummy; return dummy;
}

void LibraryManager::loadFromDirectory(const QString &dirPath, QTreeWidget *tree, LibraryType type)
{
    QString rootNameRaw = QFileInfo(dirPath).fileName();
    if (rootNameRaw.isEmpty()) rootNameRaw = QDir(dirPath).dirName();
    QString rootName = QUrl::fromPercentEncoding(rootNameRaw.toUtf8());

    QStringList validExtensions;
    if (type == LibraryType::Texture) {
        validExtensions << "json" << "png" << "jpg" << "jpeg" << "bmp";
    }
    // ---> FIX 1: Diciamo al programma di cercare anche i file audio! <---
    else if (type == LibraryType::Sound) {
        validExtensions << "json" << "mp3" << "wav" << "ogg";
    }
    else {
        validExtensions << "json";
    }

    QDirIterator it(dirPath, QStringList(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    if (tree->columnCount() < 1) tree->setColumnCount(1);

    while (it.hasNext()) {
        QString rawFullPath = it.next();
        QFileInfo fileInfo(rawFullPath);

        // Calcoliamo i percorsi puliti per navigare l'albero
        QString decodedFullPath = QUrl::fromPercentEncoding(rawFullPath.toUtf8());
        QString cleanFull = QDir::cleanPath(decodedFullPath);
        QStringList pathParts = cleanFull.split('/', Qt::SkipEmptyParts);

        int rootIndex = -1;
        for (int i = pathParts.size() - 2; i >= 0; --i) {
            if (pathParts[i] == rootName) {
                rootIndex = i;
                break;
            }
        }

        // --- A. GESTIONE CARTELLE (Se è una directory) ---
        if (fileInfo.isDir()) {
            if (rootIndex != -1) {
                QTreeWidgetItem *parentNode = nullptr;
                for (int i = rootIndex + 1; i < pathParts.size(); ++i) {
                    parentNode = getOrCreateSubCategory(tree, parentNode, pathParts[i]);

                    QString segmentPath = QStringList(pathParts.mid(0, i + 1)).join("/");
#ifdef Q_OS_WIN
                    // Su Windows, se mid(0) include la lettera drive, va bene.
#else
                    if (!segmentPath.startsWith("/")) segmentPath.prepend("/");
#endif
                    parentNode->setData(0, Qt::UserRole + 10, segmentPath);
                }
            }
            continue;
        }

        // --- B. GESTIONE FILE (Se è un file valido) ---
        if (!validExtensions.contains(fileInfo.suffix(), Qt::CaseInsensitive)) {
            continue;
        }

        QList<LibraryItem> *targetList = nullptr;
        if (type == LibraryType::Surface) targetList = &m_surfaces;
        else if (type == LibraryType::Texture) targetList = &m_textures;
        else if (type == LibraryType::Motion) targetList = &m_motions;
        else if (type == LibraryType::Sound) targetList = &m_sounds;

        bool alreadyLoaded = false;
        for (const auto &item : *targetList) {
            if (item.filePath == rawFullPath) {
                alreadyLoaded = true;
                break;
            }
        }
        if (alreadyLoaded) continue;

        LibraryItem item;
        item.filePath = rawFullPath;
        item.type = type;
        bool valid = false;

        if (rawFullPath.endsWith(".json", Qt::CaseInsensitive)) {
            item = parseJson(rawFullPath, type);
            if (!item.name.isEmpty()) valid = true;
        }
        else if (type == LibraryType::Texture) {
            item.name = QFileInfo(rawFullPath).baseName();
            item.isImage = true;
            valid = true;
        }
        // ---> FIX 2: Creiamo l'oggetto in memoria col nome del brano <---
        else if (type == LibraryType::Sound) {
            item.name = QFileInfo(rawFullPath).baseName(); // Rimuove l'estensione per pulizia visiva
            valid = true;
        }

        if (!valid) continue;

        targetList->append(item);
        int listIndex = targetList->size() - 1;

        // Costruzione Albero per il FILE
        if (rootIndex != -1 && rootIndex < pathParts.size() - 1) {
            QTreeWidgetItem *parentNode = nullptr;
            for (int i = rootIndex + 1; i < pathParts.size() - 1; ++i) {
                parentNode = getOrCreateSubCategory(tree, parentNode, pathParts[i]);

                QString segmentPath = QStringList(pathParts.mid(0, i + 1)).join("/");
#ifndef Q_OS_WIN
                if (!segmentPath.startsWith("/")) segmentPath.prepend("/");
#endif
                parentNode->setData(0, Qt::UserRole + 10, segmentPath);
            }

            QTreeWidgetItem *fileItem = new QTreeWidgetItem();
            QString displayText = item.name;
            if (type == LibraryType::Texture && item.isImage) displayText = "[IMG] " + item.name;
            // ---> FIX 3: Aggiunge la scritta [AUDIO] prima del nome nell'albero <---
            else if (type == LibraryType::Sound && !rawFullPath.endsWith(".json", Qt::CaseInsensitive)) displayText = "[AUDIO] " + item.name;

            fileItem->setText(0, displayText);
            fileItem->setToolTip(0, item.filePath);

            int roleOffset = 0;
            if (type == LibraryType::Surface) roleOffset = 0;
            else if (type == LibraryType::Texture) roleOffset = 1;
            else if (type == LibraryType::Motion) roleOffset = 2;
            else if (type == LibraryType::Sound) roleOffset = 3;

            fileItem->setData(0, Qt::UserRole + roleOffset, listIndex);

            if (parentNode) parentNode->addChild(fileItem);
            else tree->addTopLevelItem(fileItem);
        } else {
            // Caso file nella root
            QTreeWidgetItem *fileItem = new QTreeWidgetItem();
            QString displayText = item.name;
            if (type == LibraryType::Texture && item.isImage) displayText = "[IMG] " + item.name;
            // ---> FIX 3: Ripetuto per i file nella radice principale <---
            else if (type == LibraryType::Sound && !rawFullPath.endsWith(".json", Qt::CaseInsensitive)) displayText = "[AUDIO] " + item.name;

            fileItem->setText(0, displayText);
            fileItem->setToolTip(0, item.filePath);

            int roleOffset = 0;
            if (type == LibraryType::Surface) roleOffset = 0;
            else if (type == LibraryType::Texture) roleOffset = 1;
            else if (type == LibraryType::Motion) roleOffset = 2;
            else if (type == LibraryType::Sound) roleOffset = 3;

            fileItem->setData(0, Qt::UserRole + roleOffset, listIndex);
            tree->addTopLevelItem(fileItem);
        }
    }
    tree->sortItems(0, Qt::AscendingOrder);
}

QTreeWidgetItem* LibraryManager::getOrCreateSubCategory(QTreeWidget* tree, QTreeWidgetItem* parent, const QString& name)
{
    int childCount = (parent) ? parent->childCount() : tree->topLevelItemCount();
    for (int i = 0; i < childCount; ++i) {
        QTreeWidgetItem* item = (parent) ? parent->child(i) : tree->topLevelItem(i);
        if (item->text(0) == name) return item;
    }

    QTreeWidgetItem* newItem = new QTreeWidgetItem();
    newItem->setText(0, name);
    newItem->setData(0, Qt::UserRole, QVariant());

    if (parent) parent->addChild(newItem);
    else tree->addTopLevelItem(newItem);

    newItem->setExpanded(false);
    return newItem;
}

LibraryItem LibraryManager::parseJson(const QString &filePath, LibraryType type)
{
    LibraryItem d;
    d.filePath = filePath;
    d.type = type;
    d.name = QFileInfo(filePath).baseName();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return d;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull()) return d;
    QJsonObject root = doc.object();
    QString jsonType = root["type"].toString();

    // --- MOTION PARSING ---
    if (type == LibraryType::Motion) {
        if (jsonType != "motion") { d.name = ""; return d; }

        if (root.contains("speeds")) {
            QJsonObject s = root["speeds"].toObject();
            d.speedNut = s["nutation"].toDouble(); d.speedPrec = s["precession"].toDouble(); d.speedSpin = s["spin"].toDouble();
            d.speedOmega = s["omega"].toDouble(); d.speedPhi = s["phi"].toDouble(); d.speedPsi = s["psi"].toDouble();
        }
        if (root.contains("equations")) {
            QJsonObject eq = root["equations"].toObject();
            d.x = eq["x"].toString(); d.y = eq["y"].toString(); d.z = eq["z"].toString(); d.w = eq["p"].toString();
            d.explicitW = eq["explicitW"].toString();
            d.explicitU = eq["explicitU"].toString();
            d.explicitV = eq["explicitV"].toString();
            d.defU = eq["defU"].toString();
            d.defV = eq["defV"].toString();
            d.defW = eq["defW"].toString();
        }
        if (root.contains("geodesic")) {
            QJsonObject geo = root["geodesic"].toObject();
            d.geoU0 = geo["u0"].toString();
            d.geoV0 = geo["v0"].toString();
            d.geoW0 = geo["w0"].toString();
            d.geoDU = geo["du"].toString();
            d.geoDV = geo["dv"].toString();
            d.geoDW = geo["dw"].toString();
            d.geoConform = geo["conform"].toString();
        }
        if (root.contains("isImplicitMode")) {
            d.isImplicitMode = root["isImplicitMode"].toBool();
            d.implicitEq = root["implicitEquation"].toString();
        }
        if (root.contains("path4D")) {
            QJsonObject p4 = root["path4D"].toObject();
            d.path4D_x = p4["x"].toString();
            d.path4D_y = p4["y"].toString();
            d.path4D_z = p4["z"].toString();
            d.path4D_w = p4["w"].toString();
            d.path4D_alpha = p4["alpha"].toString();
            d.path4D_beta  = p4["beta"].toString();
            d.path4D_gamma = p4["gamma"].toString();
        }
        if (root.contains("path3D")) {
            QJsonObject p3 = root["path3D"].toObject();
            d.path3D_x = p3["x"].toString();
            d.path3D_y = p3["y"].toString();
            d.path3D_z = p3["z"].toString();
            d.path3D_roll = p3["roll"].toString();
        }
        if (root.contains("scriptCode")) {
            d.isScript = true;
            d.scriptCode = root["scriptCode"].toString();
        }
        if (root.contains("limits")) {
            QJsonObject l = root["limits"].toObject();
            d.uMin=l["uMin"].toDouble(); d.uMax=l["uMax"].toDouble();
            d.vMin=l["vMin"].toDouble(); d.vMax=l["vMax"].toDouble();
            d.wMin=l["wMin"].toDouble(); d.wMax=l["wMax"].toDouble();

            d.xMin=l["xMin"].toDouble(-1000.0); d.xMax=l["xMax"].toDouble(1000.0);
            d.yMin=l["yMin"].toDouble(-1000.0); d.yMax=l["yMax"].toDouble(1000.0);
            d.zMin=l["zMin"].toDouble(-1000.0); d.zMax=l["zMax"].toDouble(1000.0);
        }
        d.steps = root["steps"].toInt(100);
        if (root.contains("constants")) {
            QJsonObject c = root["constants"].toObject();
            d.a=c["A"].toDouble(0.0); d.b=c["B"].toDouble(0.0); d.c=c["C"].toDouble(0.0);
            d.d=c["D"].toDouble(0.0); d.e=c["E"].toDouble(0.0); d.f=c["F"].toDouble(0.0);
            if (c.contains("S")) d.s = c["S"].toDouble(0.0);
        }
        if (root.contains("colors")) {
            QJsonObject col = root["colors"].toObject();
            // Due formati storici per il colore superficie:
            //  - stringa "#rrggbb" in "surfColor" (saveScript);
            //  - componenti numeriche r/g/b 0..1 (saveSurface, es. Ergosphere.json).
            // Il reader leggeva solo il primo: i preset salvati col secondo
            // restavano senza colore (default verde) e non trasparenti. Accettiamo
            // entrambi, convertendo r/g/b nella stringa "#rrggbb" attesa a valle.
            // (La vecchia chiave "bordColor" di preset legacy viene semplicemente ignorata.)
            if (col.contains("surfColor")) {
                d.color1 = col["surfColor"].toString();
            } else if (col.contains("r")) {
                QColor surf = QColor::fromRgbF(col["r"].toDouble(), col["g"].toDouble(), col["b"].toDouble());
                d.color1 = surf.name();
            }
            d.hasCustomColors = true;

            if (col.contains("alpha")) {
                d.alpha = col["alpha"].toDouble(1.0);
            }
        }
        if (root.contains("background")) {
            QJsonObject bg = root["background"].toObject();
            d.bgTextureEnabled = bg["enabled"].toBool();
            d.bgTextureCode = bg["code"].toString();
            if (bg.contains("color")) d.bgColor = bg["color"].toString();
        }
        if (root.contains("texture")) {
            QJsonObject tex = root["texture"].toObject();
            d.textureEnabled = tex["enabled"].toBool();
            if (tex.contains("displacement")) {
                d.displacementCode = tex["displacement"].toString();
            }
            if (tex.contains("zoom")) d.zoom = tex["zoom"].toDouble(1.0);
            if (tex.contains("pan_x")) d.panX = tex["pan_x"].toDouble(0.0);
            if (tex.contains("pan_y")) d.panY = tex["pan_y"].toDouble(0.0);
            if (tex.contains("rotation")) d.rotation = tex["rotation"].toDouble(0.0);

            // Colori
            if (tex.contains("col1") && tex.contains("col2")) {
                d.texColor1 = tex["col1"].toString();
                d.texColor2 = tex["col2"].toString();
            }

            // Codice (Texture Script o IMG path)
            if (tex.contains("code")) {
                d.textureCode = tex["code"].toString();
                // Importante: se c'è codice o path immagine, è "custom"
                d.isTextureCustom = !d.textureCode.isEmpty();
            }
        }
        if (root.contains("lightingMode")) {
            d.lightingMode = root["lightingMode"].toInt();
        }
        if (root.contains("lightIntensity")) {
            d.lightIntensity = root["lightIntensity"].toDouble(1.0);
        }
        if (root.contains("use4DLighting")) {
            d.use4DLighting = root["use4DLighting"].toBool();
            d.hasLightingState = true;
        }
        if (root.contains("renderMode")) {
            d.renderMode = root["renderMode"].toInt();
        } else {
            d.renderMode = 0;
        }
        if (root.contains("projectionMode")) {
            d.projectionMode = root["projectionMode"].toInt();
        }
        d.cameraFov = (float)root["cameraFov"].toDouble(45.0);
        // FOV indipendenti dei due path; i JSON vecchi (solo cameraFov) lo
        // ereditano su entrambi.
        d.fov3D = (float)root["fov3D"].toDouble(d.cameraFov);
        d.fov4D = (float)root["fov4D"].toDouble(d.cameraFov);

        // Densità wireframe (opzionale): assente nei preset vecchi -> hasWireframe resta
        // false e il load applica il default. Il tag STEP_DEF=4 lato GLWidget clampa i valori.
        if (root.contains("wireframe")) {
            QJsonObject wf = root["wireframe"].toObject();
            d.hasWireframe = true;
            d.wireframeUStep = wf["uStep"].toInt(4);
            d.wireframeVStep = wf["vStep"].toInt(4);
        }

        if (root.contains("camera3D")) {
            d.hasCamera3D = true;
            QJsonObject cam = root["camera3D"].toObject();
            d.camX = cam["x"].toDouble(0.0);
            d.camY = cam["y"].toDouble(0.0);
            d.camZ = cam["z"].toDouble(4.0);
            d.rotW = cam["rot_w"].toDouble(1.0);
            d.rotX = cam["rot_x"].toDouble(0.0);
            d.rotY = cam["rot_y"].toDouble(0.0);
            d.rotZ = cam["rot_z"].toDouble(0.0);
            d.camYaw = cam["yaw"].toDouble(0.0);
            d.camPitch = cam["pitch"].toDouble(0.0);
            d.camRoll = cam["roll"].toDouble(0.0);
        }
        if (root.contains("angles")) {
            QJsonObject a = root["angles"].toObject();
            d.startOmega = a["omega"].toDouble(0.0);
            d.startPhi   = a["phi"].toDouble(0.0);
            d.startPsi   = a["psi"].toDouble(0.0);
            d.restoreAngles = true;
        }

        return d;
    }

    // --- SOUND ---
    if (type == LibraryType::Sound) {
        if (root.contains("code")) {
            d.isScript = true;
            d.scriptCode = root["code"].toString();
        }
        return d;
    }

    // --- TEXTURE ---
    if (type == LibraryType::Texture) {
        if (root.contains("equations") || root.contains("scriptCode") || jsonType == "motion") { d.name = ""; return d; }
        if (root.contains("isImplicitMode")) {
            d.isImplicitMode = root["isImplicitMode"].toBool();
        }
        if (root.contains("displacement")) {
            d.displacementCode = root["displacement"].toString();
        }
        if (root.contains("code")) {
            d.scriptCode = root["code"].toString();
            d.textureCode = d.scriptCode;
            d.isTextureCustom = true; d.isImage = false;

            // Una texture-immagine viene salvata come JSON col path della PNG nel
            // tag "//IMG:<path>" in cima al code. Va riconosciuta qui, altrimenti
            // isImage resta false -> al load il ramo immagine non parte, la
            // protezione anti-cambio-modo salta e la scena collassa in RM default.
            QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
            QRegularExpressionMatch imgMatch = imgRe.match(d.scriptCode);
            if (imgMatch.hasMatch()) {
                QString imgPath = imgMatch.captured(1).trimmed();
                if (!imgPath.isEmpty()) {
                    d.isImage = true;
                    d.imagePath = imgPath;
                }
            }

            if (root.contains("zoom")) d.zoom = root["zoom"].toDouble(1.0);
            if (root.contains("pan_x")) d.panX = root["pan_x"].toDouble(0.0);
            if (root.contains("pan_y")) d.panY = root["pan_y"].toDouble(0.0);
            if (root.contains("rotation")) d.rotation = root["rotation"].toDouble(0.0);
            if (root.contains("color1") && root.contains("color2")) {
                d.hasCustomColors = true;
                d.color1 = root["color1"].toString(); d.color2 = root["color2"].toString();
                d.texColor1 = d.color1; d.texColor2 = d.color2;
            }
        }
        return d;
    }

    // --- SURFACE ---
    else {
        // Controllo di sicurezza sul tipo
        if (jsonType == "custom_texture" || jsonType == "motion") { d.name = ""; return d; }

        // Caso 1: È uno SCRIPT
        if (root.contains("scriptCode")) {
            d.isScript = true;
            d.scriptCode = root["scriptCode"].toString();

            // Carichiamo anche i parametri UI di backup se presenti
            if (root.contains("equations")) {
                QJsonObject eq = root["equations"].toObject();
                d.x = eq["x"].toString();
                d.y = eq["y"].toString();
                d.z = eq["z"].toString();
                d.w = eq["p"].toString();
            }
            // Mappa di visualizzazione custom di uno script metrico (Flamm ecc.)
            if (root.contains("metricDisplayMap")) {
                QJsonObject map = root["metricDisplayMap"].toObject();
                d.hasMetricMap = true;
                d.metricMapX = map["x"].toString();
                d.metricMapY = map["y"].toString();
                d.metricMapZ = map["z"].toString();
                d.metricMapP = map["p"].toString();
            }
        }
        // Caso 2: È una SUPERFICIE PARAMETRICA
        else if (root.contains("equations")) {
            d.isScript = false;
            QJsonObject eq = root["equations"].toObject();
            d.x = eq["x"].toString();
            d.y = eq["y"].toString();
            d.z = eq["z"].toString();
            d.w = eq["p"].toString();

            // --- AGGIUNTA FONDAMENTALE PER I VINCOLI ---
            d.explicitW = eq["explicitW"].toString();
            d.explicitU = eq["explicitU"].toString();
            d.explicitV = eq["explicitV"].toString();
            d.defU = eq["defU"].toString();
            d.defV = eq["defV"].toString();
            d.defW = eq["defW"].toString();
        }
        else { d.name = ""; return d; }
        if (root.contains("geodesic")) {
            QJsonObject geo = root["geodesic"].toObject();
            d.geoU0 = geo["u0"].toString();
            d.geoV0 = geo["v0"].toString();
            d.geoW0 = geo["w0"].toString();
            d.geoDU = geo["du"].toString();
            d.geoDV = geo["dv"].toString();
            d.geoDW = geo["dw"].toString();
            d.geoConform = geo["conform"].toString();
        }
        // Caso 3: E' una superficie implicita
        if (root.contains("isImplicitMode")) {
            d.isImplicitMode = root["isImplicitMode"].toBool();
            d.implicitEq = root["implicitEquation"].toString();
        }
        // Lettura parametri comuni (Limiti, step, costanti...)
        if (root.contains("limits")) {
            QJsonObject l = root["limits"].toObject();
            d.uMin=l["uMin"].toDouble(); d.uMax=l["uMax"].toDouble();
            d.vMin=l["vMin"].toDouble(); d.vMax=l["vMax"].toDouble();
            d.wMin=l["wMin"].toDouble(); d.wMax=l["wMax"].toDouble();

            d.xMin=l["xMin"].toDouble(-1000.0); d.xMax=l["xMax"].toDouble(1000.0);
            d.yMin=l["yMin"].toDouble(-1000.0); d.yMax=l["yMax"].toDouble(1000.0);
            d.zMin=l["zMin"].toDouble(-1000.0); d.zMax=l["zMax"].toDouble(1000.0);
        }
        d.steps = root["steps"].toInt(100);
        if (root.contains("constants")) {
            QJsonObject c = root["constants"].toObject();
            d.a=c["A"].toDouble(0.0); d.b=c["B"].toDouble(0.0); d.c=c["C"].toDouble(0.0);
            d.d=c["D"].toDouble(0.0); d.e=c["E"].toDouble(0.0); d.f=c["F"].toDouble(0.0);
            if (c.contains("S")) d.s = c["S"].toDouble(0.0);
        }

        // COLORE + TRASPARENZA della superficie. Questo ramo (type=="surface")
        // prima IGNORAVA del tutto "colors": ogni superficie si ricaricava verde
        // di default e opaca, qualunque cosa fosse salvata. Accettiamo entrambi i
        // formati storici (r/g/b numerici di saveSurface, "#rrggbb" di saveScript)
        // + alpha. applySurfaceExample riapplica d.color1/d.alpha dopo il reset.
        if (root.contains("colors")) {
            QJsonObject col = root["colors"].toObject();
            if (col.contains("surfColor")) {
                d.color1 = col["surfColor"].toString();
            } else if (col.contains("r")) {
                QColor surf = QColor::fromRgbF(col["r"].toDouble(), col["g"].toDouble(), col["b"].toDouble());
                d.color1 = surf.name();
            }
            d.hasCustomColors = !d.color1.isEmpty();
            if (col.contains("alpha")) d.alpha = col["alpha"].toDouble(1.0);
        }

        if (root.contains("lightingMode")) {
            d.lightingMode = root["lightingMode"].toInt();
        }
        if (root.contains("lightIntensity")) {
            d.lightIntensity = root["lightIntensity"].toDouble(1.0);
        }
        if (root.contains("use4DLighting")) {
            d.use4DLighting = root["use4DLighting"].toBool();
            d.hasLightingState = true;
        }
        if (root.contains("renderMode")) {
            d.renderMode = root["renderMode"].toInt();
        } else {
            d.renderMode = 0;
        }
        if (root.contains("projectionMode")) {
            d.projectionMode = root["projectionMode"].toInt();
        }
        d.cameraFov = (float)root["cameraFov"].toDouble(45.0);
        // FOV indipendenti dei due path; i JSON vecchi (solo cameraFov) lo
        // ereditano su entrambi.
        d.fov3D = (float)root["fov3D"].toDouble(d.cameraFov);
        d.fov4D = (float)root["fov4D"].toDouble(d.cameraFov);

        // DENSITA' WIREFRAME. Veniva letta SOLO nel ramo Motion (sopra): il ramo
        // Surface la ignorava del tutto, quindi le superfici si ricaricavano SEMPRE
        // con wireframe di default, qualunque cosa fosse salvata nel JSON.
        if (root.contains("wireframe")) {
            QJsonObject wf = root["wireframe"].toObject();
            d.hasWireframe = true;
            d.wireframeUStep = wf["uStep"].toInt(4);
            d.wireframeVStep = wf["vStep"].toInt(4);
        }
    }

    if (root.contains("camera3D")) {
        d.hasCamera3D = true;
        QJsonObject cam = root["camera3D"].toObject();
        d.camX = cam["x"].toDouble(0.0);
        d.camY = cam["y"].toDouble(0.0);
        d.camZ = cam["z"].toDouble(4.0);
        d.rotW = cam["rot_w"].toDouble(1.0);
        d.rotX = cam["rot_x"].toDouble(0.0);
        d.rotY = cam["rot_y"].toDouble(0.0);
        d.rotZ = cam["rot_z"].toDouble(0.0);
        d.camYaw = cam["yaw"].toDouble(0.0);
        d.camPitch = cam["pitch"].toDouble(0.0);
        d.camRoll = cam["roll"].toDouble(0.0);
    }

    if (root.contains("angles")) {
        // Preset Nuovi
        QJsonObject a = root["angles"].toObject();
        d.startOmega = a["omega"].toDouble(0.0);
        d.startPhi   = a["phi"].toDouble(0.0);
        d.startPsi   = a["psi"].toDouble(0.0);
        d.restoreAngles = true;
    } else {
        // Fallback per Preset Vecchi
        d.startOmega = root["omega"].toDouble(0.0);
        d.startPhi   = root["phi"].toDouble(0.0);
        d.startPsi   = root["psi"].toDouble(0.0);
        if (d.startOmega != 0.0 || d.startPhi != 0.0 || d.startPsi != 0.0) {
            d.restoreAngles = true;
        }
    }

    return d;
}

DeletionBackup LibraryManager::softDelete(int index, LibraryType type)
{
    DeletionBackup backup;
    backup.isValid = false; // Default: azione fallita

    QList<LibraryItem> *targetList = nullptr;
    if (type == LibraryType::Surface) targetList = &m_surfaces;
    else if (type == LibraryType::Texture) targetList = &m_textures;
    else if (type == LibraryType::Motion) targetList = &m_motions;
    else if (type == LibraryType::Sound) targetList = &m_sounds;

    // Controlli di sicurezza standard
    if (!targetList || index < 0 || index >= targetList->size()) return backup;

    // Recuperiamo l'elemento
    LibraryItem item = (*targetList)[index];

    if (item.filePath.startsWith(":")) {
        return backup;
    }

    backup.data = item;
    backup.originalPath = backup.data.filePath;

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // --- MOBILE LOGIC (iOS/Android): Spostamento nel cestino interno ---
    QSettings settings;
    QString trashDir = settings.value("libraryRootPath").toString() + "/.trash";
    QDir().mkpath(trashDir);

    QString fileName = QFileInfo(backup.originalPath).fileName();
    QString internalTrashPath = trashDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_del_" + fileName;

    if (QFile::rename(backup.originalPath, internalTrashPath)) {
        backup.isValid = true;
        backup.backupPath = internalTrashPath;
        targetList->removeAt(index);
    }
#else
    // --- DESKTOP LOGIC: Cestino di sistema originale ---
    QString pathInTrash;
    if (QFile::moveToTrash(backup.originalPath, &pathInTrash)) {
        backup.isValid = true;
        backup.backupPath = pathInTrash;
        targetList->removeAt(index);
    }
#endif

    return backup;
}

bool LibraryManager::restore(const DeletionBackup &backup)
{
    if (!backup.isValid) return false;

    if (QFile::exists(backup.originalPath)) {
        QFile::remove(backup.originalPath);
    }

    QFile file(backup.backupPath);

    if (file.rename(backup.originalPath)) {
        if (backup.data.type == LibraryType::Surface) m_surfaces.append(backup.data);
        else if (backup.data.type == LibraryType::Texture) m_textures.append(backup.data);
        else if (backup.data.type == LibraryType::Motion) m_motions.append(backup.data);
        else if (backup.data.type == LibraryType::Sound) m_sounds.append(backup.data);
        return true;
    }
    return false;
}

bool LibraryManager::moveFile(const QString &oldPath, const QString &newFolder)
{
    QFile file(oldPath);
    if (!file.exists()) return false;
    QString fileName = QFileInfo(oldPath).fileName();
    QString newPath = newFolder + "/" + fileName;
    if (newPath == oldPath) return false;
    if (QFile::exists(newPath)) return false;
    return file.rename(newPath);
}
