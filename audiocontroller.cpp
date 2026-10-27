#include "audiocontroller.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "synthesizer.h"

#include <QRegularExpression>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryFile>
#include <algorithm>

// I due tag dell'audio, per playFromScript, validateScript e containsAudio.
static const QRegularExpression &musicTagRe()
{
    static const QRegularExpression re(R"(^\s*//MUSIC:\s*(.*)$)", QRegularExpression::MultilineOption);
    return re;
}

static const QRegularExpression &soundBlockRe()
{
    static const QRegularExpression re(R"(//SOUND_BEGIN(.*?)//SOUND_END)", QRegularExpression::DotMatchesEverythingOption);
    return re;
}

QString AudioController::musicPathOf(const QString &scriptCode)
{
    const QRegularExpressionMatch m = musicTagRe().match(scriptCode);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

bool AudioController::containsAudio(const QString &scriptCode)
{
    if (musicTagRe().match(scriptCode).hasMatch()) return true;
    const QRegularExpressionMatch m = soundBlockRe().match(scriptCode);
    return m.hasMatch() && !m.captured(1).trimmed().isEmpty();
}

AudioController::AudioController(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
    voiceAt(0);
}

AudioController::Voice *AudioController::voiceAt(int voice)
{
    if (voice < 0) voice = 0;
    while (m_voices.size() <= voice) m_voices.append(Voice());
    Voice &v = m_voices[voice];
    if (!v.synth) {
        v.player = new QMediaPlayer(this);
        v.output = new QAudioOutput(this);
        v.player->setAudioOutput(v.output);
        v.output->setVolume(0.5);
        v.player->setLoops(QMediaPlayer::Infinite);
        connect(v.player, &QMediaPlayer::playbackStateChanged, this, &AudioController::playingChanged);
        v.synth = new Synthesizer(m_mainWindow);
    }
    return &v;
}

const AudioController::Voice *AudioController::voiceIfAny(int voice) const
{
    if (voice < 0 || voice >= m_voices.size() || !m_voices[voice].synth) return nullptr;
    return &m_voices[voice];
}

// Il tasto Run/Stop Sound del dock e' quello dell'oggetto SELEZIONATO: le
// altre voci non lo toccano.
void AudioController::syncScriptButton(int voice, bool playing)
{
    if (voice != m_mainWindow->ui->glWidget->selectedObject()) return;
    if (m_mainWindow->ui->btnRunCurrentScript && m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound)
        m_mainWindow->ui->btnRunCurrentScript->setText(playing ? "Stop Sound" : "Run Sound");
}

bool AudioController::playMusic(const QString &filePath, int voice)
{
    // File assente: si torna false e sara' il chiamante ad avvisare. Prima qui
    // c'era un return muto e il record partiva silenzioso senza dire nulla --
    // a differenza delle IMMAGINI mancanti, che hanno sempre avuto il loro
    // popup. Non basta exists(): sotto sandbox un file puo' esistere ed essere
    // illeggibile (stessa ragione di TextureCode::resolveImagePath).
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile() || !fi.isReadable()) return false;

    Voice *v = voiceAt(voice);
    if (v->player->playbackState() == QMediaPlayer::PlayingState) {
        v->player->stop();
    }

    v->player->setSource(QUrl::fromLocalFile(filePath));
    v->player->setLoops(QMediaPlayer::Infinite);
    v->player->play();
    return true;
}

void AudioController::stopVoice(int voice)
{
    const Voice *cv = voiceIfAny(voice);
    if (!cv) return;
    Voice &v = m_voices[voice];
    if (v.player->playbackState() != QMediaPlayer::StoppedState) v.player->stop();
    if (v.synth->isOpen()) {
        v.synth->stop();
        emit playingChanged();
    }
    syncScriptButton(voice, false);
}

void AudioController::stopAll()
{
    for (int k = 0; k < m_voices.size(); ++k) stopVoice(k);

    // Aggiorna la UI della MainWindow se siamo nella tab del suono
    if (m_mainWindow->ui->btnRunCurrentScript && m_mainWindow->m_currentScriptMode == MainWindow::ScriptModeSound) {
        m_mainWindow->ui->btnRunCurrentScript->setText("Run Sound");
    }
}

void AudioController::destroyVoice(Voice &v)
{
    if (!v.synth) return;
    v.player->stop();
    v.synth->stop();
    delete v.synth;
    delete v.player;
    delete v.output;
    v = Voice();
}

void AudioController::removeVoice(int voice)
{
    if (voice <= 0 || voice >= m_voices.size()) return;
    destroyVoice(m_voices[voice]);
    m_voices.removeAt(voice);
    emit playingChanged();
}

bool AudioController::isPlaying(int voice) const
{
    const Voice *v = voiceIfAny(voice);
    if (!v) return false;
    return v->synth->isOpen() || v->player->playbackState() == QMediaPlayer::PlayingState;
}

bool AudioController::isPlaying() const
{
    for (int k = 0; k < m_voices.size(); ++k)
        if (isPlaying(k)) return true;
    return false;
}

QList<int> AudioController::playingVoices() const
{
    QList<int> out;
    for (int k = 0; k < m_voices.size(); ++k)
        if (isPlaying(k)) out.append(k);
    return out;
}

bool AudioController::playFromScript(const QString &scriptCode, QString *outError, int voice)
{
    if (scriptCode.trimmed().isEmpty()) return true; // niente da suonare: non è un errore

    Voice *v = voiceAt(voice);

    // 1. MUSICA MP3/WAV
    QRegularExpressionMatch musicMatch = musicTagRe().match(scriptCode);
    if (musicMatch.hasMatch()) {
        if (v->synth) v->synth->stop();
        QString newMusicPath = musicMatch.captured(1).trimmed();
        QString currentPlaying = (v->player->playbackState() == QMediaPlayer::PlayingState) ? v->player->source().toLocalFile() : "";
        if (currentPlaying != newMusicPath) {
            if (!playMusic(newMusicPath, voice)) {
                // File audio mancante: non e' un errore di SINTASSI, quindi si
                // distingue col prefisso, che il chiamante riconosce per
                // mostrare l'avviso giusto (e non un "Syntax Error").
                if (outError) *outError = "MISSING_FILE|" + newMusicPath;
                syncScriptButton(voice, false);
                return false;
            }
        }
        syncScriptButton(voice, true);
        return true;
    }

    // 2. SCRIPT AUDIO GPU
    QRegularExpressionMatch blockMatch = soundBlockRe().match(scriptCode);
    if (blockMatch.hasMatch()) {
        QString glslCode = blockMatch.captured(1).trimmed();
        if (!glslCode.isEmpty()) {
            v->player->stop();
            v->synth->stop();
            v->synth->setRhi(m_mainWindow->ui->glWidget->getRhi());

            if (v->synth->updateScript(glslCode, false)) {
                v->synth->start();
                emit playingChanged();
                syncScriptButton(voice, true);
                return true;
            } else {
                if (outError) *outError = v->synth->lastError();
                syncScriptButton(voice, false);
                return false; // errore di compilazione audio
            }
        }
    }

    // 3. NESSUN TAG: ferma questa voce
    stopVoice(voice);
    return true;
}

bool AudioController::validateScript(const QString &scriptCode, QString *outError, int voice)
{
    if (scriptCode.trimmed().isEmpty()) return true;

    // La musica MP3/WAV non si compila: niente da validare qui.
    if (musicTagRe().match(scriptCode).hasMatch()) return true;

    // Script audio GPU: compila SENZA suonare (updateScript non avvia l'audio).
    QRegularExpressionMatch blockMatch = soundBlockRe().match(scriptCode);
    if (blockMatch.hasMatch()) {
        QString glslCode = blockMatch.captured(1).trimmed();
        if (!glslCode.isEmpty()) {
            Voice *v = voiceAt(voice);
            v->synth->setRhi(m_mainWindow->ui->glWidget->getRhi());
            if (!v->synth->updateScript(glslCode, false)) {
                if (outError) *outError = v->synth->lastError();
                return false;
            }
        }
    }
    return true; // nessun tag o compilazione ok
}

bool AudioController::saveSynthToRawFile(const QString &filePath, int durationSeconds, int voice)
{
    const Voice *v = voiceIfAny(voice);
    if (v && v->synth) {
        return v->synth->saveToRawFile(filePath, durationSeconds);
    }
    return false;
}

// Un file audio decodificato in float32 stereo 44.1 kHz, ripetuto fino a
// `frames` campioni (il player lo suona in loop).
static bool decodeMusicLooped(const QString &path, qint64 frames, QVector<float> &out)
{
    QAudioDecoder decoder;
    QAudioFormat fmt;
    fmt.setSampleRate(44100);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);
    decoder.setAudioFormat(fmt);
    decoder.setSource(QUrl::fromLocalFile(path));

    QVector<float> once;
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
        const QAudioBuffer b = decoder.read();
        if (!b.isValid() || b.format().sampleFormat() != QAudioFormat::Float) return;
        const float *p = b.constData<float>();
        once.append(QVector<float>(p, p + b.sampleCount()));
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, &QEventLoop::quit);
    // Una canzone lunga si decodifica molto piu' in fretta del tempo reale; il
    // limite serve solo a non restare appesi su un file che non finisce mai.
    guard.start(120000);
    decoder.start();
    loop.exec();
    guard.stop();
    if (once.isEmpty()) return false;

    const qint64 samples = frames * 2;
    out.resize(int(samples));
    for (qint64 i = 0; i < samples; ++i) out[int(i)] = once[int(i % once.size())];
    return true;
}

bool AudioController::renderMixToRaw(const QList<int> &voices, const QStringList &sources,
                                     int durationSeconds, const QString &filePath)
{
    const qint64 frames = qint64(std::max(1, durationSeconds)) * 44100;
    QVector<float> mix(int(frames * 2), 0.0f);
    bool any = false;
    for (int i = 0; i < voices.size() && i < sources.size(); ++i) {
        const QString &src = sources.at(i);
        if (src.contains(QLatin1String("mainSound"))) {
            QTemporaryFile tmp;
            if (!tmp.open()) continue;
            const QString tmpPath = tmp.fileName();
            tmp.close();
            if (!saveSynthToRawFile(tmpPath, durationSeconds, voices.at(i))) continue;
            QFile f(tmpPath);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.readAll();
            const float *p = reinterpret_cast<const float *>(raw.constData());
            const int n = std::min<int>(mix.size(), int(raw.size() / sizeof(float)));
            for (int k = 0; k < n; ++k) mix[k] += p[k];
            any = true;
        } else {
            const QString music = musicPathOf(src);
            QVector<float> pcm;
            if (music.isEmpty() || !QFile::exists(music) || !decodeMusicLooped(music, frames, pcm)) continue;
            // Alla stessa altezza di quando suona dal vivo (volume del player).
            const float gain = 0.5f;
            for (int k = 0; k < mix.size() && k < pcm.size(); ++k) mix[k] += pcm[k] * gain;
            any = true;
        }
    }
    if (!any) return false;
    for (float &x : mix) x = std::clamp(x, -1.0f, 1.0f);
    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    out.write(reinterpret_cast<const char *>(mix.constData()), qint64(mix.size()) * sizeof(float));
    return true;
}
