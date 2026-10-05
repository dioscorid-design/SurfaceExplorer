// librarytreefocus.h - Quale voce degli alberi della Library corrisponde a
// cio' che e' in scena, e la sua evidenziazione. Senza stato: albero e
// libreria arrivano come argomenti (gli alberi esistono solo dopo setupUi).
// CHE COSA cercare -- la texture efficace della superficie, dello sfondo o
// della fascia, e la sua ancora -- lo decide chi chiama, che conosce la scena.
#pragma once

#include <QString>

class LibraryManager;
class QTreeWidget;
class QTreeWidgetItem;
struct LibraryItem;

namespace LibraryTreeFocus {

// Evidenzia una voce: selezionata, corrente, genitori aperti, portata in
// vista. Non deseleziona le altre: lo fa chi chiama, se serve.
void focus(QTreeWidget *tree, QTreeWidgetItem *item);
// La voce di texture col nome dato (senza badare alle maiuscole), o nullptr.
// Unica scansione dell'albero per tutte le ancore (texture di superficie, di
// sfondo, delle fasce): due copie dello stesso ciclo finirebbero per divergere
// alla prima correzione.
const LibraryItem *textureNamed(QTreeWidget *tree, const LibraryManager &lib,
                                const QString &name);
// Evidenzia nell'albero texture la voce del codice attivo. libName: l'ancora
// della texture cercata (superficie, sfondo o fascia), che vince sul codice;
// VUOTO se non nota, e allora si cerca per solo codice. Codice vuoto: niente.
void selectTexture(QTreeWidget *tree, const LibraryManager &lib,
                   const QString &activeCode, const QString &libName);
// Deseleziona l'albero dei suoni ed evidenzia la voce del suono in scena:
// searchCode e' il testo in cui cercarlo (slot del suono e codici delle
// texture, che possono portare un //MUSIC:), libName la sua ancora (vuota se
// in scena non c'e' audio). Stessi due criteri delle texture.
void selectSound(QTreeWidget *tree, const LibraryManager &lib,
                 const QString &searchCode, const QString &libName);

}
