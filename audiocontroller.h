#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QString>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QList>
#include <QStringList>

class MainWindow;
class Synthesizer;

class AudioController : public QObject
{
    Q_OBJECT
public:
    explicit AudioController(MainWindow *parent);

    // UNA VOCE PER OGGETTO (2.0): la voce k e' il suono dell'oggetto k della
    // scena (0 = l'ambiente), tutte in parallelo, ciascuna col suo player e il
    // suo sintetizzatore. Si creano al primo uso; quando un oggetto viene
    // tolto, removeVoice fa scorrere le voci come gli indici degli oggetti.
    // Ogni funzione senza voce lavora sulla 0, come quando il suono era uno solo.
    void stopAll();                  // tutte le voci
    void stopVoice(int voice);
    void removeVoice(int voice);

    // Ritorna false se il file non esiste/non e' leggibile, cosi' chi chiama
    // puo' avvisare invece di restare muto senza spiegazioni.
    bool playMusic(const QString &filePath, int voice = 0);

    // Analizza un testo, trova i tag e suona
    bool playFromScript(const QString &scriptCode, QString *outError = nullptr, int voice = 0);
    bool validateScript(const QString &scriptCode, QString *outError = nullptr, int voice = 0);

    // Verifica se qualcosa sta suonando: una voce qualsiasi, o quella data.
    bool isPlaying() const;
    bool isPlaying(int voice) const;
    QList<int> playingVoices() const;
    // Il testo contiene qualcosa da suonare: una riga //MUSIC: o un blocco
    // //SOUND_BEGIN..//SOUND_END non vuoto -- i casi in cui playFromScript suona.
    static bool containsAudio(const QString &scriptCode);
    // Il file della riga //MUSIC: del testo, se c'e'.
    static QString musicPathOf(const QString &scriptCode);

    bool saveSynthToRawFile(const QString &filePath, int durationSeconds, int voice = 0);
    // La colonna sonora di piu' voci sommate (raw float32 stereo 44.1 kHz, come
    // saveSynthToRawFile): per ogni voce il suo testo audio in `sources` (stesso
    // ordine di `voices`); gli script si renderizzano offline, i file //MUSIC:
    // si decodificano e si ripetono come fa il player.
    bool renderMixToRaw(const QList<int> &voices, const QStringList &sources,
                        int durationSeconds, const QString &filePath);

signals:
    // Cambia cio' che isPlaying() risponde. Per la musica arriva quando il
    // player cambia davvero stato -- in modo asincrono, un attimo dopo play() --
    // e per lo script audio quando il sintetizzatore parte o si ferma.
    void playingChanged();

private:
    struct Voice {
        QMediaPlayer *player = nullptr;
        QAudioOutput *output = nullptr;
        Synthesizer *synth = nullptr;
    };
    Voice *voiceAt(int voice);                    // la crea se manca
    const Voice *voiceIfAny(int voice) const;
    void syncScriptButton(int voice, bool playing);
    void destroyVoice(Voice &v);

    MainWindow *m_mainWindow;
    QList<Voice> m_voices;
};

#endif // AUDIOCONTROLLER_H
