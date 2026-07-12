#ifndef IOSEDITMENU_H
#define IOSEDITMENU_H

#include <QtGlobal>

#if defined(Q_OS_IOS)

class QWidget;
class QPoint;

// Presenta il menu di modifica nativo (UIEditMenuInteraction, iOS 16+)
// ancorato a globalPos (coordinate globali Qt). Le voci si adattano allo
// stato dell'editor (Cut/Copy solo con selezione, Undo/Redo secondo lo
// stack) ed eseguono direttamente i metodi del widget Qt.
void iosPresentEditMenu(QWidget* editor, const QPoint& globalPos);

// Congeda il menu se presentato (no-op altrimenti). Da chiamare quando
// l'utente inizia a digitare: il menu non si chiude da solo sull'input.
void iosDismissEditMenu();

#endif // Q_OS_IOS

#endif // IOSEDITMENU_H
