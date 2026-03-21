#include "videorecorder.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "glwidget.h"
#include "surfaceengine.h"
#include "synthesizer.h"
#include "audiocontroller.h"

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
    // 1. SALVATAGGIO STATO E STOP IMMEDIATO (FIX CONGELAMENTO)
    // ==============================================================
    bool wasPath4D = m_mainWindow->pathTimer->isActive();
    bool wasPath3D = m_mainWindow->pathTimer3D->isActive();
    bool wasAnimating = m_mainWindow->ui->glWidget->isAnimating();

    // ---> NUOVO: Salva e ferma l'animazione temporale ('t') <---
    bool wasTimeAnimating = false;
    if (m_mainWindow->m_btnStart && m_mainWindow->m_btnStart->text().toUpper() == "STOP") {
        wasTimeAnimating = true;
        m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
    }

    // Fermiamo tutto PRIMA di aprire finestre di dialogo
    if (wasPath4D) m_mainWindow->pathTimer->stop();
    if (wasPath3D) m_mainWindow->pathTimer3D->stop();
    m_mainWindow->ui->glWidget->stopAllTimers();
    m_mainWindow->ui->glWidget->pauseMotion();

    // Helper per ripristinare lo stato
    auto restoreState = [this, wasAnimating, wasPath4D, wasPath3D, wasTimeAnimating]() {
        if (wasAnimating) m_mainWindow->ui->glWidget->resumeMotion();
        if (wasPath4D) {
            m_mainWindow->pathTimer->start();
            m_mainWindow->ui->btnDeparture->setText("STOP");
        }
        if (wasPath3D) {
            m_mainWindow->pathTimer3D->start();
            m_mainWindow->ui->btnDeparture3D->setText("STOP");
        }
        // ---> NUOVO: Ripristina l'animazione temporale ('t') <---
        if (wasTimeAnimating) {
            m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
            m_mainWindow->ui->glWidget->startAnimationTimer();
        }
    };

    // ==============================================================
    // 2. INPUT UTENTE E PERCORSI SALVATAGGIO
    // ==============================================================
    QSettings settings;
    int defaultDuration = settings.value("lastRecDuration", 10).toInt();
    bool ok;
    int seconds = QInputDialog::getInt(m_mainWindow, "Render Video", "MAX video duration (seconds):", defaultDuration, 1, 600, 1, &ok);
    if (!ok) {
        restoreState();
        return;
    }
    settings.setValue("lastRecDuration", seconds);

    int defaultFPS = settings.value("lastRecFPS", 60).toInt();
    int fps = QInputDialog::getInt(m_mainWindow, "Frame Rate", "FPS (e.g. 30, 60):", defaultFPS, 24, 120, 1, &ok);
    if (!ok) {
        restoreState();
        return;
    }
    settings.setValue("lastRecFPS", fps);

    // ==============================================================
    // NUOVO: SELEZIONE DELLA RISOLUZIONE DEL VIDEO
    // ==============================================================
    QStringList resolutions = {
        "Monitor Default (Current Window Size)",
        "1080p Full HD (1920x1080)",
        "1440p 2K (2560x1440)",
        "2160p 4K (3840x2160)"
    };

    int defaultResIndex = settings.value("lastRecResIndex", 1).toInt();
    QString selectedRes = QInputDialog::getItem(m_mainWindow, "Video Resolution", "Select export resolution:", resolutions, defaultResIndex, false, &ok);
    if (!ok) {
        restoreState();
        return;
    }

    int resIndex = resolutions.indexOf(selectedRes);
    settings.setValue("lastRecResIndex", resIndex);

    // Calcoliamo la larghezza e l'altezza desiderate
    int targetWidth = -1;
    int targetHeight = -1;

    if (resIndex == 1) { targetWidth = 1920; targetHeight = 1080; }
    else if (resIndex == 2) { targetWidth = 2560; targetHeight = 1440; }
    else if (resIndex == 3) { targetWidth = 3840; targetHeight = 2160; }
    // Se resIndex == 0, lasciamo a -1 per usare la risoluzione nativa

    // --- Calcolo Root Libreria ---
    QString rootPath = settings.value("libraryRootPath").toString();
    if (rootPath.isEmpty()) rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer";

    // Dialogo Salvataggio MP4 (Default nella cartella Renders)
    QString defaultVideoDir = rootPath + "/Renders";
    QString lastVideoDir = settings.value("lastVideoDir", defaultVideoDir).toString();
    if (!QDir(lastVideoDir).exists()) lastVideoDir = defaultVideoDir;

    QString userSelectedFile = QFileDialog::getSaveFileName(m_mainWindow, "Save MP4 Video", lastVideoDir + "/video_output.mp4", "MP4 Video (*.mp4)");

    if (userSelectedFile.isEmpty()) {
        restoreState();
        return;
    }

    if (!userSelectedFile.endsWith(".mp4", Qt::CaseInsensitive)) userSelectedFile += ".mp4";
    settings.setValue("lastVideoDir", QFileInfo(userSelectedFile).absolutePath());

    // ==============================================================
    // 3. PREPARAZIONE REGISTRAZIONE
    // ==============================================================
    m_mainWindow->m_isRecording = true;
    m_mainWindow->m_stopRecordingRequested = false;

    // STOP AUDIO E ATTIVAZIONE TEMPO VIRTUALE
    // DOPO:
    m_mainWindow->m_audioController->stopAll();
    if (m_mainWindow->ui->glWidget) {
        m_mainWindow->ui->glWidget->setProperty("use_virtual_time", true);
        m_mainWindow->ui->glWidget->setProperty("virtual_time", 0.0f);
    }

    // Feedback visivo
    m_mainWindow->m_btnRec->setText("STOP");
    m_mainWindow->m_btnRec->setStyleSheet("QPushButton { color: white; background-color: red; font-weight: bold; border-radius: 4px; }");

    int totalFrames = seconds * fps;
    float timeStep = 1.0f / (float)fps;
    float fpsScale = 30.0f / (float)fps;

    QString targetPath = rootPath + "/Temp";
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

        float currentTime = i * timeStep;

        // >>> FIX 2: INVIA IL TEMPO VIRTUALE AL BACKGROUND <<<
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

            // Accessing the static private helper function of MainWindow
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

            float scale = m_mainWindow->ui->glWidget->getSurfaceScale();
            finalPos4D = finalPos4D * scale;
            finalTarget4D = finalTarget4D * scale;

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
            float scale = m_mainWindow->ui->glWidget->getSurfaceScale();

            QVector4D raw = engine->evaluatePath3DPosition(t);
            QVector3D curPos = raw.toVector3D() * scale;
            float roll = raw.w();

            // --- FIX PATH 3D: CENTER vs TANGENT ---
            QVector3D target;

            if (m_mainWindow->m_pathMode == MainWindow::ModeTangential) {
                // Guarda avanti (Tangent)
                QVector4D nextRaw = engine->evaluatePath3DPosition(t + 0.1f);
                target = nextRaw.toVector3D() * scale;
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

        QImage frame = m_mainWindow->ui->glWidget->getFrameForVideo();

        frame.setDevicePixelRatio(1.0);
        // ==============================================================
        // NUOVO: SCALATURA SENZA DEFORMAZIONI (LETTERBOXING)
        // ==============================================================
        if (targetWidth > 0 && targetHeight > 0) {
            // 1. Scala l'immagine mantenendo le proporzioni originali (niente stiramenti!)
            QImage scaledFrame = frame.scaled(targetWidth, targetHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            // 2. Crea una "tela" vuota della risoluzione perfetta (es. 1920x1080) e colorala di nero
            QImage finalFrame(targetWidth, targetHeight, QImage::Format_RGB32);
            finalFrame.fill(Qt::black); // Sfondo nero per riempire i vuoti

            // 3. "Incolla" il frame scalato esattamente al centro della tela nera
            QPainter painter(&finalFrame);
            int x = (targetWidth - scaledFrame.width()) / 2;
            int y = (targetHeight - scaledFrame.height()) / 2;
            painter.drawImage(x, y, scaledFrame);
            painter.end();

            frame = finalFrame;
        } else {
            // Risoluzione nativa del monitor: taglia i bordi se dispari (FFmpeg vuole numeri pari)
            if (frame.width() % 2 != 0) frame = frame.copy(0, 0, frame.width()-1, frame.height());
            if (frame.height() % 2 != 0) frame = frame.copy(0, 0, frame.width(), frame.height()-1);
        }

        QString fileName = QString("frame_%1.bmp").arg(i, 5, 10, QChar('0'));
        frame.save(m_mainWindow->m_recFolder + "/" + fileName, "BMP");

        m_mainWindow->m_renderProgress->setValue(i + 1);
        actualFramesRendered++;
    }

    // ==============================================================
    // 5. RIPRISTINO E PULIZIA
    // ==============================================================
    m_mainWindow->m_isRecording = false;

    // >>> FIX 3: DISATTIVAZIONE TEMPO VIRTUALE E RIAVVIO AUDIO <<<
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
    m_mainWindow->ui->glWidget->updateSurfaceData();
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

    QString videoFileName = userSelectedFile;
    QString inputPattern = m_mainWindow->m_recFolder + "/frame_%05d.bmp";
    QStringList arguments;

    arguments << "-y" << "-framerate" << QString::number(fps)
              << "-i" << inputPattern;

    if (hasAudio) {
        if (isRaw) arguments << "-f" << "f32le" << "-ar" << "44100" << "-ac" << "2" << "-i" << audioFile;
        else arguments << "-i" << audioFile;
    }

    arguments << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2"
              << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p" << "-crf" << "18";

    if (hasAudio) {
        arguments << "-c:a" << "aac" << "-b:a" << "192k" << "-shortest";
    }

    arguments << videoFileName;

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)

    m_mainWindow->m_isProcessingVideo = true;
    QProcess *ffmpegProcess = new QProcess(this);

#ifdef Q_OS_LINUX
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");
    ffmpegProcess->setProcessEnvironment(env);
#endif

    connect(ffmpegProcess, &QProcess::errorOccurred, this, [this, ffmpegProcess](QProcess::ProcessError err){
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QString errorMsg;
        if (err == QProcess::FailedToStart)
            errorMsg = "The 'ffmpeg' executable was not found.\nMake sure it is installed and in your PATH.";
        else
            errorMsg = "Error during FFmpeg execution.";

        QMessageBox::critical(m_mainWindow, "Video Creation Error", errorMsg);
        ffmpegProcess->deleteLater();
    });

    connect(ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, videoFileName, ffmpegProcess](int exitCode, QProcess::ExitStatus status){
                m_mainWindow->m_isProcessingVideo = false;
                m_mainWindow->m_statusLabel->clear();

                if (status == QProcess::NormalExit && exitCode == 0) {
                    QDir dir(m_mainWindow->m_recFolder);
                    dir.setNameFilters(QStringList() << "*.bmp" << "*.raw");
                    for (const QString &f : dir.entryList(QDir::Files)) dir.remove(f);
                    dir.rmdir(m_mainWindow->m_recFolder);

                    QMessageBox::information(m_mainWindow, "Finished!", "Video successfully saved:\n" + videoFileName);
                } else {
                    QString log = ffmpegProcess->readAllStandardError();
                    QMessageBox::warning(m_mainWindow, "Encoding Error",
                                         "FFmpeg exited with an error.\nExit code: " + QString::number(exitCode) +
                                             "\n\nLog:\n" + log.right(500));
                }
                ffmpegProcess->deleteLater();
            });

    QString program = QStandardPaths::findExecutable("ffmpeg");
    if (program.isEmpty()) {
        QStringList candidates;
#ifdef Q_OS_WIN
        candidates << "C:/ffmpeg/bin/ffmpeg.exe" << "C:/Program Files/ffmpeg/bin/ffmpeg.exe";
#else
        candidates << "/opt/homebrew/bin/ffmpeg" << "/usr/local/bin/ffmpeg" << "/usr/bin/ffmpeg";
#endif
        for(const QString &path : candidates) {
            if (QFile::exists(path)) {
                program = path;
                break;
            }
        }
    }

    if (program.isEmpty()) {
        m_mainWindow->m_isProcessingVideo = false;
        m_mainWindow->m_statusLabel->clear();
        QMessageBox::critical(m_mainWindow, "Missing FFmpeg",
                              "Cannot find 'ffmpeg'.\n\n"
                              "1. Install it and make sure it is in your PATH.");
        return;
    }

    ffmpegProcess->start(program, arguments);

#else
    QMessageBox::information(m_mainWindow, "Export Frames",
                             "On iOS/Android, automatic video assembly (FFmpeg) is not supported.\n"
                             "Frames saved in:\n" + m_mainWindow->m_recFolder);
    m_mainWindow->m_statusLabel->clear();
#endif
}
