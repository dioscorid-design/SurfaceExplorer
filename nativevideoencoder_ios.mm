#include "nativevideoencoder.h"
#include <QtGlobal>

#ifdef Q_OS_IOS

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import <CoreVideo/CoreVideo.h>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QDebug>

bool NativeVideoEncoder::createMP4(const QString& framesDir, const QString& outputFile, int fps, int width, int height, const QString& audioFile) {
    NSString *outPath = outputFile.toNSString();
    NSURL *outURL = [NSURL fileURLWithPath:outPath];

    if ([[NSFileManager defaultManager] fileExistsAtPath:outPath]) {
        [[NSFileManager defaultManager] removeItemAtPath:outPath error:nil];
    }

    NSError *error = nil;
    AVAssetWriter *videoWriter = [[AVAssetWriter alloc] initWithURL:outURL fileType:AVFileTypeMPEG4 error:&error];
    if (error) return false;

    // --- SETUP VIDEO ---
    // I frame sorgente sono NITIDI (verificato) e il bitrate è altissimo, eppure
    // H.264 impastava le linee sottili del wireframe: la perdita è strutturale,
    // dovuta al chroma subsampling 4:2:0. Le linee sono verde puro (quasi solo chroma)
    // su sfondo scuro -> il 4:2:0 ne dimezza la risoluzione di colore e le sfoca, e
    // nessun bitrate lo recupera del tutto. Usiamo HEVC (H.265): a parità di bitrate
    // ricostruisce molto meglio le alte frequenze del chroma, riducendo l'effetto, e
    // teniamo il bitrate molto alto (bpp 0.8) per spremere il massimo dal 4:2:0.
    // HEVC è accelerato in hardware sugli iPad recenti.
    double bpp = 0.8;                        // bit per pixel per frame (molto alto)
    long long bitRate = (long long)((double)width * (double)height * (double)fps * bpp);
    long long minBitRate = 30000000LL;       // 30 Mbps minimo
    if (bitRate < minBitRate) bitRate = minBitRate;

    NSDictionary *compression = @{
        AVVideoAverageBitRateKey: @(bitRate),
        AVVideoMaxKeyFrameIntervalKey: @(fps),  // almeno un keyframe al secondo
        AVVideoQualityKey: @(1.0)               // qualità massima richiesta all'encoder
    };

    NSDictionary *videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeHEVC,
        AVVideoWidthKey: @(width),
        AVVideoHeightKey: @(height),
        AVVideoCompressionPropertiesKey: compression
    };

    AVAssetWriterInput *writerInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
    writerInput.expectsMediaDataInRealTime = NO;

    // Formato BGRA: combacia byte-per-byte col Format_RGB32 di Qt (in memoria little-
    // endian i pixel sono B,G,R,A), così i frame RAW si copiano con un memcpy senza
    // scambiare canali. Il fallback BMP disegna comunque in questo stesso layout.
    NSDictionary *sourcePixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (NSString *)kCVPixelBufferWidthKey: @(width),
        (NSString *)kCVPixelBufferHeightKey: @(height)
    };
    AVAssetWriterInputPixelBufferAdaptor *adaptor = [AVAssetWriterInputPixelBufferAdaptor
        assetWriterInputPixelBufferAdaptorWithAssetWriterInput:writerInput
        sourcePixelBufferAttributes:sourcePixelBufferAttributes];

    [videoWriter addInput:writerInput];

    // --- SETUP AUDIO (Se fornito) ---
    AVAssetWriterInput *audioInput = nil;
    AVAssetReader *audioReader = nil;
    AVAssetReaderTrackOutput *audioReaderOutput = nil;

    if (!audioFile.isEmpty()) {
        NSURL *audioURL = [NSURL fileURLWithPath:audioFile.toNSString()];
        AVAsset *audioAsset = [AVAsset assetWithURL:audioURL];
        NSError *readErr = nil;
        audioReader = [AVAssetReader assetReaderWithAsset:audioAsset error:&readErr];

        if (audioReader) {
            AVAssetTrack *audioTrack = [[audioAsset tracksWithMediaType:AVMediaTypeAudio] firstObject];
            if (audioTrack) {
                audioReaderOutput = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:audioTrack outputSettings:nil];
                [audioReader addOutput:audioReaderOutput];
                [audioReader startReading];

                AudioChannelLayout acl;
                bzero(&acl, sizeof(acl));
                acl.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;

                NSDictionary *audioSettings = @{
                    AVFormatIDKey: @(kAudioFormatMPEG4AAC),
                    AVNumberOfChannelsKey: @(2),
                    AVSampleRateKey: @(44100.0),
                    AVEncoderBitRateKey: @(192000),
                    AVChannelLayoutKey: [NSData dataWithBytes:&acl length:sizeof(acl)]
                };

                audioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:audioSettings];
                audioInput.expectsMediaDataInRealTime = NO;
                if ([videoWriter canAddInput:audioInput]) {
                    [videoWriter addInput:audioInput];
                }
            }
        }
    }

    // --- INIZIO REGISTRAZIONE ---
    [videoWriter startWriting];
    [videoWriter startSessionAtSourceTime:kCMTimeZero];

    QDir dir(framesDir);
    // Preferiamo i frame RAW (RGBA a 4 byte/pixel, Format_RGB32 di Qt): si copiano
    // direttamente nel CVPixelBuffer con un memcpy riga-per-riga, senza decodifica
    // UIImage né redraw in CGContext (vecchia via BMP, ~2 conversioni a frame).
    // Se non ci sono RAW (build vecchia / fallback) ricadiamo sui BMP via UIImage.
    dir.setNameFilters(QStringList() << "*.raw");
    dir.setSorting(QDir::Name);
    QStringList files = dir.entryList();

    const bool useRaw = !files.isEmpty();
    if (!useRaw) {
        dir.setNameFilters(QStringList() << "*.bmp");
        files = dir.entryList();
    }

int frameCount = 0;
    bool videoDone = (files.count() == 0);
    bool audioDone = (audioInput == nil);

    // ---> FIX: Calcoliamo la durata esatta del video (es. 5 secondi)
    CMTime maxDuration = CMTimeMake(files.count(), fps);

    // CICLO DI MULTIPLEXING
    while (!videoDone || !audioDone) {

        // 1. Scrivi Audio se pronto
        if (!audioDone && audioInput.readyForMoreMediaData) {
            CMSampleBufferRef audioSample = [audioReaderOutput copyNextSampleBuffer];
            if (audioSample) {
                // Controlliamo il "tempo" di questo frammento audio
                CMTime currentAudioTime = CMSampleBufferGetPresentationTimeStamp(audioSample);

                // Se l'audio ha superato la durata del video, STACCHIAMO LA SPINA (Effetto -shortest)
                if (CMTimeCompare(currentAudioTime, maxDuration) >= 0) {
                    [audioInput markAsFinished];
                    audioDone = true;
                    CFRelease(audioSample);
                } else {
                    [audioInput appendSampleBuffer:audioSample];
                    CFRelease(audioSample);
                }
            } else {
                [audioInput markAsFinished];
                audioDone = true;
            }
        }

        // 2. Scrivi Video se pronto
        if (!videoDone && writerInput.readyForMoreMediaData) {
            if (frameCount < files.count()) {
                QString fullPath = dir.absoluteFilePath(files[frameCount]);

                if (useRaw) {
                    // --- VIA RAPIDA: frame RAW (4 byte/pixel) -> memcpy nel buffer ---
                    // Il lato Qt salva i pixel come Format_RGB32, che in memoria (little-
                    // endian) è B,G,R,A: coincide 1:1 col layout 32BGRA del pixel buffer,
                    // quindi niente conversione né scambio di canali.
                    QFile rawFile(fullPath);
                    if (rawFile.open(QIODevice::ReadOnly)) {
                        CVPixelBufferRef buffer = NULL;
                        CVReturn status = CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &buffer);
                        if (status == kCVReturnSuccess && buffer != NULL) {
                            CVPixelBufferLockBaseAddress(buffer, 0);
                            uchar *dst = (uchar *)CVPixelBufferGetBaseAddress(buffer);
                            const size_t dstStride = CVPixelBufferGetBytesPerRow(buffer);
                            const size_t srcStride = (size_t)width * 4; // RGB32: 4 byte/pixel

                            // Copia riga per riga: dstStride può essere > srcStride (padding).
                            for (int row = 0; row < height; ++row) {
                                QByteArray line = rawFile.read(srcStride);
                                if ((size_t)line.size() < srcStride) break; // file corto/corrotto
                                memcpy(dst + row * dstStride, line.constData(), srcStride);
                            }
                            CVPixelBufferUnlockBaseAddress(buffer, 0);

                            CMTime frameTime = CMTimeMake(frameCount, fps);
                            [adaptor appendPixelBuffer:buffer withPresentationTime:frameTime];
                            CVPixelBufferRelease(buffer);
                        }
                        rawFile.close();
                    }
                } else {
                    // --- FALLBACK: vecchia via BMP via UIImage + CGContext ---
                    UIImage *image = [UIImage imageWithContentsOfFile:fullPath.toNSString()];
                    if (image) {
                        CVPixelBufferRef buffer = NULL;
                        CVReturn status = CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &buffer);
                        if (status == kCVReturnSuccess && buffer != NULL) {
                            CVPixelBufferLockBaseAddress(buffer, 0);
                            void *pxdata = CVPixelBufferGetBaseAddress(buffer);
                            CGColorSpaceRef rgbColorSpace = CGColorSpaceCreateDeviceRGB();
                            // Pool BGRA: byte order little + skipFirst => layout B,G,R,A.
                            CGContextRef context = CGBitmapContextCreate(pxdata, width, height, 8, CVPixelBufferGetBytesPerRow(buffer), rgbColorSpace, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
                            if (context) {
                                CGContextDrawImage(context, CGRectMake(0, 0, width, height), image.CGImage);
                                CGContextRelease(context);
                            }
                            CGColorSpaceRelease(rgbColorSpace);
                            CVPixelBufferUnlockBaseAddress(buffer, 0);

                            CMTime frameTime = CMTimeMake(frameCount, fps);
                            [adaptor appendPixelBuffer:buffer withPresentationTime:frameTime];
                            CVPixelBufferRelease(buffer);
                        }
                    }
                }
                frameCount++;
            } else {
                [writerInput markAsFinished];
                videoDone = true;

                // Se il video è finito, diciamo al ciclo di non aspettare più l'audio
                audioDone = true;
            }
        }

        // Riposa la CPU se non c'è nulla da scrivere in questo millisecondo
        if (!videoDone && !writerInput.readyForMoreMediaData && !audioDone && !audioInput.readyForMoreMediaData) {
            [NSThread sleepForTimeInterval:0.005];
        }
    }

    // --- CHIUSURA FINALE ---
    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [videoWriter finishWritingWithCompletionHandler:^{
        dispatch_semaphore_signal(sema);
    }];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    return videoWriter.status == AVAssetWriterStatusCompleted;
}

void NativeVideoEncoder::setKeepScreenOn(bool keepOn) {
    // Comunichiamo con il thread principale della UI di Apple
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = keepOn ? YES : NO;
    });
}

#endif
