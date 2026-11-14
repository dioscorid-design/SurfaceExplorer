#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

#include <QObject>
#include <functional>

class MainWindow; // Forward declaration
class QDialog;

class VideoRecorder : public QObject
{
    Q_OBJECT
public:
    explicit VideoRecorder(MainWindow *mainWindow, QObject *parent = nullptr);

    // Solo test: chiamata col dialogo delle impostazioni video appena aperto
    // (desktop: DesktopVideoDialog; mobile: MobileVideoSaveDialog). Chi la
    // passa chiude il dialogo (accept/reject).
    static void setTestDialogHandler(std::function<void(QDialog *)> handler);

public slots:
    void toggleRecord();

private:
    MainWindow *m_mainWindow;
};

#endif // VIDEORECORDER_H
