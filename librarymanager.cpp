#include "librarymanager.h"
#include <QDirIterator>
#include <QUrl>
#include <QDebug>

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
            d.color1 = col["surfColor"].toString();
            d.color2 = col["bordColor"].toString();
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
        if (root.contains("showBorder")) {
            d.showBorder = root["showBorder"].toBool();
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
        if (root.contains("code")) {
            d.scriptCode = root["code"].toString();
            d.textureCode = d.scriptCode;
            d.isTextureCustom = true; d.isImage = false;
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

        // Lettura parametri comuni (Limiti, step, costanti...)
        if (root.contains("limits")) {
            QJsonObject l = root["limits"].toObject();
            d.uMin=l["uMin"].toDouble(); d.uMax=l["uMax"].toDouble();
            d.vMin=l["vMin"].toDouble(); d.vMax=l["vMax"].toDouble();
            d.wMin=l["wMin"].toDouble(); d.wMax=l["wMax"].toDouble();
        }
        d.steps = root["steps"].toInt(100);
        if (root.contains("constants")) {
            QJsonObject c = root["constants"].toObject();
            d.a=c["A"].toDouble(0.0); d.b=c["B"].toDouble(0.0); d.c=c["C"].toDouble(0.0);
            d.d=c["D"].toDouble(0.0); d.e=c["E"].toDouble(0.0); d.f=c["F"].toDouble(0.0);
            if (c.contains("S")) d.s = c["S"].toDouble(0.0);
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
        qDebug() << "Tentativo di cancellare risorsa protetta:" << item.filePath;
        return backup; // Restituisce backup vuoto, nessuna cancellazione avviene.
    }

    // Procedura standard di cancellazione per i file utente
    backup.data = item;
    backup.originalPath = backup.data.filePath;

    QString pathInTrash;
    // moveToTrash restituisce il percorso interno al cestino, utilissimo per il nostro "Undo"
    if (QFile::moveToTrash(backup.originalPath, &pathInTrash)) {
        backup.isValid = true;
        backup.backupPath = pathInTrash;
        targetList->removeAt(index);
    }

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
