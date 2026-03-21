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
    void playMusic(const QString &filePath);

    // Analizza un testo, trova i tag e suona
    void playFromScript(const QString &scriptCode);

    // Verifica se qualcosa sta suonando
    bool isPlaying() const;

    bool saveSynthToRawFile(const QString &filePath, int durationSeconds);

private:
    MainWindow *m_mainWindow;
    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    Synthesizer *m_synth;
};

#endif // AUDIOCONTROLLER_H
