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
    // true = fra press e release il mouse non si e' mosso (clic secco), quindi
    // l'utente NON ha trascinato la vista. Usato da GLWidget per distinguere il
    // trascinamento dal semplice clic prima di segnalare userMovedView().
    bool wasClickWithoutDrag() const { return m_isClickCandidate; }

    // true = il tocco appena gestito ha davvero MOSSO la vista (rotazione a un
    // dito oltre la deadzone, o pinch di zoom). Il percorso touch e' l'unico
    // senza un "release" da cui dedurlo come fa il mouse: senza questa
    // segnalazione GLWidget non emetteva userMovedView() e su mobile ne'
    // rotazione ne' zoom sporcavano la scena (nessun avviso "vuoi salvare?").
    // Si consuma a ogni lettura: chi lo legge ha gia' emesso il segnale.
    bool takeTouchMovedView() { bool v = m_touchMovedView; m_touchMovedView = false; return v; }

private:
    GLWidget* m_glWidget;

    // Stato Mouse
    QPoint m_lastMousePos;

    // --- NUOVE VARIABILI PER GESTIONE CLICK/DRAG ---
    QPoint m_pressPos;
    bool m_isClickCandidate;
    bool m_touchMovedView = false;   // vedi takeTouchMovedView()
    // Stato Touch
    QPointF m_lastTouchPos;
    float m_lastPinchDist = 0.0f;
    float m_lastTouchAngle = 0.0f;
    int m_lastPointCount = 0;
};

#endif // INPUTHANDLER_H
