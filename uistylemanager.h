#ifndef UISTYLEMANAGER_H
#define UISTYLEMANAGER_H

#include <QList>

// Forward declarations
class QMainWindow;
class QDockWidget;
class QSlider;
class QLabel;
class QWidget;

class UiStyleManager
{
public:
    static void applyDarkTheme(QMainWindow* window);
    static void applyPlatformStyle(QMainWindow* window);
    static void setupDockScroll(QDockWidget* dock, bool isExamplesDock = false);
    static void compactForMobile(const QList<QWidget*>& containers,
                                 QWidget* panelColor = nullptr,
                                 const QList<QWidget*>& sliderRows = {},
                                 const QList<QLabel*>& valueLabels = {});
    static void setupBigSliders(QSlider* r, QSlider* g, QSlider* b, QSlider* alpha, QSlider* light = nullptr, QSlider* speed3D = nullptr, QSlider* speed4D = nullptr);
};

#endif // UISTYLEMANAGER_H
