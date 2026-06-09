#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "surfaceengine.h"
#include "expressionparser.h"
#include "uistylemanager.h"
#include "glsltranslator.h"
#include "videorecorder.h"
#include "librarymenucontroller.h"
#include "presetserializer.h"
#include "libraryfileoperations.h"
#include "librarydragdrophandler.h"
#include "audiocontroller.h"
#include "inputvalidator.h"
#include "expressionparser.h"

#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QAction>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QFileDialog>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTreeWidgetItem>
#include <QButtonGroup>
#include <QStandardPaths>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScroller>
#include <QScrollBar>
#include <QSettings>
#include <QGestureEvent>
#include <QTapAndHoldGesture>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDirIterator>
#include <QInputDialog>
#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <QPainter>
#include <QDropEvent>
#include <QTextBrowser>
#include <QDialog>
#include <QVBoxLayout>
#include <QEvent>
#include <QTextStream>
#include <QtConcurrent>
#include <QFuture>
#include <QClipboard>
#include <QInputMethod>
#include <QGuiApplication>
#include <cmath>
#include <algorithm>
#include <functional>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QCoreApplication>
#endif

#if defined(Q_OS_ANDROID)
void notifyAndroidMediaStore(const QString& filePath) {
    QJniEnvironment env;
    jstring jFilePath = env->NewStringUTF(filePath.toUtf8().constData());
    jobjectArray pathsArray = env->NewObjectArray(1, env->FindClass("java/lang/String"), jFilePath);

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "android/media/MediaScannerConnection",
        "scanFile",
        "(Landroid/content/Context;[Ljava/lang/String;[Ljava/lang/String;Landroid/media/MediaScannerConnection$OnScanCompletedListener;)V",
        context.object(),
        pathsArray,
        nullptr,
        nullptr
    );

    env->DeleteLocalRef(pathsArray);
    env->DeleteLocalRef(jFilePath);
}
#endif

// ==========================================
// Controllo robustezza valori prima dell'invio in GPU
// ==========================================
static constexpr double kMaxRenderableMagnitude = 1.0e5;
static constexpr double kSpikeRatio = 50.0;
static constexpr double kAliasEdgeFraction = 0.15;

static inline bool isMeshSafeValue(double val) {
    return std::isfinite(val) && std::abs(val) <= kMaxRenderableMagnitude;
}

// ==========================================
// Filtro per catturare il ridimensionamento OpenGL
// ==========================================
class GLResizeFilter : public QObject {
public:
    std::function<void()> onResize;
    GLResizeFilter(QObject* parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Resize) {
            if (onResize) onResize();
        }
        return QObject::eventFilter(obj, event);
    }
};

// ==========================================
// Filtro per Desktop: Blocca l'andata a capo nelle equazioni
// ==========================================
class DesktopInputFilter : public QObject {
public:
    DesktopInputFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                const QString on = obj->objectName();
                if (on == "txtScriptEditor" || on == "lineVariations" || on == "lineTexture") {
                    return false;
                }

                // Campi equazione principali del tab parametrico:
                // Enter NON deve eseguire. Lo consumiamo (niente a-capo) e basta.
                if (on == "lineX" || on == "lineY" || on == "lineZ" || on == "lineP") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    return true;
                }

                if (QWidget* w = qobject_cast<QWidget*>(obj)) {
                    w->clearFocus();

                    // Applica i campi modificati (sia in moto che a superficie ferma)
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitFieldsOnEnter();
                    }
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

// ==========================================
// Filtro per Mobile: Tastiera e Navigazione
// ==========================================
class MobileInputFilter : public QObject {
    bool m_isProcessingQuery = false; // Evita loop infiniti durante l'intercettazione
public:
    MobileInputFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {

        // 1. DISATTIVAZIONE MENU CONTESTUALI (Usiamo la nostra Toolbar)
        if (event->type() == QEvent::ContextMenu) {
            const QString on = obj->objectName();
            if (on == "txtScriptEditor" || on == "lineVariations" || on == "lineTexture") {
                return false;
            }
            return true;
        }

        // 2. GESTIONE TASTI INVIO E TAB
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            // Intercettiamo Return, Enter e Tab
            if (keyEvent->key() == Qt::Key_Return ||
                keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Tab) {

                if (obj->objectName() == "txtScriptEditor") {
                    return false;
                }

                if (QWidget* w = qobject_cast<QWidget*>(obj)) {
                    w->clearFocus();

                    // Notifica a MainWindow di applicare i campi modificati
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitUiFieldsDuringMotion();
                    }

                    QGuiApplication::inputMethod()->hide();
                    return true;
                }
            }
        }

        return QObject::eventFilter(obj, event);
    }
};

class EnterApplyFilter : public QObject {
public:
    std::function<void()> onEnter;
    EnterApplyFilter(QObject* parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                    if (onEnter) onEnter();
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // =========================================================================
    // 1. CORE INITIALIZATION
    // =========================================================================
    ui->setupUi(this);

    // FULL IMMERSION
    if (this->centralWidget() && this->centralWidget()->layout()) {
        this->centralWidget()->layout()->setContentsMargins(0, 0, 0, 0);
        this->centralWidget()->layout()->setSpacing(0);
    }

    //QSettings().clear(); // Primo avvio dell'applicazione

    setWindowTitle("Surface Explorer");
    setAttribute(Qt::WA_AcceptTouchEvents);

    m_isCustomMode = false;
    m_isImageMode = false;
    m_blockTextureGen = false;
    m_currentTexturePath = "";
    m_currentTexturePresetPath = "";
    m_surfaceTextureState = false;

#if defined(Q_OS_ANDROID)
    // 1. Controlla prima l'API Level (Deve essere >= 30 per questa funzione)
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {

        // 2. Ora è sicuro chiamare isExternalStorageManager
        bool isStorageManager = QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager");

        if (!isStorageManager) {
            QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                              QJniObject::fromString("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION").object<jstring>());

            // 3. Recupera dinamicamente il VERO nome del pacchetto dell'app, niente hardcoding!
            QJniObject context = QNativeInterface::QAndroidApplication::context();
            QJniObject packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
            QString uriString = "package:" + packageName.toString();

            QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                                                QJniObject::fromString(uriString).object<jstring>());

            intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object());
            intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000); // FLAG_ACTIVITY_NEW_TASK

            if (context.isValid()) {
                context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
            }
        }
    }

    connect(QGuiApplication::inputMethod(), &QInputMethod::keyboardRectangleChanged,
            this, [this]() {

        QRectF kbdRect = QGuiApplication::inputMethod()->keyboardRectangle();
        int kbdHeight = kbdRect.height();

        // 1. Assicuriamoci che il central widget (e la vista 3D) rimangano
        // intatti a schermo intero senza innescare la Status Bar.
        if (ui->centralwidget) {
            ui->centralwidget->setContentsMargins(0, 0, 0, 0);
        }

        // 2. Applichiamo la compensazione SOLO ai pannelli laterali
        QList<QDockWidget*> docks = {
            ui->dockEquations, ui->dockScripts, ui->dockRenders,
            ui->dock3D, ui->dock4D, ui->dockSurfaces
        };

        for (QDockWidget* dock : docks) {
            // Se il dock è visibile e contiene la ScrollArea creata dal manager
            if (dock->isVisible() && dock->widget()) {
                if (kbdHeight > 0) {
                    // Comprime l'area di scorrimento aggiungendo spazio vuoto in fondo
                    dock->widget()->setContentsMargins(0, 0, 0, kbdHeight);

                    // Trova il campo di testo su cui stiamo digitando
                    QWidget* fw = this->focusWidget();
                    if (fw) {
                        // Un piccolo timer permette al layout di aggiornarsi prima di scorrere
                        QTimer::singleShot(100, fw, [fw]() {
                            QWidget* p = fw->parentWidget();
                            while (p) {
                                if (QScrollArea* sa = qobject_cast<QScrollArea*>(p)) {
                                    // Forza lo scorrimento verso il campo, lasciando 50px di margine
                                    sa->ensureWidgetVisible(fw, 0, 50);
                                    break;
                                }
                                p = p->parentWidget();
                            }
                        });
                    }
                } else {
                    // Tastiera chiusa: il dock torna a coprire l'intera altezza
                    dock->widget()->setContentsMargins(0, 0, 0, 0);
                }
            }
        }
    });

#endif

#if defined(Q_OS_IOS)
    // Su iPhone iOS non solleva la finestra automaticamente come fa su iPad,
    // quindi applichiamo lo stesso meccanismo di Android: margine inferiore +
    // scroll al widget focalizzato.
    {
        QSize s = QGuiApplication::primaryScreen()->availableSize();
        const bool isIPhone = qMin(s.width(), s.height()) < 700;
        if (isIPhone) {
            connect(QGuiApplication::inputMethod(), &QInputMethod::keyboardRectangleChanged,
                    this, [this]() {
                QRectF kbdRect = QGuiApplication::inputMethod()->keyboardRectangle();
                int kbdHeight = kbdRect.height();

                QList<QDockWidget*> docks = {
                    ui->dockEquations, ui->dockScripts, ui->dockRenders,
                    ui->dock3D, ui->dock4D, ui->dockSurfaces
                };

                for (QDockWidget* dock : docks) {
                    if (dock->isVisible() && dock->widget()) {
                        if (kbdHeight > 0) {
                            dock->widget()->setContentsMargins(0, 0, 0, kbdHeight);
                            QWidget* fw = this->focusWidget();
                            if (fw) {
                                QTimer::singleShot(100, fw, [fw]() {
                                    QWidget* p = fw->parentWidget();
                                    while (p) {
                                        if (QScrollArea* sa = qobject_cast<QScrollArea*>(p)) {
                                            sa->ensureWidgetVisible(fw, 0, 50);
                                            break;
                                        }
                                        p = p->parentWidget();
                                    }
                                });
                            }
                        } else {
                            dock->widget()->setContentsMargins(0, 0, 0, 0);
                        }
                    }
                }
            });
        }
    }
#endif

    // --- SBLOCCO DEI CAMPI COSTANTI (Permette lettere, 'pi', formule) ---
    ui->lineA->setValidator(nullptr);
    ui->lineB->setValidator(nullptr);
    ui->lineC->setValidator(nullptr);
    ui->lineD->setValidator(nullptr);
    ui->lineE->setValidator(nullptr);
    ui->lineF->setValidator(nullptr);
    ui->lineS->setValidator(nullptr);

    // =========================================================================
    // 2. STYLING & FONTS
    // =========================================================================
    UiStyleManager::applyPlatformStyle(this);
    UiStyleManager::applyDarkTheme(this);

    QList<QWidget*> mainInputFields = {
        ui->lineX, ui->lineY, ui->lineZ, ui->lineP,
        ui->lineX_P3D, ui->lineY_P3D, ui->lineZ_P3D, ui->lineR_P3D,
        ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P,
        ui->lineAlpha_P, ui->lineBeta_P, ui->lineGamma_P
    };
    UiStyleManager::applyInputFieldsStyle(mainInputFields);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // ==========================================
    // 1. COMPATTAZIONE INTERFACCIA
    // ==========================================
    QList<QWidget*> dockContents = {
        ui->dockEquations->widget(),
        ui->dockRenders->widget(),
        //ui->dockScripts->widget(),
        ui->dock3D->widget(),
        ui->dock4D->widget(),
        ui->dockSurfaces->widget()
    };

    // CHIAMATA UNICA CENTRALIZZATA
    UiStyleManager::compactForMobile(dockContents);

    // ==========================================
    // FONT E LENTE IOS (VERSIONE CORRETTA VIA LAYOUT)
    // ==========================================
    QString mobileInputStyle = "QLineEdit, QPlainTextEdit, QTextEdit { font-size: 14px; }";

    if (ui->dockEquations->widget()) {
        QString currentEqStyle = ui->dockEquations->widget()->styleSheet();
        ui->dockEquations->widget()->setStyleSheet(currentEqStyle + " " + mobileInputStyle);
    }

    if (ui->dockScripts->widget()) {
        QWidget* originalWidget = ui->dockScripts->widget();

        // Applichiamo la dimensione del font pulita al widget originale
        QString currentScriptStyle = originalWidget->styleSheet();
        originalWidget->setStyleSheet(currentScriptStyle + " " + mobileInputStyle);

        // --- SOLUZIONE: INIEZIONE DI UNA QSCROLLAREA PER L'INTERO DOCK ---
        // Creiamo la scroll area che conterrà l'intero blocco del pannello script
        QScrollArea* dockScrollArea = new QScrollArea(ui->dockScripts);
        dockScrollArea->setWidgetResizable(true);
        dockScrollArea->setFrameShape(QFrame::NoFrame); // Rimuove bordi interni antiestetici di Qt
        dockScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        dockScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        // FORZATURA ALTEZZA: Garantisce che l'editor abbia sempre uno spazio di digitazione
        // umano (es. 300px) e non venga mai schiacciato a zero pixel dalla tastiera.
        ui->txtScriptEditor->setMinimumHeight(300);

        // Attiviamo lo scorrimento cinetico (Touch Gesture) sull'intero contenitore del dock
        QScroller::grabGesture(dockScrollArea->viewport(), QScroller::TouchGesture);

        // Sostituiamo il widget principale del dock inserendovi la ScrollArea,
        // e mettiamo il vecchio widget all'interno della scroll area.
        dockScrollArea->setWidget(originalWidget);
        ui->dockScripts->setWidget(dockScrollArea);

    }


    // ==========================================
    // 2. GESTIONE TASTIERA E NAVIGAZIONE CURSORE
    // ==========================================
    MobileInputFilter* mobileFilter = new MobileInputFilter(this);
    QList<QWidget*> allTextInputs;

    // Riempiamo la lista iterando sui risultati per permettere il cast a QWidget*
    for(auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        allTextInputs.append(textEdit);
    }
    for(auto* lineEdit : this->findChildren<QLineEdit*>()) {
        allTextInputs.append(lineEdit);
    }

    for (QWidget* input : allTextInputs) {
        input->installEventFilter(mobileFilter);

        input->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    }

    for (QWidget* input : allTextInputs) {
        input->installEventFilter(mobileFilter);

        // 1. Recuperiamo gli hint nativi del widget
        Qt::InputMethodHints hints = input->inputMethodHints();

        // 2. Ripristiniamo ESATTAMENTE i flag che volevi tu
        hints |= Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase;

        // 3. La magia per l'Invio: filtriamo in base al nome dell'oggetto
        if (input->objectName() == "txtScriptEditor") {
            // Per lo script: ci assicuriamo che il MultiLine sia ACCESO
            hints |= Qt::ImhMultiLine;
        } else {
            // Per le equazioni: SPEGNIAMO forzatamente il MultiLine!
            // L'operatore &= ~ rimuove uno specifico bit preservando gli altri.
            // Così la tastiera mostrerà il tasto "Fatto/Vai" e innescherà la chiusura.
            hints &= ~Qt::ImhMultiLine;
        }

        // 4. Applichiamo i flag finali
        input->setInputMethodHints(hints);
    }

    UiStyleManager::setupRaymarchTabMobile(ui->dockEquations->widget());
#endif

    // =========================================================================
    // 3. DOCK WIDGETS & TAB LAYOUT
    // =========================================================================
    // --- RIORDINO SCHEDE LIBRARY (FIX DESIGNER) ---
    ui->tabWidget->clear();
    ui->tabWidget->addTab(ui->Surface, "Surfaces");
    ui->tabWidget->addTab(ui->Texture, "Textures");
    ui->tabWidget->addTab(ui->Sounds, "Sounds");
    ui->tabWidget->addTab(ui->Motions, "Records");
    ui->tabWidget->setCurrentIndex(0);

    UiStyleManager::setupDockScroll(ui->dockEquations, true);
    UiStyleManager::setupDockScroll(ui->dockRenders, true);
    UiStyleManager::setupDockScroll(ui->dock3D, true);
    UiStyleManager::setupDockScroll(ui->dock4D, true);
    UiStyleManager::setupDockScroll(ui->dockScripts, true);
    UiStyleManager::setupDockScroll(ui->dockSurfaces, true);

    if (ui->dockEquations) UiStyleManager::addScrollToDock(ui->dockEquations);
    if (ui->dockRenders) UiStyleManager::addScrollToDock(ui->dockRenders);
    if (ui->dock3D) UiStyleManager::addScrollToDock(ui->dock3D);
    if (ui->dock4D) UiStyleManager::addScrollToDock(ui->dock4D);
    if (ui->dockScripts) UiStyleManager::addScrollToDock(ui->dockScripts);

    // --- BLOCCO SPOSTAMENTO DOCK ---
    auto lockDock = [](QDockWidget* dock) {
        dock->setFeatures(QDockWidget::DockWidgetClosable);
    };

    lockDock(ui->dockEquations);
    lockDock(ui->dockRenders);
    lockDock(ui->dock3D);
    lockDock(ui->dock4D);
    lockDock(ui->dockScripts);
    lockDock(ui->dockSurfaces);
    // --------------------------------

    // Diciamo a Qt di mettere TUTTI i pannelli a DESTRA (RightDockWidgetArea)
    addDockWidget(Qt::RightDockWidgetArea, ui->dockEquations);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockRenders);
    addDockWidget(Qt::RightDockWidgetArea, ui->dock3D);
    addDockWidget(Qt::RightDockWidgetArea, ui->dock4D);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockScripts);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockSurfaces);

    // IMPILIAMO i pannelli uno sopra l'altro (Tabify)
    tabifyDockWidget(ui->dockEquations, ui->dockRenders);
    tabifyDockWidget(ui->dockRenders, ui->dock3D);
    tabifyDockWidget(ui->dock3D, ui->dock4D);
    tabifyDockWidget(ui->dock4D, ui->dockScripts);
    tabifyDockWidget(ui->dockScripts, ui->dockSurfaces);

    // INVISIBILITÀ INIZIALE
    ui->dockEquations->close();
    ui->dockRenders->close();
    ui->dock3D->close();
    ui->dock4D->close();
    ui->dockScripts->close();
    ui->dockSurfaces->close();

    // =========================================================================
    // 4. GLOBAL ACTIONS & MACOS MENUS
    // =========================================================================
    connect(ui->actionSave, &QAction::triggered, this, [this](){ saveSurfaceToFile(); });
    connect(ui->actionDelete, &QAction::triggered, this, &MainWindow::deleteSelectedExample);
    connect(ui->actionSelectFolder, &QAction::triggered, this, [this](){
        QWidget* currentTab = ui->tabWidget->currentWidget();
        LibraryType currentType = LibraryType::Surface;
        if (currentTab == ui->Texture) currentType = LibraryType::Texture;
        else if (currentTab == ui->Motions) currentType = LibraryType::Motion;
        else if (currentTab->objectName().contains("Sound", Qt::CaseInsensitive)) currentType = LibraryType::Sound;
        onAddRepositoryClicked(currentType);
    });

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    ui->actionSelectFolder->setVisible(false);
#endif

    connect(ui->actionCut, &QAction::triggered, this, [this](){ performCut(nullptr); });
    connect(ui->actionPaste, &QAction::triggered, this, [this](){
        if (!m_cutFilePaths.isEmpty()) onPasteExample();
        else if (!m_cutTexturePaths.isEmpty()) onPasteTexture();
    });

    connect(ui->actionUndoDelete, &QAction::triggered, this, &MainWindow::onUndoDelete);

    // MACOS MENU
    ui->actionQuit->setMenuRole(QAction::QuitRole);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);

    ui->actionAbout->setMenuRole(QAction::AboutRole);
    connect(ui->actionAbout, &QAction::triggered, this, [this](){
        QMessageBox::about(this, "About Surface Explorer",
                           "<b>Surface Explorer</b><br>"
                           "Version 3.0<br><br>"
                           "Developed by: <b>Gaetano Moschetti</b><br>"
                           "License: <b>GNU GPL v3</b><br><br>"
                           "This is free software: you are free to change and redistribute it "
                           "under the terms of the GNU General Public License as published by "
                           "the Free Software Foundation.<br><br>"
                           "<i>Exploring the beauty of four-dimensional geometry.</i>");
    });

    ui->actionDocumentation->setMenuRole(QAction::NoRole);
    connect(ui->actionDocumentation, &QAction::triggered, this, [this](){

        QDialog* docDialog = new QDialog(this);
        docDialog->setWindowTitle("Documentation");

        QVBoxLayout* layout = new QVBoxLayout(docDialog);

        QTextBrowser* browser = new QTextBrowser(docDialog);
        browser->setOpenExternalLinks(true);
        browser->setSource(QUrl("qrc:/documentation.html"));

        QPushButton* closeBtn = new QPushButton("Close", docDialog);

        // 1. Decidiamo cosa fa il click: chiude la finestra o torna indietro?
        connect(closeBtn, &QPushButton::clicked, docDialog, [browser, docDialog, closeBtn]() {
            if (closeBtn->text() == "Back") {
                browser->backward(); // Torna alla pagina precedente della cronologia
            } else {
                docDialog->accept(); // Chiude la finestra normalmente
            }
        });

        // 2. Cambiamo automaticamente il testo del bottone leggendo la pagina corrente
        connect(browser, &QTextBrowser::sourceChanged, docDialog, [closeBtn](const QUrl &src) {
            if (src.toString().endsWith("CREDITS.html", Qt::CaseInsensitive)) {
                closeBtn->setText("Back");
            } else {
                closeBtn->setText("Close");
            }
        });

        layout->addWidget(browser);
        layout->addWidget(closeBtn);

        // Deleghiamo tutta l'estetica a UiStyleManager!
        UiStyleManager::setupDocumentationDialog(docDialog, layout, browser, closeBtn);

        // 1. Trova il bottone PRIMA di aprire il dialog e salva la sua posizione reale
        QPoint originalPos;
        QPushButton* menuBtn = this->findChild<QPushButton*>("mobileMenuBtn");
        if (menuBtn) {
            originalPos = menuBtn->pos();
        }

        // 2. Apri la documentazione (il codice si ferma qui finché non chiudi la finestra)
        docDialog->exec();

        // 3. Ripristina il bottone
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Passiamo 'originalPos' dentro la lambda per ricordarci dove stava
        QTimer::singleShot(50, this, [this, originalPos]() {
            QPushButton* btn = this->findChild<QPushButton*>("mobileMenuBtn");
            if (btn) {
                btn->raise();
                // Proviamo a rimetterlo dove era "nativamente"
                if (!originalPos.isNull()) {
                    btn->move(originalPos);
                } else {
                    // Fallback unificato: ora che la barra è nascosta, 10,10 è perfetto per tutti
                    btn->move(10, 10);
                }
                btn->show();
            }
        });
#endif
    });

    // =========================================================================
    // 5. STATUS BAR & BOTTOM CONTROLS
    // =========================================================================
    m_btnStart = new QPushButton("START", this);
    m_btnStart->setFlat(true);
    QFont fontBold = m_btnStart->font();
    fontBold.setBold(true);
    m_btnStart->setFont(fontBold);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    m_btnResetView = new QPushButton("RESET", this);
    m_btnResetView->setFlat(true);
    m_btnResetView->setFont(fontBold);
    connect(m_btnResetView, &QPushButton::clicked, this, &MainWindow::onResetViewClicked);

    m_btnProjection = new QPushButton("Perspective", this);
    m_btnProjection->setFlat(true);
    m_btnProjection->setFont(fontBold);
    connect(m_btnProjection, &QPushButton::clicked, this, &MainWindow::toggleProjection);

    m_btnRec = new QPushButton("REC", this);
    m_btnRec->setFlat(true);
    m_btnRec->setFont(fontBold);
    UiStyleManager::applyRecordButtonStyle(m_btnRec);
    m_videoRecorder = new VideoRecorder(this, this);
    connect(m_btnRec, &QPushButton::clicked, m_videoRecorder, &VideoRecorder::toggleRecord);

    m_statusLabel = new QLabel("", this);
    m_renderProgress = new QProgressBar(this);
    m_renderProgress->setRange(0, 100);
    m_renderProgress->setValue(0);
    m_renderProgress->setTextVisible(true);
    m_renderProgress->setVisible(false);
    m_renderProgress->setFixedWidth(150);

    ui->statusbar->addWidget(m_btnStart);
    ui->statusbar->addWidget(m_btnResetView);
    ui->statusbar->addWidget(m_btnProjection);
    ui->statusbar->addWidget(m_btnRec);
    ui->statusbar->addWidget(m_renderProgress);
    ui->statusbar->addWidget(m_statusLabel, 1);

    // Lambda helper apertura dock
    auto closeAllDocks = [this](){
        ui->dockEquations->close();
        ui->dockRenders->close();
        ui->dock3D->close();
        ui->dock4D->close();
        ui->dockScripts->close();
        ui->dockSurfaces->close();
    };

    auto safeOpenDock = [this, closeAllDocks](QDockWidget* dock) {

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        static qint64 lastToggleTime = 0;
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - lastToggleTime < 400) return;
        lastToggleTime = currentTime;

        if (dock->isVisible()) {
            dock->close();
            return;
        }

        if (dock->isFloating()) dock->setFloating(false);

        // 1. CONGELIAMO LO SCHERMO (Elimina il flash visivo)
        this->setUpdatesEnabled(false);

        // 2. FORZIAMO LA LARGHEZZA (Evita il collasso a 0 pixel e il "millimetro" di splitter)
        dock->setFixedWidth(400);

        // 3. AGGANCIAMO E MOSTRIAMO
        this->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->show();
        dock->raise();

        // 4. CHIUDIAMO I VECCHI IN BACKGROUND
        if (dock != ui->dockEquations) ui->dockEquations->close();
        if (dock != ui->dockRenders) ui->dockRenders->close();
        if (dock != ui->dock3D) ui->dock3D->close();
        if (dock != ui->dock4D) ui->dock4D->close();
        if (dock != ui->dockScripts) ui->dockScripts->close();
        if (dock != ui->dockSurfaces) ui->dockSurfaces->close();

        // 5. SCONGELIAMO LO SCHERMO
        this->setUpdatesEnabled(true);

        QTimer::singleShot(100, dock, [this, dock]() {
            dock->setMinimumWidth(400);

            if (dock == ui->dockEquations) {
                dock->setMaximumWidth(400);
            } else {
                dock->setMaximumWidth(16777215);
            }

            // 6. SBLOCCHIAMO LA LARGHEZZA DOPO L'APERTURA
            QTimer::singleShot(100, dock, [this, dock]() {
                dock->setMinimumWidth(400);

                if (dock == ui->dockEquations) {
                    // Blocca l'espansione massima SOLO per il dockEquations
                    dock->setMaximumWidth(400);
                } else {
                    // Lascia gli altri liberi
                    dock->setMaximumWidth(16777215);
                }
            });
        });

#else
        // =========================================================
        // RAMO DESKTOP
        // =========================================================
        if (dock->isVisible()) {
            dock->close();
            return;
        }

        closeAllDocks();

        if (dock->isFloating()) dock->setFloating(false);
        this->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->show();
#endif
    };

    // Tasti Docks nella Statusbar
    QPushButton* btnEq = new QPushButton("Equations", this); btnEq->setFlat(true);
    connect(btnEq, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockEquations); });
    ui->statusbar->addPermanentWidget(btnEq);

    QPushButton* btnRen = new QPushButton("Renderer", this); btnRen->setFlat(true);
    connect(btnRen, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockRenders); });
    ui->statusbar->addPermanentWidget(btnRen);

    QPushButton* btn3D = new QPushButton("3D View", this); btn3D->setFlat(true);
    connect(btn3D, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dock3D); });
    ui->statusbar->addPermanentWidget(btn3D);

    QPushButton* btn4D = new QPushButton("4D View", this); btn4D->setObjectName("btnDock4D"); btn4D->setFlat(true);
    connect(btn4D, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dock4D); });
    ui->statusbar->addPermanentWidget(btn4D);

    QPushButton* btnScript = new QPushButton("Script", this); btnScript->setFlat(true);
    connect(btnScript, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockScripts); });
    ui->statusbar->addPermanentWidget(btnScript);

    QPushButton* btnEx = new QPushButton("Library", this); btnEx->setFlat(true);
    connect(btnEx, &QPushButton::clicked, this, [this, safeOpenDock](){
        QSettings settings;
        QString root = settings.value("libraryRootPath").toString();
        if (root.isEmpty() || !QDir(root).exists()) setupDefaultFolders();
        safeOpenDock(ui->dockSurfaces);
    });
    ui->statusbar->addPermanentWidget(btnEx);

    // TOP MESSAGE OVERLAY (Toast Notification)
    m_topMessageBar = new QLabel(this->centralWidget());
    m_topMessageBar->setAlignment(Qt::AlignCenter);
    m_topMessageBar->hide(); // Nascosto di default

    // Crea una piccola ombra per farlo staccare dal 3D
    UiStyleManager::applyToastShadow(m_topMessageBar);

    m_topMessageTimer = new QTimer(this);
    m_topMessageTimer->setSingleShot(true);
    connect(m_topMessageTimer, &QTimer::timeout, m_topMessageBar, &QLabel::hide);

    // =========================================================================
    // 6. EQUATIONS, CONSTANTS & PARAMETERS
    // =========================================================================

    // 1. INIZIALIZZA LA MEMORIA
    m_lastParametricSteps = 100;
    m_lastImplicitSteps = 400;

    // 2. PREPARA L'INTERFACCIA E LO SLIDER AL LORO STATO INIZIALE (Senza lanciare segnali!)
    ui->tabModeSelector->setCurrentIndex(0);
    ui->glWidget->setEngineMode(GLWidget::ModeParametric);

    ui->stepSlider->setRange(10, 1000);
    ui->stepSlider->setValue(m_lastParametricSteps);
    ui->lineSteps->setText(QString::number(m_lastParametricSteps));
    ui->glWidget->setResolution(m_lastParametricSteps);

    ui->lblSteps->setText("Steps=");

    // 3. SOLO ORA COLLEGA IL SEGNALE DEL CAMBIO TAB
    connect(ui->tabModeSelector, &QTabWidget::currentChanged, this, [this](int index) {
        ui->stepSlider->blockSignals(true);

        auto resetExtraFields = [this]() {
            bool oldU = ui->lineU->blockSignals(true);
            ui->lineU->clear(); ui->lineV->clear(); ui->lineW->clear();
            ui->lineExplicitU->clear(); ui->lineExplicitV->clear(); ui->lineExplicitW->clear();

            if (ui->lnU) {
                ui->lnU->clear(); ui->lnV->clear(); ui->lnW->clear();
                ui->lndU->clear(); ui->lndV->clear(); ui->lndW->clear();
                ui->lineConform->clear();
            }
            ui->lineU->blockSignals(oldU);
        };

        resetExtraFields();

        // ==========================================================
        // AGGIORNAMENTO UI E PULIZIA MOTORE SCRIPT AL CAMBIO TAB
        // ==========================================================
        // 1. Aggiorna dinamicamente i nomi sui bottoni del dock script
        updateScriptButtonText();

        // 2. Spegne la modalità script per evitare che il codice parametrico
        // finisca nel Ray Marching (e causi il crash "unexpected EQUAL")
        if (ui->glWidget && ui->glWidget->getEngine()) {
            ui->glWidget->getEngine()->setScriptMode(false);
            ui->glWidget->getEngine()->setScriptCodeGLSL("");
        }

        // 3. Svuota l'editor visivamente (se aperto su Surface) e in memoria
        if (m_currentScriptMode == ScriptModeSurface) {
            ui->txtScriptEditor->blockSignals(true);
            ui->txtScriptEditor->clear();
            ui->txtScriptEditor->blockSignals(false);
        }
        m_surfaceScriptText.clear();
        // ==========================================================

        // ==========================================================
        // GESTIONE STATI TEXTURE
        // ==========================================================
        m_surfaceTextureState = false;
        m_isCustomMode = false;
        m_isImageMode = false;
        m_blockTextureGen = false;
        m_currentTexturePath.clear();
        m_surfaceTextureCode.clear();

        // Modifichiamo la UI solo se NON stiamo guardando il Background
        if (!ui->radioBackground->isChecked()) {
            bool oldBlock = ui->chkBoxTexture->blockSignals(true);
            ui->chkBoxTexture->setChecked(false);
            ui->chkBoxTexture->blockSignals(oldBlock);
        }

        // Spegniamo in modo incondizionato la texture dal motore per la superficie
        if (ui->glWidget) ui->glWidget->setTextureEnabled(false);
        // ==========================================================

        // ==========================================================
        // RESET AUDIO E COLORI
        // ==========================================================
        // 1. Ferma l'audio per QUALSIASI cambio tab
        if (m_audioController) {
            m_audioController->stopAll();
        }
        m_soundScriptText.clear();
        if (ui->btnRunCurrentScript && ui->btnRunCurrentScript->text() == "Stop Sound") {
            ui->btnRunCurrentScript->setText("Run Sound");
        }

        // 2. Resetta i colori al default per evitare "sanguinamenti" dai record precedenti
        m_currentBackgroundColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
        m_currentSurfaceColor = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
        m_currentBorderColor = m_currentSurfaceColor;
        m_texColor1 = m_currentSurfaceColor;
        m_texColor2 = Qt::black;
        m_bgTexColor1 = QColor::fromRgbF(0.2f, 0.2f, 0.8f);
        m_bgTexColor2 = Qt::black;

        if (ui->glWidget) {
            ui->glWidget->setBackgroundColor(m_currentBackgroundColor);
            ui->glWidget->setColor(m_currentSurfaceColor.redF(), m_currentSurfaceColor.greenF(), m_currentSurfaceColor.blueF());
            ui->glWidget->setBorderColor(m_currentBorderColor.redF(), m_currentBorderColor.greenF(), m_currentBorderColor.blueF());
            ui->glWidget->setTextureColors(m_texColor1, m_texColor2);
        }

        // Aggiorna gli slider colore della UI per allinearli ai valori appena resettati
        onColorTargetChanged();
        // ==========================================================

        if (index == 1) { // --- PASSAGGIO A IMPLICIT (RAY MARCHING) ---
            ui->lineEquation->setPlainText("x*x + y*y + z*z - 1.0");
            ui->lineTexture->setPlainText("vec3(0.5, 0.5, 0.5)"); // Grigio neutro o il tuo default
            ui->lineVariations->setPlainText("0.0");

            m_lastParametricSteps = ui->stepSlider->value();

            // 1. SALVA IN MEMORIA IL VALORE PARAMETRICO DELLA S
            m_lastParametricS = ui->lineS->text().toDouble();

            // 2. Ferma tutte le animazioni
            if (ui->glWidget->isAnimating()) ui->glWidget->pauseMotion();
            if (pathTimer->isActive()) pathTimer->stop();
            if (pathTimer3D->isActive()) pathTimer3D->stop();
            ui->glWidget->setSurfaceAnimating(false);
            ui->glWidget->stopAnimationTimer();

            // 4. Azzera Texture e Rilievi (Ritorna alla forma nuda)
            ui->lineTexture->blockSignals(true);
            ui->lineTexture->clear();
            ui->lineTexture->blockSignals(false);
            ui->glWidget->setTextureCode("");

            ui->lineVariations->blockSignals(true);
            ui->lineVariations->clear();
            ui->lineVariations->blockSignals(false);
            ui->glWidget->setDisplacementCode("");

            ui->glWidget->setBackgroundTextureEnabled(false);
                        m_bgTextureCode = "";
                        m_bgTextureScriptText = "";

            // 5. Ripristina l'equazione di default
            ui->lineEquation->blockSignals(true);
            QString eq = ui->lineEquation->toPlainText().trimmed();
            if (eq.isEmpty() || !eq.contains("=")) {
                ui->lineEquation->setPlainText("x^2 + y^2 + z^2 = 1.0");
            }
            ui->lineEquation->blockSignals(false);

            // ==========================================================
            // ADATTAMENTO SLIDER "S" IN "STEP RELAX" (MEMORIA SEPARATA)
            // ==========================================================
            ui->lblS->setText("Step Relax");
            ui->sSlider->setMinimum(0);

            // Protezione di sicurezza per la memoria implicita
            if (m_lastImplicitS <= 0.0) {
                m_lastImplicitS = 0.4;
            }

            // Allarghiamo dinamicamente lo slider se la memoria aveva un valore alto
            if (m_lastImplicitS > 1.0) {
                ui->sSlider->setMaximum(m_lastImplicitS * 100);
            } else {
                ui->sSlider->setMaximum(100);
            }

            // RIPRISTINA NELLA UI IL VALORE IMPLICITO SALVATO!
            ui->lineS->setText(QString::number(m_lastImplicitS));
            // Forza l'aggiornamento dello slider (e quindi della GPU)
            ui->sSlider->setValue(m_lastImplicitS * 100);
            // ==========================================================

            // --- SETUP MODALITA' RAY MARCHING (Originale) ---
            ui->glWidget->setEngineMode(GLWidget::ModeImplicit);

            ui->lblSteps->setText("Ray Steps=");

            ui->stepSlider->setValue(m_lastImplicitSteps);

            // Aggiornamento forzato manuale della UI ignorando il blocco segnali
            ui->lineSteps->setText(QString::number(m_lastImplicitSteps));
            ui->glWidget->setRaySteps(m_lastImplicitSteps);

            // Reset Limiti Spaziali per non tagliare la superficie di default
            ui->lineXMin->clear(); ui->lineXMax->clear();
            ui->lineYMin->clear(); ui->lineYMax->clear();
            ui->lineZMin->clear(); ui->lineZMax->clear();
            if (ui->glWidget) {
                ui->glWidget->setRangeX(-1000.0f, 1000.0f);
                ui->glWidget->setRangeY(-1000.0f, 1000.0f);
                ui->glWidget->setRangeZ(-1000.0f, 1000.0f);

                // FIX 3: Compilazione e invio della sfera alla GPU
                QString implicitEqF = "(x^2 + y^2 + z^2) - (1.0)";
                ui->glWidget->setImplicitEquation(implicitEqF);
                ui->glWidget->validateAndApplyImplicitShader(implicitEqF, "", "");
                ui->glWidget->rebuildShader();
            }
        }
        else { // --- PASSAGGIO A PARAMETRIC (TAB 0) ---
            m_lastImplicitSteps = ui->stepSlider->value();
            m_lastImplicitS = ui->lineS->text().toDouble();

            // 1. STOP E RESET FISICO
            onStopClicked();
            if (pathTimer->isActive()) onDepartureClicked();
            if (pathTimer3D->isActive()) onDeparture3DClicked();
            ui->glWidget->resetTransformations();
            ui->glWidget->resetTime();
            ui->glWidget->setSurfaceAnimating(false);
            if (m_btnStart) m_btnStart->setText("START");

            ui->lblNutVal->setText("0.00"); ui->lblPrecVal->setText("0.00"); ui->lblSpinVal->setText("0.00");
            ui->lblOmegaVal->setText("0.00"); ui->lblPhiVal->setText("0.00"); ui->lblPsiVal->setText("0.00");

            // 2. RESET ILLUMINAZIONE E RENDER MODE (Fix Bug persistenza)
            // Riportiamo tutto al modello "Basic" (Lambert) senza specolarità
            m_savedRenderMode = 0;
            if (ui->radioBasic) ui->radioBasic->setChecked(true);
            ui->glWidget->setSpecularEnabled(false); // Spegne Phong residuo

            // Spegniamo categoricamente l'illuminazione 4D (non usata nel reset RM)
            ui->glWidget->set4DLighting(false);
            m_lightingMode4D = 0;
            if (ui->btnLightMode) ui->btnLightMode->setText("Directional Lighting");
            ui->glWidget->setLightingMode4D(0);

            // 3. RESET SFONDO E LIMITI
            ui->glWidget->setBackgroundTextureEnabled(false);
            m_bgTextureCode = "";
            m_bgTextureScriptText = "";
            ui->chkBoxTexture->setText("Texture");
            ui->chkBoxTexture->setChecked(m_surfaceTextureState);

            ui->uMinEdit->setText("0");
            ui->uMaxEdit->setText("6.28318");
            ui->vMinEdit->setText("0");
            ui->vMaxEdit->setText("6.28318");
            ui->wMinEdit->setText("0");
            ui->wMaxEdit->setText("1");
            updateULimits(); updateVLimits(); updateWLimits();

            // 4. RIPRISTINO GEOMETRIA TORO
            ui->lineX->setPlainText("(0.8 + 0.3*cos(v))*cos(u)");
            ui->lineY->setPlainText("(0.8 + 0.3*cos(v))*sin(u)");
            ui->lineZ->setPlainText("0.3*sin(v)");
            ui->lineP->setPlainText("0.0");
            ui->glWidget->setParametricEquations(ui->lineX->toPlainText(), ui->lineY->toPlainText(),
                                                 ui->lineZ->toPlainText(), ui->lineP->toPlainText());

            // 5. CONFIGURAZIONE ENGINE PARAMETRICO
            ui->lblS->setText("s=");
            ui->sSlider->setMinimum(-1000);
            ui->sSlider->setMaximum(1000);
            ui->lineS->setText(QString::number(m_lastParametricS));
            ui->sSlider->setValue(m_lastParametricS * 100);

            ui->glWidget->setEngineMode(GLWidget::ModeParametric);
            ui->lblSteps->setText("Steps=");

            ui->stepSlider->setValue(m_lastParametricSteps);
            // FIX 4: Aggiornamento forzato manuale del testo
            ui->lineSteps->setText(QString::number(m_lastParametricSteps));
            ui->glWidget->setResolution(m_lastParametricSteps);

            ui->glWidget->updateSurfaceData();
            ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);

            onStopClicked();
        }

        updateRenderState();
        checkParametricDependency();
        ui->glWidget->update();
        updateScriptButtonText();
        ui->stepSlider->blockSignals(false);
    });

    ui->lineX->setPlainText("(0.8 + 0.3*cos(v))*cos(u)");
    ui->lineY->setPlainText("(0.8 + 0.3*cos(v))*sin(u)");
    ui->lineZ->setPlainText("0.3*sin(v)");
    ui->lineP->setPlainText("0.0");

    ui->glWidget->setParametricEquations(ui->lineX->toPlainText(), ui->lineY->toPlainText(), ui->lineZ->toPlainText(), ui->lineP->toPlainText());

    ui->uMinEdit->setText(QString::number(uMin, 'g', 12));
    ui->uMaxEdit->setText(QString::number(uMax, 'g', 12));
    ui->vMinEdit->setText(QString::number(vMin, 'g', 12));
    ui->vMaxEdit->setText(QString::number(vMax, 'g', 12));
    ui->wMinEdit->setText(QString::number(wMin, 'g', 12));
    ui->wMaxEdit->setText(QString::number(wMax, 'g', 12));
    ui->wMinEdit->setEnabled(false);
    ui->wMaxEdit->setEnabled(false);

    updateULimits();
    updateVLimits();

    ui->aSlider->setRange(0, 1000); ui->aSlider->setValue(100);
    ui->bSlider->setRange(0, 1000); ui->bSlider->setValue(100);
    ui->cSlider->setRange(0, 1000); ui->cSlider->setValue(100);
    ui->dSlider->setRange(0, 1000); ui->dSlider->setValue(100);
    ui->eSlider->setRange(0, 1000); ui->eSlider->setValue(100);
    ui->fSlider->setRange(0, 1000); ui->fSlider->setValue(100);
    ui->sSlider->setRange(-1000, 1000); ui->sSlider->setValue(0);

    // --- Inizializzazione Testi di Default ---
    if (ui->lineA->text().isEmpty()) ui->lineA->setText("1");
    if (ui->lineB->text().isEmpty()) ui->lineB->setText("1");
    if (ui->lineC->text().isEmpty()) ui->lineC->setText("1");
    if (ui->lineD->text().isEmpty()) ui->lineD->setText("1");
    if (ui->lineE->text().isEmpty()) ui->lineE->setText("1");
    if (ui->lineF->text().isEmpty()) ui->lineF->setText("1");
    if (ui->lineS->text().isEmpty() || ui->lineS->text() == "0.4") {
        ui->lineS->setText("0");
    }

    if (!m_meshDebounce) {
        m_meshDebounce = new QTimer(this);
        m_meshDebounce->setSingleShot(true);
        m_meshDebounce->setInterval(120);
        connect(m_meshDebounce, &QTimer::timeout, this, [this]() {
            if (ui->glWidget && ui->tabModeSelector->currentIndex() != 1) {
                ui->glWidget->setResolution(ui->stepSlider->value());
            }
            checkAndTriggerMeshUpdate();
        });
    }

    // --- MOTORE COSTANTI A CASCATA ---
    auto evaluateCascade = [this]() {
        bool okA=true, okB=true, okC=true, okD=true, okE=true, okF=true, okS=true;
        QString negativeConstName;

        auto resolveConst = [this, &negativeConstName](QLineEdit* edit, const QString& name, float raw, bool ok) -> float {
            if (ok && raw >= 0.0f) {
                m_lastValidConst[edit] = raw;
                return raw;
            }
            if (ok) { // raw < 0: ripristino + memorizza l'errore
                float prev = m_lastValidConst.value(edit, 1.0f);
                QSignalBlocker block(edit);   // niente editingFinished/valueChanged ricorsivi
                edit->setText(QString::number(prev, 'g', 6));
                if (negativeConstName.isEmpty()) negativeConstName = name;   // primo errore
                return prev;
            }
            return raw; // !ok
        };

        // Variabili intermedie per evitare l'ordine di valutazione non garantito di &okX
        float rawA = parseUIConstant(ui->lineA->text(), 0, 0, 0, 0, 0, 0, 0, &okA);
        float valA = resolveConst(ui->lineA, "A", rawA, okA);
        float rawB = parseUIConstant(ui->lineB->text(), valA, 0, 0, 0, 0, 0, 0, &okB);
        float valB = resolveConst(ui->lineB, "B", rawB, okB);
        float rawC = parseUIConstant(ui->lineC->text(), valA, valB, 0, 0, 0, 0, 0, &okC);
        float valC = resolveConst(ui->lineC, "C", rawC, okC);
        float rawD = parseUIConstant(ui->lineD->text(), valA, valB, valC, 0, 0, 0, 0, &okD);
        float valD = resolveConst(ui->lineD, "D", rawD, okD);
        float rawE = parseUIConstant(ui->lineE->text(), valA, valB, valC, valD, 0, 0, 0, &okE);
        float valE = resolveConst(ui->lineE, "E", rawE, okE);
        float rawF = parseUIConstant(ui->lineF->text(), valA, valB, valC, valD, valE, 0, 0, &okF);
        float valF = resolveConst(ui->lineF, "F", rawF, okF);

        // Tolto il vincolo std::max(0.0f) per S, così in parametrica accetta ancora i negativi
        float valS = parseUIConstant(ui->lineS->text(), valA, valB, valC, valD, valE, valF, 0, &okS);

        {
            struct Ck { bool ok; QLineEdit* edit; QString name; };
            const Ck checks[] = {
                {okA, ui->lineA, "A"}, {okB, ui->lineB, "B"}, {okC, ui->lineC, "C"},
                {okD, ui->lineD, "D"}, {okE, ui->lineE, "E"}, {okF, ui->lineF, "F"},
                {okS, ui->lineS, "S"},
            };
            for (const auto& c : checks) {
                if (!c.ok) {
                    if (!m_constantPopupActive) {
                        m_constantPopupActive = true;
                        InputValidator::showInvalidConstantError(this, c.name, c.edit->text());

                        // Ripristino focus senza ri-emettere editingFinished
                        {
                            QSignalBlocker blocker(c.edit);
                            c.edit->setFocus();
                            c.edit->selectAll();
                        }
                        // Reset rimandato: il flag resta true per tutto il resto
                        // del ciclo di eventi (compreso il setFocus in onStartClicked)
                        QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
                    }
                    return;
                }
            }
        }

        // ESPANSIONE DINAMICA: Nota l'aggiunta di '[this]' per leggere il Tab attuale
        auto setSmartSlider = [this](QSlider* s, float v, bool isS) {
            bool old = s->blockSignals(true);
            int intVal = static_cast<int>(v * 100.0f);

            int newMin;
            int newMax;

            // --- NUOVA LOGICA: Se siamo sul Tab 1 (Ray Marching) e stiamo aggiornando lo slider S ---
            if (isS && ui->tabModeSelector->currentIndex() == 1) {
                newMin = 0; // In Ray Marching lo slider parte rigorosamente da 0
                // Il massimo è 100 (1.0), ma se l'utente digita un numero enorme, si espande!
                newMax = std::max(100, intVal);

                // Forza anche il valore in modo che non scenda mai sotto lo 0
                if (intVal < 0) { intVal = 40; /* 0.4 di default */ }
            }
            // --- VECCHIA LOGICA: Parametrica o altri slider ---
            else {
                // Garantisce un limite standard di 10 (1000) o espande se il valore digitato è maggiore
                newMin = isS ? std::min(-1000, intVal) : 0;
                newMax = std::max(1000, intVal);
            }

            // Protezione "anti-collasso" se usi il mouse
            if (s->hasFocus() || s->isSliderDown() || s->underMouse()) {
                newMin = std::min(newMin, s->minimum());
                newMax = std::max(newMax, s->maximum());
            }

            s->setRange(newMin, newMax);
            s->setValue(intVal);
            s->blockSignals(old);
        };

        setSmartSlider(ui->aSlider, valA, false);
        setSmartSlider(ui->bSlider, valB, false);
        setSmartSlider(ui->cSlider, valC, false);
        setSmartSlider(ui->dSlider, valD, false);
        setSmartSlider(ui->eSlider, valE, false);
        setSmartSlider(ui->fSlider, valF, false);
        setSmartSlider(ui->sSlider, valS, true); // true = questo è lo slider S!

        if (ui->glWidget) {
            ui->glWidget->setEquationConstants(valA, valB, valC, valD, valE, valF, valS);
            m_meshDebounce->start();
        }

        if (!negativeConstName.isEmpty() && !m_constantPopupActive) {
            m_constantPopupActive = true;
            InputValidator::showNegativeConstantError(this, negativeConstName);
            QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
        }
    };

    auto connectSlider = [this, evaluateCascade](QSlider* slider, QLineEdit* line) {
        connect(slider, &QSlider::valueChanged, this, [line, evaluateCascade](int val) {
            if (!line->hasFocus()) {
                bool oldState = line->blockSignals(true);
                line->setText(QString::number(val / 100.0f, 'g', 6));
                line->blockSignals(oldState);
                evaluateCascade(); // Aggiorna le altre caselle che dipendono da questo!
            }
        });
    };

    connectSlider(ui->aSlider, ui->lineA); connectSlider(ui->bSlider, ui->lineB);
    connectSlider(ui->cSlider, ui->lineC); connectSlider(ui->dSlider, ui->lineD);
    connectSlider(ui->eSlider, ui->lineE); connectSlider(ui->fSlider, ui->lineF);
    connectSlider(ui->sSlider, ui->lineS);

    auto connectLineEdit = [this, evaluateCascade](QLineEdit* line) {
        connect(line, &QLineEdit::editingFinished, this, [this, evaluateCascade]() {
            evaluateCascade();                           // clamp + cascata + slider + push costanti
            if (m_meshDebounce) m_meshDebounce->stop();  // evita il doppio ridisegno asincrono
            commitFieldsOnEnter();                       // valida: se ok ridisegna, altrimenti vecchia immagine + popup
        });
    };

    connectLineEdit(ui->lineA); connectLineEdit(ui->lineB);
    connectLineEdit(ui->lineC); connectLineEdit(ui->lineD);
    connectLineEdit(ui->lineE); connectLineEdit(ui->lineF);
    connectLineEdit(ui->lineS);

    evaluateCascade();

    ui->stepSlider->setRange(10, 1000);
    int initialSteps = 100;
    ui->stepSlider->setValue(initialSteps);
    ui->lineSteps->setText(QString::number(initialSteps));
    ui->glWidget->setResolution(initialSteps);
    ui->lblSteps->setText(QString("Steps="));

    // (1) valueChanged: aggiorna testo + avvia debounce
    connect(ui->stepSlider, &QSlider::valueChanged, this, [this](int val) {
        ui->lineSteps->setText(QString::number(val));
        if (!ui->glWidget) return;
        if (ui->tabModeSelector->currentIndex() == 1) {
            ui->glWidget->setRaySteps(val);
            ui->glWidget->update();
        } else {
            m_meshDebounce->start();
        }
    });

    // (2) sliderPressed: sospende il rendering durante il trascinamento
    connect(ui->stepSlider, &QSlider::sliderPressed, this, [this]() {
        if (ui->glWidget) ui->glWidget->setUpdatesEnabled(false);
    });

    // (3) sliderReleased: riattiva il rendering e rigenera al rilascio
    connect(ui->stepSlider, &QSlider::sliderReleased, this, [this]() {
        if (ui->glWidget) {
            ui->glWidget->setUpdatesEnabled(true);
            ui->glWidget->update();
        }
        m_meshDebounce->start();
    });

    auto applyStepsFromLine = [this](bool notify) {
        const QString txt = ui->lineSteps->text().trimmed();
        if (txt.isEmpty()) return;   // vuoto durante la digitazione: ignora
        bool ok = false;
        int val = txt.toInt(&ok);
        if (!ok) {
            // notify=true solo al commit (Enter/uscita campo): niente popup mentre si digita
            if (!ok) {
                if (notify && !m_constantPopupActive) {
                    m_constantPopupActive = true;
                    InputValidator::showInvalidStepsError(this, txt);
                    bool oldL = ui->lineSteps->blockSignals(true);
                    ui->lineSteps->setText(QString::number(ui->stepSlider->value()));
                    ui->lineSteps->blockSignals(oldL);
                    ui->lineSteps->selectAll();
                    m_constantPopupActive = false;
                }
                return;
            }
        }
        val = std::clamp(val, ui->stepSlider->minimum(), ui->stepSlider->maximum());
        if (val != ui->stepSlider->value())
            ui->stepSlider->setValue(val);   // emette valueChanged -> aggiorna glWidget e testo
    };

    // Trigger "forti": al commit notifichiamo (notify=true)
    connect(ui->lineSteps, &QLineEdit::editingFinished, this, [applyStepsFromLine]() { applyStepsFromLine(true); });
    connect(ui->lineSteps, &QLineEdit::returnPressed,   this, [applyStepsFromLine]() { applyStepsFromLine(true); });

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_stepsDebounce = new QTimer(this);
    m_stepsDebounce->setSingleShot(true);
    m_stepsDebounce->setInterval(350);
    connect(m_stepsDebounce, &QTimer::timeout, this, [applyStepsFromLine]() { applyStepsFromLine(false); });

    connect(ui->lineSteps, &QLineEdit::textEdited, this, [this](const QString&) {
        m_stepsDebounce->start();
    });
#endif

    // 1. Dipendenze delle equazioni principali
    connect(ui->lineX, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineY, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineZ, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineP, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    auto markUserEdit = [this]() { this->setProperty("isPresetActive", false); };
    connect(ui->lineX, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineY, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineZ, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineP, &QPlainTextEdit::textChanged, this, markUserEdit);

    auto markTextureModified = [this]() { this->setProperty("isTextureModified", true); };
    connect(ui->txtScriptEditor, &QPlainTextEdit::textChanged, this, markTextureModified);
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, markTextureModified);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, markTextureModified);

    connect(ui->txtScriptEditor, &QPlainTextEdit::textChanged, this, [this](){
        updateScriptButtonText();
        updateConstantsUIState();
    });

    // 2. Mutua esclusione dei vincoli (con blocco segnali per evitare loop a catena!)
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, [this](){
        if(!ui->lineExplicitU->toPlainText().isEmpty()) {
            ui->lineExplicitV->blockSignals(true); ui->lineExplicitV->clear(); ui->lineExplicitV->blockSignals(false);
            ui->lineExplicitW->blockSignals(true); ui->lineExplicitW->clear(); ui->lineExplicitW->blockSignals(false);
        }
    });
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, [this](){
        if(!ui->lineExplicitV->toPlainText().isEmpty()) {
            ui->lineExplicitU->blockSignals(true); ui->lineExplicitU->clear(); ui->lineExplicitU->blockSignals(false);
            ui->lineExplicitW->blockSignals(true); ui->lineExplicitW->clear(); ui->lineExplicitW->blockSignals(false);
        }
    });
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, [this](){
        if(!ui->lineExplicitW->toPlainText().isEmpty()) {
            ui->lineExplicitU->blockSignals(true); ui->lineExplicitU->clear(); ui->lineExplicitU->blockSignals(false);
            ui->lineExplicitV->blockSignals(true); ui->lineExplicitV->clear(); ui->lineExplicitV->blockSignals(false);
        }
    });

    // 3. Dipendenze dei vincoli (chiamano checkParametricDependency, che a sua volta chiamerà updateConstraintState)
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    // 4. Dipendenze delle composizioni
    connect(ui->lineU, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineV, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineW, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    connect(ui->lineEquation, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);

    if (ui->lnU) {
        connect(ui->lnU,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lnV,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lnW,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndU,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndV,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndW,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lineConform, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

        connect(ui->lineConform, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnU, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnV, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnW, &QPlainTextEdit::textChanged, this, markUserEdit);

        connect(ui->lndU, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lndV, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lndW, &QPlainTextEdit::textChanged, this, markUserEdit);
    }

    checkParametricDependency();
    updateConstraintState();

    // ----------------------------------------------------
    // IMPLICIT MODE SETTINGS (Equazioni e Limiti Spaziali)
    // ---------------------------------------------------

    // --- 0. Impostazioni di Default UI ---
    ui->radioShell->setChecked(true);      // Attiva "Shell" di default
    ui->glWidget->setRenderMode(1);        // Diciamo subito al motore che siamo in modalità Shell (1)

    // --- Connessione dei Radio Button (Solid/Shell) ---
    auto updateImplicitRenderMode = [this]() {
        if (ui->glWidget) {
            // Se radioShell è attivo manda 1, altrimenti manda 0 (Solid)
            int mode = ui->radioShell->isChecked() ? 1 : 0;
            ui->glWidget->setRenderMode(mode);
        }
    };
    connect(ui->radioShell, &QRadioButton::toggled, this, updateImplicitRenderMode);
    connect(ui->radioSolid, &QRadioButton::toggled, this, updateImplicitRenderMode);


    // --- 1. Equazioni Implicite di Default (Solo 3D) ---
    ui->lineEquation->setPlainText("x^2 + y^2 + z^2 = 1.0");

    auto updateImplicitEquations = [this]() {
        if(ui->glWidget) {
            QString rawEq = ui->lineEquation->toPlainText().trimmed();
            QString implicitEqF;

            // Formatttiamo l'equazione rimuovendo l'uguale per renderla digeribile da GLSL
            if (rawEq.contains("=")) {
                QStringList parts = rawEq.split("=");
                if (parts.size() == 2) {
                    implicitEqF = QString("(%1) - (%2)").arg(parts[0].trimmed(), parts[1].trimmed());
                }
            } else {
                implicitEqF = QString("(%1) - (0.0)").arg(rawEq);
            }

            ui->glWidget->setImplicitEquation(implicitEqF);
        }
    };

    updateImplicitEquations();


    // --- 2. Limiti Spaziali (Facoltativi) ---
    // Partiamo con le caselle vuote = Nessun taglio applicato
    ui->lineXMin->clear(); ui->lineXMax->clear();
    ui->lineYMin->clear(); ui->lineYMax->clear();
    ui->lineZMin->clear(); ui->lineZMax->clear();

    auto connectSpaceLimit = [this](QLineEdit* minEdit, QLineEdit* maxEdit,
            void (GLWidget::*setLimitFunc)(float, float),
            const QString& axis) {
        auto applyLimits = [this, minEdit, maxEdit, setLimitFunc, axis](bool notify) {
            // helper: campo vuoto -> usa il default (nessun taglio); non valido -> popup + stop
            auto readField = [this, notify, axis](QLineEdit* edit, float def, const QString& side, bool* good) -> float {
                *good = true;
                if (edit->text().trimmed().isEmpty()) return def;   // vuoto = nessun limite
                bool ok = false;
                float v = parseMath(edit->text(), &ok);
                if (!ok) {
                    *good = false;
                    if (notify && !m_constantPopupActive) {
                        m_constantPopupActive = true;
                        InputValidator::showInvalidLimitError(this, axis + " " + side, edit->text());
                        edit->setFocus();
                        edit->selectAll();
                        m_constantPopupActive = false;
                    }
                    return def;
                }
                return v;
            };

            bool okMin = true, okMax = true;
            float minVal = readField(minEdit, -1000.0f, "min", &okMin);
            if (!okMin) return;                       // non applichiamo limiti non validi
            float maxVal = readField(maxEdit,  1000.0f, "max", &okMax);
            if (!okMax) return;

            if (minVal >= maxVal) return;             // range impossibile: non applicare

            if (ui->glWidget) {
                (ui->glWidget->*setLimitFunc)(minVal, maxVal);
            }
        };

        connect(minEdit, &QLineEdit::editingFinished, this, [applyLimits]() { applyLimits(true); });
        connect(maxEdit, &QLineEdit::editingFinished, this, [applyLimits]() { applyLimits(true); });

        applyLimits(false);   // chiamata iniziale silenziosa
    };

    connectSpaceLimit(ui->lineXMin, ui->lineXMax, &GLWidget::setRangeX, "X");
    connectSpaceLimit(ui->lineYMin, ui->lineYMax, &GLWidget::setRangeY, "Y");
    connectSpaceLimit(ui->lineZMin, ui->lineZMax, &GLWidget::setRangeZ, "Z");

    auto connectParametricLimit = [this](QLineEdit* minEdit, QLineEdit* maxEdit,
            bool (MainWindow::*updateFunc)()) {
        auto apply = [this, updateFunc]() {
            if ((this->*updateFunc)()) {                   // applica solo se min < max
                if (m_meshDebounce) m_meshDebounce->start();
            }
        };
        // editingFinished: copre Enter su desktop e perdita di focus su mobile.
        connect(minEdit, &QLineEdit::editingFinished, this, apply);
        connect(maxEdit, &QLineEdit::editingFinished, this, apply);
        // returnPressed: trigger "forte" su desktop, innocuo su mobile (evento consumato dal filtro).
        connect(minEdit, &QLineEdit::returnPressed,   this, apply);
        connect(maxEdit, &QLineEdit::returnPressed,   this, apply);
    };

    connectParametricLimit(ui->uMinEdit, ui->uMaxEdit, &MainWindow::updateULimits);
    connectParametricLimit(ui->vMinEdit, ui->vMaxEdit, &MainWindow::updateVLimits);
    connectParametricLimit(ui->wMinEdit, ui->wMaxEdit, &MainWindow::updateWLimits);

    connect(ui->btnTextureCode, &QPushButton::clicked, this, [this]() {
        onRunRaymarchTextureClicked();
    });

    connect(ui->btnSave, &QPushButton::clicked, this, &MainWindow::onSaveTextureClicked);

    // =========================================================================
    // 7. RENDERER, COLORS & LIGHTING
    // =========================================================================
    ui->glWidget->setProjectionMode(1);
    updateProjectionButtonText();

    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->addButton(ui->radioBasic, 0);
    m_modeGroup->addButton(ui->radioPhong, 1);
    m_modeGroup->addButton(ui->radioWF,    2);
    m_modeGroup->addButton(ui->radioBackground, 3);
    m_modeGroup->setExclusive(true);

    connect(m_modeGroup, &QButtonGroup::idPressed, this, [this](int id){
        if (id == 3 && !ui->radioBackground->isChecked()) {
            // SALVATAGGIO: L'utente sta per entrare nella modalità Background.
            // Salviamo lo stato della checkbox (che ora appartiene alla superficie).
            if (m_savedRenderMode != 2) {
                m_surfaceTextureState = ui->chkBoxTexture->isChecked();
            }
        }
        else if (id != 3 && ui->radioBackground->isChecked()) {
            // RIPRISTINO: L'utente sta cliccando su un altro radio button per uscire.
            // Ripristiniamo la checkbox al valore salvato per la superficie.
            bool oldBlock = ui->chkBoxTexture->blockSignals(true);
            ui->chkBoxTexture->setChecked(m_surfaceTextureState);
            ui->chkBoxTexture->blockSignals(oldBlock);
        }
    });

    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id){
        if (ui->glWidget) ui->glWidget->setFlatViewTarget(id == 3 ? 1 : 0);

        if (id == 3) {
            if (m_currentScriptMode == ScriptModeTexture) {
                m_surfaceTextureScriptText = ui->txtScriptEditor->toPlainText();
                bool oldBlock = ui->txtScriptEditor->blockSignals(true);
                ui->txtScriptEditor->setPlainText(m_bgTextureScriptText);
                ui->txtScriptEditor->blockSignals(oldBlock);
                ui->btnRunCurrentScript->setText("Run Background Texture");
                updateScriptButtonText();
            }

            bool bgTexActive = ui->glWidget->isBackgroundTextureEnabled();
            ui->chkBoxTexture->setText("Background Texture");
            ui->chkBoxTexture->setEnabled(true);

            ui->radioEditSurf->setEnabled(false);
            ui->radioEditBorder->setEnabled(false);

            ui->radioTexColor1->setEnabled(bgTexActive);
            ui->radioTexColor2->setEnabled(bgTexActive);

            if (bgTexActive) {
                bool oldBlock2 = ui->radioTexColor1->blockSignals(true);
                ui->radioTexColor1->setChecked(true);
                ui->radioTexColor1->blockSignals(oldBlock2);
            }

            bool oldBlock = ui->chkBoxTexture->blockSignals(true);
            ui->chkBoxTexture->setChecked(bgTexActive);
            ui->chkBoxTexture->blockSignals(oldBlock);

            onColorTargetChanged();
        }
        else {
            m_savedRenderMode = id;

            if (m_currentScriptMode == ScriptModeTexture) {
                m_bgTextureScriptText = ui->txtScriptEditor->toPlainText();
                bool oldBlock = ui->txtScriptEditor->blockSignals(true);
                ui->txtScriptEditor->setPlainText(m_surfaceTextureScriptText);
                ui->txtScriptEditor->blockSignals(oldBlock);
                ui->btnRunCurrentScript->setText("Run Surface Texture");
                updateScriptButtonText();
            }

            ui->radioEditSurf->setEnabled(true);
            ui->radioEditBorder->setEnabled(true);

            ui->chkBoxTexture->setText("Texture");
            bool oldBlock = ui->chkBoxTexture->blockSignals(true);
            if (id == 2) {
                ui->chkBoxTexture->setChecked(false);
                ui->chkBoxTexture->setEnabled(false);
            } else {
                ui->chkBoxTexture->setEnabled(true);
                ui->chkBoxTexture->setChecked(m_surfaceTextureState);
            }
            ui->chkBoxTexture->blockSignals(oldBlock);

            updateTextureUIState(m_surfaceTextureState);
            ui->glWidget->setTextureEnabled(m_surfaceTextureState && (id != 2));
            onColorTargetChanged();
        }
        updateFlatPreviewButton();
        updateRenderState();

        // SINCRONIZZA L'ALBERO TEXTURE AL CAMBIO MODALITÀ
        ui->treeTextures->clearSelection();
        QTreeWidgetItemIterator itTex(ui->treeTextures);

        QString activeCode;
        if (ui->radioBackground->isChecked()) {
            activeCode = m_bgTextureCode;
        } else {
            if (ui->tabModeSelector->currentIndex() == 1) {
                activeCode = ui->lineTexture->toPlainText();
            } else {
                activeCode = m_surfaceTextureCode;
            }
        }

        QString cleanedActive =  cleanCodeForComparison(activeCode);

        // Usiamo activeCode.trimmed()! Così se è un'immagine entra comunque nel ciclo.
        if (!activeCode.trimmed().isEmpty()) {
            while (*itTex) {
                QVariant vTex = (*itTex)->data(0, Qt::UserRole + 1);
                if (vTex.isValid()) {
                    int idx = vTex.toInt();
                    const LibraryItem &texItem = m_libraryManager.getTexture(idx);
                    bool isMatch = false;

                    if (texItem.isImage) {
                        // MATCH ROBUSTO PER IMMAGINI (Usa il codice NON pulito)
                        QString fileName = QFileInfo(texItem.filePath).fileName();
                        if (!fileName.isEmpty() && activeCode.contains(fileName)) {
                            isMatch = true;
                        }
                    } else {
                        // MATCH PROCEDURALE (Usa i codici PULITI)
                        QString cleanLibCode =  cleanCodeForComparison(texItem.scriptCode);
                        if (!cleanedActive.isEmpty() && cleanedActive == cleanLibCode) {
                            isMatch = true;
                        }
                    }

                    // IL TUO ULTIMO IF (Intatto e funzionante!)
                    if (isMatch) {
                        (*itTex)->setSelected(true);
                        ui->treeTextures->setCurrentItem(*itTex);
                        QTreeWidgetItem* parent = (*itTex)->parent();
                        while(parent) { parent->setExpanded(true); parent = parent->parent(); }
                        ui->treeTextures->scrollToItem(*itTex);
                        break;
                    }
                }
                ++itTex;
            }
        }

        updateScriptButtonText();
    });

    ui->radioBasic->setChecked(true);

    connect(ui->radioBasic, &QRadioButton::toggled, this, &MainWindow::updateRenderState);
    connect(ui->radioPhong, &QRadioButton::toggled, this, &MainWindow::updateRenderState);
    connect(ui->radioWF,    &QRadioButton::toggled, this, &MainWindow::updateRenderState);

    connect(ui->radioWF, &QRadioButton::toggled, this, [this](bool checked){
        if (checked) {
            ui->glWidget->setRenderMode(2);
            ui->chkBoxTexture->setEnabled(false);
        }
    });

    ui->alphaSlider->setRange(0, 100);
    ui->alphaSlider->setValue(100);
    alphaValue = 1.0f;
    ui->glWidget->setAlpha(alphaValue);
    ui->lblAlphaVal->setText("1.00");

    connect(ui->alphaSlider, &QSlider::valueChanged, this, [this](int value){
        alphaValue = static_cast<float>(value) / 100.0f;
        ui->lblAlphaVal->setText(QString::number(alphaValue, 'f', 2));
        ui->glWidget->setAlpha(alphaValue);
    });

    connect(ui->chkBoxTexture, &QCheckBox::toggled, this, [this](bool checked){
        if (ui->radioBackground->isChecked()) {
            ui->glWidget->setBackgroundTextureEnabled(checked);
            ui->radioTexColor1->setEnabled(checked);
            ui->radioTexColor2->setEnabled(checked);

            if (checked && !ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked()) {
                bool oldBlock = ui->radioTexColor1->blockSignals(true);
                ui->radioTexColor1->setChecked(true);
                ui->radioTexColor1->blockSignals(oldBlock);
            }

            if (!checked) {
                m_bgTextureCode.clear();
                m_bgTextureScriptText.clear();

                // Se stiamo visualizzando il dock degli script in modalità Texture
                if (m_currentScriptMode == ScriptModeTexture) {
                    bool oldBlockTxt = ui->txtScriptEditor->blockSignals(true);
                    ui->txtScriptEditor->clear();
                    ui->txtScriptEditor->blockSignals(oldBlockTxt);

                    // Disattiva coerentemente i tasti
                    ui->btnSaveScript->setEnabled(false);
                    ui->btnRunCurrentScript->setEnabled(false);
                }
            }

            onColorTargetChanged();
        }
        else {
            // CANCELLAZIONE SCRIPTS CON WARNING (SURFACE TEXTURE)
            if (!checked && !m_blockTextureGen) {
                // 1. Verifichiamo se c'è effettivamente del codice che andrebbe perso
                bool hasCode = false;
                bool isModified = this->property("isTextureModified").toBool();

                if (ui->tabModeSelector->currentIndex() == 1) { // Ray Marching
                    QString tex = ui->lineTexture->toPlainText().trimmed();
                    QString disp = ui->lineVariations->toPlainText().trimmed();

                    // Ignoriamo i Triplanar Mapping standard generati automaticamente
                    bool isAutoTriplanar = tex.contains("vec3 blend = abs(n_model);");
                    if (!tex.isEmpty() && !isAutoTriplanar) hasCode = true;
                    if (!disp.isEmpty()) hasCode = true;

                } else { // Parametrica
                    QString tex = m_surfaceTextureCode.trimmed();

                    // Se c'è SOLO un tag immagine (es. //IMG:/percorso.png) senza logica a capo, ignoralo
                    bool isOnlyImage = tex.startsWith("//IMG:") && !tex.contains("\n");
                    if (!tex.isEmpty() && !isOnlyImage) hasCode = true;
                }

                // MOSTRA IL WARNING SOLO SE C'È VERO CODICE *E* L'UTENTE LO HA MODIFICATO MANUALMENTE
                if (hasCode && isModified) {
                    auto reply = QMessageBox::warning(this, "Warning",
                                                      "If not saved, disabling the texture will permanently clear your current scripts.\n"
                                                      "Any custom code entered may be lost. Do you want to proceed?",
                                                      QMessageBox::Yes | QMessageBox::No);

                    if (reply == QMessageBox::No) {
                        // Se l'utente clicca NO, ripristiniamo il check senza scatenare loop
                        bool oldBlock = ui->chkBoxTexture->blockSignals(true);
                        ui->chkBoxTexture->setChecked(true);
                        ui->chkBoxTexture->blockSignals(oldBlock);
                        return; // Interrompe l'operazione
                    }

                    // 2. Se l'utente clicca SI, cancelliamo fisicamente tutto
                    if (ui->tabModeSelector->currentIndex() == 1) {
                        // Svuota i campi della tab Ray Marching
                        ui->lineTexture->clear();
                        ui->lineVariations->clear();
                        if (ui->glWidget) {
                            ui->glWidget->setTextureCode("");
                            ui->glWidget->setDisplacementCode("");
                        }
                    } else {
                        // Svuota memoria e variabili parametriche
                        m_surfaceTextureCode.clear();
                        m_surfaceTextureScriptText.clear();
                        m_isCustomMode = false;
                        m_isImageMode = false;
                        m_currentTexturePath.clear();

                        // Se l'utente sta visualizzando il dock script in modalità texture, svuota l'editor
                        if (m_currentScriptMode == ScriptModeTexture) {
                            ui->txtScriptEditor->clear();
                        }

                        if (ui->glWidget) {
                            ui->glWidget->loadCustomShader(""); // Torna allo shader standard
                            ui->glWidget->clearTexture();
                        }
                    }
                }
            }

            m_surfaceTextureState = checked;
            updateTextureUIState(checked);
            if (ui->glWidget) ui->glWidget->setTextureEnabled(checked);

            if (!m_blockTextureGen && checked) {
                // --- LOGICA RAY MARCHING (Tab 1) ---
                if (ui->tabModeSelector->currentIndex() == 1) {
                    QString currentTex = ui->lineTexture->toPlainText().trimmed();

                    if (currentTex.isEmpty()) {
                        QString targetImg = ":/defaultray.png";

                        // Codice Triplanar Mapping automatico
                        QString defaultRM = "//IMG:" + targetImg + "\n"
                                                                   "vec3 blend = abs(n_model);\n"
                                                                   "blend /= max(blend.x + blend.y + blend.z, 0.00001);\n"
                                                                   "float scale = 0.5;\n"
                                                                   "vec3 cX = texture(tex, pModel.yz * scale).rgb;\n"
                                                                   "vec3 cY = texture(tex, pModel.xz * scale).rgb;\n"
                                                                   "vec3 cZ = texture(tex, pModel.xy * scale).rgb;\n"
                                                                   "textureCol = cX * blend.x + cY * blend.y + cZ * blend.z;";

                        ui->lineTexture->setPlainText(defaultRM);
                        if (ui->glWidget) ui->glWidget->setTextureCode(defaultRM);

                        // Carichiamo la texture dalla risorsa interna
                        if (QFile::exists(targetImg)) {
                            ui->glWidget->loadTextureFromFile(targetImg);
                            m_isImageMode = true;
                            m_currentTexturePath = targetImg;
                        } else {
                            generateTexture();
                        }
                    }
                }
                // --- LOGICA PARAMETRICA (Tab 0) ---
                else {
                    if (m_isCustomMode) m_isCustomMode = false;
                    m_isImageMode = false;

                    if (ui->glWidget) ui->glWidget->setTextureColors(m_texColor1, m_texColor2);

                    bool oldBlock = ui->radioTexColor1->blockSignals(true);
                    ui->radioTexColor1->setChecked(true);
                    ui->radioTexColor1->blockSignals(oldBlock);
                    onColorTargetChanged();

                    generateTexture();
                }

                if (ui->glWidget) ui->glWidget->rebuildShader();
            }
        }

        updateFlatPreviewButton();

        // --- GESTIONE AUTOMATICA ANIMAZIONE (START/STOP) SICURA ---
        bool needsAnim = false;

        // 1. Controllo Equazioni Base
        if (ui->tabModeSelector->currentIndex() == 1) { // Ray Marching
            QString eq = ui->lineEquation->toPlainText() + " " + ui->lineVariations->toPlainText() + " " + m_surfaceScriptText;
            if (hasTimeVariable(eq)) needsAnim = true;
        } else { // Parametrica
            QString eq = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " + ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText();
            if (hasTimeVariable(eq)) needsAnim = true;
        }

        // 2. Controllo Texture Superficie (SOLO SE ABILITATA)
        bool isSurfTexActive = ui->radioBackground->isChecked() ? m_surfaceTextureState : checked;
        if (isSurfTexActive) {
            QString tex = (ui->tabModeSelector->currentIndex() == 1)
                    ? (ui->lineTexture->toPlainText() + ui->lineVariations->toPlainText())
                    : m_surfaceTextureCode;
            if (hasTimeVariable(tex)) needsAnim = true;
        }

        // 3. Controllo Texture Sfondo (SOLO SE ABILITATA)
        if (ui->glWidget && ui->glWidget->isBackgroundTextureEnabled()) {
            if (hasTimeVariable(m_bgTextureCode)) needsAnim = true;
        }

        // 4. APPLICAZIONE STATO E AGGIORNAMENTO UI
        if (needsAnim) {
            if (m_btnStart && m_btnStart->text() != "START") {
                applyAnimationState(true);
            }
        } else {
            applyAnimationState(false);
        }

        if (ui->glWidget) ui->glWidget->update();
    });

    connect(ui->btnWireUPlus,  &QPushButton::clicked, this, [this](){ ui->glWidget->increaseWireframeUDensity(); });
    connect(ui->btnWireVPlus,  &QPushButton::clicked, this, [this](){ ui->glWidget->increaseWireframeVDensity(); });
    connect(ui->btnWireUMinus, &QPushButton::clicked, this, [this](){ ui->glWidget->decreaseWireframeUDensity(); });
    connect(ui->btnWireVMinus, &QPushButton::clicked, this, [this](){ ui->glWidget->decreaseWireframeVDensity(); });

    // Colori Default
    float defR = 0.20f, defG = 0.80f, defB = 0.20f;
    m_currentSurfaceColor = QColor::fromRgbF(defR, defG, defB);

    // Imposti il colore del bordo come desideri (es. Giallo Oro)
    m_currentBorderColor = QColor::fromRgbF(1.0f, 0.85f, 0.0f);

    m_texColor1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
    m_texColor2 = Qt::black;

    if (ui->glWidget) {
        // Colore superficie (Verde)
        ui->glWidget->setColor(defR, defG, defB);

        // Colore bordo (legge i canali RGB da m_currentBorderColor)
        ui->glWidget->setBorderColor(m_currentBorderColor.redF(),
                                     m_currentBorderColor.greenF(),
                                     m_currentBorderColor.blueF());
    }

    ui->sliderR->setRange(0, 255);
    ui->sliderG->setRange(0, 255);
    ui->sliderB->setRange(0, 255);
    ui->lightSlider->setRange(0, 200); ui->lightSlider->setValue(100);
    ui->lblValLight->setText(QString::number(ui->lightSlider->value()) + " %");
    ui->speed3DSlider->setRange(1, 100); ui->speed3DSlider->setValue(10);
    ui->speed4DSlider->setRange(1, 100); ui->speed4DSlider->setValue(10);

    UiStyleManager::setupBigSliders(ui->sliderR, ui->sliderG, ui->sliderB, ui->alphaSlider, ui->lightSlider, ui->speed3DSlider, ui->speed4DSlider);

    m_colorGroup = new QButtonGroup(this);
    m_colorGroup->addButton(ui->radioEditSurf);
    m_colorGroup->addButton(ui->radioEditBorder);
    m_colorGroup->addButton(ui->radioTexColor1);
    m_colorGroup->addButton(ui->radioTexColor2);
    m_colorGroup->setExclusive(true);

    m_currentBackgroundColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
    ui->glWidget->setBackgroundColor(m_currentBackgroundColor);

    connect(ui->radioBackground, &QRadioButton::toggled, this, &MainWindow::onColorTargetChanged);

    ui->btnBorder->setChecked(false);
    ui->btnBorder->setText("Border OFF");
    ui->radioEditBorder->setEnabled(false);

    connect(ui->btnBorder, &QPushButton::toggled, this, [this](bool checked){
        ui->glWidget->setShowBorders(checked);
        ui->btnBorder->setText(checked ? "Border ON" : "Border OFF");

        // --- DELEGA DELLO STILE AL MANAGER ---
        UiStyleManager::applyActiveToggleStyle(ui->btnBorder, checked);

        if(checked) {
            if (m_currentBorderColor == m_currentSurfaceColor) {
                m_currentBorderColor = QColor::fromRgbF(1.0f, 0.0f, 0.0f);
                ui->glWidget->setBorderColor(m_currentBorderColor.redF(), m_currentBorderColor.greenF(), m_currentBorderColor.blueF());
            }

            // --- SPOSTAMENTO AUTOMATICO DEL FOCUS ---
            ui->radioEditBorder->setEnabled(true);
            ui->radioEditBorder->setChecked(true);
            onColorTargetChanged();
        }
        else {
            ui->radioEditBorder->setEnabled(false);

            // Se stavamo modificando il bordo e lo spegniamo, torniamo alla superficie
            if (ui->radioEditBorder->isChecked()) {
                ui->radioEditSurf->setChecked(true);
                onColorTargetChanged();
            }
        }
    });

    auto handleColorChange = [this]() {
        int r = ui->sliderR->value(); int g = ui->sliderG->value(); int b = ui->sliderB->value();
        ui->valR->setNum(r); ui->valG->setNum(g); ui->valB->setNum(b);
        QColor newColor(r, g, b);

        if (ui->radioBackground->isChecked()) {
            if (ui->chkBoxTexture->isChecked()) {
                if (ui->radioTexColor1->isChecked()) m_bgTexColor1 = newColor;
                else m_bgTexColor2 = newColor;

                ui->glWidget->setProperty("bg_col1", QVector3D(m_bgTexColor1.redF(), m_bgTexColor1.greenF(), m_bgTexColor1.blueF()));
                ui->glWidget->setProperty("bg_col2", QVector3D(m_bgTexColor2.redF(), m_bgTexColor2.greenF(), m_bgTexColor2.blueF()));
                ui->glWidget->update();
            } else {
                m_currentBackgroundColor = newColor;
                ui->glWidget->setBackgroundColor(m_currentBackgroundColor);
                ui->glWidget->update();
            }
        }
        else if (ui->radioEditSurf->isChecked()) {
            m_currentSurfaceColor = newColor;
            ui->glWidget->setColor(r/255.0f, g/255.0f, b/255.0f);
        }
        else if (ui->radioEditBorder->isChecked()) {
            m_currentBorderColor = newColor;
            ui->glWidget->setBorderColor(r/255.0f, g/255.0f, b/255.0f);
        }
        else {
            bool isTex1 = ui->radioTexColor1->isChecked();
            if (isTex1) m_texColor1 = newColor; else m_texColor2 = newColor;
            ui->glWidget->setTextureColors(m_texColor1, m_texColor2);
            if (!m_isCustomMode && !m_isImageMode) scheduleTextureGeneration();
        }
    };

    ui->sliderR->disconnect(); ui->sliderG->disconnect(); ui->sliderB->disconnect();
    connect(ui->sliderR, &QSlider::valueChanged, this, handleColorChange);
    connect(ui->sliderG, &QSlider::valueChanged, this, handleColorChange);
    connect(ui->sliderB, &QSlider::valueChanged, this, handleColorChange);

    connect(ui->lightSlider, &QSlider::valueChanged, this, [this](int val){
        float intensity = val / 100.0f;
        ui->glWidget->setLightIntensity(intensity);
        ui->lblValLight->setText(QString::number(val) + " %");
    });

    connect(m_colorGroup, &QButtonGroup::buttonClicked, this, [this](){ onColorTargetChanged(); });

    ui->radioEditSurf->setChecked(true);

    // =========================================================================
    // 8. MOTION, PATHS & NAVIGATION
    // =========================================================================
    float omega = 0.0f, phi = 0.0f, psi = 0.0f;
    ui->lblNutVal->setText("0"); ui->lblPrecVal->setText("0"); ui->lblSpinVal->setText("0");
    ui->lblOmegaVal->setText(QString::number(omega)); ui->lblPhiVal->setText(QString::number(phi)); ui->lblPsiVal->setText(QString::number(psi));

    ui->glWidget->setRotation4D(omega, phi, psi);
    ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
    ui->glWidget->setNutationSpeed(0.0f); ui->glWidget->setPrecessionSpeed(0.0f); ui->glWidget->setSpinSpeed(0.0f);
    ui->glWidget->setOmegaSpeed(0.0f); ui->glWidget->setPhiSpeed(0.0f); ui->glWidget->setPsiSpeed(0.0f);

    ui->btnStart_2->setText("GO");

    m_lightingMode4D = 0;
    ui->glWidget->setLightingMode4D(0);
    ui->btnLightMode->setText("Directional Lighting");

    connect(ui->btnLightMode, &QPushButton::clicked, this, [this](){
        QString xEq = ui->lineX->toPlainText().trimmed(); QString yEq = ui->lineY->toPlainText().trimmed();
        QString zEq = ui->lineZ->toPlainText().trimmed(); QString pEq = ui->lineP->toPlainText().trimmed();

        auto isNullCoord = [](const QString &s) { return s.isEmpty() || s == "0" || s == "0.0"; };

        bool isDegenerate4D = isNullCoord(xEq) || isNullCoord(yEq) || isNullCoord(zEq) || isNullCoord(pEq);
        int numModes = (!isDegenerate4D) ? 3 : 2;
        m_lightingMode4D = (m_lightingMode4D + 1) % numModes;

        switch (m_lightingMode4D) {
        case 0: ui->glWidget->setLightingMode4D(0); ui->btnLightMode->setText("Directional Lighting"); break;
        case 1: ui->glWidget->setLightingMode4D(1); ui->btnLightMode->setText("Observer Lighting"); break;
        case 2: ui->glWidget->setLightingMode4D(2); ui->btnLightMode->setText("Slice Lighting"); break;
        }
    });

    navTimer = new QTimer(this); navTimer->setInterval(30);
    connect(navTimer, &QTimer::timeout, this, &MainWindow::onNavTimerTick);

    pathTimer = new QTimer(this); pathTimer->setInterval(30);
    connect(pathTimer, &QTimer::timeout, this, &MainWindow::onPathTimerTick);

    pathTimer3D = new QTimer(this); pathTimer3D->setInterval(30);
    connect(pathTimer3D, &QTimer::timeout, this, &MainWindow::onPath3DTimerTick);

    ui->btnDeparture->setEnabled(false); connect(ui->btnDeparture, &QPushButton::clicked, this, &MainWindow::onDepartureClicked);
    ui->btnDeparture3D->setEnabled(false); connect(ui->btnDeparture3D, &QPushButton::clicked, this, &MainWindow::onDeparture3DClicked);

    m_pathMode = ModeTangential;
    ui->pushView->setText("Tangent View"); connect(ui->pushView, &QPushButton::clicked, this, &MainWindow::onToggleViewClicked);
    ui->pushView3D->setText("Tangent View"); connect(ui->pushView3D, &QPushButton::clicked, this, &MainWindow::onToggleViewClicked);

    connect(ui->lineX_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineY_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineZ_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineP_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);

    connect(ui->lineAlpha_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineBeta_P,  &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineGamma_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);

    connect(ui->lineX_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    connect(ui->lineY_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    connect(ui->lineZ_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    connect(ui->lineR_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);

    connect(ui->speed3DSlider, &QSlider::valueChanged, this, [this](int val){ m_pathSpeed3D = val / 1000.0f; });
    connect(ui->speed4DSlider, &QSlider::valueChanged, this, [this](int val){ m_pathSpeed4D = val / 1000.0f; });

    connectNavButton(ui->btnForward, GLWidget::MoveForward); connectNavButton(ui->btnBackward, GLWidget::MoveBack);
    connectNavButton(ui->btnLeft, GLWidget::MoveLeft); connectNavButton(ui->btnRight, GLWidget::MoveRight);
    connectNavButton(ui->btnDown, GLWidget::MoveDown); connectNavButton(ui->btnUp, GLWidget::MoveUp);
    connectNavButton(ui->btnRollLeft, GLWidget::RollLeft); connectNavButton(ui->btnRollRight, GLWidget::RollRight);
    connectNavButton(ui->btnXPlus,  GLWidget::ObsMoveXPos); connectNavButton(ui->btnXMinus, GLWidget::ObsMoveXNeg);
    connectNavButton(ui->btnYPlus,  GLWidget::ObsMoveYPos); connectNavButton(ui->btnYMinus, GLWidget::ObsMoveYNeg);
    connectNavButton(ui->btnZPlus,  GLWidget::ObsMoveZPos); connectNavButton(ui->btnZMinus, GLWidget::ObsMoveZNeg);
    connectNavButton(ui->btnPPlus,  GLWidget::ObsMovePPos); connectNavButton(ui->btnPMinus, GLWidget::ObsMovePNeg);
    connectNavButton(ui->btnOmegaAhead, GLWidget::RotOmegaPos); connectNavButton(ui->btnOmegaRear,  GLWidget::RotOmegaNeg);
    connectNavButton(ui->btnPhiRear, GLWidget::RotPhiNeg); connectNavButton(ui->btnPhiAhead,  GLWidget::RotPhiPos);
    connectNavButton(ui->btnPsiAhead,   GLWidget::RotPsiPos); connectNavButton(ui->btnPsiRear,    GLWidget::RotPsiNeg);

    // =========================================================================
    // 9. SCRIPTING & TEXTURE DOCK
    // =========================================================================
    connect(ui->btnScriptMode, &QPushButton::clicked, this, &MainWindow::onToggleScriptMode);
    connect(ui->btnRunCurrentScript, &QPushButton::clicked, this, &MainWindow::onRunCurrentScript);
    connect(ui->btnSaveScript, &QPushButton::clicked, this, &MainWindow::onSaveScriptClicked);

    m_currentScriptMode = ScriptModeSurface;
    updateScriptButtonText();

    ui->btnFlatPreview->setText("2D View");
    ui->btnFlatPreview->setEnabled(false);

    connect(ui->btnFlatPreview, &QPushButton::toggled, this, [this](bool checked){
        if (checked) {
            ui->btnFlatPreview->setText("3D View");
            ui->alphaSlider->setEnabled(false); // Blocchiamo solo la trasparenza
        } else {
            updateFlatPreviewButton();
            ui->alphaSlider->setEnabled(true);
        }

        if (ui->radioBackground->isChecked()) ui->glWidget->setFlatViewTarget(1);
        else ui->glWidget->setFlatViewTarget(0);

        ui->glWidget->setFlatView(checked);
        ui->glWidget->update();
    });

    updateFlatPreviewButton();

    ui->txtScriptEditor->setPlaceholderText("Write GLSL code for custom texture.\nExample: return vec4(0.2 * u - 0.5, 0.2 * v - 0.5, 0.2 * sin(u * v), 1.0);");

    // =========================================================================
    // 10. LIBRARY TREES & FILE SYSTEM
    // =========================================================================
    QSettings settings;
    QStringList repos = settings.value("repositoryPaths").toStringList();

    if (!repos.isEmpty() && QDir(repos.first()).exists()) lastTextureFolder = repos.first();
    else {
        QString osBaseDir;
#ifdef Q_OS_ANDROID
        osBaseDir = "/storage/emulated/0/Download";
#elif defined(Q_OS_LINUX)
        osBaseDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (osBaseDir.isEmpty()) osBaseDir = QDir::homePath();
#else
        osBaseDir = QDir::homePath();
#endif
        QString potentialPath = osBaseDir + "/Texture";
        if (QDir(potentialPath).exists()) lastTextureFolder = potentialPath;
        else lastTextureFolder = osBaseDir;
    }

    m_menuController = new LibraryMenuController(this);
    m_presetSerializer = new PresetSerializer(this);
    m_fileOps = new LibraryFileOperations(this);
    m_dragDropHandler = new LibraryDragDropHandler(this);
    m_audioController = new AudioController(this);

    auto initTree = [this](QTreeWidget* tree) {
        tree->setHeaderHidden(true);
        tree->setColumnCount(1);
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tree->setDragDropMode(QAbstractItemView::InternalMove);

        // 1. FORZA lo scroll per pixel
        tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

        // 2. IL VERO SEGRETO: Dichiara che le righe sono tutte alte uguali.
        // Senza questo, Qt annulla lo scroll fluido e torna agli "scatti"!
        tree->setUniformRowHeights(true);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // 3. FIX VIEWPORT: Applica il tocco alla "tela" interna, non al bordo!
        QScroller::grabGesture(tree->viewport(), QScroller::TouchGesture);
        tree->grabGesture(Qt::TapAndHoldGesture);

        tree->setIndentation(12);
#endif

        tree->installEventFilter(m_dragDropHandler);
        tree->viewport()->installEventFilter(m_dragDropHandler);
    };
    initTree(ui->treeSurfaces);
    connect(ui->treeSurfaces, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeSurfaces->itemAt(pos)) { ui->treeSurfaces->clearSelection(); ui->treeSurfaces->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeSurfaces, pos);
    });
    connect(ui->treeSurfaces, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeTextures);
    connect(ui->treeTextures, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeTextures->itemAt(pos)) { ui->treeTextures->clearSelection(); ui->treeTextures->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeTextures, pos);
    });
    connect(ui->treeTextures, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeMotions);
    connect(ui->treeMotions, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeMotions->itemAt(pos)) { ui->treeMotions->clearSelection(); ui->treeMotions->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeMotions, pos);
    });
    connect(ui->treeMotions, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeSounds);
    connect(ui->treeSounds, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeSounds->itemAt(pos)) { ui->treeSounds->clearSelection(); ui->treeSounds->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeSounds, pos);
    });
    connect(ui->treeSounds, &QTreeWidget::itemClicked, this, &MainWindow::onSoundItemClicked);

    connect(ui->btnSyncLibrary, &QPushButton::clicked, this, &MainWindow::onSyncPresetsClicked);

    m_fsWatcher = new QFileSystemWatcher(this);
    m_fsSyncTimer = new QTimer(this);
    m_fsSyncTimer->setSingleShot(true);
    m_fsSyncTimer->setInterval(500);

    connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &){ m_fsSyncTimer->start(); });
    connect(m_fsWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &){ m_fsSyncTimer->start(); });
    connect(m_fsSyncTimer, &QTimer::timeout, this, &MainWindow::refreshRepositories);

    // =========================================================================
    // 11. FINAL STARTUP CALLS
    // =========================================================================
    ui->chkBoxTexture->setChecked(false);
    ui->glWidget->setTextureEnabled(false);
    updateTextureUIState(false);

    connectSidePanels();
    switchToMainMode();
    refreshRepositories();
    updateWatcherPaths();

#if defined(Q_OS_IOS)
    // SU IOS: showFullScreen() è obbligatorio per sbloccare la risoluzione nativa
    this->showFullScreen();
#elif defined(Q_OS_ANDROID)
    // SU ANDROID: Ripristiniamo showMaximized().
    // Evita l'Immersive Mode che spinge la UI a destra contro il Notch.
    this->showMaximized();
#else
    this->resize(1280, 720);
    this->showMaximized();
#endif

    QPushButton* mobileMenuBtn = nullptr;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    ui->menuBar->hide();

    // Creiamo il bottone, con parent 'this' per evitare problemi di touch col 3D
    mobileMenuBtn = new QPushButton(this);
    mobileMenuBtn->setObjectName("mobileMenuBtn");

    // Applichiamo tutto il disegno e lo stile tramite il manager!
    UiStyleManager::styleMobileMenuButton(mobileMenuBtn);

    mobileMenuBtn->move(10, 10);

    QMenu* overflowMenu = new QMenu(this);
    overflowMenu->addAction(ui->actionDocumentation);
    overflowMenu->addAction(ui->actionAbout);
    overflowMenu->addSeparator();
    overflowMenu->addAction(ui->actionQuit);

    // Applichiamo il foglio di stile CSS del menu
    UiStyleManager::styleMobileOverflowMenu(overflowMenu);

    // Connessione al tocco
    connect(mobileMenuBtn, &QPushButton::released, this, [mobileMenuBtn, overflowMenu]() {
        QPoint pos = mobileMenuBtn->mapToGlobal(QPoint(0, mobileMenuBtn->height()));
        overflowMenu->exec(pos);
    });

    mobileMenuBtn->raise();
    mobileMenuBtn->show();
#endif

#if defined (Q_OS_IOS)
    // Manteniamo il tuo HACK DI PRE-RISCALDAMENTO per l'iPad
    this->setUpdatesEnabled(false);

    ui->dockEquations->show(); ui->dockEquations->hide();
    ui->dockRenders->show();   ui->dockRenders->hide();
    ui->dock3D->show();        ui->dock3D->hide();
    ui->dock4D->show();        ui->dock4D->hide();
    ui->dockScripts->show();   ui->dockScripts->hide();
    ui->dockSurfaces->show();  ui->dockSurfaces->hide();

    ui->dock3D->blockSignals(false);
    ui->dock4D->blockSignals(false);

    this->setUpdatesEnabled(true);
#endif

    // =========================================================================
    // 12. RESCALING & DESKTOP FILTERS
    // =========================================================================
    GLResizeFilter* resizeFixer = new GLResizeFilter(ui->glWidget);

    resizeFixer->onResize = [this]() {
        if (m_topMessageBar && m_topMessageBar->isVisible()) {
            int x = (this->centralWidget()->width() - m_topMessageBar->width()) / 2;
            m_topMessageBar->move(x, 40);
        }
    };

    ui->glWidget->installEventFilter(resizeFixer);

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Applica il blocco dell'invio su Desktop per TUTTI i campi di testo
    DesktopInputFilter* desktopFilter = new DesktopInputFilter(this);

    // Per i campi multi-riga delle equazioni (QPlainTextEdit)
    for (auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        textEdit->installEventFilter(desktopFilter);
    }

    // Per i campi a riga singola di costanti, limiti e percorsi (QLineEdit)
    for (auto* lineEdit : this->findChildren<QLineEdit*>()) {
        lineEdit->installEventFilter(desktopFilter);
    }
#endif

    EnterApplyFilter* equationEnterFilter = new EnterApplyFilter(this);
    equationEnterFilter->onEnter = [this]() { onStartClicked(); };
    ui->lineEquation->installEventFilter(equationEnterFilter);
}


// ==========================================================
// UI & STATE MANAGEMENT
// ==========================================================

void MainWindow::switchToMainMode()
{
    updateLayoutForMode(0);
}

void MainWindow::switchTo3DMode()
{   updateLayoutForMode(1);
    ui->glWidget->set4DLighting(false);
    updateProjectionButtonText();
}

void MainWindow::switchTo4DMode() {
    updateLayoutForMode(2);
    ui->glWidget->set4DLighting(true);
    updateProjectionButtonText();
}

void MainWindow::update4DButtonState()
{
    // 1. Controllo Equazione P
    QString pText = ui->lineP->toPlainText().trimmed();

    // Gestione Virgola/Punto
    QString sanitizedP = pText;
    sanitizedP.replace(",", ".");

    bool isNumber;
    float val = sanitizedP.toFloat(&isNumber);

    bool isEquation3D = true;

    if (!pText.isEmpty()) {
        if (isNumber) {
            if (std::abs(val) > 0.001f) {
                isEquation3D = false;
            }
        } else {
            isEquation3D = false;
        }
    }

    // 2. Controllo Rotazione 4D (Angoli Omega, Phi, Psi)
    bool has4DRotation = false;
    if (ui->glWidget) {
        float eps = 0.01f;
        bool angleNonZero = (std::abs(ui->glWidget->getOmega()) > eps ||
                             std::abs(ui->glWidget->getPhi())   > eps ||
                             std::abs(ui->glWidget->getPsi())   > eps);
        has4DRotation = angleNonZero;
    }

    // 3. Logica: ABILITA SE (P è valido e != 0) OPPURE (C'è rotazione 4D)
    bool enable4D = (!isEquation3D) || has4DRotation;

    // 4. --- CERCA IL PULSANTE NELLA STATUS BAR E ABILITALO/DISABILITALO ---
    QPushButton* btn4D = ui->statusbar->findChild<QPushButton*>("btnDock4D");
    if (btn4D) {
        btn4D->setEnabled(true);
    }

    // 5. Uscita forzata se disabilitato mentre attivo
    if (!enable4D && ui->dock4D->isVisible()) {
        ui->dock4D->close();
    }
}

void MainWindow::updateRenderState()
{
    // 1. Identifichiamo se siamo in modalità Ray Marching
    bool isImplicitMode = (ui->tabModeSelector->currentIndex() == 1);

        // --- DISATTIVAZIONE CONTROLLI NON SUPPORTATI ---
        ui->radioWF->setEnabled(!isImplicitMode);
        ui->btnBorder->setEnabled(!isImplicitMode);

        // Disattiviamo i controlli della densità del Wireframe
        ui->btnWireUMinus->setEnabled(!isImplicitMode);
        ui->btnWireUPlus->setEnabled(!isImplicitMode);
        ui->btnWireVMinus->setEnabled(!isImplicitMode);
        ui->btnWireVPlus->setEnabled(!isImplicitMode);

        // --- 1. DISATTIVAZIONE ROTAZIONI 4D ---
        // Omega (W-X)
        ui->btnOmegaMinus->setEnabled(!isImplicitMode);
        ui->btnOmegaPlus->setEnabled(!isImplicitMode);

        // Phi (W-Y)
        ui->btnPhiMinus->setEnabled(!isImplicitMode);
        ui->btnPhiPlus->setEnabled(!isImplicitMode);

        // Psi (W-Z)
        ui->btnPsiMinus->setEnabled(!isImplicitMode);
        ui->btnPsiPlus->setEnabled(!isImplicitMode);

        // --- 3. DISATTIVAZIONE PANNELLO 4D ---
        if (ui->dockWidgetContents_3) {
            ui->dockWidgetContents_3->setEnabled(!isImplicitMode);
        }

        if (isImplicitMode) {
            // Ripristino forzato se l'utente era in modalità non compatibili
            if (ui->radioWF->isChecked()) {
                ui->radioPhong->setChecked(true);
                m_savedRenderMode = 1;
            }
            if (ui->btnBorder->isChecked()) {
                ui->btnBorder->setChecked(false);
            }
        }

    // 2. RECUPERA LO STATO AGGIORNATO
    int mode = m_savedRenderMode;
    bool wantTexture = ui->chkBoxTexture->isChecked();

    if (ui->radioBackground) {
        if (ui->radioBackground->isChecked()) {
            wantTexture = m_surfaceTextureState;
        } else {
            wantTexture = ui->chkBoxTexture->isChecked();
        }
    }

    // 3. LOGICA TEXTURE (Mantenendo il fix per il Background)
    if (mode == 2 && !ui->radioBackground->isChecked()) {
        ui->chkBoxTexture->setEnabled(false);
    }
    else {
        ui->chkBoxTexture->setEnabled(true);
        if (wantTexture && !ui->radioBackground->isChecked()) {
            if (m_isCustomMode) mode = 11;
        }
    }

    bool isPhong = (m_savedRenderMode == 1);

    // 4. APPLICAZIONE AL MOTORE GRAFICO
    if (ui->glWidget) {
        ui->glWidget->setSpecularEnabled(isPhong);

        // Blocca la texture della superficie SOLO in modalità Wireframe (mode == 2)
        bool blockSurfaceTexture = (mode == 2);
        ui->glWidget->setTextureEnabled(wantTexture && !blockSurfaceTexture);

        if (isImplicitMode) {
            // Modalità Ray Marching: Ascolta SOLO i radio button Shell/Solid dedicati
            ui->glWidget->setRenderMode(ui->radioShell->isChecked() ? 1 : 0);
        } else {
            // Modalità Parametrica: Ascolta i radio button classici
            if (ui->radioWF->isChecked()) {
                ui->glWidget->setRenderMode(2);
            } else if (ui->radioPhong->isChecked()) {
                ui->glWidget->setRenderMode(1);
            } else {
                ui->glWidget->setRenderMode(0);
            }
        }

        ui->glWidget->update();
    }
}

void MainWindow::checkParametricDependency()
{
    QString eqX = ui->lineX->toPlainText();
    QString eqY = ui->lineY->toPlainText();
    QString eqZ = ui->lineZ->toPlainText();
    QString eqP = ui->lineP->toPlainText();

    // Testi espliciti
    QString eqExplU = ui->lineExplicitU->toPlainText();
    QString eqExplV = ui->lineExplicitV->toPlainText();
    QString eqExplW = ui->lineExplicitW->toPlainText();

    // Testi composizione
    QString defU = ui->lineU->toPlainText();
    QString defV = ui->lineV->toPlainText();
    QString defW = ui->lineW->toPlainText();

    // 1. ANALISI RAW: Contiamo ESATTAMENTE cosa ha digitato l'utente
    QString mainEqs = eqX + " " + eqY + " " + eqZ + " " + eqP;

    bool hasRaw_u = mainEqs.contains(QRegularExpression("\\bu\\b"));
    bool hasRaw_v = mainEqs.contains(QRegularExpression("\\bv\\b"));
    bool hasRaw_w = mainEqs.contains(QRegularExpression("\\bw\\b"));
    int rawLowerCount = (hasRaw_u ? 1 : 0) + (hasRaw_v ? 1 : 0) + (hasRaw_w ? 1 : 0);

    bool hasRaw_U = mainEqs.contains(QRegularExpression("\\bU\\b"));
    bool hasRaw_V = mainEqs.contains(QRegularExpression("\\bV\\b"));
    bool hasRaw_W = mainEqs.contains(QRegularExpression("\\bW\\b"));
    int rawUpperCount = (hasRaw_U ? 1 : 0) + (hasRaw_V ? 1 : 0) + (hasRaw_W ? 1 : 0);

    // Variabili di stato per i Tab
    bool needsConstraint = false;
    bool needsComposition = false;
    bool needsGeodesic = false;

    // Controllo se ci sono testi inseriti nelle tab Composition o Geodesic
    bool compHasText = !defU.trimmed().isEmpty() ||
                       !defV.trimmed().isEmpty() ||
                       !defW.trimmed().isEmpty();

   bool geoHasText = hasGeodesicText();

    // 2. MACCHINA A STATI: Apertura e Blocco Tab intelligente
    if (ui->panelImplicit) {
        if (rawLowerCount == 3 && rawUpperCount == 0) {
            // Caso 3 minuscole: Solo Vincoli attivi
            needsConstraint = true;
            ui->panelImplicit->setTabEnabled(0, true);
            ui->panelImplicit->setTabEnabled(1, false); // RIMESSO: Spegne Composition
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false); // RIMESSO: Spegne Geodesic

            // Se la tab corrente è disabilitata, allora sposta il focus su Constraints
            if (!ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                ui->panelImplicit->setCurrentIndex(0);
            }
        }
        else if (rawUpperCount == 3 && rawLowerCount == 0) {
            // Caso 3 maiuscole: Esclusione mutua tra Composition e Geodesic
            ui->panelImplicit->setTabEnabled(0, false); // Vincoli sempre OFF

            if (geoHasText) {
                // Sto scrivendo in Geodesic: blocco Composition
                needsGeodesic = true;
                ui->panelImplicit->setTabEnabled(1, false);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);
            }
            else if (compHasText) {
                // Sto scrivendo in Composition: blocco Geodesic
                needsComposition = true;
                ui->panelImplicit->setTabEnabled(1, true);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false);
            }
            else {
                // Entrambi vuoti: entrambi abilitati e pronti all'uso
                needsComposition = true;
                needsGeodesic = true;
                ui->panelImplicit->setTabEnabled(1, true);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);
            }

            // Gestione Focus dolce
            if (!ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                if (compHasText) ui->panelImplicit->setCurrentIndex(1);
                else if (geoHasText) ui->panelImplicit->setCurrentIndex(2);
                else ui->panelImplicit->setCurrentIndex(1); // Default su Composition
            }
        }
        else {
            // Qualsiasi altra combinazione (incluse solo 2 maiuscole/minuscole): TUTTO SPENTO
            ui->panelImplicit->setTabEnabled(0, false);
            ui->panelImplicit->setTabEnabled(1, false);
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false);
        }
    }

    // 3. Applica stato fisico ai campi dei Vincoli
    ui->lineExplicitU->setEnabled(needsConstraint);
    ui->lineExplicitV->setEnabled(needsConstraint);
    ui->lineExplicitW->setEnabled(needsConstraint);

    // 4. Applica stato fisico ai campi Composition
    ui->lineU->setEnabled(needsComposition && hasRaw_U);
    ui->lineV->setEnabled(needsComposition && hasRaw_V);
    ui->lineW->setEnabled(needsComposition && hasRaw_W);

    // 5. Applica stato fisico ai campi Geodesic
    if (ui->lnU) {
        ui->lnU->setEnabled(needsGeodesic);
        ui->lnV->setEnabled(needsGeodesic);
        ui->lnW->setEnabled(needsGeodesic);
        ui->lndU->setEnabled(needsGeodesic);
        ui->lndV->setEnabled(needsGeodesic);
        ui->lndW->setEnabled(needsGeodesic);
    }

    // 6. ANALISI COMPOSTA: Accensione degli Slider (Rigidamente Case-Sensitive!)
    QString allEqs = mainEqs + " " + eqExplU + " " + eqExplV + " " + eqExplW;
    QString composedAllEqs = composeEquation(allEqs, defU, defV, defW);

    bool isGeodesicActive = (rawUpperCount > 0) && geoHasText && (ui->tabModeSelector->currentIndex() == 0);

    if (ui->stepSlider->maximum() != 1000) {
        ui->stepSlider->setMaximum(1000);
    }

    updateConstraintState();
    updateConstantsUIState();
}

void MainWindow::updateConstraintState()
{
    QString txtU = ui->lineExplicitU->toPlainText().trimmed();
    QString txtV = ui->lineExplicitV->toPlainText().trimmed();
    QString txtW = ui->lineExplicitW->toPlainText().trimmed();

    bool hasConstraintU = !txtU.isEmpty();
    bool hasConstraintV = !txtV.isEmpty();
    bool hasConstraintW = !txtW.isEmpty();

    QString defU = ui->lineU->toPlainText();
    QString defV = ui->lineV->toPlainText();
    QString defW = ui->lineW->toPlainText();

    QString allMainEqs = ui->lineX->toPlainText() + " " +
                         ui->lineY->toPlainText() + " " +
                         ui->lineZ->toPlainText() + " " +
                         ui->lineP->toPlainText();

    QString composedEqs = composeEquation(allMainEqs, defU, defV, defW);

    bool usesU = composedEqs.contains(QRegularExpression("\\bu\\b"));
    bool usesV = composedEqs.contains(QRegularExpression("\\bv\\b"));
    bool usesW = composedEqs.contains(QRegularExpression("\\bw\\b"));

    // --- Disabilitazione e svuotamento Limiti W in modalità Geodesic Flow / Composition ---
    int upperCount = (allMainEqs.contains(QRegularExpression("\\bU\\b")) ? 1 : 0) +
            (allMainEqs.contains(QRegularExpression("\\bV\\b")) ? 1 : 0) +
            (allMainEqs.contains(QRegularExpression("\\bW\\b")) ? 1 : 0);

    bool geoHasText = hasGeodesicText();

    bool isGeodesicActive = (upperCount > 0) && geoHasText && (ui->tabModeSelector->currentIndex() == 0);

    // Aggiungiamo la rilevazione per la modalità Composition
    bool isCompositionActive = (upperCount > 0 || !defU.trimmed().isEmpty() || !defV.trimmed().isEmpty() || !defW.trimmed().isEmpty()) && !geoHasText && (ui->tabModeSelector->currentIndex() == 0);

    if (isGeodesicActive || isCompositionActive) {
        usesW = false;
    }

    auto applyLimitsState = [](QLineEdit* minEdit, QLineEdit* maxEdit, bool enable) {
        minEdit->setEnabled(enable);
        maxEdit->setEnabled(enable);
        if (!enable) {
            minEdit->clear();
            maxEdit->clear();
        }
    };

    if (hasConstraintU) {
        UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Active);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Inactive);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Inactive);

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, false);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
    }
    else if (hasConstraintV) {
        UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Active);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Inactive);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Inactive);

        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, false);
        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
    }
    else {
        if (hasConstraintW) {
            UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Active);
            UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Inactive);
            UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Inactive);

            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, false);
        } else {
            if (usesW) {
                UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Default);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Default);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Default);
            } else {
                UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Disabled);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Disabled);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Disabled);
            }
            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);
        }

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
    }
}

void MainWindow::updateConstantsUIState() {
    QString activeText = "";
    int currentTab = ui->tabModeSelector->currentIndex();

    // 1. RACCOLTA TESTO SPECIFICA PER TAB
    if (currentTab == 0) { // MODALITÀ PARAMETRICA
        activeText = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
                     ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText() + " " +
                     ui->lineExplicitU->toPlainText() + " " + ui->lineExplicitV->toPlainText() + " " +
                     ui->lineExplicitW->toPlainText() + " " + ui->lineU->toPlainText() + " " +
                     ui->lineV->toPlainText() + " " + ui->lineW->toPlainText();

        if (ui->lnU) { // Campi Geodetici (Tab 0)
            activeText += " " + ui->lnU->toPlainText() +
                    " " + ui->lnV->toPlainText() +
                    " " + ui->lnW->toPlainText() +
                    " " + ui->lndU->toPlainText() +
                    " " + ui->lndV->toPlainText() +
                    " " + ui->lndW->toPlainText() +
                    " " + ui->lineConform->toPlainText();
        }
        // In parametrica aggiungiamo lo script della superficie se non siamo in Ray Marching
        activeText += m_surfaceTextureCode;
    }
    else { // MODALITÀ RAY MARCHING
        activeText = ui->lineEquation->toPlainText() + " " +
                     ui->lineTexture->toPlainText() + " " +
                     ui->lineVariations->toPlainText();
    }

    // 2. AGGIUNGI CAMPI SEMPRE ATTIVI (Shared)
    activeText += " " + m_bgTextureCode; // Lo sfondo è comune
    activeText += " " + ui->txtScriptEditor->toPlainText(); // L'editor mostra il codice della tab attuale

    // 3. LOGICA DI BLOCCO/SBLOCCO E RESET
    auto updateControl = [&](const QString& letter, QSlider* slider, QLineEdit* line) {

        bool used = false;

        // FIX FONDAMENTALE: In Ray Marching (tab 1), "S" funge da Step Relax!
        // Deve rimanere sempre attivo e NON deve mai essere resettato a 0,
        // altrimenti i raggi si congelano causando glitch grafici e cerchi concentrici.
        if (currentTab == 1 && letter == "S") {
            used = true;
        } else {
            QRegularExpression re("\\b" + letter + "\\b", QRegularExpression::CaseInsensitiveOption);
            used = activeText.contains(re);
        }

        if (!used) {
            bool oldS = slider->blockSignals(true);
            bool oldL = line->blockSignals(true);

            // RESET: S a 0.0, le altre (A-F) a 1.0
            if (letter == "S") {
                line->setText("0");
                slider->setValue(0);
            } else {
                line->setText("1");
                slider->setValue(100);
            }

            slider->setEnabled(false);
            line->setEnabled(false);

            slider->blockSignals(oldS);
            line->blockSignals(oldL);
        } else {
            slider->setEnabled(true);
            line->setEnabled(true);
        }
    };

    updateControl("A", ui->aSlider, ui->lineA);
    updateControl("B", ui->bSlider, ui->lineB);
    updateControl("C", ui->cSlider, ui->lineC);
    updateControl("D", ui->dSlider, ui->lineD);
    updateControl("E", ui->eSlider, ui->lineE);
    updateControl("F", ui->fSlider, ui->lineF);
    updateControl("S", ui->sSlider, ui->lineS);
}

void MainWindow::performMasterStop()
{
    m_masterStopped = true;

    // Ferma le animazioni delle variabili t
    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(false);
        ui->glWidget->setSurfaceTextureAnimating(false);
        ui->glWidget->setBackgroundTextureAnimating(false);
    }

    // Ferma il flusso geodetico
    QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
    if (geoAnimTimer && geoAnimTimer->isActive()) {
        geoAnimTimer->stop();
    }

    // Ferma le rotazioni 3D/4D delegando al suo tasto dedicato
    if (ui->glWidget && ui->glWidget->isAnimating()) onStopClicked();

    // Ferma i path delegando ai loro tasti dedicati
    if (pathTimer && pathTimer->isActive()) onDepartureClicked();
    if (pathTimer3D && pathTimer3D->isActive()) onDeparture3DClicked();

    // Ferma l'audio
    if (m_audioController && m_audioController->isPlaying()) {
        m_audioController->stopAll();
        updateScriptButtonText();
    }

    // Sincronizza il bottone principale
    updateMasterButtonState();
}

void MainWindow::applyStartSideEffects()
{
    if (!ui->glWidget) return;

    bool hasRotationSpeed = (std::abs(ui->glWidget->getNutationSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getPrecessionSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getSpinSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getPhiSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getPsiSpeed()) > 0.001f);
    if (hasRotationSpeed && !ui->glWidget->isAnimating()) {
        onStopClicked();
    }

    bool hasPath4D = !ui->lineX_P->text().isEmpty() && ui->lineX_P->text() != "0";
    if (hasPath4D && !pathTimer->isActive()) {
        onDepartureClicked();
    }

    bool hasPath3D = !ui->lineX_P3D->text().isEmpty() && ui->lineX_P3D->text() != "0";
    if (hasPath3D && !pathTimer3D->isActive()) {
        onDeparture3DClicked();
    }

    if (m_audioController && !m_audioController->isPlaying()) {
        QString codeToAnalyze = m_soundScriptText + "\n" + m_surfaceScriptText + "\n" +
                                m_surfaceTextureCode + "\n" + m_bgTextureCode;
        if (codeToAnalyze.trimmed().isEmpty()) codeToAnalyze = ui->txtScriptEditor->toPlainText();

        if (!codeToAnalyze.trimmed().isEmpty()) {
            m_audioController->playFromScript(codeToAnalyze);
            updateScriptButtonText();
        }
    }
}


// ==========================================================
// RENDERING & VISUALS
// ==========================================================

void MainWindow::onColorTargetChanged()
{
    // Blocchiamo i segnali per evitare loop infiniti
    ui->sliderR->blockSignals(true);
    ui->sliderG->blockSignals(true);
    ui->sliderB->blockSignals(true);

    QColor target;

    if (ui->radioBackground->isChecked()) {
        if (ui->chkBoxTexture->isChecked()) {
            if (ui->radioTexColor1->isChecked()) {
                target = m_bgTexColor1;
            } else {
                target = m_bgTexColor2;
            }
        } else {
            target = m_currentBackgroundColor;
        }
    }
    else {
        if (ui->radioEditSurf->isChecked()) {
            target = m_currentSurfaceColor;
        }
        else if (ui->radioEditBorder->isChecked()) {
            target = m_currentBorderColor;
        }
        else if (ui->radioTexColor1->isChecked()) {
            target = m_texColor1;
        }
        else {
            target = m_texColor2;
        }
    }

    // Imposta gli slider al valore del colore selezionato
    ui->sliderR->setValue(target.red());
    ui->sliderG->setValue(target.green());
    ui->sliderB->setValue(target.blue());

    // Aggiorna anche le etichette numeriche
    ui->valR->setNum(target.red());
    ui->valG->setNum(target.green());
    ui->valB->setNum(target.blue());

    // Riattiva i segnali
    ui->sliderR->blockSignals(false);
    ui->sliderG->blockSignals(false);
    ui->sliderB->blockSignals(false);
}

void MainWindow::scheduleTextureGeneration()
{
    // Controllo di sicurezza
    if (!ui->glWidget) return;

    // Evitiamo di generare l'immagine se il programma sta caricando un file
    if (m_blockTextureGen) return;

    // Genera la nuova scacchiera con i colori aggiornati
    generateTexture();
}

void MainWindow::handleTextureSelection(int index)
{
    // 1. Recupera i dati
    const LibraryItem &data = m_libraryManager.getTexture(index);

    // Ricorda il file della texture caricata: serve come nome di default in salvataggio
    // (stessa logica dei record). Vuoto solo se non è stata caricata nessuna texture.
    m_currentTexturePresetPath = data.filePath;

    static int lastTextureIndex = -1;
    static bool lastWasBg = false;
    bool isBg = ui->radioBackground->isChecked();

    // Se l'utente clicca di nuovo lo stesso script procedurale, scarta l'immagine!
    if (index == lastTextureIndex && isBg == lastWasBg && !data.isImage) {
        m_isImageMode = false;
        m_currentTexturePath.clear();

        // Rimuove il tag //IMG: dall'editor testuale
        QString currentText = ui->txtScriptEditor->toPlainText();
        currentText.remove(QRegularExpression(R"(^\s*//IMG:\s*(.*)$\n?)", QRegularExpression::MultilineOption));
        ui->txtScriptEditor->blockSignals(true);
        ui->txtScriptEditor->setPlainText(currentText);
        ui->txtScriptEditor->blockSignals(false);
    }
    lastTextureIndex = index;
    lastWasBg = isBg;

    // 2. CONTROLLO MODALITÀ SFONDO
    if (ui->radioBackground->isChecked()) {
        if (data.hasCustomColors) {
            m_bgTexColor1 = QColor(data.color1);
            m_bgTexColor2 = QColor(data.color2);
        } else {
            m_bgTexColor1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f); // Verde default
            m_bgTexColor2 = Qt::black;
        }

        if (data.isImage) {
            ui->glWidget->setBackgroundTexture(data.filePath);
            m_bgTextureCode = "//IMG:" + data.filePath;

            if (ui->glWidget) {
                ui->glWidget->setProperty("bg_zoom", data.zoom);
                ui->glWidget->setProperty("bg_pan", QVector2D(data.panX, data.panY));
                ui->glWidget->setProperty("bg_rot", data.rotation);
            }

            if (m_currentScriptMode == ScriptModeTexture) {
                ui->txtScriptEditor->blockSignals(true);
                ui->txtScriptEditor->setPlainText(m_bgTextureCode);
                ui->txtScriptEditor->blockSignals(false);
            }
        }
        else {
            if (data.isImplicitMode) {
                QMessageBox::warning(this, "Incompatible Texture",
                                     "Procedural textures for 3D (implicit) surfaces cannot be used as a background.\n"
                                     "The background will be restored to its default state.");

                // 1. Ripristino colori di default
                m_bgTexColor1 = QColor::fromRgbF(0.2f, 0.2f, 0.8f);
                m_bgTexColor2 = Qt::black;

                if (ui->glWidget) {
                    ui->glWidget->setProperty("bg_col1", QVector3D(m_bgTexColor1.redF(), m_bgTexColor1.greenF(), m_bgTexColor1.blueF()));
                    ui->glWidget->setProperty("bg_col2", QVector3D(m_bgTexColor2.redF(), m_bgTexColor2.greenF(), m_bgTexColor2.blueF()));
                    ui->glWidget->setProperty("bg_zoom", 1.0f);
                    ui->glWidget->setProperty("bg_pan", QVector2D(0.0f, 0.0f));
                    ui->glWidget->setProperty("bg_rot", 0.0f);
                }

                // 2. Stringa sicura che l'engine riconosce per evitare crash dello shader
                QString safeDefault = "";
                m_bgTextureCode = safeDefault;
                m_bgTextureScriptText = safeDefault;

                if (m_currentScriptMode == ScriptModeTexture) {
                    ui->txtScriptEditor->blockSignals(true);
                    ui->txtScriptEditor->setPlainText(safeDefault);
                    ui->txtScriptEditor->blockSignals(false);
                }

                // 3. Generiamo fisicamente la scacchiera nella memoria video
                generateTexture();

                // 4. Manteniamo la checkbox attiva per mostrare la texture di default
                bool oldBlock = ui->chkBoxTexture->blockSignals(true);
                ui->chkBoxTexture->setChecked(true);
                ui->chkBoxTexture->blockSignals(oldBlock);

                ui->radioTexColor1->setEnabled(true);
                ui->radioTexColor2->setEnabled(true);
                bool oldRad = ui->radioTexColor1->blockSignals(true);
                ui->radioTexColor1->setChecked(true);
                ui->radioTexColor1->blockSignals(oldRad);

                // 5. Ricostruzione pulita e nativa dello shader di background
                if (ui->glWidget) {
                    ui->glWidget->setBackgroundTextureEnabled(true);
                    ui->glWidget->rebuildBackgroundShader(true, safeDefault);
                }

                onColorTargetChanged();
                updateFlatPreviewButton();

                if (ui->glWidget) ui->glWidget->update();

                return; // Salvataggio riuscito, usciamo
            }

            // CARICAMENTO TEXTURE PROCEDURALE 2D
            QString newCode = data.scriptCode;

            // 1. Controlliamo se c'era già un'immagine di sfondo attiva nell'editor.
            // Questo permette di mixare immagini e codice procedurale al primo clic.
            QString currentEditorText = ui->txtScriptEditor->toPlainText();
            QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
            QRegularExpressionMatch imgMatch = imgRe.match(currentEditorText);

            if (imgMatch.hasMatch() && !newCode.contains("//IMG:")) {
                newCode = "//IMG:" + imgMatch.captured(1).trimmed() + "\n" + newCode;
            }

            m_bgTextureCode = newCode;
            m_bgTextureScriptText = newCode;

            // Salviamo il testo dell'editor per non rovinare altre schede (es. Surface)
            QString prevEditorText = ui->txtScriptEditor->toPlainText();

            // 2. Scriviamo il codice nell'editor e "premiamo" il tasto Run virtualmente
            ui->txtScriptEditor->blockSignals(true);
            ui->txtScriptEditor->setPlainText(newCode);

            // 3. Eseguiamo il metodo centralizzato (fa parsing, carica immagini, compila e fa l'Update GPU!)
            onApplyTextureScriptClicked();

            // Ripristiniamo l'editor se non eravamo nella tab Texture
            if (m_currentScriptMode != ScriptModeTexture) {
                ui->txtScriptEditor->setPlainText(prevEditorText);
            }
            ui->txtScriptEditor->blockSignals(false);

            // 4. Applica proprietà aggiuntive di trasformazione
            if (ui->glWidget) {
                ui->glWidget->setProperty("bg_zoom", data.zoom);
                ui->glWidget->setProperty("bg_pan", QVector2D(data.panX, data.panY));
                ui->glWidget->setProperty("bg_rot", data.rotation);
            }

            return;
        }

        ui->glWidget->setBackgroundTextureEnabled(true);
        bool oldBlock = ui->chkBoxTexture->blockSignals(true);
        ui->chkBoxTexture->setChecked(true);
        ui->chkBoxTexture->blockSignals(oldBlock);

        ui->radioTexColor1->setEnabled(true);
        ui->radioTexColor2->setEnabled(true);
        bool oldRad = ui->radioTexColor1->blockSignals(true);
        ui->radioTexColor1->setChecked(true);
        ui->radioTexColor1->blockSignals(oldRad);

        onColorTargetChanged();
        updateFlatPreviewButton();
        return; // Fine caricamento Sfondo
    }

    // =====================================================================
    // 2.5 CONTROLLO COMPATIBILITÀ MODO E SUPERFICIE DI DEFAULT
    // =====================================================================
    bool texIsImplicit = data.isImplicitMode;
    bool currentIsImplicit = (ui->tabModeSelector->currentIndex() == 1);

    if (data.isImage) {
        texIsImplicit = currentIsImplicit;
    }

    bool modeSwitched = false;

    if (texIsImplicit != currentIsImplicit) {
        // A. Cambia automaticamente il pannello (Tab)
        ui->tabModeSelector->setCurrentIndex(texIsImplicit ? 1 : 0);

        // B. Imposta una Superficie di Default sicura e azzera il resto
        if (texIsImplicit) {
            //modeSwitched = true;
            // --- PREPARA AMBIENTE RAY MARCHING ---
            ui->lineEquation->setPlainText("x*x + y*y + z*z = 1.0"); // Sfera Implicita
            ui->lineVariations->clear();
            ui->lineX->clear();
            ui->lineY->clear();
            ui->lineZ->clear();
            ui->lineP->clear();
            m_surfaceTextureCode.clear();
        } else {
            // --- PREPARA AMBIENTE PARAMETRICO ---
            ui->lineX->setPlainText("(0.8 + 0.3 * cos(v)) * cos(u))");
            ui->lineY->setPlainText("(0.8 + 0.3 * cos(v)) * sin(u)");
            ui->lineZ->setPlainText("0.3 * sin(v)");
            ui->uMinEdit->setText("0");
            ui->uMaxEdit->setText("6.28318");
            ui->vMinEdit->setText("0");
            ui->vMaxEdit->setText("6.28318");

            ui->lineEquation->clear();
            ui->lineTexture->clear();
            ui->lineVariations->clear();

            if (ui->glWidget) {
                ui->glWidget->setDisplacementCode("");
                ui->glWidget->setTextureCode("");
            }
        }

        // C. Resetta lo shader nel widget per rimuovere codice obsoleto
        if (ui->glWidget) {
            ui->glWidget->loadCustomShader("");
            ui->glWidget->clearTexture();
            ui->glWidget->setTextureCode(0);
        }
    }

    // =====================================================================
    // 3. APPLICAZIONE DATI TEXTURE (Separati per Parametrico / Implicito)
    // =====================================================================
    m_blockTextureGen = true;

    if (data.hasCustomColors) {
        m_texColor1 = QColor(data.color1);
        m_texColor2 = QColor(data.color2);
    } else {
        m_texColor1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
        m_texColor2 = Qt::black;
    }

    if (ui->glWidget) ui->glWidget->setTextureColors(m_texColor1, m_texColor2);

    if (ui->radioTexColor1->isChecked() || ui->radioTexColor2->isChecked()) {
        onColorTargetChanged();
    }

    // DIVIDIAMO IL FLUSSO IN BASE ALLA NATURA DELLA TEXTURE
    if (!texIsImplicit) {

        // --- LOGICA PARAMETRICA ---
        if (data.isImage) {
            m_currentTexturePath = data.filePath;
            if (ui->glWidget) {
                ui->glWidget->loadCustomShader("");
                ui->glWidget->loadTextureFromFile(data.filePath);
                ui->glWidget->setTextureEnabled(true);
                ui->glWidget->rebuildShader();

                if (!ui->chkBoxTexture->isChecked()) {
                    bool old = ui->chkBoxTexture->blockSignals(true);
                    ui->chkBoxTexture->setChecked(true);
                    ui->chkBoxTexture->blockSignals(old);
                    updateTextureUIState(true);
                    m_surfaceTextureState = true;
                }

                m_isCustomMode = false;
                m_isImageMode = true;
                m_surfaceTextureCode = "//IMG:" + data.filePath;

                ui->glWidget->setFlatViewTarget(0);
                ui->glWidget->setFlatZoom(data.zoom);
                ui->glWidget->setFlatPan(data.panX, data.panY);
                ui->glWidget->setFlatRotation(data.rotation);

                updateRenderState();
                ui->glWidget->update();
            }

            if (m_currentScriptMode == ScriptModeTexture) {
                ui->txtScriptEditor->blockSignals(true);
                ui->txtScriptEditor->setPlainText(m_surfaceTextureCode);
                ui->txtScriptEditor->blockSignals(false);
            }

        } else if (!data.scriptCode.isEmpty()) {
            QString newCode = data.scriptCode;

            // Preserviamo l'immagine se già caricata
            if (m_isImageMode && !m_currentTexturePath.isEmpty()) {
                newCode = "//IMG:" + m_currentTexturePath + "\n" + newCode;
            } else {
                m_isImageMode = false;
            }

            m_surfaceTextureCode = newCode;
            m_surfaceTextureScriptText = newCode;

            QString prevEditorText = ui->txtScriptEditor->toPlainText();
            ui->txtScriptEditor->blockSignals(true);
            ui->txtScriptEditor->setPlainText(newCode);

            onApplyTextureScriptClicked();

            if (m_currentScriptMode != ScriptModeTexture) {
                ui->txtScriptEditor->setPlainText(prevEditorText);
            }
            ui->txtScriptEditor->blockSignals(false);

            if (ui->glWidget) {
                ui->glWidget->setFlatViewTarget(0);
                ui->glWidget->setFlatZoom(data.zoom);
                ui->glWidget->setFlatPan(data.panX, data.panY);
                ui->glWidget->setFlatRotation(data.rotation);
            }
        }
    } else {    
        // --- LOGICA RAY MARCHING (IMPLICIT) ---
        // 1. Caricamento fisico dell'immagine nella GPU
        if (data.isImage) {
            m_currentTexturePath = data.filePath;
            m_isImageMode = true;
            m_isCustomMode = false;
            if (ui->glWidget) {
                ui->glWidget->loadTextureFromFile(data.filePath);
            }
        }

        ui->lineVariations->blockSignals(true);
        ui->lineVariations->setPlainText(data.displacementCode);
        ui->lineVariations->blockSignals(false);
        if (ui->glWidget) ui->glWidget->setDisplacementCode(data.displacementCode);

        // Backup per retrocompatibilità coi vecchi preset
        QString rmTexCode = data.textureCode.isEmpty() ? data.scriptCode : data.textureCode;

        // 2. Iniezione automatica del Triplanar Mapping per le immagini pure!
        if (data.isImage) {
            // Se l'immagine non ha già uno script personalizzato, generiamo noi il Triplanar!
            if (rmTexCode.trimmed().isEmpty()) {
                rmTexCode = "vec3 blend = abs(n_model);\n"
                            "blend /= max(blend.x + blend.y + blend.z, 0.00001);\n"
                            "float scale = 0.5;\n"
                            "vec3 cX = texture(tex, pModel.yz * scale).rgb;\n"
                            "vec3 cY = texture(tex, pModel.xz * scale).rgb;\n"
                            "vec3 cZ = texture(tex, pModel.xy * scale).rgb;\n"
                            "textureCol = cX * blend.x + cY * blend.y + cZ * blend.z;";
            }
            // Aggiungiamo il tag in cima per dire al motore di caricare il file fisico
            rmTexCode = "//IMG:" + data.filePath + "\n" + rmTexCode;
        }

        ui->lineTexture->blockSignals(true);
        ui->lineTexture->setPlainText(rmTexCode);
        ui->lineTexture->blockSignals(false);

        if (ui->glWidget) {
            // 1. Preparazione dell'equazione (per darla in pasto al validatore)
            QString rawEq = ui->lineEquation->toPlainText().trimmed();
            QString implicitEqF;
            if (rawEq.contains("=")) {
                QStringList parts = rawEq.split("=");
                implicitEqF = QString("(%1) - (%2)").arg(parts[0].trimmed(), parts[1].trimmed());
            } else {
                implicitEqF = QString("(%1) - (0.0)").arg(rawEq);
            }

            // 2. Stato UI
            bool oldBlock = ui->chkBoxTexture->blockSignals(true);
            ui->chkBoxTexture->setChecked(true);
            ui->chkBoxTexture->blockSignals(oldBlock);

            // ---> LE RIGHE CRITICHE RIPRISTINATE: Sincronizziamo la memoria! <---
            ui->glWidget->setTextureCode(rmTexCode);
            ui->glWidget->setTextureEnabled(true);
            m_surfaceTextureState = true;

            // 3. Validazione reale
            bool success = ui->glWidget->validateAndApplyImplicitShader(implicitEqF, rmTexCode, data.displacementCode);

            if (!success) {
                performMasterStop();
                InputValidator::showShaderCompilationError(this, "Preset Shader Error", ui->glWidget->getShaderError());
                ui->glWidget->setTextureCode("");
                ui->glWidget->setTextureEnabled(false);
                ui->glWidget->rebuildShader();
                return; // Esce in sicurezza senza crashare
            }

            // ---> COMPILAZIONE FINALE RIPRISTINATA <---
            ui->glWidget->rebuildShader();

            ui->glWidget->updateSurfaceData();
            ui->glWidget->update();
        }
    }

    m_blockTextureGen = false;
    updateFlatPreviewButton();

    // ==========================================
    // ANIMAZIONE: selezionare una texture di SUPERFICIE avvia solo il proprio
    // orologio. NON tocca né la geometria (SDF) né lo sfondo né la camera.
    // ==========================================
    QRegularExpression timeRegex("\\b(t|iTime|u_time)\\b");
    bool isRM = (ui->tabModeSelector->currentIndex() == 1);
    bool isSurfTexActive = ui->chkBoxTexture->isChecked();

    bool texColorAnim = false;   // 't' nel colore texture -> orologio TEXTURE
    bool geomAnim     = false;   // 't' in displacement/equazione -> orologio GEOMETRIA
    if (isSurfTexActive) {
        if (isRM) {
            texColorAnim = ui->lineTexture->toPlainText().contains(timeRegex);
            geomAnim     = ui->lineVariations->toPlainText().contains(timeRegex) ||
                           ui->lineEquation->toPlainText().contains(timeRegex);
        } else {
            texColorAnim = m_surfaceTextureCode.contains(timeRegex);
        }
    }
    if (texColorAnim || geomAnim) m_masterStopped = false;

    if (ui->glWidget) {
        ui->glWidget->setSurfaceTextureAnimating(texColorAnim);
        if (isRM) ui->glWidget->setSurfaceAnimating(geomAnim);   // <-- il displacement vive qui
    }
    updateMasterButtonState();

    // Se abbiamo cambiato scheda, assicuriamoci che l'engine inizializzi correttamente
    // le equazioni chiamando onStartClicked() se necessario
    if (modeSwitched) {
        onStartClicked();
    }

    if (m_currentScriptMode != ScriptModeSound) {
        ui->btnRunCurrentScript->setEnabled(false);
    }

    onColorTargetChanged();

    this->setProperty("isTextureModified", false);
}


// ==========================================================
// EQUATIONS & MATHEMATICS
// ==========================================================

bool MainWindow::updateULimits() {
    float lo = parseMath(ui->uMinEdit->text());
    float hi = parseMath(ui->uMaxEdit->text());
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    uMin = lo; uMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeU(uMin, uMax);
    return true;
}

bool MainWindow::updateVLimits() {
    float lo = parseMath(ui->vMinEdit->text());
    float hi = parseMath(ui->vMaxEdit->text());
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    vMin = lo; vMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeV(vMin, vMax);
    return true;
}

bool MainWindow::updateWLimits() {
    float lo = parseMath(ui->wMinEdit->text());
    float hi = parseMath(ui->wMaxEdit->text());
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    wMin = lo; wMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeW(wMin, wMax);
    return true;
}


// ==========================================================
// ANIMATION, MOTION & TIMERS
// ==========================================================

void MainWindow::onStartClicked()
{
    m_geodesicErrorPending = false;
    setProperty("geoErrorShown", false);   // riarma il popup geodetico per la nuova azione
    if (!property("rmApplyOnly").toBool())
        setProperty("collapseErrorShown", false);  // riarma il collasso solo sulle azioni vere (Start/caricamento)

    // --- 1. BLOCCO STOP GLOBALE (MASTER) ---
    if (m_btnStart && m_btnStart->text().toUpper() == "STOP") {
        if (sender() == m_btnStart) {
            performMasterStop();
            return;
        }
    }

    // --- 2. BLOCCO START GLOBALE (MASTER) ---
    if (m_btnStart && m_btnStart->text().toUpper() == "START") {
        if (sender() == m_btnStart) {
            m_masterStopped = false;

            this->setProperty("active_lineX", ui->lineX->toPlainText());
            this->setProperty("active_lineY", ui->lineY->toPlainText());
            this->setProperty("active_lineZ", ui->lineZ->toPlainText());
            this->setProperty("active_lineP", ui->lineP->toPlainText());
            if (ui->lnU) {
                this->setProperty("active_lnU", ui->lnU->toPlainText());
                this->setProperty("active_lnV", ui->lnV->toPlainText());
                this->setProperty("active_lnW", ui->lnW->toPlainText());
                this->setProperty("active_lndU", ui->lndU->toPlainText());
                this->setProperty("active_lndV", ui->lndV->toPlainText());
                this->setProperty("active_lndW", ui->lndW->toPlainText());
                this->setProperty("active_lineConform", ui->lineConform->toPlainText());
            }
        }
    }

    // ==========================================================
    // AZIONI COMUNI (Evita ripetizioni di codice!)
    // ==========================================================
    ui->glWidget->setFocus();

    // --- VALIDAZIONE COSTANTI (parametriche e implicite) ---
    if (m_constantPopupActive)
            return;

    auto constParse = [this](const QString& s, bool* ok) {
        return parseUIConstant(s, 0, 0, 0, 0, 0, 0, 0, ok);
    };
    if (!InputValidator::validateConstants(this, {
        {"A", ui->lineA->text()},
        {"B", ui->lineB->text()},
        {"C", ui->lineC->text()},
        {"D", ui->lineD->text()},
        {"E", ui->lineE->text()},
        {"F", ui->lineF->text()},
        {"S", ui->lineS->text()},
    }, constParse)) {
    return;
    }

    // Lettura delle Costanti a cascata valida per entrambe le modalità.
    auto clampConst = [this](QLineEdit* edit, float raw) -> float {
        if (raw < 0.0f)
            return m_lastValidConst.value(edit, 1.0f);
        return raw;
    };

    float valA = clampConst(ui->lineA, parseUIConstant(ui->lineA->text(), 0, 0, 0, 0, 0, 0, 0));
    float valB = clampConst(ui->lineB, parseUIConstant(ui->lineB->text(), valA, 0, 0, 0, 0, 0, 0));
    float valC = clampConst(ui->lineC, parseUIConstant(ui->lineC->text(), valA, valB, 0, 0, 0, 0, 0));
    float valD = clampConst(ui->lineD, parseUIConstant(ui->lineD->text(), valA, valB, valC, 0, 0, 0, 0));
    float valE = clampConst(ui->lineE, parseUIConstant(ui->lineE->text(), valA, valB, valC, valD, 0, 0, 0));
    float valF = clampConst(ui->lineF, parseUIConstant(ui->lineF->text(), valA, valB, valC, valD, valE, 0, 0));
    float valS = parseUIConstant(ui->lineS->text(), valA, valB, valC, valD, valE, valF, 0);

    ui->glWidget->setEquationConstants(valA, valB, valC, valD, valE, valF, valS);

    // Aggiornamento visivo degli slider (valido per entrambe le modalità)
    auto updateSlider = [this](QSlider* s, float v, bool isS) {
        bool old = s->blockSignals(true);
        int intVal = static_cast<int>(v * 100.0f);

        int newMin;
        int newMax;

        if (isS && ui->tabModeSelector->currentIndex() == 1) {
            // Modalità Ray Marching: Step Relax parte da 0 a 1 (100)
            newMin = 0;
            newMax = std::max(100, intVal);
            if (intVal < 0) intVal = 40; // Default di sicurezza a 0.4
        } else {
            // Modalità Parametrica o altri slider
            newMin = isS ? std::min(-1000, intVal) : 0;
            newMax = std::max(1000, intVal);
        }

        s->setRange(newMin, newMax);
        s->setValue(intVal);
        s->blockSignals(old);
    };

    updateSlider(ui->aSlider, valA, false);
    updateSlider(ui->bSlider, valB, false);
    updateSlider(ui->cSlider, valC, false);
    updateSlider(ui->dSlider, valD, false);
    updateSlider(ui->eSlider, valE, false);
    updateSlider(ui->fSlider, valF, false);
    updateSlider(ui->sSlider, valS, true);

    // 2.5. RIPRESA PER GLI SCRIPT
    if (ui->glWidget->getEngine()->isScriptModeActive()) {
        QString currentScript = (m_currentScriptMode == ScriptModeSurface)
                ? ui->txtScriptEditor->toPlainText()
                : m_surfaceScriptText;

        // Ri-valida lo script corrente prima di riprendere: se contiene un
        // errore non corretto NON facciamo ripartire il moto (come le equazioni).
        QString glslBody;
        QString copy = currentScript;
        QTextStream stream(&copy);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.contains(":=")) continue;
            glslBody.append(line + "\n");
        }
        glslBody = GlslTranslator::translateEquation(glslBody);

        const bool isImplicit = (ui->tabModeSelector->currentIndex() == 1);
        const bool ok = isImplicit
                ? ui->glWidget->validateAndApplyImplicitScript(glslBody)
                : ui->glWidget->validateAndApplyParametricScript(glslBody);

        if (!ok) {
            InputValidator::showShaderCompilationError(this,
                                                       "Script Compilation Error", ui->glWidget->getShaderError());
            return;
        }

        if (ui->tabModeSelector->currentIndex() == 1) {
            QString texCode = ui->lineTexture->toPlainText();
            QString dispCode = ui->lineVariations->toPlainText();

            // Controllo parentesi (popup di "Unmatched parentheses" chiaro)
            auto stripComments = [](QString s) {
                s.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
                s.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));
                return s;
            };
            if (!InputValidator::validateParentheses(this, stripComments(texCode))) return;
            if (!InputValidator::validateParentheses(this, stripComments(dispCode))) return;

            // Validazione compilazione GLSL completa per texture+displacement
            if (!ui->glWidget->validateAndApplyTextureDisplacement(texCode, dispCode)) {
                InputValidator::showShaderCompilationError(this,
                                                           "Texture/Displacement Compilation Error",
                                                           ui->glWidget->getShaderError());
                return;
            }

        } else {
            if (!ui->radioBackground->isChecked() && ui->chkBoxTexture->isChecked()) {
                QString texSrc = (m_currentScriptMode == ScriptModeTexture)
                        ? ui->txtScriptEditor->toPlainText()
                        : m_surfaceTextureScriptText;
                bool texHasLogic = texSrc.contains("return") || texSrc.contains("vec3")
                        || texSrc.contains("vec4") || texSrc.contains("mainImage");
                if (texHasLogic) {
                    if (!ui->glWidget->validateAndApplyParametricShader(texSrc)) {
                        InputValidator::showShaderCompilationError(this,
                                                                   "Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
                        return;
                    }
                    m_surfaceTextureCode = texSrc; // applicata e valida: committa
                }
            }
        }

        if (!applyBackgroundTextureIfNeeded()) return;

        {
            QString soundSrc = (m_currentScriptMode == ScriptModeSound)
                    ? ui->txtScriptEditor->toPlainText()
                    : m_soundScriptText;
            if (!soundSrc.trimmed().isEmpty()) {
                QString audioErr;
                if (!m_audioController->validateScript(soundSrc, &audioErr)) {
                    InputValidator::showShaderCompilationError(this,
                                                               "Syntax Error (Sound Script)",
                                                               audioErr.isEmpty() ? "Audio shader compilation failed." : audioErr);
                    return;
                }
                // Bozza valida: committa, così applyStartSideEffects suona questa.
                if (m_currentScriptMode == ScriptModeSound) m_soundScriptText = soundSrc;
            }
        }


        const bool applyOnly = this->property("rmApplyOnly").toBool();

        if (!applyOnly &&
                hasTimeVariable(currentScript + "\n" + m_surfaceTextureCode + "\n" + m_bgTextureCode)) {
            applyAnimationState(true);
        }

        if (!applyOnly) {
            applyStartSideEffects();
        }
        ui->glWidget->update();
        return;
    }

    // ==========================================================
    // MODALITÀ IMPLICITA (RAY MARCHING)
    // ==========================================================
    if (ui->tabModeSelector->currentIndex() == 1) {

        // 1. Lettura e validazione equazione
        QString rawEq = ui->lineEquation->toPlainText().trimmed();
        if (rawEq.isEmpty()) return;

        if (!InputValidator::validateImplicitEquation(this, rawEq)) return;
        if (!InputValidator::validateExpressionSyntax(this, rawEq, "Implicit Equation")) return;
        if (!InputValidator::validateIdentifiers(this, rawEq, "Implicit Equation")) return;

        QString implicitEqF;
        if (rawEq.contains("=")) {
            QStringList parts = rawEq.split("=");
            implicitEqF = QString("(%1) - (%2)").arg(parts[0].trimmed(), parts[1].trimmed());
        } else {
            // Aggiungiamo silenziosamente "= 0.0" se l'utente lo ha omesso, senza fastidiosi popup
            QString correctedEq = rawEq + " = 0.0";
            ui->lineEquation->blockSignals(true);
            ui->lineEquation->setPlainText(correctedEq);
            ui->lineEquation->blockSignals(false);

            implicitEqF = QString("(%1) - (0.0)").arg(rawEq);
        }

        // 2. Lettura e validazione Texture
        QString texCode = ui->lineTexture->toPlainText().trimmed();
        QString dispCode = ui->lineVariations->toPlainText().trimmed();

        if (!InputValidator::validateImplicitScriptContext(this, texCode)) return;

        auto stripComments = [](QString s) {
            s.remove(QRegularExpression(R"(//.*$)",
                                        QRegularExpression::MultilineOption));
            s.remove(QRegularExpression(R"(/\*.*?\*/)",
                                        QRegularExpression::DotMatchesEverythingOption));
            return s;
        };
        if (!InputValidator::validateParentheses(this, stripComments(texCode))) return;

        if (!InputValidator::validateParentheses(this, stripComments(dispCode))) return;

        ui->glWidget->setTextureCode(texCode);

        // TEST E APPLICAZIONE
        bool success = ui->glWidget->validateAndApplyImplicitShader(implicitEqF, texCode, dispCode);
        if (!success) {
            InputValidator::showShaderCompilationError(this, "Syntax Error (Ray Marching)", ui->glWidget->getShaderError());
            return;
        }

        QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
        QRegularExpressionMatch imgMatch = imgRe.match(texCode);
        if (imgMatch.hasMatch()) {
            QString imgPath = imgMatch.captured(1).trimmed();
            if (QFile::exists(imgPath)) {
                m_isImageMode = true;
                m_currentTexturePath = imgPath;
                ui->glWidget->loadTextureFromFile(imgPath);
            }
        } else {
            if (m_isImageMode) {
                m_isImageMode = false;
                m_currentTexturePath.clear();
                ui->glWidget->clearTexture(); // Rimuove l'immagine dalla GPU
            }
        }

        if (texCode.isEmpty() && dispCode.isEmpty()) {
            bool oldState = ui->chkBoxTexture->blockSignals(true);
            ui->chkBoxTexture->setChecked(false);
            ui->chkBoxTexture->blockSignals(oldState);
            ui->glWidget->setTextureEnabled(false);
            m_surfaceTextureState = false;
        } else {
            ui->glWidget->setTextureEnabled(true);
            m_surfaceTextureState = true;

            if (!ui->chkBoxTexture->isChecked()) {
                bool oldState = ui->chkBoxTexture->blockSignals(true);
                ui->chkBoxTexture->setChecked(true);
                ui->chkBoxTexture->blockSignals(oldState);
                updateTextureUIState(true);
            }
        }

        // 4. Animazione dinamica sicura
        bool isAnimated = hasTimeVariable(implicitEqF);
        if (ui->chkBoxTexture->isChecked()) {
            isAnimated = isAnimated || hasTimeVariable(texCode) || hasTimeVariable(dispCode);
        }
        // Anche la texture di background può essere animata (gate indipendente da chkBoxTexture)
        if (ui->glWidget->isBackgroundTextureEnabled()) {
            isAnimated = isAnimated || hasTimeVariable(m_bgTextureCode);
        }

        const bool applyOnly = this->property("rmApplyOnly").toBool();

        if (!applyOnly) {
            applyAnimationState(isAnimated);
        }
        updateMasterButtonState();

        if (ui->radioShell->isChecked()) {
            ui->glWidget->setRenderMode(1);
        } else {
            ui->glWidget->setRenderMode(0);
        }

        ui->glWidget->rebuildShader();
        if (!applyOnly) {
            applyStartSideEffects();
        }
        ui->glWidget->update();
        return;
    }


    // ==========================================================
    // MODALITÀ PARAMETRICA
    // ==========================================================

    // --- 0. SMART INTERCEPTOR ---
    QString allEqs = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
            ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText() + " " +
            ui->lineU->toPlainText() + " " + ui->lineV->toPlainText() + " " +
            ui->lineW->toPlainText();

    bool hasExplicit = !ui->lineExplicitU->toPlainText().trimmed().isEmpty() ||
            !ui->lineExplicitV->toPlainText().trimmed().isEmpty() ||
            !ui->lineExplicitW->toPlainText().trimmed().isEmpty();

    if (!InputValidator::validateWUsage(this, allEqs, hasExplicit)) return;

    // 1. SAFETY CHECK: VARIABLES AND SYNTAX
    QString mainEqs = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
            ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText();

    QString allEqsToTest = mainEqs + " " + ui->lineExplicitU->toPlainText() + " " +
            ui->lineExplicitV->toPlainText() + " " + ui->lineExplicitW->toPlainText();

    if (!InputValidator::validateParametricVariables(this, mainEqs, allEqsToTest, hasExplicit)) return;

    if (!InputValidator::validateFieldList(this, {
        {"x(u,v)",     ui->lineX->toPlainText()},
        {"y(u,v)",     ui->lineY->toPlainText()},
        {"z(u,v)",     ui->lineZ->toPlainText()},
        {"p(u,v)",     ui->lineP->toPlainText()},
        {"U-comp",     ui->lineU->toPlainText()},
        {"V-comp",     ui->lineV->toPlainText()},
        {"W-comp",     ui->lineW->toPlainText()},
        {"Explicit U", ui->lineExplicitU->toPlainText()},
        {"Explicit V", ui->lineExplicitV->toPlainText()},
        {"Explicit W", ui->lineExplicitW->toPlainText()},
    }))  return;

    // --- BLOCCO VALIDAZIONE COMPOSITION & GEODESIC FLOW ---
    QString cU = ui->lineU->toPlainText().trimmed();
    QString cV = ui->lineV->toPlainText().trimmed();
    QString cW = ui->lineW->toPlainText().trimmed();

    bool geoHasText = hasGeodesicText();

    QString composedTest = composeEquation(mainEqs, cU, cV, cW);

    if (!InputValidator::validateCompositionFields(this, mainEqs, cU, cV, cW, geoHasText, composedTest)) return;

    // --- 1. LETTURA E VALIDAZIONE DEI LIMITI ---
    bool uActive = ui->uMinEdit->isEnabled();
    bool vActive = ui->vMinEdit->isEnabled();
    bool wActive = ui->wMinEdit->isEnabled();

    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, uActive}, {ui->uMaxEdit, uActive},
        {ui->vMinEdit, vActive}, {ui->vMaxEdit, vActive},
        {ui->wMinEdit, wActive}, {ui->wMaxEdit, wActive},
    };

    QVector<float> limitValues;
    auto parseFn = [this](const QString& s, bool* ok) { return this->parseMath(s, ok); };
    if (!InputValidator::validateAndParseLimits(this, limitFields, parseFn, limitValues)) return;

    float uMin = limitValues[0], uMax = limitValues[1];
    float vMin = limitValues[2], vMax = limitValues[3];
    float wMin = limitValues[4], wMax = limitValues[5];

    // Controllo distrazione dell'utente (Min >= Max) solo sulle variabili in uso!
    if (!InputValidator::validateLimits(this, uMin, uMax, uActive, vMin, vMax, vActive, wMin, wMax, wActive)) return;

    // --- AGGIORNAMENTO UI: Svuota e disabilita i limiti W in modalità Composition ---
    bool has_U = mainEqs.contains(QRegularExpression("\\bU\\b"));
    bool has_V = mainEqs.contains(QRegularExpression("\\bV\\b"));
    bool has_W = mainEqs.contains(QRegularExpression("\\bW\\b"));
    int upperCount = (has_U ? 1 : 0) + (has_V ? 1 : 0) + (has_W ? 1 : 0);

    bool isCompositionActive = ((upperCount > 0) || !cU.isEmpty() || !cV.isEmpty() || !cW.isEmpty()) && !geoHasText;
    bool isGeodesicActive = (upperCount > 0) && geoHasText;

    if (isCompositionActive) {
        bool b1 = ui->wMinEdit->blockSignals(true);
        bool b2 = ui->wMaxEdit->blockSignals(true);

        ui->wMinEdit->clear();
        ui->wMaxEdit->clear();
        ui->wMinEdit->setEnabled(false);
        ui->wMaxEdit->setEnabled(false);

        ui->wMinEdit->blockSignals(b1);
        ui->wMaxEdit->blockSignals(b2);
    }

    if (isGeodesicActive) {
        // 1. Se il campo del fattore conforme è vuoto, forziamo il default "1.0"
        if (ui->lineConform->toPlainText().trimmed().isEmpty()) {
            bool oldBlock = ui->lineConform->blockSignals(true);
            ui->lineConform->setPlainText("1.0");
            ui->lineConform->blockSignals(oldBlock);
        }

        // 2. Controllo Geometria Euclidea delegato al Validator
        bool isPreset = this->property("isPresetActive").toBool();
        if (sender() == m_btnStart && !isPreset) {
            InputValidator::validateGeodesicConformalFactor(
                        this,
                        ui->lineX->toPlainText(), ui->lineY->toPlainText(),
                        ui->lineZ->toPlainText(), ui->lineP->toPlainText(),
                        ui->lineConform->toPlainText(),
                        true
                        );
        }

        if (!InputValidator::validateFieldList(this, {
            {"x(U,V,W)",         ui->lineX->toPlainText()},
            {"y(U,V,W)",         ui->lineY->toPlainText()},
            {"z(U,V,W)",         ui->lineZ->toPlainText()},
            {"p(U,V,W)",         ui->lineP->toPlainText()},
            {"u(t)",             ui->lnU->toPlainText()},
            {"v(t)",             ui->lnV->toPlainText()},
            {"w(t)",             ui->lnW->toPlainText()},
            {"du/dt",            ui->lndU->toPlainText()},
            {"dv/dt",            ui->lndV->toPlainText()},
            {"dw/dt",            ui->lndW->toPlainText()},
        {"Conformal Factor", ui->lineConform->toPlainText()},
    })) return;

        QString geoEqs = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
                ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText() + " " +
                ui->lnU->toPlainText() + " " + ui->lnV->toPlainText() + " " + ui->lnW->toPlainText() + " " +
                ui->lndU->toPlainText() + " " + ui->lndV->toPlainText() + " " + ui->lndW->toPlainText()+
                ui->lineConform->toPlainText();

        if (ui->chkBoxTexture->isChecked()) geoEqs += " " + m_surfaceTextureCode;
        if (ui->radioBackground->isChecked() || ui->glWidget->isBackgroundTextureEnabled()) geoEqs += " " + m_bgTextureCode;

        // updateGeodesicMesh() calcola, verifica e restituisce false se i dati sono corrotti
        if (!updateGeodesicMesh()) {
            if (this->property("geoErrorType").toString() == "singularity"
                    && !property("geoErrorShown").toBool()) {
                setProperty("geoErrorShown", true);
                InputValidator::showGeodesicSingularityError(this);
            }
            return;
        }

        applyAnimationState(hasTimeVariable(geoEqs));
        applyStartSideEffects();
        ui->glWidget->update();
        return;
    }

    QString pEq = ui->lineP->toPlainText().trimmed();
    bool isSurface4D = !pEq.isEmpty() && pEq != "0" && pEq != "0.0";

    if (isSurface4D) {
        if (std::abs(ui->glWidget->getOmega()) < 0.0001f &&
                std::abs(ui->glWidget->getPhi()) < 0.0001f &&
                std::abs(ui->glWidget->getPsi()) < 0.0001f)
        {
            float safety = 0.0001f;
            ui->glWidget->setRotation4D(safety, safety, safety);
        }
    }

    bool wasCustomTexture = m_isCustomMode;
    QString currentScript;
    if (m_currentScriptMode == ScriptModeTexture && !ui->radioBackground->isChecked()) {
        currentScript = ui->txtScriptEditor->toPlainText();
        m_surfaceTextureCode = currentScript;
        m_surfaceTextureScriptText = currentScript;
    } else {
        currentScript = m_surfaceTextureCode;
    }

    ui->glWidget->setScriptCheck(false);

    // 2. EQUATIONS E CONSTRAINTS
    QString defU = ui->lineU->toPlainText();
    QString defV = ui->lineV->toPlainText();
    QString defW = ui->lineW->toPlainText();

    QString rawX = composeEquation(ui->lineX->toPlainText(), defU, defV, defW);
    QString rawY = composeEquation(ui->lineY->toPlainText(), defU, defV, defW);
    QString rawZ = composeEquation(ui->lineZ->toPlainText(), defU, defV, defW);
    QString rawP = composeEquation(ui->lineP->toPlainText(), defU, defV, defW);

    QString xEq = GlslTranslator::translateEquation(rawX);
    QString yEq = GlslTranslator::translateEquation(rawY);
    QString zEq = GlslTranslator::translateEquation(rawZ);
    QString wEq = GlslTranslator::translateEquation(rawP);

    QString rawU = composeEquation(ui->lineExplicitU->toPlainText(), defU, defV, defW).trimmed();
    QString rawV = composeEquation(ui->lineExplicitV->toPlainText(), defU, defV, defW).trimmed();
    QString rawW = composeEquation(ui->lineExplicitW->toPlainText(), defU, defV, defW).trimmed();

    if (!rawU.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
        ui->glWidget->getEngine()->setExplicitU(GlslTranslator::translateEquation(rawU));
    }
    else if (!rawV.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
        ui->glWidget->getEngine()->setExplicitV(GlslTranslator::translateEquation(rawV));
    }
    else if (!rawW.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
        ui->glWidget->getEngine()->setExplicitW(GlslTranslator::translateEquation(rawW));
    }
    else {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
        ui->glWidget->getEngine()->setExplicitU("");
        ui->glWidget->getEngine()->setExplicitV("");
        ui->glWidget->getEngine()->setExplicitW("");
    }

    {
        auto probeEquation = [&](const QString& eq) -> bool {
            if (eq.trimmed().isEmpty()) return true;  // P vuoto è lecito

            ExpressionParser p;
            double pu = 0.0, pv = 0.0, pw = 0.0, pp_ = 0.0;
            p.setupVariables<double>(pu, pv, pw, pp_);
            p.setupConstants<double>(
                        (double)valA, (double)valB, (double)valC, (double)valD,
                        (double)valE, (double)valF, (double)valS);

            if (!p.compile(eq)) {
                // Errore di sintassi: lo gestisce il controllo successivo.
                return true;
            }

            constexpr int N = 48;  // griglia più fitta: intercetta i poli vicini ai bordi
            QVector<double> mags;
            mags.reserve((N + 1) * (N + 1));
            double maxMag = 0.0;

            for (int i = 0; i <= N; ++i) {
                for (int j = 0; j <= N; ++j) {
                    pu = (double)uMin + ((double)uMax - (double)uMin) * i / (double)N;
                    pv = (double)vMin + ((double)vMax - (double)vMin) * j / (double)N;
                    double val = p.value();

                    // 1) inf / NaN espliciti (1/0, log(0), ...)
                    if (!std::isfinite(val)) return false;

                    // 2) tetto assoluto di sicurezza (oltre la portata float32 GPU)
                    if (std::abs(val) > kMaxRenderableMagnitude) return false;

                    double m = std::abs(val);
                    maxMag = std::max(maxMag, m);
                    mags.push_back(m);
                }
            }

            // 3) Picco RELATIVO alla scala della superficie. La mediana è una
            //    stima robusta della scala "tipica"; se il massimo la supera di
            //    troppo è un polo/spike che farà appiattire il render.
            if (!mags.isEmpty()) {
                std::nth_element(mags.begin(), mags.begin() + mags.size() / 2, mags.end());
                double medianMag = mags[mags.size() / 2];
                if (medianMag > 1e-9 && maxMag > kSpikeRatio * medianMag)
                    return false;
            }
            return true;
        };

        if (!probeEquation(rawX) ||
                !probeEquation(rawY) ||
                !probeEquation(rawZ) ||
                !probeEquation(rawP))
        {
            if (!property("collapseErrorShown").toBool()) {
                setProperty("collapseErrorShown", true);
                InputValidator::showMathematicalCollapseError(this);
            }
            return;
        }
    }

    bool success = ui->glWidget->setParametricEquations(xEq, yEq, zEq, wEq);

    if (!success) {
        InputValidator::showEquationSyntaxError(this);
        return;
    }

    // 3. READ VALUES
    ui->glWidget->setRangeU(uMin, uMax);
    ui->glWidget->setRangeV(vMin, vMax);
    ui->glWidget->setRangeW(wMin, wMax);

    QString rawEqsForT = ui->lineX->toPlainText() + " " +
            ui->lineY->toPlainText() + " " +
            ui->lineZ->toPlainText() + " " +
            ui->lineP->toPlainText() + " " +
            ui->lineExplicitU->toPlainText() + " " +
                         ui->lineExplicitV->toPlainText() + " " +
                         ui->lineExplicitW->toPlainText() + " " +
                         ui->lineU->toPlainText() + " " +
                         ui->lineV->toPlainText() + " " +
                         ui->lineW->toPlainText() + " " +
                         m_surfaceTextureCode + " " +
            m_bgTextureCode;

    bool isSurfTexEnabled = ui->radioBackground->isChecked() ? m_surfaceTextureState : ui->chkBoxTexture->isChecked();
    if (isSurfTexEnabled && wasCustomTexture && !currentScript.isEmpty()) {

        if (!ui->glWidget->loadCustomShader(currentScript)) {
            InputValidator::showShaderCompilationError(this,
                                                       "Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
            return;
        }
    }

    if (ui->glWidget->isBackgroundTextureEnabled()) {
        QString bgSrc = m_bgTextureScriptText;
        bool bgHasLogic = bgSrc.contains("return") || bgSrc.contains("vec3")
                || bgSrc.contains("vec4") || bgSrc.contains("mainImage");
        if (bgHasLogic) {
            if (!ui->glWidget->validateAndApplyBackgroundShader(bgSrc)) {
                InputValidator::showShaderCompilationError(this,
                                                           "Syntax Error (Background Texture)", ui->glWidget->getShaderError());
                return;
            }
            m_bgTextureCode = bgSrc; // valida: committa
        }
    }

    const bool applyOnly = this->property("rmApplyOnly").toBool();
    applyAnimationState(applyOnly ? false : hasTimeVariable(rawEqsForT));
    updateMasterButtonState();

    // 4. REPAINT E VALIDAZIONE GEOMETRIA
    ui->glWidget->updateSurfaceData(); // Calcola la mesh

    // Controllo Collasso Matematico
    if (!ui->glWidget->getEngine()->isMeshValid()) {
        if (!property("collapseErrorShown").toBool()) {
            setProperty("collapseErrorShown", true);
            InputValidator::showMathematicalCollapseError(this);
        }
        return;
    }

    if (!applyOnly) applyStartSideEffects();
    setProperty("collapseErrorShown", false);  // superficie valida: riarma il popup di collasso
    ui->glWidget->update();
}

void MainWindow::onStopClicked() {
    bool isRunning = ui->glWidget->isAnimating();

    if (isRunning) {
        ui->glWidget->pauseMotion();
        if (ui->btnStart_2) ui->btnStart_2->setText("GO");
    } else {
        bool hasSpeed = (std::abs(ui->glWidget->getNutationSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getPrecessionSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getSpinSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getPhiSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getPsiSpeed()) > 0.001f);

        if (!hasSpeed) return;

        ui->glWidget->resumeMotion();
        if (ui->btnStart_2) ui->btnStart_2->setText("STOP");
    }

    // Aggiornamento centralizzato per entrambi i rami
    updateMasterButtonState();
    update4DButtonState();
}

void MainWindow::onResetViewClicked()
{
    // 1. Ferma SOLO i percorsi (Path) che alterano esplicitamente la telecamera
    if (pathTimer->isActive()) pathTimer->stop();
    if (pathTimer3D->isActive()) pathTimer3D->stop();

    pathTimeT = 0.0f;
    pathTimeT3D = 0.0f;

    ui->btnDeparture->setText("DEPARTURE");
    ui->btnDeparture3D->setText("DEPARTURE");

    checkPathFields();
    checkPath3DFields();

    // 2. Il motore ora resetta SOLO la vista spaziale (angoli e posizione),
    // senza uccidere il tempo 't' e senza azzerare le velocità di rotazione!
    ui->glWidget->resetTransformations();

    // 3. Aggiorna in sicurezza la mesh
    checkAndTriggerMeshUpdate();

    // 4. Sincronizza il pulsante principale (che rimarrà su STOP se la superficie o le rotazioni stanno andando)
    updateMasterButtonState();
}

void MainWindow::onNavTimerTick()
{
    if (activeNavActions.isEmpty()) return;

    for (int action : activeNavActions) {
        ui->glWidget->virtualMove(static_cast<GLWidget::MoveDir>(action), m_pathSpeed3D, m_pathSpeed4D);
    }

    checkAndTriggerMeshUpdate();
}

void MainWindow::onDepartureClicked()
{
    // CASO 1: VOGLIAMO FERMARE
    if (pathTimer->isActive()) {
        pathTimer->stop();
        ui->btnDeparture->setText("DEPARTURE");
        checkPathFields();
        updateMasterButtonState();
        return;
    }

    // CASO 2: VOGLIAMO PARTIRE
    if (pathTimer3D->isActive()) {
        pathTimer3D->stop();
        ui->btnDeparture3D->setText("DEPARTURE");
    }

    // Funzione helper per pulire l'input
    auto getSafeEq = [](QLineEdit* line) {
        QString t = line->text().trimmed();
        if (t.isEmpty()) return QString("0");
        return t.replace(",", ".");
    };

    // Recupera equazioni pulite
    QString eqX = getSafeEq(ui->lineX_P);
    QString eqY = getSafeEq(ui->lineY_P);
    QString eqZ = getSafeEq(ui->lineZ_P);
    QString eqP = getSafeEq(ui->lineP_P);

    // Opzionali
    QString eqAlpha = getSafeEq(ui->lineAlpha_P);
    QString eqBeta  = getSafeEq(ui->lineBeta_P);
    QString eqGamma = getSafeEq(ui->lineGamma_P);

    if (!InputValidator::validateFieldList(this, {
        {"X(t)", eqX},
        {"Y(t)", eqY},
        {"Z(t)", eqZ},
        {"W(t)", eqP},
        {"Alpha(t)", eqAlpha},
        {"Beta(t)", eqBeta},
        {"Gamma(t)", eqGamma}
    })) {
        return; // Ferma l'avvio se c'è un errore matematico
    }

    // Compila
    bool ok = ui->glWidget->getEngine()->compilePathEquations(eqX, eqY, eqZ, eqP, eqAlpha, eqBeta, eqGamma);

    if (!ok) {
        QMessageBox::warning(this, "Error", "Path 4D compilation error .\nCheck the syntax.");
        return;
    }

    pathTimer->start();
    ui->btnDeparture->setText("STOP");

    updateMasterButtonState();
}

void MainWindow::onPathTimerTick()
{
    if (!pathTimer->isActive()) return;

    // 1. SETUP BASE
    float surfaceScale = ui->glWidget->getSurfaceScale();
    float advanceStep = m_pathSpeed4D;

    pathTimeT += advanceStep;
    float dt = 0.01f;

    SurfaceEngine* engine = ui->glWidget->getEngine();

    // 2. VALUTAZIONE POSIZIONE E TANGENTE
    QVector4D p_prev = engine->evaluatePathPosition(pathTimeT - dt);
    QVector4D p_curr = engine->evaluatePathPosition(pathTimeT);
    QVector4D p_next = engine->evaluatePathPosition(pathTimeT + dt);

    QVector4D velocity = p_next - p_prev;
    QVector4D V = (velocity.lengthSquared() > 1e-8f) ? velocity.normalized() : QVector4D(0, 1, 0, 0);

    // 3. RECUPERO ANGOLI (Alpha, Beta, Gamma)
    float alpha = engine->evaluatePathAlpha(pathTimeT);
    float beta  = engine->evaluatePathBeta(pathTimeT);
    float gamma = engine->evaluatePathGamma(pathTimeT);

    // 4. CALCOLO BASE ORTONORMALE LOCALE (N1, N2, N3)
    QVector4D N1, N2, N3;
    QVector4D finalPos4D, finalTarget4D, finalUp4D;

    if (m_pathMode == ModeTangential) {
        QVector4D K(0.0f, 0.0f, 1.0f, 0.0f);
        N1 = K - V * QVector4D::dotProduct(K, V);
        if (N1.lengthSquared() > 1e-6f) N1.normalize();
        else {
            QVector4D Y(0.0f, 1.0f, 0.0f, 0.0f);
            N1 = (Y - V * QVector4D::dotProduct(Y, V)).normalized();
        }

        QVector3D v3 = V.toVector3D();
        QVector3D n13 = N1.toVector3D();
        QVector3D side3 = QVector3D::crossProduct(v3, n13);

        if (side3.lengthSquared() > 1e-6f) {
            N2 = QVector4D(side3, 0.0f).normalized();
            N2 = N2 - V * QVector4D::dotProduct(N2, V) - N1 * QVector4D::dotProduct(N2, N1);
            N2.normalize();
        } else {
            QVector4D I(1.0f, 0.0f, 0.0f, 0.0f);
            N2 = I - V * QVector4D::dotProduct(I, V) - N1 * QVector4D::dotProduct(I, N1);
            N2.normalize();
        }
        finalPos4D = p_curr - V * 0.2f;
        finalTarget4D = p_next;
    } else {
        finalPos4D = p_curr;
        finalTarget4D = QVector4D(0,0,0,0);
        QVector4D viewDir = (finalTarget4D - finalPos4D);
        V = (viewDir.lengthSquared() > 1e-8f) ? viewDir.normalized() : QVector4D(0,0,-1,0);
        N1 = QVector4D(0,0,1,0);

        QVector4D globalX(1,0,0,0);
        N2 = globalX - V * QVector4D::dotProduct(globalX, V) - N1 * QVector4D::dotProduct(globalX, N1);
        N2.normalize();
    }

    // Calcolo N3 (Ana)
    float dx =  det3x3(V.y(), V.z(), V.w(),  N1.y(), N1.z(), N1.w(),  N2.y(), N2.z(), N2.w());
    float dy = -det3x3(V.x(), V.z(), V.w(),  N1.x(), N1.z(), N1.w(),  N2.x(), N2.z(), N2.w());
    float dz =  det3x3(V.x(), V.y(), V.w(),  N1.x(), N1.y(), N1.w(),  N2.x(), N2.y(), N2.w());
    float dw = -det3x3(V.x(), V.y(), V.z(),  N1.x(), N1.y(), N1.z(),  N2.x(), N2.y(), N2.z());
    N3 = QVector4D(dx, dy, dz, dw).normalized();

    // Composizione Orientamento Locale
    float ca = std::cos(alpha), sa = std::sin(alpha);
    float cb = std::cos(beta),  sb = std::sin(beta);
    float cg = std::cos(gamma), sg = std::sin(gamma);

    float c1 = ca * cb;
    float c2 = sa * cg - ca * sb * sg;
    float c3 = sa * sg + ca * sb * cg;

    finalUp4D = N1 * c1 + N2 * c2 + N3 * c3;
    finalUp4D.normalize();

    // =========================================================================
    // >>> SINCRONIZZATO BETA + GAMMA <<<
    // =========================================================================

    // 1. Definiamo le rotazioni globali per compensare
    float rotOmega = 0.0f;     // X-W (Opzionale)
    float rotPhi   = -gamma;   // Y-W (Fix per Gamma)
    float rotPsi   = -beta;    // Z-W (Fix per Beta)

    // 2. Aggiorniamo la GPU (Shader)
    ui->glWidget->setRotation4D(rotOmega, rotPhi, rotPsi);

    // 3. Funzione helper per ruotare la CPU Camera
    auto transformCPU = [&](QVector4D v) {
        // A. Rotazione YW (Phi / Gamma Fix)
        if (std::abs(rotPhi) > 1e-6f) {
            float c = std::cos(rotPhi);
            float s = std::sin(rotPhi);
            float y = v.y();
            float w = v.w();
            v.setY( y * c + w * s);
            v.setW(-y * s + w * c);
        }
        // B. Rotazione ZW (Psi / Beta Fix)
        if (std::abs(rotPsi) > 1e-6f) {
            float c = std::cos(rotPsi);
            float s = std::sin(rotPsi);
            float z = v.z();
            float w = v.w();
            v.setZ( z * c + w * s);
            v.setW(-z * s + w * c);
        }
        return v;
    };

    // 4. Applichiamo la trasformazione ai vettori camera
    QVector4D rotPos    = transformCPU(finalPos4D);
    QVector4D rotTarget = transformCPU(finalTarget4D);
    QVector4D rotUp     = transformCPU(finalUp4D);

    // 5. Invio finale
    ui->glWidget->setCameraFrom4DVectors(rotPos, rotTarget, rotUp);
}

void MainWindow::checkPathFields()
{
    int filled = 0;
    if (!ui->lineX_P->text().trimmed().isEmpty()) filled++;
    if (!ui->lineY_P->text().trimmed().isEmpty()) filled++;
    if (!ui->lineZ_P->text().trimmed().isEmpty()) filled++;
    if (!ui->lineP_P->text().trimmed().isEmpty()) filled++;

    // --- AGGIUNTA VARIABILI ANGOLARI ---
    if (!ui->lineAlpha_P->text().trimmed().isEmpty()) filled++;
    if (!ui->lineBeta_P->text().trimmed().isEmpty()) filled++;
    if (!ui->lineGamma_P->text().trimmed().isEmpty()) filled++;

    if (pathTimer->isActive()) {
        ui->btnDeparture->setEnabled(true);
    } else {
        ui->btnDeparture->setEnabled(filled >= 2);
    }
}

void MainWindow::onDeparture3DClicked()
{
    // CASO 1: STOP
    if (pathTimer3D->isActive()) {
        pathTimer3D->stop();
        ui->btnDeparture3D->setText("DEPARTURE");
        checkPath3DFields();
        updateMasterButtonState();
        return;
    }

    // CASO 2: START
    if (pathTimer->isActive()) {
        pathTimer->stop();
        ui->btnDeparture->setText("DEPARTURE");
    }

    auto getSafeEq = [](QLineEdit* line) {
        QString t = line->text().trimmed();
        if (t.isEmpty()) return QString("0");
        return t.replace(",", ".");
    };

    QString eqX = getSafeEq(ui->lineX_P3D);
    QString eqY = getSafeEq(ui->lineY_P3D);
    QString eqZ = getSafeEq(ui->lineZ_P3D);
    QString eqR = getSafeEq(ui->lineR_P3D);

    if (!InputValidator::validateFieldList(this, {
    {"X(t)", eqX},
        {"Y(t)", eqY},
        {"Z(t)", eqZ},
        {"Roll(t)", eqR}
    })) {
        return;
    }

    bool ok = ui->glWidget->getEngine()->compilePath3DEquations(eqX, eqY, eqZ, eqR);
    if (!ok) {
        QMessageBox::warning(this, "Error", "4D path compilation error.\nCheck the syntax.");
        return;
    }

    pathTimer3D->start();
    ui->btnDeparture3D->setText("STOP");

    updateMasterButtonState();
}

void MainWindow::onPath3DTimerTick()
{
    if (!pathTimer3D->isActive()) return;

    // Recuperiamo la scala dinamicamente
    float surfaceScale = ui->glWidget->getSurfaceScale();

    float dt = m_pathSpeed3D;

    pathTimeT3D += dt;

    QVector4D rawData = ui->glWidget->getEngine()->evaluatePath3DPosition(pathTimeT3D);

    // Scala la posizione (XYZ) ma NON il rollio (W)
    QVector3D currentPos = rawData.toVector3D();
    float currentRoll = rawData.w();

    QVector3D target;

    if (m_pathMode == ModeTangential) {
        float delta = 0.1f;
        QVector4D futureData = ui->glWidget->getEngine()->evaluatePath3DPosition(pathTimeT3D + delta);
        target = futureData.toVector3D();
    } else {
        target = QVector3D(0, 0, 0);
    }

    ui->glWidget->setCameraPosAndDirection3D(currentPos, target, currentRoll);
}

void MainWindow::checkPath3DFields()
{
    int filled = 0;
    if (!ui->lineX_P3D->text().trimmed().isEmpty()) filled++;
    if (!ui->lineY_P3D->text().trimmed().isEmpty()) filled++;
    if (!ui->lineZ_P3D->text().trimmed().isEmpty()) filled++;
    if (!ui->lineR_P3D->text().trimmed().isEmpty()) filled++;

    if (pathTimer3D->isActive()) {
        ui->btnDeparture3D->setEnabled(true);
    } else {
        ui->btnDeparture3D->setEnabled(filled >= 2);
    }
}

void MainWindow::onToggleViewClicked()
{
    if (m_pathMode == ModeTangential) {
        m_pathMode = ModeCentered;
        ui->pushView->setText("Center View");
        ui->pushView3D->setText("Center View");
    } else {
        m_pathMode = ModeTangential;
        ui->pushView->setText("Tangent View");
        ui->pushView3D->setText("Tangent View");
    }
}


// ==========================================================
// SCRIPTING ENGINE
// ==========================================================

void MainWindow::onToggleScriptMode()
{
    // 1. Salva il testo che l'utente ha appena scritto nella variabile correnta
    QString currentText = ui->txtScriptEditor->toPlainText();
    if (m_currentScriptMode == ScriptModeSurface) m_surfaceScriptText = currentText;
    else if (m_currentScriptMode == ScriptModeTexture) {
        if (ui->radioBackground->isChecked()) m_bgTextureScriptText = currentText;
        else m_surfaceTextureScriptText = currentText;
    }
    else if (m_currentScriptMode == ScriptModeSound) m_soundScriptText = currentText;

    // 2. Passa alla modalità successiva (0 -> 1 -> 2 -> 0)
    m_currentScriptMode = static_cast<ScriptMode>((m_currentScriptMode + 1) % 3);

    // 3. Ripristina il testo senza innescare eventi indesiderati
    ui->txtScriptEditor->blockSignals(true);

    if (m_currentScriptMode == ScriptModeSurface) {
        ui->txtScriptEditor->setPlainText(m_surfaceScriptText);
        ui->btnRunCurrentScript->setEnabled(false);
    } else if (m_currentScriptMode == ScriptModeTexture) {
        if (ui->radioBackground->isChecked()) {
            ui->txtScriptEditor->setPlainText(m_bgTextureScriptText);
        } else {
            ui->txtScriptEditor->setPlainText(m_surfaceTextureScriptText);
        }
    } else if (m_currentScriptMode == ScriptModeSound) {
        ui->txtScriptEditor->setPlainText(m_soundScriptText);
    }

    ui->txtScriptEditor->blockSignals(false);

    // 4. DELEGA tutta la gestione dei tasti e delle label alla funzione centralizzata!
    updateScriptButtonText();
}

void MainWindow::onRunCurrentScript()
{
    // 1. Estrae il testo correntemente scritto nell'editor
    QString currentText = ui->txtScriptEditor->toPlainText();

    // --- CONTROLLO DI SICUREZZA (WRONG MODE BLOCK) DELEGATO ---
    // Passiamo l'enum castato a int
    if (!InputValidator::validateScriptModeContext(this, static_cast<int>(m_currentScriptMode), currentText)) {
        ui->txtScriptEditor->clear();
        return;
    }

    // 2. Salva e avvia in base alla modalità attuale
    if (m_currentScriptMode == ScriptModeSurface) {
        if (ui->btnRunCurrentScript->text().startsWith("Stop")) {
            if (ui->glWidget) ui->glWidget->setSurfaceAnimating(false);
            updateMasterButtonState();
            return;
        }

        bool isImplicitTab = (ui->tabModeSelector->currentIndex() == 1);

        // Analisi del "DNA" dello script
        bool hasParametricReturn = currentText.contains(QRegularExpression(R"(return\s+vec[34]\b)"));
        bool hasParametricLimits = currentText.contains("u_min") || currentText.contains("v_min") || currentText.contains("w_min");
        bool seemsParametric = hasParametricReturn || hasParametricLimits;

        bool usesPointP = currentText.contains(QRegularExpression(R"(\bp\b)"));
        bool hasImplicitReturn = !hasParametricReturn && (usesPointP || currentText.contains("length("));
        bool seemsImplicit = hasImplicitReturn;

        bool modeSwitched = false;

        if (isImplicitTab && seemsParametric) {
            modeSwitched = true;
            ui->tabModeSelector->setCurrentIndex(0); // Passa a Parametrico
        }
        else if (!isImplicitTab && seemsImplicit) {
            modeSwitched = true;
            ui->tabModeSelector->setCurrentIndex(1); // Passa a Ray Marching
        }

        // Se abbiamo cambiato scheda, il segnale 'currentChanged' ha appena svuotato l'editor.
        // Dobbiamo ripristinare il testo salvato prima di procedere con la compilazione!
        if (modeSwitched) {
            m_surfaceScriptText = currentText;
            bool oldBlock = ui->txtScriptEditor->blockSignals(true);
            ui->txtScriptEditor->setPlainText(currentText);
            ui->txtScriptEditor->blockSignals(oldBlock);

            // Aggiorniamo la variabile di stato per il ramo di esecuzione qui sotto
            isImplicitTab = (ui->tabModeSelector->currentIndex() == 1);
        }

        // BIFORCAZIONE TRA RAY MARCHING (IMPLICIT) E PARAMETRIC
        m_surfaceScriptText = currentText;
        this->setProperty("rawSurfaceScript", currentText);

        if (ui->tabModeSelector->currentIndex() == 1) {
            // --- RAMO 1: SCRIPT IMPLICITO (RAY MARCHING) MULTI-RIGA ---

            // 1. PRIMA LINEA DI DIFESA: Sanity Check Testuale Minimalista
            QString cleanCode = currentText;
            cleanCode.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
            cleanCode.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));

            // DELEGA LA VALIDAZIONE E BLOCCA SE FALLISCE
            if (!InputValidator::validateImplicitScriptReturn(this, cleanCode)) {
                return;
            }

            QString glslBody;
            QString scriptCopy = currentText;
            QTextStream stream(&scriptCopy);
            while (!stream.atEnd()) {
                QString line = stream.readLine();
                if (line.contains(":=")) continue;
                glslBody.append(line + "\n");
            }
            glslBody = GlslTranslator::translateEquation(glslBody);

            ui->glWidget->setRaySteps(ui->stepSlider->value());

            // 2. SECONDA LINEA DI DIFESA: dry-run del fragment implicito.
            if (!ui->glWidget->validateAndApplyImplicitScript(glslBody)) {
                InputValidator::showShaderCompilationError(this,
                                                           "Script Compilation Error",
                                                           ui->glWidget->getShaderError());

                return;
            }

            parseAndApplyScriptParams(currentText, false);

            bool oldX = ui->lineX->blockSignals(true); ui->lineX->clear(); ui->lineX->blockSignals(oldX);
            bool oldY = ui->lineY->blockSignals(true); ui->lineY->clear(); ui->lineY->blockSignals(oldY);
            bool oldZ = ui->lineZ->blockSignals(true); ui->lineZ->clear(); ui->lineZ->blockSignals(oldZ);
            bool oldP = ui->lineP->blockSignals(true); ui->lineP->clear(); ui->lineP->blockSignals(oldP);

            // TUTTO OK: ABILITIAMO IL TASTO SALVA
            ui->btnSaveScript->setEnabled(true);

            ui->lineEquation->blockSignals(true);
            ui->lineEquation->setPlainText("// Controlled by Script");
            ui->lineEquation->blockSignals(false);

            m_masterStopped = false;
            if (ui->glWidget) {
                ui->glWidget->setSurfaceAnimating(hasTimeVariable(currentText));
            }
            updateMasterButtonState();
            ui->glWidget->update();

        } else {
            // --- RAMO 2: SCRIPT PARAMETRICO STANDARD ---
            onRunScriptClicked();
        }

    } else if (m_currentScriptMode == ScriptModeTexture) {
        if (ui->btnRunCurrentScript->text().startsWith("Stop")) {
            if (ui->glWidget) {
                if (ui->radioBackground->isChecked())
                    ui->glWidget->setBackgroundTextureAnimating(false);
                else
                    ui->glWidget->setSurfaceTextureAnimating(false);
            }
            updateMasterButtonState();   // riallinea i pulsanti -> "Run ..."
            return;
        }

        if (ui->radioBackground->isChecked()) m_bgTextureScriptText = currentText;
        else m_surfaceTextureScriptText = currentText;
        onApplyTextureScriptClicked();

    } else if (m_currentScriptMode == ScriptModeSound) {
        m_soundScriptText = currentText;

        // Prendi il testo visibile pulito come base
        QString targetText = ui->radioBackground->isChecked() ? m_bgTextureScriptText : m_surfaceTextureScriptText;

        targetText.remove(QRegularExpression(R"(^\s*//(SYNTH|MUSIC):.*$\n?)", QRegularExpression::MultilineOption));
        targetText.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));

        QString finalCode = targetText.trimmed();

        if (!m_soundScriptText.isEmpty()) {
            if (m_soundScriptText.startsWith("//MUSIC:")) {
                finalCode = m_soundScriptText + "\n" + finalCode;
            } else if (m_soundScriptText.contains("//SOUND_BEGIN")) {
                finalCode = m_soundScriptText + "\n\n" + finalCode;
            } else {
                finalCode = "//SOUND_BEGIN\n" + m_soundScriptText + "\n//SOUND_END\n\n" + finalCode;
            }
        }

        // Salviamo il codice combinato per la compilazione, ma NON lo passiamo all'editor visivo!
        if (ui->radioBackground->isChecked()) m_bgTextureCode = finalCode;
        else m_surfaceTextureCode = finalCode;

        onRunSoundClicked();
    }

    updateScriptButtonText();
}

void MainWindow::onRunScriptClicked()
{
    QString fullText = ui->txtScriptEditor->toPlainText();
    if (fullText.trimmed().isEmpty()) return;

    // 1. PRIMA LINEA DI DIFESA: Evita che testo spazzatura faccia crashare il parser
    QString cleanCode = fullText;
    cleanCode.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
    cleanCode.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));

    // DELEGA LA VALIDAZIONE E BLOCCA SE FALLISCE
    if (!InputValidator::validateParametricScriptReturn(this, cleanCode)) {
        return;
    }

    this->setProperty("rawSurfaceScript", fullText);
    parseAndApplyScriptParams(fullText, false);

    QString glslBody;
    QTextStream stream(&fullText);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.contains(":=")) continue;
        glslBody.append(line + "\n");
    }
    glslBody = GlslTranslator::translateEquation(glslBody);

    ui->glWidget->setResolution(ui->stepSlider->value());
    float valA = ui->aSlider->value() / 100.0f;
    float valB = ui->bSlider->value() / 100.0f;
    float valC = ui->cSlider->value() / 100.0f;
    float valD = ui->dSlider->value() / 100.0f;
    float valE = ui->eSlider->value() / 100.0f;
    float valF = ui->fSlider->value() / 100.0f;
    float valS = ui->sSlider->value() / 100.0f;
    ui->glWidget->setEquationConstants(valA, valB, valC, valD, valE, valF, valS);

    // 2. SECONDA LINEA DI DIFESA: dry-run del vertex shader script
    if (!ui->glWidget->validateAndApplyParametricScript(glslBody)) {
        InputValidator::showShaderCompilationError(this,
                                                   "Script Compilation Error",
                                                   ui->glWidget->getShaderError());

        return;
    }

    // 3. TERZA LINEA DI DIFESA: Controllo Esecuzione e Collasso Matematico
    ui->glWidget->updateSurfaceData();

    if (!ui->glWidget->getEngine()->isMeshValid()) {
        performMasterStop();
        InputValidator::showMathematicalCollapseError(this);

        ui->glWidget->getEngine()->setScriptMode(false);
        ui->glWidget->getEngine()->setScriptCodeGLSL("");
        ui->glWidget->rebuildShader();
        ui->glWidget->updateSurfaceData();

        return;
    }

    // TUTTO OK: ABILITIAMO IL TASTO SALVA
    ui->btnSaveScript->setEnabled(true);

    // Tutto OK
    bool oldX = ui->lineX->blockSignals(true); ui->lineX->clear(); ui->lineX->blockSignals(oldX);
    bool oldY = ui->lineY->blockSignals(true); ui->lineY->clear(); ui->lineY->blockSignals(oldY);
    bool oldZ = ui->lineZ->blockSignals(true); ui->lineZ->clear(); ui->lineZ->blockSignals(oldZ);
    bool oldP = ui->lineP->blockSignals(true); ui->lineP->clear(); ui->lineP->blockSignals(oldP);

    ui->lineExplicitU->clear(); ui->lineExplicitV->clear(); ui->lineExplicitW->clear();
    ui->lineU->clear(); ui->lineV->clear(); ui->lineW->clear();

    if (ui->glWidget) {
        ui->glWidget->setParametricEquations("0", "0", "0", "0");
        if (ui->glWidget->getEngine()) {
            ui->glWidget->getEngine()->setExplicitU("");
            ui->glWidget->getEngine()->setExplicitV("");
            ui->glWidget->getEngine()->setExplicitW("");
        }
    }

    m_masterStopped = false;
    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(hasTimeVariable(fullText));
    }
    updateMasterButtonState();
    ui->glWidget->update();
}

void MainWindow::onApplyTextureScriptClicked()
{
    this->setProperty("rawTextureScript", ui->txtScriptEditor->toPlainText());
    QString code = ui->txtScriptEditor->toPlainText();

    if (!code.trimmed().isEmpty()) {
        if (ui->radioBackground->isChecked()) m_bgTextureCode = code;
        else m_surfaceTextureCode = code;
    }

    if (code.trimmed().isEmpty()) return;

    QString imgPath = extractAndResolveImagePath(code);
    if (imgPath.startsWith("NOT_FOUND|")) {
        InputValidator::showImageNotFoundError(this, imgPath.split("|").last());
        imgPath = "";
    }

    // Determina se il codice contiene logica procedurale
    bool hasCustomLogic = code.contains("return") || code.contains("vec3") || code.contains("vec4") || code.contains("mainImage");

    if (ui->radioBackground->isChecked()) {
        // --- RAMO A: SFONDO ---
        ui->glWidget->setBackgroundTextureEnabled(true);
        bool oldBlock = ui->chkBoxTexture->blockSignals(true);
        ui->chkBoxTexture->setChecked(true);
        ui->chkBoxTexture->blockSignals(oldBlock);

        ui->radioTexColor1->setEnabled(true);
        ui->radioTexColor2->setEnabled(true);
        if (!ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked()) {
            bool oldRad = ui->radioTexColor1->blockSignals(true);
            ui->radioTexColor1->setChecked(true);
            ui->radioTexColor1->blockSignals(oldRad);
        }

        if (ui->glWidget) {
            ui->glWidget->setProperty("bg_col1", QVector3D(m_bgTexColor1.redF(), m_bgTexColor1.greenF(), m_bgTexColor1.blueF()));
            ui->glWidget->setProperty("bg_col2", QVector3D(m_bgTexColor2.redF(), m_bgTexColor2.greenF(), m_bgTexColor2.blueF()));
        }

        // 1. Carica l'immagine (se c'è)
        if (!imgPath.isEmpty()) {
            ui->glWidget->setBackgroundTexture(imgPath);
        }

        // 2. Applica la logica custom (se c'è) o resetta al default
        if (hasCustomLogic || imgPath.isEmpty()) {
            // Dry-run del fragment shader prima di toccare la pipeline
            if (!ui->glWidget->validateAndApplyBackgroundShader(code)) {
                InputValidator::showShaderCompilationError(this,
                                                           "Background Shader Error",
                                                           ui->glWidget->getShaderError());
                return;
            }
            if (ui->glWidget && imgPath.isEmpty()) {
                ui->glWidget->setProperty("bg_zoom", 1.0f);
                ui->glWidget->setProperty("bg_pan", QVector2D(0.0f, 0.0f));
                ui->glWidget->setProperty("bg_rot", 0.0f);
            }
        }

        m_bgTextureCode = code;
        updateRenderState();
        if (ui->glWidget) ui->glWidget->update();
        updateFlatPreviewButton();

    } else {
        // --- RAMO B: SUPERFICIE ---
        if (!InputValidator::validateParametricScriptContext(this, code)) return;

        if (hasCustomLogic && ui->glWidget &&
                !ui->glWidget->validateAndApplyParametricShader(code)) {
            InputValidator::showShaderCompilationError(this,
                                                       "Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
            return;
        }

        m_surfaceTextureCode = code;

        if (!ui->chkBoxTexture->isChecked()) {
            const bool wasBlocked = ui->chkBoxTexture->blockSignals(true);
            ui->chkBoxTexture->setChecked(true);
            ui->chkBoxTexture->blockSignals(wasBlocked);
            updateTextureUIState(true);
            ui->glWidget->setTextureEnabled(true);
            m_surfaceTextureState = true;
        }

        // 1. GESTIONE MEMORIA IMMAGINE
        if (!imgPath.isEmpty()) {
            m_isImageMode = true;
            m_currentTexturePath = imgPath;
            if (ui->glWidget) {
                ui->glWidget->loadCustomShader("");
                ui->glWidget->setTextureColors(m_texColor1, m_texColor2);
                ui->glWidget->loadTextureFromFile(imgPath);
            }
        } else {
            m_isImageMode = false;
            m_currentTexturePath.clear();
            if (ui->glWidget) {
                ui->glWidget->clearTexture();
            }
        }

        // 2. GESTIONE COMPILAZIONE SHADER
        if (hasCustomLogic) {
            m_isCustomMode = true;
            if (ui->glWidget) {
                ui->glWidget->setTextureColors(m_texColor1, m_texColor2);

                if (imgPath.isEmpty()) {
                    generateTexture();
                }

                // TEST E APPLICAZIONE (Solo Parametrica come da nuove regole)
                bool success = ui->glWidget->validateAndApplyParametricShader(code);

                if (!success) {
                    InputValidator::showShaderCompilationError(this, "Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
                    return;
                }

                // Auto-switch: Se eravamo nel tab Ray Marching, passiamo automaticamente alla parametrica
                if (ui->tabModeSelector->currentIndex() == 1) {
                    ui->tabModeSelector->setCurrentIndex(0);
                }

                ui->glWidget->setFlatViewTarget(0);
                ui->glWidget->setFlatPan(0.0f, 0.0f);
                ui->glWidget->setFlatZoom(1.0f);
                ui->glWidget->setFlatRotation(0.0f);
            }
        } else {
            m_isCustomMode = false;
            if (ui->glWidget) {
                ui->glWidget->setTextureColors(m_texColor1, m_texColor2);

                bool success = ui->glWidget->validateAndApplyParametricShader("");
                if (!success) {
                    InputValidator::showShaderCompilationError(this, "GLSL Reset Error", ui->glWidget->getShaderError());
                    return;
                }
            }
        }

        if (ui->glWidget) {
            updateRenderState();
            ui->glWidget->update();
        }
    }

    QRegularExpression timeRegex("\\b(t|iTime|u_time)\\b");

    // RUN texture: agisce SOLO sul clock della texture interessata (Problema 3).
    // La superficie e l'altro canale texture NON vengono toccati.
    m_masterStopped = false;
    if (ui->radioBackground->isChecked()) {
        // RUN sulla texture di SFONDO: solo il clock background.
        bool bgNeedsAnim = m_bgTextureCode.contains(timeRegex);
        if (ui->glWidget) ui->glWidget->setBackgroundTextureAnimating(bgNeedsAnim);
    } else {
        // RUN sulla texture di SUPERFICIE: solo il clock texture superficie.
        bool surfTexNeedsAnim = false;
        if (ui->chkBoxTexture->isChecked()) {
            QString surfTexToCheck = (ui->tabModeSelector->currentIndex() == 1)
                    ? (ui->lineTexture->toPlainText() + ui->lineVariations->toPlainText())
                    : m_surfaceTextureCode;
            if (surfTexToCheck.contains(timeRegex)) surfTexNeedsAnim = true;
        }
        if (ui->glWidget) ui->glWidget->setSurfaceTextureAnimating(surfTexNeedsAnim);
    }
    updateMasterButtonState();

    updateFlatPreviewButton();
    ui->btnSaveScript->setEnabled(true);
}

void MainWindow::onRunRaymarchTextureClicked()
{
    if (!ui->glWidget) return;

    QRegularExpression timeRegex("\\b(t|iTime|u_time)\\b");
    bool texColorHasTime = ui->lineTexture->toPlainText().contains(timeRegex);
    bool geomHasTime     = ui->lineVariations->toPlainText().contains(timeRegex) ||
                           ui->lineEquation->toPlainText().contains(timeRegex);

    // STOP: ferma entrambi gli orologi che una texture RM può usare
    if (ui->btnTextureCode->text() == "Stop") {
        ui->glWidget->setSurfaceTextureAnimating(false);
        ui->glWidget->setSurfaceAnimating(false);
        updateMasterButtonState();
        return;
    }

    // RUN: ricompila e avvia l'orologio giusto a seconda di dove sta 't'
    this->setProperty("rmApplyOnly", true);
    onStartClicked();
    this->setProperty("rmApplyOnly", false);

    bool active = ui->chkBoxTexture->isChecked();
    if (active && (texColorHasTime || geomHasTime)) m_masterStopped = false;

    ui->glWidget->setSurfaceTextureAnimating(active && texColorHasTime);
    ui->glWidget->setSurfaceAnimating(active && geomHasTime);

    updateMasterButtonState();
}

void MainWindow::onRunSoundClicked()
{
    // Se sta suonando, ferma
    if (ui->btnRunCurrentScript->text() == "Stop Sound") {
        m_audioController->stopAll();
        if (m_currentScriptMode == ScriptModeSound) {
            ui->btnRunCurrentScript->setText("Run Sound");
        }
        updateMasterButtonState();
        return;
    }

    QString codeToAnalyze = m_soundScriptText + "\n" + m_surfaceScriptText + "\n" + m_surfaceTextureCode + "\n" + m_bgTextureCode;
    if (codeToAnalyze.trimmed().isEmpty()) codeToAnalyze = ui->txtScriptEditor->toPlainText();

    QString audioErr;
    if (!m_audioController->playFromScript(codeToAnalyze, &audioErr)) {
        InputValidator::showShaderCompilationError(this,
                                                   "Syntax Error (Sound Script)",
                                                   audioErr.isEmpty() ? "Audio shader compilation failed." : audioErr);
        updateMasterButtonState();
        return;
    }

    // playFromScript ha già impostato "Stop Sound" sul ramo che suona.
    updateMasterButtonState();
}


// ==========================================================
// LIBRARY & WORKSPACE MANAGEMENT
// ==========================================================

void MainWindow::onExampleItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    if (QTreeWidget *src = qobject_cast<QTreeWidget*>(sender())) {
        for (QTreeWidget *tree : { ui->treeSurfaces, ui->treeTextures,
             ui->treeMotions, ui->treeSounds }) {
            if (tree && tree != src) {
                bool b = tree->blockSignals(true);
                tree->clearSelection();
                tree->setCurrentItem(nullptr);
                tree->blockSignals(b);
            }
        }
    }

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }
#endif

    // ==========================================
    // 1. SURFACES
    // ==========================================
    QVariant vSurf = item->data(0, Qt::UserRole);
    if (vSurf.isValid()) {
        static QString lastSurfacePath = "";
        int index = vSurf.toInt();
        const LibraryItem &data = m_libraryManager.getSurface(index);

        if (!data.name.isEmpty()) {
            lastSurfacePath = data.filePath;
            applySurfaceExample(data);
        }
        return;
    }

    // ==========================================
    // 2. TEXTURES
    // ==========================================
    // --- SEZIONE TEXTURE ---
    QVariant vTex = item->data(0, Qt::UserRole + 1);
    if (vTex.isValid()) {
        int index = vTex.toInt();
        const LibraryItem &data = m_libraryManager.getTexture(index);

        // 1. Recuperiamo il codice attualmente in uso nel tab attivo
        QString activeCode;
        if (ui->radioBackground->isChecked()) {
            activeCode = m_bgTextureCode;
        } else {
            activeCode = (ui->tabModeSelector->currentIndex() == 1)
                    ? ui->lineTexture->toPlainText()
                    : m_surfaceTextureCode;
        }

        // 2. Verifichiamo se la texture cliccata è già quella visualizzata
        bool isMatch = false;
        if (data.isImage) {
            QString fileName = QFileInfo(data.filePath).fileName();
            isMatch = (!fileName.isEmpty() && activeCode.contains(fileName));
        } else {
            isMatch = (cleanCodeForComparison(activeCode) == cleanCodeForComparison(data.scriptCode));
        }

        // 3. LOGICA DI TOGGLE (Stop/Restart) STRUTTURALE
        if (isMatch && ui->chkBoxTexture->isChecked()) {
            bool isBg = ui->radioBackground->isChecked();

            if (isBg) {
                if (ui->glWidget->isBackgroundTextureAnimating()) {
                    ui->glWidget->setBackgroundTextureAnimating(false);
                } else {
                    ui->glWidget->setBackgroundTextureAnimating(true);
                }
            } else {
                // Il toggle della texture agisce SOLO sull'orologio della texture,
                // sia in Parametrico che in Ray Marching. La geometria (SurfaceAnimating)
                // NON viene mai toccata: se la 't' della SDF era ferma, resta ferma.
                bool isCurrentlyAnimating = ui->glWidget->isSurfaceTextureAnimating();

                if (isCurrentlyAnimating) {
                    ui->glWidget->setSurfaceTextureAnimating(false);
                } else {
                    ui->glWidget->setSurfaceTextureAnimating(true);
                    // Sblocca il master se stiamo riattivando l'animazione dal toggle
                    m_masterStopped = false;
                }
            }

            updateMasterButtonState();
            return;
        }

        // 4. Se non è un match (o se la texture era spenta), carichiamola normalmente

        // FIX 1: Sblocca lo stato di STOP globale. Così handleTextureSelection
        // è libera di attivare il motore se la nuova texture contiene u_time.
        m_masterStopped = false;

        handleTextureSelection(index);

        // FIX 2: Rimosso il blocco if/else che forzava setSurfaceTextureAnimating(true).
        // handleTextureSelection() sa già calcolare perfettamente se serve l'animazione
        // e imposta tutti i flag necessari in modo coerente per Parametric e Ray Marching.

        updateMasterButtonState();
        return;
    }

    // ==========================================
    // 3. RECORDS (MOTIONS)
    // ==========================================
    QVariant vMot = item->data(0, Qt::UserRole + 2);
    if (vMot.isValid()) {
        int index = vMot.toInt();
        const LibraryItem &data = m_libraryManager.getMotion(index);

        if (!data.name.isEmpty()) {
            QString activeMotion = this->property("activeMotionPath").toString();
            bool isPresetIntact = this->property("isPresetActive").toBool();

            // LOGICA DI TOGGLE (Stop/Restart)
            if (activeMotion == data.filePath && isPresetIntact) {
                if (m_btnStart && m_btnStart->text().toUpper() == "STOP") {
                    // 1. Se l'animazione è in corso, la mettiamo in pausa
                    m_btnStart->click();
                } else {
                    // 2. Se è in pausa, forziamo il riavvio DALL'INIZIO ricaricando il preset
                    applyMotionExample(data);
                }
                return;
            }

            // Altrimenti, registra il nuovo record e forza un riavvio pulito
            this->setProperty("activeMotionPath", data.filePath);
            applyMotionExample(data);
        }
        return;
    }
}

void MainWindow::applySurfaceExample(const LibraryItem &d)
{
    InputValidator::resetGeodesicWarning();

    // 1. Pulizia totale
    ui->glWidget->pauseMotion();
    ui->glWidget->resetTransformations();
    ui->glWidget->resetVisuals();

    m_audioController->stopAll();

    if (ui->btnStart_2) ui->btnStart_2->setText("GO");
    if (m_btnStart) m_btnStart->setText("START");

    // Reset Label Interfaccia
    ui->lblNutVal->setText("0"); ui->lblPrecVal->setText("0"); ui->lblSpinVal->setText("0");
    ui->lblOmegaVal->setText("0"); ui->lblPhiVal->setText("0"); ui->lblPsiVal->setText("0");

    if (ui->glWidget) {
        ui->glWidget->setNutationSpeed(0.0f);
        ui->glWidget->setPrecessionSpeed(0.0f);
        ui->glWidget->setSpinSpeed(0.0f);
        ui->glWidget->setOmegaSpeed(0.0f);
        ui->glWidget->setPhiSpeed(0.0f);
        ui->glWidget->setPsiSpeed(0.0f);
    }

    // ==========================================================
    // AZZERAMENTO TOTALE STATO TEXTURE, TESTI E COLORI
    // ==========================================================
    m_isCustomMode = false;
    m_isImageMode = false;
    m_blockTextureGen = false;
    m_surfaceTextureState = false;

    // Svuota tutti i testi dei vecchi script in memoria
    m_surfaceTextureCode.clear();
    m_surfaceTextureScriptText.clear();
    m_currentTexturePath.clear();

    ui->lineVariations->blockSignals(true);
    ui->lineVariations->clear();
    ui->lineVariations->blockSignals(false);
    if(ui->glWidget) ui->glWidget->setDisplacementCode("");

    ui->lineTexture->blockSignals(true);
    ui->lineTexture->clear();
    ui->lineTexture->blockSignals(false);
    if(ui->glWidget) ui->glWidget->setTextureCode("");

    m_bgTextureCode = "";
    m_bgTextureScriptText = "";

    m_soundScriptText.clear();

    m_surfaceScriptText.clear();
    ui->txtScriptEditor->blockSignals(true);
    ui->txtScriptEditor->clear();
    ui->txtScriptEditor->blockSignals(false);

    // Reset Colori a Default (Superficie Verde, Sfondo Grigio scuro)
    float defR = 0.20f, defG = 0.80f, defB = 0.20f;
    m_currentSurfaceColor = QColor::fromRgbF(defR, defG, defB);
    m_currentBorderColor  = QColor::fromRgbF(defR, defG, defB);
    m_currentBackgroundColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
    m_texColor1 = QColor::fromRgbF(defR, defG, defB);
    m_texColor2 = Qt::black;
    m_bgTexColor1 = QColor::fromRgbF(0.2f, 0.2f, 0.8f);
    m_bgTexColor2 = Qt::black;

    if (ui->glWidget) {
        ui->glWidget->setColor(defR, defG, defB);

        // TROVA QUESTA RIGA: Invia ancora il verde (defR, defG, defB)!
        ui->glWidget->setBorderColor(defR, defG, defB);

        // SOSTITUISCILA CON QUESTA: Legge il rosso corretto dalla variabile
        ui->glWidget->setBorderColor(m_currentBorderColor.redF(), m_currentBorderColor.greenF(), m_currentBorderColor.blueF());

        ui->glWidget->setBackgroundColor(m_currentBackgroundColor);
        ui->glWidget->setTextureColors(m_texColor1, m_texColor2);
    }

    ui->alphaSlider->setValue(d.alpha * 100);
    ui->lightSlider->setValue(d.lightIntensity * 100);

    onColorTargetChanged();

    // 2. Spegni Checkbox UI (senza triggerare segnali a cascata inutili)
    if (ui->chkBoxTexture->isChecked()) {
        bool wasBlocked = ui->chkBoxTexture->blockSignals(true);
        ui->chkBoxTexture->setChecked(false);
        ui->chkBoxTexture->blockSignals(wasBlocked);
    }

    // 3. Disabilita UI correlata (Slider colori texture, ecc.)
    updateTextureUIState(false);

    // 4. Reset Engine Grafico (Spegne tutte le texture)
    if (ui->glWidget) {
        ui->glWidget->setTextureEnabled(false);
        ui->glWidget->setBackgroundTextureEnabled(false);

        // CRUCIALE: Ricostruisce lo shader standard (Phong/Basic)
        ui->glWidget->rebuildShader();
    }

    updateRenderState();

    // 5. CARICAMENTO DATI (Equazioni, Colori, ecc.)
    applyCommonData(d);

    if (d.isImplicitMode) {
        QString eqToLoad = d.implicitEq.trimmed();

        // Se l'equazione letta dal file è vuota o NON contiene l'uguale,
        // forziamo la stringa di default per non far scattare il popup d'errore!
        if (eqToLoad.isEmpty() || !eqToLoad.contains("=")) {
            eqToLoad = "x^2 + y^2 + z^2 = 1.0";
        }

        ui->lineEquation->blockSignals(true);
        ui->lineEquation->setPlainText(eqToLoad);
        ui->lineEquation->blockSignals(false);

        if (ui->glWidget) {
            ui->glWidget->setImplicitEquation(eqToLoad);
        }
    }

    // Leggiamo i dati esatti salvati nel JSON per la posa statica
    float startOmega = d.startOmega;
    float startPhi   = d.startPhi;
    float startPsi   = d.startPsi;

    // Controllo Anti-Glitch per il 4D
    bool isFlat4D = (qFuzzyIsNull(startOmega) && qFuzzyIsNull(startPhi) && qFuzzyIsNull(startPsi));
    QString wText = d.w.trimmed();
    bool isSurface4D = !wText.isEmpty() && wText != "0" && wText != "0.0";

    if (isFlat4D && isSurface4D) {
        float smartOffset = 0.01f;
        // Applichiamo la minuscola rotazione solo al motore matematico
        startOmega = smartOffset; startPhi = smartOffset; startPsi = smartOffset;
    }

    // UNICO E DEFINITIVO invio alla GPU per la posizione della telecamera!
    ui->glWidget->setRotation4D(startOmega, startPhi, startPsi);

    ui->lblOmegaVal->setText("0.00");
    ui->lblPhiVal->setText("0.00");
    ui->lblPsiVal->setText("0.00");

    // 6. Eseguiamo onStartClicked per inizializzare equazioni
    bool hasValidEquations = (d.x.trimmed().length() > 0 && d.x != "0" && d.x != "0.0") || d.isImplicitMode;

    // Inferiamo che è uno script se c'è codice e le equazioni sono vuote!
    bool isScript = d.isScript || (!d.scriptCode.isEmpty() && !hasValidEquations);

    if (!isScript) {
        onStartClicked();
    } else {
        applyAnimationState(hasTimeVariable(m_surfaceScriptText));
    }

    // 7. Recuperiamo i dati di illuminazione dal file
    int modeToApply = (d.lightingMode != -1) ? d.lightingMode : 0;

    bool want4D = d.hasLightingState ? d.use4DLighting : isSurface4D;

    // 8. Applichiamo le impostazioni ALLA VARIABILE MEMBRO
    m_lightingMode4D = modeToApply;

    // 9. Applichiamo le impostazioni AL WIDGET GL
    if (ui->glWidget) {
        ui->glWidget->set4DLighting(want4D);

        ui->glWidget->setLightingMode4D(modeToApply);

        ui->glWidget->update();
    }

    // 10. Aggiorna il testo del bottone UI
    QString btnText;
    switch(modeToApply) {
    case 0: btnText = "Directional Lighting"; break;
    case 1: btnText = "Observer Lighting"; break;
    case 2: btnText = "Slice Lighting"; break;
    default: btnText = "Directional Lighting"; break;
    }
    if (ui->btnLightMode) ui->btnLightMode->setText(btnText);

    update4DButtonState();

    // 11. Finalizzazione
    ui->glWidget->setProjectionMode(d.projectionMode);
    updateProjectionButtonText();

    // Ripristino intelligente della Telecamera / Rotazione 3D
    if (!d.hasCamera3D) {
        // Preset vecchi (senza telecamera salvata): visuale standard inclinata
        ui->glWidget->setCameraPos(QVector3D(0.0f, 0.0f, 4.0f));
        ui->glWidget->setRotationQuat(QQuaternion());
        ui->glWidget->setCameraYaw(0.0f);
        ui->glWidget->setCameraPitch(0.0f);
        ui->glWidget->setCameraRoll(0.0f);
        ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
    } else {
        // Preset nuovi: ripristina SOLO l'inquadratura esatta! (Senza doppioni)
        ui->glWidget->setCameraPos(QVector3D(d.camX, d.camY, d.camZ));
        ui->glWidget->setRotationQuat(QQuaternion(d.rotW, d.rotX, d.rotY, d.rotZ));
        ui->glWidget->setCameraYaw(d.camYaw);
        ui->glWidget->setCameraPitch(d.camPitch);
        ui->glWidget->setCameraRoll(d.camRoll);
    }

    // Forza il ridisegno immediato con le nuove angolazioni
    ui->glWidget->update();

    // 12. Estrai eventuale audio dallo script per la scheda Sound
    QString fullLoadedText = m_surfaceScriptText + "\n" + m_surfaceTextureCode + "\n" + m_bgTextureCode;
    m_soundScriptText = extractAudioDirectives(fullLoadedText);

    // 12b. AVVIO AUTOMATICO DELL'AUDIO AL CARICAMENTO!
    if (fullLoadedText.contains("//MUSIC:") || fullLoadedText.contains("//SOUND_BEGIN")) {
        onRunSoundClicked();
    }

    // 13. Aggiorna visivamente il text editor in base alla modalità in cui si trova l'utente
    bool block = ui->txtScriptEditor->blockSignals(true);
    if (m_currentScriptMode == ScriptModeSurface) {
        ui->txtScriptEditor->setPlainText(m_surfaceScriptText);
    } else if (m_currentScriptMode == ScriptModeTexture) {
        ui->txtScriptEditor->setPlainText(m_bgTextureScriptText);
    } else if (m_currentScriptMode == ScriptModeSound) {
        ui->txtScriptEditor->setPlainText(m_soundScriptText);
    }
    ui->txtScriptEditor->blockSignals(block);

    updateScriptButtonText();

    QTimer::singleShot(20, this, [this, isScript]() {
        if (ui->glWidget) {
            updateULimits();
            updateVLimits();
            updateWLimits();
            ui->glWidget->setResolution(ui->stepSlider->value());
            ui->glWidget->setRaySteps(ui->stepSlider->value());

            if (!isScript) {
                checkAndTriggerMeshUpdate();
            } else {
                ui->glWidget->update();
            }
        }
    });

    this->setProperty("isTextureModified", false);
}

void MainWindow::applyMotionExample(const LibraryItem &data)
{
    m_masterStopped = false;

    InputValidator::resetGeodesicWarning();

    // 1. STOP TOTALE (Reset stato iniziale)
    m_audioController->stopAll();

    ui->glWidget->pauseMotion(); // Ferma rotazioni
    ui->glWidget->resetTransformations();

    if (pathTimer->isActive()) onDepartureClicked();
    if (pathTimer3D->isActive()) onDeparture3DClicked();

    QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
    if (geoAnimTimer && geoAnimTimer->isActive()) {
        geoAnimTimer->stop();
    }

    if (m_btnStart) m_btnStart->setText("START");
    if (ui->btnStart_2) ui->btnStart_2->setText("GO");

    // =================================================================
    // 1.5 SANIFICAZIONE E SEPARAZIONE DEI MODI (Parametrico vs Ray Marching)
    // =================================================================
    bool isImplicit = data.isImplicitMode;

    if (isImplicit) {
        ui->tabModeSelector->setCurrentIndex(1); // Forza Tab Ray Marching

        // Distruggiamo i dati parametrici precedenti
        ui->lineX->clear();
        ui->lineY->clear();
        ui->lineZ->clear();
        ui->lineP->clear();
        m_surfaceTextureCode.clear();
        m_surfaceScriptText.clear();

        ui->txtScriptEditor->blockSignals(true);
        ui->txtScriptEditor->clear();
        ui->txtScriptEditor->blockSignals(false);

        // +++ FILTRO DI SICUREZZA PER L'EQUAZIONE IMPLICITA +++QRegularExpression timeRegex("\\b(t|iTime|u_time)\\b");
        QString eqToLoad = data.implicitEq.trimmed();

        // Se l'equazione letta dal file è vuota o NON contiene l'uguale (vecchio formato),
        // forziamo la stringa umana di default per evitare crash della scheda video!
        if (eqToLoad.isEmpty() || !eqToLoad.contains("=")) {
            eqToLoad = "x^2 + y^2 + z^2 = 1.0";
        }

        ui->lineEquation->blockSignals(true);
        ui->lineEquation->setPlainText(eqToLoad);
        ui->lineEquation->blockSignals(false);

        if (ui->glWidget) {
            ui->glWidget->setImplicitEquation(eqToLoad);
        }
        // ++++++++++++++++++++++++++++++++++++++++++++++++++++++

    } else {
        ui->tabModeSelector->setCurrentIndex(0); // Forza Tab Parametrica
        // Distruggiamo i dati Ray Marching precedenti
        ui->lineEquation->clear();
        ui->lineVariations->clear();
        ui->lineTexture->clear();
        if (ui->glWidget) {
            ui->glWidget->setDisplacementCode("");
            ui->glWidget->setTextureCode("");
        }
    }

    // Reset sicuro di default per disinnescare vecchi shader bloccati
    if (ui->glWidget) {
        ui->glWidget->clearTexture();
        ui->glWidget->loadCustomShader("");
        ui->glWidget->setTextureCode(0);
    }

    // 2. Dati Comuni (Surface)
    applyCommonData(data);

    // 3. Colori
    if (data.hasCustomColors && !data.color1.isEmpty()) {
        QColor surfCol(data.color1);
        QColor bordCol(data.color2);
        m_currentSurfaceColor = surfCol;
        m_currentBorderColor = bordCol;

        ui->glWidget->setColor(surfCol.redF(), surfCol.greenF(), surfCol.blueF());
        ui->glWidget->setBorderColor(bordCol.redF(), bordCol.greenF(), bordCol.blueF());
        onColorTargetChanged();
    }

    ui->alphaSlider->setValue(data.alpha * 100);

    // 3b. Colore Sfondo
    if (!data.bgColor.isEmpty()) {
        m_currentBackgroundColor = QColor(data.bgColor);
        ui->glWidget->setBackgroundColor(m_currentBackgroundColor);
    }

    // 4. RIEMPI CAMPI TESTO PATH
    ui->lineX_P->setText(data.path4D_x);
    ui->lineY_P->setText(data.path4D_y);
    ui->lineZ_P->setText(data.path4D_z);
    ui->lineP_P->setText(data.path4D_w);
    ui->lineAlpha_P->setText(data.path4D_alpha);
    ui->lineBeta_P->setText(data.path4D_beta);
    ui->lineGamma_P->setText(data.path4D_gamma);
    checkPathFields();

    ui->lineX_P3D->setText(data.path3D_x);
    ui->lineY_P3D->setText(data.path3D_y);
    ui->lineZ_P3D->setText(data.path3D_z);
    ui->lineR_P3D->setText(data.path3D_roll);
    checkPath3DFields();

    // 5. TEXTURE E AUDIO SUPERFICIE E SFONDO
    bool oldTxtBlock = ui->txtScriptEditor->blockSignals(true);

    bool texEnabled = data.textureEnabled;
    QString texCode = data.textureCode;
    bool bgTexEnabled = data.bgTextureEnabled;
    QString bgCode = data.bgTextureCode;

    float surfZoom = data.zoom;
    float surfPanX = data.panX, surfPanY = data.panY;
    float surfRot = data.rotation;

    QColor loadedBgCol1 = QColor::fromRgbF(0.2f, 0.2f, 0.8f);
    QColor loadedBgCol2 = Qt::black;
    float bgZoom = 1.0f;
    float bgPanX = 0.0f, bgPanY = 0.0f;
    float bgRot = 0.0f;

    // LETTURA DEL FILE JSON (Bypassiamo la limitazione della libreria)
    QFile file(data.filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject root = doc.object();

        if (root.contains("texture")) {
            QJsonObject tex = root["texture"].toObject();
            if (tex.contains("enabled")) texEnabled = tex["enabled"].toBool();

            // 1. CARICAMENTO TEXTURE 2D (Energia/Colore)
            if (tex.contains("code")) {
                QString rawCode = tex["code"].toString();

                if (isImplicit) {
                    // Modalità Ray Marching: va nei campi dedicati
                    ui->lineTexture->blockSignals(true);
                    ui->lineTexture->setPlainText(rawCode);
                    ui->lineTexture->blockSignals(false);
                    if (ui->glWidget) ui->glWidget->setTextureCode(rawCode);

                    // FONDAMENTALE: svuotiamo texCode per evitare che inneschi la pipeline Parametrica più giù
                    texCode = "";
                } else {
                    // Modalità Parametrica: segue il percorso classico
                    texCode = rawCode;
                    m_surfaceTextureCode = texCode;
                }
            }

            // 2. CARICAMENTO DISPLACEMENT 3D (Bernoccoli)
            if (tex.contains("displacement") && isImplicit) {
                QString dispCode = tex["displacement"].toString();
                ui->lineVariations->blockSignals(true);
                ui->lineVariations->setPlainText(dispCode);
                ui->lineVariations->blockSignals(false);
                if (ui->glWidget) ui->glWidget->setDisplacementCode(dispCode);
            } else {
                ui->lineVariations->clear();
                if (ui->glWidget) ui->glWidget->setDisplacementCode("");
            }

            if (tex.contains("col1")) m_texColor1 = QColor(tex["col1"].toString());
            if (tex.contains("col2")) m_texColor2 = QColor(tex["col2"].toString());
            if (tex.contains("zoom")) surfZoom = tex["zoom"].toDouble(1.0);
            if (tex.contains("pan_x")) surfPanX = tex["pan_x"].toDouble(0.0);
            if (tex.contains("pan_y")) surfPanY = tex["pan_y"].toDouble(0.0);
            if (tex.contains("rotation")) surfRot = tex["rotation"].toDouble(0.0);
        }

        if (root.contains("background")) {
            QJsonObject bg = root["background"].toObject();
            if (bg.contains("enabled")) bgTexEnabled = bg["enabled"].toBool();
            if (bg.contains("code")) bgCode = bg["code"].toString();
            if (bg.contains("col1")) loadedBgCol1 = QColor(bg["col1"].toString());
            if (bg.contains("col2")) loadedBgCol2 = QColor(bg["col2"].toString());
            if (bg.contains("zoom")) bgZoom = bg["zoom"].toDouble(1.0);
            if (bg.contains("pan_x")) bgPanX = bg["pan_x"].toDouble(0.0);
            if (bg.contains("pan_y")) bgPanY = bg["pan_y"].toDouble(0.0);
            if (bg.contains("rotation")) bgRot = bg["rotation"].toDouble(0.0);
        }

        if (!data.hasCamera3D) {
            ui->glWidget->setCameraPos(QVector3D(0.0f, 0.0f, 4.0f));
            ui->glWidget->setRotationQuat(QQuaternion());
            ui->glWidget->setCameraYaw(0.0f);
            ui->glWidget->setCameraPitch(0.0f);
            ui->glWidget->setCameraRoll(0.0f);
            ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
        } else {
            ui->glWidget->setCameraPos(QVector3D(data.camX, data.camY, data.camZ));
            ui->glWidget->setRotationQuat(QQuaternion(data.rotW, data.rotX, data.rotY, data.rotZ));
            ui->glWidget->setCameraYaw(data.camYaw);
            ui->glWidget->setCameraPitch(data.camPitch);
            ui->glWidget->setCameraRoll(data.camRoll);
        }

        if (root.contains("observer4D")) {
            ui->glWidget->setObserverPos4D(root["observer4D"].toDouble(4.0));
        }

        if (root.contains("pathMode")) {
            // Usa decltype per fare un cast sicuro all'Enum originale
            m_pathMode = static_cast<decltype(m_pathMode)>(root["pathMode"].toInt());
        } else {
            // Retrocompatibilità per i vecchi record salvati prima di questa modifica
            m_pathMode = ModeTangential;
        }

        // Aggiorniamo subito i testi dei pulsanti nella UI
        if (m_pathMode == ModeTangential) {
            ui->pushView->setText("Tangent View");
            ui->pushView3D->setText("Tangent View");
        } else {
            ui->pushView->setText("Center View");
            ui->pushView3D->setText("Center View");
        }

        if (root.contains("speeds")) {
            QJsonObject spd = root["speeds"].toObject();

            if (spd.contains("path3D")) ui->speed3DSlider->setValue(spd["path3D"].toInt());
            else ui->speed3DSlider->setValue(0); // Reset per i vecchi file

            if (spd.contains("path4D")) ui->speed4DSlider->setValue(spd["path4D"].toInt());
            else ui->speed4DSlider->setValue(0); // Reset per i vecchi file
        } else {
            // Se il blocco speeds non esiste affatto
            ui->speed3DSlider->setValue(0);
            ui->speed4DSlider->setValue(0);
        }
    }

    // SEPARAZIONE IMMEDIATA AUDIO-GRAFICA
    // Recuperiamo il codice 2D corretto in base alla modalità corrente
    QString sourceForAudio = isImplicit ? ui->lineTexture->toPlainText() : texCode;
    QString fullLoadedText = sourceForAudio + "\n" + bgCode;

    m_soundScriptText = extractAudioDirectives(fullLoadedText);

    // Rimuoviamo la musica dai codici grafici per proteggere OpenGL!
    QRegularExpression cleanMusicRe(R"(^\s*//MUSIC:.*$\n?)", QRegularExpression::MultilineOption);
    QRegularExpression cleanBlockRe(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption);

    if (isImplicit) {
        QString cleanRM = ui->lineTexture->toPlainText();
        cleanRM.remove(cleanMusicRe); cleanRM.remove(cleanBlockRe);
        ui->lineTexture->setPlainText(cleanRM.trimmed());
        if (ui->glWidget) ui->glWidget->setTextureCode(cleanRM.trimmed());
    } else {
        texCode.remove(cleanMusicRe); texCode.remove(cleanBlockRe); texCode = texCode.trimmed();
    }

    bgCode.remove(cleanMusicRe); bgCode.remove(cleanBlockRe); bgCode = bgCode.trimmed();
    // ===================================================================

    m_surfaceTextureState = texEnabled;
    ui->glWidget->setTextureEnabled(texEnabled);
    m_surfaceTextureScriptText = texCode;
    m_surfaceTextureCode = texCode;

    m_bgTextureScriptText = bgCode;
    m_bgTextureCode = bgCode;

    // L'Editor ora riceve codice perfettamente pulito
    if (m_currentScriptMode == ScriptModeSurface) {
        ui->txtScriptEditor->setPlainText(m_surfaceScriptText);
    } else if (m_currentScriptMode == ScriptModeTexture) {
        if (ui->radioBackground->isChecked()) ui->txtScriptEditor->setPlainText(m_bgTextureScriptText);
        else ui->txtScriptEditor->setPlainText(m_surfaceTextureScriptText);
    } else if (m_currentScriptMode == ScriptModeSound) {
        ui->txtScriptEditor->setPlainText(m_soundScriptText);
    }

    QString imgPath = extractAndResolveImagePath(texCode);
    if (imgPath.startsWith("NOT_FOUND|")) {
        QMessageBox::warning(this, "Record Image Not Found",
                             "The image used in this record was not found at:\n\n" +
                             imgPath.split("|").last() + "\n\nThe animation will be loaded without this texture.");
        imgPath = "";
        texCode = "";
    }

    m_bgTexColor1 = loadedBgCol1;
    m_bgTexColor2 = loadedBgCol2;
    ui->glWidget->setTextureColors(m_texColor1, m_texColor2);

    // Svuota forzatamente gli shader procedurali "incastrati" prima di caricare il nuovo!
    if (ui->glWidget) {
        ui->glWidget->clearTexture();
        ui->glWidget->loadCustomShader("");
        ui->glWidget->rebuildShader();
    }

    // --- APPLICAZIONE TEXTURE SUPERFICIE ---
    if (texEnabled) {
        int currentTarget = ui->radioBackground->isChecked() ? 1 : 0;
        ui->glWidget->setFlatViewTarget(0);
        ui->glWidget->setFlatZoom(surfZoom);
        ui->glWidget->setFlatPan(surfPanX, surfPanY);
        ui->glWidget->setFlatRotation(surfRot);

        if (!texCode.isEmpty()) {
            bool hasCustomLogic = texCode.contains("return") || texCode.contains("vec3") || texCode.contains("vec4") || texCode.contains("mainImage");

            // 1. Carica l'immagine se presente
            if (!imgPath.isEmpty()) {
                ui->glWidget->loadTextureFromFile(imgPath);
                m_isImageMode = true;
                m_currentTexturePath = imgPath;
            } else {
                m_isImageMode = false;
                m_currentTexturePath.clear();
            }

            // 2. Carica lo script indipendentemente dall'immagine
            if (hasCustomLogic) {
                m_isCustomMode = true;

                if (imgPath.isEmpty()) {
                    generateTexture();
                }

                ui->glWidget->loadCustomShader(texCode);
            } else {
                m_isCustomMode = false;
                if (imgPath.isEmpty()) generateTexture();
                ui->glWidget->rebuildShader();
            }
        }
        else {
            m_isCustomMode = false;
            m_isImageMode = false;
            generateTexture();
            ui->glWidget->rebuildShader();
        }

        ui->glWidget->setFlatViewTarget(currentTarget);
    } else {
        m_isCustomMode = false;
        m_isImageMode = false;
        m_currentTexturePath.clear();
        m_surfaceTextureCode.clear();
    }

    // --- APPLICAZIONE TEXTURE BACKGROUND ---
    ui->glWidget->setBackgroundTextureEnabled(bgTexEnabled);
    m_bgTextureCode = bgCode;

    if (bgTexEnabled && !bgCode.isEmpty()) {
        if (ui->glWidget) {
            ui->glWidget->setProperty("bg_col1", QVector3D(m_bgTexColor1.redF(), m_bgTexColor1.greenF(), m_bgTexColor1.blueF()));
            ui->glWidget->setProperty("bg_col2", QVector3D(m_bgTexColor2.redF(), m_bgTexColor2.greenF(), m_bgTexColor2.blueF()));
            ui->glWidget->setProperty("bg_zoom", bgZoom);
            ui->glWidget->setProperty("bg_pan", QVector2D(bgPanX, bgPanY));
            ui->glWidget->setProperty("bg_rot", bgRot);
        }

        QString bgImgPath = extractAndResolveImagePath(bgCode);
        if (!bgImgPath.isEmpty() && !bgImgPath.startsWith("NOT_FOUND|")) {
            ui->glWidget->setBackgroundTexture(bgImgPath);
        }

        bool bgHasCustomLogic = bgCode.contains("return") || bgCode.contains("vec3") || bgCode.contains("vec4") || bgCode.contains("mainImage");
        if (bgHasCustomLogic || bgImgPath.isEmpty() || bgImgPath.startsWith("NOT_FOUND|")) {
            ui->glWidget->loadBackgroundScript(bgCode);
        }
    }

    if (ui->radioBackground->isChecked()) {
        bool oldBlock = ui->chkBoxTexture->blockSignals(true);
        ui->chkBoxTexture->setChecked(bgTexEnabled);
        ui->chkBoxTexture->blockSignals(oldBlock);

        ui->radioTexColor1->setEnabled(bgTexEnabled);
        ui->radioTexColor2->setEnabled(bgTexEnabled);
        if (bgTexEnabled && !ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked()) {
            bool oldRad = ui->radioTexColor1->blockSignals(true);
            ui->radioTexColor1->setChecked(true);
            ui->radioTexColor1->blockSignals(oldRad);
        }
        ui->radioEditSurf->setEnabled(!texEnabled);
        if (!texEnabled && ui->btnFlatPreview->isChecked()) {
            ui->btnFlatPreview->setChecked(false);
        }
    } else {
        bool oldBlock = ui->chkBoxTexture->blockSignals(true);
        ui->chkBoxTexture->setChecked(texEnabled);
        ui->chkBoxTexture->blockSignals(oldBlock);
        updateTextureUIState(texEnabled);
    }

    ui->txtScriptEditor->blockSignals(oldTxtBlock);

    if(ui->radioTexColor1->isChecked() || ui->radioTexColor2->isChecked() || ui->radioBackground->isChecked()) {
        onColorTargetChanged();
    }

    updateRenderState();

    // 6. VELOCITÀ E ANGOLI
    ui->glWidget->setNutationSpeed(data.speedNut);
    ui->glWidget->setPrecessionSpeed(data.speedPrec);
    ui->glWidget->setSpinSpeed(data.speedSpin);

    float spdOmega = isImplicit ? 0.0f : data.speedOmega;
    float spdPhi   = isImplicit ? 0.0f : data.speedPhi;
    float spdPsi   = isImplicit ? 0.0f : data.speedPsi;

    ui->glWidget->setOmegaSpeed(spdOmega);
    ui->glWidget->setPhiSpeed(spdPhi);
    ui->glWidget->setPsiSpeed(spdPsi);

    ui->lightSlider->setValue(data.lightIntensity * 100);

    int savedMode = (data.lightingMode != -1) ? data.lightingMode : 0;
    bool want4D = false;
    if (data.hasLightingState) {
        want4D = data.use4DLighting;
    } else {
        QString wText = data.w.trimmed();
        want4D = (!wText.isEmpty() && wText != "0" && wText != "0.0");
    }

    m_lightingMode4D = savedMode;
    ui->glWidget->set4DLighting(want4D);
    ui->glWidget->setLightingMode4D(savedMode);

    QString btnText;
    switch(savedMode) {
    case 0: btnText = "Directional Lighting"; break;
    case 1: btnText = "Observer Lighting"; break;
    case 2: btnText = "Slice Lighting"; break;
    default: btnText = "Directional Lighting"; break;
    }
    if (ui->btnLightMode) ui->btnLightMode->setText(btnText);
    update4DButtonState();

    ui->lblNutVal->setText(QString::number(data.speedNut, 'f', 2));
    ui->lblPrecVal->setText(QString::number(data.speedPrec, 'f', 2));
    ui->lblSpinVal->setText(QString::number(data.speedSpin, 'f', 2));
    // FIX: Usiamo le variabili ripulite per aggiornare le Label
    ui->lblOmegaVal->setText(QString::number(spdOmega, 'f', 2));
    ui->lblPhiVal->setText(QString::number(spdPhi, 'f', 2));
    ui->lblPsiVal->setText(QString::number(spdPsi, 'f', 2));

    if (data.restoreAngles) {
        // FIX: Anche gli angoli statici vengono azzerati in Ray Marching
        float stOmega = isImplicit ? 0.0f : data.startOmega;
        float stPhi   = isImplicit ? 0.0f : data.startPhi;
        float stPsi   = isImplicit ? 0.0f : data.startPsi;
        ui->glWidget->setRotation4D(stOmega, stPhi, stPsi);
    }

    ui->glWidget->update();

    auto isReal = [](const QString &s) {
        QString t = s.trimmed();
        return !t.isEmpty() && t != "0" && t != "0.0";
    };

    bool hasRotation = (std::abs(data.speedPrec) > 0.001f || std::abs(data.speedOmega) > 0.001f ||
                        std::abs(data.speedNut) > 0.001f || std::abs(data.speedSpin) > 0.001f ||
                        std::abs(data.speedPhi) > 0.001f || std::abs(data.speedPsi) > 0.001f);
    bool hasPath4D = isReal(data.path4D_x) || isReal(data.path4D_y) || isReal(data.path4D_z) || isReal(data.path4D_w) ||
            isReal(data.path4D_alpha) || isReal(data.path4D_beta) || isReal(data.path4D_gamma);

    bool hasPath3D = isReal(data.path3D_x) || isReal(data.path3D_y) || isReal(data.path3D_z) || isReal(data.path3D_roll);

    if (hasRotation) {
        if (ui->btnStart_2) ui->btnStart_2->setText("STOP");
        ui->glWidget->resumeMotion();
    }

    if (hasPath4D) onDepartureClicked();
    else if (hasPath3D) onDeparture3DClicked();

    ui->glWidget->setProjectionMode(data.projectionMode);
    updateProjectionButtonText();
    ui->btnBorder->setChecked(data.showBorder);

    // 7. AVVIO AUTOMATICO GRAFICA
    // 1. Nuova logica di validazione sicura
    bool hasValidEquations;
    if (data.isImplicitMode) {
        // In modalità Ray Marching, è valida solo se non è il segnaposto dello script
        hasValidEquations = !data.implicitEq.trimmed().isEmpty() &&
                !data.implicitEq.contains("// Controlled by Script");
    } else {
        // In modalità Parametrica, controlliamo la X
        hasValidEquations = (data.x.trimmed().length() > 0 && data.x != "0" && data.x != "0.0");
    }

    // 2. Deduzione corretta
    bool isScript = data.isScript || (!data.scriptCode.isEmpty() && !hasValidEquations);

    snapshotActiveEquations();

    if (!isScript) {
        onStartClicked();
    } else {
        if (ui->glWidget) {

            // Applica lo shader personalizzato se presente
            if (texEnabled && m_isCustomMode && !m_surfaceTextureCode.isEmpty()) {
                ui->glWidget->setRenderMode(11);
                ui->glWidget->loadCustomShader(m_surfaceTextureCode);
            }

            ui->glWidget->rebuildShader();

            ui->glWidget->updateSurfaceData();
            ui->glWidget->update();
        }

        QString scriptToCheck = m_surfaceScriptText + " " + m_surfaceTextureCode + " " + m_bgTextureCode;
        applyAnimationState(hasTimeVariable(scriptToCheck));
    }

    // =======================================================
    // RIATTIVAZIONE ANIMAZIONI TEXTURE AL CARICAMENTO
    // =======================================================
    if (ui->glWidget) {
        if (texEnabled && hasTimeVariable(m_surfaceTextureCode)) {
            ui->glWidget->setSurfaceTextureAnimating(true);
        }
        if (bgTexEnabled && hasTimeVariable(m_bgTextureCode)) {
            ui->glWidget->setBackgroundTextureAnimating(true);
        }
    }
    updateMasterButtonState();
    // =======================================================

    // 8. AVVIO AUDIO
    if (!m_soundScriptText.isEmpty()) {
        QString audioErr;
        bool audioOk = m_audioController->playFromScript(m_soundScriptText, &audioErr);
        if (!audioOk) {
            InputValidator::showShaderCompilationError(this,
                                                       "Syntax Error (Sound Script)",
                                                       audioErr.isEmpty() ? "Audio shader compilation failed." : audioErr);
            // niente return: animazione gia' avviata resta attiva, l'audio no
        }
        if (m_currentScriptMode == ScriptModeSound) {
            // Il testo del pulsante riflette l'esito reale, non la sola presenza di codice
            ui->btnRunCurrentScript->setText(audioOk ? "Stop Sound" : "Run Sound");
        }
    } else {
        m_audioController->stopAll();
        if (m_currentScriptMode == ScriptModeSound) {
            ui->btnRunCurrentScript->setText("Run Sound");
        }
    }

    if (m_currentScriptMode == ScriptModeSound) {
        if (!m_soundScriptText.isEmpty()) {
            ui->btnRunCurrentScript->setText("Stop Sound");
        } else {
            ui->btnRunCurrentScript->setText("Run Sound");
        }
    }

    // 9. HIGHLIGHT AUTOMATICO: SELEZIONA TEXTURE E SUONI NELL'ALBERO
    // A. Sincronizzazione Suoni (Cerca l'audio in TUTTI gli script attivi!)
    ui->treeSounds->clearSelection();

    QString fullAudioSearchCode = m_soundScriptText + "\n" + m_surfaceTextureCode + "\n" + m_bgTextureCode;

    if (!fullAudioSearchCode.trimmed().isEmpty()) {

        // FUNZIONE DI PULIZIA AGGRESSIVA (Rimuove TUTTI i commenti e gli spazi)
        auto cleanAudioForComparison = [](QString str) {
            str.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
            str.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));
            str.replace(QRegularExpression("\\s+"), "");
            return str;
        };

        QString normLoadedSound = cleanAudioForComparison(fullAudioSearchCode);

        QTreeWidgetItemIterator itSnd(ui->treeSounds);
        while (*itSnd) {
            QVariant vSnd = (*itSnd)->data(0, Qt::UserRole + 3);
            if (vSnd.isValid()) {
                int idx = vSnd.toInt();
                const LibraryItem &sndItem = m_libraryManager.getSound(idx);
                bool isMatch = false;

                bool isMedia = sndItem.filePath.endsWith(".mp3", Qt::CaseInsensitive) ||
                        sndItem.filePath.endsWith(".wav", Qt::CaseInsensitive) ||
                        sndItem.filePath.endsWith(".ogg", Qt::CaseInsensitive);

                if (isMedia) {
                    // MATCH ROBUSTO PER MEDIA: Estrae e confronta solo il nome del file (usando il codice NON pulito)
                    QString fileName = QFileInfo(sndItem.filePath).fileName();
                    if (!fileName.isEmpty() && fullAudioSearchCode.contains(fileName)) {
                        isMatch = true;
                    }
                } else if (!sndItem.scriptCode.isEmpty()) {
                    // MATCH PER SCRIPT PROCEDURALI: Usa il codice pulito (solo matematica GLSL)
                    QString normLibSound = cleanAudioForComparison(sndItem.scriptCode);

                    if (!normLibSound.isEmpty() && normLoadedSound.contains(normLibSound)) {
                        isMatch = true;
                    }
                }

                if (isMatch) {
                    (*itSnd)->setSelected(true);
                    ui->treeSounds->setCurrentItem(*itSnd);
                    QTreeWidgetItem* parent = (*itSnd)->parent();
                    while(parent) { parent->setExpanded(true); parent = parent->parent(); }
                    ui->treeSounds->scrollToItem(*itSnd);
                    break; // Ferma al primo match
                }
            }
            ++itSnd;
        }
    }

    // B. Sincronizzazione Texture (Evidenzia SOLO la texture della modalità attiva!)
    ui->treeTextures->clearSelection();
    QTreeWidgetItemIterator itTex(ui->treeTextures);

    QString activeCode;
    if (ui->radioBackground->isChecked()) {
        activeCode = m_bgTextureCode;
    } else {
        // Se siamo in Ray Marching usiamo il campo texture, altrimenti lo script superficie
        activeCode = (ui->tabModeSelector->currentIndex() == 1) ?
                    ui->lineTexture->toPlainText() : m_surfaceTextureCode;
    }

    QString cleanedActive = cleanCodeForComparison(activeCode);

    // Usiamo activeCode.trimmed()! Così se è un'immagine entra comunque nel ciclo.
    if (!activeCode.trimmed().isEmpty()) {
        while (*itTex) {
            QVariant vTex = (*itTex)->data(0, Qt::UserRole + 1);
            if (vTex.isValid()) {
                int idx = vTex.toInt();
                const LibraryItem &texItem = m_libraryManager.getTexture(idx);
                bool isMatch = false;

                if (texItem.isImage) {
                    QString fileName = QFileInfo(texItem.filePath).fileName();
                    if (!fileName.isEmpty() && activeCode.contains(fileName)) {
                        isMatch = true; // È un'immagine, usa il codice sporco
                    }
                } else {
                    QString cleanLibCode = cleanCodeForComparison(texItem.scriptCode);
                    // Match solo se non è vuota (per evitare falsi positivi con le immagini)
                    if (!cleanedActive.isEmpty() && cleanedActive == cleanLibCode) {
                        isMatch = true; // È procedurale, usa il codice pulito
                    }
                }

                if (isMatch) {
                    (*itTex)->setSelected(true);
                    ui->treeTextures->setCurrentItem(*itTex);

                    QTreeWidgetItem* parent = (*itTex)->parent();
                    while(parent) {
                        parent->setExpanded(true);
                        parent = parent->parent();
                    }

                    ui->treeTextures->scrollToItem(*itTex);
                    break;
                }
            }
            ++itTex;
        }
    }

    updateScriptButtonText();

    // MODIFICA: Catturiamo 'isScript' (calcolato al punto 7) dentro la parentesi quadra
    QTimer::singleShot(20, this, [this, isScript]() {
        if (ui->glWidget) {
            updateULimits();
            updateVLimits();
            updateWLimits();
            ui->glWidget->setResolution(ui->stepSlider->value());
            ui->glWidget->setRaySteps(ui->stepSlider->value());

            if (!isScript) {
                checkAndTriggerMeshUpdate();
            } else {
                ui->glWidget->update(); // Facciamo solo un refresh visivo
            }
        }
    });

    this->setProperty("isTextureModified", false);
}

void MainWindow::deleteSelectedExample() {
    m_fileOps->deleteSelected();
}

void MainWindow::onUndoDelete() {
    m_fileOps->undoDelete();
}

void MainWindow::onAddRepositoryClicked(LibraryType /*type*/)
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    QMessageBox::information(this, "Gestione Libreria",
                             "Su iPhone e iPad la tua libreria è gestita in automatico dal sistema.\n"
                             "Apri l'app 'File' di iOS per organizzare le tue cartelle e i preset.");
    return;
#else
    QSettings settings;
    QString currentRoot = settings.value("libraryRootPath", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();

    QString selectedPath = QFileDialog::getExistingDirectory(this, "Select Location for Presets Folder", currentRoot,
                                                             QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedPath.isEmpty()) return;

    selectedPath = QDir::cleanPath(selectedPath);
    QString finalPath = selectedPath + "/Presets";

    QDir().mkpath(finalPath);
    settings.setValue("libraryRootPath", finalPath);

    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathRecords");
    settings.remove("pathSounds");

    setupDefaultFolders();
#endif
}

void MainWindow::onCreateFolderClicked()
{
    bool ok;
    QString folderName = QInputDialog::getText(this, "New Folder", "Folder Name:", QLineEdit::Normal, "NewFolder", &ok);
    if (!ok || folderName.isEmpty()) return;

    folderName.replace("/", "_");
    folderName.replace("\\", "_");

    QString basePath;
    QTreeWidgetItem *item = getCurrentLibraryItem();
    QSettings settings;

    // A. C'è un item selezionato? Usiamo la sua cartella madre.
    if (item) {
        if (item->data(0, Qt::UserRole).isValid()) basePath = QFileInfo(m_libraryManager.getSurface(item->data(0, Qt::UserRole).toInt()).filePath).absolutePath();
        else if (item->data(0, Qt::UserRole + 1).isValid()) basePath = QFileInfo(m_libraryManager.getTexture(item->data(0, Qt::UserRole + 1).toInt()).filePath).absolutePath();
        else if (item->data(0, Qt::UserRole + 2).isValid()) basePath = QFileInfo(m_libraryManager.getMotion(item->data(0, Qt::UserRole + 2).toInt()).filePath).absolutePath();
        else if (item->data(0, Qt::UserRole + 3).isValid()) basePath = QFileInfo(m_libraryManager.getSound(item->data(0, Qt::UserRole + 3).toInt()).filePath).absolutePath();
        // Se è una cartella
        else if (item->data(0, Qt::UserRole + 10).isValid()) basePath = item->data(0, Qt::UserRole + 10).toString();
    }

    // B. Nessun item selezionato? Inseriamo nella root della categoria corretta.
    if (basePath.isEmpty()) {
        QWidget *currentTab = ui->tabWidget->currentWidget();

        // --- UNICO BLOCCO rootPath (Scopo limitato a dove serve davvero) ---
    QString rootPath = presetsRootPath();

        if (currentTab == ui->Texture) basePath = settings.value("pathTextures", rootPath + "/textures").toString();
        else if (currentTab == ui->Motions) basePath = settings.value("pathRecords", rootPath + "/records").toString();
        else if (currentTab->objectName().contains("Sound", Qt::CaseInsensitive)) basePath = settings.value("pathSounds", rootPath + "/sounds").toString();
        else basePath = settings.value("pathSurfaces", rootPath + "/surfaces").toString();
    }

    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        QMessageBox::warning(this, "Warning", "The destination library folder does not exist. Try resetting the Library.");
        return;
    }

    if (baseDir.mkdir(folderName)) {
        refreshRepositories();
        updateWatcherPaths();
    } else {
        QMessageBox::critical(this, "Error", "Could not create folder. A folder with this name might already exist.");
    }
}

void MainWindow::onSyncPresetsClicked()
{
    QSettings settings;

    // 1. AMNESIA FORZATA: Cancelliamo le vecchie configurazioni sballate
    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathMotions");
    settings.remove("pathRecords");
    settings.remove("pathSounds");

    // 2. PERCORSO DINAMICO (La chiave per iOS!)
    QString rootPath = presetsRootPath();

    if (rootPath.isEmpty()) {
        setupDefaultFolders();
        return;
    }

    auto reply = QMessageBox::question(this, "Restore Presets",
                                       "Do you want to restore the factory presets?\n"
                                       "This will organize your Library into the correct folders.",
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::No) return;

    // 3. PERCORSI ASSOLUTI
    QString pathSurf = rootPath + "/surfaces";
    QString pathTex  = rootPath + "/textures";
    QString pathRec  = rootPath + "/records";
    QString pathSnd  = rootPath + "/sounds";

    settings.setValue("pathSurfaces", pathSurf);
    settings.setValue("pathTextures", pathTex);
    settings.setValue("pathRecords", pathRec);
    settings.setValue("pathSounds", pathSnd);

    // 4. CREAZIONE FISICA CARTELLE
    QDir().mkpath(pathSurf);
    QDir().mkpath(pathTex);
    QDir().mkpath(pathRec);
    QDir().mkpath(pathSnd);

    // 5. ESTRAZIONE RICORSIVA
    int overwriteState = 0; // 0 = Chiedi, 1 = Yes to All, 2 = No to All

    syncResourcesToFolder(":/library/presets/surfaces", pathSurf, true, &overwriteState);
    syncResourcesToFolder(":/library/presets/textures", pathTex, true, &overwriteState);
    syncResourcesToFolder(":/library/presets/records", pathRec, true, &overwriteState);
    syncResourcesToFolder(":/library/presets/sounds", pathSnd, true, &overwriteState);

    refreshRepositories();
    updateWatcherPaths();
    QMessageBox::information(this, "Completed", "Library successfully updated and repaired!");
}


// ==========================================================
// FILE I/O & CLIPBOARD
// ==========================================================

void MainWindow::saveSurfaceToFile(const QString &suggestedPath) {
    m_presetSerializer->saveSurface(suggestedPath);
}

void MainWindow::onPasteExample() {
    m_fileOps->performPasteExample();
}

void MainWindow::onPasteTexture() {
    m_fileOps->performPasteTexture();
}

void MainWindow::performCut(QTreeWidgetItem* targetItem) {
    m_fileOps->performCut(targetItem);
}

void MainWindow::performCopy(QTreeWidgetItem* targetItem) {
    m_fileOps->performCopy(targetItem);
}

void MainWindow::onSaveTexJsonClicked() // SAVE AS
{
    // Recupera l'ultima cartella usata o quella del file corrente
    QSettings settings;
    QString startDir = settings.value("lastCustomTexDir", lastTextureFolder).toString();

    if (!m_currentTexturePath.isEmpty()) {
        startDir = QFileInfo(m_currentTexturePath).absolutePath();
    }

    bool wasAnimating = ui->glWidget->isAnimating();
    bool wasPath4D = pathTimer->isActive();
    bool wasPath3D = pathTimer3D->isActive();

    if (wasAnimating) ui->glWidget->pauseMotion();
    if (wasPath4D) pathTimer->stop();
    if (wasPath3D) pathTimer3D->stop();

    // Apre il dialogo
    QString fileName;

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // --- APPROCCIO MOBILE: Niente esplora risorse, chiediamo solo il nome ---
    bool ok;
    QString baseName = QInputDialog::getText(this, "Save Texture Preset",
                                             "Scegli un nome per il preset:",
                                             QLineEdit::Normal, "NuovaTexture", &ok);

    if (!ok || baseName.isEmpty()) {
        // L'utente ha annullato
        if (wasAnimating) ui->glWidget->resumeMotion();
        if (wasPath4D) pathTimer->start();
        if (wasPath3D) pathTimer3D->start();
        return;
    }

    // Costruiamo il percorso sicuro forzando l'estensione json
    QString safeDir = settings.value("pathTextures", settings.value("libraryRootPath").toString() + "/Textures").toString();
    fileName = safeDir + "/" + baseName + ".json";

#else
    // --- APPROCCIO DESKTOP: QFileDialog classico ---
    fileName = QFileDialog::getSaveFileName(this,
                                            "Save Texture Preset",
                                            startDir,
                                            "Texture Preset (*.json)");

    if (fileName.isEmpty()) {
        if (wasAnimating) ui->glWidget->resumeMotion();
        if (wasPath4D) pathTimer->start();
        if (wasPath3D) pathTimer3D->start();
        return;
    }
#endif

    if (wasAnimating) ui->glWidget->resumeMotion();
    if (wasPath4D) pathTimer->start();
    if (wasPath3D) pathTimer3D->start();

    if (fileName.isEmpty()) return;

    // Forza estensione .json
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    // Salva la cartella usata per il futuro
    settings.setValue("lastCustomTexDir", QFileInfo(fileName).absolutePath());

    // Chiama l'helper
    saveTextureConfig(fileName);
}

void MainWindow::onSaveTextureClicked()
{
    QSettings settings;
    QString rootPath = settings.value("libraryRootPath").toString();

    // Cartella di partenza: ultima usata per le texture, con fallback a /textures
    // (stessa logica di PresetSerializer::saveScript()).
    QString startDir = settings.value("lastCustomTexDir").toString();
    if (startDir.isEmpty() || startDir.contains("build", Qt::CaseInsensitive) || !QDir(startDir).exists()) {
        startDir = settings.value("pathTextures", rootPath + "/textures").toString();
    }

    // Stesso dialog navigabile del dock Library: MobileSaveDialog su mobile,
    // QFileDialog su desktop. saveTextureAs() poi chiama saveTexture().
    m_presetSerializer->saveTextureAs(startDir, m_currentTexturePresetPath);
}

void MainWindow::onSaveScriptClicked() {
    m_presetSerializer->saveScript();
}

void MainWindow::onSaveMotionClicked() {
    m_presetSerializer->saveMotion();
}


// ==========================================================
// AUDIO & MEDIA
// ==========================================================

void MainWindow::onSoundItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }
#endif

    QVariant vSound = item->data(0, Qt::UserRole + 3);
    if (!vSound.isValid()) return;

    int index = vSound.toInt();
    const LibraryItem &soundData = m_libraryManager.getSound(index);

    // 1. AGGIUNGIAMO LA FUNZIONE DI PULIZIA QUI
    auto cleanAudioCode = [](QString str) {
        str.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
        str.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));
        str.replace(QRegularExpression("\\s+"), "");
        return str;
    };

    QString audioSnippet;
    bool isAlreadyPresent = false;

    bool isMedia = soundData.filePath.endsWith(".mp3", Qt::CaseInsensitive) ||
            soundData.filePath.endsWith(".wav", Qt::CaseInsensitive) ||
            soundData.filePath.endsWith(".ogg", Qt::CaseInsensitive);

    if (isMedia) {
        audioSnippet = "//MUSIC: " + soundData.filePath;
        isAlreadyPresent = m_soundScriptText.contains(soundData.filePath);
    } else {
        audioSnippet = "//SOUND_BEGIN\n" + soundData.scriptCode.trimmed() + "\n//SOUND_END";

        // 2. MODIFICHIAMO SOLO QUESTA RIGA PER GLI SCRIPT:
        // Usiamo la pulizia per confrontare il codice in memoria con quello della libreria
        isAlreadyPresent = (cleanAudioCode(m_soundScriptText) == cleanAudioCode(soundData.scriptCode));
    }

    // 3. IL RESTO RIMANE ESATTAMENTE UGUALE
    if (isAlreadyPresent) {
        if (m_audioController && m_audioController->isPlaying()) {
            // Ferma l'audio ma LASCIA intatta la memoria (così START funzionerà!)
            m_audioController->stopAll();

            // Sincronizza l'interfaccia
            if (m_currentScriptMode == ScriptModeSound) {
                ui->btnRunCurrentScript->setText("Run Sound");
            }
            updateMasterButtonState();
        } else {
            // Se era fermo, lo facciamo ripartire usando il suo metodo nativo
            onRunSoundClicked();
        }
        return;
    }

    // PULIZIA ASSOLUTA: Rimuove l'audio da eventuali vecchi caricamenti spuri
    QRegularExpression reMusic(R"(^\s*//MUSIC:.*$\n?)", QRegularExpression::MultilineOption);
    QRegularExpression reProc(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption);

    m_surfaceTextureScriptText.remove(reMusic);
    m_surfaceTextureScriptText.remove(reProc);
    m_bgTextureScriptText.remove(reMusic);
    m_bgTextureScriptText.remove(reProc);

    // AGGIORNAMENTO MEMORIA AUDIO
    m_soundScriptText = audioSnippet;

    if (ui->radioBackground->isChecked()) {
        m_bgTextureCode = (m_soundScriptText + "\n\n" + m_bgTextureScriptText.trimmed()).trimmed();
        m_surfaceTextureCode = m_surfaceTextureScriptText.trimmed();
    } else {
        m_surfaceTextureCode = (m_soundScriptText + "\n\n" + m_surfaceTextureScriptText.trimmed()).trimmed();
        m_bgTextureCode = m_bgTextureScriptText.trimmed();
    }

    // AGGIORNAMENTO VISIVO DELL'EDITOR
    bool oldBlock = ui->txtScriptEditor->blockSignals(true);
    if (m_currentScriptMode == ScriptModeTexture) {
        ui->txtScriptEditor->setPlainText(ui->radioBackground->isChecked() ? m_bgTextureScriptText : m_surfaceTextureScriptText);
    } else if (m_currentScriptMode == ScriptModeSound) {
        ui->txtScriptEditor->setPlainText(m_soundScriptText);
    }
    ui->txtScriptEditor->blockSignals(oldBlock);

    m_audioController->stopAll();

    // Imposta lo stato visivo e abilita i tasti per il nuovo suono
    if (m_currentScriptMode == ScriptModeSound) {
        ui->btnRunCurrentScript->setText("Run Sound");
        ui->btnRunCurrentScript->setEnabled(true);
        ui->btnSaveScript->setEnabled(true);
    }

    onRunSoundClicked();
}


// ==========================================================
// PRIVATE HELPER METHODS
// ==========================================================

// --- Data & Initialization ---

void MainWindow::setupDefaultFolders()
{
    QSettings settings;

    QString rootPath;

#if defined(Q_OS_ANDROID)
    rootPath = "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
    QDir().mkpath(rootPath);
    settings.setValue("libraryRootPath", rootPath);
#elif defined(Q_OS_IOS)
    QString docPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    rootPath = docPath + "/SurfaceExplorer_Presets";
    bool mkpathResult = QDir().mkpath(rootPath);
    settings.setValue("libraryRootPath", rootPath);
#else
    // Su Desktop leggiamo la memoria e mostriamo il popup se manca
    rootPath = settings.value("libraryRootPath").toString();
    if (rootPath.isEmpty() || !QDir(rootPath).exists()) {
        QMessageBox::information(this, "Welcome to Surface Explorer",
                                 "Choose a location to install your Library.\n"
                                 "A 'Presets' folder will be automatically created there.");

        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString selectedPath = QFileDialog::getExistingDirectory(this, "Select Master Folder", defaultPath);

        if (selectedPath.isEmpty()) {
            rootPath = defaultPath + "/SurfaceExplorer_Presets";
        } else {
            selectedPath = QDir::cleanPath(selectedPath);
            rootPath = selectedPath + "/presets";
        }

        QDir().mkpath(rootPath);
        settings.setValue("libraryRootPath", rootPath);
    }
#endif

    // --- CREAZIONE DELLE 4 SOTTOCARTELLE FISSE ---
    QString surfDirUser = rootPath + "/surfaces";
    QString texDirUser  = rootPath + "/textures";
    QString recDirUser  = rootPath + "/records";
    QString sndDirUser  = rootPath + "/sounds";

    QDir().mkpath(surfDirUser);
    QDir().mkpath(texDirUser);
    QDir().mkpath(recDirUser);
    QDir().mkpath(sndDirUser);

    // --- ESTRAZIONE RISORSE ---
    syncResourcesToFolder(":/library/presets/surfaces", surfDirUser);
    syncResourcesToFolder(":/library/presets/textures", texDirUser);
    syncResourcesToFolder(":/library/presets/records", recDirUser);
    syncResourcesToFolder(":/library/presets/sounds", sndDirUser);

    // --- AMNESIA FORZATA: PULIZIA VECCHIA MEMORIA ---
    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathMotions");
    settings.remove("pathRecords");
    settings.remove("pathSounds");
    settings.remove("repoPathsSurfaces");
    settings.remove("repoPathsTextures");
    settings.remove("repoPathsMotions");
    settings.remove("repoPathsSounds");
    settings.remove("repositoryPaths");

    refreshRepositories();
    updateWatcherPaths();
}

void MainWindow::connectSidePanels()
{
    // --- Navigazione Dock ---
    connect(ui->dock3D, &QDockWidget::visibilityChanged, this, [this](bool visible){
        if (visible) switchTo3DMode();
    });
    connect(ui->dock4D, &QDockWidget::visibilityChanged, this, [this](bool visible){
        if (visible) switchTo4DMode();
    });

    // --- CONTROLLI ROTAZIONE

    // Precessione
    setupSpeedControl(ui->btnPrecessionPlus, ui->btnPrecessionMinus, ui->lblPrecVal,
                      [this](float v){ ui->glWidget->setPrecessionSpeed(v); });

    // Nutazione
    setupSpeedControl(ui->btnNutationPlus, ui->btnNutationMinus, ui->lblNutVal,
                      [this](float v){ ui->glWidget->setNutationSpeed(v); });

    // Spin
    setupSpeedControl(ui->btnSpinPlus, ui->btnSpinMinus, ui->lblSpinVal,
                      [this](float v){ ui->glWidget->setSpinSpeed(v); });

    // Omega (4D)
    setupSpeedControl(ui->btnOmegaPlus, ui->btnOmegaMinus, ui->lblOmegaVal,
                      [this](float v){
                          ui->glWidget->setOmegaSpeed(v);
                          update4DButtonState();
                      });

    // Phi (4D)
    setupSpeedControl(ui->btnPhiPlus, ui->btnPhiMinus, ui->lblPhiVal,
                      [this](float v){
                          ui->glWidget->setPhiSpeed(v);
                          update4DButtonState();
                      });

    // Psi (4D)
    setupSpeedControl(ui->btnPsiPlus, ui->btnPsiMinus, ui->lblPsiVal,
                      [this](float v){
                          ui->glWidget->setPsiSpeed(v);
                          update4DButtonState();
                      });

    // Tasto Stop/Go laterale
    connect(ui->btnStart_2, &QPushButton::clicked, this, &MainWindow::onStopClicked);
}

void MainWindow::connectNavButton(QPushButton *btn, int action)
{
    if (!btn) return;

    // Quando PREMI un bottone
    connect(btn, &QPushButton::pressed, this, [this, action]() {
        // Se è il primo tasto che premo, avvio il timer
        if (activeNavActions.isEmpty()) {
            navTimer->start();
        }
        // Aggiungo questa azione alla lista delle azioni attive
        activeNavActions.insert(action);

        // (Opzionale) Eseguo subito uno scatto per reattività immediata
        onNavTimerTick();
    });

    // Quando RILASCI un bottone
    connect(btn, &QPushButton::released, this, [this, action]() {
        // Rimuovo l'azione dalla lista
        activeNavActions.remove(action);

        // Se non ci sono più tasti premuti, fermo il timer
        if (activeNavActions.isEmpty()) {
            navTimer->stop();
        }
    });
}

// --- Library & File I/O ---

void MainWindow::syncResourcesToFolder(const QString &resourcePath, const QString &diskPath, bool forceRestore, int *overwriteState)
{
    QDir diskDir(diskPath);

    if (!diskDir.exists()) {
        diskDir.mkpath(".");
    }

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // =========================================================
    // VERSIONE MOBILE (iOS/Android)
    // =========================================================
    QDirIterator it(resourcePath, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString src = it.next();

        QString relativePath = src.mid(resourcePath.length());
        if (relativePath.startsWith("/")) relativePath = relativePath.mid(1);

        QString dst = diskDir.absoluteFilePath(relativePath);
        QString dstDir = QFileInfo(dst).absolutePath();

        if (!QDir(dstDir).exists()) QDir().mkpath(dstDir);

        QString deletedPath = dst + ".deleted";
        if (forceRestore && QFile::exists(deletedPath)) QFile::remove(deletedPath);

        bool isDeleted = QFile::exists(deletedPath);
        bool needsCopy = resolveNeedsCopy(src, dst, forceRestore, isDeleted, overwriteState);

        if (needsCopy) {
            if (QFileInfo(src).fileName().startsWith("._")) continue;

            QFile inFile(src);
            if (inFile.open(QIODevice::ReadOnly)) {
                QFile outFile(dst);

                if (outFile.exists()) {
                    outFile.setPermissions(QFile::WriteOwner | QFile::WriteUser);
                    outFile.remove();
                }

                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(inFile.readAll());
                    outFile.close();
#if defined(Q_OS_ANDROID)
                    notifyAndroidMediaStore(dst);
#endif
                }
                inFile.close();
            }
        }
    }

#else
    // =========================================================
    // VERSIONE DESKTOP ORIGINALE
    // =========================================================
    QDir resDir(resourcePath);

    for (const QString &filename : resDir.entryList(QDir::Files)) {
        QString src = resourcePath + "/" + filename;
        QString dst = diskDir.absoluteFilePath(filename);
        QString deletedPath = dst + ".deleted";

        if (forceRestore && QFile::exists(deletedPath)) {
            QFile::remove(deletedPath);
        }

        bool isDeleted = QFile::exists(deletedPath);
        bool needsCopy = resolveNeedsCopy(src, dst, forceRestore, isDeleted, overwriteState);

        if (needsCopy) {
            if (filename.startsWith("._")) continue;

            if (QFile::exists(dst)) {
                QFile::setPermissions(dst, QFile::WriteOwner | QFile::WriteUser);
                QFile::remove(dst);
            }

            if (QFile::copy(src, dst)) {
                QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
            }
        }
    }

    // GESTIONE SOTTOCARTELLE (Nota l'aggiunta di overwriteState)
    for (const QString &dirName : resDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString subResPath = resourcePath + "/" + dirName;
        QString subDiskPath = diskPath + "/" + dirName;

        syncResourcesToFolder(subResPath, subDiskPath, forceRestore, overwriteState);
    }
#endif
}

void MainWindow::refreshRepositories()
{
    if (m_fsWatcher) m_fsWatcher->blockSignals(true);

    // 0. Blocchiamo i segnali della UI per non innescare eventi a catena durante la pulizia
    ui->treeSurfaces->blockSignals(true);
    ui->treeTextures->blockSignals(true);
    ui->treeMotions->blockSignals(true);
    ui->treeSounds->blockSignals(true);

    // 1. LAMBDA AVANZATA: Ricostruisce il percorso completo dell'albero (es. "Cartella/MioFile")
    auto getItemPath = [](QTreeWidgetItem* item) -> QString {
        QString path = item->text(0);
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            path = parent->text(0) + "/" + path;
            parent = parent->parent();
        }
        return path;
    };

    // 2. SALVATAGGIO STATO ESPANSIONE E SELEZIONE (Ora basato sul percorso univoco)
    QSet<QString> expSurfaces, expTextures, expMotions, expSounds;
    QSet<QString> selSurfaces, selTextures, selMotions, selSounds;

    auto saveState = [&](QTreeWidget* tree, QSet<QString>& expSet, QSet<QString>& selSet) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            QString path = getItemPath(*it);
            if ((*it)->isExpanded()) expSet.insert(path);
            if ((*it)->isSelected()) selSet.insert(path);
            ++it;
        }
    };

    saveState(ui->treeSurfaces, expSurfaces, selSurfaces);
    saveState(ui->treeTextures, expTextures, selTextures);
    saveState(ui->treeMotions, expMotions, selMotions);
    saveState(ui->treeSounds, expSounds, selSounds);

    // 3. SALVATAGGIO POSIZIONE BARRE DI SCORRIMENTO (Impedisce il salto visivo)
    int scrollSurfaces = ui->treeSurfaces->verticalScrollBar()->value();
    int scrollTextures = ui->treeTextures->verticalScrollBar()->value();
    int scrollMotions  = ui->treeMotions->verticalScrollBar()->value();
    int scrollSounds   = ui->treeSounds->verticalScrollBar()->value();

    // 4. PULIZIA SICURA
    ui->treeSurfaces->clearSelection();
    ui->treeTextures->clearSelection();
    ui->treeMotions->clearSelection();
    ui->treeSounds->clearSelection();

    ui->treeSurfaces->clear();
    ui->treeTextures->clear();
    ui->treeMotions->clear();
    ui->treeSounds->clear();
    m_libraryManager.clear();

    // 5. CARICAMENTO DAL FILE SYSTEM
    QSettings settings;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#else
    QString rootPath = settings.value("libraryRootPath").toString();
#endif

    QString pathSurf = settings.value("pathSurfaces", rootPath + "/surfaces").toString();
    QString pathTex  = settings.value("pathTextures", rootPath + "/textures").toString();
    QString pathRec  = settings.value("pathRecords",  rootPath + "/records").toString();
    QString pathSnd  = settings.value("pathSounds",   rootPath + "/sounds").toString();

    if (QDir(pathSurf).exists()) m_libraryManager.loadFromDirectory(pathSurf, ui->treeSurfaces, LibraryType::Surface);
    if (QDir(pathTex).exists())  m_libraryManager.loadFromDirectory(pathTex,  ui->treeTextures, LibraryType::Texture);
    if (QDir(pathRec).exists())  m_libraryManager.loadFromDirectory(pathRec,  ui->treeMotions,  LibraryType::Motion);
    if (QDir(pathSnd).exists())  m_libraryManager.loadFromDirectory(pathSnd,  ui->treeSounds,   LibraryType::Sound);

    // 6. RIPRISTINO STATO ESPANSIONE E SELEZIONE
    auto restoreState = [&](QTreeWidget* tree, const QSet<QString>& expSet, const QSet<QString>& selSet) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            QString path = getItemPath(*it);
            if (expSet.contains(path)) (*it)->setExpanded(true);
            if (selSet.contains(path)) (*it)->setSelected(true);
            ++it;
        }
    };

    restoreState(ui->treeSurfaces, expSurfaces, selSurfaces);
    restoreState(ui->treeTextures, expTextures, selTextures);
    restoreState(ui->treeMotions, expMotions, selMotions);
    restoreState(ui->treeSounds, expSounds, selSounds);

    // 7. RIPRISTINO BARRE DI SCORRIMENTO
    ui->treeSurfaces->verticalScrollBar()->setValue(scrollSurfaces);
    ui->treeTextures->verticalScrollBar()->setValue(scrollTextures);
    ui->treeMotions->verticalScrollBar()->setValue(scrollMotions);
    ui->treeSounds->verticalScrollBar()->setValue(scrollSounds);

    // 8. RIATTIVIAMO I SEGNALI
    ui->treeSurfaces->blockSignals(false);
    ui->treeTextures->blockSignals(false);
    ui->treeMotions->blockSignals(false);
    ui->treeSounds->blockSignals(false);

    if (m_fsWatcher) m_fsWatcher->blockSignals(false);
}

void MainWindow::updateWatcherPaths()
{
    if (!m_fsWatcher) return;

    // Rimuove i vecchi percorsi sorvegliati
    if (!m_fsWatcher->directories().isEmpty()) m_fsWatcher->removePaths(m_fsWatcher->directories());
    if (!m_fsWatcher->files().isEmpty()) m_fsWatcher->removePaths(m_fsWatcher->files());

    // Helper per aggiungere la cartella radice e tutte le sue sottocartelle
    auto addDirsToWatcher = [this](const QString &root) {
        if (!QDir(root).exists()) return;
        m_fsWatcher->addPath(root); // Aggiunge la root

        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            m_fsWatcher->addPath(it.next()); // Aggiunge ogni sottocartella
        }
    };

    QSettings settings;

    QString rootPath = presetsRootPath();

    addDirsToWatcher(settings.value("pathSurfaces", rootPath + "/surfaces").toString());
    addDirsToWatcher(settings.value("pathTextures", rootPath + "/textures").toString());
    addDirsToWatcher(settings.value("pathMotions", rootPath + "/motions").toString());
    addDirsToWatcher(settings.value("pathSounds", rootPath + "/sounds").toString());
}



void MainWindow::copyPath(QString src, QString dst) {
    m_fileOps->copyPath(src, dst);
}

void MainWindow::saveTextureConfig(const QString &path) {
    m_presetSerializer->saveTexture(path);
    this->setProperty("isTextureModified", false);
}

QTreeWidgetItem* MainWindow::getCurrentLibraryItem() {
    QWidget* currentTab = ui->tabWidget->currentWidget();

    // Ci fidiamo SOLO della selezione esplicita e reale, ignorando il focus invisibile!
    if (currentTab == ui->Surface) {
        if (!ui->treeSurfaces->selectedItems().isEmpty()) return ui->treeSurfaces->selectedItems().first();
    }
    else if (currentTab == ui->Texture) {
        if (!ui->treeTextures->selectedItems().isEmpty()) return ui->treeTextures->selectedItems().first();
    }
    else if (currentTab == ui->Motions) {
        if (!ui->treeMotions->selectedItems().isEmpty()) return ui->treeMotions->selectedItems().first();
    }
    else if (currentTab->objectName().contains("Sound", Qt::CaseInsensitive)) {
        if (!ui->treeSounds->selectedItems().isEmpty()) return ui->treeSounds->selectedItems().first();
    }
    return nullptr;
}

void MainWindow::applyCommonData(const LibraryItem &d)
{
    // ==========================================================
    // 1. RESET GLOBALE PRE-CARICAMENTO E UI
    // ==========================================================

    this->setProperty("isInitialLoad", true);

    onStopClicked();

    if (m_statusLabel) {
        m_statusLabel->setStyleSheet("");
        m_statusLabel->clear();
    }

    onStopClicked();

    if (pathTimer->isActive()) onDepartureClicked();
    if (pathTimer3D->isActive()) onDeparture3DClicked();

    QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
    if (geoAnimTimer && geoAnimTimer->isActive()) {
        geoAnimTimer->stop();
    }

    // Reset Telecamera e Rotazioni 3D/4D
    ui->glWidget->resetTransformations();
    ui->glWidget->resetTime();
    ui->glWidget->setRotation4D(0.0f, 0.0f, 0.0f);

    // Reset Etichette Rotazioni UI
    ui->lblNutVal->setText("0.00");
    ui->lblPrecVal->setText("0.00");
    ui->lblSpinVal->setText("0.00");
    ui->lblOmegaVal->setText("0.00");
    ui->lblPhiVal->setText("0.00");
    ui->lblPsiVal->setText("0.00");

    // Reset Limiti Intervalli di Default (Evita glitch se il preset non li dichiara)
    // Parametriche
    ui->uMinEdit->setText("0");
    ui->uMaxEdit->setText("6.28318");
    ui->vMinEdit->setText("0");
    ui->vMaxEdit->setText("6.28318");
    ui->wMinEdit->setText("0");
    ui->wMaxEdit->setText("1");
    // Ray Marching
    ui->lineXMin->clear();
    ui->lineXMax->clear();
    ui->lineYMin->clear();
    ui->lineYMax->clear();
    ui->lineZMin->clear();
    ui->lineZMax->clear();

    // Reset Bordo
    ui->btnBorder->setChecked(false);

    // ==========================================================
    // 2. APPLICAZIONE DATI DEL PRESET
    // ==========================================================

    m_savedRenderMode = d.renderMode;

    bool isShell = false;
    if (d.isImplicitMode) {
        if (m_savedRenderMode >= 10) {
            isShell = true;
            m_savedRenderMode -= 10;
        } else {
            isShell = false;
        }
    }

    if (ui->radioBackground->isChecked()) {
        if (m_currentScriptMode == ScriptModeTexture) {
            m_bgTextureScriptText = ui->txtScriptEditor->toPlainText();
            bool oldBlock = ui->txtScriptEditor->blockSignals(true);
            ui->txtScriptEditor->setPlainText(m_surfaceTextureScriptText);
            ui->txtScriptEditor->blockSignals(oldBlock);
            ui->btnRunCurrentScript->setText("Run Surface Texture");
        }
        ui->radioEditSurf->setEnabled(true);
        ui->radioEditBorder->setEnabled(true);
    }

    // Forza il testo della checkbox indipendentemente da tutto
    ui->chkBoxTexture->setText("Texture");

    bool oldBas = ui->radioBasic->blockSignals(true);
    bool oldPho = ui->radioPhong->blockSignals(true);
    bool oldWF = ui->radioWF->blockSignals(true);

    if (m_savedRenderMode == 1 && ui->radioPhong) {
        ui->radioPhong->setChecked(true);
    }
    else if (m_savedRenderMode == 2 && ui->radioWF) {
        ui->radioWF->setChecked(true);
    }
    else if (ui->radioBasic) {
        m_savedRenderMode = 0;
        ui->radioBasic->setChecked(true);
    }

    ui->chkBoxTexture->setText("Texture");
    ui->radioBasic->blockSignals(oldBas);
    ui->radioPhong->blockSignals(oldPho);
    ui->radioWF->blockSignals(oldWF);

    // 2. Risoluzione e Limiti (Sovrascrive i default se il preset li contiene)
    bool oldStep = ui->stepSlider->blockSignals(true);

    if (ui->stepSlider->maximum() < d.steps) {
        ui->stepSlider->setMaximum(std::max(1000, d.steps));
    }

    ui->stepSlider->setValue(d.steps);
    ui->lineSteps->setText(QString::number(d.steps));
    ui->stepSlider->blockSignals(oldStep);

    ui->glWidget->setResolution(d.steps);
    ui->glWidget->setRaySteps(d.steps);

    auto setLim = [](QLineEdit* line, float val) {
        line->setText(QString::number(val, 'g', 12));
        line->setCursorPosition(0); // Riporta il cursore a sinistra
    };

    setLim(ui->uMinEdit, d.uMin);
    setLim(ui->uMaxEdit, d.uMax);
    setLim(ui->vMinEdit, d.vMin);
    setLim(ui->vMaxEdit, d.vMax);
    setLim(ui->wMinEdit, d.wMin);
    setLim(ui->wMaxEdit, d.wMax);

    updateULimits();
    updateVLimits();
    updateWLimits();

    auto setLimSpace = [](QLineEdit* line, float val, float defVal) {
        if (std::abs(val - defVal) < 0.001f) {
            line->clear(); // Se è il valore di default estremo, lascia la casella pulita
        } else {
            line->setText(QString::number(val, 'g', 6));
        }
        line->setCursorPosition(0);
    };

    setLimSpace(ui->lineXMin, d.xMin, -1000.0f);
    setLimSpace(ui->lineXMax, d.xMax, 1000.0f);
    setLimSpace(ui->lineYMin, d.yMin, -1000.0f);
    setLimSpace(ui->lineYMax, d.yMax, 1000.0f);
    setLimSpace(ui->lineZMin, d.zMin, -1000.0f);
    setLimSpace(ui->lineZMax, d.zMax, 1000.0f);

    // Forza immediatamente i limiti sulla GPU cancellando le reminiscenze vecchie
    if (ui->glWidget) {
        ui->glWidget->setRangeX(d.xMin, d.xMax);
        ui->glWidget->setRangeY(d.yMin, d.yMax);
        ui->glWidget->setRangeZ(d.zMin, d.zMax);
    }

    // 3. Costanti Matematiche
    ui->aSlider->blockSignals(true); ui->lineA->blockSignals(true);
    ui->bSlider->blockSignals(true); ui->lineB->blockSignals(true);
    ui->cSlider->blockSignals(true); ui->lineC->blockSignals(true);
    ui->dSlider->blockSignals(true); ui->lineD->blockSignals(true);
    ui->eSlider->blockSignals(true); ui->lineE->blockSignals(true);
    ui->fSlider->blockSignals(true); ui->lineF->blockSignals(true);
    ui->sSlider->blockSignals(true); ui->lineS->blockSignals(true);

    // Lambda per espandere il range quando si carica un salvataggio estremo
    auto updateSliderPreset = [](QSlider* s, float v, bool isS) {
        int intVal = static_cast<int>(v * 100.0f);
        int newMin = isS ? std::min(-1000, intVal) : 0;
        int newMax = std::max(1000, intVal);
        s->setRange(newMin, newMax);
        s->setValue(intVal);
    };

    updateSliderPreset(ui->aSlider, d.a, false);
    updateSliderPreset(ui->bSlider, d.b, false);
    updateSliderPreset(ui->cSlider, d.c, false);
    updateSliderPreset(ui->dSlider, d.d, false);
    updateSliderPreset(ui->eSlider, d.e, false);
    updateSliderPreset(ui->fSlider, d.f, false);
    updateSliderPreset(ui->sSlider, d.s, true);

    ui->lineA->setText(QString::number(d.a, 'f', 2));
    ui->lineB->setText(QString::number(d.b, 'f', 2));
    ui->lineC->setText(QString::number(d.c, 'f', 2));
    ui->lineD->setText(QString::number(d.d, 'f', 2));
    ui->lineE->setText(QString::number(d.e, 'f', 2));
    ui->lineF->setText(QString::number(d.f, 'f', 2));
    ui->lineS->setText(QString::number(d.s, 'f', 2));

    ui->aSlider->blockSignals(false); ui->lineA->blockSignals(false);
    ui->bSlider->blockSignals(false); ui->lineB->blockSignals(false);
    ui->cSlider->blockSignals(false); ui->lineC->blockSignals(false);
    ui->dSlider->blockSignals(false); ui->lineD->blockSignals(false);
    ui->eSlider->blockSignals(false); ui->lineE->blockSignals(false);
    ui->fSlider->blockSignals(false); ui->lineF->blockSignals(false);
    ui->sSlider->blockSignals(false); ui->lineS->blockSignals(false);

    ui->glWidget->setEquationConstants(d.a, d.b, d.c, d.d, d.e, d.f, d.s);

    // --- CARICAMENTO FLUSSO GEODETICO ---
    // 1. Blocchiamo i segnali per evitare l'auto-cancellazione da parte di checkParametricDependency()
    bool b1 = ui->lnU->blockSignals(true);
    bool b2 = ui->lnV->blockSignals(true);
    bool b3 = ui->lnW->blockSignals(true);
    bool b4 = ui->lndU->blockSignals(true);
    bool b5 = ui->lndV->blockSignals(true);
    bool b6 = ui->lndW->blockSignals(true);
    bool b7 = ui->lineConform->blockSignals(true);

    // 2. Assegnazione immediata dalle variabili pre-caricate nella struct LibraryItem
    ui->lnU->setPlainText(d.geoU0);
    ui->lnV->setPlainText(d.geoV0);
    ui->lnW->setPlainText(d.geoW0);
    ui->lndU->setPlainText(d.geoDU);
    ui->lndV->setPlainText(d.geoDV);
    ui->lndW->setPlainText(d.geoDW);
    ui->lineConform->setPlainText(d.geoConform.isEmpty() ? "1.0" : d.geoConform);

    // 3. Sblocco dei segnali
    ui->lnU->blockSignals(b1);
    ui->lnV->blockSignals(b2);
    ui->lnW->blockSignals(b3);
    ui->lndU->blockSignals(b4);
    ui->lndV->blockSignals(b5);
    ui->lndW->blockSignals(b6);
    ui->lineConform->blockSignals(b7);
    // ------------------------------------

    // 4. Logica Caricamento Equazioni vs Script
    bool hasValidEquations = false;
    if (d.x.trimmed().length() > 0 && d.x != "0" && d.x != "0.0") hasValidEquations = true;
    if (d.isImplicitMode) hasValidEquations = true;

    // Salvataggio
    bool isScript = d.isScript || (!d.scriptCode.isEmpty() && !hasValidEquations);

    bool oldTabSig = ui->tabModeSelector->blockSignals(true);
    if (d.isImplicitMode) {
        ui->tabModeSelector->setCurrentIndex(1); // Cambia al tab Implicit
        ui->glWidget->setEngineMode(GLWidget::ModeImplicit);
    } else {
        ui->tabModeSelector->setCurrentIndex(0); // Cambia al tab Parametric
        ui->glWidget->setEngineMode(GLWidget::ModeParametric);
    }
    ui->tabModeSelector->blockSignals(oldTabSig);

    m_currentScriptMode = ScriptModeSurface;

    updateScriptButtonText();

    if (isScript && !d.scriptCode.isEmpty()) {
        m_surfaceScriptText = d.scriptCode;
        this->setProperty("rawSurfaceScript", d.scriptCode);

        // Blocca i segnali prima di fare clear per non innescare reset indesiderati
        bool bX = ui->lineX->blockSignals(true);
        bool bY = ui->lineY->blockSignals(true);
        bool bZ = ui->lineZ->blockSignals(true);
        bool bP = ui->lineP->blockSignals(true);

        ui->lineX->clear();
        ui->lineY->clear();
        ui->lineZ->clear();
        ui->lineP->clear();

        // Ripristina i segnali
        ui->lineX->blockSignals(bX);
        ui->lineY->blockSignals(bY);
        ui->lineZ->blockSignals(bZ);
        ui->lineP->blockSignals(bP);

        if (ui->glWidget) {
            // 1. Spegne m_isCustomMesh interno e azzera le funzioni base
            ui->glWidget->setParametricEquations("0", "0", "0", "0");

            // 2. Svuota la memoria delle variabili composte U, V, W
            if (ui->glWidget->getEngine()) {
                ui->glWidget->getEngine()->setExplicitU("");
                ui->glWidget->getEngine()->setExplicitV("");
                ui->glWidget->getEngine()->setExplicitW("");
            }
        }

        ui->txtScriptEditor->blockSignals(true);
        ui->txtScriptEditor->setPlainText(d.scriptCode);

        if (d.isImplicitMode) {
            parseAndApplyScriptParams(d.scriptCode);

            QString glslBody;
            QString scriptCopy = d.scriptCode;
            QTextStream stream(&scriptCopy);

            while (!stream.atEnd()) {
                QString line = stream.readLine();
                if (line.contains(":=")) continue;
                glslBody.append(line + "\n");
            }
            glslBody = GlslTranslator::translateEquation(glslBody);

            ui->glWidget->getEngine()->setScriptCodeGLSL(glslBody);
            ui->glWidget->getEngine()->setScriptMode(true);
            ui->glWidget->setRaySteps(ui->stepSlider->value());

            ui->glWidget->rebuildShader();

            ui->lineEquation->blockSignals(true);
            ui->lineEquation->setPlainText("// Controlled by Script");
            ui->lineEquation->blockSignals(false);

            applyAnimationState(hasTimeVariable(d.scriptCode));
        } else {
            // Esecuzione Parametrica standard
            onRunScriptClicked();
        }

        updateScriptButtonText();
        ui->txtScriptEditor->blockSignals(false);
    }
    else {
        ui->glWidget->setScriptCheck(false);
        m_surfaceScriptText.clear();

        if (d.isImplicitMode) {
            ui->lineEquation->setPlainText(d.implicitEq);
            ui->glWidget->setImplicitEquation(d.implicitEq);

            // Ripristina lo stile Shell o Solid
            if (isShell) {
                ui->radioShell->setChecked(true);
                if (ui->glWidget) ui->glWidget->setRenderMode(1);
            } else {
                ui->radioSolid->setChecked(true);
                if (ui->glWidget) ui->glWidget->setRenderMode(0);
            }
        } else {
            ui->tabModeSelector->setCurrentIndex(0); // Cambia al tab Parametric
            ui->glWidget->setEngineMode(GLWidget::ModeParametric);
        }
        ui->tabModeSelector->blockSignals(oldTabSig);

        bool bX = ui->lineX->blockSignals(true);
        bool bY = ui->lineY->blockSignals(true);
        bool bZ = ui->lineZ->blockSignals(true);
        bool bP = ui->lineP->blockSignals(true);

        ui->lineX->setPlainText(d.x);
        ui->lineY->setPlainText(d.y);
        ui->lineZ->setPlainText(d.z);
        ui->lineP->setPlainText(d.w);

        ui->lineX->blockSignals(bX);
        ui->lineY->blockSignals(bY);
        ui->lineZ->blockSignals(bZ);
        ui->lineP->blockSignals(bP);

        bool bCU = ui->lineU->blockSignals(true);
        bool bCV = ui->lineV->blockSignals(true);
        bool bCW = ui->lineW->blockSignals(true);

        ui->lineU->setPlainText(d.defU);
        ui->lineV->setPlainText(d.defV);
        ui->lineW->setPlainText(d.defW);

        ui->lineExplicitU->setPlainText(d.explicitU);
        ui->lineExplicitV->setPlainText(d.explicitV);
        ui->lineExplicitW->setPlainText(d.explicitW);

        ui->lineU->blockSignals(bCU);
        ui->lineV->blockSignals(bCV);
        ui->lineW->blockSignals(bCW);

        checkParametricDependency();

        if (!d.explicitU.isEmpty()) {
            ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
            ui->glWidget->getEngine()->setExplicitU(d.explicitU);
            ui->glWidget->getEngine()->setExplicitV("");
            ui->glWidget->getEngine()->setExplicitW("");
        }
        else if (!d.explicitV.isEmpty()) {
            ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
            ui->glWidget->getEngine()->setExplicitV(d.explicitV);
            ui->glWidget->getEngine()->setExplicitU("");
            ui->glWidget->getEngine()->setExplicitW("");
        }
        else {
            ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
            ui->glWidget->getEngine()->setExplicitW(d.explicitW);
            ui->glWidget->getEngine()->setExplicitU("");
            ui->glWidget->getEngine()->setExplicitV("");
        }

        ui->glWidget->setParametricEquations(
                    GlslTranslator::translateEquation(d.x),
                    GlslTranslator::translateEquation(d.y),
                    GlslTranslator::translateEquation(d.z),
                    GlslTranslator::translateEquation(d.w)
                    );
    }

    // --- 5. RESET E CARICAMENTO PATH ---

    // Path 3D
    ui->lineX_P3D->setText(d.path3D_x);
    ui->lineY_P3D->setText(d.path3D_y);
    ui->lineZ_P3D->setText(d.path3D_z);
    ui->lineR_P3D->setText(d.path3D_roll);

    ui->glWidget->getEngine()->compilePath3DEquations(d.path3D_x, d.path3D_y, d.path3D_z, d.path3D_roll);

    // Path 4D
    ui->lineX_P->setText(d.path4D_x);
    ui->lineY_P->setText(d.path4D_y);
    ui->lineZ_P->setText(d.path4D_z);
    ui->lineP_P->setText(d.path4D_w);
    ui->lineAlpha_P->setText(d.path4D_alpha);
    ui->lineBeta_P->setText(d.path4D_beta);
    ui->lineGamma_P->setText(d.path4D_gamma);

    ui->glWidget->getEngine()->compilePathEquations(
                d.path4D_x, d.path4D_y, d.path4D_z, d.path4D_w,
                d.path4D_alpha, d.path4D_beta, d.path4D_gamma
                );

    // Reset Variabili Tempo Locali
    pathTimeT = 0.0f;
    pathTimeT3D = 0.0f;
    this->setProperty("geoTime", 0.0);
    if (ui->glWidget) ui->glWidget->resetTime();

    updateRenderState();
    this->setProperty("isPresetActive", true);
    updateMasterButtonState();
}

QString MainWindow::presetsRootPath() const {
#if defined(Q_OS_ANDROID)
    return "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
#elif defined(Q_OS_IOS)
    // Percorso live dal sistema operativo, così non scade mai
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#else
    return QSettings().value("libraryRootPath").toString();
#endif
}

bool MainWindow::resolveNeedsCopy(const QString& src, const QString& dst,
                                  bool forceRestore, bool isDeleted, int* overwriteState)
{
    bool needsCopy = false;

    // --- CONTROLLO ESISTENZA E CONTENUTO ---
    if (!QFile::exists(dst)) {
        if (!isDeleted || forceRestore) needsCopy = true;
    } else if (forceRestore) {
        QFile srcFile(src);
        QFile dstFile(dst);
        if (srcFile.open(QIODevice::ReadOnly) && dstFile.open(QIODevice::ReadOnly)) {
            if (srcFile.readAll() != dstFile.readAll()) {

                if (overwriteState && *overwriteState == 1) {
                    needsCopy = true; // Yes To All
                } else if (overwriteState && *overwriteState == 2) {
                    needsCopy = false; // No To All
                } else {
                    // Chiediamo all'utente
                    QMessageBox msgBox(this);
                    msgBox.setWindowTitle("Modified Preset Detected");
                    msgBox.setText(QString("The preset '%1' has been modified.\nDo you want to overwrite it with the factory default?").arg(QFileInfo(dst).fileName()));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll);
                    msgBox.setDefaultButton(QMessageBox::No);

                    int ret = msgBox.exec();
                    if (ret == QMessageBox::Yes) {
                        needsCopy = true;
                    } else if (ret == QMessageBox::YesToAll) {
                        needsCopy = true;
                        if (overwriteState) *overwriteState = 1;
                    } else if (ret == QMessageBox::NoToAll) {
                        needsCopy = false;
                        if (overwriteState) *overwriteState = 2;
                    } else {
                        needsCopy = false; // No
                    }
                }
            }
        }
    }

    return needsCopy;
}


 // --- Parsing, Strings & Scripts ---

float MainWindow::parseMath(const QString &text, bool *ok)
{
    QString clean = text.trimmed();
    if (clean.isEmpty()) {
        if (ok) *ok = false;
        return 0.0f;
    }

    // Uniformiamo la virgola decimale (locale italiano)
    clean.replace(',', '.');

    bool parseOk = false;
    float v = ExpressionParser::evaluateSimple(clean, parseOk);
    if (ok) *ok = parseOk;
    return parseOk ? v : 0.0f;
}

float MainWindow::parseUIConstant(const QString &exprStr, float A, float B, float C, float D, float E, float F, float S, bool* ok)
{
    QString cleanExpr = exprStr.trimmed();
    if (cleanExpr.isEmpty()) { if (ok) *ok = true; return 0.0f; }

    // 1. Uniformiamo la punteggiatura
    cleanExpr.replace(",", ".");

    // 2. PASSAGGIO A DOUBLE: ExprTk è nativo e infallibile in double
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    symbol_table_t symbol_table;
    symbol_table.add_constants();

    // 3. AGGIUNTA MANUALE FORZATA: Nel caso add_constants() faccia i capricci
    symbol_table.add_constant("pi", 3.14159265358979323846);
    symbol_table.add_constant("PI", 3.14159265358979323846);
    symbol_table.add_constant("e",  2.71828182845904523536);
    symbol_table.add_constant("tau", 6.28318530717958647692);
    symbol_table.add_constant("TAU", 6.28318530717958647692);

    // 4. FIX FONDAMENTALE: Usiamo add_constant invece di add_variable!
    // Inserendo i numeri come costanti assolute evitiamo qualsiasi crash
    // di puntatori o reference in memoria da parte di ExprTk.
    symbol_table.add_constant("A", (double)A);
    symbol_table.add_constant("B", (double)B);
    symbol_table.add_constant("C", (double)C);
    symbol_table.add_constant("D", (double)D);
    symbol_table.add_constant("E", (double)E);
    symbol_table.add_constant("F", (double)F);
    symbol_table.add_constant("S", (double)S); symbol_table.add_constant("s", (double)S);

    expression_t expression;
    expression.register_symbol_table(symbol_table);

    parser_t parser;

    // 5. COMPILAZIONE
    if (parser.compile(cleanExpr.toStdString(), expression)) {
        if (ok) *ok = true;
        return static_cast<float>(expression.value());
    } else {
        if (ok) *ok = false;
        return 0.0f;
    }
}

QString MainWindow::composeEquation(const QString &eq, const QString &uDef, const QString &vDef, const QString &wDef) {
    if (eq.isEmpty()) return eq;

    QString res = eq;

    // Se un campo è vuoto, il suo valore di default è la rispettiva variabile minuscola.
    // Usiamo le parentesi per garantire l'ordine delle operazioni matematiche!
    QString subU = uDef.trimmed().isEmpty() ? "u" : "(" + uDef.trimmed() + ")";
    QString subV = vDef.trimmed().isEmpty() ? "v" : "(" + vDef.trimmed() + ")";
    QString subW = wDef.trimmed().isEmpty() ? "w" : "(" + wDef.trimmed() + ")";

    // \b indica un "word boundary", così sostituisce la "U" isolata,
    // ma ignora ad esempio la "U" dentro una parola fittizia
    res.replace(QRegularExpression("\\bU\\b"), subU);
    res.replace(QRegularExpression("\\bV\\b"), subV);
    res.replace(QRegularExpression("\\bW\\b"), subW);

    return res;
}

void MainWindow::parseAndApplyScriptParams(const QString &scriptCode, bool restartAudio)
{
    QRegularExpression re(R"(^\s*(u_min|u_max|v_min|v_max|w_min|w_max|steps|A|B|C|D|E|F|S)\s*[:=]+\s*([^;]+);)",
                          QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator i = re.globalMatch(scriptCode);

    bool limitsChanged = false;

    // Funzione per impostare valore e range dinamico dagli script
    auto setScriptSlider = [](QSlider* s, float val, bool isS) {
        int intVal = static_cast<int>(val * 100.0f);
        int newMin = isS ? std::min(-1000, intVal) : 0;
        int newMax = std::max(1000, intVal);
        s->setRange(newMin, newMax);
        s->setValue(intVal);
    };

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString varName = match.captured(1).toLower(); // es. "u_min"
        QString valStr  = match.captured(2);           // es. "-3.14" o "2*PI"

        // Usiamo il tuo parser per calcolare il valore (es. "2*PI" -> 6.28)
        float value = ExpressionParser::evaluateSimple(valStr);

        // Funzione per impostare valore E range dinamico dagli script
        if (varName == "u_min") { ui->uMinEdit->setText(QString::number(value, 'g', 12)); limitsChanged = true; }
        else if (varName == "u_max") { ui->uMaxEdit->setText(QString::number(value, 'g', 12)); limitsChanged = true; }
        else if (varName == "v_min") { ui->vMinEdit->setText(QString::number(value, 'g', 12)); limitsChanged = true; }
        else if (varName == "v_max") { ui->vMaxEdit->setText(QString::number(value, 'g', 12)); limitsChanged = true; }
        else if (varName == "w_min") { ui->wMinEdit->setText(QString::number(value, 'g', 12)); limitsChanged = true; }
        else if (varName == "w_max") { ui->wMaxEdit->setText(QString::number(value, 'g', 12)); limitsChanged = true; }
        else if (varName == "steps") { ui->stepSlider->setValue((int)value); }
        else if (varName == "a") { ui->aSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "b") { ui->bSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "c") { ui->cSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "d") { ui->dSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "e") { ui->eSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "f") { ui->fSlider->setValue(static_cast<int>(value * 100.0f)); }
        else if (varName == "s") { ui->sSlider->setValue(static_cast<int>(value * 100.0f)); }
    }

    // Se abbiamo cambiato i limiti, aggiorniamo subito le variabili interne del motore
    if (limitsChanged) {
        updateULimits();
        updateVLimits();
        updateWLimits();
    }

    if (!m_audioController->isPlaying()) {
        if (restartAudio) {
            QString globalCode = m_soundScriptText + "\n" + scriptCode + "\n"
                    + m_surfaceTextureCode + "\n" + m_bgTextureCode;
            m_audioController->playFromScript(globalCode);
        }
    }
}

bool MainWindow::hasTimeVariable(const QString& code) {
    QString cleanCode = code;

    // Rimuove i commenti di linea e di blocco per evitare falsi positivi (es. "don't")
    cleanCode.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
    cleanCode.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));

    static const QRegularExpression timeRegex("\\b(t|iTime|u_time)\\b");
    return cleanCode.contains(timeRegex);
}

QString MainWindow::extractAndResolveImagePath(const QString& scriptCode) {
    QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatch imgMatch = imgRe.match(scriptCode);

    if (!imgMatch.hasMatch()) return ""; // Nessun tag immagine trovato

    QString imgPath = imgMatch.captured(1).trimmed();
    if (QFile::exists(imgPath)) return imgPath; // Trovata al percorso originale!

    // --- SMART PATH RESOLVER (Ricerca automatica) ---
    QString fileName = QFileInfo(imgPath).fileName();
    QSettings settings;
    QString texDir = settings.value("pathTextures", settings.value("libraryRootPath").toString() + "/textures").toString();
    QDirIterator it(texDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);

    if (it.hasNext()) return it.next(); // Ritrovata nella nuova cartella!

    return "NOT_FOUND|" + imgPath; // Restituisce un flag per far gestire l'errore a chi l'ha chiamata
}

QString MainWindow::extractAudioDirectives(const QString& fullText) {
    QString extractedSound;

    // 1. Estrae file MP3/WAV (con Smart Path Resolver integrato)
    QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator musicIt = musicRe.globalMatch(fullText);

    while (musicIt.hasNext()) {
        QRegularExpressionMatch match = musicIt.next();
        QString rawPath = match.captured(1).trimmed();

        // A. Controlliamo se il file esiste al percorso originale
        if (QFile::exists(rawPath)) {
            extractedSound += "//MUSIC: " + rawPath + "\n";
        } else {
            // B. Se il percorso è rotto, cerchiamo il file nella cartella Sounds locale
            QString fileName = QFileInfo(rawPath).fileName();
            QSettings settings;
            QString rootPath;

            rootPath = presetsRootPath();

            QString sndDir = settings.value("pathSounds", rootPath + "/sounds").toString();

            // Cerca il file in tutte le sottocartelle dei suoni
            QDirIterator it(sndDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);

            if (it.hasNext()) {
                QString resolvedPath = it.next();
                extractedSound += "//MUSIC: " + resolvedPath + "\n"; // Sostituisce il path rotto con quello giusto!
            } else {
                // Fallback di sicurezza: rimette quello vecchio
                extractedSound += "//MUSIC: " + rawPath + "\n";
            }
        }
    }

    // 2. Estrae il formato procedurale (Sintesi GLSL) - Lasciato intatto
    QRegularExpression blockRe(R"(//\s*SOUND_BEGIN(.*?)//\s*SOUND_END)", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator blockIt = blockRe.globalMatch(fullText);
    while (blockIt.hasNext()) {
        extractedSound += "//SOUND_BEGIN\n" + blockIt.next().captured(1).trimmed() + "\n//SOUND_END\n";
    }

    return extractedSound.trimmed();
}

QString MainWindow::cleanCodeForComparison(QString str) {
    QRegularExpression blockRe(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    while (str.contains(blockRe)) str.remove(blockRe);
    str.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)",            QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
    str.remove(QRegularExpression(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
    str.remove(QRegularExpression(R"(^\s*//IMG:.*$\n?)",                       QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
    str.remove(QRegularExpression(R"(//.*$)",                                  QRegularExpression::MultilineOption));
    str.remove(QRegularExpression(R"(/\*.*?\*/)",                              QRegularExpression::DotMatchesEverythingOption));
    str.replace(QRegularExpression("\\s+"), "");
    return str;
}

 // --- UI State & Graphics ---

void MainWindow::updateLayoutForMode(int mode)
{
    bool old3D = ui->dock3D->blockSignals(true);
    bool old4D = ui->dock4D->blockSignals(true);

    if (mode == 1) { // 3D Mode
        if (!ui->dock3D->isVisible()) ui->dock3D->show();
        if (ui->dock4D->isVisible())  ui->dock4D->close();
        ui->dock3D->raise(); // Porta in primo piano
    }
    else if (mode == 2) { // 4D Mode
        if (ui->dock3D->isVisible())  ui->dock3D->close();
        if (!ui->dock4D->isVisible()) ui->dock4D->show();
        ui->dock4D->raise(); // Porta in primo piano
    }

    ui->dock3D->blockSignals(old3D);
    ui->dock4D->blockSignals(old4D);
}

void MainWindow::setupSpeedControl(QPushButton* btnPlus, QPushButton* btnMinus, QLabel* label, std::function<void(float)> setter) {

    // 1. Pulizia totale delle connessioni per evitare comandi fantasma
    disconnect(btnPlus, &QPushButton::clicked, nullptr, nullptr);
    disconnect(btnMinus, &QPushButton::clicked, nullptr, nullptr);

    auto changeVal = [this, label, setter](int direction) {
        // Usiamo un passo di 0.1
        float step = 0.1f;

        // 2. Lettura ultra-robusta: rimuove spazi e converte virgole in punti
        QString text = label->text().trimmed();
        text.replace(",", ".");

        bool ok;
        float currentVal = text.toFloat(&ok);
        if (!ok) currentVal = 0.0f;

        // 3. Calcolo del nuovo valore
        // Moltiplichiamo la direzione (1 o -1) per lo step
        float newVal = currentVal + (static_cast<float>(direction) * step);

        // 4. ARROTONDAMENTO CRITICO:
        // Arrotondiamo a 1 decimale PRIMA di ogni altra operazione
        newVal = std::round(newVal * 10.0f) / 10.0f;

        // 5. Gestione dello zero assoluto (Zero-Snap)
        // Se siamo molto vicini allo zero, forziamolo a 0.0 per resettare il segno
        if (std::abs(newVal) < 0.01f) {
            newVal = 0.0f;
        }

        // 6. Aggiornamento dell'etichetta
        if (newVal == 0.0f) {
            label->setText("0.0");
        } else {
            // 'f', 1 forza la visualizzazione di un decimale (es. -0.1)
            label->setText(QString::number(newVal, 'f', 1));
        }

        // 7. Invio al motore
        setter(newVal);

        // 8. Se tutto è fermo, riporta il tasto a START
        if (ui->glWidget) {
            bool anyMotion = std::abs(ui->glWidget->getNutationSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getPrecessionSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getSpinSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getPhiSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getPsiSpeed()) > 0.001f;

            if (!anyMotion) {
                    // Se si azzera tutto, fermiamo l'animazione per sicurezza
                    ui->glWidget->pauseMotion();
                    if (ui->btnStart_2) ui->btnStart_2->setText("GO");
                } else {
                    // Aggiorniamo il tasto solo in base allo stato REALE dell'animazione.
                    if (ui->btnStart_2) {
                        ui->btnStart_2->setText(ui->glWidget->isAnimating() ? "STOP" : "GO");
                    }
                }

                updateMasterButtonState();
                ui->glWidget->update();
        }
    };

    // Usiamo il contesto 'this' per garantire che la connessione sia stabile
    connect(btnPlus, &QPushButton::clicked, this, [changeVal](){ changeVal(1); });
    connect(btnMinus, &QPushButton::clicked, this, [changeVal](){ changeVal(-1); });
}

void MainWindow::updateProjectionButtonText()
{
    int mode = (int)ui->glWidget->projectionMode;
    QString txt;

    if (mode == 0) {
        txt = "Orthogonal";
    } else if (mode == 1) {
        txt = "Perspective";
    } else if (mode == 2) {
        txt = "Stereographic";
    }

    // Aggiorna il NUOVO tasto sulla status bar
    if (m_btnProjection) { m_btnProjection->setText(txt); }
}

void MainWindow::updateScriptButtonText() {
    bool isRayMarching = (ui->tabModeSelector->currentIndex() == 1);
    bool isBackground = ui->radioBackground->isChecked();

    QString rawText = ui->txtScriptEditor->toPlainText();

    // 1. ANALISI DEL TESTO: Cerchiamo il VERO codice GLSL
    QString codeOnly = rawText;
    // Rimuoviamo i tag audio e immagine per valutare se c'è logica procedurale
    codeOnly.remove(QRegularExpression(R"(^\s*//(SYNTH|MUSIC|IMG):.*$\n?)", QRegularExpression::MultilineOption));
    codeOnly.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));

    bool hasGLSLCode = !codeOnly.trimmed().isEmpty();
    bool hasAnyText = !rawText.trimmed().isEmpty();

    // 2. CONTROLLO MODIFICHE
    const QString editorText = rawText.trimmed();
    bool isModified = false;
    if (m_currentScriptMode == ScriptModeSurface) {
        isModified = (editorText != m_surfaceScriptText.trimmed());
    } else if (m_currentScriptMode == ScriptModeTexture) {
        isModified = isBackground
                ? (editorText != m_bgTextureScriptText.trimmed())
                : (editorText != m_surfaceTextureScriptText.trimmed());
    }

    // Regola 1: Tasto Modo sempre attivo per poter scorrere le Tab
    ui->btnScriptMode->setEnabled(true);

    // Variabili di stato per i pulsanti (Regole 2 e 4)
    bool enableRun = false;
    bool enableSave = false;

    // ==========================================
    // LOGICA PER TAB SUPERFICIE
    // ==========================================
    if (m_currentScriptMode == ScriptModeSurface) {
        ui->txtScriptEditor->setEnabled(true);

        // Lo stato del tasto rispecchia DIRETTAMENTE l'orologio della geometria,
        // non il testo dell'editor (vuoto al load per i record impliciti non-script).
        bool isSurfaceMoving = ui->glWidget && ui->glWidget->isSurfaceAnimating();

        if (isRayMarching) {
            ui->btnScriptMode->setText("Implicit Surface");
            ui->btnRunCurrentScript->setText(isSurfaceMoving ? "Stop Implicit" : "Run Implicit");
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for Implicit Surface (Ray Marching).\nExample: return length(p) - 1.0;");
        } else {
            ui->btnScriptMode->setText("Parametric Surface");
            ui->btnRunCurrentScript->setText(isSurfaceMoving ? "Stop Parametric" : "Run Parametric");
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for Parametric Surface.\nExample: return vec4(0.2 * u - 0.5, 0.2 * v - 0.5, 0.2 * sin(u * v), 1.0);");
        }

        // Sempre attivo se c'è codice; resta attivo se in moto (per poterla fermare).
        enableRun = hasGLSLCode || isSurfaceMoving;
        enableSave = hasGLSLCode;
    }

    // ==========================================
    // LOGICA PER TAB TEXTURE
    // ==========================================
    else if (m_currentScriptMode == ScriptModeTexture) {
        // Stato reale dell'orologio della texture interessata (sfondo o superficie).
        bool texMoving = false;
        if (ui->glWidget) {
            if (isBackground) {
                texMoving = ui->glWidget->isBackgroundTextureAnimating()
                        && ui->glWidget->isBackgroundTextureEnabled()
                        && hasTimeVariable(m_bgTextureCode);
            } else {
                texMoving = ui->glWidget->isSurfaceTextureAnimating()
                        && ui->chkBoxTexture->isChecked()
                        && hasTimeVariable(m_surfaceTextureCode);
            }
        }

        if (isBackground) {
            ui->btnScriptMode->setText("Texture");
            ui->btnRunCurrentScript->setText(texMoving ? "Stop Background Texture" : "Run Background Texture");
            ui->txtScriptEditor->setEnabled(true);
            ui->txtScriptEditor->setPlaceholderText("Write GLSL for Background Texture.\nExample: return vec3(uv.x, uv.y, 0.5);");

            enableRun = hasGLSLCode || texMoving;
            enableSave = hasGLSLCode;
        } else {
            if (isRayMarching) {
                ui->btnScriptMode->setText("Texture (Disabled)");
                ui->btnRunCurrentScript->setText("Not Available in Ray Marching");
                ui->txtScriptEditor->setEnabled(false);
                ui->txtScriptEditor->setPlaceholderText("Surface textures in Ray Marching are handled directly via the Equations Panel.");

                enableRun = false;
                enableSave = false;
            } else {
                ui->btnScriptMode->setText("Texture");
                ui->btnRunCurrentScript->setText(texMoving ? "Stop Parametric Texture" : "Run Parametric Texture");
                ui->txtScriptEditor->setEnabled(true);
                ui->txtScriptEditor->setPlaceholderText("Write GLSL for Parametric Texture.\nExample: return vec3(u / tau, v / tau, 1.0);");

                enableRun = hasGLSLCode || texMoving;
                enableSave = hasGLSLCode;
            }
        }
    }

    // ==========================================
    // LOGICA PER TAB SUONI
    // ==========================================
    else if (m_currentScriptMode == ScriptModeSound) {
        ui->btnScriptMode->setText("Sound");
        ui->txtScriptEditor->setEnabled(true);

        // ---> NUOVO PLACEHOLDER AUDIO <---
        ui->txtScriptEditor->setPlaceholderText("Add audio to your scene.\nExamples:\n//MUSIC: /path/to/song.mp3\n\n//SOUND_BEGIN\n// Write GLSL Synth Code here\n//SOUND_END");

        bool isPlaying = m_audioController && m_audioController->isPlaying();
        ui->btnRunCurrentScript->setText(isPlaying ? "Stop Sound" : "Run Sound");

        // I suoni agiscono come un interruttore Play/Stop, ignoriamo "isModified" qui
        enableRun = hasAnyText || isPlaying;
        enableSave = hasAnyText;
    }

    // Applicazione finale degli stati
    ui->btnRunCurrentScript->setEnabled(enableRun);
    ui->btnSaveScript->setEnabled(enableSave);

    // Aggiorniamo a cascata il bottone 2D
    updateFlatPreviewButton();

    ui->txtScriptEditor->update();
    if (ui->txtScriptEditor->viewport()) {
        ui->txtScriptEditor->viewport()->update();
    }
}

void MainWindow::updateTextureUIState(bool isTextureOn)
{
    // 1. Surface è abilitato SOLO se la texture è SPENTA
    ui->radioEditSurf->setEnabled(!isTextureOn);

    // 2. I controlli Colore Texture sono abilitati SOLO se la texture è ACCESA
    ui->radioTexColor1->setEnabled(isTextureOn);
    ui->radioTexColor2->setEnabled(isTextureOn);

    // 3. GESTIONE SPOSTAMENTO "PALLINO"
    if (isTextureOn) {
        if (ui->radioEditSurf->isChecked()) {
            bool oldBlock = ui->radioTexColor1->blockSignals(true);
            ui->radioTexColor1->setChecked(true);
            ui->radioTexColor1->blockSignals(oldBlock);
            onColorTargetChanged();
        }
    } else {
        bool oldBlock = ui->radioEditSurf->blockSignals(true);
        ui->radioEditSurf->setChecked(true);
        ui->radioEditSurf->blockSignals(oldBlock);
        onColorTargetChanged();
    }

    updateFlatPreviewButton();
}

void MainWindow::updateFlatPreviewButton() {
    bool isTextureScriptMode = (m_currentScriptMode == ScriptModeTexture);
    bool isParametric = (ui->tabModeSelector->currentIndex() == 0);
    bool bgMode = ui->radioBackground->isChecked();

    // 1. Gestione del Testo
    if (bgMode) {
        ui->btnFlatPreview->setText("2D Background");
    } else {
        ui->btnFlatPreview->setText("2D Surface");
    }

    // 2. Controllo attivazione fisica (Checkbox / Motore)
    bool isTexActive = bgMode ? ui->chkBoxTexture->isChecked() : m_surfaceTextureState;

    // 3. Regola di abilitazione:
    // Deve essere aperta la tab Texture (isTextureScriptMode)
    // La texture deve essere attiva (isTexActive)
    // Se è Background va bene tutto, se è Superficie DEVE essere Parametrica (isParametric)
    bool canBeEnabled = isTextureScriptMode && isTexActive && (bgMode || isParametric);

    ui->btnFlatPreview->setEnabled(canBeEnabled);

    // 4. Sicurezza: Se disabilitiamo il bottone, togliamo la spunta per evitare bug visivi
    if (!canBeEnabled && ui->btnFlatPreview->isChecked()) {
        ui->btnFlatPreview->setChecked(false);
    }
}

void MainWindow::updateMasterButtonState()
{
    if (!m_btnStart) return;

    // 1. Controllo Rotazioni 3D/4D
    bool rotActive = false;
    if (ui->glWidget) rotActive = ui->glWidget->isAnimating();
    // Controllo incrociato: se il tasto singolo dice GO, l'utente l'ha fermato
    if (ui->btnStart_2 && ui->btnStart_2->text() == "GO") {
        rotActive = false;
    }

    // 2. Controllo Audio
    bool audioActive = false;
    if (m_audioController) audioActive = m_audioController->isPlaying();
    if (m_currentScriptMode == ScriptModeSound && ui->btnRunCurrentScript) {
        if (ui->btnRunCurrentScript->text() == "Run Sound") {
            audioActive = false;
        } else if (!audioActive) {
            // AUTO-FIX: Se l'audio è finito da solo, resetta il tasto laterale
            ui->btnRunCurrentScript->setText("Run Sound");
        }
    }

    // 3. Controllo animazione intrinseca (variabile tempo 't' e texture)
    bool surfaceActive = false;
    if (ui->glWidget) {
        bool isRM = (ui->tabModeSelector->currentIndex() == 1);

        // A. Orologio della Geometria Principale
        bool geomClockRunning = ui->glWidget->isSurfaceAnimating();

        QString mainEq;
        if (isRM) {
            mainEq = ui->lineEquation->toPlainText() + " " + ui->lineVariations->toPlainText() + " " + m_surfaceScriptText;
        } else {
            mainEq = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
                    ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText() + " " +
                    ui->lineU->toPlainText() + " " + ui->lineV->toPlainText() + " " + ui->lineW->toPlainText() + " " +
                    ui->lineExplicitU->toPlainText() + " " + ui->lineExplicitV->toPlainText() + " " + ui->lineExplicitW->toPlainText() + " " +
                    m_surfaceScriptText;
            if (ui->lnU) {
                mainEq += " " + ui->lnU->toPlainText() + " " + ui->lnV->toPlainText() + " " + ui->lnW->toPlainText() +
                        " " + ui->lndU->toPlainText() + " " + ui->lndV->toPlainText() + " " + ui->lndW->toPlainText() +
                        " " + ui->lineConform->toPlainText();
            }
        }

        bool geomHasTime = hasTimeVariable(mainEq);
        bool isGeomVisuallyMoving = geomClockRunning && geomHasTime;

        // B. Orologio della Texture di Superficie
        bool isSurfTexActive = ui->radioBackground->isChecked() ? m_surfaceTextureState : ui->chkBoxTexture->isChecked();
        bool isTexVisuallyMoving;
        if (isRM) {
            // displacement -> orologio geometria (geomClockRunning); colore -> orologio texture
            bool texColorMoving = ui->glWidget->isSurfaceTextureAnimating() && isSurfTexActive &&
                                  hasTimeVariable(ui->lineTexture->toPlainText());
            bool dispMoving     = geomClockRunning && isSurfTexActive &&
                                  hasTimeVariable(ui->lineVariations->toPlainText());
            isTexVisuallyMoving = texColorMoving || dispMoving;
        } else {
            bool texClockRunning = ui->glWidget->isSurfaceTextureAnimating();
            bool texHasTime = isSurfTexActive && hasTimeVariable(m_surfaceTextureCode);
            isTexVisuallyMoving = texClockRunning && texHasTime;
        }

        // C. Orologio della Texture di Sfondo
        bool bgClockRunning = ui->glWidget->isBackgroundTextureAnimating();
        bool bgHasTime = ui->glWidget->isBackgroundTextureEnabled() && hasTimeVariable(m_bgTextureCode);
        bool isBgVisuallyMoving = bgClockRunning && bgHasTime;

        // La grafica è "attiva" SOLO se c'è almeno un elemento che usa il tempo E il suo orologio è acceso
        surfaceActive = isGeomVisuallyMoving || isTexVisuallyMoving || isBgVisuallyMoving;

        if (ui->btnTextureCode) {
            ui->btnTextureCode->setText(isTexVisuallyMoving ? "Stop" : "Run");
        }
    }

    // 4. Controllo Timer Flusso Geodetico
    QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
    bool geoActive = (geoAnimTimer && geoAnimTimer->isActive());

    // 5. Tiriamo le somme: c'è ALMENO UNA cosa che si sta muovendo/suonando fisicamente?
    bool somethingIsMoving = rotActive ||
                             (pathTimer && pathTimer->isActive()) ||
                             (pathTimer3D && pathTimer3D->isActive()) ||
                             surfaceActive ||
                             audioActive ||
                             geoActive;

    // 6. Aggiorniamo dinamicamente il Master Button
    m_btnStart->setText(somethingIsMoving ? "STOP" : "START");

    updateScriptButtonText();
}

void MainWindow::applyAnimationState(bool animated) {
    const bool effective = animated && !m_masterStopped;

    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(effective);
        ui->glWidget->setSurfaceTextureAnimating(effective);
        ui->glWidget->setBackgroundTextureAnimating(effective);
    }

    updateMasterButtonState();
}

void MainWindow::generateTexture()
{
    // 1. Crea un'immagine 512x512
    int size = 512;
    QImage img(size, size, QImage::Format_RGBA8888);
    QPainter p(&img);

    // 2. Sfondo (Colore 1)
    p.fillRect(0, 0, size, size, m_texColor1);

    // 3. Scacchi (Colore 2)
    p.setBrush(m_texColor2);
    p.setPen(Qt::NoPen);
    int step = 64; // Dimensione quadretti

    for (int y=0; y<size; y+=step) {
        for (int x=0; x<size; x+=step) {
            // Disegna a scacchiera
            if (((x/step) + (y/step)) % 2 == 1) {
                p.drawRect(x, y, step, step);
            }
        }
    }
    p.end();

    // 4. Invia al GLWidget
    if (ui->glWidget) {
        ui->glWidget->loadTextureFromImage(img);
    }
}

void MainWindow::toggleProjection()
{
    // Leggi modo attuale forzandolo a intero (0=Ortho, 1=Persp, 2=Wide)
    int current = (int)ui->glWidget->projectionMode;

    // Calcola il prossimo (aggiunge 1 e torna a 0 quando arriva a 3)
    int nextMode = (current + 1) % 3;

    // Applica
    ui->glWidget->setProjectionMode(nextMode);

    // Aggiorna il testo e forza il repaint
    updateProjectionButtonText();
    ui->glWidget->update();
}

void MainWindow::showTopMessage(const QString& msg, bool isError)
{
    m_topMessageBar->setText(msg);

    // Stile "Toast" moderno
    UiStyleManager::applyToastStyle(m_topMessageBar, isError);

    m_topMessageBar->adjustSize();

    // Posizionalo al centro-alto (es. a 40 pixel dal top)
    int x = (this->centralWidget()->width() - m_topMessageBar->width()) / 2;
    int y = 40;

    m_topMessageBar->move(x, y);
    m_topMessageBar->show();
    m_topMessageBar->raise(); // Assicurati che stia sopra l'OpenGL

    // Nascondi automaticamente dopo 3 secondi
    m_topMessageTimer->start(3000);
}

void MainWindow::hideTopMessage()
{
    if (m_topMessageBar && m_topMessageBar->isVisible()) {
        m_topMessageTimer->stop(); // Ferma il timer per evitare conflitti
        m_topMessageBar->hide();   // Nascondi immediatamente
    }
}

bool MainWindow::applyBackgroundTextureIfNeeded() {
    if (!ui->glWidget->isBackgroundTextureEnabled()) return true;

    QString bgSrc = (m_currentScriptMode == ScriptModeTexture && ui->radioBackground->isChecked())
            ? ui->txtScriptEditor->toPlainText()
            : m_bgTextureScriptText;
    bool bgHasLogic = bgSrc.contains("return") || bgSrc.contains("vec3")
            || bgSrc.contains("vec4") || bgSrc.contains("mainImage");
    if (bgHasLogic) {
        if (!ui->glWidget->validateAndApplyBackgroundShader(bgSrc)) {
            InputValidator::showShaderCompilationError(this,
                "Syntax Error (Background Texture)", ui->glWidget->getShaderError());
            return false;
        }
        m_bgTextureCode = bgSrc; // applicata e valida: committa
    }
    return true;
}


 // --- Geometry & Geodesic Flow ---

namespace {
constexpr const char* kActiveEqProps[] = {
    "active_lineX", "active_lineY", "active_lineZ", "active_lineP",
    "active_lnU",   "active_lnV",   "active_lnW",
    "active_lndU",  "active_lndV",  "active_lndW",
    "active_lineConform"
};
}

void MainWindow::snapshotActiveEquations() {
    setProperty("active_lineX", ui->lineX->toPlainText());
    setProperty("active_lineY", ui->lineY->toPlainText());
    setProperty("active_lineZ", ui->lineZ->toPlainText());
    setProperty("active_lineP", ui->lineP->toPlainText());
    if (ui->lnU) {
        setProperty("active_lnU",   ui->lnU->toPlainText());
        setProperty("active_lnV",   ui->lnV->toPlainText());
        setProperty("active_lnW",   ui->lnW->toPlainText());
        setProperty("active_lndU",  ui->lndU->toPlainText());
        setProperty("active_lndV",  ui->lndV->toPlainText());
        setProperty("active_lndW",  ui->lndW->toPlainText());
        setProperty("active_lineConform", ui->lineConform->toPlainText());
    }
}

QStringList MainWindow::readActiveEquations() const {
    QStringList out;
    out.reserve(int(std::size(kActiveEqProps)));
    for (const char* name : kActiveEqProps) {
        out << property(name).toString();
    }
    return out;
}

void MainWindow::restoreActiveEquations(const QStringList &saved) {
    Q_ASSERT(saved.size() == int(std::size(kActiveEqProps)));
    for (size_t i = 0; i < std::size(kActiveEqProps); ++i) {
        setProperty(kActiveEqProps[i], saved[int(i)]);
    }
}

void MainWindow::commitUiFieldsDuringMotion() {
    if (!m_btnStart || m_btnStart->text().toUpper() != "STOP") return;
    m_geodesicErrorPending = false;

    QString mainEqs = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
                      ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText();
    int upperCount = (mainEqs.contains(QRegularExpression("\\bU\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bV\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bW\\b")) ? 1 : 0);
    bool geoHasText = hasGeodesicText();
    bool isGeodesicActive = (upperCount > 0) && geoHasText &&
            (ui->tabModeSelector->currentIndex() == 0);
    if (!isGeodesicActive) {
        onStartClicked();
        return;
    }

    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, true}, {ui->uMaxEdit, true},
        {ui->vMinEdit, true}, {ui->vMaxEdit, true},
    };
    QVector<float> dummy;
    auto parseFn = [this](const QString& s, bool* ok) { return this->parseMath(s, ok); };
    if (!InputValidator::validateAndParseLimits(this, limitFields, parseFn, dummy)) {
        return;  // popup mostrato dal validator, niente dry-run
    }

    // Snapshot dei valori attuali prima di provare i nuovi.
        const QStringList previous = readActiveEquations();
        bool wasTimerActive = isGeodesicMotionActive();

        // Tentativo: applichiamo i nuovi e validiamo.
        snapshotActiveEquations();
        if (updateGeodesicMesh()) {
            return;  // OK: i nuovi valori restano nelle active_*, il moto continua.
        }

        // Errore: ripristiniamo i valori validi precedenti.
        // Il tasto rimane su STOP; mostriamo un solo popup.
        restoreActiveEquations(previous);
        m_geodesicErrorPending = false;
        setProperty("geoErrorType", "none");

        if (!property("geoErrorShown").toBool()) {
            setProperty("geoErrorShown", true);
            InputValidator::showGeodesicSingularityError(this);
        }

        // Riavvia il timer con i dati ripristinati (se era in moto).
        if (wasTimerActive) {
            QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
            if (geoAnimTimer && !geoAnimTimer->isActive())
                geoAnimTimer->start();
        }
    }

void MainWindow::commitFieldsOnEnter() {
    m_geodesicErrorPending = false;

    // In moto: usa la logica live già esistente.
    if (m_btnStart && m_btnStart->text().toUpper() == "STOP") {
        commitUiFieldsDuringMotion();
        return;
    }

    // A superficie ferma: applica solo nel tab parametrico.
    if (ui->tabModeSelector->currentIndex() != 0) return;

    // Stesso routing di checkAndTriggerMeshUpdate: geodetico vs standard.
    QString mainEqs = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
                      ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText();
    int upperCount = (mainEqs.contains(QRegularExpression("\\bU\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bV\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bW\\b")) ? 1 : 0);

    if ((upperCount > 0) && hasGeodesicText()) {
        if (!updateGeodesicMesh()
                && property("geoErrorType").toString() == "singularity"
                && !property("geoErrorShown").toBool()) {
            setProperty("geoErrorShown", true);
            InputValidator::showGeodesicSingularityError(this);
        }
        return;
    }

    // Standard / composition / constraint: applica equazioni, composizioni e
    // vincoli correnti e rigenera la mesh, senza far ripartire moto/rotazioni/audio.
    setProperty("rmApplyOnly", true);
    onStartClicked();                 // sender != m_btnStart -> niente toggle START/STOP
    setProperty("rmApplyOnly", false);
}

bool MainWindow::updateGeodesicMesh()
{
    // Resettiamo il flag degli errori per questa esecuzione
    this->setProperty("geoErrorType", "none");

    if (m_geodesicErrorPending) return false;

    // --- 1. LETTURA E VALIDAZIONE DEI LIMITI (U/V sempre attivi nel geodesic) ---
    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, true}, {ui->uMaxEdit, true},
        {ui->vMinEdit, true}, {ui->vMaxEdit, true},
    };

    QVector<float> limitValues;
    bool allOk = true;
    for (const auto& field : limitFields) {
        if (!field.active) {
            limitValues.append(0.0f);
            continue;
        }
        bool ok = false;
        float v = parseMath(field.edit->text(), &ok);
        if (!ok) { allOk = false; break; }
        limitValues.append(v);
    }
    if (!allOk) {
        return false;
    }

    float uMin = limitValues[0], uMax = limitValues[1];
    float vMin = limitValues[2], vMax = limitValues[3];

    int steps = ui->stepSlider->value();
    int safeSteps = std::min(steps, 500);  // Limite fisico per le geodetiche

    // Controllo Min >= Max (U e V sempre attivi nel geodesic)
    if (!InputValidator::validateLimits(this, uMin, uMax, true, vMin, vMax, true, 0.0f, 0.0f, false)) {
        stopGeodesicAnimation();
        return false;
    }

    // 1. GESTIONE TEMPO (Animazione)
    double t = this->property("geoTime").toDouble();

    if (this->property("isInitialLoad").toBool()) {
        if (ui->glWidget) ui->glWidget->setUpdatesEnabled(false);
        if (m_statusLabel) {
            m_statusLabel->setStyleSheet("color: #00bfff; font-weight: bold;");
        }
        this->setProperty("isInitialLoad", false);
    }

    // --- LOGICA DI DISACCOPPIAMENTO PER IL MOTO CORRENTE ---
    bool isRunning = isGeodesicMotionActive();

    QString rawX = (isRunning && this->property("active_lineX").isValid()) ? this->property("active_lineX").toString() : ui->lineX->toPlainText();
    QString rawY = (isRunning && this->property("active_lineY").isValid()) ? this->property("active_lineY").toString() : ui->lineY->toPlainText();
    QString rawZ = (isRunning && this->property("active_lineZ").isValid()) ? this->property("active_lineZ").toString() : ui->lineZ->toPlainText();
    QString rawP = (isRunning && this->property("active_lineP").isValid()) ? this->property("active_lineP").toString() : ui->lineP->toPlainText();

    QString rawU = (isRunning && this->property("active_lnU").isValid()) ? this->property("active_lnU").toString() : (ui->lnU ? ui->lnU->toPlainText() : "");
    QString rawV = (isRunning && this->property("active_lnV").isValid()) ? this->property("active_lnV").toString() : (ui->lnV ? ui->lnV->toPlainText() : "");
    QString rawW = (isRunning && this->property("active_lnW").isValid()) ? this->property("active_lnW").toString() : (ui->lnW ? ui->lnW->toPlainText() : "");

    QString rawDU = (isRunning && this->property("active_lndU").isValid()) ? this->property("active_lndU").toString() : (ui->lndU ? ui->lndU->toPlainText() : "");
    QString rawDV = (isRunning && this->property("active_lndV").isValid()) ? this->property("active_lndV").toString() : (ui->lndV ? ui->lndV->toPlainText() : "");
    QString rawDW = (isRunning && this->property("active_lndW").isValid()) ? this->property("active_lndW").toString() : (ui->lndW ? ui->lndW->toPlainText() : "");

    QString rawConf = (isRunning && this->property("active_lineConform").isValid()) ? this->property("active_lineConform").toString() : (ui->lineConform ? ui->lineConform->toPlainText() : "");
    // --- FINE LOGICA DI DISACCOPPIAMENTO ---

    QRegularExpression varRegex("\\b(u|v|w|U|V|W|x|y|z|t|iTime|u_time)\\b");

    if (!rawConf.contains(varRegex)) {
        float valA = ui->aSlider->value() / 100.0f;
        float valB = ui->bSlider->value() / 100.0f;
        float valC = ui->cSlider->value() / 100.0f;
        float valD = ui->dSlider->value() / 100.0f;
        float valE = ui->eSlider->value() / 100.0f;
        float valF = ui->fSlider->value() / 100.0f;
        float valS = ui->sSlider->value() / 100.0f;

        float lambdaVal = parseUIConstant(rawConf, valA, valB, valC, valD, valE, valF, valS);

        if (lambdaVal <= 1e-8f) {
            m_geodesicErrorPending = true;
            QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
            if (geoAnimTimer && geoAnimTimer->isActive()) geoAnimTimer->stop();
            if (!property("geoErrorShown").toBool()) {
                setProperty("geoErrorShown", true);
                InputValidator::showInvalidConformalConstantError(this);
            }
            return false;
        }
    }

    SurfaceEngine* engine = ui->glWidget->getEngine();
    QRhi* rhi = ui->glWidget->getRhi(); // <-- Recuperiamo l'interfaccia GPU

    if (!rhi) {
        return false;
    }

    // ==============================================================
    // 2.5 ESTRAZIONE DELLE COSTANTI
    // ==============================================================
    {
        auto clampConst = [this](QLineEdit* edit, float raw) -> float {
            if (raw < 0.0f) {
                float prev = m_lastValidConst.value(edit, 1.0f);
                QSignalBlocker b(edit);
                edit->setText(QString::number(prev, 'g', 6));
                return prev;
            }
            m_lastValidConst[edit] = raw;
            return raw;
        };

        float cA = clampConst(ui->lineA, parseUIConstant(ui->lineA->text(), 0, 0, 0, 0, 0, 0, 0));
        float cB = clampConst(ui->lineB, parseUIConstant(ui->lineB->text(), cA, 0, 0, 0, 0, 0, 0));
        float cC = clampConst(ui->lineC, parseUIConstant(ui->lineC->text(), cA, cB, 0, 0, 0, 0, 0));
        float cD = clampConst(ui->lineD, parseUIConstant(ui->lineD->text(), cA, cB, cC, 0, 0, 0, 0));
        float cE = clampConst(ui->lineE, parseUIConstant(ui->lineE->text(), cA, cB, cC, cD, 0, 0, 0));
        float cF = clampConst(ui->lineF, parseUIConstant(ui->lineF->text(), cA, cB, cC, cD, cE, 0, 0));
        float cS = parseUIConstant(ui->lineS->text(), cA, cB, cC, cD, cE, cF, 0);
        if (ui->glWidget) ui->glWidget->setEquationConstants(cA, cB, cC, cD, cE, cF, cS);

        if (!m_inGeoAnimTick &&
                !geodesicFieldsAreFinite({rawU, rawV, rawW, rawDU, rawDV, rawDW,
                                         rawConf, rawX, rawY, rawZ, rawP},
                                         uMin, uMax, vMin, vMax,
                                         cA, cB, cC, cD, cE, cF, cS)) {
            m_geodesicErrorPending = true;
            QTimer* gt = this->findChild<QTimer*>("geoAnimTimer");
            if (gt && gt->isActive()) gt->stop();
            this->setProperty("geoErrorType", "nonfinite");
            if (!property("geoErrorShown").toBool()) {
                setProperty("geoErrorShown", true);
                InputValidator::showGeodesicSingularityError(this);
            }
            return false;
        }
    }

    // Recupera la mappa delle costanti (già calcolate a cascata da onStartClicked)
    QMap<QString, float> constantsMap = ui->glWidget->getConstantsMap();

    QString shaderError; // <--- Prepariamo la stringa per l'errore

    // 3. CALCOLO SINCRONO SU GPU
    QVector<QVector<QVector4D>> grid = engine->computeGeodesicFlow(
                rhi,
                rawX, rawY, rawZ, rawP,
                rawU, rawV, rawW,
                rawDU, rawDV, rawDW,
                rawConf,
                uMin, uMax, safeSteps,
                vMin, vMax, safeSteps,
                constantsMap,
                static_cast<float>(t),       // <--- NUOVO: tempo come uniform
                &shaderError
                );

    // --- GESTIONE DEGLI ERRORI ---
    if (grid.isEmpty()) {
        if (!shaderError.isEmpty()) {
            m_geodesicErrorPending = true;
            QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
            if (geoAnimTimer && geoAnimTimer->isActive()) geoAnimTimer->stop();
            setProperty("geoErrorShown", true);
            InputValidator::showShaderCompilationError(this, "Geodesic Shader Error", shaderError);
            this->setProperty("geoErrorType", "syntax");
        } else {
            m_geodesicErrorPending = true;
            QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
            if (geoAnimTimer && geoAnimTimer->isActive()) geoAnimTimer->stop();
            this->setProperty("geoErrorType", "singularity");
        }
        return false;
    }

    // 4. APPLICHIAMO IMMEDIATAMENTE IL RISULTATO
    if (!grid.isEmpty()) {
        if (!ui->glWidget->setCustomMesh(grid)) {
            m_geodesicErrorPending = true;
            QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
            if (geoAnimTimer && geoAnimTimer->isActive()) geoAnimTimer->stop();
            this->setProperty("geoErrorType", "singularity");
            return false;
        }

        // Ripristiniamo la UI post-caricamento (caso successo)
        if (ui->glWidget && !ui->glWidget->updatesEnabled())
            ui->glWidget->setUpdatesEnabled(true);
    }

    // 5. LOGICA DEL TIMER CPU PER ANIMAZIONI
    QString geoEqs = rawX + " " + rawY + " " + rawZ + " " + rawP + " " +
            rawU + " " + rawV + " " + rawW + " " +
            rawDU + " " + rawDV + " " + rawDW + " " + rawConf;

    bool hasTime = hasTimeVariable(geoEqs);

    QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
    if (!geoAnimTimer) {
        geoAnimTimer = new QTimer(this);
        geoAnimTimer->setObjectName("geoAnimTimer");
        geoAnimTimer->setInterval(16);

        connect(geoAnimTimer, &QTimer::timeout, this, [this]() {
            // Guardia 1: finestra nascosta (app in background su iOS/Android)
            if (!isVisible()) return;

            // Guardia 2: contesto GPU non ancora ripristinato dopo il ritorno
            if (!ui->glWidget || !ui->glWidget->getRhi()) return;

            // Guardia 3: l'utente sta editando una costante (A..F / S).
            {
                QWidget* fw = qApp->focusWidget();
                if (fw == ui->lineA || fw == ui->lineB || fw == ui->lineC ||
                        fw == ui->lineD || fw == ui->lineE || fw == ui->lineF ||
                        fw == ui->lineS)
                    return;
            }

            double currentT = this->property("geoTime").toDouble();
            this->setProperty("geoTime", currentT + 0.015);

            m_inGeoAnimTick = true;
            bool meshOk = updateGeodesicMesh();
            m_inGeoAnimTick = false;
            if (!meshOk) {
                if (this->property("geoErrorType").toString() == "singularity") {
                    InputValidator::showAnimatedGeodesicSingularityError(this);
                }
            }
        });
    }

    if (hasTime && m_btnStart && m_btnStart->text() == "STOP") {
        if (!geoAnimTimer->isActive()) geoAnimTimer->start();
    } else {
        if (geoAnimTimer->isActive()) geoAnimTimer->stop();
    }

    setProperty("geoErrorShown", false);
    m_geodesicErrorPending = false;
    return true;
}

void MainWindow::checkAndTriggerMeshUpdate() {
    if (!ui->glWidget) return;

    if (ui->glWidget->getEngine() && ui->glWidget->getEngine()->isScriptModeActive()) {
        ui->glWidget->update(); // Aggiorna solo la visualizzazione (Uniforms)
        return;                 // Uscita anticipata per proteggere la GPU
    }

    // 1. Recupero equazioni principali
    QString mainEqs = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " + ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText();

    // 2. Analisi variabili composte (U, V, W)
    int upperCount = (mainEqs.contains(QRegularExpression("\\bU\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bV\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bW\\b")) ? 1 : 0);

    // 3. Verifica presenza campi Geodetici
    bool geoHasText = hasGeodesicText();

    // 4. Routing: se Geodetico è attivo e siamo nel tab Parametrico (0), usa il calcolatore Tensoriale
    if ((upperCount > 0) && geoHasText && (ui->tabModeSelector->currentIndex() == 0)) {
        if (m_geodesicErrorPending) return;

        bool success = updateGeodesicMesh();
        if (!success) {
            return;
        }
        hideTopMessage();
    } else {
        // Altrimenti, rigenera la griglia poligonale standard
        ui->glWidget->updateSurfaceData();
        ui->glWidget->update();
    }
}

void MainWindow::stopGeodesicAnimation()
{
    QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
    if (geoAnimTimer && geoAnimTimer->isActive()) geoAnimTimer->stop();

    if (m_btnStart) m_btnStart->setText("START");

    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(false);
        if (!ui->glWidget->updatesEnabled())
            ui->glWidget->setUpdatesEnabled(true);
    }
}

bool MainWindow::isGeodesicMotionActive() const {
    QTimer* t = findChild<QTimer*>("geoAnimTimer");
    return t && t->isActive();
}

bool MainWindow::hasGeodesicText() const {
    if (!ui->lnU) return false;
    return !ui->lnU->toPlainText().trimmed().isEmpty()  ||
           !ui->lnV->toPlainText().trimmed().isEmpty()  ||
           !ui->lnW->toPlainText().trimmed().isEmpty()  ||
           !ui->lndU->toPlainText().trimmed().isEmpty() ||
           !ui->lndV->toPlainText().trimmed().isEmpty() ||
           !ui->lndW->toPlainText().trimmed().isEmpty();
}

bool MainWindow::geodesicFieldsAreFinite(const QStringList& exprs,
                                         float uMin, float uMax,
                                         float vMin, float vMax,
                                         float A, float B, float C,
                                         float D, float E, float F, float S)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    // Variabili legate per riferimento. u,v,t li facciamo variare;
    // gli altri restano a un valore interno "sicuro" così le espressioni che li
    // citano compilano e non incappano nella loro singolarità naturale di bordo
    // (es. il fattore conforme di Poincaré che esplode solo a U^2+V^2+W^2 = 1).
    double u = 0, v = 0, t = 0;
    double w = 0.137, U = 0.137, V = 0.137, W = 0.137;
    double x = 0.137, y = 0.137, z = 0.137, iTime = 0, u_time = 0;

    symbol_table_t st;
    st.add_constants();
    st.add_constant("pi", 3.14159265358979323846);
    st.add_constant("PI", 3.14159265358979323846);
    st.add_constant("e",  2.71828182845904523536);
    st.add_constant("tau", 6.28318530717958647692);
    st.add_constant("TAU", 6.28318530717958647692);
    st.add_constant("A", (double)A); st.add_constant("B", (double)B);
    st.add_constant("C", (double)C); st.add_constant("D", (double)D);
    st.add_constant("E", (double)E); st.add_constant("F", (double)F);
    st.add_constant("S", (double)S); st.add_constant("s", (double)S);
    st.add_variable("u", u); st.add_variable("v", v); st.add_variable("w", w);
    st.add_variable("t", t);
    st.add_variable("U", U); st.add_variable("V", V); st.add_variable("W", W);
    st.add_variable("x", x); st.add_variable("y", y); st.add_variable("z", z);
    st.add_variable("iTime", iTime); st.add_variable("u_time", u_time);

    // Punti campione interni: evitiamo gli estremi esatti per non scambiare una
    // singolarità legittima di bordo (es. il fattore conforme di Poincaré, che
    // esplode solo a U^2+V^2+W^2 = 1) per un errore.
    const double fu = uMax - uMin, fv = vMax - vMin;
    const double us[3] = { uMin + 0.25*fu, uMin + 0.5*fu, uMin + 0.75*fu };
    const double vs[3] = { vMin + 0.25*fv, vMin + 0.5*fv, vMin + 0.75*fv };
    const double ts[3] = { 0.0, 0.5, 1.0 };

    for (const QString& raw : exprs) {
        QString clean = raw.trimmed();
        if (clean.isEmpty()) continue;
        clean.replace(",", ".");

        expression_t expr;
        expr.register_symbol_table(st);
        parser_t parser;
        if (!parser.compile(clean.toStdString(), expr))
            continue;   // errore di sintassi: lo gestisce la validazione esistente

        for (double su : us)
            for (double sv : vs)
                for (double stime : ts) {
                    u = su; v = sv; t = stime; iTime = stime; u_time = stime;
                    if (!isMeshSafeValue(expr.value()))
                        return false;
                }
    }
    return true;
}


// ==========================================
// PROTECTED
// ==========================================


void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (windowState() & Qt::WindowMinimized) {
            if (m_audioController && m_audioController->isPlaying()) {
                m_audioController->stopAll();
            }
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::hideEvent(QHideEvent* event)
{
    // Ferma l'audio
    if (m_audioController && m_audioController->isPlaying()) {
        m_audioController->stopAll();
    }

    // Ferma il timer del flusso geodetico e memorizza che era attivo,
    // così possiamo ripristinarlo al ritorno in foreground.
    // Necessario su iOS: al ritorno dal background il QRhi può essere
    // valido ma le risorse Metal non ancora ripristinate -> grid vuota
    // -> falso popup di singolarita'.
    QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
    if (geoAnimTimer && geoAnimTimer->isActive()) {
        geoAnimTimer->stop();
        this->setProperty("geoAnimTimerWasRunning", true);
    }

    QMainWindow::hideEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // Riavvia il timer geodetico solo se era stato fermato da hideEvent.
    // Il delay (300 ms) serve a dare al backend grafico il tempo di
    // ricreare le risorse GPU su iOS/Android prima del primo tick.
    if (this->property("geoAnimTimerWasRunning").toBool()) {
        this->setProperty("geoAnimTimerWasRunning", false);

        QTimer::singleShot(300, this, [this]() {
            // Ricontrolla che l'utente non abbia premuto STOP nel frattempo
            if (!m_btnStart || m_btnStart->text() != "STOP") return;
            if (!isVisible()) return;
            if (!ui->glWidget || !ui->glWidget->getRhi()) return;

            QTimer* geoAnimTimer = this->findChild<QTimer*>("geoAnimTimer");
            if (geoAnimTimer && !geoAnimTimer->isActive()) {
                geoAnimTimer->start();
            }
        });
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}
