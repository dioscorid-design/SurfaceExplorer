// scenariotest_drive.cpp - Gli scenari della guida libera (2.1): sezione
// "drive" di ScenarioTest. In un file suo: la 2.1 differisce dalla 2.0 solo
// per la guida, e cosi' la fusione da v2 di scenariotest.cpp resta pulita.
#include "scenariotest.h"

#include "mainwindow.h"
#include "mainwindow_p.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "cameradrive.h"
#include "controlswindow.h"
#include "gamepad.h"
#include "librarymanager.h"
#include "presetserializer.h"

#include <QFile>
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
    check(m_mw->driveRunning() && gl->driveTouchButtons() == 4 && overlay && overlay->isVisible()
              && m_mw->m_driveHud->text().contains(QLatin1String("Left thumb")),
          QStringLiteral("DRIVE coi comandi a tocco: stick e quattro tastini (su, giu', W+, W-) disegnati sulla "
                         "vista, l'indicatore dice i comandi a tocco"));

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
