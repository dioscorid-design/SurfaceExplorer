#ifndef GLSLTRANSLATOR_H
#define GLSLTRANSLATOR_H

#include <QString>

class GlslTranslator
{
public:
    // La funzione principale che richiamerai dal tuo codice
    static QString translateEquation(const QString& mathInput);

private:
    // Funzioni helper interne
    static QString fixIntegersToFloats(QString expr);
    static QString convertPowersToPowFunction(const QString& expr);
    static QString convertModuloToModFunction(QString expr);
};

#endif // GLSLTRANSLATOR_H
