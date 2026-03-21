#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <QObject>
#include <QPoint>
#include <QPointF>

class GLWidget;
class QMouseEvent;
class QTouchEvent;
class QWheelEvent;
class QEvent;

class InputHandler : public QObject
{
    Q_OBJECT
public:
    explicit InputHandler(GLWidget* parentWidget);

    void handleMousePress(QMouseEvent* event);
    void handleMouseMove(QMouseEvent* event);
    void handleMouseRelease(QMouseEvent* event);
    void handleWheel(QWheelEvent* event);
    bool handleTouch(QEvent* event);

private:
    GLWidget* m_glWidget;

    // Stato Mouse
    QPoint m_lastMousePos;

    // --- NUOVE VARIABILI PER GESTIONE CLICK/DRAG ---
    QPoint m_pressPos;      // Dove ho premuto
    bool m_isClickCandidate; // E' ancora un possibile click?

    // Stato Touch
    QPointF m_lastTouchPos;
    float m_lastPinchDist = 0.0f;
    float m_lastTouchAngle = 0.0f;
    int m_lastPointCount = 0;
};

#endif // INPUTHANDLER_H
