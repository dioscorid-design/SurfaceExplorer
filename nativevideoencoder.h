#ifndef NATIVEVIDEOENCODER_H
#define NATIVEVIDEOENCODER_H

#include <QString>

class NativeVideoEncoder {
public:
    static bool createMP4(const QString& framesDir, const QString& outputFile, int fps, int width, int height, const QString& audioFile = "");
    static void setKeepScreenOn(bool keepOn);
};

#endif // NATIVEVIDEOENCODER_H
