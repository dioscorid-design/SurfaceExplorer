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

void AudioController::playFromScript(const QString &scriptCode)
{
    if (scriptCode.trimmed().isEmpty()) return;

    // 1. CERCA MUSICA MP3/WAV
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
        return;
    }

    // 2. CERCA SCRIPT AUDIO GPU (GLSL protetto dai tag)
    QRegularExpression blockRe(R"(//SOUND_BEGIN(.*?)//SOUND_END)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch blockMatch = blockRe.match(scriptCode);

    if (blockMatch.hasMatch()) {
        QString glslCode = blockMatch.captured(1).trimmed();
        if (!glslCode.isEmpty()) {
            if (m_player) m_player->stop();
            if (m_synth) m_synth->stop();

            // Passiamo 'false' perché passiamo direttamente la funzione GLSL pura!
            if (m_synth->updateScript(glslCode, false)) {
                m_synth->start();
                if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound) {
                    m_mainWindow->ui->btnRunCurrentScript->setText("Stop Sound");
                }
            } else {
                if (m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound) {
                    m_mainWindow->ui->btnRunCurrentScript->setText("Run Sound");
                }
            }
            return;
        }
    }

    // 3. SE NESSUN TAG È TROVATO, FERMA TUTTO
    stopAll();
}

bool AudioController::saveSynthToRawFile(const QString &filePath, int durationSeconds)
{
    if (m_synth) {
        return m_synth->saveToRawFile(filePath, durationSeconds);
    }
    return false;
}
