#include "audiocontroller.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "synthesizer.h"

#include <QRegularExpression>
#include <QUrl>
#include <QFile>
#include <QDebug>

AudioController::AudioController(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.5);
    m_player->setLoops(QMediaPlayer::Infinite);

    m_synth = new Synthesizer(m_mainWindow);
}

void AudioController::playMusic(const QString &filePath)
{
    if (!QFile::exists(filePath)) return;

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->stop();
    }

    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_player->setLoops(QMediaPlayer::Infinite);
    m_player->play();
}

void AudioController::stopAll()
{
    if (m_player && m_player->playbackState() != QMediaPlayer::StoppedState) {
        m_player->stop();
    }
    if (m_synth && m_synth->isOpen()) {
        m_synth->stop();
    }

    // Aggiorna la UI della MainWindow se siamo nella tab del suono
    if (m_mainWindow->ui->btnRunCurrentScript && m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound) {
        m_mainWindow->ui->btnRunCurrentScript->setText("Run Sound");
    }
}

bool AudioController::isPlaying() const
{
    bool playing = false;
    if (m_synth && m_synth->isOpen()) playing = true;
    if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState) playing = true;
    return playing;
}

bool AudioController::playFromScript(const QString &scriptCode, QString *outError)
{
    if (scriptCode.trimmed().isEmpty()) return true; // niente da suonare: non è un errore

    // 1. MUSICA MP3/WAV
    QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatch musicMatch = musicRe.match(scriptCode);
    if (musicMatch.hasMatch()) {
        if (m_synth) m_synth->stop();
        QString newMusicPath = musicMatch.captured(1).trimmed();
        QString currentPlaying = (m_player->playbackState() == QMediaPlayer::PlayingState) ? m_player->source().toLocalFile() : "";
        if (currentPlaying != newMusicPath) {
            playMusic(newMusicPath);
        }
        if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound) {
            m_mainWindow->ui->btnRunCurrentScript->setText("Stop Sound");
        }
        return true;
    }

    // 2. SCRIPT AUDIO GPU
    QRegularExpression blockRe(R"(//SOUND_BEGIN(.*?)//SOUND_END)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch blockMatch = blockRe.match(scriptCode);
    if (blockMatch.hasMatch()) {
        QString glslCode = blockMatch.captured(1).trimmed();
        if (!glslCode.isEmpty()) {
            if (m_player) m_player->stop();
            if (m_synth) m_synth->stop();
            m_synth->setRhi(m_mainWindow->ui->glWidget->getRhi());

            if (m_synth->updateScript(glslCode, false)) {
                m_synth->start();
                if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound) {
                    m_mainWindow->ui->btnRunCurrentScript->setText("Stop Sound");
                }
                return true;
            } else {
                if (outError) *outError = m_synth->lastError();
                if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound) {
                    m_mainWindow->ui->btnRunCurrentScript->setText("Run Sound");
                }
                return false; // errore di compilazione audio
            }
        }
    }

    // 3. NESSUN TAG: ferma tutto
    stopAll();
    return true;
}

bool AudioController::validateScript(const QString &scriptCode, QString *outError)
{
    if (scriptCode.trimmed().isEmpty()) return true;

    // La musica MP3/WAV non si compila: niente da validare qui.
    QRegularExpression musicRe(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
    if (musicRe.match(scriptCode).hasMatch()) return true;

    // Script audio GPU: compila SENZA suonare (updateScript non avvia l'audio).
    QRegularExpression blockRe(R"(//SOUND_BEGIN(.*?)//SOUND_END)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch blockMatch = blockRe.match(scriptCode);
    if (blockMatch.hasMatch()) {
        QString glslCode = blockMatch.captured(1).trimmed();
        if (!glslCode.isEmpty()) {
            m_synth->setRhi(m_mainWindow->ui->glWidget->getRhi());
            if (!m_synth->updateScript(glslCode, false)) {
                if (outError) *outError = m_synth->lastError();
                return false;
            }
        }
    }
    return true; // nessun tag o compilazione ok
}

bool AudioController::saveSynthToRawFile(const QString &filePath, int durationSeconds)
{
    if (m_synth) {
        return m_synth->saveToRawFile(filePath, durationSeconds);
    }
    return false;
}
