#ifndef LIBRARYMENUCONTROLLER_H
#define LIBRARYMENUCONTROLLER_H

#include <QObject>
#include <QPoint>
#include <QHash>
#include <QStringList>

#include "librarytreefocus.h"

class MainWindow;
class QTreeWidget;

class LibraryMenuController : public QObject
{
    Q_OBJECT
public:
    explicit LibraryMenuController(MainWindow *parent);

    // Passiamo esplicitamente l'albero per maggiore sicurezza (meglio di sender())
    void showMenu(QTreeWidget *tree, const QPoint &pos);

    // L'evidenziazione della Library resta sul preset A SCHERMO: il menu la
    // sposta sulla voce cliccata (e' il bersaglio dei comandi) e alla chiusura
    // torna com'era alla pressione che l'ha aperto. Va chiamata una volta per
    // albero: osserva le pressioni sul suo viewport.
    void watchTree(QTreeWidget *tree);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    MainWindow *m_mainWindow;
    // Presa alla pressione, PRIMA che Qt sposti la selezione sulla voce sotto
    // il cursore; la consuma il menu che quella pressione apre.
    QHash<QTreeWidget *, LibraryTreeFocus::Highlight> m_pressHighlight;
};

#endif // LIBRARYMENUCONTROLLER_H
