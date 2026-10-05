// docsearch.h - Ricerca nella guida (le pagine HTML del .qrc, :/docs).
#pragma once

#include <QString>
#include <QVector>

namespace DocSearch {

struct Hit {
    QString file;      // "qrc:/docs/doc_raymarching.html"
    QString title;     // dal <title> della pagina
    QString context;   // frase attorno alla prima occorrenza
    int     count = 0; // occorrenze nella pagina
};

// Cerca `needle` in TUTTE le pagine del manuale (:/docs/*.html), non nella
// sola pagina aperta: il manuale e' spezzato in piu' file e un Ctrl+F locale
// non troverebbe cio' che sta altrove. Restituisce le pagine con un frammento
// di contesto, ordinate per numero di occorrenze.
QVector<Hit> search(const QString &needle);

}
