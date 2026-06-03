#include "videorecorder.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "surfaceengine.h"
#include "synthesizer.h"
#include "audiocontroller.h"
#include "nativevideoencoder.h"

#include <QInputDialog>
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QMessageBox>
#include <QApplication>
#include <QMediaPlayer>
#include <QDebug>
#include <QPainter>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
class MobileVideoSaveDialog : public QDialog {
public:
    QSpinBox* spinDuration;
    QSpinBox* spinFps;
    QComboBox* comboRes;
    QComboBox* comboEngine;
    QLineEdit* nameEdit;
#if !defined(Q_OS_IOS)
    QComboBox* comboFormat;
#endif

    MobileVideoSaveDialog(const QString& startDir, int defDur, int defFps, int defRes, QWidget* parent = nullptr)
        : QDialog(parent), currentDir(startDir) {
        setWindowTitle("Export Settings & Save");

        // RIMOSSO setMinimumSize(320, 500)! Ora blocchiamo solo la larghezza, lasciando
        // l'altezza libera di restringersi e adattarsi in landscape.
        setMinimumWidth(320);

        // Layout principale: divide l'area scorrevole dai bottoni in fondo
        QVBoxLayout* mainDialogLayout = new QVBoxLayout(this);
        mainDialogLayout->setContentsMargins(0, 0, 0, 0); // Massimizza lo spazio

        // --- CREAZIONE DELL'AREA DI SCORRIMENTO ---
        QScrollArea* scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame); // Rimuove bordi antiestetici

        // Widget contenitore che andrà DENTRO l'area di scorrimento
        QWidget* scrollContent = new QWidget(scrollArea);
        QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);

        // --- 1. IMPOSTAZIONI VIDEO (Dentro la Scrollbar) ---
        QFormLayout* form = new QFormLayout();
        spinDuration = new QSpinBox(scrollContent);
        spinDuration->setRange(1, 86400);
        spinDuration->setValue(defDur);
        spinDuration->setStyleSheet("padding: 8px; font-size: 16px;");
        form->addRow("Duration (sec):", spinDuration);

        spinFps = new QSpinBox(scrollContent);
        spinFps->setRange(24, 120);
        spinFps->setValue(defFps > 120 ? 120 : defFps);
        spinFps->setStyleSheet("padding: 8px; font-size: 16px;");
        form->addRow("FPS (24-120):", spinFps);

        comboRes = new QComboBox(scrollContent);
        comboRes->addItems({
            "Monitor Default (Current Size)",
            "1080p Full HD (1920x1080)",
            "1440p 2K (2560x1440)",
            "2160p 4K (3840x2160)"
        });
        int safeRes = (defRes >= 0 && defRes < comboRes->count()) ? defRes : 1;
        comboRes->setCurrentIndex(safeRes);
        comboRes->setStyleSheet("padding: 8px; font-size: 14px;");
        form->addRow("Resolution:", comboRes);

#if !defined(Q_OS_IOS)
        comboFormat = new QComboBox(scrollContent);
        comboFormat->addItems({
            "PNG (Lossless - Saves Disk Space)",
            "BMP (Uncompressed - Faster)"
        });
        comboFormat->setStyleSheet("padding: 8px; font-size: 14px;");
        form->addRow("Frame Format:", comboFormat);
#endif

        comboEngine = new QComboBox(scrollContent);
        comboEngine->addItems({
            "Native Resolution (FBO - Max Quality, Slower)",
            "Screen Upscale (Standard, Faster)"
        });
        comboEngine->setStyleSheet("padding: 8px; font-size: 14px;");
        form->addRow("Render Engine:", comboEngine);

        scrollLayout->addLayout(form);

        // --- 2. NAVIGATORE CARTELLE (Dentro la Scrollbar) ---
        pathLabel = new QLabel(currentDir.absolutePath(), scrollContent);
        pathLabel->setWordWrap(true);
        pathLabel->setStyleSheet("font-size: 12px; color: gray; margin-top: 10px;");
        scrollLayout->addWidget(pathLabel);

        QHBoxLayout* nameLayout = new QHBoxLayout();
        nameLayout->addWidget(new QLabel("Name:", scrollContent));
        nameEdit = new QLineEdit("NewVideo", scrollContent);
        nameEdit->setStyleSheet("padding: 10px; font-size: 16px;");
        nameLayout->addWidget(nameEdit);
        connect(nameEdit, &QLineEdit::returnPressed, nameEdit, &QLineEdit::clearFocus);
        scrollLayout->addLayout(nameLayout);

        listWidget = new QListWidget(scrollContent);
        listWidget->setStyleSheet("QListWidget::item { padding: 18px; border-bottom: 1px solid #ddd; font-size: 16px; }");

        // Disattiviamo le scrollbar interne per evitare sovrapposizioni
        listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        scrollLayout->addWidget(listWidget);

        // Chiude il pacchetto e lo inserisce nell'area di scorrimento
        scrollContent->setLayout(scrollLayout);
        scrollArea->setWidget(scrollContent);
        mainDialogLayout->addWidget(scrollArea);

        // --- 3. BOTTONI SALVATAGGIO (FUORI dalla scrollbar, sempre visibili) ---
        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->setContentsMargins(10, 10, 10, 10); // Margine per dare respiro ai tasti
        QPushButton* cancelBtn = new QPushButton("Cancel", this);
        QPushButton* saveBtn = new QPushButton("Save", this);
        cancelBtn->setAutoDefault(false);
        saveBtn->setAutoDefault(false);
        cancelBtn->setStyleSheet("padding: 12px; font-size: 16px;");
        saveBtn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold; color: #cc0000;");
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);

        mainDialogLayout->addLayout(btnLayout);

        // --- LOGICA BOTTONI E SELEZIONE ---
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            QString finalPath = getSelectedPath();
            if (QFile::exists(finalPath)) {
                QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                                          "Overwrite File?",
                                                                          "A video with this name already exists in this folder.\nDo you want to overwrite it?",
                                                                          QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::No) return;
            }
            accept();
        });

        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString data = item->data(Qt::UserRole).toString();

            if (data == "..") {
                currentDir.cdUp();
                refreshList();
            } else if (data.startsWith("DIR|")) {
                // È una cartella: navighiamo all'interno
                currentDir.cd(data.mid(4));
                refreshList();
            } else if (data.startsWith("FILE|")) {
                // È un file video: copiamo il suo nome nella casella di input!
                QString fileName = data.mid(5);
                QFileInfo fi(fileName);
                nameEdit->setText(fi.completeBaseName()); // Prende il nome senza il ".mp4"
            }
        });

        refreshList();
    }

    QString getSelectedPath() const {
        QString name = nameEdit->text();
        if (!name.endsWith(".mp4", Qt::CaseInsensitive)) name += ".mp4";
        return currentDir.absoluteFilePath(name);
    }

private:
    void refreshList() {
        listWidget->clear();

        // 1. Aggiungiamo sempre il tasto per tornare indietro
        QListWidgetItem* upItem = new QListWidgetItem("📁 .. (Up)", listWidget);
        upItem->setData(Qt::UserRole, "..");

        // =========================================================
        // METODO SICURO A DUE PASSAGGI (Anti-Bug per filtri Mobile)
        // =========================================================

        // FASE A: Recuperiamo SOLO LE CARTELLE (azzerando i filtri testuali)
        currentDir.setNameFilters(QStringList());
        QFileInfoList dirs = currentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

        for (const QFileInfo& info : dirs) {
            QListWidgetItem* dirItem = new QListWidgetItem("📁 " + info.fileName(), listWidget);
            dirItem->setData(Qt::UserRole, "DIR|" + info.fileName());
        }

        // FASE B: Recuperiamo SOLO I FILE VIDEO (applicando il filtro)
        currentDir.setNameFilters(QStringList() << "*.mp4" << "*.mov" << "*.avi" << "*.mkv");
        QFileInfoList files = currentDir.entryInfoList(QDir::Files, QDir::Name);

        for (const QFileInfo& info : files) {
            QListWidgetItem* fileItem = new QListWidgetItem("🎞️ " + info.fileName(), listWidget);
            fileItem->setData(Qt::UserRole, "FILE|" + info.fileName());
            fileItem->setForeground(Qt::gray);
        }

        // Pulizia finale: riazzeriamo il filtro per non corrompere la navigazione futura
        currentDir.setNameFilters(QStringList());

        // =========================================================

        pathLabel->setText(currentDir.absolutePath());

        // Espansione dinamica per disattivare la doppia scrollbar
        int itemHeight = 55;
        int totalListHeight = listWidget->count() * itemHeight;
        listWidget->setMinimumHeight(qMax(150, totalListHeight));
    }

    QDir currentDir;
    QLabel* pathLabel;
    QListWidget* listWidget;
};
#endif

VideoRecorder::VideoRecorder(MainWindow *mainWindow, QObject *parent)
    : QObject(parent), m_mainWindow(mainWindow)
{
}

void VideoRecorder::toggleRecord()
{
    // --- GESTIONE TASTO STOP ---
    if (m_mainWindow->m_isRecording) {
        m_mainWindow->m_stopRecordingRequested = true;
        return;
    }

    // ==============================================================
    // 1. SALVATAGGIO STATO E STOP IMMEDIATO (FIX CONGELAMENTO E GEODETICO)
    // ==============================================================
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();

    bool wasTimeAnimating = false;
    if (m_mainWindow->m_btnStart && m_mainWindow->m_btnStart->text().toUpper() == "STOP") {
        wasTimeAnimating = true;
        m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
    }

    // ---> FIX: GESTIONE FLUSSO GEODETICO <---
    QTimer* geoAnimTimer = m_mainWindow->findChild<QTimer*>("geoAnimTimer");
    bool wasGeoAnimating = false;
    if (geoAnimTimer && geoAnimTimer->isActive()) {
        wasGeoAnimating = true;
        geoAnimTimer->stop(); // Fermiamo i calcoli asincroni
    }

    // Rilevamento preciso per sapere se dovremo ricalcolare la mesh frame-by-frame
    QString mainEqs = m_mainWindow->ui->lineX->toPlainText() + " " + m_mainWindow->ui->lineY->toPlainText() + " " + m_mainWindow->ui->lineZ->toPlainText() + " " + m_mainWindow->ui->lineP->toPlainText();
    int upperCount = (mainEqs.contains(QRegularExpression("\\bU\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bV\\b")) ? 1 : 0) +
                     (mainEqs.contains(QRegularExpression("\\bW\\b")) ? 1 : 0);
    bool geoHasText = false;
    if (m_mainWindow->ui->lnU) {
        geoHasText = !m_mainWindow->ui->lnU->toPlainText().trimmed().isEmpty() ||
                     !m_mainWindow->ui->lnV->toPlainText().trimmed().isEmpty() ||
                     !m_mainWindow->ui->lnW->toPlainText().trimmed().isEmpty() ||
                     !m_mainWindow->ui->lndU->toPlainText().trimmed().isEmpty() ||
                     !m_mainWindow->ui->lndV->toPlainText().trimmed().isEmpty() ||
                     !m_mainWindow->ui->lndW->toPlainText().trimmed().isEmpty();
    }
    bool isGeodesicActive = (upperCount > 0) && geoHasText && (m_mainWindow->ui->tabModeSelector->currentIndex() == 0);
    double startGeoTime = m_mainWindow->property("geoTime").toDouble();
    // ----------------------------------------

    // Fermiamo tutto PRIMA di aprire finestre di dialogo
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();
    m_mainWindow->ui->glWidget->stopAllTimers();
    m_mainWindow->ui->glWidget->pauseMotion();

    // Helper per ripristinare lo stato
    auto restoreState = [this, wasAnimating, wasPath4D, wasPath3D, wasTimeAnimating, wasGeoAnimating, geoAnimTimer]() {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) {
            m_mainWindow->pathTimer->start();
            m_mainWindow->ui->btnDeparture->setText("STOP");
        }
        if (wasPath3D) {
            m_mainWindow->pathTimer3D->start();
            m_mainWindow->ui->btnDeparture3D->setText("STOP");
        }
        if (wasTimeAnimating) {
            m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
            m_mainWindow->ui->glWidget->startAnimationTimer();
        }
        if (wasGeoAnimating && geoAnimTimer) {
            geoAnimTimer->start(); // Riavviamo l'asincronia per la normale visualizzazione
        }
    };

    // ==============================================================
    // 2. INPUT UTENTE E PERCORSI SALVATAGGIO
    // ==============================================================
    QSettings settings;
    // DICHIARIAMO TUTTE LE VARIABILI QUI AL SICURO, GLOBALI PER LA FUNZIONE!
    int seconds = 10;
    int fps = 60;
    int resIndex = 1;
    int targetWidth = -1;
    int targetHeight = -1;
    bool useFBO = false;
    bool usePng = false;

    QString rootPath;
#if defined(Q_OS_ANDROID)
    rootPath = "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
#elif defined(Q_OS_IOS)
    rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#else
    rootPath = settings.value("libraryRootPath").toString();
    if (rootPath.isEmpty()) rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#endif

    QString defaultVideoDir = rootPath + "/renders";
    QDir().mkpath(defaultVideoDir);
    QString userSelectedFile;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // --- ANDROID e IOS: FINESTRA UNIFICATA MOBILE ---
    int defDur = settings.value("lastRecDuration", 10).toInt();
    int defFps = settings.value("lastRecFPS", 60).toInt();
    int defRes = settings.value("lastRecResIndex", 1).toInt();

    MobileVideoSaveDialog pathDialog(defaultVideoDir, defDur, defFps, defRes, m_mainWindow);
    if (pathDialog.exec() != QDialog::Accepted) {
        restoreState();
        return;
    }

    seconds = pathDialog.spinDuration->value();
    fps = pathDialog.spinFps->value();
    resIndex = pathDialog.comboRes->currentIndex();
    userSelectedFile = pathDialog.getSelectedPath();
    useFBO = (pathDialog.comboEngine->currentIndex() == 0);

#if defined(Q_OS_IOS)
    usePng = false; // iOS forza sempre BMP silenziosamente
#else
    usePng = (pathDialog.comboFormat->currentIndex() == 0);
#endif

#else
    // --- DESKTOP (Mac, Windows, Linux): FINESTRE CLASSICHE ---
    int defaultDuration = settings.value("lastRecDuration", 10).toInt();
    bool ok;
    seconds = QInputDialog::getInt(m_mainWindow, "Render Video", "Video duration (seconds):", defaultDuration, 1, 86400, 1, &ok);
    if (!ok) { restoreState(); return; }

    int defaultFPS = settings.value("lastRecFPS", 60).toInt();

#if defined(Q_OS_MACOS)
    int maxFps = 84;
    QString fpsPrompt = "FPS (24-84) [QuickTime slo-mo limit]:";
#else
    int maxFps = 120;
    QString fpsPrompt = "FPS (24-120):";
#endif

    if (defaultFPS > maxFps) defaultFPS = maxFps;

    fps = QInputDialog::getInt(m_mainWindow, "Frame Rate", fpsPrompt, defaultFPS, 24, maxFps, 1, &ok);
    if (!ok) { restoreState(); return; }

    QStringList resolutions = {
        "Monitor Default (Current Window Size)",
        "1080p Full HD (1920x1080)",
        "1440p 2K (2560x1440)",
        "2160p 4K (3840x2160)"
    };
    int defaultResIndex = settings.value("lastRecResIndex", 1).toInt();
    QString selectedRes = QInputDialog::getItem(m_mainWindow, "Video Resolution", "Select export resolution:", resolutions, defaultResIndex, false, &ok);
    if (!ok) { restoreState(); return; }
    resIndex = resolutions.indexOf(selectedRes);

    // Formato
    QStringList formats = {
        "PNG (Lossless - Saves Disk Space)",
        "BMP (Uncompressed - Faster)"
    };
    int defaultFormatIndex = settings.value("lastRecFormat", 0).toInt();
    QString selectedFormat = QInputDialog::getItem(m_mainWindow, "Frame Format", "Select temporary frame format:", formats, defaultFormatIndex, false, &ok);
    if (!ok) { restoreState(); return; }
    int formatIndex = formats.indexOf(selectedFormat);
    settings.setValue("lastRecFormat", formatIndex);
    usePng = (formatIndex == 0);

    // FBO
    QStringList engineModes = {
        "Native Resolution (FBO - Max Quality, Slower)",
        "Screen Upscale (Standard, Faster)"
    };
    int defaultEngineIndex = settings.value("lastRecEngine", 0).toInt();
    QString selectedEngine = QInputDialog::getItem(m_mainWindow, "Render Engine", "Select rendering engine:", engineModes, defaultEngineIndex, false, &ok);
    if (!ok) { restoreState(); return; }
    int engineIndex = engineModes.indexOf(selectedEngine);
    settings.setValue("lastRecEngine", engineIndex);
    useFBO = (engineIndex == 0);

    // Salvataggio File
    QString lastVideoDir = settings.value("lastVideoDir", defaultVideoDir).toString();
    if (!QDir(lastVideoDir).exists()) lastVideoDir = defaultVideoDir;
    QString defaultSelection = lastVideoDir + "/NewVideo.mp4";
    userSelectedFile = QFileDialog::getSaveFileName(m_mainWindow, "Save MP4 Video", defaultSelection, "MP4 Video (*.mp4)", nullptr, QFileDialog::DontUseNativeDialog);
    if (userSelectedFile.isEmpty()) { restoreState(); return; }
    if (!userSelectedFile.endsWith(".mp4", Qt::CaseInsensitive)) userSelectedFile += ".mp4";
    settings.setValue("lastVideoDir", QFileInfo(userSelectedFile).absolutePath());
#endif

    // --- SALVATAGGIO IMPOSTAZIONI E CALCOLO RISOLUZIONE (COMUNE) ---
    settings.setValue("lastRecDuration", seconds);
    settings.setValue("lastRecFPS", fps);
    settings.setValue("lastRecResIndex", resIndex);

    if (resIndex == 1) { targetWidth = 1920; targetHeight = 1080; }
    else if (resIndex == 2) { targetWidth = 2560; targetHeight = 1440; }
    else if (resIndex == 3) { targetWidth = 3840; targetHeight = 2160; }

    // ==============================================================
    // 3. PREPARAZIONE REGISTRAZIONE
    // ==============================================================
    m_mainWindow->m_isRecording = true;
    m_mainWindow->m_stopRecordingRequested = false;
    m_mainWindow->ui->glWidget->setUpdatesEnabled(false);

    // ---> BLOCCA LO SPEGNIMENTO DELLO SCHERMO <---
#if defined(Q_OS_IOS)
    NativeVideoEncoder::setKeepScreenOn(true);
#elif defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) {
            QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (window.isValid()) {
                const int FLAG_KEEP_SCREEN_ON = 128; // Flag di sistema Android
                window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
            }
        }
    });
#endif
    // STOP AUDIO E ATTIVAZIONE TEMPO VIRTUALE
    m_mainWindow->m_audioController->stopAll();

    // 1. Creiamo una variabile per salvare il momento esatto in cui l'utente ha premuto REC
    float initialTimeOffset = 0.0f;

    if (m_mainWindow->ui->glWidget) {
        // 2. Leggiamo il tempo reale corrente dello shader dal vivo
        initialTimeOffset = m_mainWindow->ui->glWidget->property("virtual_time").toFloat();

        if (initialTimeOffset < 0.001f) initialTimeOffset = 0.001f;

        // Diciamo al widget di smettere di usare il tempo reale
        m_mainWindow->ui->glWidget->setProperty("use_virtual_time", true);

        // 3. Impostiamo il virtual_time iniziale al tempo appena catturato
        m_mainWindow->ui->glWidget->setProperty("virtual_time", initialTimeOffset);
    }

    // Feedback visivo
    m_mainWindow->m_btnRec->setText("STOP");
    m_mainWindow->m_btnRec->setStyleSheet("QPushButton { color: white; background-color: red; font-weight: bold; border-radius: 4px; }");

    int totalFrames = seconds * fps;
    float timeStep = 1.0f / (float)fps;
    float fpsScale = 30.0f / (float)fps;

#ifdef Q_OS_ANDROID
    // Android: Usa la cache interna dell'app in modo che FFmpeg (C nativo) abbia i permessi
    QString targetPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    QString targetPath = rootPath + "/renders";
#endif

    QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_mainWindow->m_recFolder = targetPath + "/Temp_Render_" + timeStamp;
    if (!QDir(m_mainWindow->m_recFolder).exists()) {
        QDir().mkpath(m_mainWindow->m_recFolder);
    }

    // Recupero variabili per il loop
    float currentPathT = m_mainWindow->pathTimeT;

    float vNut   = m_mainWindow->ui->glWidget->getNutationSpeed();
    float vPrec  = m_mainWindow->ui->glWidget->getPrecessionSpeed();
    float vSpin  = m_mainWindow->ui->glWidget->getSpinSpeed();
    float vOmega = m_mainWindow->ui->glWidget->getOmegaSpeed();
    float vPhi   = m_mainWindow->ui->glWidget->getPhiSpeed();
    float vPsi   = m_mainWindow->ui->glWidget->getPsiSpeed();

    float startOmega = m_mainWindow->ui->glWidget->getOmega();
    float startPhi   = m_mainWindow->ui->glWidget->getPhi();
    float startPsi   = m_mainWindow->ui->glWidget->getPsi();

    float baseStep = m_mainWindow->m_pathSpeed4D;
    float pathStep = baseStep * fpsScale;

    // UI Feedback
    m_mainWindow->m_statusLabel->clear();
    m_mainWindow->m_renderProgress->setRange(0, totalFrames);
    m_mainWindow->m_renderProgress->setValue(0);
    m_mainWindow->m_renderProgress->setVisible(true);
    m_mainWindow->m_renderProgress->setFormat("%v / %m (%p%)");
    m_mainWindow->m_renderProgress->setAlignment(Qt::AlignCenter);

    m_mainWindow->ui->dockEquations->setEnabled(false);
    m_mainWindow->ui->dockSurfaces->setEnabled(false);
    m_mainWindow->ui->dock3D->setEnabled(false);
    m_mainWindow->ui->dock4D->setEnabled(false);

    SurfaceEngine* engine = m_mainWindow->ui->glWidget->getEngine();
    int actualFramesRendered = 0;

    // ==============================================================
    // 4. CICLO RENDERING
    // ==============================================================
    for (int i = 0; i < totalFrames; i++) {

        if (i % 5 == 0) {
            QApplication::processEvents();
        }

        if (m_mainWindow->m_stopRecordingRequested) {
            break;
        }

        // Calcolo corretto del tempo basato sull'istante in cui è iniziata la registrazione
        float currentTime = initialTimeOffset + (i * timeStep);

        // INVIA IL TEMPO VIRTUALE AL BACKGROUND
        if (m_mainWindow->ui->glWidget) {
            m_mainWindow->ui->glWidget->setProperty("virtual_time", currentTime);
        }

        // ====================================================

        if (wasPath4D) {
            float t = currentPathT + (i * pathStep);
            float dt = 0.01f;

            QVector4D p_curr = engine->evaluatePathPosition(t);
            QVector4D p_next = engine->evaluatePathPosition(t + dt);

            // --- FIX PATH 4D: CENTER vs TANGENT ---
            QVector4D V, finalPos4D, finalTarget4D;

            if (m_mainWindow->m_pathMode == MainWindow::ModeTangential) {
                QVector4D velocity = p_next - engine->evaluatePathPosition(t - dt);
                V = (velocity.lengthSquared() > 1e-8f) ? velocity.normalized() : QVector4D(0, 1, 0, 0);

                finalPos4D = p_curr - V * 0.2f;
                finalTarget4D = p_next;
            }
            else {
                finalPos4D = p_curr;
                finalTarget4D = QVector4D(0,0,0,0);
                QVector4D viewDir = finalTarget4D - finalPos4D;
                V = (viewDir.lengthSquared() > 1e-8f) ? viewDir.normalized() : QVector4D(0,0,-1,0);
            }
            // ---------------------------------------

            float alpha = engine->evaluatePathAlpha(t);
            float beta  = engine->evaluatePathBeta(t);
            float gamma = engine->evaluatePathGamma(t);

            QVector4D N1, N2, N3;
            QVector4D K(0.0f, 0.0f, 1.0f, 0.0f);
            N1 = K - V * QVector4D::dotProduct(K, V);
            if (N1.lengthSquared() > 1e-6f) N1.normalize();
            else N1 = QVector4D(0,1,0,0);

            QVector3D v3 = V.toVector3D();
            QVector3D n13 = N1.toVector3D();
            QVector3D side3 = QVector3D::crossProduct(v3, n13);
            if (side3.lengthSquared() > 1e-6f) {
                N2 = QVector4D(side3, 0.0f).normalized();
                N2 = N2 - V * QVector4D::dotProduct(N2, V) - N1 * QVector4D::dotProduct(N2, N1);
                N2.normalize();
            } else N2 = QVector4D(1,0,0,0);

            float dx =  MainWindow::det3x3(V.y(), V.z(), V.w(),  N1.y(), N1.z(), N1.w(),  N2.y(), N2.z(), N2.w());
            float dy = -MainWindow::det3x3(V.x(), V.z(), V.w(),  N1.x(), N1.z(), N1.w(),  N2.x(), N2.z(), N2.w());
            float dz =  MainWindow::det3x3(V.x(), V.y(), V.w(),  N1.x(), N1.y(), N1.w(),  N2.x(), N2.y(), N2.w());
            float dw = -MainWindow::det3x3(V.x(), V.y(), V.z(),  N1.x(), N1.y(), N1.z(),  N2.x(), N2.y(), N2.z());
            N3 = QVector4D(dx, dy, dz, dw).normalized();

            float ca = std::cos(alpha), sa = std::sin(alpha);
            float cb = std::cos(beta),  sb = std::sin(beta);
            float cg = std::cos(gamma), sg = std::sin(gamma);

            float c1 = ca * cb;
            float c2 = sa * cg - ca * sb * sg;
            float c3 = sa * sg + ca * sb * cg;
            QVector4D finalUp4D = N1 * c1 + N2 * c2 + N3 * c3;
            finalUp4D.normalize();

            // RIMOZIONE MOLTIPLICAZIONE ERRATA: Le coordinate rimangono incontaminate
            float rotPhi   = -gamma;
            float rotPsi   = -beta;
            m_mainWindow->ui->glWidget->setRotation4D(0.0f, rotPhi, rotPsi);

            auto transformCPU = [&](QVector4D v) {
                if (std::abs(rotPhi) > 1e-6f) {
                    float c = std::cos(rotPhi), s = std::sin(rotPhi);
                    float y = v.y(), w = v.w();
                    v.setY(y * c + w * s); v.setW(-y * s + w * c);
                }
                if (std::abs(rotPsi) > 1e-6f) {
                    float c = std::cos(rotPsi), s = std::sin(rotPsi);
                    float z = v.z(), w = v.w();
                    v.setZ(z * c + w * s); v.setW(-z * s + w * c);
                }
                return v;
            };

            m_mainWindow->ui->glWidget->setCameraFrom4DVectors(transformCPU(finalPos4D), transformCPU(finalTarget4D), transformCPU(finalUp4D));
        }
        else if (wasPath3D) {
            float step3D = m_mainWindow->m_pathSpeed3D * fpsScale;
            float t = m_mainWindow->pathTimeT3D + (i * step3D);

            QVector4D raw = engine->evaluatePath3DPosition(t);
            // RIMOZIONE MOLTIPLICAZIONE ERRATA
            QVector3D curPos = raw.toVector3D();
            float roll = raw.w();

            // --- FIX PATH 3D: CENTER vs TANGENT ---
            QVector3D target;

            if (m_mainWindow->m_pathMode == MainWindow::ModeTangential) {
                // Guarda avanti (Tangent)
                QVector4D nextRaw = engine->evaluatePath3DPosition(t + 0.1f);
                target = nextRaw.toVector3D(); // RIMOZIONE MOLTIPLICAZIONE ERRATA
            } else {
                // Guarda Origine (Center)
                target = QVector3D(0.0f, 0.0f, 0.0f);
            }
            // --------------------------------------

            m_mainWindow->ui->glWidget->setCameraPosAndDirection3D(curPos, target, roll);
        }
        else {
            // Animazione Standard
            float newOmega = startOmega + (vOmega * currentTime);
            float newPhi   = startPhi   + (vPhi   * currentTime);
            float newPsi   = startPsi   + (vPsi   * currentTime);
            m_mainWindow->ui->glWidget->setRotation4D(newOmega, newPhi, newPsi);

            // ---> FIX 4: Moltiplicatore globale basato sul nuovo slider 3D <---
            float timeFactor = m_mainWindow->m_pathSpeed3D / 0.01f;

            float engineMult = 2.0f;
            float ticksPerSec = 60.0f;

            float frameMult = timeFactor * engineMult * (ticksPerSec / (float)fps);

            if (std::abs(vNut) > 0.0001f || std::abs(vPrec) > 0.0001f || std::abs(vSpin) > 0.0001f) {
                m_mainWindow->ui->glWidget->addObjectRotation(vPrec * frameMult, vNut * frameMult, vSpin * frameMult);
            }
        }

        m_mainWindow->ui->glWidget->setShaderTime(currentTime);

        // 1. Estrazione del frame (con o senza FBO)
        QImage frame = m_mainWindow->ui->glWidget->getFrameForVideo(targetWidth, targetHeight, useFBO);
        frame.setDevicePixelRatio(1.0);

        // ==============================================================
        // 2. ALPHA CHANNEL (FONDAMENTALE PER IOS E FBO)
        // ==============================================================
        if (frame.format() != QImage::Format_RGB32) {
            frame = frame.convertToFormat(QImage::Format_RGB32);
        }

        // ==============================================================
        // 3. SCALING E LETTERBOXING (SOLO SE NON USIAMO L'FBO)
        // ==============================================================
        if (!useFBO && targetWidth > 0 && targetHeight > 0) {
            QImage scaledFrame = frame.scaled(targetWidth, targetHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            QImage finalFrame(targetWidth, targetHeight, QImage::Format_RGB32);
            finalFrame.fill(Qt::black);
            QPainter painter(&finalFrame);
            int x = (targetWidth - scaledFrame.width()) / 2;
            int y = (targetHeight - scaledFrame.height()) / 2;
            painter.drawImage(x, y, scaledFrame);
            painter.end();

            frame = finalFrame;
        } else if (!useFBO) {
            // Risoluzione nativa: taglia i bordi se dispari (per FFmpeg)
            if (frame.width() % 2 != 0) frame = frame.copy(0, 0, frame.width()-1, frame.height());
            if (frame.height() % 2 != 0) frame = frame.copy(0, 0, frame.width(), frame.height()-1);
        }

        // ==============================================================
        // 4. UNICO SALVATAGGIO FINALE DEL FOTOGRAMMA
        // ==============================================================
        QString fileName; // Dichiariamo la variabile UNA SOLA VOLTA qui per evitare errori

#if defined(Q_OS_IOS)
        // iOS usa sempre BMP a 24-bit per il NativeVideoEncoder
        fileName = QString("frame_%1.bmp").arg(i, 5, 10, QChar('0'));
        frame.save(m_mainWindow->m_recFolder + "/" + fileName, "BMP");
#else
        // Desktop e Android usano la scelta dell'utente
        if (usePng) {
            fileName = QString("frame_%1.png").arg(i, 5, 10, QChar('0'));
            frame.save(m_mainWindow->m_recFolder + "/" + fileName, "PNG");
        } else {
            fileName = QString("frame_%1.bmp").arg(i, 5, 10, QChar('0'));
            frame.save(m_mainWindow->m_recFolder + "/" + fileName, "BMP");
        }
#endif

        m_mainWindow->m_renderProgress->setValue(i + 1);
        actualFramesRendered++;
    }

    // ==============================================================
    // 5. RIPRISTINO E PULIZIA
    // ==============================================================
    m_mainWindow->m_isRecording = false;
    m_mainWindow->ui->glWidget->setUpdatesEnabled(true);

#if defined(Q_OS_IOS)
    NativeVideoEncoder::setKeepScreenOn(false);
#elif defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) {
            QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (window.isValid()) {
                const int FLAG_KEEP_SCREEN_ON = 128;
                window.callMethod<void>("clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
            }
        }
    });
#endif

    // >>> DISATTIVAZIONE TEMPO VIRTUALE E RIAVVIO AUDIO <<<
    if (m_mainWindow->ui->glWidget) {
        m_mainWindow->ui->glWidget->setProperty("use_virtual_time", false);
    }

    // Possiamo chiamare la funzione privata di MainWindow grazie alla friend class
    m_mainWindow->onRunSoundClicked();
    // ==============================================================

    m_mainWindow->m_btnRec->setText("REC");
    m_mainWindow->m_btnRec->setStyleSheet("QPushButton { color: red; font-weight: bold; }");

    m_mainWindow->ui->dockEquations->setEnabled(true);
    m_mainWindow->ui->dockSurfaces->setEnabled(true);
    m_mainWindow->ui->dock3D->setEnabled(true);
    m_mainWindow->ui->dock4D->setEnabled(true);

    restoreState();

    m_mainWindow->ui->glWidget->setRotation4D(startOmega, startPhi, startPsi);
    m_mainWindow->ui->glWidget->update();

    m_mainWindow->m_statusLabel->setText("Generating MP4... please wait.");
    m_mainWindow->m_renderProgress->setVisible(false);

    // ==============================================================
    // 6. GENERAZIONE VIDEO (FFMPEG)
    // ==============================================================
    float actualSeconds = (float)actualFramesRendered / (float)fps;

    // 1. Uniamo TUTTI gli script per trovare la musica (incluso il dock Sounds!)
    QString currentCode = m_mainWindow->m_surfaceScriptText + "\n" +
                          m_mainWindow->m_surfaceTextureCode + "\n" +
                          m_mainWindow->m_bgTextureCode + "\n" +
                          m_mainWindow->m_soundScriptText;

    if (currentCode.trimmed().isEmpty()) {
        currentCode = m_mainWindow->ui->txtScriptEditor->toPlainText();
    }

    QString audioFile = "";
    bool hasAudio = false;
    bool isRaw = false;

    // 2. SCENARIO A: Audio Procedurale (GPU GLSL Synth)
    if (currentCode.contains("mainSound") && m_mainWindow->m_audioController) {
        audioFile = m_mainWindow->m_recFolder + "/soundtrack.raw";

        // Bouncing Offline (Molto più veloce del tempo reale)
        if (m_mainWindow->m_audioController->saveSynthToRawFile(audioFile, (int)ceil(actualSeconds))) {
            hasAudio = true;
            isRaw = true;
        }
    }
    // 3. SCENARIO B: File Esterno (MP3/WAV)
    else {
        QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
        QRegularExpressionMatch musicMatch = musicRe.match(currentCode);
        if (musicMatch.hasMatch()) {
            audioFile = musicMatch.captured(1).trimmed();
            hasAudio = QFile::exists(audioFile);
            isRaw = false;
        }
    }

#if defined(Q_OS_ANDROID)
    // ANDROID: FFmpeg scrive in un file temporaneo isolato
    QString videoFileName = m_mainWindow->m_recFolder + "/temp_video.mp4";
#else
    // DESKTOP: FFmpeg scrive DIRETTAMENTE nel file di destinazione finale
    QString videoFileName = userSelectedFile;
#endif

#if defined(Q_OS_IOS)
    QString inputPattern = m_mainWindow->m_recFolder + "/frame_%05d.bmp";
#else
    // Diciamo a FFmpeg di leggere i file in base alla scelta che abbiamo fatto
    QString inputPattern = m_mainWindow->m_recFolder + (usePng ? "/frame_%05d.png" : "/frame_%05d.bmp");
#endif

    QStringList arguments;

    // 1. INPUT VIDEO
    arguments << "-y" << "-framerate" << QString::number(fps) << "-i" << inputPattern;

    // 2. INPUT AUDIO (Loop infinito che poi taglieremo)
    if (hasAudio) {
        arguments << "-stream_loop" << "-1";

        if (isRaw) arguments << "-f" << "f32le" << "-ar" << "44100" << "-ac" << "2" << "-i" << audioFile;
        else arguments << "-i" << audioFile;
    }

    // 3. IMPOSTAZIONI OUTPUT VIDEO
    arguments << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2"
              << "-c:v" << "libx264"
              << "-preset" << "fast"
              << "-pix_fmt" << "yuv420p"
              << "-crf" << "18"
              << "-r" << QString::number(fps) // Forza il contenitore MP4 ai tuoi FPS esatti
              << "-vframes" << QString::number(actualFramesRendered) // LA GHIGLIOTTINA: Solo N fotogrammi, vietato inventarne!
              << "-movflags" << "+faststart";

    // 4. IMPOSTAZIONI OUTPUT AUDIO (E chiusura)
    if (hasAudio) {
        arguments << "-c:a" << "aac" << "-b:a" << "192k";
        arguments << "-t" << QString::number(actualSeconds, 'f', 3); // Taglia l'audio in eccesso
    } else {
        arguments << "-t" << QString::number(actualSeconds, 'f', 3);
    }

    arguments << videoFileName;

#if !defined(Q_OS_IOS)
    // ==============================================================
    // 6. GENERAZIONE VIDEO (FFMPEG VIA QPROCESS PER DESKTOP E ANDROID)
    // ==============================================================
    m_mainWindow->m_isProcessingVideo = true;
    QProcess *ffmpegProcess = new QProcess(this);

// 1. FIX FONDAMENTALE: Applichiamo la rimozione solo su vero Linux Desktop, non su Android!
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");
    ffmpegProcess->setProcessEnvironment(env);
#endif

    connect(ffmpegProcess, &QProcess::errorOccurred, this, [this, ffmpegProcess](QProcess::ProcessError err){
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QString errorMsg = (err == QProcess::FailedToStart)
                               ? "L'eseguibile FFmpeg non ha i permessi per avviarsi o è stato corrotto.\nOS Block: W^X Violation."
                               : "Errore durante l'esecuzione di FFmpeg.";
        QMessageBox::critical(m_mainWindow, "Video Creation Error", errorMsg);
        ffmpegProcess->deleteLater();
    });

    connect(ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, videoFileName, userSelectedFile, ffmpegProcess](int exitCode, QProcess::ExitStatus status){
                m_mainWindow->m_isProcessingVideo = false;
                m_mainWindow->m_statusLabel->clear();

                if (status == QProcess::NormalExit && exitCode == 0) {

#if defined(Q_OS_ANDROID)
            // Copia fisica sicura (Byte per Byte come in MainWindow)
            QFile inFile(videoFileName);
            if (inFile.open(QIODevice::ReadOnly)) {
                QFile outFile(userSelectedFile);
                if (outFile.exists()) outFile.remove();
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(inFile.readAll());
                    outFile.close();

                    // FORZIAMO L'AGGIORNAMENTO DEL MEDIASTORE (Appare subito in Galleria!)
                    QJniEnvironment env;
                    jstring jFilePath = env->NewStringUTF(userSelectedFile.toUtf8().constData());
                    jobjectArray pathsArray = env->NewObjectArray(1, env->FindClass("java/lang/String"), jFilePath);
                    QJniObject context = QNativeInterface::QAndroidApplication::context();
                    QJniObject::callStaticMethod<void>(
                        "android/media/MediaScannerConnection", "scanFile",
                        "(Landroid/content/Context;[Ljava/lang/String;[Ljava/lang/String;Landroid/media/MediaScannerConnection$OnScanCompletedListener;)V",
                        context.object(), pathsArray, nullptr, nullptr
                        );
                    env->DeleteLocalRef(pathsArray);
                    env->DeleteLocalRef(jFilePath);
                }
                inFile.close();
            }
#endif

            // Pulizia profonda e totale dei file temporanei
            QDir tempDir(m_mainWindow->m_recFolder);
            if (tempDir.exists()) {
                tempDir.removeRecursively();
            }

            QMessageBox::information(m_mainWindow, "Finished!", "Video successfully saved:\n" + userSelectedFile);
        } else {
            QString log = ffmpegProcess->readAllStandardError();
            QMessageBox::warning(m_mainWindow, "Encoding Error",
                                 "FFmpeg exited with an error.\nExit code: " + QString::number(exitCode) +
                                     "\n\nLog:\n" + log.right(500));
        }
        ffmpegProcess->deleteLater();
    });

    QString program;

#if defined(Q_OS_ANDROID)
    // Su Android peschiamo il finto .so estratto nella cartella nativa dell'app
    program = QCoreApplication::applicationDirPath() + "/libffmpeg_exec.so";

    if (!QFile::exists(program)) {
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QMessageBox::critical(m_mainWindow, "Missing FFmpeg", "Cannot find static FFmpeg binary at:\n" + program);
        return;
    }

    // 2. LA MAGIA: Insegniamo al linker dove trovare libc++_shared.so (nella cartella dell'app!)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LD_LIBRARY_PATH", QCoreApplication::applicationDirPath());
    ffmpegProcess->setProcessEnvironment(env);

    // IL TRUCCO MAGICO: Linker di sistema manuale
    QStringList linkerArgs;
    linkerArgs << program << arguments;
    ffmpegProcess->start("/system/bin/linker64", linkerArgs);

#else
    // Logica originale Desktop
    program = QStandardPaths::findExecutable("ffmpeg");
    if (program.isEmpty()) {
        QStringList candidates;
#ifdef Q_OS_WIN
        candidates << "C:/ffmpeg/bin/ffmpeg.exe" << "C:/Program Files/ffmpeg/bin/ffmpeg.exe";
#else
        candidates << "/opt/homebrew/bin/ffmpeg" << "/usr/local/bin/ffmpeg" << "/usr/bin/ffmpeg";
#endif
        for(const QString &path : candidates) {
            if (QFile::exists(path)) { program = path; break; }
        }
    }

    if (program.isEmpty()) {
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QMessageBox::critical(m_mainWindow, "Missing FFmpeg", "Cannot find 'ffmpeg'.\nMake sure it is installed and in your PATH.");
        return;
    }

    ffmpegProcess->start(program, arguments);
#endif

#elif defined(Q_OS_IOS)
    // ==============================================================
    // VERSIONE iOS (AVFoundation Nativo con Audio Multiplexing)
    // ==============================================================
    m_mainWindow->m_statusLabel->setText("Assembling MP4 with Audio...");
    QApplication::processEvents();

    QString finalAudioFile = "";

    // NUOVO: Se c'è audio grezzo (.raw), gli costruiamo attorno un'intestazione WAV!
    if (hasAudio) {
        if (isRaw) {
            QString wavPath = m_mainWindow->m_recFolder + "/soundtrack.wav";
            QFile rawFile(audioFile);
            QFile wavFile(wavPath);
            if (rawFile.open(QIODevice::ReadOnly) && wavFile.open(QIODevice::WriteOnly)) {
                QByteArray rawData = rawFile.readAll();
                QDataStream out(&wavFile);
                out.setByteOrder(QDataStream::LittleEndian);

                // Scrittura Header WAV (44.1kHz, Stereo, 32-bit Float)
                out.writeRawData("RIFF", 4);
                out << (quint32)(36 + rawData.size());
                out.writeRawData("WAVE", 4);
                out.writeRawData("fmt ", 4);
                out << (quint32)16 << (quint16)3 << (quint16)2 << (quint32)44100;
                out << (quint32)(44100 * 2 * 4) << (quint16)(2 * 4) << (quint16)32;
                out.writeRawData("data", 4);
                out << (quint32)rawData.size();

                wavFile.write(rawData);
                rawFile.close();
                wavFile.close();
                finalAudioFile = wavPath;
            }
        } else {
            // È già un mp3 o wav
            finalAudioFile = audioFile;
        }
    }

    // Usa il percorso scelto dall'utente tramite la finestra di dialogo
    QString finalSafePath = userSelectedFile;

    QString firstFramePath = m_mainWindow->m_recFolder + "/frame_00000.bmp";
    QImage sampleFrame(firstFramePath);
    int videoW = sampleFrame.width();
    int videoH = sampleFrame.height();

    if (videoW % 2 != 0) videoW--;
    if (videoH % 2 != 0) videoH--;

    // Passiamo finalAudioFile alla funzione iOS
    bool success = NativeVideoEncoder::createMP4(m_mainWindow->m_recFolder, finalSafePath, fps, videoW, videoH, finalAudioFile);

    m_mainWindow->m_statusLabel->clear();

    if (success) {
        QDir tempDir(m_mainWindow->m_recFolder);
        if (tempDir.exists()) tempDir.removeRecursively();
        QMessageBox::information(m_mainWindow, "Finished!", "Video successfully generated on iOS!\nSaved as: " + finalSafePath);
    } else {
        QMessageBox::warning(m_mainWindow, "Encoding Error", "Failed to assemble the video on iOS.");
    }
#endif
}
