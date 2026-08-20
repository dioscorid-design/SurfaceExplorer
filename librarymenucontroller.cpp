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
#include <QTimer> // Aggiunto per prevenire i crash su iOS/Android

LibraryMenuController::LibraryMenuController(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

void LibraryMenuController::showMenu(QTreeWidget *senderTree, const QPoint &pos)
{
    if (!senderTree) return;

    QPoint localPos = pos;
    QPoint globalMenuPos = senderTree->mapToGlobal(pos);

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    globalMenuPos = QCursor::pos();
    localPos = senderTree->viewport()->mapFromGlobal(globalMenuPos);
#endif

    // Usiamo la coordinata "pulita" (localPos) per capire cosa hai toccato!
    QTreeWidgetItem *itemUnderMouse = senderTree->itemAt(localPos);

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

    QTreeWidget* safeTree = senderTree;
    QTreeWidgetItem* safeRefItem = refItem;

    // STATO DEI MOTI CATTURATO QUI, non piu' in basso.
    //
    // Serve alle due voci che cambiano cartella: sono le uniche a ricostruire gli
    // alberi, e il rebuild rientra in showMenu (vedi
    // MainWindow::refreshLibraryPreservingMotion per il meccanismo completo).
    // Va letto ORA perche' i moti girano ancora: piu' sotto showMenu li ferma, e
    // da quel punto in poi isAnimating()/isActive() direbbero "fermo" anche
    // quando l'utente li aveva accesi -- e' proprio cosi' che il rientro
    // perdeva lo stato.
    // Le altre voci non ne hanno bisogno: non ricostruiscono gli alberi, quindi
    // il ripristino in fondo a showMenu basta.
    const bool motionRotating = m_mainWindow->ui->glWidget->isAnimating();
    const bool motionPath4D   = m_mainWindow->pathTimer->isActive();
    const bool motionPath3D   = m_mainWindow->pathTimer3D->isActive();
    const bool motionTimeAnim = m_mainWindow->m_btnStart
                             && m_mainWindow->m_btnStart->text().toUpper() == "STOP";

    std::function<void()> pendingAction = nullptr;

    auto executeAction = [&pendingAction](std::function<void()> func) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        pendingAction = func;
#else
        func();
#endif
    };

    if (count > 0 && refItem) {
        if (count > 1) {
            QAction *actCopyAll = contextMenu->addAction(QString("Copy %1 Items").arg(count));
            connect(actCopyAll, &QAction::triggered, m_mainWindow, [this, refItem, executeAction](){
                executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
            });

            QAction *actCutAll = contextMenu->addAction(QString("Cut %1 Items").arg(count));
            connect(actCutAll, &QAction::triggered, m_mainWindow, [this, refItem, executeAction](){
                executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
            });

            QAction *actDeleteAll = contextMenu->addAction(QString("Delete %1 Items").arg(count));
            connect(actDeleteAll, &QAction::triggered, m_mainWindow, [this, executeAction](){
                executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
            });
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

                contextMenu->addAction("Save Surface As...", m_mainWindow, [this, path, executeAction](){
                    executeAction([this, path](){ m_mainWindow->m_presetSerializer->saveSurfaceAs(QFileInfo(path).absolutePath(), path); });
                });
                contextMenu->addAction("Copy Surface", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Surface", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCut(refItem); });
                });
                contextMenu->addAction("Delete Surface", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isTexture) {
                int index = refItem->data(0, Qt::UserRole + 1).toInt();
                const LibraryItem &data = m_mainWindow->m_libraryManager.getTexture(index);

                contextMenu->addAction("Save Texture As...", m_mainWindow, [this, data, executeAction](){
                    executeAction([this, data](){ m_mainWindow->m_presetSerializer->saveTextureAs(QFileInfo(data.filePath).absolutePath(), data.filePath); });
                });

                contextMenu->addAction("Copy Texture", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Texture", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
                });
                contextMenu->addAction("Delete Texture", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isMotion) {
                int index = refItem->data(0, Qt::UserRole + 2).toInt();
                QString path = m_mainWindow->m_libraryManager.getMotion(index).filePath;

                contextMenu->addAction("Save Record As...", m_mainWindow, [this, path, executeAction](){
                    executeAction([this, path](){ m_mainWindow->m_presetSerializer->saveMotionAs(QFileInfo(path).absolutePath(), path); });
                });
                contextMenu->addAction("Copy Record", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Record", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCut(refItem); });
                });
                contextMenu->addAction("Delete Record", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isSound) {
                int index = refItem->data(0, Qt::UserRole + 3).toInt();
                const LibraryItem &data = m_mainWindow->m_libraryManager.getSound(index);

                if (data.filePath.endsWith(".json", Qt::CaseInsensitive)) {
                    contextMenu->addAction("Save Sound As...", m_mainWindow, [this, data, executeAction](){
                        // Finestra di dialogo (2 argomenti: cartella base e file sorgente)
                        executeAction([this, data](){ m_mainWindow->m_presetSerializer->saveSoundAs(QFileInfo(data.filePath).absolutePath(), data.filePath); });
                    });
                }

                contextMenu->addAction("Copy Sound", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Sound", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
                });
                contextMenu->addAction("Delete Sound", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isFolder) {
                QString folderPath = refItem->data(0, Qt::UserRole + 10).toString();

                if (senderTree == m_mainWindow->ui->treeSurfaces) {
                    // folderPath passato anche su desktop (come Texture/Sound):
                    // il vecchio ramo senza argomento ripiegava sulla selezione
                    // corrente e, in sua assenza, su lastFolder ("ultima usata").
                    contextMenu->addAction("Save Surface Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->saveSurfaceToFile(folderPath); });
                    });
                } else if (senderTree == m_mainWindow->ui->treeTextures) {
                    contextMenu->addAction("Save Texture Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->m_presetSerializer->saveTextureAs(folderPath); });
                    });
                } else if (senderTree == m_mainWindow->ui->treeMotions) {
                    // folderPath anche su desktop, vedi Save Surface Here sopra.
                    contextMenu->addAction("Save Record Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->m_presetSerializer->saveMotion(folderPath); });
                    });
                } else if (senderTree == m_mainWindow->ui->treeSounds) {
                    contextMenu->addAction("Save Sound Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->m_presetSerializer->saveSoundAs(folderPath, ""); });
                    });
                }
                contextMenu->addSeparator();

                contextMenu->addAction("Open in File Explorer", m_mainWindow, [folderPath, executeAction](){
                    executeAction([folderPath](){ QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath)); });
                });
                contextMenu->addSeparator();

                if (!m_mainWindow->m_cutFilePaths.isEmpty() || !m_mainWindow->m_cutTexturePaths.isEmpty()) {
                    contextMenu->addAction("Paste Here", m_mainWindow, [this, executeAction](){
                        executeAction([this](){
                            if (!m_mainWindow->m_cutFilePaths.isEmpty()) m_mainWindow->onPasteExample();
                            else m_mainWindow->onPasteTexture();
                        });
                    });
                    contextMenu->addSeparator();
                }
                contextMenu->addAction("Copy Folder", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Folder", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
                });
                contextMenu->addSeparator();
                contextMenu->addAction("Delete Folder (Destroys Files!)", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
        }
        contextMenu->addSeparator();
    }

    if (!refItem) {
        // Click nel ramo PRINCIPALE (fuori da ogni cartella): il dialog deve aprirsi
        // nella root della categoria, MAI su una cartella secondaria o sull'ultima
        // usata. Per questo passiamo esplicitamente la root come suggestedPath
        // (come fanno gia' Texture e Sound); senza path, saveSurface/saveMotion
        // ricadono sull'item selezionato / lastDir -> il dialog si apre in un ramo
        // secondario.
        QString rootPath = QSettings().value("libraryRootPath").toString();
        if (senderTree == m_mainWindow->ui->treeSurfaces) {
            QString startDir = QSettings().value("pathSurfaces", rootPath + "/surfaces").toString();
            contextMenu->addAction("Save New Surface...", m_mainWindow, [this, startDir, executeAction](){
                executeAction([this, startDir](){ m_mainWindow->saveSurfaceToFile(startDir); });
            });
        } else if (senderTree == m_mainWindow->ui->treeTextures) {
            contextMenu->addAction("Save New Texture...", m_mainWindow, [this, executeAction](){
                executeAction([this](){
                    QString rootPath = QSettings().value("libraryRootPath").toString();
                    m_mainWindow->m_presetSerializer->saveTextureAs(QSettings().value("pathTextures", rootPath + "/textures").toString());
                });
            });
        } else if (senderTree == m_mainWindow->ui->treeMotions) {
            QString startDir = QSettings().value("pathRecords", rootPath + "/records").toString();
            contextMenu->addAction("Save New Record...", m_mainWindow, [this, startDir, executeAction](){
                executeAction([this, startDir](){ m_mainWindow->m_presetSerializer->saveMotion(startDir); });
            });
        } else if (senderTree == m_mainWindow->ui->treeSounds) {
            contextMenu->addAction("Save New Sound...", m_mainWindow, [this, executeAction](){
                executeAction([this](){
                    QString rootPath = QSettings().value("libraryRootPath").toString();
                    m_mainWindow->m_presetSerializer->saveSoundAs(QSettings().value("pathSounds", rootPath + "/sounds").toString(), "");
                });
            });
        }

        contextMenu->addSeparator();
    }

    bool clickedOnFolder = (refItem && refItem->data(0, Qt::UserRole + 10).isValid());
    if (!clickedOnFolder && (!m_mainWindow->m_cutFilePaths.isEmpty() || !m_mainWindow->m_cutTexturePaths.isEmpty())) {
        // Paste nel ramo PRINCIPALE: qui non c'e' un item da cui dedurre la
        // destinazione (su mobile getCurrentLibraryItem() e' nullo). Passiamo la
        // root della CATEGORIA corrente col case corretto (/surfaces, /records...
        // minuscoli, come nel resto del codice): il vecchio fallback su Surface
        // usava "/Surfaces" (maiuscolo, inesistente su iOS case-sensitive) -> copy
        // fallita -> paste inerte.
        QString rootPath = QSettings().value("libraryRootPath").toString();
        QString destRoot;
        // Case MINUSCOLO per tutte: le cartelle reali sono create minuscole
        // (mainwindow ~7430). Su iOS/APFS case-sensitive "/Textures"/"/Sounds"
        // non esistono -> copy fallita -> paste inerte (stesso bug di Surface).
        if (senderTree == m_mainWindow->ui->treeSurfaces)      destRoot = QSettings().value("pathSurfaces", rootPath + "/surfaces").toString();
        else if (senderTree == m_mainWindow->ui->treeMotions)  destRoot = QSettings().value("pathRecords",  rootPath + "/records").toString();
        else if (senderTree == m_mainWindow->ui->treeSounds)   destRoot = QSettings().value("pathSounds",   rootPath + "/sounds").toString();
        else if (senderTree == m_mainWindow->ui->treeTextures) destRoot = QSettings().value("pathTextures", rootPath + "/textures").toString();

        if (!m_mainWindow->m_cutFilePaths.isEmpty()) {
            contextMenu->addAction(QString("Paste %1 Item(s)").arg(m_mainWindow->m_cutFilePaths.count()), m_mainWindow, [this, destRoot, executeAction](){
                executeAction([this, destRoot](){ m_mainWindow->onPasteExample(destRoot); });
            });
        } else if (!m_mainWindow->m_cutTexturePaths.isEmpty()) {
            contextMenu->addAction(QString("Paste %1 Texture(s)").arg(m_mainWindow->m_cutTexturePaths.count()), m_mainWindow, [this, destRoot, executeAction](){
                executeAction([this, destRoot](){ m_mainWindow->onPasteTexture(destRoot); });
            });
        }
        contextMenu->addSeparator();
    }

    if (!m_mainWindow->m_undoStack.isEmpty()) {
        contextMenu->addAction("Undo Delete", m_mainWindow, [this, executeAction](){
            executeAction([this](){ m_mainWindow->onUndoDelete(); });
        });
        contextMenu->addSeparator();
    }

    contextMenu->addAction("Refresh Library", m_mainWindow, [this, executeAction](){
        executeAction([this](){ m_mainWindow->refreshRepositories(); });
    });
    contextMenu->addSeparator();

    contextMenu->addAction("Create New Folder...", m_mainWindow, [this, executeAction](){
        executeAction([this](){ m_mainWindow->onCreateFolderClicked(); });
    });
    contextMenu->addSeparator();

#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    // NOMI DELLE DUE VOCI: dicono la SCALA su cui operano.
    //
    // Erano "Open Workspace Folder..." e "Change Library Folder...": affiancate,
    // sembravano due varianti della stessa cosa, e non lo sono affatto -- la
    // prima cambia UN RAMO (quello dell'albero da cui si apre il menu), la
    // seconda cambia TUTTA la libreria e ignora l'albero. Chi le leggeva
    // sceglieva la seconda aspettandosi la prima, finendo per puntare la radice
    // della libreria a un ramo.
    // Ora la prima porta il nome del ramo corrente ("Change Folder for
    // Records...") e la seconda dice esplicitamente che vale per tutto
    // ("Change Library Folder..."): la differenza si vede dai nomi, senza
    // doverla scoprire per tentativi.
    QString branchLabel = "Surfaces";
    if (senderTree == m_mainWindow->ui->treeTextures)     branchLabel = "Textures";
    else if (senderTree == m_mainWindow->ui->treeMotions) branchLabel = "Records";
    else if (senderTree == m_mainWindow->ui->treeSounds)  branchLabel = "Sounds";

    // Nascondiamo l'apertura cartella Workspace su Mobile per evitare problemi di file system
    contextMenu->addAction(QString("Change Folder for %1...").arg(branchLabel), m_mainWindow,
                           [this, senderTree, executeAction,
                            motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
        executeAction([this, senderTree,
                       motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
            QSettings settings;
            QString rootPath = settings.value("libraryRootPath").toString();
            QString key;
            QString currentPath;
            if (senderTree == m_mainWindow->ui->treeTextures) { key = "pathTextures"; currentPath = settings.value(key, rootPath + "/Textures").toString(); }
            else if (senderTree == m_mainWindow->ui->treeMotions) { key = "pathRecords"; currentPath = settings.value(key, rootPath + "/records").toString(); }
            else if (senderTree == m_mainWindow->ui->treeSounds) { key = "pathSounds"; currentPath = settings.value(key, rootPath + "/Sounds").toString(); }
            else { key = "pathSurfaces"; currentPath = settings.value(key, rootPath + "/Surfaces").toString(); }

            QString dir = QFileDialog::getExistingDirectory(m_mainWindow, "Select Workspace Folder", currentPath);
            // ANNULLATO: i moti vanno rimessi qui. showMenu li ha fermati e li
            // ripristina in fondo, ma quel ripristino gira PRIMA della
            // singleShot in coda a questa voce; uscendo di qui la singleShot non
            // viene mai schedulata e nessuno li riaccende. Stessa ragione della
            // lambda restoreMotion in onAddRepositoryClicked, che ha il commento
            // esteso.
            if (dir.isEmpty()) {
                m_mainWindow->restoreMotionState(motionRotating, motionPath4D,
                                                 motionPath3D, motionTimeAnim);
                return;
            }

            settings.setValue(key, QDir::cleanPath(dir));
            settings.remove("repoPathsSurfaces"); settings.remove("repoPathsTextures");
            settings.remove("repoPathsMotions"); settings.remove("repoPathsSounds");
            settings.remove("repositoryPaths");

            // RINVIO A MENU CHIUSO: stessa ragione di onAddRepositoryClicked
            // (mainwindow.cpp), che ha il commento esteso. In breve: siamo
            // dentro contextMenu->exec(), con sopra il loop del pannello nativo,
            // e rileggere la libreria qui terrebbe il menu vivo per tutta la
            // scansione. Fuori dal menu il lavoro gira dal loop principale.
            //
            // I moti li rimette a posto refreshLibraryPreservingMotion, DOPO il
            // rebuild: il ripristino in fondo a showMenu gira prima di questo
            // singleShot e per giunta su flag che la rientranza ha falsato.
            QTimer::singleShot(0, m_mainWindow,
                               [this, motionRotating, motionPath4D, motionPath3D, motionTimeAnim]() {
                m_mainWindow->refreshLibraryPreservingMotion(motionRotating, motionPath4D,
                                                            motionPath3D, motionTimeAnim);
            });
        });
    });

    // SPOSTARE LA LIBRERIA: unico punto da cui questo comando e' raggiungibile.
    //
    // onAddRepositoryClicked esisteva ed era connessa a actionSelectFolder, ma
    // quell'azione non e' mai stata aggiunta a un menu (l'unico e' menuHelp:
    // Documentation/Quit/About), quindi era CODICE MORTO: non c'era modo di
    // cambiare la cartella della libreria.
    // "Restore Factory Presets" non copre il caso -- chiede la cartella solo se
    // la radice manca o e' irraggiungibile; con una libreria valida reinstalla i
    // preset dove sono e non sposta niente.
    // Va qui e non in un tasto: e' un'operazione rara, e il menu contestuale della
    // libreria e' dove si guarda quando si ha a che fare con le cartelle.
    //
    // Separatore: e' l'unica voce del menu che agisce su TUTTA la libreria
    // invece che sul ramo corrente, e stava attaccata a quella per-ramo.
    contextMenu->addSeparator();

    // senderTree NON si cattura: questo comando e' globale e non guarda da quale
    // albero e' stato aperto il menu. Qui si calcolava un LibraryType dal
    // senderTree per passarlo a onAddRepositoryClicked, che lo IGNORAVA (la
    // firma era `onAddRepositoryClicked(LibraryType /*type*/)`): codice che
    // non faceva nulla ma faceva sembrare il comando selettivo per ramo.
    contextMenu->addAction("Change Library Folder...", m_mainWindow,
                           [this, executeAction,
                            motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
        executeAction([this, motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
            // Lo stato dei moti viaggia fino alla rilettura: vedi
            // MainWindow::refreshLibraryPreservingMotion.
            m_mainWindow->onAddRepositoryClicked(motionRotating, motionPath4D,
                                                 motionPath3D, motionTimeAnim);
        });
    });
#endif

    // MOTI: NON SI TOCCANO DURANTE UNA RICOSTRUZIONE DELLA LIBRERIA.
    //
    // Il rebuild degli alberi riemette customContextMenuRequested e rientra qui
    // (misurato: 4 rientri, uno per albero). Quel rientro leggerebbe lo stato
    // dei moti da timer GIA' fermi -- li ha fermati la chiamata esterna --
    // registrando "fermo", e in fondo alla funzione spegnerebbe moti che
    // l'utente aveva acceso. E' il bug che ricompariva: non basta ripristinare
    // dopo il rebuild, perche' il rientro puo' arrivare anche PIU' TARDI,
    // dall'fsSyncTimer del QFileSystemWatcher (500 ms dopo il cambio cartella,
    // e chiama refreshRepositories direttamente).
    // Qui si salta stop e ripristino in blocco: chi ha avviato il rebuild
    // conosce lo stato vero e lo rimette lui.
    const bool skipMotionHandling = m_mainWindow->libraryRebuildInProgress();

    bool wasTimeAnimating = false;
    bool wasPath4D = false;
    bool wasPath3D = false;
    bool wasRotating = false;

    if (!skipMotionHandling) {
        if (m_mainWindow->m_btnStart && m_mainWindow->m_btnStart->text().toUpper() == "STOP") {
            wasTimeAnimating = true;
            m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
            m_mainWindow->ui->glWidget->stopAnimationTimer();
        }

        wasPath4D = m_mainWindow->pathTimer->isActive();
        wasPath3D = m_mainWindow->pathTimer3D->isActive();
        wasRotating = m_mainWindow->ui->glWidget->isAnimating();

        if (wasPath4D) m_mainWindow->pathTimer->stop();
        if (wasPath3D) m_mainWindow->pathTimer3D->stop();
        if (wasRotating) m_mainWindow->ui->glWidget->pauseMotion();
    }

// --- ESECUZIONE MENU ---
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // Esecuzione specifica Mobile
    contextMenu->exec(globalMenuPos);
    contextMenu->deleteLater();

    if (pendingAction) {
        QTimer::singleShot(350, m_mainWindow, [safeTree, safeRefItem, pendingAction]() {
            if (safeTree && safeRefItem) {
                safeTree->setCurrentItem(safeRefItem);
                safeRefItem->setSelected(true);
            }
            pendingAction();
        });
    }
#else
    // Esecuzione standard Desktop
    contextMenu->exec(senderTree->mapToGlobal(pos));
    delete contextMenu;
#endif

    if (wasTimeAnimating) {
        m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
        m_mainWindow->ui->glWidget->startAnimationTimer();
    }
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();
    if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
}
