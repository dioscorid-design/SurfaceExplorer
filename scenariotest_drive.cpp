// scenariotest_drive.cpp - Gli scenari della guida libera (2.1): sezione
// "drive" di ScenarioTest. In un file suo: la 2.1 differisce dalla 2.0 solo
// per la guida, e cosi' la fusione da v2 di scenariotest.cpp resta pulita.
#include "scenariotest.h"

#include "mainwindow.h"
#include "mainwindow_p.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "cameradrive.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

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
}
