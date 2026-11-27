// scenariotest_drive.cpp - Gli scenari della guida libera (2.1): sezione
// "drive" di ScenarioTest. In un file suo: la 2.1 differisce dalla 2.0 solo
// per la guida, e cosi' la fusione da v2 di scenariotest.cpp resta pulita.
#include "scenariotest.h"

#include "mainwindow.h"
#include "mainwindow_p.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "cameradrive.h"
#include "drivecollision.h"
#include "controlswindow.h"
#include "gamepad.h"
#include "librarymanager.h"
#include "presetserializer.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTouchEvent>
#include <QWindow>
#include <QScopeGuard>
#include <QTextBrowser>
#include <QtGui/private/qeventpoint_p.h>
#include <cmath>
#include <functional>

void ScenarioTest::runDriveScenarios()
{
    GLWidget *gl = m_mw->ui->glWidget;
    Ui::MainWindow *ui = m_mw->ui;
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Guida libera (2.1): DRIVE, comandi, volo registrato (RESET, cursore T, passo del REC), "
                                  "esclusivita' coi moti della camera, scene, H3, 4D =="));
    const QString kTorus = QStringLiteral("surfaces/Parametric/Surfaces/Equations/R3/Torus.json");
    const QString kSphere = QStringLiteral("surfaces/Parametric/Surfaces/Equations/R3/Sphere.json");
    const QString kCoil = QStringLiteral("records/Paths/Parametric/Surfaces/Equations/S1xS1xS1/Cytherean Coil.json");
    CameraDrive *drive = m_mw->m_drive;
    if (!drive || !m_mw->m_btnDrive) {
        check(false, QStringLiteral("la guida non c'e' (m_drive o il tasto DRIVE mancano)"));
        return;
    }
    DriveControls &c = drive->controls();
    const float dt = 1.0f / 60.0f;
    // I passi dal vivo, a mano e senza attese: i tick del timer restano fuori.
    auto fly = [&](int frames) { for (int i = 0; i < frames; ++i) drive->advance(dt, true); };
    auto camPos = [&]() { return gl->cameraPlacement().column(3).toVector3D(); };
    auto camFront = [&]() { return -gl->cameraPlacement().column(2).toVector3D().normalized(); };
    auto camUp = [&]() { return gl->cameraPlacement().column(1).toVector3D().normalized(); };
    auto sendKey = [&](QEvent::Type type, int key, bool autoRepeat = false) {
        QKeyEvent e(type, key, Qt::NoModifier, QString(), autoRepeat);
        QCoreApplication::sendEvent(gl, &e);
    };

    // --- SUPERFICIE SOLA, FERMA ------------------------------------------------
    if (!loadSurface(kTorus)) return;
    check(m_mw->m_btnDrive->isEnabled() && !m_mw->driveRunning() && !m_mw->driveOwnsCamera()
              && m_mw->m_btnDrive->text() == QLatin1String("DRIVE"),
          QStringLiteral("Torus: tasto DRIVE acceso, guida spenta"));
    const bool cleanBefore = !m_mw->hasUnsavedWork();
    const QVector3D p0 = camPos();
    const QVector3D f0 = camFront();
    const QVector3D u0 = camUp();
    const float t0 = gl->sceneTime();
    m_mw->m_btnDrive->click();
    check(m_mw->driveRunning() && drive->hasTrack() && m_mw->m_btnDrive->text() == QLatin1String("STOP DRIVE")
              && !ui->btnForward->isEnabled() && m_mw->m_driveHud && m_mw->m_driveHud->isVisible()
              && gl->m_driveCamera && (camPos() - p0).length() < 1e-4f,
          QStringLiteral("DRIVE: guida accesa dalla camera di adesso, tasti a scatto del dock 3D spenti, indicatore "
                         "sulla vista, piani near/far della guida"));

    // I comandi: tasti e mouse arrivano ai comandi della guida.
    sendKey(QEvent::KeyPress, Qt::Key_W);
    const bool wDown = c.throttleUp;
    sendKey(QEvent::KeyRelease, Qt::Key_W, /*autoRepeat=*/true);
    const bool repeatKept = c.throttleUp;
    sendKey(QEvent::KeyRelease, Qt::Key_W);
    sendKey(QEvent::KeyPress, Qt::Key_Z);
    const bool zDown = c.wMinus;
    sendKey(QEvent::KeyRelease, Qt::Key_Z);
    check(wDown && repeatKept && !c.throttleUp && zDown && !c.wMinus,
          QStringLiteral("tasti: W premuto, la ripetizione automatica non lo rilascia, il rilascio si'; Z verso W"));
    {
        const QPointF a(gl->width() / 2.0, gl->height() / 2.0), b = a + QPointF(100.0, -50.0);
        QMouseEvent press(QEvent::MouseButtonPress, a, gl->mapToGlobal(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(gl, &press);
        QMouseEvent move(QEvent::MouseMove, b, gl->mapToGlobal(b), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(gl, &move);
        const float sx = c.stickX, sy = c.stickY;
        const bool stick = c.stick && std::abs(sx - 96.0f / 196.0f) < 1e-3f && std::abs(sy + 46.0f / 196.0f) < 1e-3f;
        QMouseEvent release(QEvent::MouseButtonRelease, b, gl->mapToGlobal(b), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(gl, &release);
        QWheelEvent wheel(a, gl->mapToGlobal(a), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(gl, &wheel);
        const bool wheelStep = c.wheelSteps == 1.0f;
        check(stick && !c.stick && c.stickX == 0.0f && wheelStep,
              QStringLiteral("mouse: la cloche segue lo scostamento dal punto premuto (%1, %2) e torna al centro; "
                             "uno scatto di rotella = %3")
                  .arg(double(sx)).arg(double(sy)).arg(double(c.wheelSteps)));
        c.releaseAll();
    }

    // Avanti: W per un secondo, poi un secondo di crociera.
    std::vector<QVector3D> livePos;   // la camera a ogni passo dal vivo, per il volo registrato
    std::vector<QVector3D> liveFront;
    auto flyRecording = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            drive->advance(dt, true);
            livePos.push_back(camPos());
            liveFront.push_back(camFront());
        }
    };
    c.throttleUp = true;
    flyRecording(60);
    c.throttleUp = false;
    flyRecording(60);
    const QVector3D p1 = camPos();
    const float along = QVector3D::dotProduct(p1 - p0, f0);
    check(along > 0.2f && along < 0.8f && (p1 - p0 - f0 * along).length() < 1e-3f
              && std::abs(gl->sceneTime() - t0 - 2.0f) < 1e-3f && (camFront() - f0).length() < 1e-3f
              && drive->current().speed > 0.25f,
          QStringLiteral("W un secondo: la camera avanza di %1 lungo la vista, senza scarti, alla crociera %2 "
                         "(velocita' %3), T +%4 s")
              .arg(double(along)).arg(double(drive->current().cruise)).arg(double(drive->current().speed))
              .arg(double(gl->sceneTime() - t0)));
    check(m_mw->hasUnsavedWork() || !cleanBefore, QStringLiteral("guidare muove la vista: c'e' lavoro da salvare"));
    // + e -: tutte le velocita' a meta' e di nuovo intere; la crociera le segue.
    {
        const float cruiseBefore = drive->current().cruise;
        sendKey(QEvent::KeyPress, Qt::Key_Minus);
        sendKey(QEvent::KeyRelease, Qt::Key_Minus);
        const float halfScale = c.speedScale;
        flyRecording(1);
        const float halved = drive->current().cruise;
        const bool hudScale = m_mw->m_driveHud->text().contains(QLatin1String("scale 1/2"));
        sendKey(QEvent::KeyPress, Qt::Key_Plus);
        sendKey(QEvent::KeyRelease, Qt::Key_Plus);
        flyRecording(1);
        check(halfScale == 0.5f && c.speedScale == 1.0f && std::abs(halved - cruiseBefore * 0.5f) < 1e-4f
                  && std::abs(drive->current().cruise - cruiseBefore) < 1e-4f && hudScale,
              QStringLiteral("- e +: la scala delle velocita' a meta' (crociera %1 -> %2, indicatore \"scale 1/2\") e "
                             "di nuovo intera (%3)")
                  .arg(double(cruiseBefore)).arg(double(halved)).arg(double(drive->current().cruise)));
    }

    // IL LOOPING: muso in su per piu' di mezzo giro. La camera libera di prima
    // (yaw/pitch, pitch fermo a 89 gradi) non ci arrivava.
    float minFront = 1.0f, minUp = 1.0f;
    c.pitchUp = true;
    for (int i = 0; i < 480; ++i) {
        drive->advance(dt, true);
        livePos.push_back(camPos());
        liveFront.push_back(camFront());
        minFront = std::min(minFront, QVector3D::dotProduct(camFront(), f0));
        minUp = std::min(minUp, QVector3D::dotProduct(camUp(), u0));
    }
    c.pitchUp = false;
    c.brake = true;
    flyRecording(90);
    c.brake = false;
    check(minFront < -0.9f && minUp < -0.9f && std::abs(drive->current().speed) < 0.05f,
          QStringLiteral("looping: oltre la verticale fino a guardare indietro (%1) a testa in giu' (%2); "
                         "Spazio frena (velocita' %3)")
              .arg(double(minFront)).arg(double(minUp)).arg(double(drive->current().speed)));
    const float tFlight = gl->sceneTime();

    // Spenta: T si ferma, il volo resta.
    m_mw->m_btnDrive->click();
    wait(150);
    {
        const bool off = !m_mw->driveRunning() && m_mw->driveOwnsCamera();
        const bool still = gl->sceneTime() == tFlight;
        const bool nav = ui->btnForward->isEnabled();
        const bool hud = !m_mw->m_driveHud->isVisible();
        const bool slider = m_mw->m_sceneTimeSlider->isEnabled();
        const bool noPath = !m_mw->cameraPath();
        check(off && still && nav && hud && slider && noPath,
              QStringLiteral("DRIVE di nuovo: guida spenta col volo (%1), T fermo a %2 s (%3), tasti del dock accesi (%4), "
                             "indicatore via (%5), cursore T attivo (%6), nessun path (%7)")
                  .arg(off).arg(double(gl->sceneTime())).arg(still).arg(nav).arg(hud).arg(slider).arg(noPath));
    }

    // IL VOLO REGISTRATO. RESET: la camera all'inizio del volo, T = 0.
    m_mw->onResetClicked();
    check(gl->sceneTime() == 0.0f && (camPos() - p0).length() < 1e-4f && (camFront() - f0).length() < 1e-3f,
          QStringLiteral("RESET: T = 0, la camera al punto di partenza del volo (scarto %1)").arg(double((camPos() - p0).length())));
    // Il cursore: le pose del volo dal vivo, ai loro T.
    float seekErr = 0.0f;
    for (int k : { 30, 119, 200, 330, 420 }) {
        if (k >= int(livePos.size())) break;
        m_mw->seekSceneTime(t0 + float(k + 1) * dt);
        seekErr = std::max({ seekErr, (camPos() - livePos[k]).length(), (camFront() - liveFront[k]).length() });
    }
    m_mw->m_sceneTimeSlider->setValue(20);
    const bool sliderAt2 = std::abs(gl->sceneTime() - 2.0f) < 1e-4f && (camPos() - livePos[119]).length() < 1e-3f;
    check(seekErr < 1e-3f && sliderAt2,
          QStringLiteral("cursore T: la camera rifa' il volo (scarto massimo %1); il cursore del dock a 2 s = la "
                         "posa di 2 s").arg(double(seekErr)));
    // Il passo del REC (advanceSceneBy, 1/30 s) da 2 s: il volo, fotogramma per fotogramma.
    m_mw->startDrive();
    float recErr = 0.0f;
    for (int i = 1; i <= 30; ++i) {
        m_mw->advanceSceneBy(1.0f / 30.0f, false);
        const int k = 119 + 2 * i;
        if (k < int(livePos.size())) recErr = std::max(recErr, (camPos() - livePos[k]).length());
    }
    check(recErr < 1e-3f && std::abs(gl->sceneTime() - 3.0f) < 1e-3f && drive->replaying(),
          QStringLiteral("passo del REC a 30 fps da 2 s a 3 s: le pose del volo dal vivo (scarto massimo %1), "
                         "la guida lo sta rifacendo").arg(double(recErr)));
    // Durante il REC il tick dal vivo non fa nulla, coi tasti premuti.
    {
        const float tRec = gl->sceneTime();
        const QVector3D pRec = camPos();
        m_mw->m_isRecording = true;
        c.throttleUp = true;
        wait(200);
        m_mw->m_isRecording = false;
        c.throttleUp = false;
        check(gl->sceneTime() == tRec && (camPos() - pRec).length() < 1e-6f,
              QStringLiteral("REC in corso: il tick dal vivo e' fermo anche con W premuto (T %1 -> %2)")
                  .arg(double(tRec)).arg(double(gl->sceneTime())));
    }
    // Il pilota prende il posto del volo: la traccia si tronca e si riscrive da qui.
    {
        const float endBefore = drive->trackEnd();
        const float tTake = gl->sceneTime();
        c.yawLeft = true;
        fly(30);
        c.yawLeft = false;
        check(endBefore > tTake + 1.0f && std::abs(drive->trackEnd() - gl->sceneTime()) <= CameraDrive::kStep + 1e-4f
                  && !drive->replaying(),
              QStringLiteral("un comando dentro il volo: la traccia (fino a %1 s) si tronca a %2 s e riparte dal pilota "
                             "(fine %3 s)").arg(double(endBefore)).arg(double(tTake)).arg(double(drive->trackEnd())));
    }
    // Dal vivo col timer: T corre col tempo vero.
    {
        const float tLive = gl->sceneTime();
        c.throttleUp = true;
        wait(300);
        c.throttleUp = false;
        const float ran = gl->sceneTime() - tLive;
        check(ran > 0.15f && ran < 0.8f, QStringLiteral("dal vivo: in 0.3 s di attesa T corre di %1 s").arg(double(ran)));
    }
    // Esc spegne.
    sendKey(QEvent::KeyPress, Qt::Key_Escape);
    check(!m_mw->driveRunning() && m_mw->driveOwnsCamera(), QStringLiteral("Esc sulla vista: guida spenta"));
    // Il master: START riaccende la guida, STOP la spegne.
    m_mw->m_btnStart->click();
    const bool masterStarted = m_mw->driveRunning();
    m_mw->performMasterStop();
    check(masterStarted && !m_mw->driveRunning() && m_mw->m_btnStart->text().toUpper() == QLatin1String("START"),
          QStringLiteral("master: START riaccende la guida (%1), STOP la spegne").arg(masterStarted));

    // --- ESCLUSIVITA' COI MOTI DELLA CAMERA ----------------------------------
    // Le rotazioni della superficie sola: DRIVE le ferma, il GO congeda la guida col suo volo.
    if (!loadSurface(kTorus)) return;
    check(!m_mw->driveOwnsCamera(), QStringLiteral("un load: il volo della superficie di prima non c'e' piu'"));
    gl->setSpinSpeed(0.6f);
    if (!gl->isObjectRotating(0)) m_mw->onStopClicked();
    const bool spinning = gl->isObjectRotating(0);
    m_mw->m_btnDrive->click();
    const bool goStopped = !gl->isObjectRotating(0) && ui->btnStart_2->text() == QLatin1String("GO");
    c.throttleUp = true;
    fly(20);
    c.throttleUp = false;
    m_mw->onStopClicked();
    check(spinning && goStopped && gl->isObjectRotating(0) && !m_mw->driveRunning() && !m_mw->driveOwnsCamera()
              && !drive->hasTrack() && !gl->m_driveCamera,
          QStringLiteral("superficie in rotazione: DRIVE ferma il GO; il GO poi congeda la guida e il suo volo"));
    m_mw->onStopClicked();
    // Il path: DRIVE lo ferma, il Departure congeda la guida.
    if (!loadRecord(kCoil)) return;
    const bool pathRan = m_mw->pathRunning(CameraPaths::Path4D);
    m_mw->m_btnDrive->click();
    const bool pathStopped = !m_mw->pathRunning(CameraPaths::Path4D) && m_mw->driveRunning()
                             && !m_mw->cameraPath().has_value();
    // 4D: verso W si sposta l'osservatore.
    const float w0 = gl->observerPos().w();
    c.wPlus = true;
    fly(60);
    c.wPlus = false;
    fly(30);
    const float w1 = gl->observerPos().w();
    m_mw->seekSceneTime(gl->sceneTime() - 1.5f + dt);
    const float wBack = gl->observerPos().w();
    check(pathRan && pathStopped && m_mw->driveMovesW() && w1 - w0 > 0.1f && std::abs(wBack - w0) < 0.05f,
          QStringLiteral("Cytherean Coil: DRIVE ferma il path 4D; X porta l'osservatore verso W (%1 -> %2) e il "
                         "cursore lo riporta indietro (%3)").arg(double(w0)).arg(double(w1)).arg(double(wBack)));
    // Un Departure che non parte (equazioni sbagliate) non costa il volo.
    {
        const QString xBefore = ui->lineX_P->text();
        ui->lineX_P->setText(QStringLiteral("sin(t"));
        m_mw->onDepartureClicked();
        const bool kept = m_mw->driveOwnsCamera() && drive->hasTrack() && !m_mw->pathRunning(CameraPaths::Path4D);
        ui->lineX_P->setText(xBefore);
        check(kept, QStringLiteral("Departure con un errore nelle equazioni del path: il path non parte, la guida "
                                   "e il suo volo restano"));
    }
    m_mw->onDepartureClicked();
    check(m_mw->pathRunning(CameraPaths::Path4D) && !m_mw->driveOwnsCamera() && m_mw->cameraPath().has_value(),
          QStringLiteral("Departure: il path riprende la camera, la guida e' congedata"));
    m_mw->onDepartureClicked();

    // --- SCENA: l'ambiente si ferma, gli altri oggetti continuano --------------
    if (!buildScene(false, { kSphere })) return;
    const int sphere = gl->selectedObject();
    gl->setSpinSpeed(0.6f);
    if (!gl->isObjectRotating(sphere)) m_mw->onStopClicked();
    m_mw->selectSceneObject(0);
    gl->setSpinSpeed(0.6f);
    if (!gl->isObjectRotating(0)) m_mw->onStopClicked();
    const bool bothSpin = gl->isObjectRotating(0) && gl->isObjectRotating(sphere);
    m_mw->m_btnDrive->click();
    const QQuaternion sphereBefore = gl->objectOrientation(sphere).rotation;
    const float tScene = gl->sceneTime();
    for (int i = 0; i < 6; ++i) m_mw->advanceSceneBy(1.0f / 30.0f, false);
    check(bothSpin && !gl->isObjectRotating(0) && gl->isObjectRotating(sphere) && m_mw->driveRunning()
              && std::abs(QQuaternion::dotProduct(gl->objectOrientation(sphere).rotation, sphereBefore)) < 0.99999f
              && std::abs(gl->sceneTime() - tScene - 0.2f) < 1e-3f,
          QStringLiteral("scena: DRIVE ferma l'ambiente, la sfera continua a girare; il passo del REC avanza T con la "
                         "guida (+%1 s)").arg(double(gl->sceneTime() - tScene)));
    m_mw->stopDrive();

    // --- H3: avanti lungo la geodetica di vista, fino al bordo ----------------
    pressNew();
    m_discardOnPrompt = true;
    m_mw->startScene(MainWindow::SceneSpace::SpaceH3);
    wait(300);
    m_discardOnPrompt = false;
    if (!gl->hyperbolicSpace()) {
        check(false, QStringLiteral("H3: la scena non parte"));
        return;
    }
    const H3::Matrix h0 = gl->hyperbolicCamera();
    const double d0 = H3::distance(H3::origin(), h0 * H3::origin());
    m_mw->m_btnDrive->click();
    c.throttleUp = true;
    c.boost = true;
    fly(60);
    const double dMid = H3::distance(H3::origin(), gl->hyperbolicCamera() * H3::origin());
    const float tMid = gl->sceneTime();
    const H3::Matrix hMid = gl->hyperbolicCamera();
    fly(900);
    c.throttleUp = c.boost = false;
    fly(10);
    const double dEnd = H3::distance(H3::origin(), gl->hyperbolicCamera() * H3::origin());
    const double maxD = GLWidget::maxHyperbolicCameraDistance();
    const bool isometry = H3::isometryError(gl->hyperbolicCamera()) < 1e-6;
    const float speedAtWall = drive->current().speed;
    m_mw->seekSceneTime(tMid);
    double midErr = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) midErr = std::max(midErr, std::abs(gl->hyperbolicCamera().m[i][j] - hMid.m[i][j]));
    check(std::abs(dMid - d0) > 0.1 && dEnd <= maxD + 1e-6 && dEnd > maxD - 0.5 && isometry && midErr < 1e-4
              && std::abs(speedAtWall) < 1e-3f,
          QStringLiteral("H3: la camera si muove (distanza dal centro %1 -> %2), si ferma al bordo (%3 <= %4) restando "
                         "un'isometria; il cursore rifa' il volo (scarto %5)")
              .arg(d0).arg(dMid).arg(dEnd).arg(maxD).arg(midErr));
    m_mw->stopDrive();
    pressNew();
    check(!m_mw->driveOwnsCamera() && !gl->m_driveCamera, QStringLiteral("NEW: il volo non c'e' piu'"));

    runDriveFlightFileScenarios();
    runDriveTouchScenarios();
    runDrivePadScenarios();
    runDriveControlsWindowScenarios();
    runDriveCollisionScenarios();
}

// GLI URTI DELLA GUIDA (drivecollision.h). Prima la geometria, sulla CPU, con
// una griglia piana: la sfera della camera si ferma a un raggio dal foglio,
// dalle due parti, ci scivola sopra, si allontana libera, prende gli spigoli,
// non salta il foglio con un passo lungo, segue la posa (posizione e scala).
// Poi la sonda della GPU sulla sfera unitaria: i nodi letti stanno sulla
// sfera; volando contro, la camera si ferma a un raggio e ci scivola; il tasto
// C spegne gli urti (la si attraversa) e li riaccende (da dentro, la parete
// ferma la camera); il cursore T rifa' le pose dell'urto. In una scena conta
// la posa dell'oggetto: una sfera spostata e rimpicciolita.
void ScenarioTest::runDriveCollisionScenarios()
{
    GLWidget *gl = m_mw->ui->glWidget;
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Guida libera (2.1): urti con le superfici, la camera ci scivola sopra =="));
    const float r = CameraDrive::kCollisionRadius;
    const float tol = 0.002f;
    // UN PAD VERO COLLEGATO (un DualSense in Bluetooth) scrive i suoi assi nei
    // comandi della guida quando l'app e' in primo piano: uno stick che deriva,
    // o toccato, fa virare la camera a meta' di un volo di prova (2026-11-26,
    // fallimenti a caso). Qui un pad finto a riposo.
    Gamepad *realPad = m_mw->m_gamepad;
    if (realPad) {
        GamepadState rest;
        rest.connected = true;
        rest.name = QStringLiteral("Test pad");
        realPad->setTestState(rest);
    }
    auto restorePad = qScopeGuard([realPad] {
        if (realPad) realPad->setTestState(std::nullopt);
    });

    // --- LA GEOMETRIA: il foglio z = 0, da -1 a 1, 10 x 10 celle ---------------
    {
        DriveCollider plane;
        DriveColliderPart part;
        part.nu = part.nv = 10;
        for (int i = 0; i <= 10; ++i) {
            for (int j = 0; j <= 10; ++j) {
                part.points.emplace_back(-1.0f + 0.2f * float(i), -1.0f + 0.2f * float(j), 0.0f);
                part.valid.push_back(1);
            }
        }
        plane.parts.push_back(std::move(part));
        plane.finalize();
        const std::vector<DrivePlacedCollider> world{ { &plane, QMatrix4x4() } };
        bool cHead = false, cOblique = false, cAway = true, cBeside = true, cEdge = false;
        const QVector3D head = driveSlide(world, QVector3D(0.1f, 0.1f, 0.5f), QVector3D(0.0f, 0.0f, -1.0f), r, &cHead);
        const QVector3D oblique = driveSlide(world, QVector3D(0.1f, 0.1f, 0.5f), QVector3D(0.5f, 0.0f, -1.0f), r, &cOblique);
        const QVector3D below = driveSlide(world, QVector3D(0.1f, 0.1f, -0.5f), QVector3D(0.0f, 0.0f, 1.0f), r);
        const QVector3D away = driveSlide(world, QVector3D(0.1f, 0.1f, 0.01f), QVector3D(0.0f, 0.0f, 0.5f), r, &cAway);
        const QVector3D beside = driveSlide(world, QVector3D(1.5f, 0.1f, 0.5f), QVector3D(0.0f, 0.0f, -1.0f), r, &cBeside);
        const QVector3D fast = driveSlide(world, QVector3D(0.1f, 0.1f, 5.0f), QVector3D(0.0f, 0.0f, -10.0f), r);
        // Il centro passa a 0.02 dallo spigolo x = 1: lo tocca e gli gira attorno.
        const QVector3D edge = driveSlide(world, QVector3D(1.02f, 0.1f, 0.5f), QVector3D(0.0f, 0.0f, -1.0f), r, &cEdge);
        const float edgeDist = plane.distance(edge);
        // Il foglio spostato a z = 1 e ingrandito due volte.
        QMatrix4x4 m;
        m.translate(0.0f, 0.0f, 1.0f);
        m.scale(2.0f);
        const std::vector<DrivePlacedCollider> placed{ { &plane, m } };
        const QVector3D scaled = driveSlide(placed, QVector3D(0.2f, 0.2f, 2.0f), QVector3D(0.0f, 0.0f, -2.0f), r);
        const float scaledDist = driveDistance(placed, scaled);
        // IL TAGLIO: la cella x, y in [0, 0.2] senza i suoi due triangoli; la
        // sfera (raggio 0.03) passa dal buco, accanto no.
        plane.parts[0].cut.assign(100, 0);
        plane.parts[0].cut[5 * 10 + 5] = 3;
        bool cHole = true;
        const QVector3D hole = driveSlide(world, QVector3D(0.1f, 0.1f, 0.5f), QVector3D(0.0f, 0.0f, -1.0f), r, &cHole);
        const QVector3D besideHole = driveSlide(world, QVector3D(0.3f, 0.1f, 0.5f), QVector3D(0.0f, 0.0f, -1.0f), r);
        check(!cHole && std::abs(hole.z() + 0.5f) < 1e-5f && std::abs(besideHole.z() - r) < tol,
              QStringLiteral("taglio: dal buco di una cella la camera passa (z %1), accanto si ferma (z %2)")
                  .arg(double(hole.z())).arg(double(besideHole.z())));
        plane.parts[0].cut.clear();
        // LA PARETE CHE SI MUOVE (Moving Torus Ride, 2026-11-26): il foglio sale
        // di 0.01 a passo verso la camera ferma, sopra di lui; poi scende. La
        // camera ne e' spinta e resta a un raggio; scendendo il foglio non la tira.
        QVector3D still(0.1f, 0.1f, 0.05f);
        float worstGap = 1e30f;
        bool pushedUp = false;
        QMatrix4x4 lift;
        for (int i = 1; i <= 30; ++i) {
            lift.setToIdentity();
            lift.translate(0.0f, 0.0f, 0.01f * float(i));
            const std::vector<DrivePlacedCollider> moving{ { &plane, lift } };
            bool pushed = false;
            still = driveSlide(moving, still, QVector3D(), r, &pushed);
            pushedUp = pushedUp || pushed;
            worstGap = std::min(worstGap, still.z() - 0.01f * float(i));
        }
        const float topZ = still.z();
        for (int i = 29; i >= 0; --i) {
            lift.setToIdentity();
            lift.translate(0.0f, 0.0f, 0.01f * float(i));
            still = driveSlide({ { &plane, lift } }, still, QVector3D(), r);
        }
        check(pushedUp && worstGap > r - tol && std::abs(topZ - 0.3f - r) < tol && std::abs(still.z() - topZ) < 1e-6f,
              QStringLiteral("parete in moto: il foglio sale contro la camera ferma e la spinge (sempre a %1 o piu', in "
                             "cima a z %2); scendendo non la tira (z %3)")
                  .arg(double(worstGap)).arg(double(topZ)).arg(double(still.z())));
        check(cHead && std::abs(head.z() - r) < tol && cOblique && std::abs(oblique.x() - 0.6f) < tol
                  && std::abs(oblique.z() - r) < tol && std::abs(below.z() + r) < tol && cAway
                  && std::abs(away.z() - 0.5f - r) < tol && !cBeside && std::abs(beside.z() + 0.5f) < 1e-5f
                  && std::abs(fast.z() - r) < tol && cEdge && edgeDist > r - tol && edge.z() < 0.03f
                  && std::abs(scaled.z() - 1.0f - r) < tol && std::abs(scaledDist - r) < tol,
              QStringLiteral("geometria su un foglio piano: contro si ferma a un raggio (z %1), di sbieco ci scivola "
                             "(x %2, z %3), da sotto anche (z %4), dentro il raggio torna a un raggio e poi "
                             "si allontana (z %5), di "
                             "fianco passa (z %6), un passo di 10 non lo salta (z %7), lo spigolo lo aggira (distanza "
                             "%8, z %9), il foglio spostato e ingrandito (z %10, distanza %11)")
                  .arg(double(head.z())).arg(double(oblique.x())).arg(double(oblique.z())).arg(double(below.z()))
                  .arg(double(away.z())).arg(double(beside.z())).arg(double(fast.z())).arg(double(edgeDist))
                  .arg(double(edge.z())).arg(double(scaled.z())).arg(double(scaledDist)));
    }

    // --- LA GEOMETRIA DEL CAMPO (implicite): la sfera |p|^2 - 1 su una griglia
    // 48^3 di meta' lato 0.8 attorno a (0, 0, 1.2).
    {
        DriveCollider ball;
        DriveFieldGrid &g = ball.field;
        g.center = QVector3D(0.0f, 0.0f, 1.2f);
        g.half = 0.8f;
        g.n = 48;
        g.values.resize(size_t(g.n) * size_t(g.n) * size_t(g.n));
        for (int i = 0; i < g.n; ++i)
            for (int j = 0; j < g.n; ++j)
                for (int k = 0; k < g.n; ++k)
                    g.values[size_t((i * g.n + j) * g.n + k)] = g.node(i, j, k).lengthSquared() - 1.0f;
        ball.finalize();
        const std::vector<DrivePlacedCollider> world{ { &ball, QMatrix4x4() } };
        bool cIn = false, cOut = true;
        const QVector3D in = driveSlide(world, QVector3D(0.05f, 0.0f, 1.9f), QVector3D(0.0f, 0.0f, -1.5f), r, &cIn);
        // Da dentro la parete ferma uguale; fuori dal cubo della griglia non si sa: niente urti.
        const QVector3D inside = driveSlide(world, QVector3D(0.05f, 0.0f, 0.6f), QVector3D(0.0f, 0.0f, 1.0f), r);
        // Attraverso la sfera in z = 0, sotto il cubo (z da 0.4 a 2).
        const QVector3D out = driveSlide(world, QVector3D(0.0f, 3.0f, 0.0f), QVector3D(0.0f, -2.5f, 0.0f), r, &cOut);
        const float dIn = ball.distance(in);
        check(cIn && std::abs(in.length() - 1.0f - r) < tol && std::abs(dIn - r) < tol
                  && std::abs(inside.length() - 1.0f + r) < tol && !cOut,
              QStringLiteral("geometria del campo (sfera su una griglia): da fuori si ferma a %1 dal centro (distanza "
                             "dalla superficie %2), da dentro a %3; fuori dal cubo della griglia nessun urto")
                  .arg(double(in.length())).arg(double(dIn)).arg(double(inside.length())));
        // LO SCIVOLAMENTO SU UNA PARETE CONCAVA (dentro la sfera, segnalato
        // dall'utente il 2026-11-26): premendo di sbieco contro la parete, passo
        // dopo passo, la camera ci scorre sopra. In linea retta lungo una parete
        // concava ci si avvicina a lei a ogni passo: senza riportarla a un raggio,
        // la camera ci entrava a poco a poco e usciva dalla sfera.
        QVector3D slider(0.0f, 0.0f, 0.8f);
        float worstOut = 0.0f;
        for (int i = 0; i < 400; ++i) {
            // Verso +z (contro la parete in alto) e di lato, in un giro: 0.02 a
            // passo (1.2 unita' al secondo). Resta sulla calotta, dentro la griglia.
            const QVector3D dir = QVector3D(std::cos(0.02f * float(i)), std::sin(0.02f * float(i)), 1.5f).normalized();
            slider = driveSlide(world, slider, dir * 0.02f, r);
            worstOut = std::max(worstOut, slider.length());
        }
        check(worstOut > 0.9f && worstOut < 1.0f - r + tol,
              QStringLiteral("scivolando dentro la sfera premuti contro la parete (400 passi) la camera resta dentro: "
                             "al massimo %1 dal centro (parete a %2)")
                  .arg(double(worstOut)).arg(double(1.0f - r)));
    }

    // --- LA SONDA DELLA GPU: la sfera unitaria --------------------------------
    const QString kSphere = QStringLiteral("surfaces/Parametric/Surfaces/Equations/R3/Sphere.json");
    CameraDrive *drive = m_mw->m_drive;
    DriveControls &c = drive->controls();
    const float dt = 1.0f / 60.0f;
    auto camPos = [&]() { return gl->cameraPlacement().column(3).toVector3D(); };
    auto camFront = [&]() { return -gl->cameraPlacement().column(2).toVector3D().normalized(); };
    auto sendKey = [&](QEvent::Type type, int key) {
        QKeyEvent e(type, key, Qt::NoModifier);
        QCoreApplication::sendEvent(gl, &e);
    };
    // La lettura della sonda arriva qualche frame dopo il disegno.
    auto waitProbe = [&](const SurfaceObject &o, int before) {
        for (int k = 0; k < 60; ++k) {
            if (o.driveProbe && o.driveProbe->arrived > before && !o.driveProbe->reading) return true;
            gl->update();
            wait(50);
        }
        return false;
    };
    if (!loadSurface(kSphere)) return;
    c.collisions = true;
    const SurfaceObject &sphere = *gl->m_objects.front();
    // A guida spenta la sonda non legge (l'oggetto puo' averne una dalle
    // superfici di prima, con la guida accesa); il suo collider non vale per
    // la sfera: niente urti finche' non rilegge.
    const int arrivedBefore = sphere.driveProbe ? sphere.driveProbe->arrived : 0;
    gl->update();
    wait(200);
    const bool noProbeBefore = (sphere.driveProbe ? sphere.driveProbe->arrived : 0) == arrivedBefore
                               && !(sphere.driveProbe && sphere.driveProbe->reading)
                               && gl->driveSurfaceDistance(QVector3D(0.0f, 0.0f, 1.0f)) > 1e30f;
    m_mw->m_btnDrive->click();
    const bool arrived = waitProbe(sphere, arrivedBefore);
    bool nodes = false;
    float worst = 0.0f;
    if (arrived) {
        const DriveCollider &col = sphere.driveProbe->collider;
        const MeshPart &mp = sphere.engine->getMeshParts().front();
        nodes = col.parts.size() == 1 && col.parts[0].nu == mp.numU && col.parts[0].nv == mp.numV;
        for (size_t k = 0; nodes && k < col.parts[0].points.size(); ++k)
            if (col.parts[0].valid[k]) worst = std::max(worst, std::abs(col.parts[0].points[k].length() - 1.0f));
    }
    check(noProbeBefore && arrived && nodes && worst < 1e-4f,
          QStringLiteral("sonda: a guida spenta non legge, e la superficie di prima non urta; accesa, i nodi della "
                         "griglia disegnata (%1 x %2) arrivano dalla GPU e stanno sulla sfera (scarto %3)")
              .arg(arrived ? sphere.driveProbe->collider.parts.front().nu : 0)
              .arg(arrived ? sphere.driveProbe->collider.parts.front().nv : 0)
              .arg(double(worst)));

    // Contro la sfera, a tutta: la camera si ferma a un raggio dalla superficie.
    const bool aimed = QVector3D::dotProduct(camFront(), -camPos().normalized()) > 0.999f;
    c.throttleUp = true;
    c.boost = true;
    float minR = camPos().length();
    for (int i = 0; i < 360; ++i) {
        drive->advance(dt, true);
        minR = std::min(minR, camPos().length());
    }
    const float stopR = camPos().length();
    const bool touching = drive->contact();
    const float pressedSpeed = drive->current().speed;
    const float tStop = gl->sceneTime();
    const QVector3D pStop = camPos();
    check(aimed && minR > 1.0f + r - tol && stopR < 1.0f + r + 0.005f && touching && pressedSpeed < 0.2f,
          QStringLiteral("contro la sfera a tutta (W e Maiuscole, 6 s): la camera si ferma a %1 dal centro (raggio "
                         "della camera %2, minimo %3), a contatto, e l'urto le toglie la velocita' (%4)")
              .arg(double(stopR)).arg(double(r)).arg(double(minR)).arg(double(pressedSpeed)));

    // Virando tenendo W: scivola sulla sfera senza entrarci.
    c.yawLeft = true;
    int slidSteps = 0;
    for (int i = 0; i < 120; ++i) {
        const QVector3D before = camPos();
        drive->advance(dt, true);
        minR = std::min(minR, camPos().length());
        if (drive->contact() && (camPos() - before).length() > 1e-4f) ++slidSteps;
    }
    c.yawLeft = false;
    const float slid = (camPos() - pStop).length();
    check(slidSteps > 20 && slid > 0.05f && minR > 1.0f + r - tol,
          QStringLiteral("virando con W tenuto la camera scivola sulla sfera: %1 passi a contatto in moto, %2 di "
                         "strada, mai dentro (minimo %3)")
              .arg(slidSteps).arg(double(slid)).arg(double(minR)));

    // C spegne gli urti: la sfera si attraversa. Riaccesi da dentro, la parete
    // ferma la camera dall'altra parte del foglio.
    c.yawLeft = false;
    c.boost = false;
    c.throttleUp = false;
    c.brake = true;
    for (int i = 0; i < 60; ++i) drive->advance(dt, true);
    c.brake = false;
    // Di nuovo verso il centro.
    for (int i = 0; i < 360 && QVector3D::dotProduct(camFront(), -camPos().normalized()) < 0.999f; ++i) {
        c.yawRight = true;
        drive->advance(dt, true);
    }
    c.yawRight = false;
    sendKey(QEvent::KeyPress, Qt::Key_C);
    sendKey(QEvent::KeyRelease, Qt::Key_C);
    const bool off = !c.collisions;
    c.throttleUp = true;
    float insideR = camPos().length();
    for (int i = 0; i < 120; ++i) {
        drive->advance(dt, true);
        insideR = std::min(insideR, camPos().length());
    }
    const bool hudOff = m_mw->m_driveHud && m_mw->m_driveHud->text().contains(QLatin1String("collisions off"));
    sendKey(QEvent::KeyPress, Qt::Key_C);
    sendKey(QEvent::KeyRelease, Qt::Key_C);
    const bool on = c.collisions;
    float maxInside = 0.0f;
    for (int i = 0; i < 240; ++i) {
        drive->advance(dt, true);
        maxInside = std::max(maxInside, camPos().length());
    }
    c.throttleUp = false;
    const bool hudOn = !m_mw->m_driveHud->text().contains(QLatin1String("collisions off"));
    check(off && hudOff && insideR < 0.9f && on && hudOn && maxInside < 1.0f - r + tol,
          QStringLiteral("C: urti spenti (indicatore \"collisions off\"), la camera entra nella sfera (%1 dal "
                         "centro); riaccesi, da dentro la parete la ferma (al massimo %2)")
              .arg(double(insideR)).arg(double(maxInside)));

    // Il volo registrato rifa' le pose dell'urto.
    m_mw->seekSceneTime(tStop);
    const float replayErr = (camPos() - pStop).length();
    check(replayErr < 1e-3f, QStringLiteral("il cursore T rifa' l'urto: al T del contatto la camera e' dove si era "
                                            "fermata (scarto %1)").arg(double(replayErr)));
    m_mw->stopDrive();

    // --- 4D: verso W la proiezione cambia, e la sonda la rilegge -------------
    // Clifford's Torus e' uno script: la sonda nasce anche dal suo vertex.
    if (loadSurface(QStringLiteral("surfaces/Parametric/Surfaces/Equations/S3/Clifford's Torus.json"))) {
        const SurfaceObject &torus = *gl->m_objects.front();
        const int before4D = torus.driveProbe ? torus.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        const bool first = waitProbe(torus, before4D);
        const int afterFirst = first ? torus.driveProbe->arrived : 0;
        const size_t k = first ? torus.driveProbe->collider.parts.front().points.size() / 3 : 0;
        const QVector3D node = first ? torus.driveProbe->collider.parts.front().points[k] : QVector3D();
        const bool movesW = m_mw->driveMovesW();
        c.wPlus = true;
        for (int i = 0; i < 60; ++i) drive->advance(dt, true);
        c.wPlus = false;
        for (int i = 0; i < 30; ++i) drive->advance(dt, true);
        const bool again = waitProbe(torus, afterFirst);
        const float moved = again ? (torus.driveProbe->collider.parts.front().points[k] - node).length() : 0.0f;
        check(first && movesW && again && moved > 1e-3f,
              QStringLiteral("4D (Clifford's Torus, uno script): la sonda legge la proiezione; verso W (X) la "
                             "proiezione cambia e la sonda la rilegge (un nodo si sposta di %1)")
                  .arg(double(moved)));
        m_mw->stopDrive();
    }

    // LA CAMERA CONTRO UN PUNTO della superficie (mondo), lungo `dir`: parte a
    // 0.8 da lui, tutta avanti per 4 s. La distanza dalla superficie piu'
    // vicina non scende mai sotto il raggio, e alla fine la tocca.
    struct Approach {
        bool aimed = false, contact = false;
        float minDist = 1e30f, endDist = 1e30f;
    };
    auto approach = [&](const QVector3D &target, const QVector3D &dir) {
        Approach a;
        const QVector3D d = dir.normalized();
        const QVector3D up = std::abs(d.y()) < 0.9f ? QVector3D(0.0f, 1.0f, 0.0f) : QVector3D(1.0f, 0.0f, 0.0f);
        c.releaseAll();
        c.brake = true;
        for (int i = 0; i < 60; ++i) drive->advance(dt, true);
        c.brake = false;
        QMatrix4x4 m;
        m.translate(target - d * 0.8f);
        m.rotate(QQuaternion::fromDirection(-d, up));
        gl->setEuclideanCameraPlacement(m);
        a.aimed = QVector3D::dotProduct(camFront(), d) > 0.999f;
        c.throttleUp = c.boost = true;
        for (int i = 0; i < 240; ++i) {
            drive->advance(dt, true);
            a.minDist = std::min(a.minDist, gl->driveSurfaceDistance(camPos()));
        }
        c.throttleUp = c.boost = false;
        a.endDist = gl->driveSurfaceDistance(camPos());
        a.contact = drive->contact();
        return a;
    };
    auto approachText = [](const Approach &a) {
        return QStringLiteral("distanza minima %1, alla fine %2, a contatto %3")
            .arg(double(a.minDist)).arg(double(a.endDist)).arg(a.contact ? 1 : 0);
    };
    auto approachOk = [&](const Approach &a) {
        return a.aimed && a.contact && a.minDist > r - tol && a.endDist < r + 0.01f;
    };

    // --- I TUBI ATTORNO A UNA CURVA (Solid Curves): il tubo intero, anelli del
    // raggio del tubo di ogni parte; con piu' tubi, ciascuno col suo blocco.
    for (const QString &rel : { QStringLiteral("surfaces/Parametric/Solid Curves/Single Curves/Trefoil Knot.json"),
                                QStringLiteral("surfaces/Parametric/Solid Curves/Multi Curves/Borromean Rings.json") }) {
        if (!loadSurface(rel)) continue;
        const SurfaceObject &tube = *gl->m_objects.front();
        const int before = tube.driveProbe ? tube.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        const bool arrivedTube = waitProbe(tube, before);
        bool rings = false;
        float ringErr = 1e30f;
        QStringList radii;
        Approach a;
        const std::vector<MeshPart> &mps = tube.engine->getMeshParts();
        if (arrivedTube && tube.driveProbe->kind == DriveProbe::Tube
            && tube.driveProbe->collider.parts.size() == mps.size()) {
            ringErr = 0.0f;
            rings = true;
            for (size_t k = 0; k < mps.size(); ++k) {
                const DriveColliderPart &part = tube.driveProbe->collider.parts[k];
                const float radius = tube.tubeRadius * (mps[k].tubeThickness >= 0.0f ? mps[k].tubeThickness : 1.0f);
                radii << QString::number(double(radius));
                const auto ringCenter = [&](int i) {
                    QVector3D s;
                    for (int j = 0; j < part.nv; ++j) s += part.points[size_t(i * (part.nv + 1) + j)];
                    return s / float(part.nv);
                };
                float err = 0.0f;
                for (int i = 0; i <= part.nu; i += std::max(1, part.nu / 16)) {
                    const QVector3D centre = ringCenter(i);
                    for (int j = 0; j <= part.nv; ++j)
                        err = std::max(err, std::abs((part.points[size_t(i * (part.nv + 1) + j)] - centre).length() - radius));
                }
                ringErr = std::max(ringErr, err / radius);
                rings = rings && part.nv > 2;
                if (k == 0) {
                    // Dal di fuori, lungo la normale di un anello.
                    const int i = part.nu / 3;
                    const QVector3D node = tube.model.map(part.points[size_t(i * (part.nv + 1))]);
                    a = approach(node, tube.model.map(ringCenter(i)) - node);
                }
            }
            rings = rings && ringErr < 0.02f;
        }
        check(arrivedTube && rings && approachOk(a),
              QStringLiteral("tubo (%1, %2 parti): la sonda legge il tubo, anelli di raggio %3 attorno alla curva "
                             "(scarto relativo %4); contro il tubo la camera si ferma sulla parete (%5)")
                  .arg(QFileInfo(rel).baseName()).arg(mps.size()).arg(radii.join(QStringLiteral(", ")))
                  .arg(double(ringErr)).arg(approachText(a)));
        m_mw->stopDrive();
    }

    // --- IL TAGLIO SULLA GPU: Hopf Tori con A = 2 taglia le mesh oltre la seconda
    if (loadSurface(QStringLiteral("surfaces/Parametric/Surfaces/Multi Meshes/Hopf Tori.json"))) {
        m_mw->ui->aSlider->setValue(200);
        wait(300);
        const SurfaceObject &hopf = *gl->m_objects.front();
        const int before = hopf.driveProbe ? hopf.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        const bool arrivedHopf = waitProbe(hopf, before);
        int whole = 0, none = 0, mixed = 0;
        QStringList parts;
        bool expected = arrivedHopf && hopf.driveProbe->cutCompiled;
        if (arrivedHopf) {
            const DriveCollider &col = hopf.driveProbe->collider;
            const std::vector<MeshPart> &mps = hopf.engine->getMeshParts();
            for (size_t k = 0; k < col.parts.size() && k < mps.size(); ++k) {
                const std::vector<unsigned char> &cut = col.parts[k].cut;
                const size_t n = size_t(std::count(cut.begin(), cut.end(), (unsigned char)3));
                const bool all = !cut.empty() && n == cut.size();
                const bool zero = !cut.empty() && std::count(cut.begin(), cut.end(), (unsigned char)0) == qsizetype(cut.size());
                all ? ++whole : zero ? ++none : ++mixed;
                parts << QStringLiteral("%1:%2").arg(mps[k].meshIndex).arg(all ? "tagliata" : zero ? "intera" : "a pezzi");
                expected = expected && (mps[k].meshIndex >= 2 ? all : zero);
            }
        }
        check(expected && whole > 0 && none == 2 && mixed == 0,
              QStringLiteral("taglio (Hopf Tori, A = 2): la sonda valuta cutHere e i triangoli delle mesh tagliate non "
                             "urtano (%1)").arg(parts.join(QStringLiteral(", "))));
        m_mw->stopDrive();
    }

    // --- UN FLUSSO GEODETICO: la mesh custom coi suoi triangoli ---------------
    if (loadSurface(QStringLiteral("surfaces/Parametric/Surfaces/Geodesic Flows/R3/Tangent Developable of a Helix.json"))) {
        const SurfaceObject &flow = *gl->m_objects.front();
        const int before = flow.driveProbe ? flow.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        const bool arrivedFlow = waitProbe(flow, before);
        bool same = false;
        float nodeErr = 1e30f;
        size_t triangles = 0;
        Approach a;
        if (arrivedFlow && flow.driveProbe->kind == DriveProbe::Custom && flow.driveProbe->collider.parts.size() == 1) {
            const DriveColliderPart &part = flow.driveProbe->collider.parts.front();
            const std::vector<Vertex> &verts = flow.engine->getVertices();
            const std::vector<unsigned int> &idx = flow.engine->getIndices();
            triangles = part.indices.size() / 3;
            // In R3 (w = 0, nessuna rotazione 4D) la proiezione lascia i punti dove sono.
            float maxW = 0.0f;
            nodeErr = 0.0f;
            for (size_t k = 0; k < verts.size() && k < part.points.size(); ++k) {
                maxW = std::max(maxW, std::abs(verts[k].position.w()));
                nodeErr = std::max(nodeErr, (part.points[k] - verts[k].position.toVector3D()).length());
            }
            same = part.points.size() == verts.size() && part.indices == idx && maxW < 1e-6f && nodeErr < 1e-4f;
            // Contro il triangolo di mezzo, lungo la sua normale.
            const size_t t = triangles / 2;
            const QVector3D p0 = flow.model.map(part.points[part.indices[3 * t]]);
            const QVector3D p1 = flow.model.map(part.points[part.indices[3 * t + 1]]);
            const QVector3D p2 = flow.model.map(part.points[part.indices[3 * t + 2]]);
            const QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0).normalized();
            a = approach((p0 + p1 + p2) / 3.0f, -n);
        }
        check(arrivedFlow && same && approachOk(a),
              QStringLiteral("flusso geodetico (Tangent Developable of a Helix): la sonda legge i vertici della mesh "
                             "custom (scarto %1) coi suoi %2 triangoli; contro la mesh la camera si ferma (%3)")
                  .arg(double(nodeErr)).arg(triangles).arg(approachText(a)));
        m_mw->stopDrive();
    }

    // --- LE IMPLICITE (ray marching): la sfera x^2 + y^2 + z^2 = 1 -----------
    // Il cubo della griglia segue la camera a ogni disegno: qui un frame per passo, come dal vivo.
    auto flyRendered = [&](int steps, const std::function<void()> &each) {
        for (int i = 0; i < steps; ++i) {
            drive->advance(dt, true);
            each();
            gl->update();
            wait(17);
        }
    };
    if (loadSurface(QStringLiteral("surfaces/Implicit/3D/Sphere.json"))) {
        const SurfaceObject &ball = *gl->m_objects.front();
        const int before = ball.driveProbe ? ball.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        const bool arrivedBall = waitProbe(ball, before);
        // Il campo letto e' quello dell'equazione: |p|^2 - 1 nei nodi.
        float fieldErr = 1e30f;
        if (arrivedBall && ball.driveProbe->kind == DriveProbe::Implicit && !ball.driveProbe->collider.field.empty()) {
            const DriveFieldGrid &g = ball.driveProbe->collider.field;
            fieldErr = 0.0f;
            for (int i = 0; i < g.n; i += 7)
                for (int j = 0; j < g.n; j += 7)
                    for (int k = 0; k < g.n; k += 7) {
                        const QVector3D p = g.node(i, j, k);
                        fieldErr = std::max(fieldErr, std::abs(g.values[size_t((i * g.n + j) * g.n + k)] - (p.lengthSquared() - 1.0f)));
                    }
        }
        // Contro la sfera, a tutta, con un disegno per passo.
        const bool aimed = QVector3D::dotProduct(camFront(), -camPos().normalized()) > 0.999f;
        const float startR = camPos().length();
        float minR = startR;
        int probes = ball.driveProbe ? ball.driveProbe->arrived : 0;
        c.throttleUp = c.boost = true;
        flyRendered(300, [&] { minR = std::min(minR, camPos().length()); });
        c.throttleUp = c.boost = false;
        probes = (ball.driveProbe ? ball.driveProbe->arrived : 0) - probes;
        const float stopR = camPos().length();
        const bool touching = drive->contact();
        // Davanti a un'implicita la camera si ferma dove il marcher la disegna ancora.
        const float rImplicit = CameraDrive::kMarcherNear + r;
        check(arrivedBall && fieldErr < 1e-3f && aimed && minR > 1.0f + rImplicit - tol
                  && stopR < 1.0f + rImplicit + 0.005f && touching,
              QStringLiteral("implicita (Sphere): la sonda legge il campo attorno alla camera (scarto dall'equazione "
                             "%1); da %2 contro la sfera la camera si ferma a %3 dal centro (minimo %4: il marcher "
                             "disegna da %5 in la', piu' il raggio della camera), a contatto; il cubo della griglia "
                             "l'ha seguita (%6 letture)")
                  .arg(double(fieldErr)).arg(double(startR)).arg(double(stopR)).arg(double(minR))
                  .arg(double(CameraDrive::kMarcherNear)).arg(probes));
        m_mw->stopDrive();
    }

    // --- LA SFERA DI DEFAULT DEL RAY MARCHING (a guscio), DA DENTRO ----------
    // Il caso segnalato dall'utente (2026-11-26): dentro la sfera con lo zoom
    // del mouse, poi DRIVE e avanti contro la parete: usciva.
    {
        pressNew();
        m_discardOnPrompt = true;
        m_mw->ui->tabModeSelector->setCurrentIndex(1);
        wait(800);
        m_mw->resetScene(1, /*loadDefaultSurface=*/true);   // la superficie di default, come all'avvio
        wait(1500);
        m_discardOnPrompt = false;
        const SurfaceObject &ball = *gl->m_objects.front();
        const bool shell = ball.engineMode == SurfaceObject::ModeImplicit && ball.eqImplicitF.contains(QLatin1String("x^2"));
        const float outside = gl->cameraPlacement().column(3).toVector3D().length();
        gl->zoomCamera(outside - 0.4f);   // la camera a 0.4 dal centro, che guarda il centro
        wait(200);
        const float insideStart = camPos().length();
        const int before = ball.driveProbe ? ball.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        const bool arrivedBall = waitProbe(ball, before);
        const float rImplicit = CameraDrive::kMarcherNear + r;
        float maxR = camPos().length();
        c.throttleUp = true;
        flyRendered(300, [&] { maxR = std::max(maxR, camPos().length()); });
        c.throttleUp = false;
        const float endR = camPos().length();
        // CIO' CHE SI VEDE: davanti, la parete. Il marcher non disegna piu' vicino
        // di kMarcherNear: fermata piu' vicina, al centro della vista c'era lo
        // sfondo, la camera "fuori" dalla sfera.
        gl->update();
        wait(100);
        const QImage frame = gl->grabFramebuffer();
        const QColor centre = frame.pixelColor(frame.width() / 2, frame.height() / 2);
        const QColor bg = QColor::fromRgbF(gl->m_bgColor.x(), gl->m_bgColor.y(), gl->m_bgColor.z());
        const int seen = std::abs(centre.red() - bg.red()) + std::abs(centre.green() - bg.green())
                         + std::abs(centre.blue() - bg.blue());
        check(shell && arrivedBall && insideStart < 0.5f && maxR < 1.0f - rImplicit + tol
                  && endR > 1.0f - rImplicit - 0.01f && seen > 30,
              QStringLiteral("sfera di default del ray marching, da dentro (camera a %1 dal centro): DRIVE e avanti, "
                             "la parete la ferma dove il marcher la disegna ancora (al massimo %2, alla fine %3) e al "
                             "centro della vista c'e' la parete, non lo sfondo (scarto dallo sfondo %4)")
                  .arg(double(insideStart)).arg(double(maxR)).arg(double(endR)).arg(seen));
        // Contro la parete, virando con W tenuto: a velocita' normale, poi con le
        // Maiuscole, poi beccheggiando. La camera scivola sulla parete, dentro.
        float slideMax = 0.0f;
        int slid = 0;
        const QVector3D wallStart = camPos();
        for (int phase = 0; phase < 3; ++phase) {
            c.throttleUp = true;
            c.yawLeft = phase < 2;
            c.boost = phase == 1;
            c.pitchUp = phase == 2;
            flyRendered(180, [&] {
                slideMax = std::max(slideMax, camPos().length());
                if (drive->contact()) ++slid;
            });
        }
        c.releaseAll();
        const float slideWay = (camPos() - wallStart).length();
        check(slideMax < 1.0f - rImplicit + tol && slid > 30 && slideWay > 0.1f,
              QStringLiteral("dentro la sfera di default, virando con W tenuto (anche con le Maiuscole e beccheggiando, "
                             "9 s): la camera scivola sulla parete (%1 passi a contatto, %2 di strada) senza uscire (al "
                             "massimo %3 dal centro)")
                  .arg(slid).arg(double(slideWay)).arg(double(slideMax)));
        m_mw->stopDrive();
        m_mw->ui->tabModeSelector->setCurrentIndex(0);
        wait(800);
    }

    // --- UNA SEZIONE 4D (Cross Sections): verso W cambia la sezione, e la sonda la rilegge
    if (loadSurface(QStringLiteral("surfaces/Implicit/Cross Sections/Duocylinder.json"))) {
        const SurfaceObject &duo = *gl->m_objects.front();
        const int before = duo.driveProbe ? duo.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        const bool first = waitProbe(duo, before);
        std::vector<float> values0 = first ? duo.driveProbe->collider.field.values : std::vector<float>();
        const QVector3D center0 = first ? duo.driveProbe->collider.field.center : QVector3D();
        const int afterFirst = first ? duo.driveProbe->arrived : 0;
        const bool movesW = m_mw->driveMovesW();
        c.wPlus = true;
        for (int i = 0; i < 60; ++i) drive->advance(dt, true);
        c.wPlus = false;
        for (int i = 0; i < 30; ++i) drive->advance(dt, true);
        const bool again = waitProbe(duo, afterFirst);
        float changed = 0.0f;
        if (again && duo.driveProbe->collider.field.values.size() == values0.size()
            && (duo.driveProbe->collider.field.center - center0).length() < 1e-6f) {
            for (size_t k = 0; k < values0.size(); ++k)
                if (std::abs(values0[k]) < 1e29f)
                    changed = std::max(changed, std::abs(duo.driveProbe->collider.field.values[k] - values0[k]));
        }
        check(first && movesW && again && changed > 1e-4f,
              QStringLiteral("sezione 4D (Duocylinder): verso W (X) la sezione cambia e la sonda la rilegge (il campo "
                             "nei nodi cambia fino a %1)").arg(double(changed)));
        m_mw->stopDrive();
    }

    // --- IN H3: la sfera (mesh, poi implicita) nel centro della scena ---------
    // La camera sull'asse z a 2 raggi dal centro, che guarda verso di lui; tutta
    // avanti. Si ferma a una distanza IPERBOLICA dal centro pari al raggio della
    // sfera (sigma = scala * raggi per unita': la mappa esponenziale conserva le
    // distanze dal centro) piu' il raggio della camera -- per l'implicita, piu'
    // i kMarcherNear del ray marcher.
    for (const QString &rel : { kSphere, QStringLiteral("surfaces/Implicit/3D/Sphere.json") }) {
        pressNew();
        m_discardOnPrompt = true;
        m_mw->startScene(MainWindow::SceneSpace::SpaceH3);
        wait(300);
        m_discardOnPrompt = false;
        m_mw->addShapeToScene(shapePath(rel));
        wait(800);
        if (!gl->hyperbolicSpace() || gl->objectCount() != 2) {
            check(false, QStringLiteral("H3: la scena con %1 non parte").arg(rel));
            continue;
        }
        const int index = gl->selectedObject();
        gl->setObjectPosition(QVector3D(0.0f, 0.0f, 0.0f));
        gl->setObjectScale(1.0f);
        gl->setSpinSpeed(0.0f);
        wait(200);
        const SurfaceObject &ball = *gl->m_objects[size_t(index)];
        const bool implicit = ball.engineMode == SurfaceObject::ModeImplicit;
        const double rpu = 1.0 / gl->hyperbolicCurvatureRadius();
        const double sigma = double(ball.scale) * rpu;
        const double rho = gl->driveH3Radius();
        const double expected = sigma + rho + (implicit ? CameraDrive::kMarcherNear * sigma : 0.0);
        const int before = ball.driveProbe ? ball.driveProbe->arrived : 0;
        m_mw->m_btnDrive->click();
        gl->setDriveHyperbolicCamera(H3::translation({ 0.0, 0.0, 2.0 }));
        const bool arrivedH3 = waitProbe(ball, before);
        const auto centreDistance = [&] { return H3::distance(H3::origin(), gl->hyperbolicCamera() * H3::origin()); };
        const double start = centreDistance();
        double minD = start;
        c.throttleUp = c.boost = true;
        flyRendered(300, [&] { minD = std::min(minD, centreDistance()); });
        c.throttleUp = c.boost = false;
        const double endD = centreDistance();
        const bool touching = drive->contact();
        const float surfaceDist = gl->driveSurfaceDistanceH3();
        check(arrivedH3 && std::abs(start - 2.0) < 1e-3 && minD > expected - 0.003 && endD < expected + 0.01 && touching,
              QStringLiteral("H3, %1: dalla camera a %2 raggi dal centro, tutta avanti, si ferma a %3 raggi (attesi "
                             "%4: sfera %5 + camera %6%7; minimo %8), a contatto; distanza dalla superficie %9 unita'")
                  .arg(implicit ? QStringLiteral("sfera implicita") : QStringLiteral("sfera"))
                  .arg(start).arg(endD).arg(expected).arg(sigma).arg(rho)
                  .arg(implicit ? QStringLiteral(" + marcher %1").arg(CameraDrive::kMarcherNear * sigma) : QString())
                  .arg(minD).arg(double(surfaceDist)));
        m_mw->stopDrive();
    }

    // --- MOVING TORUS RIDE (scena H3, segnalata dall'utente il 2026-11-26) ----
    // Il toro si muove su un path; sfera, nodo e perlina hanno lui per
    // riferimento. Con DRIVE si entrava e usciva dal toro.
    {
        pressNew();
        m_discardOnPrompt = true;
        const bool loaded = m_mw->loadSceneFile(m_root + QStringLiteral("/scenes/Moving Torus Ride.json"));
        wait(1500);
        m_discardOnPrompt = false;
        m_mw->m_btnDrive->click();
        for (int k = 0; k < 40; ++k) {
            gl->update();
            wait(50);
        }
        QStringList objects;
        for (size_t k = 0; k < gl->m_objects.size(); ++k) {
            const SurfaceObject &o = *gl->m_objects[k];
            objects << QStringLiteral("%1: sonda %2, letture %3, tipo %4, nodi %5, usabile %6")
                           .arg(k)
                           .arg(o.driveProbe ? 1 : 0)
                           .arg(o.driveProbe ? o.driveProbe->arrived : 0)
                           .arg(o.driveProbe ? int(o.driveProbe->kind) : -1)
                           .arg(o.driveProbe && !o.driveProbe->collider.parts.empty()
                                    ? o.driveProbe->collider.parts.front().points.size() : 0)
                           .arg(gl->driveColliderUsable(o) ? 1 : 0);
        }
        float minDist = 1e30f;
        int contacts = 0;
        c.throttleUp = true;
        flyRendered(360, [&] {
            minDist = std::min(minDist, gl->driveSurfaceDistanceH3());
            if (drive->contact()) ++contacts;
        });
        c.throttleUp = false;
        // Mai dentro piu' di meta' raggio: la perlina corre sul suo path a ~3
        // unita' al secondo, quasi un raggio della camera a frame, e fra un
        // passo e l'altro entra nella sfera della camera prima che la spinta la
        // riporti fuori (il limite di drivePushOut).
        const float rhoForward = float(gl->driveH3Radius() * gl->hyperbolicCurvatureRadius());
        check(loaded && gl->hyperbolicSpace() && minDist > 0.5f * rhoForward,
              QStringLiteral("Moving Torus Ride (H3): avanti 6 s fra gli oggetti in moto, mai dentro una superficie "
                             "piu' di meta' raggio (distanza minima %1, raggio %2, %3 passi a contatto) [%4]")
                  .arg(double(minDist)).arg(double(rhoForward)).arg(contacts).arg(objects.join(QStringLiteral("; "))));
        // CONTRO IL TUBO DEL TORO, da fuori: la camera a 0.6 (unita' del modello)
        // dall'equatore esterno, che lo guarda lungo la normale; tutta avanti.
        // Il toro intanto si muove sul suo path.
        int torus = -1;
        for (size_t k = 1; k < gl->m_objects.size(); ++k)
            if (gl->m_objects[k]->engine && gl->m_objects[k]->engine->getMeshParts().size() == 1
                && gl->m_objects[k]->scale > 2.0f)
                torus = int(k);
        if (torus > 0) {
            const SurfaceObject &t = *gl->m_objects[size_t(torus)];
            const double rpu = 1.0 / gl->hyperbolicCurvatureRadius();
            const double sigma = double(t.scale) * rpu;
            const double outer = double(t.mathParams.x() + t.mathParams.y());
            const H3::Matrix P = gl->hyperbolicObjectPlacement(torus);
            const H3::Vec4 target = P * H3::expAtOrigin({ outer * sigma, 0.0, 0.0 });
            const H3::Vec4 from = P * H3::expAtOrigin({ (outer + 0.6) * sigma, 0.0, 0.0 });
            gl->setDriveHyperbolicCamera(H3::frameTowards(from, target) * H3::rotation({ 0.0, 1.0, 0.0 }, M_PI));
            const float rhoScene = float(gl->driveH3Radius() * gl->hyperbolicCurvatureRadius());
            const float startDist = gl->driveSurfaceDistanceH3();
            float minTube = 1e30f;
            int tubeContacts = 0;
            c.throttleUp = c.boost = true;
            flyRendered(240, [&] {
                minTube = std::min(minTube, gl->driveSurfaceDistanceH3());
                if (drive->contact()) ++tubeContacts;
            });
            c.throttleUp = c.boost = false;
            check(tubeContacts > 10 && minTube > rhoScene - tol,
                  QStringLiteral("Moving Torus Ride (H3): contro il tubo del toro che si muove, la camera si ferma sulla "
                                 "parete (distanza dalla superficie: all'inizio %1, minima %2, raggio della camera %3; "
                                 "%4 passi a contatto)")
                      .arg(double(startDist)).arg(double(minTube)).arg(double(rhoScene)).arg(tubeContacts));
        } else {
            check(false, QStringLiteral("Moving Torus Ride: il toro non c'e'"));
        }
        m_mw->stopDrive();
    }

    // --- IN UNA SCENA: una sfera spostata (1.5, 0, 0) e rimpicciolita a meta' -
    if (!buildScene(false, { kSphere })) return;
    const int index = gl->selectedObject();
    gl->setObjectPosition(QVector3D(1.5f, 0.0f, 0.0f));
    gl->setObjectScale(0.5f);
    m_mw->selectSceneObject(0);
    gl->setCameraPos(QVector3D(1.5f, 0.0f, 4.0f));
    gl->setCameraYaw(0.0f);
    gl->setCameraPitch(0.0f);
    gl->setCameraRoll(0.0f);
    wait(100);
    const SurfaceObject &small = *gl->m_objects[size_t(index)];
    const int smallBefore = small.driveProbe ? small.driveProbe->arrived : 0;
    m_mw->m_btnDrive->click();
    const bool sceneArrived = waitProbe(small, smallBefore);
    const QVector3D center(1.5f, 0.0f, 0.0f);
    const bool sceneAimed = (camPos() - QVector3D(1.5f, 0.0f, 4.0f)).length() < 1e-3f
                            && QVector3D::dotProduct(camFront(), QVector3D(0.0f, 0.0f, -1.0f)) > 0.999f;
    c.throttleUp = true;
    c.boost = true;
    float sceneMin = (camPos() - center).length();
    for (int i = 0; i < 360; ++i) {
        drive->advance(dt, true);
        sceneMin = std::min(sceneMin, (camPos() - center).length());
    }
    c.throttleUp = c.boost = false;
    const float sceneStop = (camPos() - center).length();
    check(sceneArrived && sceneAimed && sceneMin > 0.5f + r - tol && sceneStop < 0.5f + r + 0.005f,
          QStringLiteral("scena: contro la sfera spostata e rimpicciolita la camera si ferma a %1 dal suo centro "
                         "(raggio 0.5 + %2, minimo %3)")
              .arg(double(sceneStop)).arg(double(r)).arg(double(sceneMin)));
    m_mw->stopDrive();
    pressNew();
}

// LA SONDA SU TUTTA LA LIBRERIA (sezione a parte, drive-collision-audit, fuori
// dalla suite: qualche minuto). Ogni superficie parametrica, a guida accesa:
// la sonda compila, il taglio con lei se il preset ne ha uno, la lettura
// arriva e porta nodi validi. Una sonda che non compila non si vede: quella
// superficie non fermerebbe la camera e basta.
void ScenarioTest::runDriveCollisionAuditScenarios()
{
    GLWidget *gl = m_mw->ui->glWidget;
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Guida libera (2.1): la sonda degli urti su tutte le superfici (parametriche e "
                                  "implicite) =="));
    QStringList files;
    for (const QString &dir : { QStringLiteral("/surfaces/Parametric"), QStringLiteral("/surfaces/Implicit") }) {
        QDirIterator it(m_root + dir, { QStringLiteral("*.json") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << QDir(m_root).relativeFilePath(it.next());
    }
    files.sort();
    m_mw->m_drive->controls().collisions = true;
    QStringList problems;
    int read = 0, tubes = 0, customs = 0, cuts = 0, implicits = 0, hyperbolic = 0;
    for (const QString &rel : std::as_const(files)) {
        if (!loadSurface(rel)) {
            problems << rel + QStringLiteral(": non si carica");
            continue;
        }
        // In H3 la camera non urta ancora: la sonda non gira.
        if (gl->hyperbolicSpace()) {
            ++hyperbolic;
            continue;
        }
        const SurfaceObject &o = *gl->m_objects.front();
        // Una lettura arrivata (una superficie animata ne chiede una a ogni frame:
        // non si aspetta che non ce ne sia una in corso).
        const auto waitArrival = [&](int before) {
            for (int k = 0; k < 80; ++k) {
                if (o.driveProbe && o.driveProbe->arrived > before) return true;
                gl->update();
                wait(50);
            }
            return false;
        };
        m_mw->m_btnDrive->click();
        bool arrived = waitArrival(o.driveProbe ? o.driveProbe->arrived : 0);
        // Un'implicita col cubo di taglio lontano dalla camera: attorno a lei non
        // c'e' superficie (il marcher non disegna fuori dal cubo). La camera al
        // centro del cubo, e si rilegge.
        const auto noValues = [&] {
            const std::vector<float> &values = o.driveProbe->collider.field.values;
            return std::none_of(values.begin(), values.end(), [](float v) { return std::abs(v) < 1e29f; });
        };
        if (arrived && o.driveProbe->kind == DriveProbe::Implicit && noValues()) {
            QMatrix4x4 m;
            m.translate(o.model.map(QVector3D((o.xMin + o.xMax) * 0.5f, (o.yMin + o.yMax) * 0.5f, (o.zMin + o.zMax) * 0.5f)));
            gl->setEuclideanCameraPlacement(m);
            arrived = waitArrival(o.driveProbe->arrived);
        }
        const bool hasCut = o.engine && !o.engine->getCutoutCodeGLSL().trimmed().isEmpty();
        if (!arrived) {
            problems << rel + QStringLiteral(": nessuna lettura (%1)")
                                  .arg(!o.driveProbe ? QStringLiteral("nessuna sonda")
                                       : o.driveProbe->failed ? QStringLiteral("non compilata")
                                                              : QStringLiteral("in attesa"));
        } else {
            ++read;
            const DriveProbe &p = *o.driveProbe;
            if (p.kind == DriveProbe::Tube) ++tubes;
            if (p.kind == DriveProbe::Custom) ++customs;
            if (p.kind == DriveProbe::Implicit) {
                // Il campo attorno alla camera: qualche nodo con un valore vero.
                ++implicits;
                if (noValues())
                    problems << rel + QStringLiteral(": campo senza valori (%1 nodi)").arg(p.collider.field.values.size());
            } else {
                if (hasCut && p.kind != DriveProbe::Custom) {
                    if (p.cutCompiled) ++cuts;
                    else problems << rel + QStringLiteral(": il taglio non compila nella sonda");
                }
                size_t valid = 0, nodes = 0;
                for (const DriveColliderPart &part : p.collider.parts) {
                    nodes += part.valid.size();
                    valid += size_t(std::count(part.valid.begin(), part.valid.end(), (unsigned char)1));
                }
                if (valid == 0) problems << rel + QStringLiteral(": nessun nodo valido (%1 nodi)").arg(nodes);
            }
        }
        m_mw->stopDrive();
    }
    check(problems.isEmpty() && read > 0,
          QStringLiteral("%1 superfici: la sonda legge %2 (implicite %6, tubi %3, mesh custom %4, col taglio %5; "
                         "in H3, ancora senza urti, %7)%8")
              .arg(files.size()).arg(read).arg(tubes).arg(customs).arg(cuts).arg(implicits).arg(hyperbolic)
              .arg(problems.isEmpty() ? QString() : QStringLiteral("; problemi: ") + problems.join(QStringLiteral("; "))));
    pressNew();
}

// LE FINESTRE DEI COMANDI NELLA 2.1 (Help, ControlsWindow): tutte e due hanno
// la sezione della guida libera, con le sue figure (controlswindow_drive.inc);
// nel Game Controller il triangolo accende DRIVE invece della proiezione. Che
// ogni figura delle pagine si disegni lo controlla gia' la sezione comune
// controls-windows.
void ScenarioTest::runDriveControlsWindowScenarios()
{
    m_lines.append(QString());
    m_lines.append(QStringLiteral("== Guida libera nelle finestre dei comandi (Help) =="));
    for (const auto kind : { ControlsWindow::MouseTouch, ControlsWindow::GameController }) {
        ControlsWindow *w = ControlsWindow::open(m_mw, kind);
        wait(300);
        const QString text = w->browser()->toPlainText();
        const QString html = w->browser()->toHtml();
        const QStringList drivePicts = { QStringLiteral("pict:drive-throttle@"), QStringLiteral("pict:drive-stop@") };
        bool hasPicts = true;
        for (const QString &pict : drivePicts) hasPicts = hasPicts && html.contains(pict);
        check(text.contains(QStringLiteral("Free flight (DRIVE)")) && hasPicts && w->missingPictograms().isEmpty(),
              QStringLiteral("%1: sezione della guida libera, figure del volo disegnate (mancanti: %2)")
                  .arg(ControlsWindow::title(kind), w->missingPictograms().join(QStringLiteral(", "))));
        if (kind == ControlsWindow::GameController) {
            check(text.contains(QStringLiteral("DRIVE on / off")) && !text.contains(QStringLiteral("Perspective / Orthographic")),
                  QStringLiteral("Game Controller: il triangolo accende e spegne DRIVE, non cambia la proiezione"));
        } else {
            check(html.contains(QStringLiteral("pict:key-ws@")) && html.contains(QStringLiteral("pict:thumb-right-h@")),
                  QStringLiteral("Mouse & Touch: tastiera, mouse e dita della guida libera"));
        }
        w->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        wait(150);
    }
}

// IL TOCCO (iPhone, iPad): gli stick che nascono sotto il dito, i tastini sulla
// vista e il doppio tocco, coi QTouchEvent veri che arrivano alla vista. Sul
// desktop i comandi a tocco si accendono a mano (m_driveTouchUiForced).
void ScenarioTest::runDriveTouchScenarios()
{
    GLWidget *gl = m_mw->ui->glWidget;
    CameraDrive *drive = m_mw->m_drive;
    DriveControls &c = drive->controls();
    const float dt = 1.0f / 60.0f;
    auto fly = [&](int frames) { for (int i = 0; i < frames; ++i) drive->advance(dt, true); };
    auto camPos = [&]() { return gl->cameraPlacement().column(3).toVector3D(); };
    auto camUp = [&]() { return gl->cameraPlacement().column(1).toVector3D().normalized(); };
    struct Finger { int id; QEventPoint::State state; QPointF at; };
    auto touch = [&](QEvent::Type type, const QList<Finger> &fingers) {
        QList<QEventPoint> points;
        for (const Finger &f : fingers) {
            QEventPoint pt(f.id, f.state, f.at, gl->mapToGlobal(f.at));
            QMutableEventPoint::setPosition(pt, f.at);
            points << pt;
        }
        QTouchEvent e(type, nullptr, Qt::NoModifier, points);
        QCoreApplication::sendEvent(gl, &e);
    };
    const qreal R = CameraDrive::kTouchStickRadius;
    const float kAxis = float((50.0 - 4.0) / (R - 4.0));   // 50 punti dallo stick, zona morta 4

    if (!loadSurface(QStringLiteral("surfaces/Parametric/Surfaces/Equations/R3/Torus.json"))) return;
    m_mw->m_driveTouchUiForced = true;
    m_mw->m_btnDrive->click();
    QWidget *overlay = gl->findChild<QWidget *>(QStringLiteral("driveTouchOverlay"));
    // Con un pad collegato davvero (un DualSense in Bluetooth) l'indicatore dice
    // i comandi del pad, che vincono sul tocco (refreshDriveHud): e' voluto.
    const bool padConnected = m_mw->m_gamepad && m_mw->m_gamepad->isConnected();
    const QString legend = padConnected ? QStringLiteral("Left stick") : QStringLiteral("Left thumb");
    check(m_mw->driveRunning() && gl->driveTouchButtons() == CameraDrive::TouchButtonCount && overlay
              && overlay->isVisible() && m_mw->m_driveHud->text().contains(legend),
          QStringLiteral("DRIVE coi comandi a tocco: stick e cinque tastini (urti, su, giu', W+, W-) disegnati sulla "
                         "vista, l'indicatore dice i comandi %1 (guida %2, tastini %3, pannello %4)")
              .arg(padConnected ? QStringLiteral("del pad collegato") : QStringLiteral("a tocco"))
              .arg(m_mw->driveRunning() ? 1 : 0).arg(gl->driveTouchButtons())
              .arg(!overlay ? QStringLiteral("assente") : overlay->isVisible() ? QStringLiteral("visibile")
                                                                                  : QStringLiteral("nascosto")));

    // Lo stick sinistro, dove si appoggia il pollice: in su, la crociera sale.
    const QPointF left(gl->width() * 0.25, gl->height() * 0.6);
    touch(QEvent::TouchBegin, { { 1, QEventPoint::Pressed, left } });
    touch(QEvent::TouchUpdate, { { 1, QEventPoint::Updated, left + QPointF(0.0, -50.0) } });
    const float leftY = c.touchLeftY;
    const bool leftStick = c.touchLeft && c.touchLeftOrigin == left && std::abs(leftY + kAxis) < 1e-3f
                           && c.touchLeftX == 0.0f;
    const float cruise0 = drive->current().cruise;
    fly(60);
    const float cruise1 = drive->current().cruise;
    touch(QEvent::TouchEnd, { { 1, QEventPoint::Released, left + QPointF(0.0, -50.0) } });
    fly(1);
    check(leftStick && cruise1 > cruise0 + 0.2f && !c.touchLeft && std::abs(drive->current().cruise - cruise1) < 0.02f,
          QStringLiteral("stick sinistro in su di 50 punti (asse %1): la crociera sale (%2 -> %3) e resta lasciato "
                         "il dito").arg(double(leftY)).arg(double(cruise0)).arg(double(cruise1)));

    // I due pollici insieme: il destro verso destra rolla, il sinistro resta suo.
    const QPointF right(gl->width() * 0.6, gl->height() * 0.5);
    const QVector3D up0 = camUp();
    touch(QEvent::TouchBegin, { { 1, QEventPoint::Pressed, left } });
    touch(QEvent::TouchUpdate, { { 1, QEventPoint::Stationary, left }, { 2, QEventPoint::Pressed, right } });
    touch(QEvent::TouchUpdate, { { 1, QEventPoint::Stationary, left },
                                 { 2, QEventPoint::Updated, right + QPointF(50.0, 0.0) } });
    const bool both = c.touchLeft && c.touchRight && c.touchRightOrigin == right
                      && std::abs(c.touchRightX - kAxis) < 1e-3f && c.touchLeftX == 0.0f;
    fly(60);
    const float rolled = qRadiansToDegrees(std::acos(std::clamp(QVector3D::dotProduct(camUp(), up0), -1.0f, 1.0f)));
    touch(QEvent::TouchEnd, { { 1, QEventPoint::Released, left }, { 2, QEventPoint::Released, right } });
    check(both && rolled > 3.0f && !c.touchLeft && !c.touchRight,
          QStringLiteral("due pollici: lo stick destro a destra rolla la camera di %1 gradi in un secondo, il "
                         "sinistro resta suo; alzate le dita, niente resta premuto").arg(double(rolled)));

    // Il doppio tocco frena.
    {
        const QPointF at(gl->width() * 0.3, gl->height() * 0.4);
        touch(QEvent::TouchBegin, { { 3, QEventPoint::Pressed, at } });
        touch(QEvent::TouchEnd, { { 3, QEventPoint::Released, at } });
        touch(QEvent::TouchBegin, { { 4, QEventPoint::Pressed, at + QPointF(5.0, 5.0) } });
        const bool tapped = c.brakeTap;
        fly(1);
        touch(QEvent::TouchEnd, { { 4, QEventPoint::Released, at + QPointF(5.0, 5.0) } });
        check(tapped && !c.brakeTap && drive->current().cruise == 0.0f,
              QStringLiteral("doppio tocco: la crociera a zero, e il comando si consuma (crociera %1)")
                  .arg(double(drive->current().cruise)));
    }

    // Il tastino su: tenuto, la camera sale lungo il suo alto.
    {
        fly(90);   // si ferma
        const QPointF button = CameraDrive::touchButtonRect(CameraDrive::TouchLiftUp, gl->size()).center();
        const QVector3D p0 = camPos();
        const QVector3D upAxis = camUp();
        touch(QEvent::TouchBegin, { { 5, QEventPoint::Pressed, button } });
        const bool held = c.liftUp && !c.touchRight;
        fly(60);
        touch(QEvent::TouchEnd, { { 5, QEventPoint::Released, button } });
        const float rose = QVector3D::dotProduct(camPos() - p0, upAxis);
        check(held && !c.liftUp && rose > 0.1f,
              QStringLiteral("tastino su: tenuto (%1) la camera sale di %2 lungo il suo alto; lasciato, si rilascia")
                  .arg(held).arg(double(rose)));
    }

    // Il tastino in cima: un tocco spegne gli urti, uno li riaccende. E' un
    // interruttore: non tiene nulla, non e' uno stick, due tocchi non frenano.
    {
        fly(90);
        const QPointF button = CameraDrive::touchButtonRect(CameraDrive::TouchCollisions, gl->size()).center();
        const bool before = c.collisions;
        const float cruise0 = drive->current().cruise;
        touch(QEvent::TouchBegin, { { 6, QEventPoint::Pressed, button } });
        const bool offWhilePressed = !c.collisions;
        const bool noStick = !c.touchLeft && !c.touchRight && !c.any();
        touch(QEvent::TouchEnd, { { 6, QEventPoint::Released, button } });
        fly(1);
        const bool off = !c.collisions;
        const bool hudOff = m_mw->m_driveHud->text().contains(QLatin1String("collisions off"));
        // I tastini su un fondo scuro, come sulla scena (le immagini restano nel report).
        const auto columnShot = [&]() {
            const qreal dpr = overlay->devicePixelRatioF();
            QImage img((overlay->size() * dpr), QImage::Format_ARGB32_Premultiplied);
            img.setDevicePixelRatio(dpr);
            img.fill(QColor(40, 48, 60));
            overlay->render(&img);
            return img;
        };
        const QImage shot = columnShot();
        touch(QEvent::TouchBegin, { { 7, QEventPoint::Pressed, button + QPointF(3.0, 3.0) } });
        touch(QEvent::TouchEnd, { { 7, QEventPoint::Released, button + QPointF(3.0, 3.0) } });
        fly(1);
        const bool onAgain = c.collisions;
        const QImage shotOn = columnShot();
        // La sbarra (rossa) si vede solo a urti spenti.
        const auto reddish = [&](const QImage &img) {
            const QRectF r = CameraDrive::touchButtonRect(CameraDrive::TouchCollisions, gl->size());
            const qreal dpr = img.devicePixelRatio();
            int n = 0;
            for (int y = int(r.top() * dpr); y < int(r.bottom() * dpr); ++y)
                for (int x = int(r.left() * dpr); x < int(r.right() * dpr); ++x) {
                    const QColor px = img.pixelColor(x, y);
                    if (px.red() > 200 && px.green() < 160 && px.blue() < 140) ++n;
                }
            return n;
        };
        const int redOff = reddish(shot), redOn = reddish(shotOn);
        check(before && offWhilePressed && noStick && off && hudOff && onAgain && !c.brakeTap
                  && drive->current().cruise == cruise0 && redOff > 20 && redOn == 0,
              QStringLiteral("tastino degli urti: un tocco li spegne (indicatore \"collisions off\", tastino sbarrato: "
                             "%1 pixel rossi), un altro li riaccende (%2); niente stick, niente freno")
                  .arg(redOff).arg(redOn));
        shot.save(m_outDir + QStringLiteral("/drive_touch_collisions_off.png"));
        shotOn.save(m_outDir + QStringLiteral("/drive_touch_collisions_on.png"));
    }

    // La colonna sta anche in una vista bassa (un iPhone in orizzontale): i
    // tastini si rimpiccioliscono, e in una vista alta restano di 52 punti.
    {
        const QSizeF lowView(700.0, 300.0), tallView(1000.0, 800.0);
        const QRectF last = CameraDrive::touchButtonRect(CameraDrive::TouchButtonCount - 1, lowView);
        const QRectF first = CameraDrive::touchButtonRect(0, lowView);
        const QRectF tall = CameraDrive::touchButtonRect(0, tallView);
        check(first.top() >= 14.0 && last.bottom() <= lowView.height() - 14.0 + 1e-6 && first.width() >= 36.0
                  && tall.width() == 52.0,
              QStringLiteral("tastini in una vista alta 300: dal punto %1 al %2, larghi %3 (in una alta 800: %4)")
                  .arg(first.top()).arg(last.bottom()).arg(first.width()).arg(tall.width()));
    }

    m_mw->stopDrive();
    check(!overlay->isVisible() && gl->driveTouchButtons() == 0,
          QStringLiteral("guida spenta: stick e tastini spariscono, e il tocco torna alla scena"));
    m_mw->m_driveTouchUiForced = false;
}

// IL VOLO NEL FILE: Save di un record e di una scena H3 col volo, e di nuovo
// aperti. Il volo riparte da solo, la scena e' pulita, il cursore rifa' le
// pose del volo dal vivo (entro la tolleranza dello sfoltimento), e un volo
// riaperto e non toccato si riscrive identico.
void ScenarioTest::runDriveFlightFileScenarios()
{
    GLWidget *gl = m_mw->ui->glWidget;
    const QString kTorus = QStringLiteral("surfaces/Parametric/Surfaces/Equations/R3/Torus.json");
    const QString kSphere = QStringLiteral("surfaces/Parametric/Surfaces/Equations/R3/Sphere.json");
    CameraDrive *drive = m_mw->m_drive;
    DriveControls &c = drive->controls();
    const float dt = 1.0f / 60.0f;
    auto camPos = [&]() { return gl->cameraPlacement().column(3).toVector3D(); };
    auto camFront = [&]() { return -gl->cameraPlacement().column(2).toVector3D().normalized(); };
    auto text = [](const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); };
    auto writeJson = [&](const QString &path, const QJsonObject &o) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson()) > 0;
    };

    // --- UN RECORD -------------------------------------------------------------
    if (!loadSurface(kTorus)) return;
    m_mw->m_btnDrive->click();
    struct Shot { float t; QVector3D pos, front; H3::Matrix h3; };
    std::vector<Shot> shots;
    auto flyShooting = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            drive->advance(dt, true);
            if (i % 20 == 19) shots.push_back({ gl->sceneTime(), camPos(), camFront(), gl->hyperbolicCamera() });
        }
    };
    c.throttleUp = c.yawLeft = true;
    flyShooting(240);
    c.throttleUp = c.yawLeft = false;
    c.pitchUp = c.rollRight = true;
    flyShooting(120);
    c.pitchUp = c.rollRight = false;
    flyShooting(60);
    m_mw->stopDrive();
    const int samples = int(drive->track().size());
    PresetSerializer::MotionRunState run;
    const QJsonObject rec = m_mw->m_presetSerializer->buildMotionJson(QStringLiteral("Drive Flight"), run, false);
    const QJsonObject flight = rec.value(QStringLiteral("driveFlight")).toObject();
    const int poses = flight.value(QStringLiteral("poses")).toArray().size();
    const QString recPath = m_outDir + QStringLiteral("/drive-flight.json");
    check(rec.value(QStringLiteral("activeMotion")).toString() == QLatin1String("drive") && poses > 1
              && poses < samples / 2 && writeJson(recPath, rec),
          QStringLiteral("Save Record: moto \"drive\", il volo sfoltito (%1 pose per %2 campioni, %3 byte)")
              .arg(poses).arg(samples).arg(text(flight).size()));
    // Di nuovo aperto, dopo un altro preset.
    if (!loadSurface(kSphere)) return;
    LibraryManager lm;
    const LibraryItem item = lm.parseJson(recPath, LibraryType::Motion);
    m_mw->applyMotionExample(item);
    const bool replaying = m_mw->driveRunning() && m_mw->driveOwnsCamera() && !item.driveFlight.isEmpty();
    const bool cleanAtLoad = !m_mw->hasUnsavedWork();
    wait(300);   // il volo riparte da solo
    const bool cleanAfter = !m_mw->hasUnsavedWork();
    float err = 0.0f;
    for (const Shot &s : shots) {
        m_mw->seekSceneTime(s.t);
        err = std::max({ err, (camPos() - s.pos).length(), (camFront() - s.front).length() });
    }
    check(replaying && cleanAtLoad && cleanAfter && err < 2e-3f,
          QStringLiteral("record riaperto: la guida rifa' il volo da sola (%1), scena pulita (%2, %3), il cursore "
                         "ritrova le pose del volo dal vivo (scarto massimo %4)")
              .arg(replaying).arg(cleanAtLoad).arg(cleanAfter).arg(double(err)));
    const QJsonObject again = m_mw->m_presetSerializer->buildMotionJson(QStringLiteral("Drive Flight"), run, false)
                                  .value(QStringLiteral("driveFlight")).toObject();
    check(text(again) == text(item.driveFlight),
          QStringLiteral("Save del record riaperto e non toccato: lo stesso volo del file"));
    m_mw->stopDrive();

    // --- UNA SCENA H3 ------------------------------------------------------------
    pressNew();
    m_discardOnPrompt = true;
    m_mw->startScene(MainWindow::SceneSpace::SpaceH3);
    wait(300);
    m_discardOnPrompt = false;
    m_mw->addShapeToScene(shapePath(kSphere));
    wait(500);
    if (!gl->hyperbolicSpace() || gl->objectCount() != 2) {
        check(false, QStringLiteral("scena H3 con la sfera: non parte"));
        return;
    }
    m_mw->selectSceneObject(0);
    m_mw->m_btnDrive->click();
    shots.clear();
    c.throttleUp = c.yawRight = true;
    flyShooting(200);
    c.throttleUp = c.yawRight = false;
    c.pitchDown = true;
    flyShooting(100);
    c.pitchDown = false;
    m_mw->stopDrive();
    const QJsonObject sceneJson = m_mw->m_presetSerializer->buildSceneJson(QStringLiteral("Drive H3"), run, false);
    const QJsonObject sceneFlight = sceneJson.value(QStringLiteral("environment")).toObject()
                                        .value(QStringLiteral("driveFlight")).toObject();
    const QString scenePath = m_outDir + QStringLiteral("/drive-flight-h3-scene.json");
    check(sceneFlight.value(QStringLiteral("space")).toString() == QLatin1String("H3") && writeJson(scenePath, sceneJson),
          QStringLiteral("Save della scena H3: il volo nel record dell'ambiente, nello spazio H3 (%1 pose)")
              .arg(sceneFlight.value(QStringLiteral("poses")).toArray().size()));
    pressNew();
    m_discardOnPrompt = true;
    const bool loaded = m_mw->loadSceneFile(scenePath);
    m_discardOnPrompt = false;
    wait(300);
    const bool sceneReplaying = loaded && gl->hyperbolicSpace() && gl->objectCount() == 2 && m_mw->driveRunning();
    double h3Err = 0.0;
    for (const Shot &s : shots) {
        m_mw->seekSceneTime(s.t);
        const H3::Matrix &now = gl->hyperbolicCamera();
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) h3Err = std::max(h3Err, std::abs(now.m[i][j] - s.h3.m[i][j]));
    }
    check(sceneReplaying && h3Err < 2e-3,
          QStringLiteral("scena H3 riaperta: la guida rifa' il volo (%1), il cursore ritrova le camere del volo dal "
                         "vivo (scarto massimo %2)").arg(sceneReplaying).arg(h3Err));
    m_mw->stopDrive();
    pressNew();
}

// IL PAD IN DRIVE (gamepad.h), con uno stato finto: a ogni passo il pad scrive
// i comandi (InputHandler::handleGamepad) e la guida avanza dal vivo. La
// disposizione degli stick a tocco: il sinistro crociera e virata (la
// crociera allo stesso ritmo di W), il destro beccheggio e rollio, R2 e L2 su
// e giu' (come R). La deriva non e' un comando (scena pulita, volo registrato
// non interrotto), uno stick vero prende il posto del volo; un pad scollegato
// rilascia i comandi; a guida spenta torna a girare la vista.
void ScenarioTest::runDrivePadScenarios()
{
    GLWidget *gl = m_mw->ui->glWidget;
    CameraDrive *drive = m_mw->m_drive;
    Gamepad *pad = m_mw->m_gamepad;
    if (!pad) {
        check(false, QStringLiteral("il pad esiste (MainWindow::setupGamepad)"));
        return;
    }
    auto restorePad = qScopeGuard([pad] { pad->setTestState(std::nullopt); });
    DriveControls &c = drive->controls();
    const float dt = 1.0f / 60.0f;
    auto fly = [&](int frames) { for (int i = 0; i < frames; ++i) drive->advance(dt, true); };
    auto camPos = [&]() { return gl->cameraPlacement().column(3).toVector3D(); };
    auto camRight = [&]() { return gl->cameraPlacement().column(0).toVector3D().normalized(); };
    auto camUp = [&]() { return gl->cameraPlacement().column(1).toVector3D().normalized(); };
    auto camFront = [&]() { return -gl->cameraPlacement().column(2).toVector3D().normalized(); };
    auto state = [](const QPointF &left, const QPointF &right, float l2, float r2) {
        GamepadState s;
        s.connected = true;
        s.name = QStringLiteral("Test pad");
        s.leftStick = left;
        s.rightStick = right;
        s.leftTrigger = l2;
        s.rightTrigger = r2;
        return s;
    };
    const GamepadState rest = state(QPointF(), QPointF(), 0.0f, 0.0f);
    // `frames` passi col pad nello stato dato (il pad scrive, la guida
    // avanza), poi il pad a riposo per un passo: i comandi si rilasciano.
    auto flyPad = [&](const GamepadState &s, int frames) {
        pad->setTestState(s);
        for (int i = 0; i < frames; ++i) {
            pad->step(dt);
            drive->advance(dt, true);
        }
        pad->setTestState(rest);
        pad->step(dt);
    };
    // Ferma il veicolo: freno, poi l'inerzia si spegne.
    auto halt = [&] {
        c.brake = true;
        fly(120);
        c.brake = false;
        fly(120);
    };

    // Un pulsante premuto e lasciato (i fronti fra due passi).
    auto pressButton = [&](GamepadButton b) {
        GamepadState s = rest;
        s.buttons = 1u << int(b);
        pad->setTestState(s);
        pad->step(dt);
        pad->setTestState(rest);
        pad->step(dt);
        wait(150);
    };
    auto withButton = [&](GamepadState s, GamepadButton b) {
        s.buttons |= 1u << int(b);
        return s;
    };

    m_lines.append(QStringLiteral("-- il pad in DRIVE --"));
    if (!loadSurface(QStringLiteral("surfaces/Parametric/Surfaces/Equations/R3/Torus.json"))) return;
    pressButton(GamepadButton::Triangle);
    if (!m_mw->driveRunning()) {
        check(false, QStringLiteral("pad: \u25B3 non accende DRIVE"));
        return;
    }
    check(true, QStringLiteral("\u25B3 accende DRIVE da fuori, come il tasto"));
    m_mw->markWholeSceneClean();

    // La deriva di uno stick lasciato andare: non e' un comando.
    {
        const QVector3D p0 = camPos(), f0 = camFront();
        pad->setTestState(state(QPointF(0.1, -0.08), QPointF(-0.09, 0.1), 0.04f, 0.03f));
        pad->step(dt);
        const bool idle = !c.pad && !c.any();
        flyPad(state(QPointF(0.1, -0.08), QPointF(-0.09, 0.1), 0.04f, 0.03f), 60);
        check(idle && (camPos() - p0).length() < 1e-6f && (camFront() - f0).length() < 1e-6f
                  && drive->current().cruise == 0.0f && !m_mw->hasUnsavedWork(),
              QStringLiteral("pad a riposo con un po' di deriva (dentro la zona morta): nessun comando, la camera "
                             "resta ferma e la scena pulita"));
    }

    // Stick sinistro in su: la crociera sale allo stesso ritmo di W tenuto.
    {
        // Tutti e due da fermo: T non sta sulla griglia dei passi (la guida e'
        // partita col timer dal vivo) e la crociera letta e' interpolata fra
        // due campioni; da fermo la fase e' la stessa per pad e tasto.
        const float cruise0 = drive->current().cruise;
        flyPad(state(QPointF(0.0, 1.0), QPointF(), 0.0f, 0.0f), 60);
        const float byPad = drive->current().cruise - cruise0;
        const bool released = !c.pad && c.padLeftY == 0.0f;
        const bool dirty = m_mw->hasUnsavedWork();
        halt();
        const float key0 = drive->current().cruise;
        c.throttleUp = true;
        fly(60);
        c.throttleUp = false;
        const float byKey = drive->current().cruise - key0;
        check(byPad > 0.3f && std::abs(byPad - byKey) < 1e-5f && released && dirty,
              QStringLiteral("stick sinistro tutto in su per un secondo: la crociera sale di %1, come W tenuto (%2); "
                             "lasciato, si rilascia; la scena e' da salvare").arg(double(byPad)).arg(double(byKey)));
        // L3 tenuto: tutto piu' veloce, come le Maiuscole.
        fly(120);
        const float cruise = drive->current().cruise;
        flyPad(withButton(rest, GamepadButton::L3), 90);
        const float boosted = drive->current().speed;
        // Cross tenuto: il freno, e non e' lo START (la guida se lo tiene).
        const QString start0 = m_mw->m_btnStart->text();
        flyPad(withButton(rest, GamepadButton::Cross), 30);
        check(boosted > cruise * 1.5f && drive->current().cruise == 0.0f && !c.padBrake && !c.padBoost
                  && m_mw->m_btnStart->text() == start0 && m_mw->driveRunning(),
              QStringLiteral("L3 tenuto: la velocita' sale oltre la crociera (%1 -> %2); \u2715 tenuto frena (crociera "
                             "%3) e non preme START").arg(double(cruise)).arg(double(boosted))
                  .arg(double(drive->current().cruise)));
        halt();
        // La croce su e giu': le velocita' per due e a meta', come + e -.
        pressButton(GamepadButton::Up);
        const float doubled = c.speedScale;
        pressButton(GamepadButton::Down);
        pressButton(GamepadButton::Down);
        const float halved = c.speedScale;
        pressButton(GamepadButton::Up);
        check(doubled == 2.0f && halved == 0.5f && c.speedScale == 1.0f,
              QStringLiteral("croce \u2191 \u2193: le velocita' per due (%1) e a meta' (%2), come + e -")
                  .arg(double(doubled)).arg(double(halved)));
        // Il quadrato: gli urti spenti e riaccesi, come il tasto C. In DRIVE non
        // e' il Reset View di fuori: la camera resta dov'e'.
        halt();
        const bool before = c.collisions;
        const QVector3D p0 = camPos();
        pressButton(GamepadButton::Square);
        fly(1);
        const bool toggled = c.collisions != before;
        const bool hudOff = m_mw->m_driveHud->text().contains(QLatin1String("collisions off"));
        const bool legend = m_mw->m_driveHud->text().contains(QStringLiteral("\u25a1 collisions"));
        pressButton(GamepadButton::Square);
        fly(1);
        check(before && toggled && hudOff && legend && c.collisions
                  && !m_mw->m_driveHud->text().contains(QLatin1String("collisions off"))
                  && (camPos() - p0).length() < 1e-4f && m_mw->driveRunning(),
              QStringLiteral("\u25a1: gli urti spenti (indicatore \"collisions off\", legenda del pad %1) e riaccesi, "
                             "come C; la camera non torna all'inquadratura (spostata di %2)")
                  .arg(legend).arg(double((camPos() - p0).length())));
    }

    // Lo stick sinistro a destra vira a destra; il destro in su alza il muso,
    // a destra rolla a destra.
    {
        QVector3D f0 = camFront(), r0 = camRight(), u0 = camUp();
        flyPad(state(QPointF(1.0, 0.0), QPointF(), 0.0f, 0.0f), 30);
        const float yawed = QVector3D::dotProduct(camFront() - f0, r0);
        f0 = camFront(); u0 = camUp();
        flyPad(state(QPointF(), QPointF(0.0, 1.0), 0.0f, 0.0f), 30);
        const float pitched = QVector3D::dotProduct(camFront() - f0, u0);
        u0 = camUp(); r0 = camRight();
        flyPad(state(QPointF(), QPointF(1.0, 0.0), 0.0f, 0.0f), 30);
        const float rolled = QVector3D::dotProduct(camUp() - u0, r0);
        check(yawed > 0.05f && pitched > 0.05f && rolled > 0.05f,
              QStringLiteral("stick sinistro a destra: vira a destra (%1); destro in su: muso in su (%2); destro a "
                             "destra: rolla a destra (%3)").arg(double(yawed)).arg(double(pitched)).arg(double(rolled)));
        halt();
    }

    // I grilletti: R2 sale lungo l'alto della camera come R tenuto, L2 scende.
    {
        QVector3D p0 = camPos();
        const QVector3D up = camUp();
        flyPad(state(QPointF(), QPointF(), 0.0f, 1.0f), 60);
        const float byR2 = QVector3D::dotProduct(camPos() - p0, up);
        fly(240);   // l'inerzia di su e giu' si spegne
        p0 = camPos();
        c.liftUp = true;
        fly(60);
        c.liftUp = false;
        const float byKey = QVector3D::dotProduct(camPos() - p0, up);
        fly(240);
        p0 = camPos();
        flyPad(state(QPointF(), QPointF(), 1.0f, 0.0f), 60);
        const float byL2 = QVector3D::dotProduct(camPos() - p0, up);
        fly(240);
        check(byR2 > 0.1f && std::abs(byR2 - byKey) < 1e-3f && byL2 < -0.1f,
              QStringLiteral("R2 per un secondo: la camera sale di %1 lungo il suo alto, come R (%2); L2 scende (%3)")
                  .arg(double(byR2)).arg(double(byKey)).arg(double(byL2)));
    }

    // IL VOLO REGISTRATO: rifatto dal cursore, la deriva del pad non lo
    // interrompe; uno stick vero ne prende il posto e la traccia si tronca.
    {
        flyPad(state(QPointF(0.3, 0.8), QPointF(0.0, 0.4), 0.0f, 0.0f), 60);
        fly(60);
        const float tEnd = gl->sceneTime();
        m_mw->seekSceneTime(tEnd * 0.5f);
        const float endBefore = drive->trackEnd();
        flyPad(state(QPointF(0.1, -0.08), QPointF(-0.09, 0.1), 0.04f, 0.03f), 10);
        const bool kept = drive->replaying() && drive->trackEnd() == endBefore;
        const float tTake = gl->sceneTime();
        flyPad(state(QPointF(-1.0, 0.0), QPointF(), 0.0f, 0.0f), 5);
        const bool taken = !drive->replaying() && drive->trackEnd() < endBefore
                           && std::abs(drive->trackEnd() - gl->sceneTime()) <= CameraDrive::kStep + 1e-4f;
        check(kept && taken,
              QStringLiteral("volo registrato rifatto da %1 s: la deriva del pad non lo interrompe (%2); lo stick "
                             "vero ne prende il posto, la traccia si tronca e riparte dal pilota (%3)")
                  .arg(double(tTake)).arg(kept ? QStringLiteral("on") : QStringLiteral("off"))
                  .arg(taken ? QStringLiteral("on") : QStringLiteral("off")));
        halt();
    }

    // Il pad si scollega con lo stick tenuto: i comandi tornano a riposo.
    {
        pad->setTestState(state(QPointF(1.0, 0.0), QPointF(0.0, 1.0), 0.0f, 1.0f));
        pad->step(dt);
        const bool held = c.pad && c.padLeftX == 1.0f;
        emit pad->connectionChanged(false, QString());
        check(held && !c.pad && c.padLeftX == 0.0f && c.padRightY == 0.0f && c.padLift == 0.0f && !c.any(),
              QStringLiteral("pad scollegato con lo stick tenuto: i suoi comandi tornano a riposo"));
        pad->setTestState(rest);
    }

    // Circle spegne la guida (come Esc), Triangle la riaccende e la rispegne.
    pressButton(GamepadButton::Circle);
    const bool offByCircle = !m_mw->driveRunning() && m_mw->driveOwnsCamera();
    pressButton(GamepadButton::Triangle);
    const bool onAgain = m_mw->driveRunning();
    pressButton(GamepadButton::Triangle);
    check(offByCircle && onAgain && !m_mw->driveRunning(),
          QStringLiteral("\u25CB spegne DRIVE come Esc, \u25B3 lo riaccende e lo rispegne"));

    // Guida spenta: il pad torna a girare la vista (2.0), i comandi restano liberi.
    m_mw->stopDrive();
    {
        const QQuaternion r0 = gl->getRotationQuat();
        pad->setTestState(state(QPointF(1.0, 0.0), QPointF(), 0.0f, 0.0f));
        for (int i = 0; i < 6; ++i) pad->step(0.02f);
        pad->setTestState(rest);
        check(!m_mw->driveRunning() && !(gl->getRotationQuat() == r0) && !c.pad,
              QStringLiteral("guida spenta: lo stick sinistro torna a girare la superficie, come il mouse"));
    }

    // Nel 4D: R1 tenuto va verso W+, L1 verso W-, come X e Z.
    if (loadSurface(QStringLiteral("surfaces/Parametric/Surfaces/Equations/S3/Clifford's Torus.json"))) {
        pressButton(GamepadButton::Triangle);
        const bool on = m_mw->driveRunning() && m_mw->driveMovesW();
        const float w0 = drive->current().observerW;
        flyPad(withButton(rest, GamepadButton::R1), 60);
        fly(60);
        const float w1 = drive->current().observerW;
        flyPad(withButton(rest, GamepadButton::L1), 60);
        fly(60);
        const float w2 = drive->current().observerW;
        pressButton(GamepadButton::Triangle);
        check(on && w1 - w0 > 0.1f && w2 < w1 - 0.1f && !c.padWPlus && !c.padWMinus && !m_mw->driveRunning(),
              QStringLiteral("4D: R1 tenuto verso W+ (%1 -> %2), L1 verso W- (%3)")
                  .arg(double(w0)).arg(double(w1)).arg(double(w2)));
    }
}
