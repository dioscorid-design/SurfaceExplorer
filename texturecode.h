// texturecode.h - Il CODICE delle texture, senza stato: che cosa contiene,
// come si confronta con una voce della Library, dove sta l'immagine che cita.
// Funzioni pure (resolveImagePath legge il disco e le impostazioni), usate da
// MainWindow, dal Save e dagli alberi della Library.
#pragma once

#include <QString>

struct LibraryItem;

namespace TextureCode {

// Lo script di una texture parametrica contiene CODICE da compilare (e non il
// solo tag //IMG: o niente)? Euristica storica, in un punto solo.
bool hasLogic(const QString &code);
// Lo script campiona un'immagine (iChannel0..3 o il sampler `tex`)? Decide se
// una fascia tiene la propria immagine sotto la procedurale appena applicata
// (famiglia "Animated Images") o la lascia.
bool samplesImage(const QString &code);
// Lo stesso script, col percorso del tag //IMG: sostituito (se c'e').
QString withImageTagPath(const QString &code, const QString &path);
// Il percorso dell'immagine citata dal tag //IMG: dello script, LEGGIBILE: se
// non lo e' al percorso scritto, la si cerca per nome nella cartella delle
// texture (lo "Smart Path Resolver"). Vuoto senza tag; "NOT_FOUND|<percorso>"
// se non si trova.
QString resolveImagePath(const QString &scriptCode);
// Decide se una voce della Library texture e' quella attiva. Unica sede del
// confronto: lo usano la selezione nell'albero e la sincronizzazione al load
// di un record. cleanedActiveCode = cleanForComparison(activeCode).
bool itemMatchesCode(const LibraryItem &texItem, const QString &activeCode,
                     const QString &cleanedActiveCode);
// Il codice ridotto a cio' che conta per dire "e' la stessa texture": senza
// suono, tag //IMG:, commenti e spazi.
QString cleanForComparison(QString str);
// Scacchiera di default, condivisa fra texture globale e texture per-mesh.
QString defaultMeshCode();

}
