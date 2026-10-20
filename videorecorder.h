#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

#include <QObject>

class MainWindow; // Forward declaration

class VideoRecorder : public QObject
{
    Q_OBJECT
public:
    explicit VideoRecorder(MainWindow *mainWindow, QObject *parent = nullptr);

public slots:
    void toggleRecord();

private:
    MainWindow *m_mainWindow;
};

#endif // VIDEORECORDER_H
