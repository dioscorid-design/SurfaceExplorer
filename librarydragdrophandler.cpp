#include "librarydragdrophandler.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "libraryfileoperations.h"
#include "librarymenucontroller.h"

#include <QTreeWidget>
#include <QDropEvent>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QGestureEvent>
#include <QTapAndHoldGesture>

LibraryDragDropHandler::LibraryDragDropHandler(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

bool LibraryDragDropHandler::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Drop) {
        QTreeWidget *tree = qobject_cast<QTreeWidget*>(obj->parent());
        if (!tree) tree = qobject_cast<QTreeWidget*>(obj);

        if (tree) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            QPoint dropPoint = dropEvent->position().toPoint();
#else
            QPoint dropPoint = dropEvent->pos();
#endif
            QTreeWidgetItem *targetItem = tree->itemAt(dropPoint);
            QString destDir;
            // La radice del ramo (LibraryFolders::branchDir: cartelle minuscole,
            // come le crea la libreria; "/Textures"/"/Sounds"/"/Surfaces" non
            // esistono su iOS case-sensitive). Un albero estraneo: Shapes, come prima.
            const LibraryFolders::Branch branch = m_mainWindow->branchOfTree(tree);
            const QString treeRootPath = LibraryFolders::branchDir(
                branch == LibraryFolders::Branch::None ? LibraryFolders::Branch::Surfaces : branch);

            auto getTargetDirectory = [&](QTreeWidgetItem *item) -> QString {
                if (!item) return "";
                if (item->data(0, Qt::UserRole + 10).isValid()) return item->data(0, Qt::UserRole + 10).toString();

                const QString filePath = m_mainWindow->m_libraryManager.itemFilePath(item);
                if (!filePath.isEmpty()) return QFileInfo(filePath).absolutePath();
                return "";
            };

            if (targetItem) {
                QRect rect = tree->visualItemRect(targetItem);
                int margin = rect.height() / 4;
                bool droppedOnEdge = (dropPoint.y() < rect.top() + margin || dropPoint.y() > rect.bottom() - margin);

                if (droppedOnEdge) {
                    QTreeWidgetItem *parentItem = targetItem->parent();
                    destDir = parentItem ? getTargetDirectory(parentItem) : treeRootPath;
                } else {
                    destDir = getTargetDirectory(targetItem);
                }
            } else {
                destDir = treeRootPath;
            }

            bool anyMoved = false;
            QList<QTreeWidgetItem*> draggedItems = tree->selectedItems();
            int applyToAllChoice = -1;
            const bool multipleDragged = draggedItems.size() > 1;

            QStringList movedPaths;
            for (QTreeWidgetItem* item : draggedItems) {
                QString sourcePath;
                bool isDir = false;

                if (item->data(0, Qt::UserRole + 10).isValid()) {
                    sourcePath = item->data(0, Qt::UserRole + 10).toString();
                    isDir = true;
                } else {
                    sourcePath = m_mainWindow->m_libraryManager.itemFilePath(item);
                }

                if (!sourcePath.isEmpty() && !destDir.isEmpty()) {
                    QFileInfo srcInfo(sourcePath);
                    QString newPath = destDir + "/" + srcInfo.fileName();

                    if (sourcePath != newPath
                        && !LibraryFileOperations::isSameOrInside(destDir, sourcePath)) {
                        if (isDir) {
                            // Le cartelle omonime restano saltate (no merge): comportamento invariato.
                            if (!QFile::exists(newPath)) {
                                if (QDir().rename(sourcePath, newPath)) {
                                    anyMoved = true;
                                } else {
                                    m_mainWindow->m_fileOps->copyPath(sourcePath, newPath);
                                    QDir(sourcePath).removeRecursively();
                                    anyMoved = true;
                                }
                                m_mainWindow->remapLibraryPaths(sourcePath, newPath, true);
                                movedPaths << newPath;
                            }
                        } else {
                            // File omonimo a destinazione: chiedi (prima era azione nulla).
                            bool proceed = true;
                            if (QFile::exists(newPath)) {
                                LibraryFileOperations::CollisionChoice choice =
                                    m_mainWindow->m_fileOps->resolveMoveCollision(
                                        srcInfo.fileName(), newPath, multipleDragged, applyToAllChoice);
                                if (choice == LibraryFileOperations::CollisionChoice::Cancel) {
                                    proceed = false;
                                } else if (choice == LibraryFileOperations::CollisionChoice::Overwrite) {
                                    m_mainWindow->m_fileOps->backupBeforeOverwrite(newPath);
                                    QFile::remove(newPath);
                                }
                                // KeepBoth: newPath gia' riscritto col suffisso _copyN.
                            }
                            if (proceed) {
                                bool moved = false;
                                if (QFile::rename(sourcePath, newPath)) {
                                    moved = true;
                                } else if (QFile::copy(sourcePath, newPath)) {
                                    QFile::setPermissions(newPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                                    QFile::remove(sourcePath);
                                    moved = true;
                                }
                                if (moved) {
                                    anyMoved = true;
                                    m_mainWindow->remapLibraryPaths(sourcePath, newPath, false);
                                    movedPaths << newPath;
                                }
                            }
                        }
                    }
                }
            }

            dropEvent->setDropAction(Qt::IgnoreAction);
            dropEvent->accept();
            if (anyMoved) m_mainWindow->refreshAndFocusPaths(movedPaths);

            return true;
        }
    }

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (event->type() == QEvent::Gesture) {
        QGestureEvent *gEvent = static_cast<QGestureEvent*>(event);
        if (QGesture *gesture = gEvent->gesture(Qt::TapAndHoldGesture)) {
            QTapAndHoldGesture *tap = static_cast<QTapAndHoldGesture*>(gesture);
            if (tap->state() == Qt::GestureFinished) {
                QTreeWidget *tree = qobject_cast<QTreeWidget*>(obj);
                if (tree) {
                    QPoint pos = tap->position().toPoint();
                    // ---> FIX: Chiama direttamente il controller del menu! <---
                    m_mainWindow->m_menuController->showMenu(tree, pos);
                    return true;
                }
            }
        }
    }
#endif

    // Ritorna false per dire a Qt di far continuare il normale flusso degli eventi non gestiti
    return false;
}
