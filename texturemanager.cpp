#include "texturemanager.h"
#include <QDebug>
#include <cmath>

TextureManager::TextureManager()
    : m_textureId(0), m_textureReady(false)
{
}

TextureManager::~TextureManager()
{
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
    }
}

void TextureManager::init()
{
    initializeOpenGLFunctions();

    // --- Generazione Texture a Scacchi ---
    int s = 512; // Risoluzione texture

    // Creiamo un'immagine locale temporanea
    QImage checkerImg(s, s, QImage::Format_RGBA8888);

    // Dimensione dei quadrati (più piccolo = più quadrati)
    int step = 64;

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            // Logica scacchiera: (indice colonna + indice riga) pari o dispari
            bool isColor1 = ((x / step + y / step) % 2 == 0);

            if (isColor1) {
                checkerImg.setPixelColor(x, y, QColor(0, 255, 0)); // Verde
            } else {
                checkerImg.setPixelColor(x, y, Qt::black); // Nero
            }
        }
    }

    // Assegniamo l'immagine generata al membro della classe
    m_currentImage = checkerImg;

    // Carichiamo subito sulla GPU
    uploadToGPU();
}

void TextureManager::loadFromFile(const QString& path)
{
    QImage img(path);
    if (!img.isNull()) {
        int maxSize = 2048;
        if (img.width() > maxSize || img.height() > maxSize) {
            img = img.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        // Fix Warning: mirrored(false, true) specchia verticalmente senza warning
        m_currentImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));
        m_textureReady = false;
    }
}

void TextureManager::uploadToGPU()
{
    if (m_currentImage.isNull()) return;

    if (m_textureId == 0) {
        glGenTextures(1, &m_textureId);
    }

    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 m_currentImage.width(), m_currentImage.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, m_currentImage.constBits());

    m_textureReady = true;
}

void TextureManager::bind(unsigned int unit)
{
    if (!m_textureReady) {
        uploadToGPU();
    }
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
}

void TextureManager::release()
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureManager::loadFromImage(const QImage& image)
{
    if (image.isNull()) return;

    // Convertiamo e specchiamo l'immagine per OpenGL (come fai già in loadFromFile)
    m_currentImage = image.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));

    m_textureReady = false;

    uploadToGPU();
}
