#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QString>
#include <QMediaPlayer>
#include <QAudioOutput>

class MainWindow;
class Synthesizer;

class AudioController : public QObject
{
    Q_OBJECT
public:
    explicit AudioController(MainWindow *parent);

    void stopAll();
    // Ritorna false se il file non esiste/non e' leggibile, cosi' chi chiama
    // puo' avvisare invece di restare muto senza spiegazioni.
    bool playMusic(const QString &filePath);

    // Analizza un testo, trova i tag e suona
    bool playFromScript(const QString &scriptCode, QString *outError = nullptr);
    bool validateScript(const QString &scriptCode, QString *outError = nullptr);

    // Verifica se qualcosa sta suonando
    bool isPlaying() const;
    // Il testo contiene qualcosa da suonare: una riga //MUSIC: o un blocco
    // //SOUND_BEGIN..//SOUND_END non vuoto -- i casi in cui playFromScript suona.
    static bool containsAudio(const QString &scriptCode);

    bool saveSynthToRawFile(const QString &filePath, int durationSeconds);

signals:
    // Cambia cio' che isPlaying() risponde. Per la musica arriva quando il
    // player cambia davvero stato -- in modo asincrono, un attimo dopo play() --
    // e per lo script audio quando il sintetizzatore parte o si ferma.
    void playingChanged();

private:
    MainWindow *m_mainWindow;
    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    Synthesizer *m_synth;
};

#endif // AUDIOCONTROLLER_H
