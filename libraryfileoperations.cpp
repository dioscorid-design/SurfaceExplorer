#include "libraryfileoperations.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QTreeWidget>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QFileInfo>
#include <QCheckBox>
#include <QPushButton>
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
        const QString filePath = m_mainWindow->m_libraryManager.itemFilePath(item);
        if (!filePath.isEmpty()) {
            // Le texture hanno appunti propri (l'Incolla le porta nel loro ramo).
            (item->data(0, LibraryManager::itemRole(LibraryType::Texture)).isValid() ? m_mainWindow->m_cutTexturePaths
                                                       : m_mainWindow->m_cutFilePaths).append(filePath);
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
        const QString filePath = m_mainWindow->m_libraryManager.itemFilePath(item);
        if (!filePath.isEmpty()) {
            (item->data(0, LibraryManager::itemRole(LibraryType::Texture)).isValid() ? m_mainWindow->m_cutTexturePaths
                                                       : m_mainWindow->m_cutFilePaths).append(filePath);
        }
        else if (item->data(0, Qt::UserRole + 10).isValid()) {
            m_mainWindow->m_cutFilePaths.append(item->data(0, Qt::UserRole + 10).toString());
        }
    }

    m_mainWindow->m_cutFilePaths.removeDuplicates();
    m_mainWindow->m_cutTexturePaths.removeDuplicates();
}

LibraryFileOperations::CollisionChoice
LibraryFileOperations::resolveMoveCollision(const QString &fileName, QString &destPath,
                                            bool multiple, int &applyToAllChoice)
{
    // Riusa una scelta gia' presa per questo batch ("Applica a tutti").
    if (applyToAllChoice == static_cast<int>(CollisionChoice::Overwrite)) {
        return CollisionChoice::Overwrite;
    }
    if (applyToAllChoice == static_cast<int>(CollisionChoice::KeepBoth)) {
        // Trova un nome _copyN libero nella cartella di destinazione.
        QFileInfo fi(destPath);
        QString dir = fi.absolutePath();
        QString base = fi.completeBaseName();
        QString suffix = fi.suffix();
        if (!suffix.isEmpty()) suffix = "." + suffix;
        int copyIndex = 1;
        QString candidate;
        do {
            candidate = dir + "/" + base + QString("_copy%1").arg(copyIndex) + suffix;
            copyIndex++;
        } while (QFileInfo::exists(candidate));
        destPath = candidate;
        return CollisionChoice::KeepBoth;
    }

    QMessageBox box(m_mainWindow);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("File already exists"));
    box.setText(tr("A file named \"%1\" already exists in the destination folder.").arg(fileName));
    box.setInformativeText(tr("What do you want to do?"));

    QPushButton *overwriteBtn = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
    QPushButton *keepBothBtn  = box.addButton(tr("Keep both"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(keepBothBtn);

    QCheckBox *applyAll = nullptr;
    if (multiple) {
        applyAll = new QCheckBox(tr("Apply to all"), &box);
        box.setCheckBox(applyAll);
    }

    box.exec();

    CollisionChoice choice = CollisionChoice::Cancel;
    if (box.clickedButton() == overwriteBtn) {
        choice = CollisionChoice::Overwrite;
    } else if (box.clickedButton() == keepBothBtn) {
        choice = CollisionChoice::KeepBoth;
        QFileInfo fi(destPath);
        QString dir = fi.absolutePath();
        QString base = fi.completeBaseName();
        QString suffix = fi.suffix();
        if (!suffix.isEmpty()) suffix = "." + suffix;
        int copyIndex = 1;
        QString candidate;
        do {
            candidate = dir + "/" + base + QString("_copy%1").arg(copyIndex) + suffix;
            copyIndex++;
        } while (QFileInfo::exists(candidate));
        destPath = candidate;
    }

    // "Annulla" non viene mai applicato a tutti: ogni file resta annullabile
    // singolarmente, ma se scelto Overwrite/KeepBoth con la spunta lo ricordiamo.
    if (applyAll && applyAll->isChecked() && choice != CollisionChoice::Cancel) {
        applyToAllChoice = static_cast<int>(choice);
    }

    return choice;
}

void LibraryFileOperations::performPasteExample(const QString &destDirOverride)
{
    static bool isPasting = false;
    if (isPasting) return;
    isPasting = true;

    if (m_mainWindow->m_cutFilePaths.isEmpty()) {
        isPasting = false;
        return;
    }

    // Destinazione esplicita (ramo principale): il menu ci passa gia' la root della
    // categoria, quindi saltiamo del tutto la deduzione da item selezionato/euristica.
    QString destDir = destDirOverride;

    if (destDir.isEmpty()) {
        QTreeWidgetItem *currentItem = m_mainWindow->getCurrentLibraryItem();

        if (currentItem && currentItem->data(0, Qt::UserRole + 10).isValid()) {
            destDir = currentItem->data(0, Qt::UserRole + 10).toString();
        }
        // Accanto alla voce selezionata (i suoni mancavano, e si finiva nella
        // radice). Una texture no: le texture hanno il loro Incolla, e un
        // preset incollato su una texture va nel suo ramo, qui sotto.
        else if (currentItem && !currentItem->data(0, LibraryManager::itemRole(LibraryType::Texture)).isValid()) {
            const QString filePath = m_mainWindow->m_libraryManager.itemFilePath(currentItem);
            if (!filePath.isEmpty()) destDir = QFileInfo(filePath).absolutePath();
        }
    }

    if (destDir.isEmpty()) {
        // Nessuna voce: la radice del ramo da cui viene il primo file, per
        // POSIZIONE (LibraryFolders::branchOf). Prima lo si indovinava cercando
        // "record", "motion", "sound" nel percorso: una Shape con una di quelle
        // parole nel nome o nelle cartelle finiva in un altro ramo.
        const LibraryFolders::Branch branch = LibraryFolders::branchOf(m_mainWindow->m_cutFilePaths.first());
        destDir = LibraryFolders::branchDir(branch == LibraryFolders::Branch::None
                                            ? LibraryFolders::Branch::Surfaces : branch);
    }

    // Su iOS/Android la sottocartella di destinazione puo' non esistere ancora nel
    // container (prima scrittura): senza questo mkpath sia QFile::rename che
    // QFile::copy falliscono in silenzio -> opCount 0 -> nessun refresh -> il paste
    // "non fa nulla". Stessa garanzia gia' applicata in saveMotion/saveSurface.
    if (!destDir.isEmpty() && !QDir(destDir).exists())
        QDir().mkpath(destDir);

    int opCount = 0;
    int applyToAllChoice = -1;
    const bool multiple = m_mainWindow->m_cutFilePaths.size() > 1;
    QStringList pastedPaths;
    for (const QString &sourcePath : m_mainWindow->m_cutFilePaths) {
        QFileInfo fi(sourcePath);
        // Prima dello spostamento: dopo la sorgente non esiste piu'.
        const bool sourceIsDir = fi.isDir();
        QString newFilePath = destDir + "/" + fi.fileName();

        if (!m_mainWindow->m_isCopyOperation && QFileInfo(sourcePath).canonicalFilePath() == QFileInfo(newFilePath).canonicalFilePath()) {
            continue;
        }

        // Una cartella non si incolla dentro se stessa ne' in una sua
        // sottocartella (la copia non finirebbe mai). Si guarda la cartella di
        // DESTINAZIONE: incollarla accanto a se stessa e' una copia legittima
        // ("Paths" -> "Paths_copy1"), che il vecchio controllo sul percorso
        // finale scartava -- come scartava "Paths" incollata in "Paths2".
        if (fi.isDir() && isSameOrInside(destDir, sourcePath)) continue;

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

        // Move di un file su un omonimo: chiedi all'utente (prima era azione nulla,
        // perche' QFile::rename/copy falliscono in silenzio sulla destinazione esistente).
        if (!m_mainWindow->m_isCopyOperation && !fi.isDir() && QFileInfo::exists(newFilePath)) {
            CollisionChoice choice = resolveMoveCollision(fi.fileName(), newFilePath, multiple, applyToAllChoice);
            if (choice == CollisionChoice::Cancel) continue;
            if (choice == CollisionChoice::Overwrite) {
                backupBeforeOverwrite(newFilePath); // salvabile via undo
                QFile::remove(newFilePath);
            }
            // KeepBoth: newFilePath e' gia' stato riscritto col suffisso _copyN.
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
        if (ok) {
            opCount++;
            pastedPaths << newFilePath;
            // Spostamento: i percorsi ricordati seguono la voce.
            if (!m_mainWindow->m_isCopyOperation)
                m_mainWindow->remapLibraryPaths(sourcePath, newFilePath, sourceIsDir);
        }
    }

    if (opCount > 0) {
        if (!m_mainWindow->m_isCopyOperation) m_mainWindow->m_cutFilePaths.clear();
        m_mainWindow->refreshAndFocusPaths(pastedPaths);
    } else {
        // opCount 0 con lista non vuota = copy/rename falliti (permessi, cartella
        // sparita) oppure destinazione = origine. Prima era silenzio totale ("il
        // paste non fa niente"): su mobile diamo un riscontro esplicito.
        notifyPasteNoOp(destDir);
    }
    isPasting = false;
}

void LibraryFileOperations::performPasteTexture(const QString &destDirOverride)
{
    if (m_mainWindow->m_cutTexturePaths.isEmpty()) return;

    // Destinazione esplicita dal ramo principale (vedi performPasteExample).
    QString destDir = destDirOverride;

    if (destDir.isEmpty()) {
        QTreeWidgetItem *currentItem = m_mainWindow->getCurrentLibraryItem();

        if (currentItem && currentItem->data(0, Qt::UserRole + 10).isValid()) {
            destDir = currentItem->data(0, Qt::UserRole + 10).toString();
        }
        else if (currentItem && currentItem->data(0, LibraryManager::itemRole(LibraryType::Texture)).isValid()) {
            destDir = QFileInfo(m_mainWindow->m_libraryManager.getTexture(currentItem->data(0, LibraryManager::itemRole(LibraryType::Texture)).toInt()).filePath).absolutePath();
        }
        else {
            destDir = LibraryFolders::branchDir(LibraryFolders::Branch::Textures);
        }
    }

    if (destDir.isEmpty()) return;

    // Vedi performPasteExample: garantiamo l'esistenza della cartella di
    // destinazione, altrimenti su mobile copy/rename falliscono in silenzio.
    if (!QDir(destDir).exists())
        QDir().mkpath(destDir);

    int opCount = 0;
    int applyToAllChoice = -1;
    const bool multiple = m_mainWindow->m_cutTexturePaths.size() > 1;
    QStringList pastedPaths;
    for (const QString &sourcePath : m_mainWindow->m_cutTexturePaths) {
        QFileInfo fi(sourcePath);
        // Prima dello spostamento: dopo la sorgente non esiste piu'.
        const bool sourceIsDir = fi.isDir();
        QString newFilePath = destDir + "/" + fi.fileName();

        if (!m_mainWindow->m_isCopyOperation && QFileInfo(sourcePath).canonicalFilePath() == QFileInfo(newFilePath).canonicalFilePath()) {
            continue;
        }

        if (QFileInfo(newFilePath).exists()) {
            if (m_mainWindow->m_isCopyOperation) {
                // Copia: come prima, rinomina automaticamente _copyN.
                QString baseName = fi.completeBaseName();
                QString suffix = fi.suffix();
                if(!suffix.isEmpty()) suffix = "." + suffix;
                int copyIndex = 1;
                while (QFileInfo(newFilePath).exists()) {
                    newFilePath = destDir + "/" + baseName + QString("_copy%1").arg(copyIndex) + suffix;
                    copyIndex++;
                }
            } else {
                // Move su omonimo: chiedi all'utente.
                CollisionChoice choice = resolveMoveCollision(fi.fileName(), newFilePath, multiple, applyToAllChoice);
                if (choice == CollisionChoice::Cancel) continue;
                if (choice == CollisionChoice::Overwrite) {
                    backupBeforeOverwrite(newFilePath);
                    QFile::remove(newFilePath);
                }
                // KeepBoth: newFilePath gia' riscritto col suffisso _copyN.
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
        if (ok) {
            opCount++;
            pastedPaths << newFilePath;
            // Spostamento: i percorsi ricordati seguono la voce.
            if (!m_mainWindow->m_isCopyOperation)
                m_mainWindow->remapLibraryPaths(sourcePath, newFilePath, sourceIsDir);
        }
    }

    if (opCount > 0) {
        if (!m_mainWindow->m_isCopyOperation) m_mainWindow->m_cutTexturePaths.clear();
        m_mainWindow->refreshAndFocusPaths(pastedPaths);
    } else {
        notifyPasteNoOp(destDir);
    }
}

void LibraryFileOperations::deleteSelected()
{
    QTreeWidget *targetTree = nullptr;
    QTreeWidgetItem* currentItem = m_mainWindow->getCurrentLibraryItem();

    if (currentItem) {
        targetTree = currentItem->treeWidget();
    } else {
        // Nessuna selezione nella linguetta aperta: il primo albero che ne ha una.
        for (const MainWindow::LibraryBranch &b : m_mainWindow->libraryBranches()) {
            if (!b.tree->selectedItems().isEmpty()) { targetTree = b.tree; break; }
        }
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
        if (const std::optional<LibraryType> type = LibraryManager::itemType(item))
            filesToDelete.append({item->data(0, LibraryManager::itemRole(*type)).toInt(), *type});
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
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
                // --- MOBILE LOGIC: Sposta la cartella nel cestino interno ---
                QSettings settings;
                QString trashDir = settings.value("libraryRootPath").toString() + "/.trash";
                QDir().mkpath(trashDir);

                QString folderName = QFileInfo(path).fileName();
                QString internalTrashPath = trashDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_delFolder_" + folderName;

                // QDir().rename funziona perfettamente per spostare/rinominare intere cartelle
                if (QDir().rename(path, internalTrashPath)) {
                    needRefresh = true;
                }
#else
                // --- DESKTOP LOGIC: Cestino di sistema originale ---
                if (QFile::moveToTrash(path)) {
                    needRefresh = true;
                }
#endif
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

bool LibraryFileOperations::isSameOrInside(const QString &path, const QString &dir)
{
    const QString p = QDir::cleanPath(path);
    const QString d = QDir::cleanPath(dir);
    return p == d || p.startsWith(d + QLatin1Char('/'));
}

void LibraryFileOperations::copyPath(const QString &src, const QString &dst)
{
    QDir dir(src);
    if (!dir.exists()) return;

    if (isSameOrInside(dst, src)) return;   // dentro se stessa: non finirebbe mai

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

void LibraryFileOperations::notifyPasteNoOp(const QString &destDir)
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    QMessageBox::information(
        m_mainWindow, "Paste",
        QString("Nothing was pasted.\n\nThe items may already be in this folder, "
                "or the destination could not be written:\n%1")
            .arg(destDir));
#else
    Q_UNUSED(destDir);
#endif
}
