#include "librarymenucontroller.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "presetserializer.h"
#include "libraryfileoperations.h"

#include <QMenu>
#include <QFileDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>

LibraryMenuController::LibraryMenuController(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

void LibraryMenuController::showMenu(QTreeWidget *senderTree, const QPoint &pos)
{
    if (!senderTree) return;

    QTreeWidgetItem *itemUnderMouse = senderTree->itemAt(pos);

    if (itemUnderMouse) {
        if (!itemUnderMouse->isSelected()) {
            senderTree->clearSelection();
            itemUnderMouse->setSelected(true);
            senderTree->setCurrentItem(itemUnderMouse);
        }
    } else {
        senderTree->clearSelection();
    }

    QList<QTreeWidgetItem*> selectedItems = senderTree->selectedItems();
    int count = selectedItems.count();
    QTreeWidgetItem* refItem = itemUnderMouse ? itemUnderMouse : (selectedItems.isEmpty() ? nullptr : selectedItems.first());

    QMenu *contextMenu = new QMenu(m_mainWindow);
    contextMenu->setStyleSheet(
        "QMenu { background-color: #2b2b2b; color: #ffffff; border: 1px solid #3a3a3a; }"
        "QMenu::item { padding: 5px 20px; }"
        "QMenu::item:selected { background-color: #3a3a3a; }"
        );

    if (count > 0 && refItem) {
        if (count > 1) {
            QAction *actCopyAll = contextMenu->addAction(QString("Copy %1 Items").arg(count));
            connect(actCopyAll, &QAction::triggered, m_mainWindow, [this, refItem](){ m_mainWindow->performCopy(refItem); });

            QAction *actCutAll = contextMenu->addAction(QString("Cut %1 Items").arg(count));
            connect(actCutAll, &QAction::triggered, m_mainWindow, [this, refItem](){ m_mainWindow->performCut(refItem); });

            QAction *actDeleteAll = contextMenu->addAction(QString("Delete %1 Items").arg(count));
            connect(actDeleteAll, &QAction::triggered, m_mainWindow, &MainWindow::deleteSelectedExample);
        }
        else {
            bool isSurface = refItem->data(0, Qt::UserRole).isValid();
            bool isTexture = refItem->data(0, Qt::UserRole + 1).isValid();
            bool isMotion  = refItem->data(0, Qt::UserRole + 2).isValid();
            bool isSound   = refItem->data(0, Qt::UserRole + 3).isValid();
            bool isFolder  = refItem->data(0, Qt::UserRole + 10).isValid();

            if (isSurface) {
                int index = refItem->data(0, Qt::UserRole).toInt();
                QString path = m_mainWindow->m_libraryManager.getSurface(index).filePath;

                contextMenu->addAction("Save Surface As...", m_mainWindow, [this, path](){
                    m_mainWindow->m_presetSerializer->saveSurfaceAs(QFileInfo(path).absolutePath(), path);
                });
                contextMenu->addAction("Copy Surface", m_mainWindow, [this, refItem](){ m_mainWindow->m_fileOps->performCopy(refItem); });
                contextMenu->addAction("Cut Surface", m_mainWindow, [this, refItem](){ m_mainWindow->m_fileOps->performCut(refItem); });
                contextMenu->addAction("Delete Surface", m_mainWindow, [this](){ m_mainWindow->m_fileOps->deleteSelected(); });
            }
            else if (isTexture) {
                int index = refItem->data(0, Qt::UserRole + 1).toInt();
                const LibraryItem &data = m_mainWindow->m_libraryManager.getTexture(index);

                contextMenu->addAction("Save Texture As...", m_mainWindow, [this, data](){
                    m_mainWindow->m_presetSerializer->saveTextureAs(QFileInfo(data.filePath).absolutePath(), data.filePath);
                });

                contextMenu->addAction("Copy Texture", m_mainWindow, [this, refItem](){ m_mainWindow->performCopy(refItem); });
                contextMenu->addAction("Cut Texture", m_mainWindow, [this, refItem](){ m_mainWindow->performCut(refItem); });
                contextMenu->addAction("Delete Texture", m_mainWindow, &MainWindow::deleteSelectedExample);
            }
            else if (isMotion) {
                int index = refItem->data(0, Qt::UserRole + 2).toInt();
                QString path = m_mainWindow->m_libraryManager.getMotion(index).filePath;

                contextMenu->addAction("Save Record As...", m_mainWindow, [this, path](){
                    m_mainWindow->m_presetSerializer->saveMotionAs(QFileInfo(path).absolutePath(), path);
                });
                contextMenu->addAction("Copy Record", m_mainWindow, [this, refItem](){ m_mainWindow->m_fileOps->performCopy(refItem); });
                contextMenu->addAction("Cut Record", m_mainWindow, [this, refItem](){ m_mainWindow->m_fileOps->performCut(refItem); });
                contextMenu->addAction("Delete Record", m_mainWindow, [this](){ m_mainWindow->m_fileOps->deleteSelected(); });
            }
            else if (isSound) {
                int index = refItem->data(0, Qt::UserRole + 3).toInt();
                const LibraryItem &data = m_mainWindow->m_libraryManager.getSound(index);

                contextMenu->addAction("Add to Current Script", m_mainWindow, [this, data](){
                    QString currentText = m_mainWindow->ui->txtScriptEditor->toPlainText();
                    currentText.remove(QRegularExpression("^\\s*//(SYNTH|MUSIC):.*\\n?", QRegularExpression::MultilineOption));

                    QString newText;
                    if (data.filePath.endsWith(".mp3", Qt::CaseInsensitive) || data.filePath.endsWith(".wav", Qt::CaseInsensitive)) {
                        newText = "//MUSIC: " + data.filePath + "\n\n" + currentText.trimmed();
                    } else {
                        newText = data.scriptCode.trimmed() + "\n\n" + currentText.trimmed();
                    }
                    m_mainWindow->ui->txtScriptEditor->setPlainText(newText.trimmed());
                    m_mainWindow->onApplyTextureScriptClicked();
                });
                contextMenu->addSeparator();

                if (data.filePath.endsWith(".json", Qt::CaseInsensitive)) {
                    contextMenu->addAction("Save Sound As...", m_mainWindow, [this, data](){
                        m_mainWindow->m_presetSerializer->saveSound(QFileInfo(data.filePath).absolutePath(), data.filePath);
                    });
                }

                contextMenu->addAction("Copy Sound", m_mainWindow, [this, refItem](){ m_mainWindow->performCopy(refItem); });
                contextMenu->addAction("Cut Sound", m_mainWindow, [this, refItem](){ m_mainWindow->performCut(refItem); });
                contextMenu->addAction("Delete Sound", m_mainWindow, &MainWindow::deleteSelectedExample);
            }
            else if (isFolder) {
                QString folderPath = refItem->data(0, Qt::UserRole + 10).toString();

                if (senderTree == m_mainWindow->ui->treeSurfaces) {
                    contextMenu->addAction("Save Surface Here...", m_mainWindow, [this](){ m_mainWindow->saveSurfaceToFile(); });
                } else if (senderTree == m_mainWindow->ui->treeTextures) {
                    contextMenu->addAction("Save Texture Here...", m_mainWindow, [this, folderPath](){
                        m_mainWindow->m_presetSerializer->saveTextureAs(folderPath);
                    });
                } else if (senderTree == m_mainWindow->ui->treeMotions) {
                    contextMenu->addAction("Save Record Here...", m_mainWindow, &MainWindow::onSaveMotionClicked);
                } else if (senderTree == m_mainWindow->ui->treeSounds) {
                    contextMenu->addAction("Save Sound Here...", m_mainWindow, [this, folderPath](){
                        m_mainWindow->m_presetSerializer->saveSound(folderPath);
                    });
                }
                contextMenu->addSeparator();

                contextMenu->addAction("Open in File Explorer", m_mainWindow, [folderPath](){
                    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
                });
                contextMenu->addSeparator();

                if (!m_mainWindow->m_cutFilePaths.isEmpty() || !m_mainWindow->m_cutTexturePaths.isEmpty()) {
                    contextMenu->addAction("Paste Here", m_mainWindow, [this](){
                        if (!m_mainWindow->m_cutFilePaths.isEmpty()) m_mainWindow->onPasteExample();
                        else m_mainWindow->onPasteTexture();
                    });
                    contextMenu->addSeparator();
                }
                contextMenu->addAction("Copy Folder", m_mainWindow, [this, refItem](){ m_mainWindow->performCopy(refItem); });
                contextMenu->addAction("Cut Folder", m_mainWindow, [this, refItem](){ m_mainWindow->performCut(refItem); });
                contextMenu->addSeparator();
                contextMenu->addAction("Delete Folder (Destroys Files!)", m_mainWindow, &MainWindow::deleteSelectedExample);
            }
        }
        contextMenu->addSeparator();
    }

    if (!refItem) {
        if (senderTree == m_mainWindow->ui->treeSurfaces) {
            contextMenu->addAction("Save New Surface...", m_mainWindow, [this](){ m_mainWindow->saveSurfaceToFile(); });
        } else if (senderTree == m_mainWindow->ui->treeTextures) {
            contextMenu->addAction("Save New Texture...", m_mainWindow, [this](){
                QString rootPath = QSettings().value("libraryRootPath").toString();
                m_mainWindow->m_presetSerializer->saveTextureAs(QSettings().value("pathTextures", rootPath + "/Textures").toString());
            });
        } else if (senderTree == m_mainWindow->ui->treeMotions) {
            contextMenu->addAction("Save New Record"
                                   "...", m_mainWindow, &MainWindow::onSaveMotionClicked);
        } else if (senderTree == m_mainWindow->ui->treeSounds) {
            contextMenu->addAction("Save New Sound...", m_mainWindow, [this](){
                QString rootPath = QSettings().value("libraryRootPath").toString();
                m_mainWindow->m_presetSerializer->saveSound(QSettings().value("pathSounds", rootPath + "/Sounds").toString());
            });
        }
        contextMenu->addSeparator();
    }

    bool clickedOnFolder = (refItem && refItem->data(0, Qt::UserRole + 10).isValid());
    if (!clickedOnFolder && (!m_mainWindow->m_cutFilePaths.isEmpty() || !m_mainWindow->m_cutTexturePaths.isEmpty())) {
        if (!m_mainWindow->m_cutFilePaths.isEmpty()) {
            contextMenu->addAction(QString("Paste %1 Item(s)").arg(m_mainWindow->m_cutFilePaths.count()), m_mainWindow, &MainWindow::onPasteExample);
        } else if (!m_mainWindow->m_cutTexturePaths.isEmpty()) {
            contextMenu->addAction(QString("Paste %1 Texture(s)").arg(m_mainWindow->m_cutTexturePaths.count()), m_mainWindow, &MainWindow::onPasteTexture);
        }
        contextMenu->addSeparator();
    }

    if (!m_mainWindow->m_undoStack.isEmpty()) {
        contextMenu->addAction("Undo Delete", m_mainWindow, &MainWindow::onUndoDelete);
        contextMenu->addSeparator();
    }

    contextMenu->addAction("Refresh Library", m_mainWindow, [this](){ m_mainWindow->refreshRepositories(); });
    contextMenu->addSeparator();
    contextMenu->addAction("Create New Folder...", m_mainWindow, [this](){ m_mainWindow->onCreateFolderClicked(); });
    contextMenu->addSeparator();

    contextMenu->addAction("Open Workspace Folder...", m_mainWindow, [this, senderTree](){
        QSettings settings;
        QString rootPath = settings.value("libraryRootPath").toString();
        QString key;
        QString currentPath;
        if (senderTree == m_mainWindow->ui->treeTextures) { key = "pathTextures"; currentPath = settings.value(key, rootPath + "/Textures").toString(); }
        else if (senderTree == m_mainWindow->ui->treeMotions) { key = "pathMotions"; currentPath = settings.value(key, rootPath + "/Motions").toString(); }
        else if (senderTree == m_mainWindow->ui->treeSounds) { key = "pathSounds"; currentPath = settings.value(key, rootPath + "/Sounds").toString(); }
        else { key = "pathSurfaces"; currentPath = settings.value(key, rootPath + "/Surfaces").toString(); }

        QString dir = QFileDialog::getExistingDirectory(m_mainWindow, "Select Workspace Folder", currentPath);
        if (dir.isEmpty()) return;

        settings.setValue(key, QDir::cleanPath(dir));
        settings.remove("repoPathsSurfaces"); settings.remove("repoPathsTextures");
        settings.remove("repoPathsMotions"); settings.remove("repoPathsSounds");
        settings.remove("repositoryPaths");
        m_mainWindow->refreshRepositories();
    });

    bool wasTimeAnimating = false;
    if (m_mainWindow->m_btnStart && m_mainWindow->m_btnStart->text().toUpper() == "STOP") {
        wasTimeAnimating = true;
        m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
        m_mainWindow->ui->glWidget->stopAnimationTimer();
    }

    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();
    bool wasRotating = m_mainWindow->ui->glWidget->isAnimating();

    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();
    if (wasRotating) m_mainWindow->ui->glWidget->pauseMotion();

    contextMenu->exec(senderTree->mapToGlobal(pos));
    delete contextMenu;

    if (wasTimeAnimating) {
        m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
        m_mainWindow->ui->glWidget->startAnimationTimer();
    }
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();
    if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
}
