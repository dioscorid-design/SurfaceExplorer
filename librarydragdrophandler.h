#ifndef LIBRARYDRAGDROPHANDLER_H
#define LIBRARYDRAGDROPHANDLER_H

#include <QObject>

class MainWindow;

class LibraryDragDropHandler : public QObject
{
    Q_OBJECT
public:
    explicit LibraryDragDropHandler(MainWindow *parent);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    MainWindow *m_mainWindow;
};

#endif // LIBRARYDRAGDROPHANDLER_H
