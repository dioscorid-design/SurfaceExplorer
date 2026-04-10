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

// --- INIZIO CUSTOM ANDROID DIALOG ---
#if defined(Q_OS_ANDROID)
#include <QMessageBox> // Assicurati di includerlo in cima al file presetserializer.cpp

class AndroidSaveDialog : public QDialog {
public:
    AndroidSaveDialog(const QString& title, const QString& startDir, const QString& defaultFileName, QWidget* parent = nullptr)
        : QDialog(parent), currentDir(startDir) {
        setWindowTitle(title);
        setMinimumSize(320, 450); // Dimensione ideale per smartphone

        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        // 1. Label del percorso attuale (In cima)
        pathLabel = new QLabel(currentDir.absolutePath(), this);
        pathLabel->setWordWrap(true);
        pathLabel->setStyleSheet("font-size: 12px; color: gray;");
        mainLayout->addWidget(pathLabel);

        // 2. Input del nome (Spostato in ALTO per non essere coperto dalla tastiera!)
        QHBoxLayout* nameLayout = new QHBoxLayout();
        nameLayout->addWidget(new QLabel("Name:", this));
        nameEdit = new QLineEdit(defaultFileName, this);
        nameEdit->setStyleSheet("padding: 10px; font-size: 16px;");
        nameLayout->addWidget(nameEdit);
        mainLayout->addLayout(nameLayout);

        // 3. Lista delle cartelle e dei file
        listWidget = new QListWidget(this);
        listWidget->setStyleSheet("QListWidget::item { padding: 18px; border-bottom: 1px solid #ddd; font-size: 16px; }");
        mainLayout->addWidget(listWidget);

        // 4. Bottoni Salva / Annulla (In fondo)
        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* cancelBtn = new QPushButton("Cancel", this);
        QPushButton* saveBtn = new QPushButton("Save", this);
        cancelBtn->setStyleSheet("padding: 12px; font-size: 16px;");
        saveBtn->setStyleSheet("padding: 12px; font-size: 16px; font-weight: bold;");
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);
        mainLayout->addLayout(btnLayout);

        // Connessioni Bottoni
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

        // ---> MODIFICA 1: Invece di fare direttamente accept(), facciamo un controllo
        connect(saveBtn, &QPushButton::clicked, this, &AndroidSaveDialog::onSaveClicked);

        // Connessione Navigazione: leggiamo il nome e il TIPO da Qt::UserRole
        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString itemName = item->data(Qt::UserRole).toString();
            QString itemType = item->data(Qt::UserRole + 1).toString(); // Leggiamo il tipo

            if (itemType == "DIR") {
                // È una cartella: navighiamo
                if (itemName == "..") {
                    currentDir.cdUp();
                } else {
                    currentDir.cd(itemName);
                }
                refreshList();
            }
            else if (itemType == "FILE") {
                // È un file: copiamo il nome senza l'estensione (o con, come preferisci)
                nameEdit->setText(itemName);
            }
        });

        refreshList();
    }

    QString getSelectedPath() const {
        QString name = nameEdit->text();
        if (!name.endsWith(".json", Qt::CaseInsensitive)) name += ".json";
        return currentDir.absoluteFilePath(name);
    }

private:
    void refreshList() {
        listWidget->clear();

        // Elemento per tornare su
        QListWidgetItem* upItem = new QListWidgetItem("📁 .. (Up)", listWidget);
        upItem->setData(Qt::UserRole, "..");
        upItem->setData(Qt::UserRole + 1, "DIR"); // Contrassegno come Directory

        // Popoliamo le cartelle
        QFileInfoList dirs = currentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& dir : dirs) {
            QListWidgetItem* dirItem = new QListWidgetItem("📁 " + dir.fileName(), listWidget);
            dirItem->setData(Qt::UserRole, dir.fileName());
            dirItem->setData(Qt::UserRole + 1, "DIR"); // Contrassegno come Directory
        }

        // ---> MODIFICA 2: Popoliamo anche i file JSON
        QFileInfoList files = currentDir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            QListWidgetItem* fileItem = new QListWidgetItem("📄 " + file.fileName(), listWidget);
            fileItem->setData(Qt::UserRole, file.fileName());
            fileItem->setData(Qt::UserRole + 1, "FILE"); // Contrassegno come File
        }

        pathLabel->setText(currentDir.absolutePath());
    }

    // ---> MODIFICA 3: Controllo di sovrascrittura prima di chiudere la finestra
    void onSaveClicked() {
        QString finalPath = getSelectedPath();
        QFileInfo checkFile(finalPath);

        if (checkFile.exists()) {
            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, "Sovrascrivi",
                                          "Un preset con questo nome esiste già in questa cartella.\nVuoi sovrascriverlo?",
                                          QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::No) {
                return; // Ferma il processo, la finestra rimane aperta
            }
        }
        accept(); // Tutto ok, chiudiamo la finestra con successo
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
                startPath = settings.value("lastFolder", rootPath + "/Surfaces").toString();
            }
        }

        if (!startPath.endsWith(".json", Qt::CaseInsensitive)) {
            if (!startPath.endsWith("/")) startPath += "/";
            startPath += "NewSurface.json";
        }

#if defined(Q_OS_ANDROID)
        bool ok;
        QString baseName = QFileInfo(startPath).completeBaseName();
        QString inputName = QInputDialog::getText(m_mainWindow, "Save Surface", "File name:", QLineEdit::Normal, baseName, &ok);
        if (!ok || inputName.isEmpty()) {
            if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
            if (wasPath4D) m_mainWindow->pathTimer->start();
            if (wasPath3D) m_mainWindow->pathTimer3D->start();
            return;
        }
        fileName = QFileInfo(startPath).absolutePath() + "/" + inputName + ".json";
#elif defined(Q_OS_IOS)
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface", startPath, "JSON Files (*.json)");
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
    if (absPath.contains("/Motions/", Qt::CaseInsensitive) ||
        absPath.contains("/Textures/", Qt::CaseInsensitive) ||
        absPath.contains("/Sounds/", Qt::CaseInsensitive)) {
        QMessageBox::warning(m_mainWindow, "Salvataggio Bloccato",
                             "Operazione non consentita.\n\nLe Superfici statiche devono risiedere in 'Surfaces'.\nSe vuoi salvare la scena globale (che contiene questa superficie), usa il comando 'Save Motion'.");
        return;
    }

    QFileInfo fileInfo(fileName);
    settings.setValue("lastFolder", fileInfo.absolutePath());

    QJsonObject root;
    root["name"] = QFileInfo(fileName).baseName();
    root["type"] = "surface";

    QString eqX = m_mainWindow->ui->lineX->toPlainText().trimmed();
    QString eqY = m_mainWindow->ui->lineY->toPlainText().trimmed();
    QString eqZ = m_mainWindow->ui->lineZ->toPlainText().trimmed();

    if (!eqX.isEmpty() || !eqY.isEmpty() || !eqZ.isEmpty()) {
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
    } else {
        QString scriptContent = m_mainWindow->property("rawSurfaceScript").toString();
        if (!scriptContent.trimmed().isEmpty()) {
            root["scriptCode"] = scriptContent;
            QJsonObject eq; eq["x"]=""; eq["y"]=""; eq["z"]=""; eq["p"]="";
            root["equations"] = eq;
        }
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
    root["limits"] = limits;

    root["steps"] = m_mainWindow->ui->stepSlider->value();

    QJsonObject colors;
    colors["r"] = m_mainWindow->m_currentSurfaceColor.redF();
    colors["g"] = m_mainWindow->m_currentSurfaceColor.greenF();
    colors["b"] = m_mainWindow->m_currentSurfaceColor.blueF();
    root["colors"] = colors;

    root["lightingMode"] = m_mainWindow->m_lightingMode4D;
    root["lightIntensity"] = m_mainWindow->ui->lightSlider->value() / 100.0;
    root["use4DLighting"] = m_mainWindow->ui->glWidget->is4DActive();
    root["renderMode"] = m_mainWindow->m_savedRenderMode;
    root["projectionMode"] = (int)m_mainWindow->ui->glWidget->projectionMode;
    root["showBorder"] = m_mainWindow->ui->btnBorder->isChecked();

    // 1. Salva Rotazione 4D
    QJsonObject angles;
    angles["omega"] = (double)m_mainWindow->ui->glWidget->getOmega();
    angles["phi"] = (double)m_mainWindow->ui->glWidget->getPhi();
    angles["psi"] = (double)m_mainWindow->ui->glWidget->getPsi();
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

        QTimer::singleShot(100, m_mainWindow, &MainWindow::refreshRepositories);
        m_mainWindow->ui->dockSurfaces->show();
    } else {
        QMessageBox::critical(m_mainWindow, "Save Error",
                              "Could not overwrite the file. Check if it's locked by the system:\n" + fileName);
    }
}

void PresetSerializer::saveTexture(const QString &path)
{
    QString absPath = QFileInfo(path).absolutePath() + "/";
    if (absPath.contains("/Surfaces/", Qt::CaseInsensitive) ||
        absPath.contains("/Motions/", Qt::CaseInsensitive) ||
        absPath.contains("/Sounds/", Qt::CaseInsensitive)) {
        QMessageBox::warning(m_mainWindow, "Salvataggio Bloccato",
                             "Operazione non consentita.\n\nI preset Texture devono essere salvati esclusivamente nella cartella 'Textures'.");
        return;
    }

    QJsonObject root;

    QString currentCode;
    bool isBg = m_mainWindow->ui->radioBackground->isChecked();

    // Se stiamo guardando esplicitamente la scheda Texture nello script, prendiamo il testo live
    if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeTexture) {
        currentCode = m_mainWindow->ui->txtScriptEditor->toPlainText();
        if (isBg) m_mainWindow->m_bgTextureCode = currentCode;
        else m_mainWindow->m_surfaceTextureCode = currentCode;
    } else {
        currentCode = isBg ? m_mainWindow->m_bgTextureCode : m_mainWindow->m_surfaceTextureCode;
    }

    if (m_mainWindow->m_isImageMode && !m_mainWindow->m_currentTexturePath.isEmpty()) {
        // Usiamo una RegEx per rimuovere l'intera linea //IMG: ovunque si trovi,
        // senza cancellare il codice procedurale che la segue.
        QRegularExpression imgRe(R"(^\s*//IMG:.*$\n?)", QRegularExpression::MultilineOption);
        currentCode.remove(imgRe);

        // Riappendiamo il tag in cima al codice in modo pulito
        currentCode = "//IMG:" + m_mainWindow->m_currentTexturePath + "\n" + currentCode.trimmed();
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

        m_mainWindow->m_currentTexturePath = path;
        m_mainWindow->ui->tabWidget->setCurrentWidget(m_mainWindow->ui->Texture);
        QTimer::singleShot(100, m_mainWindow, &MainWindow::refreshRepositories);
        m_mainWindow->ui->dockSurfaces->show();
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

#if defined(Q_OS_ANDROID)
        bool ok;
        QString baseName = QFileInfo(startPath).completeBaseName();
        QString inputName = QInputDialog::getText(m_mainWindow, "Save Record", "File name:", QLineEdit::Normal, baseName, &ok);
        if (!ok || inputName.isEmpty()) {
            if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
            if (wasPath4D) m_mainWindow->pathTimer->start();
            if (wasPath3D) m_mainWindow->pathTimer3D->start();
            if (wasTimeAnimating) {
                m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
                m_mainWindow->ui->glWidget->startAnimationTimer();
            }
            return;
        }
        fileName = QFileInfo(startPath).absolutePath() + "/" + inputName + ".json";
#elif defined(Q_OS_IOS)
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Record", startPath, "JSON Files (*.json)");
#else
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Record", startPath, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
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
    if (absPath.contains("/Surfaces/", Qt::CaseInsensitive) ||
        absPath.contains("/Textures/", Qt::CaseInsensitive) ||
        absPath.contains("/Sounds/", Qt::CaseInsensitive)) {
        QMessageBox::warning(m_mainWindow, "Salvataggio Bloccato",
                             "Operazione non consentita.\n\nI preset Motion catturano l'intera scena (superficie compresa) e devono essere salvati esclusivamente nella cartella 'Motions'.");
        return;
    }

    QString saveFolder = QFileInfo(fileName).absolutePath();
    settings.setValue("lastMotionDir", saveFolder);

    QJsonObject root;
    root["type"] = "motion";
    root["name"] = QFileInfo(fileName).baseName();

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

    bool usingEquations = !m_mainWindow->ui->lineX->toPlainText().trimmed().isEmpty() &&
                          m_mainWindow->ui->lineX->toPlainText().trimmed() != "0";

    QString scriptContent = m_mainWindow->property("rawSurfaceScript").toString();
    if (scriptContent.isEmpty()) {
        scriptContent = m_mainWindow->ui->txtScriptEditor->toPlainText();
    }

    if (!scriptContent.trimmed().isEmpty() && !usingEquations) {
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

    // L'audio è ora completamente separato in memoria, quindi estraiamo la texture pura.
    QString codeToSave;
    if (texEnabled) {
        if (m_mainWindow->m_isImageMode && !m_mainWindow->m_currentTexturePath.isEmpty()) {
            codeToSave = "//IMG:" + m_mainWindow->m_currentTexturePath;
        } else if (m_mainWindow->m_isCustomMode) {
            codeToSave = m_mainWindow->m_surfaceTextureCode.trimmed();
        } else {
            codeToSave = m_mainWindow->m_surfaceTextureCode.trimmed();
        }
    } else {
        codeToSave = m_mainWindow->m_surfaceTextureCode.trimmed();
    }

    codeToSave.remove(QRegularExpression(R"(^\s*//MUSIC:.*$\n?)", QRegularExpression::MultilineOption));
    codeToSave.remove(QRegularExpression(R"(//SOUND_BEGIN.*?//SOUND_END\n?)", QRegularExpression::DotMatchesEverythingOption));
    codeToSave = codeToSave.trimmed();

    if (!m_mainWindow->m_soundScriptText.trimmed().isEmpty()) {
        codeToSave = m_mainWindow->m_soundScriptText.trimmed() + "\n\n" + codeToSave;
    }

    texture["code"] = codeToSave;
    root["texture"] = texture;

    QJsonObject speeds;
    speeds["nutation"] = (double)m_mainWindow->ui->glWidget->getNutationSpeed();
    speeds["precession"] = (double)m_mainWindow->ui->glWidget->getPrecessionSpeed();
    speeds["spin"] = (double)m_mainWindow->ui->glWidget->getSpinSpeed();
    speeds["omega"] = (double)m_mainWindow->ui->glWidget->getOmegaSpeed();
    speeds["phi"] = (double)m_mainWindow->ui->glWidget->getPhiSpeed();
    speeds["psi"] = (double)m_mainWindow->ui->glWidget->getPsiSpeed();
    speeds["path3D"] = m_mainWindow->ui->speed3DSlider->value();
    speeds["path4D"] = m_mainWindow->ui->speed4DSlider->value();
    root["speeds"] = speeds;

    QJsonObject angles;
    angles["omega"] = (double)m_mainWindow->ui->glWidget->getOmega();
    angles["phi"] = (double)m_mainWindow->ui->glWidget->getPhi();
    angles["psi"] = (double)m_mainWindow->ui->glWidget->getPsi();
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
    root["renderMode"] = m_mainWindow->m_savedRenderMode;
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

        QTimer::singleShot(100, m_mainWindow, &MainWindow::refreshRepositories);
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
        if (isSurface) currentMem = settings.value("pathSurfaces", rootPath + "/Surfaces").toString();
        else if (isSound) currentMem = settings.value("pathSounds", rootPath + "/Sounds").toString();
        else currentMem = settings.value("pathTextures", rootPath + "/Textures").toString();
    }

    QString fileName;

#if defined(Q_OS_ANDROID)
    QString title = isSurface ? "Save Surface Script" : (isSound ? "Save Sound Script" : "Save Texture Script");
    // currentMem è la cartella di partenza, "NewScript" è il nome di default
    AndroidSaveDialog dialog(title, currentMem, "NewScript", m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        resumeTimers();
        return;
    }
    fileName = dialog.getSelectedPath();
#elif defined(Q_OS_IOS)
    QFileDialog::Options options = QFileDialog::Options();
    if (isSurface) fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface Script", currentMem, "Surface Script (*.json)", nullptr, options);
    else if (isSound) fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Sound Script", currentMem, "Sound Script (*.json)", nullptr, options);
    else fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Texture Script", currentMem, "Texture Script (*.json)", nullptr, options);
#else
    QFileDialog::Options options = QFileDialog::DontUseNativeDialog;
    if (isSurface) fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface Script", currentMem, "Surface Script (*.json)", nullptr, options);
    else if (isSound) fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Sound Script", currentMem, "Sound Script (*.json)", nullptr, options);
    else fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Texture Script", currentMem, "Texture Script (*.json)", nullptr, options);
#endif

    resumeTimers();

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) fileName += ".json";

    QJsonObject root;
    if (isSurface) {
        root["scriptCode"] = content;
        root["steps"] = m_mainWindow->ui->stepSlider->value();

        QJsonObject limits;
        limits["uMin"] = m_mainWindow->parseMath(m_mainWindow->ui->uMinEdit->text());
        limits["uMax"] = m_mainWindow->parseMath(m_mainWindow->ui->uMaxEdit->text());
        limits["vMin"] = m_mainWindow->parseMath(m_mainWindow->ui->vMinEdit->text());
        limits["vMax"] = m_mainWindow->parseMath(m_mainWindow->ui->vMaxEdit->text());
        root["limits"] = limits;

        QJsonObject constants;
        constants["A"] = m_mainWindow->ui->aSlider->value() / 100.0f;
        constants["B"] = m_mainWindow->ui->bSlider->value() / 100.0f;
        constants["C"] = m_mainWindow->ui->cSlider->value() / 100.0f;
        constants["S"] = m_mainWindow->ui->sSlider->value() / 100.0f;
        root["constants"] = constants;
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

        settings.setValue(settingsKey, QFileInfo(fileName).absolutePath());
        QTimer::singleShot(100, m_mainWindow, &MainWindow::refreshRepositories);
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
    QTimer::singleShot(100, m_mainWindow, &MainWindow::refreshRepositories);
}

void PresetSerializer::saveTextureAs(const QString &startDir, const QString &sourceFilePath)
{
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();

    if (wasAnimating) m_mainWindow->ui->glWidget->pauseMotion();
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();

    QString defaultSelection = startDir + "/NewTexture.json";

    if (!sourceFilePath.isEmpty()) {
        QString baseName = QFileInfo(sourceFilePath).completeBaseName();
        defaultSelection = startDir + "/" + baseName + ".json";
    }

#if defined(Q_OS_ANDROID)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    AndroidSaveDialog dialog("Save Texture As...", startDir, baseName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();
#elif defined(Q_OS_IOS)
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Texture As...", defaultSelection, "JSON Files (*.json)");
#else
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

#if defined(Q_OS_ANDROID)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    AndroidSaveDialog dialog("Save Surface As...", startDir, baseName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();
#elif defined(Q_OS_IOS)
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface As...", defaultSelection, "JSON Files (*.json)");
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
#if defined(Q_OS_ANDROID)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    AndroidSaveDialog dialog("Save Sound As...", startDir, baseName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();

    // FIX ANDROID: Se è un file media, assicuriamoci che getSelectedPath non abbia
    // forzato ".json" per errore (la nostra classe aggiunge sempre .json di default).
    if (isMediaFile && savePath.endsWith(".json", Qt::CaseInsensitive)) {
        savePath.chop(5); // Rimuove ".json"
    }

#elif defined(Q_OS_IOS)
    // FIX: Usa la variabile "filter" calcolata sopra, non la stringa hardcoded!
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Sound As...", defaultSelection, filter);
#else
    // FIX: Usa la variabile "filter" calcolata sopra!
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

    m_mainWindow->ui->tabWidget->setCurrentWidget(m_mainWindow->ui->Sounds);
    QTimer::singleShot(100, m_mainWindow, &MainWindow::refreshRepositories);
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

#if defined(Q_OS_ANDROID)
    QString baseName = QFileInfo(defaultSelection).completeBaseName();
    AndroidSaveDialog dialog("Save Record As...", startDir, baseName, m_mainWindow);

    if (dialog.exec() != QDialog::Accepted) {
        // Se l'utente preme Cancel, riprendiamo l'animazione ed usciamo
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) m_mainWindow->pathTimer->start();
        if (wasPath3D) m_mainWindow->pathTimer3D->start();
        return;
    }
    QString savePath = dialog.getSelectedPath();
#elif defined(Q_OS_IOS)
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Record As...", defaultSelection, "JSON Files (*.json)");
#else
    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Record As...", defaultSelection, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
#endif

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastMotionDir", QFileInfo(savePath).absolutePath());

    saveMotion(savePath);
}
