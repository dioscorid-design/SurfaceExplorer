#include "libraryfileoperations.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QTreeWidget>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>

LibraryFileOperations::LibraryFileOperations(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

void LibraryFileOperations::performCut(QTreeWidgetItem* targetItem)
{
    QTreeWidget *targetTree = nullptr;
    QList<QTreeWidgetItem*> items;

    if (targetItem) {
        targetTree = targetItem->treeWidget();
        if (targetItem->isSelected()) items = targetTree->selectedItems();
        else items.append(targetItem);
    } else {
        QTreeWidgetItem* current = m_mainWindow->getCurrentLibraryItem();
        if (current) {
            targetTree = current->treeWidget();
            items = targetTree->selectedItems();
        }
    }

    if (items.isEmpty()) return;

    m_mainWindow->m_cutFilePaths.clear();
    m_mainWindow->m_cutTexturePaths.clear();
    m_mainWindow->m_isCopyOperation = false;

    for (QTreeWidgetItem *item : items) {
        bool success = false;
        if (item->data(0, Qt::UserRole).isValid()) {
            int index = item->data(0, Qt::UserRole).toInt();
            m_mainWindow->m_cutFilePaths.append(m_mainWindow->m_libraryManager.getSurface(index).filePath);
            success = true;
        }
        else if (item->data(0, Qt::UserRole + 2).isValid()) {
            int index = item->data(0, Qt::UserRole + 2).toInt();
            m_mainWindow->m_cutFilePaths.append(m_mainWindow->m_libraryManager.getMotion(index).filePath);
            success = true;
        }
        else if (item->data(0, Qt::UserRole + 1).isValid()) {
            int index = item->data(0, Qt::UserRole + 1).toInt();
            m_mainWindow->m_cutTexturePaths.append(m_mainWindow->m_libraryManager.getTexture(index).filePath);
            success = true;
        }
        else if (item->data(0, Qt::UserRole + 3).isValid()) {
            int index = item->data(0, Qt::UserRole + 3).toInt();
            m_mainWindow->m_cutFilePaths.append(m_mainWindow->m_libraryManager.getSound(index).filePath);
            success = true;
        }
        else if (item->data(0, Qt::UserRole + 10).isValid()) {
            m_mainWindow->m_cutFilePaths.append(item->data(0, Qt::UserRole + 10).toString());
            success = true;
        }

        if (success) {
            item->setForeground(0, QBrush(Qt::gray));
        }
    }
}

void LibraryFileOperations::performCopy(QTreeWidgetItem* targetItem)
{
    QTreeWidget *targetTree = nullptr;
    QList<QTreeWidgetItem*> items;

    if (targetItem) {
        targetTree = targetItem->treeWidget();
        if (targetItem->isSelected()) items = targetTree->selectedItems();
        else items.append(targetItem);
    } else {
        QTreeWidgetItem* current = m_mainWindow->getCurrentLibraryItem();
        if (current) {
            targetTree = current->treeWidget();
            items = targetTree->selectedItems();
        }
    }

    if (items.isEmpty()) return;

    m_mainWindow->m_cutFilePaths.clear();
    m_mainWindow->m_cutTexturePaths.clear();
    m_mainWindow->m_isCopyOperation = true;

    for (QTreeWidgetItem *item : items) {
        if (item->data(0, Qt::UserRole).isValid()) {
            m_mainWindow->m_cutFilePaths.append(m_mainWindow->m_libraryManager.getSurface(item->data(0, Qt::UserRole).toInt()).filePath);
        }
        else if (item->data(0, Qt::UserRole + 2).isValid()) {
            m_mainWindow->m_cutFilePaths.append(m_mainWindow->m_libraryManager.getMotion(item->data(0, Qt::UserRole + 2).toInt()).filePath);
        }
        else if (item->data(0, Qt::UserRole + 1).isValid()) {
            m_mainWindow->m_cutTexturePaths.append(m_mainWindow->m_libraryManager.getTexture(item->data(0, Qt::UserRole + 1).toInt()).filePath);
        }
        else if (item->data(0, Qt::UserRole + 3).isValid()) {
            m_mainWindow->m_cutFilePaths.append(m_mainWindow->m_libraryManager.getSound(item->data(0, Qt::UserRole + 3).toInt()).filePath);
        }
        else if (item->data(0, Qt::UserRole + 10).isValid()) {
            m_mainWindow->m_cutFilePaths.append(item->data(0, Qt::UserRole + 10).toString());
        }
    }

    m_mainWindow->m_cutFilePaths.removeDuplicates();
    m_mainWindow->m_cutTexturePaths.removeDuplicates();
}

void LibraryFileOperations::performPasteExample()
{
    static bool isPasting = false;
    if (isPasting) return;
    isPasting = true;

    if (m_mainWindow->m_cutFilePaths.isEmpty()) {
        isPasting = false;
        return;
    }

    QString destDir;
    QTreeWidgetItem *currentItem = m_mainWindow->getCurrentLibraryItem();
    QSettings settings;

    if (currentItem && currentItem->data(0, Qt::UserRole + 10).isValid()) {
        destDir = currentItem->data(0, Qt::UserRole + 10).toString();
    }
    else if (currentItem && currentItem->data(0, Qt::UserRole).isValid()) {
        destDir = QFileInfo(m_mainWindow->m_libraryManager.getSurface(currentItem->data(0, Qt::UserRole).toInt()).filePath).absolutePath();
    }
    else if (currentItem && currentItem->data(0, Qt::UserRole + 2).isValid()) {
        destDir = QFileInfo(m_mainWindow->m_libraryManager.getMotion(currentItem->data(0, Qt::UserRole + 2).toInt()).filePath).absolutePath();
    }

    if (destDir.isEmpty()) {
        QString rootPath = settings.value("libraryRootPath").toString();
        QString firstFile = m_mainWindow->m_cutFilePaths.first();

        if (firstFile.contains("motions", Qt::CaseInsensitive) || firstFile.contains("motion", Qt::CaseInsensitive)) {
            destDir = settings.value("pathMotions", rootPath + "/Motions").toString();
        } else if (firstFile.contains("sounds", Qt::CaseInsensitive) || firstFile.contains("sound", Qt::CaseInsensitive)) {
            destDir = settings.value("pathSounds", rootPath + "/Sounds").toString();
        } else {
            destDir = settings.value("pathSurfaces", rootPath + "/Surfaces").toString();
        }
    }

    int opCount = 0;
    for (const QString &sourcePath : m_mainWindow->m_cutFilePaths) {
        QFileInfo fi(sourcePath);
        QString newFilePath = destDir + "/" + fi.fileName();

        if (!m_mainWindow->m_isCopyOperation && QFileInfo(sourcePath).canonicalFilePath() == QFileInfo(newFilePath).canonicalFilePath()) {
            continue;
        }

        if (fi.isDir() && QDir::cleanPath(newFilePath).startsWith(QDir::cleanPath(sourcePath))) continue;

        if (QFileInfo(newFilePath).exists() && m_mainWindow->m_isCopyOperation) {
            QString baseName = fi.completeBaseName();
            QString suffix = fi.suffix();
            if(!suffix.isEmpty()) suffix = "." + suffix;

            int copyIndex = 1;
            while (QFileInfo(newFilePath).exists()) {
                newFilePath = destDir + "/" + baseName + QString("_copy%1").arg(copyIndex) + suffix;
                copyIndex++;
            }
        }

        bool ok = false;
        if (fi.isDir()) {
            if (m_mainWindow->m_isCopyOperation) {
                copyPath(sourcePath, newFilePath);
                ok = true;
            } else {
                if (QDir().rename(sourcePath, newFilePath)) ok = true;
                else {
                    copyPath(sourcePath, newFilePath);
                    QDir(sourcePath).removeRecursively();
                    ok = true;
                }
            }
        } else {
            if (m_mainWindow->m_isCopyOperation) {
                if (QFile::copy(sourcePath, newFilePath)) {
                    QFile::setPermissions(newFilePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                    ok = true;
                }
            } else {
                if (QFile::rename(sourcePath, newFilePath)) ok = true;
                else if (QFile::copy(sourcePath, newFilePath)) {
                    QFile::setPermissions(newFilePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                    QFile::remove(sourcePath);
                    ok = true;
                }
            }
        }
        if (ok) opCount++;
    }

    if (opCount > 0) {
        if (!m_mainWindow->m_isCopyOperation) m_mainWindow->m_cutFilePaths.clear();
        m_mainWindow->refreshRepositories();
    }
    isPasting = false;
}

void LibraryFileOperations::performPasteTexture()
{
    if (m_mainWindow->m_cutTexturePaths.isEmpty()) return;

    QString destDir;
    QTreeWidgetItem *currentItem = m_mainWindow->getCurrentLibraryItem();
    QSettings settings;

    if (currentItem && currentItem->data(0, Qt::UserRole + 10).isValid()) {
        destDir = currentItem->data(0, Qt::UserRole + 10).toString();
    }
    else if (currentItem && currentItem->data(0, Qt::UserRole + 1).isValid()) {
        destDir = QFileInfo(m_mainWindow->m_libraryManager.getTexture(currentItem->data(0, Qt::UserRole + 1).toInt()).filePath).absolutePath();
    }
    else {
        QString rootPath = settings.value("libraryRootPath").toString();
        destDir = settings.value("pathTextures", rootPath + "/Textures").toString();
    }

    if (destDir.isEmpty()) return;

    int opCount = 0;
    for (const QString &sourcePath : m_mainWindow->m_cutTexturePaths) {
        QFileInfo fi(sourcePath);
        QString newFilePath = destDir + "/" + fi.fileName();

        if (!m_mainWindow->m_isCopyOperation && QFileInfo(sourcePath).canonicalFilePath() == QFileInfo(newFilePath).canonicalFilePath()) {
            continue;
        }

        if (QFileInfo(newFilePath).exists()) {
            QString baseName = fi.completeBaseName();
            QString suffix = fi.suffix();
            if(!suffix.isEmpty()) suffix = "." + suffix;
            int copyIndex = 1;
            while (QFileInfo(newFilePath).exists()) {
                newFilePath = destDir + "/" + baseName + QString("_copy%1").arg(copyIndex) + suffix;
                copyIndex++;
            }
        }

        bool ok = false;
        if (m_mainWindow->m_isCopyOperation) {
            if (QFile::copy(sourcePath, newFilePath)) {
                QFile::setPermissions(newFilePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                ok = true;
            }
        } else {
            if (QFile::rename(sourcePath, newFilePath)) ok = true;
            else if (QFile::copy(sourcePath, newFilePath)) {
                QFile::setPermissions(newFilePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                QFile::remove(sourcePath);
                ok = true;
            }
        }
        if (ok) opCount++;
    }

    if (opCount > 0) {
        if (!m_mainWindow->m_isCopyOperation) m_mainWindow->m_cutTexturePaths.clear();
        m_mainWindow->refreshRepositories();
    }
}

void LibraryFileOperations::deleteSelected()
{
    QTreeWidget *targetTree = nullptr;
    QTreeWidgetItem* currentItem = m_mainWindow->getCurrentLibraryItem();

    if (currentItem) {
        targetTree = currentItem->treeWidget();
    } else {
        if (!m_mainWindow->ui->treeSurfaces->selectedItems().isEmpty()) targetTree = m_mainWindow->ui->treeSurfaces;
        else if (!m_mainWindow->ui->treeTextures->selectedItems().isEmpty()) targetTree = m_mainWindow->ui->treeTextures;
        else if (!m_mainWindow->ui->treeMotions->selectedItems().isEmpty()) targetTree = m_mainWindow->ui->treeMotions;
        else if (!m_mainWindow->ui->treeSounds->selectedItems().isEmpty()) targetTree = m_mainWindow->ui->treeSounds;
    }

    if (!targetTree) return;

    QList<QTreeWidgetItem*> items = targetTree->selectedItems();
    if (items.isEmpty()) return;

    struct ItemToDelete { int index; LibraryType type; };
    QList<ItemToDelete> filesToDelete;
    QList<QString> physicalFoldersToDelete;
    QList<QString> reposToUnmount;

    QSettings settings;

    for (QTreeWidgetItem *item : items) {
        if (item->data(0, Qt::UserRole).isValid()) filesToDelete.append({item->data(0, Qt::UserRole).toInt(), LibraryType::Surface});
        else if (item->data(0, Qt::UserRole + 1).isValid()) filesToDelete.append({item->data(0, Qt::UserRole + 1).toInt(), LibraryType::Texture});
        else if (item->data(0, Qt::UserRole + 2).isValid()) filesToDelete.append({item->data(0, Qt::UserRole + 2).toInt(), LibraryType::Motion});
        else if (item->data(0, Qt::UserRole + 3).isValid()) filesToDelete.append({item->data(0, Qt::UserRole + 3).toInt(), LibraryType::Sound});
        else if (item->data(0, Qt::UserRole + 10).isValid()) {
            physicalFoldersToDelete.append(QDir::cleanPath(item->data(0, Qt::UserRole + 10).toString()));
        }
    }

    bool needRefresh = false;

    if (!reposToUnmount.isEmpty()) {
        QString msg = QString("Do you want to remove %1 folder(s) from the library?\nFiles will NOT be deleted from the disk.").arg(reposToUnmount.count());
        if (QMessageBox::question(m_mainWindow, "Remove Repository", msg, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            auto removePathFromSettings = [&](const QString &key) {
                QStringList list = settings.value(key).toStringList();
                bool changed = false;
                for (const QString &rem : reposToUnmount) {
                    if (list.removeOne(rem) || list.removeOne(QDir::cleanPath(rem))) changed = true;
                }
                if (changed) settings.setValue(key, list);
            };
            removePathFromSettings("repoPathsSurfaces"); removePathFromSettings("repoPathsTextures");
            removePathFromSettings("repoPathsMotions"); removePathFromSettings("repoPathsSounds");
            removePathFromSettings("repositoryPaths");
            needRefresh = true;
        }
    }

    if (!physicalFoldersToDelete.isEmpty()) {
        QString msg = QString("You are about to move %1 folder(s) to the Trash/Recycle Bin.\n\nAre you sure you want to proceed?").arg(physicalFoldersToDelete.count());
        if (QMessageBox::question(m_mainWindow, "Move Folders to Trash", msg, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            for (const QString &path : physicalFoldersToDelete) {
                if (QFile::moveToTrash(path)) needRefresh = true;
            }
        }
    }

    std::sort(filesToDelete.begin(), filesToDelete.end(), [](const ItemToDelete &a, const ItemToDelete &b) { return a.index > b.index; });

    if (!filesToDelete.isEmpty()) m_mainWindow->m_undoStack.clear();

    int deletedCount = 0;
    for (const auto &it : filesToDelete) {
        DeletionBackup result = m_mainWindow->m_libraryManager.softDelete(it.index, it.type);
        if (result.isValid) {
            m_mainWindow->m_undoStack.append(result);
            deletedCount++;
        }
    }

    if (deletedCount > 0 || needRefresh) {
        m_mainWindow->refreshRepositories();
        m_mainWindow->ui->actionUndoDelete->setEnabled(!m_mainWindow->m_undoStack.isEmpty());
    }
}

void LibraryFileOperations::undoDelete()
{
    if (m_mainWindow->m_undoStack.isEmpty()) return;

    bool restoredAny = false;
    for (const DeletionBackup &backup : m_mainWindow->m_undoStack) {
        if (m_mainWindow->m_libraryManager.restore(backup)) restoredAny = true;
    }

    if (restoredAny) {
        m_mainWindow->m_undoStack.clear();
        m_mainWindow->ui->actionUndoDelete->setEnabled(false);
        m_mainWindow->refreshRepositories();
    }
}

void LibraryFileOperations::copyPath(const QString &src, const QString &dst)
{
    QDir dir(src);
    if (!dir.exists()) return;

    if (dst.startsWith(src)) return;

    QDir dirDst(dst);
    if (!dirDst.exists()) dirDst.mkpath(dst);

    foreach (QString d, dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString dst_path = dst + QDir::separator() + d;
        dirDst.mkpath(dst_path);
        copyPath(src + QDir::separator() + d, dst_path);
    }

    foreach (QString f, dir.entryList(QDir::Files)) {
        QFile::copy(src + QDir::separator() + f, dst + QDir::separator() + f);
        QFile::setPermissions(dst + QDir::separator() + f, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
    }
}

void LibraryFileOperations::backupBeforeOverwrite(const QString &filePath)
{
    // Se il file non esiste, è un nuovo salvataggio, nessun rischio!
    if (!QFile::exists(filePath)) return;

    QSettings settings;
    QString trashDir = settings.value("libraryRootPath").toString() + "/.trash";
    QDir().mkpath(trashDir);

    QString fileName = QFileInfo(filePath).fileName();
    // Aggiungiamo il tag "_backup_" per distinguerlo nel cestino
    QString trashPath = trashDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_backup_" + fileName;

    // COPIAMO il file nel cestino PRIMA che venga sovrascritto
    if (QFile::copy(filePath, trashPath)) {

        DeletionBackup backup;
        backup.backupPath = trashPath;      // <--- CORRETTO QUI (era trashPath)
        backup.originalPath = filePath;
        backup.isValid = true;

        // (Opzionale) Se la tua struct richiede di specificare un tipo per evitare crash, decommenta:
        // backup.data.type = LibraryType::Surface;

        // Inseriamo l'operazione nello stack
        m_mainWindow->m_undoStack.append(backup);

        // Adattiamo dinamicamente il testo del menu!
        m_mainWindow->ui->actionUndoDelete->setText("Undo Overwrite");
        m_mainWindow->ui->actionUndoDelete->setEnabled(true);
    }
}
