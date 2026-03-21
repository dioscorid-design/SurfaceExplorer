#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include <QOpenGLFunctions>
#include <QImage>
#include <QString>
#include <QColor>
#include <QImage>

class TextureManager : protected QOpenGLFunctions
{
public:
    TextureManager();
    virtual ~TextureManager();

    void init(); // Inizializza OpenGL

    // Caricamento Immagini
    void loadFromFile(const QString& path);

    // Gestione Texture OpenGL
    void bind(unsigned int unit = 0);
    void release();

    bool isReady() const { return m_textureReady; }
    unsigned int getId() const { return m_textureId; }

    void loadFromImage(const QImage& image);

private:
    unsigned int m_textureId;
    bool m_textureReady;
    QImage m_currentImage;

    // Carica l'immagine nella GPU
    void uploadToGPU();
};

#endif // TEXTUREMANAGER_H
