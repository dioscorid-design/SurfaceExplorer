#include "presetserializer.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "libraryfileoperations.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QSettings>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMessageBox>
#include <QTimer>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDir>

// --- INIZIO CUSTOM MOBILE DIALOG ---
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QMessageBox>

class MobileSaveDialog : public QDialog {
public:
    MobileSaveDialog(const QString& title, const QString& startDir, const QString& defaultFileName, QWidget* parent = nullptr)
        : QDialog(parent), currentDir(startDir) {
        setWindowTitle(title);

        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        pathLabel = new QLabel(currentDir.absolutePath(), this);
        pathLabel->setWordWrap(true);
        pathLabel->setStyleSheet("font-size: 12px; color: gray;");
        mainLayout->addWidget(pathLabel);

        QHBoxLayout* nameLayout = new QHBoxLayout();
        nameLayout->addWidget(new QLabel("Name:", this));
        nameEdit = new QLineEdit(defaultFileName, this);
        nameEdit->setStyleSheet("padding: 10px; font-size: 16px;");
        nameLayout->addWidget(nameEdit);
        mainLayout->addLayout(nameLayout);

        listWidget = new QListWidget(this);
        listWidget->setStyleSheet("QListWidget::item { padding: 18px; border-bottom: 1px solid #ddd; font-size: 16px; }");
        listWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        mainLayout->addWidget(listWidget, 1);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* cancelBtn = new QPushButton("Cancel", this);
        QPushButton* saveBtn = new QPushButton("Save", this);
        saveBtn->setObjectName("mobileSaveBtn");

        // ---> FIX 1: Impedisce ai pulsanti di intercettare il tasto Invio chiudendo il dialogo
        cancelBtn->setAutoDefault(false);
        saveBtn->setAutoDefault(false);

        cancelBtn->setStyleSheet("padding: 12px; font-size: 16px;");
        saveBtn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold;");
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);
        mainLayout->addLayout(btnLayout);

        // ---> FIX 2: Alla pressione di Invio, toglie solo il focus (su iOS la tastiera sparisce)
        connect(nameEdit, &QLineEdit::returnPressed, this, [this]() {
            nameEdit->clearFocus();
        });

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, &MobileSaveDialog::onSaveClicked);

        // Se l'utente cambia il nome digitando, resettiamo il tasto "Save"
        connect(nameEdit, &QLineEdit::textChanged, this, [this]() {
            QPushButton* btn = this->findChild<QPushButton*>("mobileSaveBtn");
            if (btn && btn->text() != "Save") {
                btn->setText("Save");
                btn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold;");
            }
        });

        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString itemName = item->data(Qt::UserRole).toString();
            QString itemType = item->data(Qt::UserRole + 1).toString();

            if (itemType == "DIR") {
                if (itemName == "..") currentDir.cdUp();
                else currentDir.cd(itemName);
                refreshList();
            }
            else if (itemType == "FILE") {
                nameEdit->setText(itemName);
            }
        });

        refreshList();

        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            QRect screenGeom = screen->availableGeometry();
            int w = screenGeom.width() * 0.98;
            int h = screenGeom.height() * 0.92;
            this->resize(w, h);
            this->move(screenGeom.center() - this->rect().center());
        }

        mainLayout->setContentsMargins(8, 8, 8, 8);
        mainLayout->setSpacing(6);
        mainLayout->setStretch(2, 1);
    }

    QString getSelectedPath() const {
        QString name = nameEdit->text();
        if (!name.endsWith(".json", Qt::CaseInsensitive)) name += ".json";
        return currentDir.absoluteFilePath(name);
    }

private:
    void refreshList() {
        listWidget->clear();
        QListWidgetItem* upItem = new QListWidgetItem("📁 .. (Up)", listWidget);
        upItem->setData(Qt::UserRole, "..");
        upItem->setData(Qt::UserRole + 1, "DIR");

        QFileInfoList dirs = currentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& dir : dirs) {
            QListWidgetItem* dirItem = new QListWidgetItem("📁 " + dir.fileName(), listWidget);
            dirItem->setData(Qt::UserRole, dir.fileName());
            dirItem->setData(Qt::UserRole + 1, "DIR");
        }

        QFileInfoList files = currentDir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            QListWidgetItem* fileItem = new QListWidgetItem("📄 " + file.fileName(), listWidget);
            fileItem->setData(Qt::UserRole, file.fileName());
            fileItem->setData(Qt::UserRole + 1, "FILE");
        }

        pathLabel->setText(currentDir.absolutePath());
    }

    void onSaveClicked() {
        QString rawName = nameEdit->text().trimmed();
        if (rawName.endsWith(".json", Qt::CaseInsensitive)) rawName.chop(5);
        if (rawName.isEmpty()) { nameEdit->setFocus(); return; }   // niente nome -> non salva

        QString finalPath = getSelectedPath();
        QFileInfo checkFile(finalPath);

        if (checkFile.exists()) {
            QPushButton* saveBtn = this->findChild<QPushButton*>("mobileSaveBtn");
            if (saveBtn && saveBtn->text() != "Overwrite?") {
                saveBtn->setText("Overwrite?");
                saveBtn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold; color: white; background-color: #d9534f; border-radius: 5px;");
                return;
            }
        }
        accept();
    }

    QDir currentDir;
    QLabel* pathLabel;
    QListWidget* listWidget;
    QLineEdit* nameEdit;
};
#endif

PresetSerializer::PresetSerializer(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

void PresetSerializer::saveSurface(const QString &suggestedPath)
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();

    QSettings settings("Repository");
    QSettings globalSettings;
    QString rootPath = globalSettings.value("libraryRootPath").toString();

    if (rootPath.isEmpty()) {
#if defined(Q_OS_ANDROID)
        // Su Android forziamo la cartella Download pubblica bypassando la sandbox di Qt
        rootPath = "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
#else
        rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#endif
    }

    QString fileName;
    if (!suggestedPath.isEmpty() && suggestedPath.endsWith(".json", Qt::CaseInsensitive)) {
        fileName = suggestedPath;
    } else {
        QString startPath = suggestedPath;

        if (startPath.isEmpty() || !QDir(startPath).exists()) {
            QTreeWidgetItem *selItem = m_mainWindow->getCurrentLibraryItem();
            if (selItem) {
                if (selItem->data(0, Qt::UserRole + 10).isValid()) {
                    startPath = selItem->data(0, Qt::UserRole + 10).toString(); // È una cartella
                } else {
                    startPath = QFileInfo(selItem->toolTip(0)).absolutePath(); // È un file
                }
            }
            if (startPath.isEmpty() || !QDir(startPath).exists()) {
                startPath = settings.value("lastFolder", rootPath + "/surfaces").toString();
            }
        }

        if (!startPath.endsWith(".json", Qt::CaseInsensitive)) {
            if (!startPath.endsWith("/")) startPath += "/";
            startPath += "NewSurface.json";
        }

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        MobileSaveDialog dialog("Save Surface", QFileInfo(startPath).absolutePath(), QFileInfo(startPath).completeBaseName(), m_mainWindow);
        if (dialog.exec() != QDialog::Accepted) {
            if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
            if (wasPath4D) m_mainWindow->pathTimer->start();
            if (wasPath3D) m_mainWindow->pathTimer3D->start();
            return;
        }
        fileName = dialog.getSelectedPath();
#else
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface", startPath, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
#endif
    }

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    // --- BLOCCO VALIDAZIONE RIGIDA ---
    QString absPath = QFileInfo(fileName).absolutePath() + "/";
    if (absPath.contains("/recordss/", Qt::CaseInsensitive) ||
        absPath.contains("/textures/", Qt::CaseInsensitive) ||
        absPath.contains("/sounds/", Qt::CaseInsensitive)) {
        QMessageBox::warning(m_mainWindow, "Salvataggio Bloccato",
                             "Operazione non consentita.\n\nLe Superfici statiche devono risiedere in 'Surfaces'.\nSe vuoi salvare la scena globale (che contiene questa superficie), usa il comando 'Save Motion'.");
        return;
    }

    QFileInfo fileInfo(fileName);
    settings.setValue("lastFolder", fileInfo.absolutePath());

    QJsonObject root;
    root["name"] = QFileInfo(fileName).baseName();
    root["type"] = "surface";

    // --- Salvataggio Ray Marching ---
    bool isImplicit = (m_mainWindow->ui->tabModeSelector->currentIndex() == 1);
    root["isImplicitMode"] = isImplicit;
    if (isImplicit) {
        root["implicitEquation"] = m_mainWindow->ui->lineEquation->toPlainText();
    }

    QString eqX = m_mainWindow->ui->lineX->toPlainText().trimmed();
    QString eqY = m_mainWindow->ui->lineY->toPlainText().trimmed();
    QString eqZ = m_mainWindow->ui->lineZ->toPlainText().trimmed();

    QString implicitEq = m_mainWindow->ui->lineEquation->toPlainText();

    // Controlliamo in modo inequivocabile chi comanda
    bool isImplicitScript = isImplicit && implicitEq.contains("// Controlled by Script");
    bool isParametricScript = !isImplicit && eqX.isEmpty() && eqY.isEmpty() && eqZ.isEmpty();
    // Script metrico: i campi x/y/z/p non sono vuoti (contengono la carta
    // identità), ma la geometria è definita dallo script: senza questo caso il
    // preset verrebbe salvato come superficie parametrica e lo script sparirebbe.
    bool isMetricScript = !isImplicit && !m_mainWindow->m_metricScriptBody.trimmed().isEmpty();

    if (isImplicitScript || isParametricScript || isMetricScript) {
        QString scriptContent = m_mainWindow->property("rawSurfaceScript").toString();
        // Fallback di sicurezza se la property è sfuggita
        if (scriptContent.isEmpty() && m_mainWindow->m_currentScriptMode == 0) {
            scriptContent = m_mainWindow->ui->txtScriptEditor->toPlainText();
        }

        if (!scriptContent.trimmed().isEmpty()) {
            root["scriptCode"] = scriptContent;
        }
        QJsonObject eq; eq["x"]=""; eq["y"]=""; eq["z"]=""; eq["p"]="";
        root["equations"] = eq;
    } else {
        QJsonObject equations;
        equations["x"] = m_mainWindow->ui->lineX->toPlainText();
        equations["y"] = m_mainWindow->ui->lineY->toPlainText();
        equations["z"] = m_mainWindow->ui->lineZ->toPlainText();
        equations["p"] = m_mainWindow->ui->lineP->toPlainText();
        equations["explicitW"] = m_mainWindow->ui->lineExplicitW->toPlainText();
        equations["explicitU"] = m_mainWindow->ui->lineExplicitU->toPlainText();
        equations["explicitV"] = m_mainWindow->ui->lineExplicitV->toPlainText();
        equations["defU"] = m_mainWindow->ui->lineU->toPlainText();
        equations["defV"] = m_mainWindow->ui->lineV->toPlainText();
        equations["defW"] = m_mainWindow->ui->lineW->toPlainText();
        root["equations"] = equations;
    }

    QJsonObject geo;
    geo["u0"] = m_mainWindow->ui->lnU->toPlainText();
    geo["v0"] = m_mainWindow->ui->lnV->toPlainText();
    geo["w0"] = m_mainWindow->ui->lnW->toPlainText();
    geo["du"] = m_mainWindow->ui->lndU->toPlainText();
    geo["dv"] = m_mainWindow->ui->lndV->toPlainText();
    geo["dw"] = m_mainWindow->ui->lndW->toPlainText();
    geo["conform"] = m_mainWindow->ui->lineConform->toPlainText();
    root["geodesic"] = geo;

    // Mappa di visualizzazione (embedding) di uno script metrico: i campi
    // x/y/z/p qui sopra sono stati salvati vuoti nel ramo script; se sono una
    // mappa custom (es. Flamm) la salviamo a parte per ripristinarla al load.
    m_mainWindow->writeMetricDisplayMap(root);

    QJsonObject constants;
    constants["A"] = m_mainWindow->ui->aSlider->value() / 100.0f;
    constants["B"] = m_mainWindow->ui->bSlider->value() / 100.0f;
    constants["C"] = m_mainWindow->ui->cSlider->value() / 100.0f;
    constants["D"] = m_mainWindow->ui->dSlider->value() / 100.0f;
    constants["E"] = m_mainWindow->ui->eSlider->value() / 100.0f;
    constants["F"] = m_mainWindow->ui->fSlider->value() / 100.0f;
    constants["S"] = m_mainWindow->ui->sSlider->value() / 100.0f;
    root["constants"] = constants;

    QJsonObject limits;
    limits["uMin"] = m_mainWindow->parseMath(m_mainWindow->ui->uMinEdit->text());
    limits["uMax"] = m_mainWindow->parseMath(m_mainWindow->ui->uMaxEdit->text());
    limits["vMin"] = m_mainWindow->parseMath(m_mainWindow->ui->vMinEdit->text());
    limits["vMax"] = m_mainWindow->parseMath(m_mainWindow->ui->vMaxEdit->text());
    limits["wMin"] = m_mainWindow->parseMath(m_mainWindow->ui->wMinEdit->text());
    limits["wMax"] = m_mainWindow->parseMath(m_mainWindow->ui->wMaxEdit->text());

    auto getSpaceLimit = [&](QLineEdit* edit, float defVal) {
        if (edit->text().trimmed().isEmpty()) return defVal;
        return m_mainWindow->parseMath(edit->text());
    };

    limits["xMin"] = getSpaceLimit(m_mainWindow->ui->lineXMin, -1000.0f);
    limits["xMax"] = getSpaceLimit(m_mainWindow->ui->lineXMax, 1000.0f);
    limits["yMin"] = getSpaceLimit(m_mainWindow->ui->lineYMin, -1000.0f);
    limits["yMax"] = getSpaceLimit(m_mainWindow->ui->lineYMax, 1000.0f);
    limits["zMin"] = getSpaceLimit(m_mainWindow->ui->lineZMin, -1000.0f);
    limits["zMax"] = getSpaceLimit(m_mainWindow->ui->lineZMax, 1000.0f);

    root["limits"] = limits;

    root["steps"] = m_mainWindow->ui->stepSlider->value();

    QJsonObject colors;
    colors["r"] = m_mainWindow->m_currentSurfaceColor.redF();
    colors["g"] = m_mainWindow->m_currentSurfaceColor.greenF();
    colors["b"] = m_mainWindow->m_currentSurfaceColor.blueF();
    // Trasparenza: senza questo, una superficie trasparente (es. ergosfera con
    // limite statico trasparente + orizzonte interno) si risalvava OPACA perche'
    // l'alpha non finiva mai nel JSON. Il reader la legge gia' da col["alpha"].
    colors["alpha"] = m_mainWindow->ui->alphaSlider->value() / 100.0;
    root["colors"] = colors;

    root["lightingMode"] = m_mainWindow->m_lightingMode4D;
    root["lightIntensity"] = m_mainWindow->ui->lightSlider->value() / 100.0;
    root["use4DLighting"] = m_mainWindow->ui->glWidget->is4DActive();
    if (isImplicit) {
        int shellState = m_mainWindow->ui->radioShell->isChecked() ? 10 : 0;
        root["renderMode"] = m_mainWindow->m_savedRenderMode + shellState;
    } else {
        root["renderMode"] = m_mainWindow->m_savedRenderMode;
    }
    root["projectionMode"] = (int)m_mainWindow->ui->glWidget->projectionMode;
    root["showBorder"] = m_mainWindow->ui->btnBorder->isChecked();

    // 1. Salva Rotazione 4D
    QJsonObject angles;
    angles["omega"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getOmega();
    angles["phi"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getPhi();
    angles["psi"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getPsi();
    root["angles"] = angles;

    // 2. Salva Telecamera 3D
    if (m_mainWindow->ui->glWidget) {
        QJsonObject camera3D;
        QVector3D camPos = m_mainWindow->ui->glWidget->getCameraPos();
        camera3D["x"] = (double)camPos.x();
        camera3D["y"] = (double)camPos.y();
        camera3D["z"] = (double)camPos.z();

        QQuaternion rot = m_mainWindow->ui->glWidget->getRotationQuat();
        camera3D["rot_w"] = (double)rot.scalar();
        camera3D["rot_x"] = (double)rot.x();
        camera3D["rot_y"] = (double)rot.y();
        camera3D["rot_z"] = (double)rot.z();

        camera3D["yaw"] = (double)m_mainWindow->ui->glWidget->getCameraYaw();
        camera3D["pitch"] = (double)m_mainWindow->ui->glWidget->getCameraPitch();
        camera3D["roll"] = (double)m_mainWindow->ui->glWidget->getCameraRoll();

        root["camera3D"] = camera3D;
    }

    QDir().mkpath(QFileInfo(fileName).absolutePath()); // 1. Crea la cartella se manca
    QFile file(fileName);

    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove(); // 2. Distrugge il file lucchettato da iOS per poterlo ricreare
    }

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(fileName);
    }

    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        QTimer::singleShot(100, m_mainWindow, [this, fileName]() {
            m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeSurfaces, fileName);
        });
    } else {
        QMessageBox::critical(m_mainWindow, "Error", "Could not write to file.");
    }
}

void PresetSerializer::saveTexture(const QString &path)
{
    QString absPath = QFileInfo(path).absolutePath() + "/";
    if (absPath.contains("/surfaces/", Qt::CaseInsensitive) ||
        absPath.contains("/records/", Qt::CaseInsensitive) ||
        absPath.contains("/sounds/", Qt::CaseInsensitive)) {
        QMessageBox::warning(m_mainWindow, "Salvataggio Bloccato",
                             "Operazione non consentita.\n\nI preset Texture devono essere salvati esclusivamente nella cartella 'Textures'.");
        return;
    }

    QJsonObject root;

    QString currentCode;
    bool isBg = m_mainWindow->ui->radioBackground->isChecked();
    bool isImplicit = (m_mainWindow->ui->tabModeSelector->currentIndex() == 1);

    if (isImplicit && !isBg) {
        // Se siamo in Ray Marching, leggi il codice direttamente dal box dell'interfaccia! +++
        currentCode = m_mainWindow->ui->lineTexture->toPlainText();
    } else {
        // [Logica originale per le superfici parametriche]
        if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeTexture) {
            currentCode = m_mainWindow->ui->txtScriptEditor->toPlainText();
            if (isBg) m_mainWindow->m_bgTextureCode = currentCode;
            else m_mainWindow->m_surfaceTextureCode = currentCode;
        } else {
            currentCode = isBg ? m_mainWindow->m_bgTextureCode : m_mainWindow->m_surfaceTextureCode;
        }
    }

    if (m_mainWindow->m_isImageMode && !m_mainWindow->m_currentTexturePath.isEmpty()) {
        // Usiamo una RegEx per rimuovere l'intera linea //IMG: ovunque si trovi,
        // senza cancellare il codice procedurale che la segue.
        QRegularExpression imgRe(R"(^\s*//IMG:.*$\n?)", QRegularExpression::MultilineOption);
        currentCode.remove(imgRe);

        // Riappendiamo il tag in cima al codice in modo pulito
        currentCode = "//IMG:" + m_mainWindow->m_currentTexturePath + "\n" + currentCode.trimmed();
    }

    if (isImplicit) {
        // Se siamo in Ray Marching salviamo entrambi i campi
        currentCode = m_mainWindow->ui->lineTexture->toPlainText();
        root["displacement"] = m_mainWindow->ui->lineVariations->toPlainText();
        root["isImplicitMode"] = true; // Flag fondamentale per il caricamento
    } else {
        // Logica Parametrica
        bool isBg = m_mainWindow->ui->radioBackground->isChecked();
        currentCode = isBg ? m_mainWindow->m_bgTextureCode : m_mainWindow->m_surfaceTextureCode;
        root["isImplicitMode"] = false;
    }

    if (currentCode.trimmed().isEmpty()) currentCode = "// Texture Preset";
    root["code"] = currentCode;

    if (m_mainWindow->ui->glWidget) {
        QVector2D pan = m_mainWindow->ui->glWidget->getFlatPan();
        root["pan_x"] = (double)pan.x();
        root["pan_y"] = (double)pan.y();
        root["zoom"] = (double)m_mainWindow->ui->glWidget->getFlatZoom();
        root["rotation"] = (double)m_mainWindow->ui->glWidget->getFlatRotation();
        root["hasCustomColors"] = true;
        root["color1"] = m_mainWindow->m_texColor1.name();
        root["color2"] = m_mainWindow->m_texColor2.name();
    }
    root["type"] = "custom_texture";
    root["name"] = QFileInfo(path).baseName();

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(path);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove();
    }
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        m_mainWindow->m_currentTexturePresetPath = path;

        // Refresh visivo + selezione del file appena salvato nella libreria
        QTimer::singleShot(100, m_mainWindow, [this, path]() {
            m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeTextures, path);
        });
    }
}

void PresetSerializer::saveMotion(const QString &suggestedPath)
{
    bool wasRotating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();
    bool wasTimeAnimating = false;

    if (m_mainWindow->m_btnStart && m_mainWindow->m_btnStart->text().toUpper() == "STOP") {
        wasTimeAnimating = true;
        m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
        m_mainWindow->ui->glWidget->stopAnimationTimer();
    }

    if (wasRotating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();

    QSettings settings;
    QString lastDir = settings.value("lastMotionDir", settings.value("lastFolder", QDir::homePath()).toString()).toString();

    QString fileName;
    if (!suggestedPath.isEmpty() && suggestedPath.endsWith(".json", Qt::CaseInsensitive)) {
        fileName = suggestedPath;
    } else {
        QString startPath = suggestedPath;

        if (startPath.isEmpty() || !QDir(startPath).exists()) {
            QTreeWidgetItem *selItem = m_mainWindow->getCurrentLibraryItem();
            if (selItem) {
                if (selItem->data(0, Qt::UserRole + 10).isValid()) {
                    startPath = selItem->data(0, Qt::UserRole + 10).toString(); // È una cartella
                } else {
                    startPath = QFileInfo(selItem->toolTip(0)).absolutePath(); // È un file
                }
            }
            if (startPath.isEmpty() || !QDir(startPath).exists()) {
                startPath = lastDir;
            }
        }

        if (!startPath.endsWith(".json", Qt::CaseInsensitive)) {
            if (!startPath.endsWith("/")) startPath += "/";
            startPath += "NewMotion.json";
        }

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        MobileSaveDialog dialog("Save Record", QFileInfo(startPath).absolutePath(), QFileInfo(startPath).completeBaseName(), m_mainWindow);
        if (dialog.exec() != QDialog::Accepted) {
            if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
            if (wasPath4D) m_mainWindow->pathTimer->start();
            if (wasPath3D) m_mainWindow->pathTimer3D->start();
            if (wasTimeAnimating) {
                m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
                m_mainWindow->ui->glWidget->startAnimationTimer();
            }
            return;
        }
        fileName = dialog.getSelectedPath();
#else
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save File", startPath, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
#endif
    }

    if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();
    if (wasTimeAnimating) {
        m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
        m_mainWindow->ui->glWidget->startAnimationTimer();
    }

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    // --- BLOCCO VALIDAZIONE RIGIDA ---
    QString absPath = QFileInfo(fileName).absolutePath() + "/";
    if (absPath.contains("/surfaces/", Qt::CaseInsensitive) ||
        absPath.contains("/textures/", Qt::CaseInsensitive) ||
        absPath.contains("/sounds/", Qt::CaseInsensitive)) {
        QMessageBox::warning(m_mainWindow, "Salvataggio Bloccato",
                             "Operazione non consentita.\n\nI preset Motion catturano l'intera scena (superficie compresa) e devono essere salvati esclusivamente nella cartella 'Motions'.");
        return;
    }

    QString saveFolder = QFileInfo(fileName).absolutePath();
    settings.setValue("lastMotionDir", saveFolder);

    QJsonObject root;
    root["type"] = "motion";
    root["name"] = QFileInfo(fileName).baseName();

    // --- Salvataggio Ray Marching ---
    bool isImplicit = (m_mainWindow->ui->tabModeSelector->currentIndex() == 1);
    root["isImplicitMode"] = isImplicit;
    if (isImplicit) {
        root["implicitEquation"] = m_mainWindow->ui->lineEquation->toPlainText();
    }

    QJsonObject equations;
    equations["x"] = m_mainWindow->ui->lineX->toPlainText();
    equations["y"] = m_mainWindow->ui->lineY->toPlainText();
    equations["z"] = m_mainWindow->ui->lineZ->toPlainText();
    equations["p"] = m_mainWindow->ui->lineP->toPlainText();
    equations["explicitU"] = m_mainWindow->ui->lineExplicitU->toPlainText();
    equations["explicitV"] = m_mainWindow->ui->lineExplicitV->toPlainText();
    equations["explicitW"] = m_mainWindow->ui->lineExplicitW->toPlainText();
    equations["defU"] = m_mainWindow->ui->lineU->toPlainText();
    equations["defV"] = m_mainWindow->ui->lineV->toPlainText();
    equations["defW"] = m_mainWindow->ui->lineW->toPlainText();
    root["equations"] = equations;

    QJsonObject geo;
    geo["u0"] = m_mainWindow->ui->lnU->toPlainText();
    geo["v0"] = m_mainWindow->ui->lnV->toPlainText();
    geo["w0"] = m_mainWindow->ui->lnW->toPlainText();
    geo["du"] = m_mainWindow->ui->lndU->toPlainText();
    geo["dv"] = m_mainWindow->ui->lndV->toPlainText();
    geo["dw"] = m_mainWindow->ui->lndW->toPlainText();
    geo["conform"] = m_mainWindow->ui->lineConform->toPlainText();
    root["geodesic"] = geo;

    bool usingEquations = !m_mainWindow->ui->lineX->toPlainText().trimmed().isEmpty() &&
                          m_mainWindow->ui->lineX->toPlainText().trimmed() != "0";

    if (isImplicit) {
        usingEquations = !m_mainWindow->ui->lineEquation->toPlainText().contains("// Controlled by Script");
    }

    QString scriptContent = m_mainWindow->property("rawSurfaceScript").toString();
    if (scriptContent.isEmpty() && m_mainWindow->m_currentScriptMode == 0) {
        scriptContent = m_mainWindow->ui->txtScriptEditor->toPlainText();
    }

    // In modalità metrica usingEquations è vero (carta identità nei campi),
    // ma lo script resta la sorgente della geometria e va salvato.
    bool metricScriptActive = !m_mainWindow->m_metricScriptBody.trimmed().isEmpty();
    if (!scriptContent.trimmed().isEmpty() && (!usingEquations || metricScriptActive)) {
        root["scriptCode"] = scriptContent;
    }

    QJsonObject constants;
    constants["A"] = m_mainWindow->ui->aSlider->value() / 100.0f;
    constants["B"] = m_mainWindow->ui->bSlider->value() / 100.0f;
    constants["C"] = m_mainWindow->ui->cSlider->value() / 100.0f;
    constants["D"] = m_mainWindow->ui->dSlider->value() / 100.0f;
    constants["E"] = m_mainWindow->ui->eSlider->value() / 100.0f;
    constants["F"] = m_mainWindow->ui->fSlider->value() / 100.0f;
    constants["S"] = m_mainWindow->ui->sSlider->value() / 100.0f;
    root["constants"] = constants;

    QJsonObject limits;
    limits["uMin"] = m_mainWindow->parseMath(m_mainWindow->ui->uMinEdit->text());
    limits["uMax"] = m_mainWindow->parseMath(m_mainWindow->ui->uMaxEdit->text());
    limits["vMin"] = m_mainWindow->parseMath(m_mainWindow->ui->vMinEdit->text());
    limits["vMax"] = m_mainWindow->parseMath(m_mainWindow->ui->vMaxEdit->text());
    limits["wMin"] = m_mainWindow->parseMath(m_mainWindow->ui->wMinEdit->text());
    limits["wMax"] = m_mainWindow->parseMath(m_mainWindow->ui->wMaxEdit->text());

    auto getSpaceLimit = [&](QLineEdit* edit, float defVal) {
        if (edit->text().trimmed().isEmpty()) return defVal;
        return m_mainWindow->parseMath(edit->text());
    };

    limits["xMin"] = getSpaceLimit(m_mainWindow->ui->lineXMin, -1000.0f);
    limits["xMax"] = getSpaceLimit(m_mainWindow->ui->lineXMax, 1000.0f);
    limits["yMin"] = getSpaceLimit(m_mainWindow->ui->lineYMin, -1000.0f);
    limits["yMax"] = getSpaceLimit(m_mainWindow->ui->lineYMax, 1000.0f);
    limits["zMin"] = getSpaceLimit(m_mainWindow->ui->lineZMin, -1000.0f);
    limits["zMax"] = getSpaceLimit(m_mainWindow->ui->lineZMax, 1000.0f);

    root["limits"] = limits;

    root["steps"] = m_mainWindow->ui->stepSlider->value();

    QJsonObject colors;
    colors["surfColor"] = m_mainWindow->m_currentSurfaceColor.name();
    colors["bordColor"] = m_mainWindow->m_currentBorderColor.name();
    colors["alpha"] = m_mainWindow->ui->alphaSlider->value() / 100.0;
    root["colors"] = colors;

    QJsonObject path4D;
    path4D["x"] = m_mainWindow->ui->lineX_P->text();
    path4D["y"] = m_mainWindow->ui->lineY_P->text();
    path4D["z"] = m_mainWindow->ui->lineZ_P->text();
    path4D["w"] = m_mainWindow->ui->lineP_P->text();
    path4D["alpha"] = m_mainWindow->ui->lineAlpha_P->text();
    path4D["beta"]  = m_mainWindow->ui->lineBeta_P->text();
    path4D["gamma"] = m_mainWindow->ui->lineGamma_P->text();
    root["path4D"] = path4D;

    QJsonObject path3D;
    path3D["x"] = m_mainWindow->ui->lineX_P3D->text();
    path3D["y"] = m_mainWindow->ui->lineY_P3D->text();
    path3D["z"] = m_mainWindow->ui->lineZ_P3D->text();
    path3D["roll"] = m_mainWindow->ui->lineR_P3D->text();
    root["path3D"] = path3D;
    root["pathMode"] = static_cast<int>(m_mainWindow->m_pathMode);

    bool isLookingAtBackground = m_mainWindow->ui->radioBackground->isChecked();

    if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeTexture) {
        QString currentEditorText = m_mainWindow->ui->txtScriptEditor->toPlainText();
        if (isLookingAtBackground) {
            m_mainWindow->m_bgTextureCode = currentEditorText;
            m_mainWindow->m_bgTextureScriptText = currentEditorText;
        } else {
            m_mainWindow->m_surfaceTextureCode = currentEditorText;
            m_mainWindow->m_surfaceTextureScriptText = currentEditorText;
        }
    }

    QJsonObject texture;
    bool texEnabled = isLookingAtBackground ? m_mainWindow->m_surfaceTextureState : m_mainWindow->ui->chkBoxTexture->isChecked();
    texture["enabled"] = texEnabled;

    if (m_mainWindow->ui->glWidget) {
        m_mainWindow->ui->glWidget->setFlatViewTarget(0);
        texture["zoom"] = (double)m_mainWindow->ui->glWidget->getFlatZoom();
        QVector2D pan = m_mainWindow->ui->glWidget->getFlatPan();
        texture["pan_x"] = (double)pan.x();
        texture["pan_y"] = (double)pan.y();
        texture["rotation"] = (double)m_mainWindow->ui->glWidget->getFlatRotation();
    }
    texture["col1"] = m_mainWindow->m_texColor1.name();
    texture["col2"] = m_mainWindow->m_texColor2.name();

    // --- REINIEZIONE DELL'AUDIO E GESTIONE IMMAGINI ---
    QString audioCode = m_mainWindow->m_soundScriptText.trimmed();

    if (isImplicit) {
        QString implicitTex = m_mainWindow->ui->lineTexture->toPlainText().trimmed();

        // Puliamo eventuali vecchi tag e riaggiungiamo l'audio pulito
        QRegularExpression blockRe(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        while (implicitTex.contains(blockRe)) implicitTex.remove(blockRe);
        implicitTex.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
        implicitTex.remove(QRegularExpression(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));

        if (!audioCode.isEmpty()) {
            implicitTex = audioCode + "\n\n" + implicitTex.trimmed();
        }
        texture["code"] = implicitTex;
        texture["displacement"] = m_mainWindow->ui->lineVariations->toPlainText();
    } else {
        QString codeToSave = m_mainWindow->m_surfaceTextureCode;

        // Pulizia vecchi tag audio robusta
        QRegularExpression blockRe(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        while (codeToSave.contains(blockRe)) codeToSave.remove(blockRe);
        codeToSave.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
        codeToSave.remove(QRegularExpression(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
        codeToSave = codeToSave.trimmed();

        // Se c'è un'immagine, il tag //IMG: deve stare sempre alla riga 1!
        if (texEnabled && m_mainWindow->m_isImageMode && !m_mainWindow->m_currentTexturePath.isEmpty()) {
            QRegularExpression imgRe(R"(^\s*//IMG:.*$\n?)", QRegularExpression::MultilineOption);
            codeToSave.remove(imgRe);

            QString newCode = "//IMG:" + m_mainWindow->m_currentTexturePath + "\n";
            if (!audioCode.isEmpty()) newCode += audioCode + "\n\n";
            newCode += codeToSave;
            codeToSave = newCode;
        } else if (!audioCode.isEmpty()) {
            codeToSave = audioCode + "\n\n" + codeToSave;
        }

        texture["code"] = codeToSave.trimmed();
    }
    root["texture"] = texture;

    QJsonObject speeds;
    speeds["nutation"] = (double)m_mainWindow->ui->glWidget->getNutationSpeed();
    speeds["precession"] = (double)m_mainWindow->ui->glWidget->getPrecessionSpeed();
    speeds["spin"] = (double)m_mainWindow->ui->glWidget->getSpinSpeed();
    speeds["omega"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getOmegaSpeed();
    speeds["phi"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getPhiSpeed();
    speeds["psi"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getPsiSpeed();
    speeds["path3D"] = m_mainWindow->ui->speed3DSlider->value();
    speeds["path4D"] = isImplicit ? 0 : m_mainWindow->ui->speed4DSlider->value();
    root["speeds"] = speeds;

    QJsonObject angles;
    angles["omega"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getOmega();
    angles["phi"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getPhi();
    angles["psi"] = isImplicit ? 0.0 : (double)m_mainWindow->ui->glWidget->getPsi();
    root["angles"] = angles;

    if (m_mainWindow->ui->glWidget) {
        QJsonObject camera3D;
        QVector3D camPos = m_mainWindow->ui->glWidget->getCameraPos();
        camera3D["x"] = (double)camPos.x();
        camera3D["y"] = (double)camPos.y();
        camera3D["z"] = (double)camPos.z();

        QQuaternion rot = m_mainWindow->ui->glWidget->getRotationQuat();
        camera3D["rot_w"] = (double)rot.scalar();
        camera3D["rot_x"] = (double)rot.x();
        camera3D["rot_y"] = (double)rot.y();
        camera3D["rot_z"] = (double)rot.z();

        camera3D["yaw"] = (double)m_mainWindow->ui->glWidget->getCameraYaw();
        camera3D["pitch"] = (double)m_mainWindow->ui->glWidget->getCameraPitch();
        camera3D["roll"] = (double)m_mainWindow->ui->glWidget->getCameraRoll();

        root["camera3D"] = camera3D;
        root["observer4D"] = (double)m_mainWindow->ui->glWidget->getObserverPos4D();
    }

    QJsonObject background;
    background["color"] = m_mainWindow->m_currentBackgroundColor.name();
    background["enabled"] = m_mainWindow->ui->glWidget->isBackgroundTextureEnabled();
    background["code"] = m_mainWindow->m_bgTextureCode;
    background["col1"] = m_mainWindow->m_bgTexColor1.name();
    background["col2"] = m_mainWindow->m_bgTexColor2.name();

    if (m_mainWindow->ui->glWidget) {
        m_mainWindow->ui->glWidget->setFlatViewTarget(1);
        background["zoom"] = (double)m_mainWindow->ui->glWidget->getFlatZoom();
        QVector2D bgPan = m_mainWindow->ui->glWidget->getFlatPan();
        background["pan_x"] = (double)bgPan.x();
        background["pan_y"] = (double)bgPan.y();
        background["rotation"] = (double)m_mainWindow->ui->glWidget->getFlatRotation();
        m_mainWindow->ui->glWidget->setFlatViewTarget(isLookingAtBackground ? 1 : 0);
    }

    root["background"] = background;
    root["lightingMode"] = m_mainWindow->m_lightingMode4D;
    root["lightIntensity"] = m_mainWindow->ui->lightSlider->value() / 100.0;
    root["use4DLighting"] = m_mainWindow->ui->glWidget->is4DActive();
    if (isImplicit) {
        int shellState = m_mainWindow->ui->radioShell->isChecked() ? 10 : 0;
        root["renderMode"] = m_mainWindow->m_savedRenderMode + shellState;
    } else {
        root["renderMode"] = m_mainWindow->m_savedRenderMode;
    }
    root["projectionMode"] = m_mainWindow->ui->glWidget->projectionMode;
    root["showBorder"] = m_mainWindow->ui->btnBorder->isChecked();

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(fileName);
    }

    QDir().mkpath(QFileInfo(fileName).absolutePath());
    QFile file(fileName);
    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove();
    }
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        QTimer::singleShot(100, m_mainWindow, [this, fileName]() {
            m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeMotions, fileName);
        });
    }
}

void PresetSerializer::saveScript()
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();

    auto resumeTimers = [&]() {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
    };

    QString content = m_mainWindow->ui->txtScriptEditor->toPlainText();
    if (content.trimmed().isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Warning", "Editor is empty.");
        resumeTimers();
        return;
    }

    bool isSurface = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSurface);
    bool isSound   = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound);

    QSettings settings;
    QString settingsKey = isSurface ? "lastFolder" : (isSound ? "lastSoundDir" : "lastCustomTexDir");
    QString currentMem = settings.value(settingsKey).toString();
    QString rootPath = settings.value("libraryRootPath").toString();

    if (currentMem.isEmpty() || currentMem.contains("build", Qt::CaseInsensitive) || !QDir(currentMem).exists()) {
        if (isSurface) currentMem = settings.value("pathSurfaces", rootPath + "/surfaces").toString();
        else if (isSound) currentMem = settings.value("pathSounds", rootPath + "/sounds").toString();
        else currentMem = settings.value("pathTextures", rootPath + "/textures").toString();
    }

    QString fileName;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString title = isSurface ? "Save Surface Script" : (isSound ? "Save Sound Script" : "Save Texture Script");
    // Nome di default: per le texture il nome di quella caricata, altrimenti vuoto (nuovo).
    QString defaultName;
    if (!isSurface && !isSound && !m_mainWindow->m_currentTexturePath.isEmpty())
        defaultName = QFileInfo(m_mainWindow->m_currentTexturePath).completeBaseName();
    MobileSaveDialog dialog(title, currentMem, defaultName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        resumeTimers();
        return;
    }
    fileName = dialog.getSelectedPath();
#else
    QFileDialog::Options options = QFileDialog::DontUseNativeDialog;
    if (isSurface) fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface Script", currentMem, "Surface Script (*.json)", nullptr, options);
    else if (isSound) fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Sound Script", currentMem, "Sound Script (*.json)", nullptr, options);
    else fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Texture Script", currentMem, "Texture Script (*.json)", nullptr, options);
#endif

    resumeTimers();

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    // Conferma sovrascrittura: il dialog è aperto con DontUseNativeDialog (e su
    // mobile con MobileSaveDialog), che NON mostrano il prompt nativo di
    // sovrascrittura. Senza questo controllo saveScript rimpiazzava un file
    // esistente in silenzio (il backup di backupBeforeOverwrite è muto). Chiediamo
    // conferma esplicita, così l'utente non perde un preset per sbaglio.
    if (QFile::exists(fileName)) {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            m_mainWindow, "Sovrascrivere il file?",
            QString("Il file \"%1\" esiste già.\n\nVuoi sovrascriverlo?")
                .arg(QFileInfo(fileName).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) return;
    }

    QJsonObject root;

    // Aggiungiamo il nome estratto dal percorso
    root["name"] = QFileInfo(fileName).baseName();

    if (isSurface) {
        root["type"] = "surface";
        root["scriptCode"] = content;

        root["isScript"] = true;
        root["isImplicitMode"] = (m_mainWindow->ui->tabModeSelector->currentIndex() == 1);

        root["steps"] = m_mainWindow->ui->stepSlider->value();

        QJsonObject limits;
        limits["uMin"] = m_mainWindow->parseMath(m_mainWindow->ui->uMinEdit->text());
        limits["uMax"] = m_mainWindow->parseMath(m_mainWindow->ui->uMaxEdit->text());
        limits["vMin"] = m_mainWindow->parseMath(m_mainWindow->ui->vMinEdit->text());
        limits["vMax"] = m_mainWindow->parseMath(m_mainWindow->ui->vMaxEdit->text());
        limits["wMin"] = m_mainWindow->parseMath(m_mainWindow->ui->wMinEdit->text());
        limits["wMax"] = m_mainWindow->parseMath(m_mainWindow->ui->wMaxEdit->text());

        // Helper per i limiti di spazio (come in saveSurface)
        auto getSpaceLimit = [&](QLineEdit* edit, float defVal) {
            if (edit->text().trimmed().isEmpty()) return defVal;
            return m_mainWindow->parseMath(edit->text());
        };

        limits["xMin"] = getSpaceLimit(m_mainWindow->ui->lineXMin, -1000.0f);
        limits["xMax"] = getSpaceLimit(m_mainWindow->ui->lineXMax, 1000.0f);
        limits["yMin"] = getSpaceLimit(m_mainWindow->ui->lineYMin, -1000.0f);
        limits["yMax"] = getSpaceLimit(m_mainWindow->ui->lineYMax, 1000.0f);
        limits["zMin"] = getSpaceLimit(m_mainWindow->ui->lineZMin, -1000.0f);
        limits["zMax"] = getSpaceLimit(m_mainWindow->ui->lineZMax, 1000.0f);

        root["limits"] = limits;

        QJsonObject constants;
        constants["A"] = m_mainWindow->ui->aSlider->value() / 100.0f;
        constants["B"] = m_mainWindow->ui->bSlider->value() / 100.0f;
        constants["C"] = m_mainWindow->ui->cSlider->value() / 100.0f;
        constants["D"] = m_mainWindow->ui->dSlider->value() / 100.0f;
        constants["E"] = m_mainWindow->ui->eSlider->value() / 100.0f;
        constants["F"] = m_mainWindow->ui->fSlider->value() / 100.0f;
        constants["S"] = m_mainWindow->ui->sSlider->value() / 100.0f;
        root["constants"] = constants;

        // Condizioni iniziali del flusso geodetico: servono agli script metrici
        // che non le dichiarano con le direttive U:=/dU:=/...
        QJsonObject geo;
        geo["u0"] = m_mainWindow->ui->lnU->toPlainText();
        geo["v0"] = m_mainWindow->ui->lnV->toPlainText();
        geo["w0"] = m_mainWindow->ui->lnW->toPlainText();
        geo["du"] = m_mainWindow->ui->lndU->toPlainText();
        geo["dv"] = m_mainWindow->ui->lndV->toPlainText();
        geo["dw"] = m_mainWindow->ui->lndW->toPlainText();
        geo["conform"] = m_mainWindow->ui->lineConform->toPlainText();
        root["geodesic"] = geo;

        // Mappa di visualizzazione (embedding) di uno script metrico: se la mappa
        // x/y/z/p è custom (non la carta identità, es. paraboloide di Flamm) va
        // salvata, altrimenti al ricaricamento torna l'identità.
        m_mainWindow->writeMetricDisplayMap(root);
    }
    else if (isSound) {
        root["code"] = content;
        root["type"] = "sound";
    }
    else {
        root["code"] = content;
        root["type"] = "custom_texture";

        if (m_mainWindow->ui->glWidget) {
            QVector2D pan = m_mainWindow->ui->glWidget->getFlatPan();
            root["zoom"] = (double)m_mainWindow->ui->glWidget->getFlatZoom();
            root["pan_x"] = (double)pan.x();
            root["pan_y"] = (double)pan.y();
            root["rotation"] = (double)m_mainWindow->ui->glWidget->getFlatRotation();
            root["hasCustomColors"] = true;
            root["color1"] = m_mainWindow->m_texColor1.name();
            root["color2"] = m_mainWindow->m_texColor2.name();
        }
    }

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(fileName);
    }

    QDir().mkpath(QFileInfo(fileName).absolutePath());
    QFile file(fileName);
    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
        file.remove();
    }
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        // Ricreiamo le chiavi di salvataggio per evitare errori di compilazione
        bool isSurf = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSurface);
        bool isSnd  = (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound);
        QString safeSettingsKey = isSurf ? "lastFolder" : (isSnd ? "lastSoundDir" : "lastCustomTexDir");

        QSettings().setValue(safeSettingsKey, QFileInfo(fileName).absolutePath());

        if (!isSurf && !isSnd) {
            m_mainWindow->m_currentTexturePresetPath = fileName;
        }

        QTreeWidget *targetTree = isSurf ? m_mainWindow->ui->treeSurfaces
                                : isSnd  ? m_mainWindow->ui->treeSounds
                                         : m_mainWindow->ui->treeTextures;
        QTimer::singleShot(100, m_mainWindow, [this, targetTree, fileName]() {
            m_mainWindow->refreshAndSelectPreset(targetTree, fileName);
        });

    } else {
        QMessageBox::critical(m_mainWindow, "Error", "Could not write to file.");
    }
}

void PresetSerializer::saveSound(const QString &filePath)
{
    if (filePath.isEmpty()) return;

    // 1. Se è un file audio multimediale (mp3, wav, ogg), non facciamo nulla.
    // L'app non altera i byte di un file audio originale. (Per copiarlo si usa Save As).
    if (filePath.endsWith(".mp3", Qt::CaseInsensitive) ||
        filePath.endsWith(".wav", Qt::CaseInsensitive) ||
        filePath.endsWith(".ogg", Qt::CaseInsensitive)) {
        return;
    }

    // 2. È uno script JSON. Procediamo al salvataggio silenzioso.
    QString finalPath = filePath;
    if (!finalPath.endsWith(".json", Qt::CaseInsensitive)) {
        finalPath += ".json";
    }

    // Backup di sicurezza prima di sovrascrivere (se implementato)
    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(finalPath);
    }

    // 3. Recuperiamo il contenuto aggiornato dello script
    QString content = m_mainWindow->m_soundScriptText;
    if (m_mainWindow->m_currentScriptMode == 2) { // 2 = ScriptModeSound
        content = m_mainWindow->ui->txtScriptEditor->toPlainText();
    }

    // 4. Creiamo la struttura JSON
    QJsonObject root;
    root["code"] = content;
    root["type"] = "sound";
    root["name"] = QFileInfo(finalPath).baseName();

    // 5. Scrittura fisica su disco (sovrascrittura brutale e silenziosa)
    QDir().mkpath(QFileInfo(finalPath).absolutePath());
    QFile outFile(finalPath);

    if (outFile.exists()) {
        outFile.setPermissions(QFile::WriteOwner);
        outFile.remove();
    }

    if (outFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        outFile.write(doc.toJson());
        outFile.close();
    }

    // 6. Aggiorniamo la UI per riflettere eventuali modifiche (es. orario di ultima modifica)
    QTimer::singleShot(100, m_mainWindow, [this, finalPath]() {
        m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeSounds, finalPath);
    });
}

void PresetSerializer::saveTextureAs(const QString &startDir, const QString &sourceFilePath)
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();
    // Nome di default: quello della texture caricata, vuoto se è una texture nuova.
    QString defaultName = sourceFilePath.isEmpty()
                              ? QString()
                              : QFileInfo(sourceFilePath).completeBaseName();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    MobileSaveDialog dialog("Save Texture As...", startDir, defaultName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();
#else
    QString defaultSelection = startDir + "/";
    if (!defaultName.isEmpty()) defaultSelection += defaultName + ".json";
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Texture As...", defaultSelection, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
#endif

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastCustomTexDir", QFileInfo(savePath).absolutePath());

    saveTexture(savePath);
}

void PresetSerializer::saveSurfaceAs(const QString &startDir, const QString &sourceFilePath)
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();

    QString defaultSelection = startDir + "/NewSurface.json";
    if (!sourceFilePath.isEmpty()) defaultSelection = startDir + "/" + QFileInfo(sourceFilePath).fileName();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    MobileSaveDialog dialog("Save Surface As...", startDir, baseName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();
#else
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface As...", defaultSelection, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
#endif

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastFolder", QFileInfo(savePath).absolutePath());

    saveSurface(savePath);
}

void PresetSerializer::saveSoundAs(const QString &startDir, const QString &sourceFilePath)
{
    QString defaultSelection = startDir + "/NewSound.json";
    QString filter = "JSON Files (*.json)";
    bool isMediaFile = false;
    QString expectedExt = "json"; // Inizializza con json

    // 1. Capisce se stiamo clonando un file audio reale (MP3/WAV/OGG)
    if (!sourceFilePath.isEmpty()) {
        defaultSelection = startDir + "/" + QFileInfo(sourceFilePath).fileName();
        if (sourceFilePath.endsWith(".mp3", Qt::CaseInsensitive) ||
            sourceFilePath.endsWith(".wav", Qt::CaseInsensitive) ||
            sourceFilePath.endsWith(".ogg", Qt::CaseInsensitive)) {

            isMediaFile = true;
            expectedExt = QFileInfo(sourceFilePath).suffix().toLower();
            // Aggiorna il filtro dinamicamente (Es: "MP3 Files (*.mp3)")
            filter = QString("%1 Files (*.%2)").arg(expectedExt.toUpper(), expectedExt);
        }
    }

    // ==============================================================
    // Protezione contro i congelamenti UI
    // ==============================================================
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();

    // ==============================================================
    // Apertura finestra di salvataggio
    // ==============================================================
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    MobileSaveDialog dialog("Save Sound As...", startDir, baseName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();

    // FIX MOBILE (Android & iOS): Se è un file media, assicuriamoci che getSelectedPath non abbia
    // forzato ".json" per errore (la nostra classe aggiunge sempre .json di default).
    if (isMediaFile && savePath.endsWith(".json", Qt::CaseInsensitive)) {
        savePath.chop(5); // Rimuove ".json"
    }

#else
    // FIX DESKTOP: Usa la variabile "filter" calcolata sopra!
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Sound As...", defaultSelection, filter, nullptr, QFileDialog::DontUseNativeDialog);
#endif

    // Riprendiamo i timer dopo la chiusura della finestra
    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();
    // ==============================================================

    if (savePath.isEmpty()) return;

    // 2. Forza l'estensione corretta (che sia json o mp3/wav)
    if (!savePath.endsWith("." + expectedExt, Qt::CaseInsensitive)) {
        savePath += "." + expectedExt;
    }

    QSettings settings;
    settings.setValue("lastSoundDir", QFileInfo(savePath).absolutePath());

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(savePath);
    }

    // --- SALVATAGGIO FISICO ---
    if (isMediaFile) {
        // Copia fisica del file multimediale
        if (savePath != sourceFilePath) {
            if (QFile::exists(savePath)) QFile::remove(savePath);
            if (QFile::copy(sourceFilePath, savePath)) {
                QFile::setPermissions(savePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
            }
        }
    } else if (!sourceFilePath.isEmpty()) {
        // Clonazione file JSON esistente
        QFile inFile(sourceFilePath);
        if (inFile.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
            inFile.close();
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                root["name"] = QFileInfo(savePath).baseName();
                QDir().mkpath(QFileInfo(savePath).absolutePath());

                QFile outFile(savePath);
                if (outFile.exists()) { outFile.setPermissions(QFile::WriteOwner); outFile.remove(); }
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(QJsonDocument(root).toJson());
                    outFile.close();
                }
            }
        }
    } else {
        // Creazione nuovo script audio (JSON) da zero
        QString content = m_mainWindow->m_soundScriptText;
        if (m_mainWindow->m_currentScriptMode == 2) { // 2 = ScriptModeSound
            content = m_mainWindow->ui->txtScriptEditor->toPlainText();
        }

        QJsonObject root;
        root["code"] = content;
        root["type"] = "sound";
        root["name"] = QFileInfo(savePath).baseName();

        QDir().mkpath(QFileInfo(savePath).absolutePath());

        QFile outFile(savePath);
        if (outFile.exists()) { outFile.setPermissions(QFile::WriteOwner); outFile.remove(); }
        if (outFile.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(root);
            outFile.write(doc.toJson());
            outFile.close();
        }
    }

    QTimer::singleShot(100, m_mainWindow, [this, savePath]() {
        m_mainWindow->refreshAndSelectPreset(m_mainWindow->ui->treeSounds, savePath);
    });
}

void PresetSerializer::saveMotionAs(const QString &startDir, const QString &sourceFilePath)
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();

    QString defaultSelection = startDir + "/NewMotion.json";
    if (!sourceFilePath.isEmpty()) defaultSelection = startDir + "/" + QFileInfo(sourceFilePath).fileName();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    MobileSaveDialog dialog("Save Surface As...", startDir, baseName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();
#else
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface As...", defaultSelection, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
#endif

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastMotionDir", QFileInfo(savePath).absolutePath());

    saveMotion(savePath);
}
