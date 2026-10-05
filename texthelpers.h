// texthelpers.h - Aiutanti di testo e di file usati anche fuori da MainWindow
// (TextureCode, ...). MainWindow li vede tramite mainwindow_p.h.
#pragma once

#include <QFile>
#include <QRegularExpression>
#include <QString>

// Rimuove i commenti di linea e di blocco dal codice (GLSL o equazioni)
inline QString stripCodeComments(QString s) {
    static const QRegularExpression lineComments(R"(//.*$)", QRegularExpression::MultilineOption);
    static const QRegularExpression blockComments(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption);
    s.remove(lineComments);
    s.remove(blockComments);
    return s;
}

// Un file e' utilizzabile solo se si riesce davvero ad APRIRLO. QFile::exists()
// non basta: sotto sandbox i metadati di un file fuori dallo scope autorizzato
// restano visibili (exists() = true) mentre la lettura fallisce. La differenza
// e' invisibile finche' non si prova ad aprirlo.
inline bool isReadableFile(const QString& path)
{
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    f.close();
    return true;
}
