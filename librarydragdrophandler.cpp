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
            QSettings settings;
            QString rootPath = settings.value("libraryRootPath").toString();
            QString treeRootPath;

            if (tree == m_mainWindow->ui->treeTextures) treeRootPath = settings.value("pathTextures", rootPath + "/Textures").toString();
            else if (tree == m_mainWindow->ui->treeMotions) treeRootPath = settings.value("pathMotions", rootPath + "/Motions").toString();
            else if (tree == m_mainWindow->ui->treeSounds) treeRootPath = settings.value("pathSounds", rootPath + "/Sounds").toString();
            else treeRootPath = settings.value("pathSurfaces", rootPath + "/Surfaces").toString();

            auto getTargetDirectory = [&](QTreeWidgetItem *item) -> QString {
                if (!item) return "";
                if (item->data(0, Qt::UserRole + 10).isValid()) return item->data(0, Qt::UserRole + 10).toString();

                int typeRole = -1;
                if (item->data(0, Qt::UserRole).isValid()) typeRole = Qt::UserRole;
                else if (item->data(0, Qt::UserRole + 1).isValid()) typeRole = Qt::UserRole + 1;
                else if (item->data(0, Qt::UserRole + 2).isValid()) typeRole = Qt::UserRole + 2;
                else if (item->data(0, Qt::UserRole + 3).isValid()) typeRole = Qt::UserRole + 3;

                if (typeRole != -1) {
                    int index = item->data(0, typeRole).toInt();
                    QString filePath;
                    if (typeRole == Qt::UserRole) filePath = m_mainWindow->m_libraryManager.getSurface(index).filePath;
                    else if (typeRole == Qt::UserRole + 1) filePath = m_mainWindow->m_libraryManager.getTexture(index).filePath;
                    else if (typeRole == Qt::UserRole + 2) filePath = m_mainWindow->m_libraryManager.getMotion(index).filePath;
                    else if (typeRole == Qt::UserRole + 3) filePath = m_mainWindow->m_libraryManager.getSound(index).filePath;
                    return QFileInfo(filePath).absolutePath();
                }
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

            for (QTreeWidgetItem* item : draggedItems) {
                QString sourcePath;
                bool isDir = false;

                if (item->data(0, Qt::UserRole + 10).isValid()) {
                    sourcePath = item->data(0, Qt::UserRole + 10).toString();
                    isDir = true;
                } else {
                    int typeRole = -1;
                    if (item->data(0, Qt::UserRole).isValid()) typeRole = Qt::UserRole;
                    else if (item->data(0, Qt::UserRole + 1).isValid()) typeRole = Qt::UserRole + 1;
                    else if (item->data(0, Qt::UserRole + 2).isValid()) typeRole = Qt::UserRole + 2;
                    else if (item->data(0, Qt::UserRole + 3).isValid()) typeRole = Qt::UserRole + 3;

                    if (typeRole != -1) {
                        int index = item->data(0, typeRole).toInt();
                        if (typeRole == Qt::UserRole) sourcePath = m_mainWindow->m_libraryManager.getSurface(index).filePath;
                        else if (typeRole == Qt::UserRole + 1) sourcePath = m_mainWindow->m_libraryManager.getTexture(index).filePath;
                        else if (typeRole == Qt::UserRole + 2) sourcePath = m_mainWindow->m_libraryManager.getMotion(index).filePath;
                        else if (typeRole == Qt::UserRole + 3) sourcePath = m_mainWindow->m_libraryManager.getSound(index).filePath;
                    }
                }

                if (!sourcePath.isEmpty() && !destDir.isEmpty()) {
                    QFileInfo srcInfo(sourcePath);
                    QString newPath = destDir + "/" + srcInfo.fileName();

                    if (sourcePath != newPath && !QFile::exists(newPath) && !destDir.startsWith(sourcePath)) {
                        if (isDir) {
                            if (QDir().rename(sourcePath, newPath)) {
                                anyMoved = true;
                            } else {
                                m_mainWindow->m_fileOps->copyPath(sourcePath, newPath);
                                QDir(sourcePath).removeRecursively();
                                anyMoved = true;
                            }
                        } else {
                            if (QFile::rename(sourcePath, newPath)) {
                                anyMoved = true;
                            } else if (QFile::copy(sourcePath, newPath)) {
                                QFile::setPermissions(newPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                                QFile::remove(sourcePath);
                                anyMoved = true;
                            }
                        }
                    }
                }
            }

            dropEvent->setDropAction(Qt::IgnoreAction);
            dropEvent->accept();
            if (anyMoved) m_mainWindow->refreshRepositories();

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
