#ifndef PRESETSERIALIZER_H
#define PRESETSERIALIZER_H

#include <QObject>
#include <QString>

class MainWindow;
class QJsonObject;

class PresetSerializer : public QObject
{
    Q_OBJECT
public:
    explicit PresetSerializer(MainWindow *parent);

    // Le 4 funzioni "pesanti" estratte dalla MainWindow
    void saveSurface(const QString &suggestedPath = "");
    void saveTexture(const QString &path);
    void saveMotion(const QString &suggestedPath = "");
    void saveSoundAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveSound(const QString &filePath);
    void saveScript();
    void saveTextureAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveSurfaceAs(const QString &startDir, const QString &sourceFilePath = "");
    void saveMotionAs(const QString &startDir, const QString &sourceFilePath = "");

private:
    // Scrive i sei limiti parametrici in `limits`: sempre la chiave numerica
    // (uMin/uMax/...), piu' la gemella "...Expr" con il testo grezzo quando
    // l'utente ha scritto una formula anziche' un numero. Unica implementazione:
    // i tre save (surface/motion/script) devono restare allineati.
    void writeParametricLimits(QJsonObject &limits);

    MainWindow *m_mainWindow;
};

#endif // PRESETSERIALIZER_H
