#ifndef LIBRARYMENUCONTROLLER_H
#define LIBRARYMENUCONTROLLER_H

#include <QObject>
#include <QPoint>

class MainWindow;
class QTreeWidget;

class LibraryMenuController : public QObject
{
    Q_OBJECT
public:
    explicit LibraryMenuController(MainWindow *parent);

    // Passiamo esplicitamente l'albero per maggiore sicurezza (meglio di sender())
    void showMenu(QTreeWidget *tree, const QPoint &pos);

private:
    MainWindow *m_mainWindow;
};

#endif // LIBRARYMENUCONTROLLER_H
