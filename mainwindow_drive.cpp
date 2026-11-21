// mainwindow_drive.cpp - MainWindow: la guida libera (2.1). Il tasto DRIVE
// della barra di stato, l'esclusivita' con gli altri moti della camera e
// l'indicatore sulla vista. Il volo e' CameraDrive (cameradrive.h); i suoi
// ganci nel resto di MainWindow (master, RESET, cursore T, passo del REC,
// caricamenti) chiamano le funzioni di qui.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"

bool MainWindow::driveRunning() const { return m_drive && m_drive->isOn(); }

bool MainWindow::driveOwnsCamera() const { return m_drive && m_drive->ownsCamera(); }

bool MainWindow::driveMovesW() const
{
    // Le condizioni dei tasti P+/P- (setNavControlsEnabled): in Ray Marching, e
    // sull'ambiente di una scena 3D, solo dove la sezione si inclina.
    if (implicitMode() || (sceneEnvironmentSelected() && !sceneIs4D())) return selectedRotates4D();
    return true;
}

void MainWindow::setupDrive()
{
    m_drive = new CameraDrive(ui->glWidget, [this] { return m_isRecording; },
                              [this] { return driveMovesW(); }, this);
    // I comandi arrivano alla guida finche' e' accesa, anche durante il REC:
    // li' il passo e' quello del recorder, senza comandi, e il mouse non deve
    // tornare a girare la scena dentro il video.
    ui->glWidget->setDriveControls(&m_drive->controls(), [this] { return driveRunning(); });
    connect(ui->glWidget, &GLWidget::driveExitRequested, this, [this] {
        if (driveRunning()) stopDrive();
    });
    // Guidare muove la vista: il lavoro da salvare, come un trascinamento.
    connect(m_drive, &CameraDrive::userDrove, this, [this] { noteViewControlUsed(); });
    connect(m_drive, &CameraDrive::stepped, this, &MainWindow::refreshDriveHud);
    if (m_btnDrive) {
        m_btnDrive->setToolTip(QStringLiteral(
            "Fly the camera freely through the scene. While it is on:\n"
            "W / S: faster / slower (below zero, backwards); Space: brake.\n"
            "A / D or Left / Right: turn; Up / Down: nose up / down; Q / E: roll.\n"
            "R / F: up / down; Z / X: toward W (4D). Shift: faster.\n"
            "+ / -: double or halve all the speeds (for large or small scenes).\n"
            "Mouse: drag as a stick to steer, right-drag to roll, wheel for speed.\n"
            "Esc or DRIVE again: stop. The flight is recorded on the scene time T: "
            "RESET goes back to its start, and the T slider and REC play it again."));
        connect(m_btnDrive, &QPushButton::clicked, this, &MainWindow::onDriveClicked);
    }
    refreshDriveButton();
}

void MainWindow::onDriveClicked()
{
    if (driveRunning()) stopDrive();
    else startDrive();
}

void MainWindow::startDrive()
{
    GLWidget *gl = ui->glWidget;
    if (!m_drive || !gl || gl->isFlatView() || driveRunning()) return;
    // Il moto della camera dell'ambiente e' uno: si fermano il path e le
    // rotazioni dell'ambiente (con una superficie sola, le sue). In una scena
    // gli altri oggetti continuano coi loro moti.
    stopPathAnimations();
    stopRotationMotion();
    m_userStoppedCameraMotion = false;
    m_scene.lastCameraMotion = QStringLiteral("drive");
    m_drive->start();
    // I tasti arrivano alla vista.
    gl->setFocus(Qt::OtherFocusReason);
    updateViewButtonsEnabled();   // spenti i tasti a scatto dei dock 3D/4D
    updateMasterButtonState();
    refreshDriveButton();
    refreshDriveHud();
    // Il cursore T muove ora il volo: subito, non al prossimo giro del suo timer.
    refreshSceneTimeLabel();
}

void MainWindow::stopDrive()
{
    if (!driveRunning()) return;
    m_drive->stop();
    // Fermata dall'utente, come lo Stop di un path: un Invio su un campo non la riaccende.
    m_userStoppedCameraMotion = true;
    updateViewButtonsEnabled();
    updateMasterButtonState();
    refreshDriveButton();
    refreshDriveHud();
    refreshSceneTimeLabel();
}

void MainWindow::releaseDriveCamera()
{
    if (!driveOwnsCamera()) return;
    m_drive->release();
    if (m_scene.lastCameraMotion == QLatin1String("drive")) m_scene.lastCameraMotion.clear();
    updateViewButtonsEnabled();
    refreshDriveButton();
    refreshDriveHud();
    refreshSceneTimeLabel();
}

void MainWindow::refreshDriveButton()
{
    if (!m_btnDrive) return;
    const bool on = driveRunning();
    m_btnDrive->setText(on ? QStringLiteral("STOP DRIVE") : QStringLiteral("DRIVE"));
    // In vista 2D la scena non c'e': si guida la texture.
    m_btnDrive->setEnabled(ui->glWidget && !ui->glWidget->isFlatView());
}

void MainWindow::refreshDriveHud()
{
    GLWidget *gl = ui->glWidget;
    if (!gl || !m_drive) return;
    if (!driveRunning()) {
        if (m_driveHud) m_driveHud->hide();
        return;
    }
    if (!m_driveHud) {
        // Figlio della vista come il messaggio della scena (SceneHintOverlay):
        // sopra la scena, fuori dai video, che si compongono offscreen.
        m_driveHud = new QLabel(gl);
        m_driveHud->setObjectName(QStringLiteral("driveHud"));
        m_driveHud->setTextFormat(Qt::PlainText);
        m_driveHud->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_driveHud->setStyleSheet(QStringLiteral(
            "QLabel#driveHud {"
            "  color: #ffffff;"
            "  background-color: rgba(0, 0, 0, 150);"
            "  border: 1px solid rgba(255, 255, 255, 70);"
            "  border-radius: 6px;"
            "  padding: 4px 10px;"
            "  font-size: 12px;"
            "}"));
    }
    const CameraDrive::State &s = m_drive->current();
    QString status = QStringLiteral("DRIVE   speed %1   cruise %2")
                         .arg(double(s.speed), 0, 'f', 2)
                         .arg(double(s.cruise), 0, 'f', 2);
    const float scale = m_drive->controls().speedScale;
    if (scale != 1.0f)
        status += scale > 1.0f ? QStringLiteral("   scale x%1").arg(double(scale))
                               : QStringLiteral("   scale 1/%1").arg(double(1.0f / scale));
    if (driveMovesW())
        status += QStringLiteral("   W %1").arg(double(gl->cutsSection() ? s.sectionP : s.observerW), 0, 'f', 2);
    if (m_drive->replaying()) status += QStringLiteral("   REPLAY: touch a control to take over");
    QString keys = QStringLiteral("W/S speed  A/D turn  Up/Down pitch  Q/E roll  R/F up/down");
    if (driveMovesW()) keys += QStringLiteral("  Z/X toward W");
    keys += QStringLiteral("  Space brake  Shift faster  +/- scale  Esc stop");
    const QString text = status + QLatin1Char('\n') + keys;
    if (m_driveHud->text() != text) {
        m_driveHud->setText(text);
        m_driveHud->adjustSize();
    }
    m_driveHud->move(12, std::max(0, gl->height() - m_driveHud->height() - 12));
    m_driveHud->show();
    m_driveHud->raise();
}

void MainWindow::applyDriveFlight(const LibraryItem &d)
{
    if (!m_drive || d.driveFlight.isEmpty() || !ui->glWidget) return;
    // Di un altro spazio (un volo di H3 in un record fuori da una scena) o un
    // file guasto: nessun volo, la camera e' quella salvata.
    if (!m_drive->loadJson(d.driveFlight)) return;
    m_drive->applyAt(ui->glWidget->sceneTime());
    if (d.activeMotion == QLatin1String("drive")) startDrive();
    refreshDriveButton();
    refreshSceneTimeLabel();
}
