#include "librarymenucontroller.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "presetserializer.h"
#include "libraryfileoperations.h"
#include "librarytreefocus.h"

#include <QMenu>
#include <QFileDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer> // Aggiunto per prevenire i crash su iOS/Android
#include <QMouseEvent>
#include <QTreeWidgetItemIterator>

LibraryMenuController::LibraryMenuController(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

void LibraryMenuController::watchTree(QTreeWidget *tree)
{
    if (tree) tree->viewport()->installEventFilter(this);
}

bool LibraryMenuController::eventFilter(QObject *obj, QEvent *event)
{
    // Ogni pressione fotografa l'evidenziazione (anche il dito su mobile: la
    // pressione lunga che apre il menu e' un tasto sinistro sintetizzato). Il
    // clic sinistro semplice la scarta al rilascio: quello carica, non apre
    // menu, e la foto resterebbe stantia per un menu aperto da tastiera.
    // Ctrl/Cmd+clic no: su macOS puo' essere il tasto destro.
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        auto *tree = qobject_cast<QTreeWidget *>(obj->parent());
        if (tree) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (event->type() == QEvent::MouseButtonPress)
                m_pressHighlight[tree] = LibraryTreeFocus::highlightOf(tree);
            else if (me->button() == Qt::LeftButton
                     && !(me->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
                m_pressHighlight.remove(tree);
        }
    }
    return QObject::eventFilter(obj, event);
}

void LibraryMenuController::showMenu(QTreeWidget *senderTree, const QPoint &pos)
{
    if (!senderTree) return;

    // L'EVIDENZIAZIONE DA RIMETTERE alla chiusura: quella della pressione che
    // ha aperto il menu (Qt l'ha gia' spostata sulla voce cliccata, e qui
    // sotto un clic sul vuoto la cancella); da tastiera, quella di adesso.
    // Non nei rientri da ricostruzione della libreria (vedi i moti, piu'
    // sotto): li' non c'e' un menu dell'utente da chiudere.
    const bool keepHighlight = !m_mainWindow->libraryRebuildInProgress();
    const LibraryTreeFocus::Highlight before = !keepHighlight ? LibraryTreeFocus::Highlight{}
                           : m_pressHighlight.contains(senderTree) ? m_pressHighlight.take(senderTree)
                                                                   : LibraryTreeFocus::highlightOf(senderTree);

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
    // Menu aperto: la SELEZIONE (blu) e' il bersaglio dei comandi, che leggono
    // solo lei; l'elemento CORRENTE (grigio) resta sul preset a schermo, che
    // cosi' non sparisce dall'albero finche' il menu e' aperto.
    QTreeWidgetItem *onScreen = nullptr;
    if (keepHighlight) {
        const QString onScreenPath = !before.current.isEmpty() ? before.current
                                   : before.paths.isEmpty()    ? QString() : before.paths.first();
        onScreen = LibraryTreeFocus::itemAt(senderTree, onScreenPath);
        if (onScreen) LibraryTreeFocus::setCurrentQuietly(senderTree, onScreen);
    }

    QList<QTreeWidgetItem*> selectedItems = senderTree->selectedItems();
    int count = selectedItems.count();
    QTreeWidgetItem* refItem = itemUnderMouse ? itemUnderMouse : (selectedItems.isEmpty() ? nullptr : selectedItems.first());

    QMenu *contextMenu = new QMenu(m_mainWindow);
    // Il ramo :disabled NON e' decorativo. Il QSS fissa 'color: #ffffff' per
    // tutto il menu, e senza una regola propria una voce disabilitata resta
    // BIANCA: indistinguibile da una attiva, quindi sembra che il comando sia
    // cliccabile e non faccia nulla (Qt non emette triggered su un'azione
    // disabilitata). Stessa famiglia di trappole del QSS globale sui tab.
    // ':selected:disabled' serve perche' la voce si evidenzia comunque al
    // passaggio del mouse: senza, tornerebbe bianca proprio mentre la si punta.
    contextMenu->setStyleSheet(
        "QMenu { background-color: #2b2b2b; color: #ffffff; border: 1px solid #3a3a3a; }"
        "QMenu::item { padding: 5px 20px; }"
        "QMenu::item:selected { background-color: #3a3a3a; }"
        "QMenu::item:disabled { color: #6e6e6e; }"
        "QMenu::item:selected:disabled { color: #6e6e6e; background-color: #2b2b2b; }"
        );

    // IL GRIGIO DEL PRESET A SCHERMO, finche' il menu e' a schermo. Fusion
    // disegna l'elemento corrente (grigio) solo con l'albero che ha il fuoco,
    // e il menu glielo toglie: lo si dipinge come sfondo della voce, nello
    // stesso tono, e lo si toglie alla chiusura -- prima del comando, che puo'
    // ricostruire l'albero (indice persistente: si toglie solo se c'e' ancora).
    if (onScreen && !onScreen->isSelected()) {
        const QPersistentModelIndex idx(senderTree->indexFromItem(onScreen));
        const QBrush previous = onScreen->background(0);
        onScreen->setBackground(0, QColor(60, 65, 72));
        connect(contextMenu, &QMenu::aboutToHide, senderTree, [senderTree, idx, previous] {
            if (QTreeWidgetItem *item = idx.isValid() ? senderTree->itemFromIndex(idx) : nullptr)
                item->setBackground(0, previous);
        });
    }

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
    const bool motionPath4D   = m_mainWindow->pathRunning(CameraPaths::Path4D);
    const bool motionPath3D   = m_mainWindow->pathRunning(CameraPaths::Path3D);
    const bool motionTimeAnim = m_mainWindow->isAnythingMoving();

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
            // UNA VOCE: i comandi comuni a ogni ramo (Rename, Copy, Cut, Delete,
            // col nome del ramo: "Rename Scene...") e le voci proprie di un
            // ramo. Il ramo lo dice la voce (LibraryManager::itemType), il
            // salvataggio MainWindow::saveInBranch.
            const std::optional<LibraryType> itemType = LibraryManager::itemType(refItem);
            const MainWindow::LibraryBranch *itemBranch = itemType ? m_mainWindow->libraryBranch(*itemType) : nullptr;
            bool isFolder  = refItem->data(0, Qt::UserRole + 10).isValid();

            if (itemBranch) {
                const LibraryFolders::Branch branch = itemBranch->info.branch;
                const QString item = QLatin1String(itemBranch->info.item);
                const QString path = m_mainWindow->m_libraryManager.itemFilePath(refItem);

                // 2.0: la Shape come OGGETTO della scena a schermo, che resta.
                if (branch == LibraryFolders::Branch::Surfaces) {
                    contextMenu->addAction("Add to Scene", m_mainWindow, [this, path, executeAction](){
                        executeAction([this, path](){ m_mainWindow->addShapeToScene(path); });
                    });
                    contextMenu->addSeparator();
                }
                // Un suono che non e' un preset (.wav, .mp3...) non ha "Save As".
                if (branch != LibraryFolders::Branch::Sounds || path.endsWith(".json", Qt::CaseInsensitive)) {
                    contextMenu->addAction("Save " + item + " As...", m_mainWindow, [this, branch, path, executeAction](){
                        executeAction([this, branch, path](){
                            m_mainWindow->saveInBranch(branch, QFileInfo(path).absolutePath(), path);
                        });
                    });
                }

                if (branch == LibraryFolders::Branch::Records) {
                    // SYNC FOCUSED TEXTURE: solo sul record CARICATO, e solo se la sua
                    // texture di libreria e' stata modificata dopo il salvataggio.
                    // Il record cliccato puo' non essere quello in scena (la selezione
                    // dell'albero segue il click), e agire su un altro significherebbe
                    // aggiornare una scena che l'utente non sta guardando.
                    // Voce sempre PRESENTE ma disabilitata quando non farebbe nulla:
                    // farla sparire lascerebbe intendere che la funzione non esista.
                    const bool isLoaded = !m_mainWindow->currentRecordPath().isEmpty()
                                          && QFileInfo(m_mainWindow->currentRecordPath()) == QFileInfo(path);
                    // Superficie O sfondo: basta che una delle due texture del
                    // record sia rimasta indietro rispetto alla sua voce di libreria.
                    // Il comando non segue il radio Surface/Background del Renderer:
                    // aggiorna quelle indietro, e il tooltip dice QUALI, perche' dal
                    // menu non si vede.
                    // ...e le FASCE, ognuna con la sua ancora: il comando le
                    // aggiorna tutte insieme, qualunque sia la mesh selezionata.
                    const bool surfUpdate = isLoaded && m_mainWindow->focusedTextureLibraryItem();
                    const bool bgUpdate   = isLoaded && m_mainWindow->focusedBgTextureLibraryItem();
                    const int  meshCount  = isLoaded ? m_mainWindow->focusedMeshTextureLibraryItems().size() : 0;
                    const bool hasUpdate  = surfUpdate || bgUpdate || meshCount > 0;

                    QStringList what;
                    if (surfUpdate)    what << QStringLiteral("surface");
                    if (bgUpdate)      what << QStringLiteral("background");
                    if (meshCount > 0) what << (meshCount == 1 ? QStringLiteral("1 mesh")
                                                               : QString("%1 meshes").arg(meshCount));

                    QAction *actSync = contextMenu->addAction("Sync Focused Texture", m_mainWindow, [this, executeAction](){
                        executeAction([this](){ m_mainWindow->syncFocusedTextureFromLibrary(); });
                    });
                    actSync->setEnabled(hasUpdate);
                    actSync->setToolTip(!isLoaded ? QStringLiteral("Only for the record in the scene.")
                                      : hasUpdate ? QStringLiteral("Updates textures: ") + what.join(", ") + "."
                                      : QStringLiteral("Textures already up to date."));
                    contextMenu->setToolTipsVisible(true);
                }
                contextMenu->addAction("Rename " + item + "...", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->renameLibraryItem(refItem); });
                });
                contextMenu->addAction("Copy " + item, m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
                });
                contextMenu->addAction("Cut " + item, m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
                });
                contextMenu->addAction("Delete " + item, m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isFolder) {
                QString folderPath = refItem->data(0, Qt::UserRole + 10).toString();

                // folderPath passato sempre (anche su desktop): il vecchio ramo
                // Shape senza argomento ripiegava sulla selezione corrente e, in
                // sua assenza, su lastFolder ("ultima usata").
                if (const MainWindow::LibraryBranch *treeBranch = m_mainWindow->libraryBranch(senderTree)) {
                    const LibraryFolders::Branch branch = treeBranch->info.branch;
                    contextMenu->addAction("Save " + QLatin1String(treeBranch->info.item) + " Here...", m_mainWindow,
                                           [this, branch, folderPath, executeAction](){
                        executeAction([this, branch, folderPath](){ m_mainWindow->saveInBranch(branch, folderPath); });
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
                contextMenu->addAction("Rename Folder...", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->renameLibraryItem(refItem); });
                });
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
        if (const MainWindow::LibraryBranch *treeBranch = m_mainWindow->libraryBranch(senderTree)) {
            const LibraryFolders::Branch branch = treeBranch->info.branch;
            const QString startDir = LibraryFolders::branchDir(branch);
            contextMenu->addAction("Save New " + QLatin1String(treeBranch->info.item) + "...", m_mainWindow,
                                   [this, branch, startDir, executeAction](){
                executeAction([this, branch, startDir](){ m_mainWindow->saveInBranch(branch, startDir); });
            });
        }

        contextMenu->addSeparator();
    }

    bool clickedOnFolder = (refItem && refItem->data(0, Qt::UserRole + 10).isValid());
    if (!clickedOnFolder && (!m_mainWindow->m_cutFilePaths.isEmpty() || !m_mainWindow->m_cutTexturePaths.isEmpty())) {
        // Paste nel ramo PRINCIPALE: qui non c'e' un item da cui dedurre la
        // destinazione (su mobile getCurrentLibraryItem() e' nullo). Passiamo la
        // root della CATEGORIA corrente (LibraryFolders::branchDir: cartelle
        // minuscole, come le crea la libreria; il vecchio fallback su Surface
        // usava "/Surfaces", inesistente su iOS case-sensitive -> paste inerte).
        const QString destRoot = LibraryFolders::branchDir(m_mainWindow->branchOfTree(senderTree));

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
    // Il ramo dell'albero (Shapes se non e' uno dei cinque, come prima).
    LibraryFolders::BranchInfo branch = LibraryFolders::branches().first();
    for (const LibraryFolders::BranchInfo &b : LibraryFolders::branches())
        if (b.branch == m_mainWindow->branchOfTree(senderTree)) branch = b;
    const QString branchLabel = QLatin1String(branch.label);

    // Nascondiamo l'apertura cartella Workspace su Mobile per evitare problemi di file system
    contextMenu->addAction(QString("Change Folder for %1...").arg(branchLabel), m_mainWindow,
                           [this, branch, executeAction,
                            motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
        executeAction([this, branch,
                       motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
            QSettings settings;
            const QString key = QLatin1String(branch.key);
            const QString currentPath = LibraryFolders::branchDir(branch.branch);

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
    // Rotazioni e orologio della geometria, PER OGGETTO (vedi
    // GLWidget::holdObjectMotion): Add to Scene cambia l'oggetto selezionato.
    GLWidget::ObjectMotionHold heldMotion;

    if (!skipMotionHandling) {
        if (m_mainWindow->isAnythingMoving()) {
            wasTimeAnimating = true;
            m_mainWindow->ui->glWidget->stopAnimationTimer();
        }

        wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
        wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);

        if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);
        heldMotion = m_mainWindow->ui->glWidget->holdObjectMotion();
    }

// --- ESECUZIONE MENU ---
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // Esecuzione specifica Mobile
    contextMenu->exec(globalMenuPos);
    contextMenu->deleteLater();

    if (pendingAction) {
        QTimer::singleShot(350, m_mainWindow, [safeTree, safeRefItem, pendingAction, keepHighlight, before]() {
            if (safeTree && safeRefItem) {
                safeTree->setCurrentItem(safeRefItem);
                safeRefItem->setSelected(true);
            }
            pendingAction();
            if (keepHighlight) LibraryTreeFocus::restoreHighlight(safeTree, before);
        });
    } else if (keepHighlight) {
        LibraryTreeFocus::restoreHighlight(senderTree, before);
    }
#else
    // Esecuzione standard Desktop
    contextMenu->exec(senderTree->mapToGlobal(pos));
    delete contextMenu;
    // Il comando e' gia' stato eseguito (dentro exec).
    if (keepHighlight) LibraryTreeFocus::restoreHighlight(senderTree, before);
#endif

    if (wasTimeAnimating) m_mainWindow->ui->glWidget->startAnimationTimer();
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
    m_mainWindow->ui->glWidget->releaseObjectMotion(heldMotion);
    // Il tasto GO dice se ruota l'oggetto selezionato ADESSO.
    if (m_mainWindow->ui->btnStart_2)
        m_mainWindow->ui->btnStart_2->setText(m_mainWindow->ui->glWidget->isAnimating() ? "STOP" : "GO");
}
