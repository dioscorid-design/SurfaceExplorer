#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QElapsedTimer>
#include <QHash>

class GLWidget;
class QMouseEvent;
class QTouchEvent;
class QWheelEvent;
class QEvent;
class QTouchEvent;
class QKeyEvent;
struct DriveControls;
struct GamepadState;

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

    // IL PAD (Gamepad, un passo di dt secondi con lo stato gia' ripulito): fa
    // cio' che fa il mouse, con le stesse funzioni e alle stesse condizioni.
    // Lo stick sinistro e' il trascinamento col sinistro, il destro col destro,
    // i grilletti la rotella (R2 avanti, L2 indietro). true = la vista si e'
    // mossa (userMovedView). Il REC lo esclude chi chiama.
    bool handleGamepad(const GamepadState& pad, float dt);
    // Uno stick tutto inclinato trascina di tanti pixel al secondo (col
    // sinistro 0.3 gradi a pixel: 150 gradi al secondo); i grilletti girano
    // la rotella di tanti scatti al secondo (120 = uno scatto).
    static constexpr float kPadDragPixelsPerSecond = 500.0f;
    static constexpr float kPadWheelPerSecond = 480.0f;

private:
    // I comandi della guida, se e' accesa (GLWidget::driveActive).
    DriveControls* driveControls() const;
    // IL TOCCO DELLA GUIDA: gli stick, i tastini e il doppio tocco
    // (DriveControls). Ogni dito ha il suo ruolo dal momento in cui si appoggia.
    void handleDriveTouch(QTouchEvent* event, DriveControls* c);
    void releaseDriveTouches(DriveControls* c);
    // IL TRASCINAMENTO, da chiunque venga (mouse, pad): (dx, dy) pixel della
    // vista coi tasti dati, Maiuscole per `towardW`. `clickPending`: per il
    // mouse il sinistro non e' ancora uscito dalla deadzone del clic (in 2D
    // non gira la texture, e in 3D gira ma non conta come vista mossa); il pad
    // non ha clic. true = la vista si e' mossa davvero.
    bool dragView(bool left, bool right, float dx, float dy, bool towardW, bool clickPending);
    // LA ROTELLA (mouse, grilletti del pad): `delta` in unita' di
    // angleDelta().y() (120 = uno scatto). In 2D la rotella del mouse zooma di
    // un passo fisso per evento; `continuous` (pad) in proporzione a delta.
    // true = ha zoomato la vista o scalato la traiettoria del path.
    bool wheelView(float delta, bool continuous);

    GLWidget* m_glWidget;
    // La guida: dove sono stati premuti il sinistro (cloche) e il destro (rollio).
    QPointF m_drivePress;
    QPointF m_driveRollPress;
    // Le dita sulla vista a guida accesa: che cosa comanda ciascuna (stick
    // sinistro, destro, o un tastino), dove si e' appoggiata e quando, e se si
    // e' mossa (un tocco breve e fermo conta per il doppio tocco).
    struct DriveTouch {
        int role = -1;          // kDriveLeft, kDriveRight, o kDriveButton + tastino
        QPointF pressPos;
        qint64 pressMs = 0;
        bool moved = false;
    };
    static constexpr int kDriveLeft = 0, kDriveRight = 1, kDriveButton = 10;
    QHash<int, DriveTouch> m_driveTouches;
    QElapsedTimer m_driveTouchClock;
    qint64 m_driveLastTapMs = -1;
    QPointF m_driveLastTapPos;

    // Stato Mouse. In virgola mobile: il trackpad da' posizioni frazionarie, e
    // la differenza troncata dall'ultima posizione arrotondata perdeva i passi
    // sotto il pixel -- trascinando piano la texture o la scena restavano
    // indietro rispetto al puntatore.
    QPointF m_lastMousePos;

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
