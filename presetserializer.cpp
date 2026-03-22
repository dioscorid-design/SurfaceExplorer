#include "presetserializer.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "libraryfileoperations.h"

#include <QFileDialog>
#include <QSettings>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMessageBox>

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
    if (rootPath.isEmpty()) rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer";

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

        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface", startPath, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
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

    QFile file(fileName);

    if (file.exists()) {
        file.setPermissions(file.permissions() | QFile::WriteOwner | QFile::WriteUser);
    }

    if (m_mainWindow->m_fileOps) {
        m_mainWindow->m_fileOps->backupBeforeOverwrite(fileName);
    }

    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        m_mainWindow->refreshRepositories();
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
        // Se c'era già un vecchio tag //IMG:, lo rimuoviamo per non fare duplicati
        int imgIndex = currentCode.indexOf("//IMG:");
        if (imgIndex != -1) {
            currentCode = currentCode.left(imgIndex).trimmed();
        }
        currentCode += "\n//IMG:" + m_mainWindow->m_currentTexturePath;
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

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        m_mainWindow->m_currentTexturePath = path;
        m_mainWindow->ui->tabWidget->setCurrentWidget(m_mainWindow->ui->Texture);
        m_mainWindow->refreshRepositories();
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

        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Record", startPath, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);
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

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        m_mainWindow->refreshRepositories();
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
    QFileDialog::Options options = QFileDialog::DontUseNativeDialog;

    if (isSurface) {
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface Script", currentMem, "Surface Script (*.json)", nullptr, options);
    } else if (isSound) {
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Sound Script", currentMem, "Sound Script (*.json)", nullptr, options);
    } else {
        fileName = QFileDialog::getSaveFileName(m_mainWindow, "Save Texture Script", currentMem, "Texture Script (*.json)", nullptr, options);
    }

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

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();

        settings.setValue(settingsKey, QFileInfo(fileName).absolutePath());
        m_mainWindow->refreshRepositories();
    } else {
        QMessageBox::critical(m_mainWindow, "Error", "Could not write to file.");
    }
}

void PresetSerializer::saveSound(const QString &startDir, const QString &sourceFilePath)
{
    QString defaultSelection = startDir + "/NewSound.json";
    QString filter = "JSON Files (*.json)";
    bool isMediaFile = false;

    // Capisce se stiamo clonando un file audio reale (MP3/WAV/OGG)
    if (!sourceFilePath.isEmpty()) {
        defaultSelection = startDir + "/" + QFileInfo(sourceFilePath).fileName();
        if (sourceFilePath.endsWith(".mp3", Qt::CaseInsensitive) ||
            sourceFilePath.endsWith(".wav", Qt::CaseInsensitive) ||
            sourceFilePath.endsWith(".ogg", Qt::CaseInsensitive)) {

            isMediaFile = true;
            QString ext = QFileInfo(sourceFilePath).suffix().toLower();
            filter = QString("%1 Files (*.%2)").arg(ext.toUpper(), ext);
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

    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Sound As...", defaultSelection, filter, nullptr, QFileDialog::DontUseNativeDialog);

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();
    // ==============================================================

    if (savePath.isEmpty()) return;

    // Forza l'estensione corretta
    QString expectedExt = isMediaFile ? QFileInfo(sourceFilePath).suffix() : "json";
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
                QFile outFile(savePath);
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(QJsonDocument(root).toJson());
                    outFile.close();
                }
            }
        }
    } else {
        // Creazione nuovo script audio (JSON) da zero
        QString content = m_mainWindow->m_soundScriptText;
        // Usa ScriptModeSound temporaneamente se necessario, o assumi il flag della mainWindow
        if (m_mainWindow->m_currentScriptMode == 2) { // 2 = ScriptModeSound
            content = m_mainWindow->ui->txtScriptEditor->toPlainText();
        }

        QJsonObject root;
        root["code"] = content;
        root["type"] = "sound";
        root["name"] = QFileInfo(savePath).baseName();

        QFile outFile(savePath);
        if (outFile.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(root);
            outFile.write(doc.toJson());
            outFile.close();
        }
    }

    m_mainWindow->ui->tabWidget->setCurrentWidget(m_mainWindow->ui->Sounds);
    m_mainWindow->refreshRepositories();
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

    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Texture Preset", defaultSelection, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);

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

    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Surface As...", defaultSelection, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastFolder", QFileInfo(savePath).absolutePath());

    saveSurface(savePath);
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

    QString savePath = QFileDialog::getSaveFileName(m_mainWindow, "Save Record As...", defaultSelection, "JSON Files (*.json)", nullptr, QFileDialog::DontUseNativeDialog);

    if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
    if (wasPath4D) m_mainWindow->pathTimer->start();
    if (wasPath3D) m_mainWindow->pathTimer3D->start();

    if (savePath.isEmpty()) return;
    if (!savePath.endsWith(".json", Qt::CaseInsensitive)) savePath += ".json";

    QSettings().setValue("lastMotionDir", QFileInfo(savePath).absolutePath());

    saveMotion(savePath);
}
