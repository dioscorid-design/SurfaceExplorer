#ifndef NATIVEVIDEOENCODER_H
#define NATIVEVIDEOENCODER_H

#include <QString>
#include <QtGlobal>   // Q_OS_MACOS: senza questo la sezione macOS sparisce in silenzio

class QImage;

class NativeVideoEncoder {
public:
    // --- API one-shot (iOS): i frame sono gia' su disco come .raw ---
    static bool createMP4(const QString& framesDir, const QString& outputFile, int fps, int width, int height, const QString& audioFile = "");
    static void setKeepScreenOn(bool keepOn);

#ifdef Q_OS_MACOS
    // --- API incrementale (macOS): i frame arrivano dal loop, mai dal disco ---
    // Sostituisce la pipe verso ffmpeg: stessa struttura (apri, spingi ogni
    // frame, chiudi), ma l'encoder e' AVFoundation dentro il processo. Serve
    // perche' sotto sandbox App Store non si possono lanciare eseguibili
    // esterni al bundle.
    //
    // Uso: begin() una volta note W/H (primo frame), poi appendFrame() per ogni
    // frame nell'ordine, infine end() per chiudere il file.
    //
    // ATTENZIONE all'audio: la colonna sonora va passata QUI, gia' pronta su
    // disco (wav/mp3/m4a; vuoto = clip muta). Due vincoli di AVFoundation lo
    // impongono, ed entrambi si manifestano in modi che non sembrano audio:
    //   - un input non si puo' aggiungere a sessione avviata: dichiararlo dopo
    //     begin() da' un video MUTO senza alcun errore;
    //   - un input dichiarato e lasciato vuoto blocca l'interleaving: il writer
    //     smette di accettare VIDEO e l'export va in DEADLOCK al primo frame.
    // Per questo l'audio viene versato progressivamente da appendFrame, insieme
    // ai frame. Un brano piu' corto del video viene ripetuto (come il
    // "-stream_loop -1" di ffmpeg); uno piu' lungo e' tagliato a maxSeconds.
    //
    // maxSeconds: durata oltre cui tagliare l'audio. 0 = nessun limite noto in
    // anticipo (il taglio avviene comunque alla fine dei frame).
    //
    // Le dimensioni passate a begin() valgono per tutta la clip: appendFrame
    // rifiuta (return false) un frame di dimensioni diverse, invece di produrre
    // un video corrotto.
    static bool begin(const QString& outputFile, int fps, int width, int height,
                      const QString& audioFile = QString(), double maxSeconds = 0.0);
    static bool appendFrame(const QImage& frame);
    static bool end();
    // Chiude e scarta senza produrre il file (stop anticipato / errore).
    static void abort();
    // Ultimo errore leggibile, per il messaggio all'utente.
    static QString lastError();
#endif
};

#endif // NATIVEVIDEOENCODER_H
