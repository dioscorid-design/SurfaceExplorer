#include "nativevideoencoder.h"
#include <QtGlobal>

#ifdef Q_OS_MACOS

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <QImage>
#include <QString>
#include <QDebug>

// =============================================================================
// ENCODER NATIVO macOS (AVFoundation) — sostituisce ffmpeg su tutto macOS.
//
// PERCHE': sotto sandbox App Store non si possono lanciare eseguibili esterni
// al bundle, e il recorder cercava `ffmpeg` come binario di sistema (PATH,
// /opt/homebrew/bin, ...). Incorporare ffmpeg non e' una via: il progetto usa
// ffmpeg-kit-full-gpl e la GPL e' incompatibile con l'App Store.
//
// FORMA: API incrementale (begin/appendFrame/end) e non one-shot come il
// gemello iOS, perche' qui prendiamo il posto della PIPE: i frame arrivano dal
// loop di rendering e finiscono nel CVPixelBuffer senza mai toccare il disco.
// Il ramo iOS resta a file (.raw) e non e' toccato da questo lavoro.
//
// FORMATO PIXEL: i frame Qt sono Format_RGB32, che in memoria little-endian e'
// B,G,R,A — esattamente il layout kCVPixelFormatType_32BGRA del pixel buffer.
// Nessuna conversione, solo memcpy riga per riga (dstStride puo' avere padding).
// E' lo stesso formato che la pipe passava a ffmpeg con `-pix_fmt bgra`.
// =============================================================================

namespace {

// Stato dell'encoder in corso. Uno solo alla volta: il recorder registra una
// clip per volta e il loop e' sincrono sul thread principale.
struct MacEncoderState {
    AVAssetWriter                          *writer   = nil;
    AVAssetWriterInput                     *videoIn  = nil;
    AVAssetWriterInputPixelBufferAdaptor   *adaptor  = nil;

    // --- audio ---
    // Riempito PROGRESSIVAMENTE da appendFrame, non in blocco alla fine:
    // AVAssetWriter bilancia l'interleaving fra gli input e blocca il video se
    // l'audio resta indietro (o vuoto). Vedi pumpAudioUpTo().
    AVAssetWriterInput       *audioIn     = nil;
    AVAssetReader            *audioReader = nil;
    AVAssetReaderTrackOutput *audioOut    = nil;
    QString  audioPath;                       // per riaprire l'asset a ogni ripetizione
    CMTime   audioWritten   = kCMTimeZero;    // fin dove la traccia e' riempita
    CMTime   audioPassStart = kCMTimeZero;    // offset della ripetizione in corso
    bool     audioDone      = false;          // niente piu' da scrivere
    bool     audioFinished  = false;          // markAsFinished gia' chiamato
    CMTime   audioLimit     = kCMTimeInvalid; // durata oltre cui tagliare

    int      fps        = 30;
    int      width      = 0;
    int      height     = 0;
    long long frameIndex = 0;   // timestamp del prossimo frame (in unita' di 1/fps)
    bool     active     = false;
    QString  outputFile;
    QString  error;
};

MacEncoderState g_enc;

void resetState() {
    g_enc.writer = nil;
    g_enc.videoIn = nil;
    g_enc.adaptor = nil;
    g_enc.audioIn = nil;
    g_enc.audioReader = nil;
    g_enc.audioOut = nil;
    g_enc.audioPath.clear();
    g_enc.audioWritten = kCMTimeZero;
    g_enc.audioPassStart = kCMTimeZero;
    g_enc.audioDone = false;
    g_enc.audioFinished = false;
    g_enc.audioLimit = kCMTimeInvalid;
    g_enc.fps = 30;
    g_enc.width = 0;
    g_enc.height = 0;
    g_enc.frameIndex = 0;
    g_enc.active = false;
    g_enc.outputFile.clear();
    // NB: error NON si azzera qui — chi ha fallito deve poterlo ancora leggere.
}

// Apre (o riapre) il lettore audio. Serve a ogni ripetizione del brano: un
// AVAssetReader non si riavvolge.
bool openAudioReader() {
    if (g_enc.audioPath.isEmpty()) return false;

    NSURL *url = [NSURL fileURLWithPath:g_enc.audioPath.toNSString()];
    AVURLAsset *asset = [AVURLAsset assetWithURL:url];

    // loadTracksWithMediaType e' asincrona (la variante sincrona e' deprecata da
    // macOS 15). L'asset e' un file locale: la load e' immediata, aspettiamo.
    __block AVAssetTrack *track = nil;
    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [asset loadTracksWithMediaType:AVMediaTypeAudio
                 completionHandler:^(NSArray<AVAssetTrack *> *tracks, NSError *err) {
        if (!err && tracks.count > 0) track = tracks.firstObject;
        dispatch_semaphore_signal(sema);
    }];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
    if (!track) return false;

    NSError *err = nil;
    AVAssetReader *reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
    if (!reader) return false;

    // outputSettings ESPLICITI, non nil. Con nil il lettore consegna i campioni
    // nel formato nativo del file e, su un WAV float32 come quello prodotto dal
    // sintetizzatore, restituisce pochi blocchi con presentation timestamp NaN.
    // Timestamp non numerici = la traccia audio non avanza mai, il writer
    // trattiene il VIDEO per bilanciare l'interleaving e l'export si pianta al
    // primo frame. Chiedendo PCM 16 bit interleaved i tempi tornano corretti.
    NSDictionary *readerSettings = @{
        AVFormatIDKey:               @(kAudioFormatLinearPCM),
        AVLinearPCMBitDepthKey:      @(16),
        AVLinearPCMIsFloatKey:       @(NO),
        AVLinearPCMIsBigEndianKey:   @(NO),
        AVLinearPCMIsNonInterleaved: @(NO),
        AVSampleRateKey:             @(44100.0),
        AVNumberOfChannelsKey:       @(2)
    };
    AVAssetReaderTrackOutput *out =
        [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:readerSettings];
    if (![reader canAddOutput:out]) return false;
    [reader addOutput:out];
    if (![reader startReading]) return false;

    g_enc.audioReader = reader;
    g_enc.audioOut    = out;
    return true;
}

// Attende che l'input sia pronto. expectsMediaDataInRealTime=NO fa si' che
// AVFoundation applichi backpressure quando l'encoder e' piu' lento del
// rendering: e' l'equivalente del waitForBytesWritten sulla pipe.
//
// Il timeout NON e' pignoleria: se l'interleaving si sbilancia, questa attesa
// non finisce da sola e l'export resta appeso per sempre (sintomo: la barra di
// avanzamento che si pianta a fine registrazione). Meglio un frame perso di
// un'app bloccata. Ritorna false se ha rinunciato.
bool waitUntilReady(AVAssetWriterInput *input, double timeoutSeconds = 10.0) {
    const NSTimeInterval deadline = [NSDate timeIntervalSinceReferenceDate] + timeoutSeconds;
    while (!input.readyForMoreMediaData) {
        if ([NSDate timeIntervalSinceReferenceDate] > deadline) return false;
        [NSThread sleepForTimeInterval:0.002];
    }
    return true;
}

// Chiude la traccia audio: da qui in poi il writer non l'aspetta piu' e lascia
// scorrere il VIDEO. Va chiamata appena l'audio e' esaurito o tagliato —
// lasciarla aperta e non alimentarla e' esattamente cio' che blocca l'export
// negli ultimi frame.
void finishAudioInput() {
    if (g_enc.audioIn && !g_enc.audioFinished) {
        [g_enc.audioIn markAsFinished];
        g_enc.audioFinished = true;
    }
    if (g_enc.audioReader && g_enc.audioReader.status == AVAssetReaderStatusReading)
        [g_enc.audioReader cancelReading];
    g_enc.audioReader = nil;
    g_enc.audioOut = nil;
}

// Versa audio finche' la traccia non copre `target` (o finche' l'input non e'
// piu' pronto). Chiamata a ogni frame: cosi' l'audio avanza INSIEME al video e
// il writer non blocca mai nessuno dei due.
//
// Il brano piu' corto del video si RIPETE: e' quello che faceva
// "-stream_loop -1" nel ramo ffmpeg. Ogni ripetizione e' ritimbrata a partire
// dalla fine della precedente.
void pumpAudioUpTo(CMTime target) {
    if (!g_enc.audioIn || g_enc.audioDone) return;

    while (CMTimeCompare(g_enc.audioWritten, target) < 0) {
        if (!g_enc.audioIn.readyForMoreMediaData) return;   // riprenderemo al prossimo frame
        if (g_enc.writer.status == AVAssetWriterStatusFailed) { g_enc.audioDone = true; finishAudioInput(); return; }

        if (!g_enc.audioOut && !openAudioReader()) { g_enc.audioDone = true; finishAudioInput(); return; }

        CMSampleBufferRef sample = [g_enc.audioOut copyNextSampleBuffer];
        if (!sample) {
            // Brano finito: se non copre ancora il video, si ricomincia da capo
            // spostando l'offset. Se la passata non ha prodotto nulla, il file
            // e' vuoto: fermarsi, altrimenti e' un ciclo infinito.
            if (g_enc.audioReader.status == AVAssetReaderStatusReading)
                [g_enc.audioReader cancelReading];
            g_enc.audioReader = nil;
            g_enc.audioOut = nil;

            if (CMTimeCompare(g_enc.audioWritten, g_enc.audioPassStart) <= 0) {
                g_enc.audioDone = true;   // passata a vuoto
                finishAudioInput();
                return;
            }
            g_enc.audioPassStart = g_enc.audioWritten;
            continue;
        }

        // Sposta il campione in avanti di una ripetizione intera.
        CMSampleBufferRef shifted = NULL;
        if (CMTimeCompare(g_enc.audioPassStart, kCMTimeZero) == 0) {
            shifted = (CMSampleBufferRef)CFRetain(sample);
        } else {
            CMItemCount count = 0;
            CMSampleBufferGetSampleTimingInfoArray(sample, 0, NULL, &count);
            if (count > 0) {
                CMSampleTimingInfo *timings =
                    (CMSampleTimingInfo *)malloc(sizeof(CMSampleTimingInfo) * (size_t)count);
                if (timings) {
                    if (CMSampleBufferGetSampleTimingInfoArray(sample, count, timings, &count) == noErr) {
                        for (CMItemCount k = 0; k < count; ++k) {
                            timings[k].presentationTimeStamp =
                                CMTimeAdd(timings[k].presentationTimeStamp, g_enc.audioPassStart);
                            if (CMTIME_IS_VALID(timings[k].decodeTimeStamp)) {
                                timings[k].decodeTimeStamp =
                                    CMTimeAdd(timings[k].decodeTimeStamp, g_enc.audioPassStart);
                            }
                        }
                        CMSampleBufferCreateCopyWithNewTiming(kCFAllocatorDefault, sample,
                                                              count, timings, &shifted);
                    }
                    free(timings);
                }
            }
        }
        CFRelease(sample);
        if (!shifted) continue;

        CMTime t   = CMSampleBufferGetPresentationTimeStamp(shifted);
        CMTime dur = CMSampleBufferGetDuration(shifted);

        // Timestamp non numerico: la traccia non potrebbe piu' avanzare e il
        // writer tratterrebbe il video all'infinito. Meglio chiudere qui l'audio
        // (clip muta da questo punto) che piantare l'export.
        if (!CMTIME_IS_NUMERIC(t)) {
            CFRelease(shifted);
            g_enc.audioDone = true;
            finishAudioInput();
            return;
        }

        // Oltre la fine del video: taglio netto, come il "-t" di ffmpeg.
        // markAsFinished SUBITO (finishAudioInput): da qui il video ha ancora
        // frame da scrivere, e un input audio lasciato aperto e non piu'
        // alimentato blocca il writer sugli ultimi fotogrammi — era il blocco
        // al ~92% della barra di avanzamento.
        if (CMTIME_IS_NUMERIC(g_enc.audioLimit) && CMTimeCompare(t, g_enc.audioLimit) >= 0) {
            CFRelease(shifted);
            g_enc.audioDone = true;
            finishAudioInput();
            return;
        }

        [g_enc.audioIn appendSampleBuffer:shifted];
        g_enc.audioWritten = CMTIME_IS_NUMERIC(dur) ? CMTimeAdd(t, dur) : t;
        CFRelease(shifted);
    }
}


} // namespace

bool NativeVideoEncoder::begin(const QString& outputFile, int fps, int width, int height,
                               const QString& audioFile, double maxSeconds) {
    if (g_enc.active) {
        // Un encoder gia' aperto significa che una sessione precedente non e'
        // stata chiusa: la scartiamo invece di sovrascriverne lo stato a meta'.
        NativeVideoEncoder::abort();
    }
    resetState();
    g_enc.error.clear();

    if (width <= 0 || height <= 0 || (width % 2) != 0 || (height % 2) != 0) {
        g_enc.error = QStringLiteral("Invalid video size %1x%2 (must be positive and even).")
                          .arg(width).arg(height);
        return false;
    }

    NSString *outPath = outputFile.toNSString();
    NSURL *outURL = [NSURL fileURLWithPath:outPath];

    if ([[NSFileManager defaultManager] fileExistsAtPath:outPath]) {
        [[NSFileManager defaultManager] removeItemAtPath:outPath error:nil];
    }

    NSError *error = nil;
    AVAssetWriter *writer = [[AVAssetWriter alloc] initWithURL:outURL fileType:AVFileTypeMPEG4 error:&error];
    if (!writer || error) {
        g_enc.error = QStringLiteral("Cannot create the video file: %1")
                          .arg(QString::fromNSString(error ? error.localizedDescription : @"unknown error"));
        return false;
    }

    // Bitrate: stessa scelta del ramo iOS. H.264 impastava le linee sottili del
    // wireframe — la perdita e' strutturale, dovuta al chroma subsampling 4:2:0
    // (linee di colore quasi puro su fondo scuro). HEVC ricostruisce molto
    // meglio le alte frequenze del chroma, ed e' accelerato in hardware sui Mac
    // Apple Silicon. Teniamo un bpp alto per spremere il massimo dal 4:2:0.
    double bpp = 0.8;
    long long bitRate = (long long)((double)width * (double)height * (double)fps * bpp);
    const long long minBitRate = 30000000LL;   // 30 Mbps minimo
    if (bitRate < minBitRate) bitRate = minBitRate;

    NSDictionary *compression = @{
        AVVideoAverageBitRateKey: @(bitRate),
        AVVideoMaxKeyFrameIntervalKey: @(fps),   // almeno un keyframe al secondo
        AVVideoQualityKey: @(1.0)
    };

    NSDictionary *videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeHEVC,
        AVVideoWidthKey: @(width),
        AVVideoHeightKey: @(height),
        AVVideoCompressionPropertiesKey: compression
    };

    AVAssetWriterInput *videoIn =
        [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
    // NO = l'encoder puo' far aspettare il produttore (backpressure). Con YES
    // scarterebbe i frame quando non sta dietro: qui non stiamo catturando in
    // tempo reale, ogni frame deve entrare.
    videoIn.expectsMediaDataInRealTime = NO;

    if (![writer canAddInput:videoIn]) {
        g_enc.error = QStringLiteral("The video encoder rejected the requested format.");
        return false;
    }
    [writer addInput:videoIn];

    NSDictionary *sourcePixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (NSString *)kCVPixelBufferWidthKey: @(width),
        (NSString *)kCVPixelBufferHeightKey: @(height)
    };
    AVAssetWriterInputPixelBufferAdaptor *adaptor =
        [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoIn
                                                                        sourcePixelBufferAttributes:sourcePixelBufferAttributes];

    // --- TRACCIA AUDIO ---
    // Va aggiunta ORA: dopo startWriting AVFoundation rifiuta ogni nuovo input
    // (canAddInput restituisce NO) e il muxing successivo fallirebbe in
    // SILENZIO, dando un video perfetto ma senza audio.
    // Per questo la colonna sonora dev'essere gia' pronta su disco: la
    // dichiariamo solo se c'e' davvero, perche' un input dichiarato e lasciato
    // vuoto blocca l'interleaving e manda l'export in deadlock.
    AVAssetWriterInput *audioIn = nil;
    if (!audioFile.isEmpty()) {
        AudioChannelLayout acl;
        bzero(&acl, sizeof(acl));
        acl.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;

        NSDictionary *audioSettings = @{
            AVFormatIDKey:         @(kAudioFormatMPEG4AAC),
            AVNumberOfChannelsKey: @(2),
            AVSampleRateKey:       @(44100.0),
            AVEncoderBitRateKey:   @(192000),
            AVChannelLayoutKey:    [NSData dataWithBytes:&acl length:sizeof(acl)]
        };

        audioIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:audioSettings];
        audioIn.expectsMediaDataInRealTime = NO;
        if ([writer canAddInput:audioIn]) {
            [writer addInput:audioIn];
        } else {
            // Non e' fatale: meglio un video muto che nessun video.
            audioIn = nil;
        }
    }

    if (![writer startWriting]) {
        g_enc.error = QStringLiteral("Cannot start the video encoder: %1")
                          .arg(QString::fromNSString(writer.error ? writer.error.localizedDescription : @"unknown error"));
        return false;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];

    g_enc.writer     = writer;
    g_enc.videoIn    = videoIn;
    g_enc.audioIn    = audioIn;
    g_enc.audioPath  = audioIn ? audioFile : QString();
    g_enc.audioLimit = (maxSeconds > 0.0)
                         ? CMTimeMakeWithSeconds(maxSeconds, (fps > 0 ? fps : 30))
                         : kCMTimeInvalid;
    g_enc.adaptor    = adaptor;
    g_enc.fps        = (fps > 0 ? fps : 30);
    g_enc.width      = width;
    g_enc.height     = height;
    g_enc.frameIndex = 0;
    g_enc.outputFile = outputFile;
    g_enc.active     = true;
    return true;
}

bool NativeVideoEncoder::appendFrame(const QImage& frame) {
    if (!g_enc.active) {
        g_enc.error = QStringLiteral("appendFrame called without a started encoder.");
        return false;
    }

    // Le dimensioni sono fissate a begin(): un frame diverso non si puo'
    // scrivere senza corrompere la clip. Meglio fallire e dirlo.
    if (frame.width() != g_enc.width || frame.height() != g_enc.height) {
        g_enc.error = QStringLiteral("Frame size %1x%2 does not match the video size %3x%4.")
                          .arg(frame.width()).arg(frame.height()).arg(g_enc.width).arg(g_enc.height);
        return false;
    }

    // Format_RGB32 == 32BGRA in memoria. Il chiamante lo garantisce gia', ma la
    // conversione qui e' la rete di sicurezza che evita canali scambiati.
    const QImage *src = &frame;
    QImage converted;
    if (frame.format() != QImage::Format_RGB32) {
        converted = frame.convertToFormat(QImage::Format_RGB32);
        src = &converted;
    }

    // Tiene l'audio AVANTI al video prima di aspettare l'input video: se la
    // traccia audio resta indietro, il writer smette di accettare frame e
    // waitUntilReady non tornerebbe mai.
    //
    // Il margine di UN SECONDO non e' prudenza: con un solo frame di margine
    // l'export si pianta (misurato: blocco netto al frame 48 di 150). Il writer
    // vuole l'audio sensibilmente piu' avanti del video per considerare
    // l'interleaving bilanciato, e un frame non basta.
    pumpAudioUpTo(CMTimeMake(g_enc.frameIndex + g_enc.fps, g_enc.fps));

    if (!waitUntilReady(g_enc.videoIn)) {
        // Il writer non accetta piu' frame e non si sblocca da solo. Quasi
        // sempre significa audio esaurito ma traccia ancora aperta: chiuderla
        // libera il video. Se anche cosi' non riparte, meglio perdere il frame
        // che restare appesi per sempre.
        finishAudioInput();
        if (!waitUntilReady(g_enc.videoIn, 5.0)) {
            g_enc.error = QStringLiteral("The video encoder stopped accepting frames (frame %1).")
                              .arg((qlonglong)g_enc.frameIndex);
            return false;
        }
    }

    // Se l'encoder si e' rotto nel frattempo (disco pieno, ...), fermiamoci qui
    // invece di accumulare frame in un writer morto.
    if (g_enc.writer.status == AVAssetWriterStatusFailed) {
        g_enc.error = QStringLiteral("The video encoder failed: %1")
                          .arg(QString::fromNSString(g_enc.writer.error ? g_enc.writer.error.localizedDescription : @"unknown error"));
        return false;
    }

    CVPixelBufferRef buffer = NULL;
    CVReturn status = CVPixelBufferPoolCreatePixelBuffer(NULL, g_enc.adaptor.pixelBufferPool, &buffer);
    if (status != kCVReturnSuccess || buffer == NULL) {
        g_enc.error = QStringLiteral("Cannot allocate a video buffer (CVReturn %1).").arg((int)status);
        return false;
    }

    CVPixelBufferLockBaseAddress(buffer, 0);
    uchar *dst = (uchar *)CVPixelBufferGetBaseAddress(buffer);
    const size_t dstStride = CVPixelBufferGetBytesPerRow(buffer);
    const size_t srcStride = (size_t)g_enc.width * 4;   // RGB32: 4 byte/pixel

    // I buffer vengono da un POOL riciclato: se il padding non venisse
    // sovrascritto conterrebbe il garbage del frame precedente. Azzeriamo prima
    // della copia — al peggio nero, mai rumore. (Stessa cautela del ramo iOS,
    // vedi la banda di rumore sull'ultimo frame.)
    memset(dst, 0, dstStride * (size_t)g_enc.height);

    for (int y = 0; y < g_enc.height; ++y) {
        memcpy(dst + (size_t)y * dstStride, src->constScanLine(y), srcStride);
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);

    CMTime frameTime = CMTimeMake(g_enc.frameIndex, g_enc.fps);
    BOOL ok = [g_enc.adaptor appendPixelBuffer:buffer withPresentationTime:frameTime];
    CVPixelBufferRelease(buffer);

    if (!ok) {
        g_enc.error = QStringLiteral("Cannot write video frame %1: %2")
                          .arg((qlonglong)g_enc.frameIndex)
                          .arg(QString::fromNSString(g_enc.writer.error ? g_enc.writer.error.localizedDescription : @"unknown error"));
        return false;
    }

    g_enc.frameIndex++;
    return true;
}

bool NativeVideoEncoder::end() {
    if (!g_enc.active) {
        g_enc.error = QStringLiteral("end called without a started encoder.");
        return false;
    }

    if (g_enc.frameIndex == 0) {
        // Nessun frame scritto: un mp4 vuoto non serve a nessuno.
        g_enc.error = QStringLiteral("No frames were recorded.");
        NativeVideoEncoder::abort();
        return false;
    }

    // Durata effettiva del video: i frame scritti, non quelli richiesti (lo
    // stop anticipato ne lascia meno).
    const CMTime videoEnd = CMTimeMake(g_enc.frameIndex, g_enc.fps);

    // Completa la traccia audio fino alla fine del video. Il grosso e' gia'
    // entrato durante il loop, frame per frame (pumpAudioUpTo): qui resta solo
    // la coda. Il limite e' il piu' stretto fra la durata video e il maxSeconds
    // dato a begin().
    if (g_enc.audioIn && !g_enc.audioFinished) {
        if (!CMTIME_IS_NUMERIC(g_enc.audioLimit) || CMTimeCompare(videoEnd, g_enc.audioLimit) < 0)
            g_enc.audioLimit = videoEnd;

        // pumpAudioUpTo puo' tornare senza aver finito se l'input non e' pronto:
        // insistiamo finche' copre il video o si esaurisce.
        while (!g_enc.audioDone && CMTimeCompare(g_enc.audioWritten, g_enc.audioLimit) < 0) {
            if (g_enc.writer.status == AVAssetWriterStatusFailed) break;
            if (!waitUntilReady(g_enc.audioIn)) break;   // niente attese infinite
            const CMTime before = g_enc.audioWritten;
            pumpAudioUpTo(g_enc.audioLimit);
            // Nessun avanzamento e nessuna fine dichiarata: non ha piu' senso
            // insistere (evita un ciclo infinito su un asset che non produce).
            if (CMTimeCompare(g_enc.audioWritten, before) == 0 && !g_enc.audioDone) break;
        }
    }
    // Chiude la traccia (se non gia' chiusa dal pump) e libera il lettore.
    finishAudioInput();

    [g_enc.videoIn markAsFinished];
    // Chiude la sessione esattamente alla fine dell'ultimo frame: senza questo
    // il contenitore puo' dichiarare una durata piu' lunga dei frame scritti.
    [g_enc.writer endSessionAtSourceTime:videoEnd];

    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [g_enc.writer finishWritingWithCompletionHandler:^{
        dispatch_semaphore_signal(sema);
    }];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    const bool ok = (g_enc.writer.status == AVAssetWriterStatusCompleted);
    if (!ok) {
        g_enc.error = QStringLiteral("The video encoder failed while closing the file: %1")
                          .arg(QString::fromNSString(g_enc.writer.error ? g_enc.writer.error.localizedDescription : @"unknown error"));
    }

    resetState();
    return ok;
}

void NativeVideoEncoder::abort() {
    if (!g_enc.active) return;

    AVAssetWriter *writer = g_enc.writer;
    QString outPath = g_enc.outputFile;
    resetState();

    if (writer && writer.status == AVAssetWriterStatusWriting) {
        [writer cancelWriting];
    }
    // cancelWriting di norma rimuove il file parziale; se e' rimasto, via.
    if (!outPath.isEmpty()) {
        NSString *p = outPath.toNSString();
        if ([[NSFileManager defaultManager] fileExistsAtPath:p]) {
            [[NSFileManager defaultManager] removeItemAtPath:p error:nil];
        }
    }
}

QString NativeVideoEncoder::lastError() {
    return g_enc.error;
}

// --- API one-shot (dichiarata per tutte le piattaforme Apple) ---------------
// Su macOS il percorso a file non si usa: i frame passano da appendFrame().
// La definiamo comunque perche' l'header la dichiara, cosi' un uso accidentale
// fallisce in modo esplicito invece di non linkare.
bool NativeVideoEncoder::createMP4(const QString& framesDir, const QString& outputFile, int fps, int width, int height, const QString& audioFile) {
    Q_UNUSED(framesDir); Q_UNUSED(outputFile); Q_UNUSED(fps);
    Q_UNUSED(width); Q_UNUSED(height); Q_UNUSED(audioFile);
    g_enc.error = QStringLiteral("createMP4 (file-based) is not used on macOS: use begin/appendFrame/end.");
    return false;
}

void NativeVideoEncoder::setKeepScreenOn(bool keepOn) {
    // Su macOS non impediamo lo spegnimento dello schermo: a differenza di iOS
    // il rendering non si ferma quando il display si spegne, e l'assertion
    // IOKit richiederebbe una dipendenza in piu' per nessun beneficio reale.
    // La funzione deve comunque esistere: la chiama videorecorder.cpp.
    Q_UNUSED(keepOn);
}

#endif // Q_OS_MACOS
