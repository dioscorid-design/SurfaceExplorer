#include "nativevideoencoder.h"
#include <QtGlobal>

#ifdef Q_OS_IOS

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import <CoreVideo/CoreVideo.h>
#include <QDir>
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
    NSDictionary *videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(width),
        AVVideoHeightKey: @(height)
    };
    AVAssetWriterInput *writerInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
    writerInput.expectsMediaDataInRealTime = NO;

    NSDictionary *sourcePixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32ARGB),
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
    dir.setNameFilters(QStringList() << "*.bmp");
    dir.setSorting(QDir::Name);
    QStringList files = dir.entryList();

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
                UIImage *image = [UIImage imageWithContentsOfFile:fullPath.toNSString()];

                if (image) {
                    CVPixelBufferRef buffer = NULL;
                    CVReturn status = CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &buffer);
                    if (status == kCVReturnSuccess && buffer != NULL) {
                        CVPixelBufferLockBaseAddress(buffer, 0);
                        void *pxdata = CVPixelBufferGetBaseAddress(buffer);
                        CGColorSpaceRef rgbColorSpace = CGColorSpaceCreateDeviceRGB();
                        CGContextRef context = CGBitmapContextCreate(pxdata, width, height, 8, CVPixelBufferGetBytesPerRow(buffer), rgbColorSpace, kCGImageAlphaNoneSkipFirst);
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
