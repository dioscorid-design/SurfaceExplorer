// mainwindow_drive.cpp - MainWindow: la guida libera (2.1). Il tasto DRIVE
// della barra di stato, l'esclusivita' con gli altri moti della camera e
// l'indicatore sulla vista. Il volo e' CameraDrive (cameradrive.h); i suoi
// ganci nel resto di MainWindow (master, RESET, cursore T, passo del REC,
// caricamenti) chiamano le funzioni di qui.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"
#include "gamepad.h"

#include <QPainter>

namespace {
// GLI STICK E I TASTINI DELLA GUIDA A TOCCO, disegnati sopra la vista: figlio
// del GLWidget come l'indicatore (fuori dai video, che si compongono
// offscreen). Il tocco non lo riceve (su iOS un widget figlio della vista non
// lo riceverebbe comunque): lo gestisce InputHandler sulla vista, con le stesse
// misure (CameraDrive::touchButtonRect, kTouchStickRadius).
class DriveTouchOverlay : public QWidget
{
public:
    DriveTouchOverlay(GLWidget *view, const DriveControls *controls)
        : QWidget(view), m_view(view), m_controls(controls)
    {
        setObjectName(QStringLiteral("driveTouchOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const DriveControls &c = *m_controls;
        // I tastini: su, giu', e nel 4D W+ e W-. Piu' scuri mentre si tengono.
        static const char *const kLabels[CameraDrive::TouchButtonCount] = { "\u25B2", "\u25BC", "W+", "W\u2212" };
        const bool held[CameraDrive::TouchButtonCount] = { c.liftUp, c.liftDown, c.wPlus, c.wMinus };
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(17);
        p.setFont(font);
        for (int b = 0; b < m_view->driveTouchButtons() && b < CameraDrive::TouchButtonCount; ++b) {
            const QRectF r = CameraDrive::touchButtonRect(b, size());
            p.setPen(QPen(QColor(255, 255, 255, 150), 1.5));
            p.setBrush(QColor(0, 0, 0, held[b] ? 210 : 110));
            p.drawEllipse(r);
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, QString::fromUtf8(kLabels[b]));
        }
        // Gli stick, dove sono nati: il cerchio del raggio e il pomello.
        const auto stick = [&p](bool on, const QPointF &origin, float x, float y) {
            if (!on) return;
            const qreal radius = CameraDrive::kTouchStickRadius;
            p.setPen(QPen(QColor(255, 255, 255, 120), 2.0));
            p.setBrush(QColor(0, 0, 0, 60));
            p.drawEllipse(origin, radius, radius);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 150));
            p.drawEllipse(origin + QPointF(x, y) * radius, 22.0, 22.0);
        };
        stick(c.touchLeft, c.touchLeftOrigin, c.touchLeftX, c.touchLeftY);
        stick(c.touchRight, c.touchRightOrigin, c.touchRightX, c.touchRightY);
    }

private:
    GLWidget *m_view;
    const DriveControls *m_controls;
};
} // namespace

bool MainWindow::driveRunning() const { return m_drive && m_drive->isOn(); }

bool MainWindow::driveOwnsCamera() const { return m_drive && m_drive->ownsCamera(); }

bool MainWindow::driveTouchUi() const
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return true;
#else
    return m_driveTouchUiForced;
#endif
}

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
            "Touch: the left thumb sets speed and turns, the right thumb pitches and rolls; "
            "double tap to brake; the buttons on the right go up and down (and toward W).\n"
            "Game controller: the left stick sets speed and turns, the right stick pitches and rolls; "
            "R2 / L2 go up and down; \u2715 brakes, L1 / R1 go toward W, L3 makes everything faster, "
            "up / down on the D-pad double or halve the speeds, \u25CB stops, \u25B3 turns DRIVE on and off.\n"
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
    // Un pad che si scollega a guida accesa rilascia i suoi comandi (vedi
    // CameraDrive::onGamepadConnection). Qui e non in setupDrive: il pad lo
    // crea setupGamepad, dopo.
    if (m_gamepad)
        connect(m_gamepad, &Gamepad::connectionChanged, m_drive, &CameraDrive::onGamepadConnection,
                Qt::UniqueConnection);
    // I tasti arrivano alla vista.
    gl->setFocus(Qt::OtherFocusReason);
    updateViewButtonsEnabled();   // spenti i tasti a scatto dei dock 3D/4D
    updateMasterButtonState();
    refreshDriveButton();
    refreshDriveHud();
    // Il cursore T muove ora il volo: subito, non al prossimo giro del suo timer.
    refreshSceneTime();
}

bool MainWindow::handleDrivePadButton(GamepadButton button)
{
    if (!m_drive) return false;
    if (button == GamepadButton::Triangle) {
        // Il tasto DRIVE col suo clic: le stesse condizioni.
        if (m_btnDrive && m_btnDrive->isEnabled()) m_btnDrive->click();
        return true;
    }
    if (!driveRunning()) return false;
    switch (button) {
    case GamepadButton::Circle: stopDrive(); break;
    case GamepadButton::Up:     m_drive->controls().scaleSpeeds(true); break;
    case GamepadButton::Down:   m_drive->controls().scaleSpeeds(false); break;
    default: break;
    }
    return true;
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
    refreshSceneTime();
}

void MainWindow::releaseDriveCamera()
{
    if (!driveOwnsCamera()) return;
    m_drive->release();
    if (m_scene.lastCameraMotion == QLatin1String("drive")) m_scene.lastCameraMotion.clear();
    updateViewButtonsEnabled();
    refreshDriveButton();
    refreshDriveHud();
    refreshSceneTime();
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
        if (m_driveTouchOverlay) m_driveTouchOverlay->hide();
        gl->setDriveTouchButtons(0);
        return;
    }
    // I comandi a tocco: tastini su e giu', e W dove si va verso W.
    const bool touch = driveTouchUi();
    gl->setDriveTouchButtons(!touch ? 0 : driveMovesW() ? 4 : 2);
    if (touch) {
        if (!m_driveTouchOverlay) m_driveTouchOverlay = new DriveTouchOverlay(gl, &m_drive->controls());
        if (m_driveTouchOverlay->geometry() != gl->rect()) m_driveTouchOverlay->setGeometry(gl->rect());
        m_driveTouchOverlay->show();
        m_driveTouchOverlay->raise();
        m_driveTouchOverlay->update();
    } else if (m_driveTouchOverlay) {
        m_driveTouchOverlay->hide();
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
    QString keys;
    if (m_gamepad && m_gamepad->isConnected()) {
        // Il pad: assi e pulsanti (handleDrivePadButton, DriveControls::setPad).
        keys = QStringLiteral("Left stick: speed and turn   Right stick: pitch and roll   R2 / L2: up / down   "
                              "\u2715 brake   L3 faster   \u2191 \u2193 scale   \u25CB stop");
        if (driveMovesW()) keys += QStringLiteral("   L1 / R1 toward W");
    } else if (touch) {
        keys = QStringLiteral("Left thumb: speed and turn   Right thumb: pitch and roll   Double tap: brake   "
                              "\u25B2 \u25BC up / down");
        if (driveMovesW()) keys += QStringLiteral("   W+ W\u2212 toward W");
    } else {
        keys = QStringLiteral("W/S speed  A/D turn  Up/Down pitch  Q/E roll  R/F up/down");
        if (driveMovesW()) keys += QStringLiteral("  Z/X toward W");
        keys += QStringLiteral("  Space brake  Shift faster  +/- scale  Esc stop");
    }
    const QString text = status + QLatin1Char('\n') + keys;
    // Su una riga se ci sta; altrimenti a capo entro la vista (un telefono),
    // lasciando libera la colonna dei tastini a tocco.
    const int maxWidth = std::max(120, gl->width() - 24 - (touch ? 80 : 0));
    if (m_driveHud->text() != text || m_driveHud->width() > maxWidth) {
        m_driveHud->setText(text);
        m_driveHud->setWordWrap(false);
        const QSize oneLine = m_driveHud->sizeHint();
        if (oneLine.width() <= maxWidth) {
            m_driveHud->resize(oneLine);
        } else {
            m_driveHud->setWordWrap(true);
            m_driveHud->resize(maxWidth, m_driveHud->heightForWidth(maxWidth));
        }
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
    refreshSceneTime();
}
