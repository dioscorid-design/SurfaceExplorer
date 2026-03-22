#ifndef PRESETSERIALIZER_H
#define PRESETSERIALIZER_H

#include <QObject>
#include <QString>

class MainWindow;

class PresetSerializer : public QObject
{
    Q_OBJECT
public:
    explicit PresetSerializer(MainWindow *parent);

    // Le 4 funzioni "pesanti" estratte dalla MainWindow
    void saveSurface(const QString &suggestedPath = "");
    void saveTexture(const QString &path);
    void saveMotion(const QString &suggestedPath = "");
    void saveSound(const QString &startDir, const QString &sourceFilePath = "");
    void saveScript();
    void saveTextureAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveSurfaceAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveMotionAs(const QString &startDir, const QString &sourceFilePath = "");

private:
    MainWindow *m_mainWindow;
};

#endif // PRESETSERIALIZER_H
