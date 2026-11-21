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
class QKeyEvent;
struct DriveControls;

class InputHandler : public QObject
{
    Q_OBJECT
public:
    explicit InputHandler(GLWidget* parentWidget);

    void handleMousePress(QMouseEvent* event);
    void handleMouseMove(QMouseEvent* event);
    void handleMouseRelease(QMouseEvent* event);
    // true = la rotellina ha davvero zoomato la vista, o scalato la traiettoria
    // del path in corsa. Falso per gli eventi a delta verticale nullo (scroll
    // orizzontale, fine gesto del trackpad) e per lo zoom 3D bloccato durante
    // un path che non si puo' modellare: la vista non cambia e non c'e'
    // niente da segnalare con userMovedView().
    bool handleWheel(QWheelEvent* event);
    bool handleTouch(QEvent* event);
    // I TASTI DELLA GUIDA LIBERA (GLWidget::driveActive): true = il tasto e'
    // suo (anche la ripetizione automatica, che non cambia nulla), e non va
    // oltre. Fuori dalla guida la tastiera non tocca la vista.
    bool handleKey(QKeyEvent* event, bool pressed);
    // true = il gesto del mouse appena concluso ha davvero MOSSO la vista
    // (rotazione oltre la deadzone, rotazione o pan 2D). Gemello di
    // takeTouchMovedView. Vale solo per un gesto INIZIATO sulla vista: un
    // release che arriva senza il suo press (press consumato da un filtro,
    // finestra chiusa fra press e release) non conta. Prima si deduceva dal solo
    // flag del clic, mai riarmato: un release orfano ereditava l'ultimo gesto --
    // anche di un preset precedente -- e sporcava la scena senza movimento.
    // Si consuma a ogni lettura: chi lo legge ha gia' emesso il segnale.
    bool takeMouseMovedView() { bool v = m_mouseMovedView; m_mouseMovedView = false; return v; }

    // true = il tocco appena gestito ha davvero MOSSO la vista (rotazione a un
    // dito oltre la deadzone, o pinch di zoom). Il percorso touch e' l'unico
    // senza un "release" da cui dedurlo come fa il mouse: senza questa
    // segnalazione GLWidget non emetteva userMovedView() e su mobile ne'
    // rotazione ne' zoom sporcavano la scena (nessun avviso "vuoi salvare?").
    // Si consuma a ogni lettura: chi lo legge ha gia' emesso il segnale.
    bool takeTouchMovedView() { bool v = m_touchMovedView; m_touchMovedView = false; return v; }

private:
    // I comandi della guida, se e' accesa (GLWidget::driveActive).
    DriveControls* driveControls() const;

    GLWidget* m_glWidget;
    // La guida: dove sono stati premuti il sinistro (cloche) e il destro (rollio).
    QPointF m_drivePress;
    QPointF m_driveRollPress;

    // Stato Mouse
    QPoint m_lastMousePos;

    // --- NUOVE VARIABILI PER GESTIONE CLICK/DRAG ---
    QPoint m_pressPos;
    bool m_isClickCandidate;
    bool m_mouseGestureActive = false; // press ricevuto, release non ancora arrivato
    bool m_mouseMovedView = false;   // vedi takeMouseMovedView()
    bool m_touchMovedView = false;   // vedi takeTouchMovedView()
    // Stato Touch
    QPointF m_lastTouchPos;
    float m_lastPinchDist = 0.0f;
    QPointF m_lastPinchCentre;   // punto medio delle due dita: sposta l'oggetto selezionato
    float m_lastTouchAngle = 0.0f;
    int m_lastPointCount = 0;
};

#endif // INPUTHANDLER_H
