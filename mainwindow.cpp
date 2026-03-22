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

#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
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
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>
#include <functional>
#include <QDirIterator>
#include <QInputDialog>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QDropEvent>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // =========================================================================
    // 1. CORE INITIALIZATION
    // =========================================================================
    ui->setupUi(this);

    //QSettings().clear(); // Primo avvio dell'applicazione

    setWindowTitle("Surfaces Explorer");
    setAttribute(Qt::WA_AcceptTouchEvents);

    m_isCustomMode = false;
    m_isImageMode = false;
    m_blockTextureGen = false;
    m_currentTexturePath = "";
    m_surfaceTextureState = false;

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

    QFont boldFont = ui->lineX->font(); // Prende il font di base già assegnato dal tema
    boldFont.setBold(true);             // Lo trasforma in grassetto

    // Applica il nuovo font a tutti i campi principali
    ui->lineX->setFont(boldFont);
    ui->lineY->setFont(boldFont);
    ui->lineZ->setFont(boldFont);
    ui->lineP->setFont(boldFont);

    ui->lineX_P3D->setFont(boldFont);
    ui->lineY_P3D->setFont(boldFont);
    ui->lineZ_P3D->setFont(boldFont);
    ui->lineR_P3D->setFont(boldFont);

    ui->lineX_P->setFont(boldFont);
    ui->lineY_P->setFont(boldFont);
    ui->lineZ_P->setFont(boldFont);
    ui->lineP_P->setFont(boldFont);

    ui->lineAlpha_P->setFont(boldFont);
    ui->lineBeta_P->setFont(boldFont);
    ui->lineGamma_P->setFont(boldFont);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // A. Lista dei contenitori principali da compattare
    QList<QWidget*> dockContents = {
        ui->dockEquations->widget(),
        ui->dockRenders->widget(),
        ui->dockScripts->widget(),
        ui->dock3D->widget(),
        ui->dock4D->widget(),
        ui->dockSurfaces->widget()
    };

    // B. Lista delle righe degli slider (per forzare altezza minima e tocco facile)
    QList<QWidget*> sliderRows = {
        ui->panelRed,
        ui->panelGreen,
        ui->panelBlue,
        ui->panelSliderTrans
    };

    // C. Lista delle label valori (per evitare che il numero "salti" quando cambia cifre)
    QList<QLabel*> valueLabels = {
        ui->valR,
        ui->valG,
        ui->valB,
        ui->lblAlphaVal
    };

    // CHIAMATA UNICA CENTRALIZZATA
    UiStyleManager::compactForMobile(
                dockContents,       // Widget generali
                ui->panelColor,     // Pannello colori (per fix margini)
                sliderRows,         // Righe slider
                valueLabels         // Label valori
                );
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

    if (ui->dockEquations) addScrollToDock(ui->dockEquations);
    if (ui->dockRenders) addScrollToDock(ui->dockRenders);
    if (ui->dock3D) addScrollToDock(ui->dock3D);
    if (ui->dock4D) addScrollToDock(ui->dock4D);
    if (ui->dockScripts) addScrollToDock(ui->dockScripts);
    if (ui->dockSurfaces) addScrollToDock(ui->dockSurfaces);

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
                           "<b>Surface Explorer 4D</b><br>"
                           "Version 1.0<br><br>"
                           "Developed by: <b>Dioscorid</b><br>"
                           "License: <b>GNU GPL v3</b><br><br>"
                           "This is free software: you are free to change and redistribute it "
                           "under the terms of the GNU General Public License as published by "
                           "the Free Software Foundation.<br><br>"
                           "<i>Exploring the beauty of four-dimensional geometry.</i>");
    });

    ui->actionDocumentation->setMenuRole(QAction::NoRole);
    connect(ui->actionDocumentation, &QAction::triggered, this, [this](){
        QSettings settings;
        QStringList repos = settings.value("repositoryPaths").toStringList();
        QString targetDir = !repos.isEmpty() ? repos.first() : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

        QString docPath = targetDir + "/documentation.html";
        QString credPath = targetDir + "/CREDITS.txt"; // <--- Preparazione percorso CREDITS

        // Estrazione Documentazione
        if (QFile::exists(docPath)) QFile::remove(docPath);
        if (QFile::copy(":/documentation.html", docPath)) {
            QFile::setPermissions(docPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
        }

        // Estrazione File di Testo dei Crediti
        if (QFile::exists(credPath)) QFile::remove(credPath);
        if (QFile::copy(":/CREDITS.txt", credPath)) {
            QFile::setPermissions(credPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
        }

        // Apertura nel Browser
        if (QFile::exists(docPath)) QDesktopServices::openUrl(QUrl::fromLocalFile(docPath));
        else QMessageBox::warning(this, "Error", "Documentation file not found in resources.\nMake sure 'documentation.html' is added to your .qrc file.");
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
    m_btnRec->setStyleSheet("QPushButton { color: red; font-weight: bold; }");
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
        if (dock->isVisible()) { dock->close(); return; }
        closeAllDocks();
#if defined(Q_OS_IOS)
        QTimer::singleShot(10, this, [this, dock](){
            if (!dock->isFloating()) dock->setFloating(true);
            int screenW = this->width();
            int screenH = this->height();
            int w = screenW * 0.40; if (w < 320) w = 320;
            int h = screenH - 40;
            int winX = this->geometry().x();
            int winY = this->geometry().y();
            dock->setGeometry(winX, winY, w, h);
            dock->show();
            dock->raise();
        });
#else
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

    // =========================================================================
    // 6. EQUATIONS, CONSTANTS & PARAMETERS
    // =========================================================================
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
    if (ui->lineS->text().isEmpty()) ui->lineS->setText("0");

    // --- MOTORE COSTANTI A CASCATA ---
    auto evaluateCascade = [this]() {
        float valA = std::max(0.0f, parseUIConstant(ui->lineA->text(), 0, 0, 0, 0, 0, 0, 0));
        float valB = std::max(0.0f, parseUIConstant(ui->lineB->text(), valA, 0, 0, 0, 0, 0, 0));
        float valC = std::max(0.0f, parseUIConstant(ui->lineC->text(), valA, valB, 0, 0, 0, 0, 0));
        float valD = std::max(0.0f, parseUIConstant(ui->lineD->text(), valA, valB, valC, 0, 0, 0, 0));
        float valE = std::max(0.0f, parseUIConstant(ui->lineE->text(), valA, valB, valC, valD, 0, 0, 0));
        float valF = std::max(0.0f, parseUIConstant(ui->lineF->text(), valA, valB, valC, valD, valE, 0, 0));
        float valS = parseUIConstant(ui->lineS->text(), valA, valB, valC, valD, valE, valF, 0);

        auto setSmartSlider = [](QSlider* s, float v, bool isS) {
            bool old = s->blockSignals(true);
            int intVal = static_cast<int>(v * 100.0f);

            // Garantisce un limite standard di 10 (1000) o espande se il valore digitato è maggiore
            int newMin = isS ? std::min(-1000, intVal) : 0;
            int newMax = std::max(1000, intVal);

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
        setSmartSlider(ui->sSlider, valS, true);

        if (ui->glWidget) {
            ui->glWidget->setEquationConstants(valA, valB, valC, valD, valE, valF, valS);

            // Ricostruiamo la griglia poligonale con i nuovi parametri
            ui->glWidget->makeCurrent();
            ui->glWidget->updateSurfaceData();
            ui->glWidget->doneCurrent();

            // Diciamo allo schermo di rinfrescare l'immagine
            ui->glWidget->update();
        }
    };

    // 1. Quando premi Invio o perdi il focus: Ricalcola TUTTO senza toccare i testi (salva la formula!)
    connect(ui->lineA, &QLineEdit::editingFinished, this, evaluateCascade);
    connect(ui->lineB, &QLineEdit::editingFinished, this, evaluateCascade);
    connect(ui->lineC, &QLineEdit::editingFinished, this, evaluateCascade);
    connect(ui->lineD, &QLineEdit::editingFinished, this, evaluateCascade);
    connect(ui->lineE, &QLineEdit::editingFinished, this, evaluateCascade);
    connect(ui->lineF, &QLineEdit::editingFinished, this, evaluateCascade);
    connect(ui->lineS, &QLineEdit::editingFinished, this, evaluateCascade);

    // 2. Quando l'utente FORZA un nuovo valore spostando lo slider col mouse: sovrascrivi la formula e ricalcola
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

    evaluateCascade();

    ui->stepSlider->setRange(10, 1000);
    int initialSteps = 100;
    ui->stepSlider->setValue(initialSteps);
    ui->lineSteps->setText(QString::number(initialSteps));
    ui->glWidget->setResolution(initialSteps);
    ui->lblSteps->setText(QString("Steps="));

    connect(ui->stepSlider, &QSlider::valueChanged, this, [this](int val){
        if (!ui->lineSteps->hasFocus()) ui->lineSteps->setText(QString::number(val));
        if(ui->glWidget) {
            ui->glWidget->setResolution(val);
            ui->glWidget->updateSurfaceData();
            ui->glWidget->update();
        }
    });

    connect(ui->lineSteps, &QLineEdit::editingFinished, this, [this](){
        bool ok;
        int val = ui->lineSteps->text().toInt(&ok);
        if (ok) {
            int min = ui->stepSlider->minimum();
            int max = ui->stepSlider->maximum();
            val = std::clamp(val, min, max);
            ui->stepSlider->setValue(val);
            ui->lineSteps->setText(QString::number(val));
            ui->lineSteps->clearFocus();
        }
    });

    // 1. Dipendenze delle equazioni principali
    connect(ui->lineX, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineY, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineZ, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineP, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->txtScriptEditor, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

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

    checkParametricDependency();
    updateConstraintState();

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
        if (id != 3 && ui->radioBackground->isChecked()) {
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
            }
            if (m_savedRenderMode != 2) m_surfaceTextureState = ui->chkBoxTexture->isChecked();

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

        // FIX: SINCRONIZZA L'ALBERO TEXTURE AL CAMBIO MODALITÀ
        ui->treeTextures->clearSelection();
        QTreeWidgetItemIterator itTex(ui->treeTextures);
        QString activeCode = ui->radioBackground->isChecked() ? m_bgTextureCode : m_surfaceTextureCode;

        activeCode.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)", QRegularExpression::MultilineOption));
        activeCode.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));
        activeCode.replace(QRegularExpression("\\s+"), "");

        if (!activeCode.isEmpty()) {
            while (*itTex) {
                QVariant vTex = (*itTex)->data(0, Qt::UserRole + 1);
                if (vTex.isValid()) {
                    int idx = vTex.toInt();
                    const LibraryItem &texItem = m_libraryManager.getTexture(idx);
                    bool isMatch = false;

                    if (texItem.isImage) {
                        // MATCH ROBUSTO PER IMMAGINI: Estrae e confronta solo il nome del file
                        QString fileName = QFileInfo(texItem.filePath).fileName();
                        if (!fileName.isEmpty() && activeCode.contains(fileName)) {
                            isMatch = true;
                        }
                    } else {
                        QString cleanCode = texItem.scriptCode;
                        cleanCode.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)", QRegularExpression::MultilineOption));
                        cleanCode.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));
                        cleanCode.replace(QRegularExpression("\\s+"), "");

                        if (!cleanCode.isEmpty() && activeCode == cleanCode) isMatch = true;
                    }

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
            onColorTargetChanged();
        }
        else {
            m_surfaceTextureState = checked;
            updateTextureUIState(checked);
            if (ui->glWidget) ui->glWidget->setTextureEnabled(checked);

            if (!m_blockTextureGen) {
                if (checked) {
                    if (m_isCustomMode) m_isCustomMode = false;
                    m_isImageMode = false;

                    if (ui->glWidget) ui->glWidget->setTextureColors(m_texColor1, m_texColor2);

                    bool oldBlock = ui->radioTexColor1->blockSignals(true);
                    ui->radioTexColor1->setChecked(true);
                    ui->radioTexColor1->blockSignals(oldBlock);
                    onColorTargetChanged();

                    generateTexture();

                    ui->glWidget->makeCurrent();
                    ui->glWidget->rebuildShader();
                    ui->glWidget->doneCurrent();
                }
            }
        }
        updateFlatPreviewButton();
        ui->glWidget->update();
    });

    connect(ui->btnWireUPlus,  &QPushButton::clicked, this, [this](){ ui->glWidget->increaseWireframeUDensity(); });
    connect(ui->btnWireVPlus,  &QPushButton::clicked, this, [this](){ ui->glWidget->increaseWireframeVDensity(); });
    connect(ui->btnWireUMinus, &QPushButton::clicked, this, [this](){ ui->glWidget->decreaseWireframeUDensity(); });
    connect(ui->btnWireVMinus, &QPushButton::clicked, this, [this](){ ui->glWidget->decreaseWireframeVDensity(); });

    // Colori Default
    float defR = 0.20f, defG = 0.80f, defB = 0.20f;
    m_currentSurfaceColor = QColor::fromRgbF(defR, defG, defB);
    m_currentBorderColor  = QColor::fromRgbF(defR, defG, defB);
    m_texColor1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
    m_texColor2 = Qt::black;

    if (ui->glWidget) {
        ui->glWidget->setColor(defR, defG, defB);
        ui->glWidget->setBorderColor(defR, defG, defB);
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
        if(checked) ui->btnBorder->setStyleSheet("color: #44FF44; font-weight: bold;");
        else        ui->btnBorder->setStyleSheet("");
        ui->radioEditBorder->setEnabled(checked);

        if (!checked && ui->radioEditBorder->isChecked()) {
            ui->radioEditSurf->setChecked(true);
            onColorTargetChanged();
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
    ui->btnScriptMode->setText("Surface");
    ui->btnRunCurrentScript->setText("Run Surface");

    ui->btnFlatPreview->setText("2D View");
    ui->btnFlatPreview->setEnabled(false);

    connect(ui->btnFlatPreview, &QPushButton::toggled, this, [this](bool checked){
        if (checked) {
            ui->btnFlatPreview->setText("3D View");
            ui->radioBasic->setEnabled(false); ui->radioPhong->setEnabled(false); ui->radioWF->setEnabled(false); ui->alphaSlider->setEnabled(false);
        } else {
            updateFlatPreviewButton();
            ui->radioBasic->setEnabled(true); ui->radioPhong->setEnabled(true); ui->radioWF->setEnabled(true); ui->alphaSlider->setEnabled(true);
        }
        if (ui->radioBackground->isChecked()) ui->glWidget->setFlatViewTarget(1);
        else ui->glWidget->setFlatViewTarget(0);

        ui->glWidget->setFlatView(checked);
        ui->glWidget->update();
    });

    updateFlatPreviewButton();

    ui->txtScriptEditor->setPlaceholderText("Write GLSL code for custom texture.\nExample: return vec3(u, v, 0.0);");

    // =========================================================================
    // 10. LIBRARY TREES & FILE SYSTEM
    // =========================================================================
    QSettings settings;
    QStringList repos = settings.value("repositoryPaths").toStringList();

    if (!repos.isEmpty() && QDir(repos.first()).exists()) lastTextureFolder = repos.first();
    else {
        QString osBaseDir;
#ifdef Q_OS_ANDROID
        osBaseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (osBaseDir.isEmpty()) osBaseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
        tree->setHeaderHidden(true); tree->setColumnCount(1);
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tree->setDragDropMode(QAbstractItemView::InternalMove);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        QScroller::grabGesture(tree, QScroller::TouchGesture);
        tree->grabGesture(Qt::TapAndHoldGesture);
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

    this->resize(1280, 720);
    this->showMaximized();
}

// ==========================================================
// UI & STATE MANAGEMENT
// ==========================================================

void MainWindow::switchToMainMode()
{
    updateLayoutForMode(0);
}

void MainWindow::switchTo3DMode()
{
    updateLayoutForMode(1);

    ui->glWidget->set4DLighting(false);
    updateProjectionButtonText();
}

void MainWindow::switchTo4DMode()
{
    updateLayoutForMode(2);

    ui->glWidget->set4DLighting(true);
    updateProjectionButtonText();
}

void MainWindow::resetInterface() {
    // 1. Ferma le animazioni
    onStopClicked();
    ui->glWidget->resetTransformations();

    // 2. Pulisci campi testo
    ui->lineX->setPlainText("0");
    ui->lineY->setPlainText("0");
    ui->lineZ->setPlainText("0");
    ui->lineP->setPlainText("0");

    ui->lineExplicitW->clear();
    ui->lineExplicitU->clear();
    ui->lineExplicitV->clear();

    ui->lineU->clear();
    ui->lineV->clear();
    ui->lineW->clear();

    ui->aSlider->setValue(0); ui->bSlider->setValue(0); ui->cSlider->setValue(0);
    ui->dSlider->setValue(0); ui->eSlider->setValue(0); ui->fSlider->setValue(0);
    ui->sSlider->setValue(0);

    if (ui->glWidget->getEngine()) {
        ui->glWidget->getEngine()->setExplicitW("");
        ui->glWidget->getEngine()->setExplicitU("");
        ui->glWidget->getEngine()->setExplicitV("");
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
    }

    // 3. Reset Limiti standard
    if (ui->btnBorder) { ui->btnBorder->setText("Border OFF"); }
    ui->radioEditBorder->setEnabled(false);

    // 5. RESET MODALITÀ DI RENDER (Importante!)
    if (ui->radioBasic) ui->radioBasic->setChecked(true);
    if (ui->radioWF) ui->radioWF->setChecked(false);

    m_surfaceTextureState = false;
    ui->chkBoxTexture->setText("Texture");

    ui->alphaSlider->setValue(100);

    // 6. RESET COLORI (Verde Default)
    float defR = 0.20f, defG = 0.80f, defB = 0.20f;

    // Aggiorna variabili e motore
    m_currentSurfaceColor = QColor::fromRgbF(defR, defG, defB);
    m_currentBorderColor = QColor::fromRgbF(defR, defG, defB);

    ui->glWidget->setColor(defR, defG, defB);
    ui->glWidget->setBorderColor(defR, defG, defB);

    // Resetta la selezione su "Surface"
    ui->radioEditSurf->setChecked(true);

    // Aggiorna gli slider visualmente
    onColorTargetChanged();

    // 7. Reset Sfondo
    m_currentBackgroundColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
    ui->glWidget->setBackgroundColor(m_currentBackgroundColor);
    ui->glWidget->setBackgroundTextureEnabled(false);

    // 8. Aggiorna tutto e pulisci schermo
    updateULimits();
    updateVLimits();

    ui->glWidget->resetVisuals();
    ui->glWidget->repaint();
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
    // 1. RECUPERA LO STATO SALVATO (Non dai bottoni, che potrebbero essere spenti)
    int mode = m_savedRenderMode; // 0=Base, 1=Phong, 2=WF
    bool wantTexture = ui->chkBoxTexture->isChecked();

    if (ui->radioBackground->isChecked()) {
        wantTexture = m_surfaceTextureState;
    } else {
        wantTexture = ui->chkBoxTexture->isChecked();
    }

    // 2. LOGICA SPECIALIZZATA
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

    // 3. APPLICAZIONE AL WIDGET
    if (ui->glWidget) {
        ui->glWidget->setRenderMode(mode);
        ui->glWidget->setSpecularEnabled(isPhong);

        // Texture attiva solo se checkbox ON e non siamo in wireframe
        ui->glWidget->setTextureEnabled(wantTexture && (m_savedRenderMode != 2));

        ui->glWidget->update();
    }
}
// 1. Recuperiamo il testo dalle 4 equazioni coordinate + SCRIPT

void MainWindow::checkParametricDependency()
{
    QString eqX = ui->lineX->toPlainText();
    QString eqY = ui->lineY->toPlainText();
    QString eqZ = ui->lineZ->toPlainText();
    QString eqP = ui->lineP->toPlainText();
    QString script = ui->txtScriptEditor->toPlainText();
    QString activeSurfaceScript = m_surfaceScriptText;

    QString eqExplU = ui->lineExplicitU->toPlainText();
    QString eqExplV = ui->lineExplicitV->toPlainText();
    QString eqExplW = ui->lineExplicitW->toPlainText();

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

    // 2. MACCHINA A STATI: Apertura e Blocco Tab intelligente (Chirurgico)
    bool needsConstraint = false;
    bool needsComposition = false;

    if (ui->tabWidget_2) {
        if (rawLowerCount == 3 && rawUpperCount == 0) {
            // Solo 3 minuscole: Vincoli attivi, Composizioni SPENTE
            needsConstraint = true;
            ui->tabWidget_2->setTabEnabled(0, true);  // Accende linguetta Vincoli
            ui->tabWidget_2->setTabEnabled(1, false); // Ingrigisce linguetta Composizioni
            ui->tabWidget_2->setCurrentIndex(0);
        }
        // ---> FIX: ACCETTA SIA 2 CHE 3 MAIUSCOLE (es. U, V oppure U, V, W) <---
        else if ((rawUpperCount == 2 || rawUpperCount == 3) && rawLowerCount == 0) {
            // Solo maiuscole: Composizioni attive, Vincoli SPENTI
            needsComposition = true;
            ui->tabWidget_2->setTabEnabled(0, false); // Ingrigisce linguetta Vincoli
            ui->tabWidget_2->setTabEnabled(1, true);  // Accende linguetta Composizioni
            ui->tabWidget_2->setCurrentIndex(1);
        }
        else {
            // Qualsiasi altra combinazione (2 minuscole, miste, ecc): TUTTO SPENTO
            ui->tabWidget_2->setTabEnabled(0, false);
            ui->tabWidget_2->setTabEnabled(1, false);
        }
    }

    // 3. Applica stato fisico ai campi dei Vincoli
    ui->lineExplicitU->setEnabled(needsConstraint);
    ui->lineExplicitV->setEnabled(needsConstraint);
    ui->lineExplicitW->setEnabled(needsConstraint);

    if (!needsConstraint) {
        ui->lineExplicitU->blockSignals(true); ui->lineExplicitU->clear(); ui->lineExplicitU->blockSignals(false);
        ui->lineExplicitV->blockSignals(true); ui->lineExplicitV->clear(); ui->lineExplicitV->blockSignals(false);
        ui->lineExplicitW->blockSignals(true); ui->lineExplicitW->clear(); ui->lineExplicitW->blockSignals(false);

        if (ui->glWidget && ui->glWidget->getEngine()) {
            ui->glWidget->getEngine()->setExplicitW("");
        }
    }

    // --- NUOVO: Abilitazione selettiva dei campi Composition ---
    ui->lineU->setEnabled(needsComposition && hasRaw_U);
    ui->lineV->setEnabled(needsComposition && hasRaw_V);
    ui->lineW->setEnabled(needsComposition && hasRaw_W);

    if (!needsComposition || !hasRaw_U) {
        ui->lineU->blockSignals(true); ui->lineU->clear(); ui->lineU->blockSignals(false);
    }
    if (!needsComposition || !hasRaw_V) {
        ui->lineV->blockSignals(true); ui->lineV->clear(); ui->lineV->blockSignals(false);
    }
    if (!needsComposition || !hasRaw_W) {
        ui->lineW->blockSignals(true); ui->lineW->clear(); ui->lineW->blockSignals(false);
    }

    // 4. ANALISI COMPOSTA: Accensione degli Slider (Rigidamente Case-Sensitive!)
    QString allEqs = mainEqs + " " + eqExplU + " " + eqExplV + " " + eqExplW;
    QString composedAllEqs = composeEquation(allEqs, defU, defV, defW);

    updateConstraintState();
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

    auto applyLimitsState = [](QLineEdit* minEdit, QLineEdit* maxEdit, bool enable, bool zeroOut) {
        minEdit->setEnabled(enable);
        maxEdit->setEnabled(enable);

        if (zeroOut) {
            bool b1 = minEdit->blockSignals(true);
            minEdit->setText("0");
            minEdit->blockSignals(b1);

            bool b2 = maxEdit->blockSignals(true);
            maxEdit->setText("0");
            maxEdit->blockSignals(b2);
        }
    };

    QString styleActive = "QPlainTextEdit { background-color: #1E1E1E; color: #FFFFFF; font-weight: bold; border: 1px solid #007ACC; }";
    QString styleInactive = "QPlainTextEdit { background-color: #1E1E1E; color: #666666; border: 1px solid #3E3E42; } QPlainTextEdit:focus { border: 1px solid #007ACC; }";
    QString styleDefault = "QPlainTextEdit { background-color: #1E1E1E; color: #FFFFFF; border: 1px solid #3E3E42; } QPlainTextEdit:focus { border: 1px solid #007ACC; }";
    QString styleDisabled = "QPlainTextEdit { background-color: #252526; color: #555555; border: 1px solid #333; } QPlainTextEdit:focus { border: 1px solid #007ACC; background-color: #1E1E1E; }";

    if (hasConstraintU) {
        ui->lineExplicitU->setStyleSheet(styleActive);
        ui->lineExplicitV->setStyleSheet(styleInactive);
        ui->lineExplicitW->setStyleSheet(styleInactive);

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, false, false);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV, false);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW, false);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
    }
    else if (hasConstraintV) {
        ui->lineExplicitV->setStyleSheet(styleActive);
        ui->lineExplicitU->setStyleSheet(styleInactive);
        ui->lineExplicitW->setStyleSheet(styleInactive);

        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, false, false);
        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU, false);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW, false);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
    }
    else {
        if (hasConstraintW) {
            ui->lineExplicitW->setStyleSheet(styleActive);
            ui->lineExplicitU->setStyleSheet(styleInactive);
            ui->lineExplicitV->setStyleSheet(styleInactive);

            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, false, false);
        } else {
            if (usesW) {
                ui->lineExplicitW->setStyleSheet(styleDefault);
                ui->lineExplicitU->setStyleSheet(styleDefault);
                ui->lineExplicitV->setStyleSheet(styleDefault);
            } else {
                ui->lineExplicitW->setStyleSheet(styleDisabled);
                ui->lineExplicitU->setStyleSheet(styleDisabled);
                ui->lineExplicitV->setStyleSheet(styleDisabled);
            }
            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW, false);
        }

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU, false);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV, false);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
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

    // 2. CONTROLLO MODALITÀ SFONDO
    if (ui->radioBackground->isChecked()) {
        if (data.isImage) {
            ui->glWidget->setBackgroundTexture(data.filePath);

            m_bgTextureCode = "//IMG:" + data.filePath;

            if (ui->glWidget) {
                ui->glWidget->setProperty("bg_zoom", data.zoom);
                ui->glWidget->setProperty("bg_pan", QVector2D(data.panX, data.panY));
                ui->glWidget->setProperty("bg_rot", data.rotation);
            }

            QString surfCode = ui->txtScriptEditor->toPlainText();
            ui->txtScriptEditor->blockSignals(true);
            ui->txtScriptEditor->setPlainText(m_bgTextureCode);
            ui->txtScriptEditor->blockSignals(false);

            // RIMOSSO onRunSoundClicked();
        }
        else {
            QColor c1 = data.hasCustomColors ? QColor(data.color1) : QColor::fromRgbF(0.2f, 0.2f, 0.8f);
            QColor c2 = data.hasCustomColors ? QColor(data.color2) : Qt::black;

            m_bgTexColor1 = c1;
            m_bgTexColor2 = c2;

            if (ui->glWidget) {
                ui->glWidget->setProperty("bg_col1", QVector3D(c1.redF(), c1.greenF(), c1.blueF()));
                ui->glWidget->setProperty("bg_col2", QVector3D(c2.redF(), c2.greenF(), c2.blueF()));
                ui->glWidget->setProperty("bg_zoom", data.zoom);
                ui->glWidget->setProperty("bg_pan", QVector2D(data.panX, data.panY));
                ui->glWidget->setProperty("bg_rot", data.rotation);
            }

            m_bgTextureCode = data.scriptCode;
            m_bgTextureScriptText = m_bgTextureCode;

            QString prevEditorText = ui->txtScriptEditor->toPlainText();
            ui->txtScriptEditor->blockSignals(true);
            ui->txtScriptEditor->setPlainText(m_bgTextureCode);
            onApplyTextureScriptClicked();

            if (m_currentScriptMode != ScriptModeTexture) {
                ui->txtScriptEditor->setPlainText(prevEditorText);
            }
            ui->txtScriptEditor->blockSignals(false);
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
        return;
    }

    // 3. CONTROLLO MODALITÀ SUPERFICIE
    m_currentTexturePath = data.filePath;
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

    if (data.isImage) {
        if (ui->glWidget) {
            ui->glWidget->loadTextureFromFile(data.filePath);
            ui->glWidget->setTextureEnabled(true);
            ui->glWidget->makeCurrent();
            ui->glWidget->rebuildShader();
            ui->glWidget->doneCurrent();

            if (!ui->chkBoxTexture->isChecked()) {
                bool old = ui->chkBoxTexture->blockSignals(true);
                ui->chkBoxTexture->setChecked(true);
                ui->chkBoxTexture->blockSignals(old);
                updateTextureUIState(true);
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

        QString surfCode = ui->txtScriptEditor->toPlainText();
        ui->txtScriptEditor->blockSignals(true);
        ui->txtScriptEditor->setPlainText(m_surfaceTextureCode);
        ui->txtScriptEditor->blockSignals(false);

        // RIMOSSO onRunSoundClicked();
    }
    else if (!data.scriptCode.isEmpty()) {
        m_isImageMode = false;

        QString newCode = data.scriptCode;
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

    m_blockTextureGen = false;
    updateFlatPreviewButton();
}


// ==========================================================
// EQUATIONS & MATHEMATICS
// ==========================================================

void MainWindow::updateULimits() {
    uMin = parseMath(ui->uMinEdit->text());
    uMax = parseMath(ui->uMaxEdit->text());
    if (ui->glWidget) ui->glWidget->setRangeU(uMin, uMax);
}

void MainWindow::updateVLimits() {
    vMin = parseMath(ui->vMinEdit->text());
    vMax = parseMath(ui->vMaxEdit->text());
    if (ui->glWidget) ui->glWidget->setRangeV(vMin, vMax);
}

void MainWindow::updateWLimits() {
    wMin = parseMath(ui->wMinEdit->text());
    wMax = parseMath(ui->wMaxEdit->text());
    if (ui->glWidget) ui->glWidget->setRangeW(wMin, wMax);
}


// ==========================================================
// ANIMATION, MOTION & TIMERS
// ==========================================================

void MainWindow::onStartClicked()
{
    if (m_btnStart && m_btnStart->text().toUpper() == "STOP") {
        ui->glWidget->setSurfaceAnimating(false);
        m_btnStart->setText("START");
        return;
    }

    if (ui->lineX->toPlainText().trimmed().isEmpty() && ui->glWidget->getEngine()->isScriptModeActive()) {
        QString currentScript = ui->txtScriptEditor->toPlainText();
        if (currentScript.contains(QRegularExpression("\\bt\\b"))) {
            ui->glWidget->setSurfaceAnimating(true);
            if (m_btnStart) m_btnStart->setText("STOP");
        }
        return;
    }

    // --- 0. SMART INTERCEPTOR ---
    QString allEqs = ui->lineX->toPlainText() + " " + ui->lineY->toPlainText() + " " +
                     ui->lineZ->toPlainText() + " " + ui->lineP->toPlainText() + " " +
                     ui->lineU->toPlainText() + " " + ui->lineV->toPlainText() + " " +
                     ui->lineW->toPlainText();

    // Cerca se le variabili 'u', 'v' e 'w' esistono come parole isolate
    bool hasU = allEqs.contains(QRegularExpression("\\bu\\b"));
    bool hasV = allEqs.contains(QRegularExpression("\\bv\\b"));
    bool hasW = allEqs.contains(QRegularExpression("\\bw\\b"));

    // Controlla se l'utente ha attivato volontariamente un vincolo (Constraint)
    bool hasExplicit = !ui->lineExplicitU->toPlainText().trimmed().isEmpty() ||
                       !ui->lineExplicitV->toPlainText().trimmed().isEmpty() ||
                       !ui->lineExplicitW->toPlainText().trimmed().isEmpty();

    // Se usa 'w' ma manca 'u' o 'v', e non ha impostato vincoli, blocca l'avvio!
    if (hasW && (!hasU || !hasV) && !hasExplicit) {
        QMessageBox::warning(this,
                             "Parameter Error",
                             "You used the variable 'w' instead of 'u' or 'v' without setting a 3D/4D constraint.\n\n"
                             "In standard 2D surfaces, the two base parametric variables must be 'u' and 'v'.\n"
                             "Please replace 'w' with the missing variable in your equations, or add a constraint in the Constraints panel to draw the figure.");

        return; // Interrompe onStartClicked qui, lasciando la grafica invariata
    }

    // 1. FORCE FOCUS RELEASE
    ui->glWidget->setFocus();

    // 2. SAFETY CHECK: VARIABLES AND SYNTAX
    QString eqX = ui->lineX->toPlainText();
    QString eqY = ui->lineY->toPlainText();
    QString eqZ = ui->lineZ->toPlainText();
    QString eqP = ui->lineP->toPlainText();
    QString mainEqs = eqX + " " + eqY + " " + eqZ + " " + eqP;

    // --- NUOVO: CONTROLLO LETTERE SCONOSCIUTE (Error 0) ---
    QString allEqsToTest = mainEqs + " " + ui->lineExplicitU->toPlainText() + " " + ui->lineExplicitV->toPlainText() + " " + ui->lineExplicitW->toPlainText();

    // Cerca tutte le singole lettere isolate nell'equazione
    QRegularExpression singleLetterRegex("\\b[A-Za-z]\\b");
    QRegularExpressionMatchIterator it = singleLetterRegex.globalMatch(allEqsToTest);

    // Lista bianca di tutte le lettere ammesse (incluse x, y, z per i componenti vettoriali)
    QStringList allowedSingleLetters = {
        "u", "v", "w", "U", "V", "W", "t", "T",
        "a", "b", "c", "d", "e", "f", "s",
        "A", "B", "C", "D", "E", "F",
        "x", "y", "z", "P", "p"
    };

    QString invalidLetter = "";
    while (it.hasNext()) {
        QString letter = it.next().captured(0);
        if (!allowedSingleLetters.contains(letter)) {
            invalidLetter = letter;
            break;
        }
    }

    if (!invalidLetter.isEmpty()) {
        QMessageBox::critical(this, "Unrecognized Variable",
                              QString("You typed the letter '%1', which is not a recognized variable.\n\n"
                                      "Allowed variables: u, v, w (spatial), U, V, W (composite), "
                                      "t (time), or A, B, C, D, E, F, s (parameters).").arg(invalidLetter));
        return;
    }

    bool has_u = mainEqs.contains(QRegularExpression("\\bu\\b"));
    bool has_v = mainEqs.contains(QRegularExpression("\\bv\\b"));
    bool has_w = mainEqs.contains(QRegularExpression("\\bw\\b"));
    int lowerCount = (has_u ? 1 : 0) + (has_v ? 1 : 0) + (has_w ? 1 : 0);

    bool has_U = mainEqs.contains(QRegularExpression("\\bU\\b"));
    bool has_V = mainEqs.contains(QRegularExpression("\\bV\\b"));
    bool has_W = mainEqs.contains(QRegularExpression("\\bW\\b"));
    int upperCount = (has_U ? 1 : 0) + (has_V ? 1 : 0) + (has_W ? 1 : 0);

    // Error A: Mixed Variables
    if (lowerCount > 0 && upperCount > 0) {
        QMessageBox::critical(this, "Syntax Error",
                              "You cannot mix lowercase (u, v, w) and uppercase (U, V, W) spatial variables in the main equations!\n"
                              "Please choose only one coordinate system.");
        return;
    }

    // Error B: Wrong number of variables
    int totalVars = lowerCount + upperCount;
    if (totalVars != 2 && totalVars != 3) {
        QMessageBox::critical(this, "Spatial Dimension Error",
                              "You must use exactly 2 or 3 valid spatial variables.\n"
                              "For example: 'u, v' (2D) or 'U, V, W' (3D composite).\n"
                              "You have entered an invalid number of variables or unrecognized characters.");
        return;
    }

    // Error C: Missing Constraint (If 3 variables are used)
    bool hasConstraint = !ui->lineExplicitU->toPlainText().trimmed().isEmpty() ||
                         !ui->lineExplicitV->toPlainText().trimmed().isEmpty() ||
                         !ui->lineExplicitW->toPlainText().trimmed().isEmpty();

    if (totalVars == 3 && !hasConstraint && upperCount == 0) {
        QMessageBox::warning(this, "Missing Constraint Equation",
                             "You have used parameters 'u', 'v', and 'w' in your equations, "
                             "but no explicit constraint has been defined.\n\n"
                             "Please provide a constraint in the Constraints tab (e.g., w = h(u,v)) "
                             "to compute the surface correctly.");
        return;
    }

    // --- BLOCCO VALIDAZIONE COMPOSITION & ANTI-COLLASSO ---
    QString cU = ui->lineU->toPlainText().trimmed();
    QString cV = ui->lineV->toPlainText().trimmed();
    QString cW = ui->lineW->toPlainText().trimmed();

    // Controlliamo se l'utente sta cercando di usare le Composizioni
    bool isCompositionActive = (upperCount > 0) || !cU.isEmpty() || !cV.isEmpty() || !cW.isEmpty();

    if (isCompositionActive) {
        // 1. BLOCCO CAMPI VUOTI SOLO PER LE VARIABILI EFFETTIVAMENTE USATE
        if ((has_U && cU.isEmpty()) || (has_V && cV.isEmpty()) || (has_W && cW.isEmpty())) {
            QMessageBox::warning(this, "Missing Composition Fields",
                                 "You have activated the Composition mode, but one of the used fields (U, V, or W) is empty.\n\n"
                                 "Please explicitly fill the corresponding composition fields.");
            return; // Blocca l'avvio
        }

        // 2. CONTROLLO COLLASSO DIMENSIONALE: Ci sono sia 'u' che 'v'?
        QString composedTest = composeEquation(mainEqs, cU, cV, cW);
        bool finalU = composedTest.contains(QRegularExpression("\\bu\\b"));
        bool finalV = composedTest.contains(QRegularExpression("\\bv\\b"));

        if (!finalU || !finalV) {
            QMessageBox::critical(this, "Dimensional Collapse",
                                  "To draw a valid surface, the final composed equations must depend on both 'u' and 'v'.\n\n"
                                  "Currently, one of the two parameters is missing from the Composition panel, "
                                  "which would collapse the 2D surface into a 1D line.\n\n"
                                  "Please ensure both 'u' and 'v' are used.");
            return; // Blocca l'avvio
        }
    }
    // ==========================================================

    QString pEq = ui->lineP->toPlainText().trimmed();
    bool isSurface4D = !pEq.isEmpty() && pEq != "0" && pEq != "0.0";

    // Apply rotation ONLY if it's 4D and angles are zero
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

    // --- LETTURA E CALCOLO COSTANTI A CASCATA ---
    float valA = parseUIConstant(ui->lineA->text(), 0, 0, 0, 0, 0, 0, 0);
    float valB = parseUIConstant(ui->lineB->text(), valA, 0, 0, 0, 0, 0, 0);
    float valC = parseUIConstant(ui->lineC->text(), valA, valB, 0, 0, 0, 0, 0);
    float valD = parseUIConstant(ui->lineD->text(), valA, valB, valC, 0, 0, 0, 0);
    float valE = parseUIConstant(ui->lineE->text(), valA, valB, valC, valD, 0, 0, 0);
    float valF = parseUIConstant(ui->lineF->text(), valA, valB, valC, valD, valE, 0, 0);
    float valS = parseUIConstant(ui->lineS->text(), valA, valB, valC, valD, valE, valF, 0);

    // 1. Invia i valori calcolati al motore grafico
    ui->glWidget->setEquationConstants(valA, valB, valC, valD, valE, valF, valS);

    // 2. Allinea gli slider ai nuovi valori calcolati (Con Auto-Espansione)
    auto updateSlider = [](QSlider* s, float v, bool isS) {
        bool old = s->blockSignals(true);
        int intVal = static_cast<int>(v * 100.0f);

        int newMin = isS ? std::min(-1000, intVal) : 0;
        int newMax = std::max(1000, intVal);

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

    // 3. EQUATIONS, CONSTRAINTS AND COMPOSITIONS
    QString defU = ui->lineU->toPlainText();
    QString defV = ui->lineV->toPlainText();
    QString defW = ui->lineW->toPlainText();

    // Compose main equations BEFORE translation
    QString rawX = composeEquation(ui->lineX->toPlainText(), defU, defV, defW);
    QString rawY = composeEquation(ui->lineY->toPlainText(), defU, defV, defW);
    QString rawZ = composeEquation(ui->lineZ->toPlainText(), defU, defV, defW);
    QString rawP = composeEquation(ui->lineP->toPlainText(), defU, defV, defW);

    QString xEq = GlslTranslator::translateEquation(rawX);
    QString yEq = GlslTranslator::translateEquation(rawY);
    QString zEq = GlslTranslator::translateEquation(rawZ);
    QString wEq = GlslTranslator::translateEquation(rawP);

    // Compose explicit constraints
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

    bool success = ui->glWidget->setParametricEquations(xEq, yEq, zEq, wEq);

    if (!success) {
        QMessageBox::critical(this, "Equation Error",
                              "Syntax error in the parametric equations or constraints.\n\n"
                              "Please check your math, brackets, and try again.");
        return; // Ferma tutto: non avvia l'animazione e non rovina la vecchia superficie!
    }

    // 3. READ VALUES
    float uMin = parseMath(ui->uMinEdit->text());
    float uMax = parseMath(ui->uMaxEdit->text());
    ui->glWidget->setRangeU(uMin, uMax);

    float vMin = parseMath(ui->vMinEdit->text());
    float vMax = parseMath(ui->vMaxEdit->text());
    ui->glWidget->setRangeV(vMin, vMax);

    float wMin = parseMath(ui->wMinEdit->text());
    float wMax = parseMath(ui->wMaxEdit->text());
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
                         ui->lineW->toPlainText();

    if (rawEqsForT.contains(QRegularExpression("\\bt\\b"))) {
        ui->glWidget->setSurfaceAnimating(true);
        if (m_btnStart) m_btnStart->setText("STOP");
    } else {
        ui->glWidget->setSurfaceAnimating(false);
        if (m_btnStart) m_btnStart->setText("START");
    }

    if (ui->chkBoxTexture->isChecked() && wasCustomTexture && !currentScript.isEmpty()) {
        ui->glWidget->loadCustomShader(currentScript);
    }

    // 4. REPAINT (Safety Step 3)
    QString textToCheck = ui->lineX->toPlainText() + " " +
                          ui->lineY->toPlainText() + " " +
                          ui->lineZ->toPlainText() + " " +
                          ui->lineP->toPlainText();

    SurfaceEngine::ConstraintMode mode = ui->glWidget->getEngine()->getConstraintMode();
    if (mode == SurfaceEngine::ConstraintU) {
        textToCheck += " " + ui->lineExplicitU->toPlainText();
    } else if (mode == SurfaceEngine::ConstraintV) {
        textToCheck += " " + ui->lineExplicitV->toPlainText();
    } else {
        textToCheck += " " + ui->lineExplicitW->toPlainText();
    }

    ui->glWidget->makeCurrent();
    ui->glWidget->updateSurfaceData();
    ui->glWidget->doneCurrent();

    ui->glWidget->update();
}

void MainWindow::onStopClicked() {
    // 1. DETERMINA LO STATO REALE
    bool isRunning = ui->glWidget->isAnimating();

    if (isRunning) {
        ui->glWidget->pauseMotion();

        if (ui->btnStart_2)
            ui->btnStart_2->setText("GO");

    } else {
        // 2. CONTROLLO VELOCITÀ
        bool hasSpeed = (std::abs(ui->glWidget->getNutationSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getPrecessionSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getSpinSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getPhiSpeed()) > 0.001f ||
                         std::abs(ui->glWidget->getPsiSpeed()) > 0.001f);

        if (!hasSpeed) {
            return;
        }

        ui->glWidget->resumeMotion();

        if (ui->btnStart_2)
            ui->btnStart_2->setText("STOP");
    }

    update4DButtonState();
}

void MainWindow::onResetViewClicked(){
    // 1. Ferma i Timer (Path 4D e 3D)
    if (pathTimer->isActive()) pathTimer->stop();
    if (pathTimer3D->isActive()) pathTimer3D->stop();

    pathTimeT = 0.0f;
    pathTimeT3D = 0.0f;

    ui->btnDeparture->setText("DEPARTURE");
    ui->btnDeparture3D->setText("DEPARTURE");

    checkPathFields();
    checkPath3DFields();

    // 2. RESET TRASFORMAZIONI
    ui->glWidget->resetTransformations();

    QString pEq = ui->lineP->toPlainText().trimmed();
    bool isSurface4D = !pEq.isEmpty() && pEq != "0" && pEq != "0.0";

    if (isSurface4D) {
        float safety = 0.0001f;
        ui->glWidget->setRotation4D(safety, safety, safety);
    }

    // 3. RESET INTERFACCIA (UI)
    ui->lblNutVal->setText("0.00");
    ui->lblPrecVal->setText("0.00");
    ui->lblSpinVal->setText("0.00");
    ui->lblOmegaVal->setText("0.00");
    ui->lblPhiVal->setText("0.00");
    ui->lblPsiVal->setText("0.00");

    if (ui->btnStart_2) {
        ui->btnStart_2->setText("GO");
    }

    if (m_btnStart) m_btnStart->setText("START");
    ui->glWidget->stopAnimationTimer();
    ui->glWidget->resetTime();

    // 4. RIGENERAZIONE FINALE
    ui->glWidget->updateSurfaceData();
    ui->glWidget->update();
}

void MainWindow::onNavTimerTick()
{
    if (activeNavActions.isEmpty()) return;

    bool slow = (m_pathSpeed3D < 0.01f);

    for (int action : activeNavActions) {
        ui->glWidget->virtualMove(static_cast<GLWidget::MoveDir>(action), slow);
    }
}

void MainWindow::onDepartureClicked()
{
    // CASO 1: VOGLIAMO FERMARE
    if (pathTimer->isActive()) {
        pathTimer->stop();
        ui->btnDeparture->setText("DEPARTURE");
        checkPathFields();
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

    // Compila
    bool ok = ui->glWidget->getEngine()->compilePathEquations(eqX, eqY, eqZ, eqP, eqAlpha, eqBeta, eqGamma);

    if (!ok) {
        QMessageBox::warning(this, "Error", "Path 4D compilation error .\nCheck the syntax.");
        return;
    }

    pathTimeT = 0.0f;

    pathTimer->start();
    ui->btnDeparture->setText("STOP");
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

    // Scaling
    finalPos4D = finalPos4D * surfaceScale;
    finalTarget4D = finalTarget4D * surfaceScale;

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
        return;
    }

    // CASO 2: START
    if (pathTimer->isActive()) {
        pathTimer->stop();
        ui->btnDeparture->setText("DEPARTURE");
        // checkPathFields(); // Opzionale, aggiorna solo la UI del 4D
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

    bool ok = ui->glWidget->getEngine()->compilePath3DEquations(eqX, eqY, eqZ, eqR);
    if (!ok) {
        QMessageBox::warning(this, "Error", "4D path compilation error.\nCheck the syntax.");
        return;
    }

    pathTimeT3D = 0.0f;
    pathTimer3D->start();
    ui->btnDeparture3D->setText("STOP");
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
    QVector3D currentPos = rawData.toVector3D() * surfaceScale;
    float currentRoll = rawData.w();

    QVector3D target;

    if (m_pathMode == ModeTangential) {
        float delta = 0.1f;
        QVector4D futureData = ui->glWidget->getEngine()->evaluatePath3DPosition(pathTimeT3D + delta);
        target = futureData.toVector3D() * surfaceScale;
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
    // 1. Salva il testo che l'utente ha appena scritto nella variabile corretta
    QString currentText = ui->txtScriptEditor->toPlainText();
    if (m_currentScriptMode == ScriptModeSurface) m_surfaceScriptText = currentText;
    else if (m_currentScriptMode == ScriptModeTexture) {
        if (ui->radioBackground->isChecked()) m_bgTextureScriptText = currentText;
        else m_surfaceTextureScriptText = currentText;
    }
    else if (m_currentScriptMode == ScriptModeSound) m_soundScriptText = currentText;

    // 2. Passa alla modalità successiva (0 -> 1 -> 2 -> 0)
    m_currentScriptMode = static_cast<ScriptMode>((m_currentScriptMode + 1) % 3);

    // 3. Ripristina il testo e aggiorna i bottoni senza innescare eventi indesiderati
    ui->txtScriptEditor->blockSignals(true);

    if (m_currentScriptMode == ScriptModeSurface) {
        ui->txtScriptEditor->setPlainText(m_surfaceScriptText);
        ui->btnScriptMode->setText("Surface");
        ui->btnRunCurrentScript->setText("Run Surface");

    } else if (m_currentScriptMode == ScriptModeTexture) {
        ui->btnScriptMode->setText("Texture");

        // Carica la memoria giusta a seconda del radio button
        if (ui->radioBackground->isChecked()) {
            ui->txtScriptEditor->setPlainText(m_bgTextureScriptText);
            ui->btnRunCurrentScript->setText("Run Background Texture");
        } else {
            ui->txtScriptEditor->setPlainText(m_surfaceTextureScriptText);
            ui->btnRunCurrentScript->setText("Run Surface Texture");
        }

    } else if (m_currentScriptMode == ScriptModeSound) {
        ui->txtScriptEditor->setPlainText(m_soundScriptText);
        ui->btnScriptMode->setText("Sound");

        bool isPlaying = m_audioController->isPlaying();

        ui->btnRunCurrentScript->setText(isPlaying ? "Stop Sound" : "Run Sound");
    }

    ui->txtScriptEditor->blockSignals(false);
}

void MainWindow::onRunCurrentScript()
{
    // 1. Estrae il testo correntemente scritto nell'editor
    QString currentText = ui->txtScriptEditor->toPlainText();

    // 2. Salva e avvia in base alla modalità attuale
    if (m_currentScriptMode == ScriptModeSurface) {
        m_surfaceScriptText = currentText;
        onRunScriptClicked();

    } else if (m_currentScriptMode == ScriptModeTexture) {
        if (ui->radioBackground->isChecked()) m_bgTextureScriptText = currentText;
        else m_surfaceTextureScriptText = currentText;
        onApplyTextureScriptClicked();

    } else if (m_currentScriptMode == ScriptModeSound) {
        m_soundScriptText = currentText;

        QString& targetTexture = ui->radioBackground->isChecked() ? m_bgTextureScriptText : m_surfaceTextureScriptText;

        // Pulizia vecchi tag
        targetTexture.remove(QRegularExpression(R"(^\s*//(SYNTH|MUSIC):.*$\n?)", QRegularExpression::MultilineOption));
        targetTexture.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));

        if (!m_soundScriptText.isEmpty()) {
            if (m_soundScriptText.startsWith("//MUSIC:")) {
                targetTexture = m_soundScriptText + "\n" + targetTexture.trimmed();
            } else {
                // Avvolgiamo il codice GLSL che l'utente ha scritto a mano nell'editor!
                targetTexture = "//SOUND_BEGIN\n" + m_soundScriptText + "\n//SOUND_END\n\n" + targetTexture.trimmed();
            }
        }

        if (ui->radioBackground->isChecked()) m_bgTextureCode = targetTexture;
        else m_surfaceTextureCode = targetTexture;

        onRunSoundClicked();
    }
}

void MainWindow::onRunScriptClicked()
{
    QString fullText = ui->txtScriptEditor->toPlainText();
    if (fullText.trimmed().isEmpty()) return;

    this->setProperty("rawSurfaceScript", fullText);

    if (fullText.contains("void mainImage", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "Script Type Error",
                             "You pasted a TEXTURE (Shader) script\n"
                             "but clicked 'Run Script' (Surface).\n\n"
                             "Use the 'Apply Texture Script' button in the Texture panel.");
        return;
    }

    this->setProperty("rawSurfaceScript", fullText);

    // 1. Analizza i parametri di configurazione (u_min, steps, etc.)
    parseAndApplyScriptParams(fullText);

    // 2. Estrai SOLO il codice GLSL da inviare alla GPU.
    QString glslBody;
    QTextStream stream(&fullText);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.contains(":=")) continue;
        glslBody.append(line + "\n");
    }

    glslBody = GlslTranslator::translateEquation(glslBody);

    // 3. Passa il codice pulito all'engine
    ui->glWidget->getEngine()->setScriptCodeGLSL(glslBody);
    ui->glWidget->getEngine()->setScriptMode(true); // Attiva modalità script

    // 4. Aggiorna Risoluzione e Costanti (Copiati dalla UI aggiornata al punto 1)
    ui->glWidget->setResolution(ui->stepSlider->value());
    float valA = ui->aSlider->value() / 100.0f;
    float valB = ui->bSlider->value() / 100.0f;
    float valC = ui->cSlider->value() / 100.0f;
    float valD = ui->dSlider->value() / 100.0f;
    float valE = ui->eSlider->value() / 100.0f;
    float valF = ui->fSlider->value() / 100.0f;
    float valS = ui->sSlider->value() / 100.0f;
    ui->glWidget->setEquationConstants(valA, valB, valC, valD, valE, valF, valS);

    // 5. Ricompila lo shader
    ui->glWidget->makeCurrent();
    bool success = ui->glWidget->rebuildShader();
    ui->glWidget->doneCurrent();

    if (!success) {
        QMessageBox::critical(this, "Script Error",
                              "Syntax error in your GLSL script.\n\n"
                              "Please check the code and try again.");
        return; // Blocca tutto
    }

    // A. Svuota le equazioni parametriche senza innescare aggiornamenti visivi
    bool oldX = ui->lineX->blockSignals(true); ui->lineX->clear(); ui->lineX->blockSignals(oldX);
    bool oldY = ui->lineY->blockSignals(true); ui->lineY->clear(); ui->lineY->blockSignals(oldY);
    bool oldZ = ui->lineZ->blockSignals(true); ui->lineZ->clear(); ui->lineZ->blockSignals(oldZ);
    bool oldP = ui->lineP->blockSignals(true); ui->lineP->clear(); ui->lineP->blockSignals(oldP);

    ui->lineExplicitU->clear();
    ui->lineExplicitV->clear();
    ui->lineExplicitW->clear();

    ui->lineU->clear();
    ui->lineV->clear();
    ui->lineW->clear();

    // B. Cerca la 't' e avvia automaticamente il respiro
    if (fullText.contains(QRegularExpression("\\bt\\b"))) {
        ui->glWidget->setSurfaceAnimating(true);
        if (m_btnStart) m_btnStart->setText("STOP");
    } else {
        ui->glWidget->setSurfaceAnimating(false);
        if (m_btnStart) m_btnStart->setText("START");
    }

    // 6. Render
    ui->glWidget->updateSurfaceData();
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

    QString imgPath;
    QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatch imgMatch = imgRe.match(code);
    if (imgMatch.hasMatch()) {
        imgPath = imgMatch.captured(1).trimmed();
        if (!QFile::exists(imgPath)) {
            // --- SMART PATH RESOLVER ---
            QString fileName = QFileInfo(imgPath).fileName();
            QSettings settings;
            QString texDir = settings.value("pathTextures", settings.value("libraryRootPath").toString() + "/Textures").toString();
            QDirIterator it(texDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);

            if (it.hasNext()) {
                imgPath = it.next(); // Ritrovata automaticamente!
            } else {
                QMessageBox::warning(this, "Image Not Found",
                                     "Warning: the original image linked to this preset was not found:\n\n" +
                                         imgPath + "\n\nIt might have been moved or renamed. It will be ignored to prevent graphical errors.");
                imgPath = "";
            }
        }
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
            ui->glWidget->loadBackgroundScript(code);
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
        m_surfaceTextureCode = code;

        if (!ui->chkBoxTexture->isChecked()) {
            const bool wasBlocked = ui->chkBoxTexture->blockSignals(true);
            ui->chkBoxTexture->setChecked(true);
            ui->chkBoxTexture->blockSignals(wasBlocked);
            updateTextureUIState(true);
            ui->glWidget->setTextureEnabled(true);
        }

        // 1. GESTIONE MEMORIA IMMAGINE
        if (!imgPath.isEmpty()) {
            m_isImageMode = true;
            m_currentTexturePath = imgPath;
            if (ui->glWidget) {
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
                ui->glWidget->loadCustomShader(code);
                ui->glWidget->setFlatViewTarget(0);
                ui->glWidget->setFlatPan(0.0f, 0.0f);
                ui->glWidget->setFlatZoom(1.0f);
                ui->glWidget->setFlatRotation(0.0f);
            }
        } else {
            m_isCustomMode = false;
            if (ui->glWidget) {
                ui->glWidget->setTextureColors(m_texColor1, m_texColor2);
                ui->glWidget->makeCurrent();
                ui->glWidget->rebuildShader(); // Torna allo shader standard
                ui->glWidget->doneCurrent();
            }
        }

        if (ui->glWidget) {
            updateRenderState();
            ui->glWidget->update();
        }
    }

    updateFlatPreviewButton();
}

void MainWindow::onRunSoundClicked()
{
    // Se sta suonando, ferma
    if (ui->btnRunCurrentScript->text() == "Stop Sound") {
        m_audioController->stopAll();
        return;
    }

    QString codeToAnalyze = m_soundScriptText + "\n" + m_surfaceScriptText + "\n" + m_surfaceTextureCode + "\n" + m_bgTextureCode;

    if (codeToAnalyze.trimmed().isEmpty()) codeToAnalyze = ui->txtScriptEditor->toPlainText();

    // Passa la palla all'audio controller
    m_audioController->playFromScript(codeToAnalyze);
}


// ==========================================================
// LIBRARY & WORKSPACE MANAGEMENT
// ==========================================================

void MainWindow::onExampleItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    // 1. Surface
    QVariant vSurf = item->data(0, Qt::UserRole);
    if (vSurf.isValid()) {
        int index = vSurf.toInt();
        const LibraryItem &data = m_libraryManager.getSurface(index);
        if (!data.name.isEmpty()) applySurfaceExample(data);
        return;
    }

    // 2. Texture
    QVariant vTex = item->data(0, Qt::UserRole + 1);
    if (vTex.isValid()) {
        int index = vTex.toInt();
        const LibraryItem &data = m_libraryManager.getTexture(index);

        static QString lastBgTexturePath = ""; // Memoria per lo sfondo

        // Controllo automatico della modalità 2D tramite il pulsante FlatPreview
        bool is2DMode = ui->btnFlatPreview->isChecked();

        if (ui->radioBackground->isChecked()) {
            if (ui->chkBoxTexture->isChecked() && lastBgTexturePath == data.filePath) {
                if (!is2DMode) {
                    ui->chkBoxTexture->setChecked(false); // Spegne la texture solo se siamo in 3D
                    lastBgTexturePath = "";               // Resetta la memoria
                }
                return;
            }
            lastBgTexturePath = data.filePath;
        } else {
            if (ui->chkBoxTexture->isChecked() && m_currentTexturePath == data.filePath) {
                if (!is2DMode) {
                    ui->chkBoxTexture->setChecked(false); // Spegne la texture solo se siamo in 3D
                    m_currentTexturePath = "";            // Resetta la memoria
                }
                return;
            }
        }

        handleTextureSelection(index);
        return;
    }

    // 3. Records
    QVariant vMot = item->data(0, Qt::UserRole + 2);
    if (vMot.isValid()) {
        static QString lastMotionPath = ""; // Memoria per il toggle del click

        int index = vMot.toInt();
        const LibraryItem &data = m_libraryManager.getMotion(index);

        // Controllo: se abbiamo cliccato esattamente lo stesso record
        if (lastMotionPath == data.filePath) {
            // Verifichiamo se c'è qualcosa in riproduzione in questo momento
            bool isPlaying = (pathTimer && pathTimer->isActive()) ||
                             (pathTimer3D && pathTimer3D->isActive()) ||
                             (ui->btnStart_2 && ui->btnStart_2->text() == "STOP") ||
                             (m_btnStart && m_btnStart->text() == "STOP") ||
                             (ui->btnRunCurrentScript && ui->btnRunCurrentScript->text() == "Stop Sound");

            if (isPlaying) {
                // Congela la scena: mettiamo in pausa rotazioni, tempo, e path
                if (ui->glWidget) {
                    ui->glWidget->pauseMotion();
                    ui->glWidget->setSurfaceAnimating(false);
                }
                if (m_btnStart) m_btnStart->setText("START");
                if (ui->btnStart_2) ui->btnStart_2->setText("GO");

                if (pathTimer && pathTimer->isActive()) {
                    pathTimer->stop();
                    ui->btnDeparture->setText("DEPARTURE");
                    checkPathFields();
                }
                if (pathTimer3D && pathTimer3D->isActive()) {
                    pathTimer3D->stop();
                    ui->btnDeparture3D->setText("DEPARTURE");
                    checkPath3DFields();
                }

                // Ferma l'audio
                if (m_audioController) m_audioController->stopAll();
                if (ui->btnRunCurrentScript && ui->btnRunCurrentScript->text() == "Stop Sound") {
                    ui->btnRunCurrentScript->setText("Run Sound");
                }

                return; // Fermiamo qui l'esecuzione per NON ricaricare la scena
            }
        }

        // Se non era in esecuzione o è un record diverso, caricalo e avvialo
        if (!data.name.isEmpty()) {
            lastMotionPath = data.filePath;
            applyMotionExample(data);
        }
        return;
    }
}

void MainWindow::applySurfaceExample(const LibraryItem &d)
{
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

    m_bgTextureCode = "// Default Background Image";
    m_bgTextureScriptText = "// Default Background Image";

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
        ui->glWidget->setBorderColor(defR, defG, defB);
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
        ui->glWidget->makeCurrent();
        ui->glWidget->rebuildShader();
        ui->glWidget->doneCurrent();
    }

    updateRenderState();

    // 5. CARICAMENTO DATI (Equazioni, Colori, ecc.)
    applyCommonData(d);

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

    // FIX: Essendo una superficie statica, azzeriamo esplicitamente le etichette delle VELOCITÀ
    ui->lblOmegaVal->setText("0.00");
    ui->lblPhiVal->setText("0.00");
    ui->lblPsiVal->setText("0.00");

    // 6. Eseguiamo onStartClicked per inizializzare equazioni
    bool hasValidEquations = (d.x.trimmed().length() > 0 && d.x != "0" && d.x != "0.0");
    bool isScript = (d.isScript && !d.scriptCode.isEmpty() && !hasValidEquations);

    if (!isScript) {
        onStartClicked();
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
    QString extractedSound;

    // Estrae file MP3/WAV
    QRegularExpression musicRe(R"(^\s*//MUSIC:.*$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator musicIt = musicRe.globalMatch(fullLoadedText);
    while (musicIt.hasNext()) {
        extractedSound += musicIt.next().captured(0).trimmed() + "\n";
    }

    // Estrae il nuovo formato GLSL (Pestis ecc.)
    QRegularExpression blockRe(R"(//SOUND_BEGIN(.*?)//SOUND_END)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator blockIt = blockRe.globalMatch(fullLoadedText);
    while (blockIt.hasNext()) {
        extractedSound += "//SOUND_BEGIN\n" + blockIt.next().captured(1).trimmed() + "\n//SOUND_END\n";
    }
    m_soundScriptText = extractedSound.trimmed();

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

    QTimer::singleShot(20, this, [this]() {
        if (ui->glWidget) {
            updateULimits();
            updateVLimits();
            updateWLimits();
            ui->glWidget->setResolution(ui->stepSlider->value());

            ui->glWidget->makeCurrent();
            ui->glWidget->updateSurfaceData();
            ui->glWidget->doneCurrent();
            ui->glWidget->update();
        }
    });
}

void MainWindow::applyMotionExample(const LibraryItem &data)
{
    // 1. STOP TOTALE (Reset stato iniziale)
    m_audioController->stopAll();

    ui->glWidget->pauseMotion(); // Ferma rotazioni
    ui->glWidget->resetTransformations();

    if (pathTimer->isActive()) onDepartureClicked();
    if (pathTimer3D->isActive()) onDeparture3DClicked();

    if (m_btnStart) m_btnStart->setText("START");

    if (ui->btnStart_2) ui->btnStart_2->setText("GO");

    m_surfaceTextureCode.clear();
    if (ui->glWidget) ui->glWidget->loadCustomShader("");

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
            if (tex.contains("code")) texCode = tex["code"].toString();
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

        if (root.contains("pathMode")) {
            m_pathMode = static_cast<decltype(m_pathMode)>(root["pathMode"].toInt());
        } else {
            m_pathMode = ModeTangential;
        }

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
    QString fullLoadedText = texCode + "\n" + bgCode;

    QString extractedSound;
    QRegularExpression musicRe(R"(^\s*//MUSIC:.*$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator musicIt = musicRe.globalMatch(fullLoadedText);
    while (musicIt.hasNext()) { extractedSound += musicIt.next().captured(0).trimmed() + "\n"; }

    QRegularExpression blockRe(R"(//SOUND_BEGIN(.*?)//SOUND_END)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator blockIt = blockRe.globalMatch(fullLoadedText);
    while (blockIt.hasNext()) { extractedSound += "//SOUND_BEGIN\n" + blockIt.next().captured(1).trimmed() + "\n//SOUND_END\n"; }

    m_soundScriptText = extractedSound.trimmed();

    // Rimuoviamo la musica dai codici grafici per proteggere OpenGL!
    texCode.remove(musicRe); texCode.remove(blockRe); texCode = texCode.trimmed();
    bgCode.remove(musicRe); bgCode.remove(blockRe); bgCode = bgCode.trimmed();
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

    QString imgPath;
    QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatch imgMatch = imgRe.match(texCode);
    if (imgMatch.hasMatch()) {
        imgPath = imgMatch.captured(1).trimmed();
        if (!QFile::exists(imgPath)) {
            // --- SMART PATH RESOLVER ---
            QString fileName = QFileInfo(imgPath).fileName();
            QSettings settings;
            QString texDir = settings.value("pathTextures", settings.value("libraryRootPath").toString() + "/Textures").toString();
            QDirIterator it(texDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);

            if (it.hasNext()) {
                imgPath = it.next(); // Ritrovata automaticamente!
            } else {
                QMessageBox::warning(this, "Record Image Not Found",
                                     "The image used in this record was not found at:\n\n" +
                                         imgPath + "\n\nThe animation will be loaded without this texture.");
                imgPath = "";
                texCode = "";
            }
        }
    }

    m_bgTexColor1 = loadedBgCol1;
    m_bgTexColor2 = loadedBgCol2;
    ui->glWidget->setTextureColors(m_texColor1, m_texColor2);

    // Svuota forzatamente gli shader procedurali "incastrati" prima di caricare il nuovo!
    if (ui->glWidget) {
        ui->glWidget->clearTexture();
        ui->glWidget->loadCustomShader("");
        ui->glWidget->makeCurrent();
        ui->glWidget->rebuildShader();
        ui->glWidget->doneCurrent();
    }

    // --- APPLICAZIONE TEXTURE SUPERFICIE ---
    if (texEnabled) {
        int currentTarget = ui->radioBackground->isChecked() ? 1 : 0;
        ui->glWidget->setFlatViewTarget(0);
        ui->glWidget->setFlatZoom(surfZoom);
        ui->glWidget->setFlatPan(surfPanX, surfPanY);
        ui->glWidget->setFlatRotation(surfRot);

        if (!texCode.isEmpty()) {
            if (!imgPath.isEmpty()) {
                ui->glWidget->loadTextureFromFile(imgPath);
                ui->glWidget->makeCurrent(); ui->glWidget->rebuildShader(); ui->glWidget->doneCurrent();
                m_isImageMode = true;
                m_isCustomMode = false;
                m_currentTexturePath = imgPath;
            }
            else if (texCode.contains("return") || texCode.contains("vec3") || texCode.contains("vec4") || texCode.contains("mainImage")) {
                m_isCustomMode = true;
                m_isImageMode = false;
                ui->glWidget->loadCustomShader(texCode);
                // Sicurezza aggiuntiva: forziamo subito la ricompilazione!
                ui->glWidget->makeCurrent();
                ui->glWidget->doneCurrent();
            }
            else {
                m_isCustomMode = false;
                m_isImageMode = false;
                generateTexture();
                ui->glWidget->makeCurrent(); ui->glWidget->rebuildShader(); ui->glWidget->doneCurrent();
            }
        }
        else {
            m_isCustomMode = false;
            m_isImageMode = false;
            generateTexture();
            ui->glWidget->makeCurrent(); ui->glWidget->rebuildShader(); ui->glWidget->doneCurrent();
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
        QRegularExpressionMatch bgImgMatch = imgRe.match(bgCode);
        if (bgImgMatch.hasMatch()) {
            QString bgImgPath = bgImgMatch.captured(1).trimmed();
            if (!QFile::exists(bgImgPath)) {
                // --- SMART PATH RESOLVER ---
                QString fileName = QFileInfo(bgImgPath).fileName();
                QSettings settings;
                QString texDir = settings.value("pathTextures", settings.value("libraryRootPath").toString() + "/Textures").toString();
                QDirIterator it(texDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);

                if (it.hasNext()) bgImgPath = it.next(); // Ritrovata automaticamente!
            }
            ui->glWidget->setBackgroundTexture(bgImgPath);
        } else {
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
    ui->glWidget->setOmegaSpeed(data.speedOmega);
    ui->glWidget->setPhiSpeed(data.speedPhi);
    ui->glWidget->setPsiSpeed(data.speedPsi);

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
    ui->lblOmegaVal->setText(QString::number(data.speedOmega, 'f', 2));
    ui->lblPhiVal->setText(QString::number(data.speedPhi, 'f', 2));
    ui->lblPsiVal->setText(QString::number(data.speedPsi, 'f', 2));

    if (data.restoreAngles) {
        ui->glWidget->setRotation4D(data.startOmega, data.startPhi, data.startPsi);
    }

    ui->glWidget->update();

    auto isReal = [](const QString &s) {
        QString t = s.trimmed();
        return !t.isEmpty() && t != "0" && t != "0.0";
    };

    bool hasRotation = (std::abs(data.speedPrec) > 0.001f || std::abs(data.speedOmega) > 0.001f ||
                        std::abs(data.speedNut) > 0.001f || std::abs(data.speedSpin) > 0.001f);

    bool hasPath4D = isReal(data.path4D_x) || isReal(data.path4D_y) || isReal(data.path4D_z) ||
                     isReal(data.path4D_alpha) || isReal(data.path4D_beta);

    bool hasPath3D = isReal(data.path3D_x) || isReal(data.path3D_y);

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
    bool hasValidEquations = (data.x.trimmed().length() > 0 && data.x != "0" && data.x != "0.0");
    bool isScript = (data.isScript && !data.scriptCode.isEmpty() && !hasValidEquations);

    if (!isScript) {
        onStartClicked();
    } else {
        if (ui->glWidget) {
            ui->glWidget->makeCurrent();

            // Carica lo shader personalizzato OPPURE rigenera quello standard, mai entrambi.
            if (texEnabled && m_isCustomMode && !m_surfaceTextureCode.isEmpty()) {
                ui->glWidget->setRenderMode(11);
                ui->glWidget->loadCustomShader(m_surfaceTextureCode);
            } else {
                ui->glWidget->rebuildShader();
            }

            ui->glWidget->doneCurrent();
            ui->glWidget->updateSurfaceData();
            ui->glWidget->update();
        }
    }

    // 8. AVVIO AUDIO
    if (!m_soundScriptText.isEmpty()) {
        m_audioController->playFromScript(m_soundScriptText);
    } else {
        m_audioController->stopAll();
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
        QString normLoadedSound = fullAudioSearchCode;
        normLoadedSound.remove(QRegularExpression(R"(//SOUND_BEGIN|//SOUND_END)", QRegularExpression::CaseInsensitiveOption));
        normLoadedSound.replace(QRegularExpression("\\s+"), "");

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
                    // MATCH ROBUSTO PER MEDIA: Estrae e confronta solo il nome del file
                    QString fileName = QFileInfo(sndItem.filePath).fileName();
                    if (!fileName.isEmpty() && fullAudioSearchCode.contains(fileName)) {
                        isMatch = true;
                    }
                } else if (!sndItem.scriptCode.isEmpty()) {
                    QString normLibSound = sndItem.scriptCode;
                    normLibSound.remove(QRegularExpression(R"(//SOUND_BEGIN|//SOUND_END)", QRegularExpression::CaseInsensitiveOption));
                    normLibSound.replace(QRegularExpression("\\s+"), "");

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
    QString activeCode = ui->radioBackground->isChecked() ? m_bgTextureCode : m_surfaceTextureCode;

    activeCode.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)", QRegularExpression::MultilineOption));
    activeCode.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));
    activeCode.replace(QRegularExpression("\\s+"), "");

    if (!activeCode.isEmpty()) {
        while (*itTex) {
            QVariant vTex = (*itTex)->data(0, Qt::UserRole + 1);
            if (vTex.isValid()) {
                int idx = vTex.toInt();
                const LibraryItem &texItem = m_libraryManager.getTexture(idx);
                bool isMatch = false;

                if (texItem.isImage) {
                    // MATCH ROBUSTO PER IMMAGINI: Estrae e confronta solo il nome del file
                    QString fileName = QFileInfo(texItem.filePath).fileName();
                    if (!fileName.isEmpty() && activeCode.contains(fileName)) {
                        isMatch = true;
                    }
                } else {
                    QString cleanCode = texItem.scriptCode;
                    cleanCode.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)", QRegularExpression::MultilineOption));
                    cleanCode.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));
                    cleanCode.replace(QRegularExpression("\\s+"), "");

                    if (!cleanCode.isEmpty() && activeCode == cleanCode) isMatch = true;
                }

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

    QTimer::singleShot(20, this, [this]() {
        if (ui->glWidget) {
            updateULimits();
            updateVLimits();
            updateWLimits();
            ui->glWidget->setResolution(ui->stepSlider->value());

            ui->glWidget->makeCurrent();
            ui->glWidget->updateSurfaceData();
            ui->glWidget->doneCurrent();
            ui->glWidget->update();
        }
    });
}

void MainWindow::deleteSelectedExample() {
    m_fileOps->deleteSelected();
}

void MainWindow::onUndoDelete() {
    m_fileOps->undoDelete();
}

void MainWindow::onAddRepositoryClicked(LibraryType /*type*/)
{
    QSettings settings;
    QString currentRoot = settings.value("libraryRootPath", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();

    QString selectedPath = QFileDialog::getExistingDirectory(this, "Select Location for Presets Folder", currentRoot,
                                                             QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedPath.isEmpty()) return;

    // PULIZIA DEL PERCORSO e AGGIUNTA FORZATA
    selectedPath = QDir::cleanPath(selectedPath);
    QString finalPath = selectedPath + "/Presets"; // <-- CREA SEMPRE LA SOTTOCARTELLA

    QDir().mkpath(finalPath);
    settings.setValue("libraryRootPath", finalPath);

    // Essendo cambiato il percorso root, eliminiamo le vecchie chiavi specifiche
    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathRecords");
    settings.remove("pathSounds");

    setupDefaultFolders();
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
    QString rootPath = settings.value("libraryRootPath").toString();

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
        QString rootPath = settings.value("libraryRootPath").toString();

        if (currentTab == ui->Texture) basePath = settings.value("pathTextures", rootPath + "/Textures").toString();
        else if (currentTab == ui->Motions) basePath = settings.value("pathMotions", rootPath + "/Motions").toString();
        else if (currentTab->objectName().contains("Sound", Qt::CaseInsensitive)) basePath = settings.value("pathSounds", rootPath + "/Sounds").toString();
        else basePath = settings.value("pathSurfaces", rootPath + "/Surfaces").toString();
    }

    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        QMessageBox::warning(this, "Warning", "The destination library folder does not exist. Try resetting the Library.");
        return;
    }

    if (baseDir.mkdir(folderName)) {
        refreshRepositories();
    } else {
        QMessageBox::critical(this, "Error", "Could not create folder. A folder with this name might already exist.");
    }
}

void MainWindow::onSyncPresetsClicked()
{
    QSettings settings;

    // 1. AMNESIA FORZATA: Cancelliamo TUTTE le vecchie configurazioni sballate
    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathMotions");
    settings.remove("pathRecords");
    settings.remove("pathSounds");
    settings.remove("repoPathsSurfaces");
    settings.remove("repoPathsTextures");
    settings.remove("repoPathsMotions");
    settings.remove("repoPathsSounds");

    QString rootPath = settings.value("libraryRootPath").toString();

    // Se la cartella radice non c'è, avviamo il setup iniziale
    if (rootPath.isEmpty() || !QDir(rootPath).exists()) {
        setupDefaultFolders();
        return;
    }

    auto reply = QMessageBox::question(this, "Restore Presets",
                                       "Do you want to restore the factory presets?\n"
                                       "This will organize your Library into the correct folders.",
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::No) return;

    // 2. PERCORSI ASSOLUTI E INVALICABILI
    QString pathSurf = rootPath + "/Surfaces";
    QString pathTex  = rootPath + "/Textures";
    QString pathRec  = rootPath + "/Records";
    QString pathSnd  = rootPath + "/Sounds";

    // Li salviamo puliti in memoria
    settings.setValue("pathSurfaces", pathSurf);
    settings.setValue("pathTextures", pathTex);
    settings.setValue("pathRecords", pathRec);
    settings.setValue("pathSounds", pathSnd);

    // 3. CREAZIONE FISICA CARTELLE (se mancano)
    QDir().mkpath(pathSurf);
    QDir().mkpath(pathTex);
    QDir().mkpath(pathRec);
    QDir().mkpath(pathSnd);

    // 4. ESTRAZIONE RISORSE (Assicurati che queste 4 righe siano ESATTAMENTE così)
    syncResourcesToFolder(":/library/presets/surfaces", pathSurf, true);
    syncResourcesToFolder(":/library/presets/textures", pathTex, true);
    syncResourcesToFolder(":/library/presets/records", pathRec, true);
    syncResourcesToFolder(":/library/presets/sounds", pathSnd, true);

    refreshRepositories();
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
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    "Save Texture Preset",
                                                    startDir,
                                                    "Texture Preset (*.json)");

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
    // CASO 1: Nessun file aperto, oppure stiamo modificando un'immagine diretta (.png/.jpg)
    if (m_currentTexturePath.isEmpty() || !m_currentTexturePath.endsWith(".json", Qt::CaseInsensitive)) {
        onSaveTexJsonClicked();
        return;
    }

    // CASO 2: Stiamo già lavorando su un file .json -> Sovrascrivilo
    saveTextureConfig(m_currentTexturePath);
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

    QVariant vSound = item->data(0, Qt::UserRole + 3);
    if (!vSound.isValid()) return;

    int index = vSound.toInt();
    const LibraryItem &soundData = m_libraryManager.getSound(index);

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
        isAlreadyPresent = m_soundScriptText.contains(soundData.scriptCode.trimmed());
    }

    // PULIZIA ASSOLUTA: Rimuove l'audio da eventuali vecchi caricamenti spuri
    QRegularExpression reMusic(R"(^\s*//MUSIC:.*$\n?)", QRegularExpression::MultilineOption);
    QRegularExpression reProc(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption);

    m_surfaceTextureScriptText.remove(reMusic);
    m_surfaceTextureScriptText.remove(reProc);
    m_surfaceTextureCode = m_surfaceTextureScriptText.trimmed();

    m_bgTextureScriptText.remove(reMusic);
    m_bgTextureScriptText.remove(reProc);
    m_bgTextureCode = m_bgTextureScriptText.trimmed();

    // AGGIORNAMENTO MEMORIA AUDIO (Completamente isolata dalla grafica!)
    if (isAlreadyPresent) {
        m_soundScriptText = ""; // Toggle Off
    } else {
        m_soundScriptText = audioSnippet; // Toggle On
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
    if (m_currentScriptMode == ScriptModeSound) {
        ui->btnRunCurrentScript->setText("Run Sound");
    }

    if (!isAlreadyPresent) {
        onRunSoundClicked();
    }
}


// ==========================================================
// PRIVATE HELPER METHODS
// ==========================================================

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

float MainWindow::parseMath(const QString &text)
{
    return ExpressionParser::evaluateSimple(text);

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
    QString rootPath = settings.value("libraryRootPath").toString();

    QString pathSurf = settings.value("pathSurfaces", rootPath + "/Surfaces").toString();
    QString pathTex  = settings.value("pathTextures", rootPath + "/Textures").toString();
    QString pathRec  = settings.value("pathRecords",  rootPath + "/Records").toString();
    QString pathSnd  = settings.value("pathSounds",   rootPath + "/Sounds").toString();

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

        // 8. FINEZZA: Se tutto è fermo, riporta il tasto a START
        if (ui->glWidget) {
            bool anyMotion = std::abs(ui->glWidget->getNutationSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getPrecessionSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getSpinSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getPhiSpeed()) > 0.001f ||
                             std::abs(ui->glWidget->getPsiSpeed()) > 0.001f;

            if (!anyMotion) {
                ui->glWidget->pauseMotion();
                if (m_btnStart) m_btnStart->setText("START");
                if (ui->btnStart_2) ui->btnStart_2->setText("GO");
            }
            ui->glWidget->update();
        }
    };

    // Usiamo il contesto 'this' per garantire che la connessione sia stabile
    connect(btnPlus, &QPushButton::clicked, this, [changeVal](){ changeVal(1); });
    connect(btnMinus, &QPushButton::clicked, this, [changeVal](){ changeVal(-1); });
}

void MainWindow::parseAndApplyScriptParams(const QString &scriptCode)
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

    QString globalCode = scriptCode + "\n" + m_surfaceTextureCode + "\n" + m_bgTextureCode;
    m_audioController->playFromScript(globalCode);
}

void MainWindow::updateTextureUIState(bool isTextureOn)
{
    // 1. Surface è abilitato SOLO se la texture è SPENTA
    ui->radioEditSurf->setEnabled(!isTextureOn);

    // 2. I controlli Colore Texture sono abilitati SOLO se la texture è ACCESA
    ui->radioTexColor1->setEnabled(isTextureOn);
    ui->radioTexColor2->setEnabled(isTextureOn);

    ui->btnFlatPreview->setEnabled(isTextureOn);

    if (!isTextureOn && ui->btnFlatPreview->isChecked()) {
        ui->btnFlatPreview->setChecked(false);
    }

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
}

void MainWindow::saveTextureConfig(const QString &path) {
    m_presetSerializer->saveTexture(path);
}

QTreeWidgetItem* MainWindow::getCurrentLibraryItem() {
    QWidget* currentTab = ui->tabWidget->currentWidget();

    if (currentTab == ui->Surface) {
        if (ui->treeSurfaces->currentItem()) return ui->treeSurfaces->currentItem();
        if (!ui->treeSurfaces->selectedItems().isEmpty()) return ui->treeSurfaces->selectedItems().first();
    }
    else if (currentTab == ui->Texture) {
        if (ui->treeTextures->currentItem()) return ui->treeTextures->currentItem();
        if (!ui->treeTextures->selectedItems().isEmpty()) return ui->treeTextures->selectedItems().first();
    }
    else if (currentTab == ui->Motions) {
        if (ui->treeMotions->currentItem()) return ui->treeMotions->currentItem();
        if (!ui->treeMotions->selectedItems().isEmpty()) return ui->treeMotions->selectedItems().first();
    }
    else if (currentTab->objectName().contains("Sound", Qt::CaseInsensitive)) {
        if (ui->treeSounds->currentItem()) return ui->treeSounds->currentItem();
        if (!ui->treeSounds->selectedItems().isEmpty()) return ui->treeSounds->selectedItems().first();
    }
    return nullptr;
}

void MainWindow::applyCommonData(const LibraryItem &d)
{
    // 1. Ferma tutto subito per sicurezza
    onStopClicked();
    if (pathTimer->isActive()) onDepartureClicked();
    if (pathTimer3D->isActive()) onDeparture3DClicked();

    m_savedRenderMode = d.renderMode;

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

    // 2. Risoluzione e Limiti (FIX VISIVO "Numero Nascosto")
    ui->stepSlider->setValue(d.steps);
    ui->glWidget->setResolution(d.steps);

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

    // 4. Logica Caricamento Equazioni vs Script
    bool hasValidEquations = false;
    if (d.x.trimmed().length() > 0 && d.x != "0" && d.x != "0.0") hasValidEquations = true;

    if (d.isScript && !d.scriptCode.isEmpty() && !hasValidEquations) {
        m_surfaceScriptText = d.scriptCode;
        this->setProperty("rawSurfaceScript", d.scriptCode);
        ui->lineX->clear(); ui->lineY->clear(); ui->lineZ->clear(); ui->lineP->clear();

        QString temp = ui->txtScriptEditor->toPlainText();
        ui->txtScriptEditor->blockSignals(true);
        ui->txtScriptEditor->setPlainText(d.scriptCode);
        onRunScriptClicked();
        ui->txtScriptEditor->setPlainText(temp);
        ui->txtScriptEditor->blockSignals(false);
    }
    else {
        ui->glWidget->setScriptCheck(false);
        m_surfaceScriptText.clear();

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

        // --- IL FIX REALE: Carichiamo i testi PRIMA di controllare le dipendenze ---
        ui->lineU->setPlainText(d.defU);
        ui->lineV->setPlainText(d.defV);
        ui->lineW->setPlainText(d.defW);

        ui->lineExplicitU->setPlainText(d.explicitU);
        ui->lineExplicitV->setPlainText(d.explicitV);
        ui->lineExplicitW->setPlainText(d.explicitW);

        ui->lineU->blockSignals(bCU);
        ui->lineV->blockSignals(bCV);
        ui->lineW->blockSignals(bCW);

        // ORA controlliamo! Troverà A e B nelle composizioni e non distruggerà gli slider!
        checkParametricDependency();
        // ---------------------------------------------------------------------------

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

        ui->glWidget->updateSurfaceData();
        ui->glWidget->update();
    }

    // --- 5. RESET E CARICAMENTO PATH ---

    // Path 3D
    ui->lineX_P3D->setText(d.path3D_x);
    ui->lineY_P3D->setText(d.path3D_y);
    ui->lineZ_P3D->setText(d.path3D_z);
    ui->lineR_P3D->setText(d.path3D_roll);

    // Invia i dati all'engine. Se le stringhe sono vuote, l'engine disabiliterà il path.
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

    updateRenderState();
}

void MainWindow::addScrollToDock(QDockWidget* dock) {
    if (!dock) return;

    // Recupera il widget che contiene attualmente i bottoni/caselle
    QWidget* innerWidget = dock->widget();

    // Se c'è già una scrollarea o il widget è nullo, ci fermiamo
    if (!innerWidget || qobject_cast<QScrollArea*>(innerWidget)) return;

    // Crea la nuova ScrollArea
    QScrollArea* scrollArea = new QScrollArea(dock);

    // Impostazioni estetiche e funzionali
    scrollArea->setWidget(innerWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Assegna la scrollarea come nuovo widget principale del dock
    dock->setWidget(scrollArea);
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

void MainWindow::setupDefaultFolders()
{
    QSettings settings;
    QString rootPath = settings.value("libraryRootPath").toString();

#ifdef Q_OS_ANDROID
    QString androidDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString autoPath = androidDataPath + "/SurfaceExplorer_Presets";

    if (rootPath.isEmpty() || !QDir(rootPath).exists()) {
        rootPath = autoPath;
        QDir().mkpath(rootPath);
        settings.setValue("libraryRootPath", rootPath);
    }
#else
    if (rootPath.isEmpty() || !QDir(rootPath).exists()) {
        QMessageBox::information(this, "Welcome to Surface Explorer",
                                 "Choose a location to install your Library.\n"
                                 "A 'Presets' folder will be automatically created there.");

        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString selectedPath = QFileDialog::getExistingDirectory(this, "Select Master Folder", defaultPath);

        if (selectedPath.isEmpty()) {
            // L'utente ha annullato, usiamo il default
            rootPath = defaultPath + "/SurfaceExplorer_Presets";
        } else {
            // PULIZIA DEL PERCORSO e AGGIUNTA FORZATA
            selectedPath = QDir::cleanPath(selectedPath);
            rootPath = selectedPath + "/Presets"; // <-- CREA SEMPRE LA SOTTOCARTELLA
        }

        // Creiamo fisicamente la cartella madre sul disco
        QDir().mkpath(rootPath);
        settings.setValue("libraryRootPath", rootPath);
    }
#endif

    // --- CREAZIONE DELLE 4 SOTTOCARTELLE FISSE ---
    QString surfDirUser = rootPath + "/Surfaces";
    QString texDirUser  = rootPath + "/Textures";
    QString recDirUser  = rootPath + "/Records";
    QString sndDirUser  = rootPath + "/Sounds";

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
}

void MainWindow::copyPath(QString src, QString dst) {
    m_fileOps->copyPath(src, dst);
}

void MainWindow::syncResourcesToFolder(const QString &resourcePath, const QString &diskPath, bool forceRestore)
{
    QDir resDir(resourcePath);
    QDir diskDir(diskPath);

    if (!diskDir.exists()) {
        diskDir.mkpath(".");
    }

    // 1. COPIA DEI FILE (Se ce ne sono nella cartella corrente)
    for (const QString &filename : resDir.entryList(QDir::Files)) {
        QString src = resourcePath + "/" + filename;
        QString dst = diskDir.absoluteFilePath(filename);
        QString deletedPath = dst + ".deleted";

        if (forceRestore && QFile::exists(deletedPath)) {
            QFile::remove(deletedPath);
        }

        // Procediamo alla copia solo se non esiste la versione .deleted (o se stiamo forzando il ripristino)
        bool isDeleted = QFile::exists(deletedPath);

        if (!QFile::exists(dst) && (!isDeleted || forceRestore)) {
            if (filename.startsWith("._")) continue;

            if (QFile::copy(src, dst)) {
                QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
                qDebug() << "Copiato:" << dst;
            }
        }
    }

    // 2. GESTIONE SOTTOCARTELLE
    for (const QString &dirName : resDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString subResPath = resourcePath + "/" + dirName;
        QString subDiskPath = diskPath + "/" + dirName;

        // Passiamo il parametro forceRestore anche alle sottocartelle
        syncResourcesToFolder(subResPath, subDiskPath, forceRestore);
    }
}

void MainWindow::updateFlatPreviewButton()
{
    bool hasTex = ui->chkBoxTexture->isChecked();
    bool bgMode = ui->radioBackground->isChecked();

    if (bgMode) {
        ui->btnFlatPreview->setText("2D Background");
    } else {
        ui->btnFlatPreview->setText("2D Surface");
    }

    // Abilita il bottone solo se la casella texture è spuntata
    ui->btnFlatPreview->setEnabled(hasTex);

    // Se stiamo disabilitando il bottone, assicuriamoci che non rimanga premuto
    if (!hasTex && ui->btnFlatPreview->isChecked()) {
        ui->btnFlatPreview->setChecked(false);
    }
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
    QString rootPath = settings.value("libraryRootPath").toString();

    addDirsToWatcher(settings.value("pathSurfaces", rootPath + "/Surfaces").toString());
    addDirsToWatcher(settings.value("pathTextures", rootPath + "/Textures").toString());
    addDirsToWatcher(settings.value("pathMotions", rootPath + "/Motions").toString());
    addDirsToWatcher(settings.value("pathSounds", rootPath + "/Sounds").toString());
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

#include <QDebug> // Assicurati di avere questo in cima al file!

float MainWindow::parseUIConstant(const QString &exprStr, float A, float B, float C, float D, float E, float F, float S)
{
    QString cleanExpr = exprStr.trimmed();
    if (cleanExpr.isEmpty()) return 0.0f;

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
    symbol_table.add_constant("e",  2.71828182845904523536);

    // 4. FIX FONDAMENTALE: Usiamo add_constant invece di add_variable!
    // Inserendo i numeri come costanti assolute evitiamo qualsiasi crash
    // di puntatori o reference in memoria da parte di ExprTk.
    symbol_table.add_constant("A", (double)A); symbol_table.add_constant("a", (double)A);
    symbol_table.add_constant("B", (double)B); symbol_table.add_constant("b", (double)B);
    symbol_table.add_constant("C", (double)C); symbol_table.add_constant("c", (double)C);
    symbol_table.add_constant("D", (double)D); symbol_table.add_constant("d", (double)D);

    // Solo 'E' maiuscola per la UI, per non sovrascrivere la 'e' di Nepero (2.718)
    symbol_table.add_constant("E", (double)E);

    symbol_table.add_constant("F", (double)F); symbol_table.add_constant("f", (double)F);
    symbol_table.add_constant("S", (double)S); symbol_table.add_constant("s", (double)S);

    expression_t expression;
    expression.register_symbol_table(symbol_table);

    parser_t parser;

    // 5. COMPILAZIONE CON DEBUG
    if (parser.compile(cleanExpr.toStdString(), expression)) {
        return static_cast<float>(expression.value());
    } else {
        // SE FALLISCE, ORA SAPREMO IL PERCHÈ!
        qDebug() << "ERRORE EXPRTK: Impossibile calcolare:" << cleanExpr;
        qDebug() << "Motivo:" << QString::fromStdString(parser.error());
        return 0.0f;
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}
