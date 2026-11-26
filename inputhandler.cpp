#include "inputhandler.h"
#include "glwidget.h"
#include "surfaceengine.h"
#include "gamepad.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

InputHandler::InputHandler(GLWidget* parentWidget)
    : QObject(parentWidget), m_glWidget(parentWidget)
{
    m_isClickCandidate = false;
}

void InputHandler::handleMousePress(QMouseEvent* event)
{
    m_lastMousePos = event->position();

    // Inizio del gesto: solo il PRIMO tasto premuto lo apre (un secondo tasto
    // durante un trascinamento lo prosegue, non ne azzera l'esito).
    if (!m_mouseGestureActive) {
        m_mouseGestureActive = true;
        m_mouseMovedView = false;
    }

    // Se premo il sinistro, salvo la posizione per capire se sarà un Click o un Drag
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_isClickCandidate = true;
    }
}

void InputHandler::handleMouseRelease(QMouseEvent* event)
{
    // Release ORFANO (il press non e' arrivato qui): non chiude nessun gesto
    // della vista. Senza questa guardia in 2D un m_isClickCandidate rimasto
    // vero dall'ultimo clic ruotava la texture di 90 gradi.
    if (!m_mouseGestureActive) return;

    // Solo in 2D e solo se rilascio il tasto sinistro
    if (m_glWidget->isFlatView() && event->button() == Qt::LeftButton) {

        // Se m_isClickCandidate è ancora true, significa che NON abbiamo trascinato
        // Quindi è un click secco -> Ruota di 90°
        if (m_isClickCandidate) {
            m_glWidget->rotateFlat90();
        }
    }

    // Fine del gesto quando non resta premuto nessun tasto.
    if (event->buttons() == Qt::NoButton) m_mouseGestureActive = false;
}

void InputHandler::handleMouseMove(QMouseEvent* event)
{
    const float dx = float(event->position().x() - m_lastMousePos.x());
    const float dy = float(event->position().y() - m_lastMousePos.y());

    // DEADZONE, PRIMA DEL RAMO DI VISTA: superati 5 px col sinistro premuto non
    // e' piu' un clic secco ma un trascinamento. Viveva DENTRO il ramo 2D, e in
    // 3D/4D quindi non veniva mai spenta: ruotare l'oggetto col mouse contava
    // come clic, e GLWidget non emetteva userMovedView() -- cioe' ruotare col
    // mouse non sporcava la scena e l'avviso "vuoi salvare?" non compariva.
    if (event->buttons() & Qt::LeftButton) {
        if ((event->pos() - m_pressPos).manhattanLength() > 5)
            m_isClickCandidate = false;
    }

    // Il gesto vero e proprio, in comune col pad (dragView). Conta come vista
    // mossa solo dentro un gesto iniziato qui (vedi takeMouseMovedView).
    const bool left = event->buttons() & Qt::LeftButton;
    const bool right = event->buttons() & Qt::RightButton;
    if (dragView(left, right, dx, dy, event->modifiers() & Qt::ShiftModifier, m_isClickCandidate)
        && m_mouseGestureActive)
        m_mouseMovedView = true;

    m_lastMousePos = event->position();
}

bool InputHandler::dragView(bool left, bool right, float dx, float dy, bool towardW, bool clickPending)
{
    // =========================================================
    // --- GESTIONE VISTA 2D (FLAT) ---
    // =========================================================
    if (m_glWidget->isFlatView()) {
        // TASTO SINISTRO: ROTAZIONE CONTINUA
        if (left) {
            if (clickPending) return false;
            const float sensitivity = 0.5f;
            m_glWidget->addFlatRotation(dx * sensitivity);
            return true;
        }
        // TASTO DESTRO: SPOSTAMENTO (PAN). La texture segue il puntatore, a ogni
        // zoom e rotazione (GLWidget::panFlatOnScreen).
        if (right) return m_glWidget->panFlatOnScreen(dx, dy);
        return false;
    }

    // =========================================================
    // --- GESTIONE VISTA 3D / 4D  ---
    // =========================================================
    // Il mouse sulla TRAIETTORIA del path della camera (tasto MOUSE: PATH, vedi
    // GLWidget::setPathReshapeGate), in corsa o fermo. Sinistro: rotazione,
    // con Maiuscole (Shift) verso W; destro: spostamento. Le dita passano di
    // qui: un dito come il sinistro, il punto medio di due come il destro
    // (handleTouch). Nessuna deadzone: anche i primi pixel muovono. Non conta
    // come vista mossa: la traiettoria si scrive nei campi del path.
    if (m_glWidget->pathReshapeActive()) {
        if (dx != 0 || dy != 0) {
            if (left) {
                m_glWidget->requestPathReshape(towardW ? GLWidget::ReshapeRotateW : GLWidget::ReshapeRotate,
                                               dx, dy);
            } else if (right) {
                m_glWidget->requestPathReshape(GLWidget::ReshapeTranslate, dx, dy);
            }
        }
        return false;
    }
    // IL DESTRO SPOSTA L'OGGETTO SELEZIONATO (GLWidget::dragObjectOnScreen):
    // il suo centro resta sotto il puntatore. Alle condizioni dei tasti del
    // dock 3D, path in corsa compreso; non sull'ambiente ne' su un oggetto
    // fisso, non durante il REC (objectDragActive). Nessuna deadzone, come
    // sulla traiettoria.
    if (right && m_glWidget->objectDragActive())
        return (dx != 0 || dy != 0) && m_glWidget->dragObjectOnScreen(dx, dy);
    // ALTRIMENTI IL DESTRO FA SCORRERE LA SCENA (GLWidget::dragSceneOnScreen):
    // la camera scorre di lato e il centro della scena segue il puntatore.
    // Superficie sola, ambiente o oggetto fisso selezionati, path fermo con
    // MOUSE: SCENE; non a path in corsa ne' durante il REC (sceneDragActive).
    // Nessuna deadzone, come sull'oggetto.
    if (right && m_glWidget->sceneDragActive())
        return (dx != 0 || dy != 0) && m_glWidget->dragSceneOnScreen(dx, dy);
    // Mentre un path anima la telecamera (MOUSE: SCENE) la rotazione manuale
    // 3D/4D e' bloccata (la vista 2D sopra resta invece libera).
    if (m_glWidget->isPathAnimating() || !left) return false;
    // TASTO SINISTRO: Precession + Nutation
    const float sensitivity = 0.3f;
    if (m_glWidget->dragOrbitsHyperbolicCamera()) {
        // Scena in H3: gira il mondo attorno al centro, cielo compreso.
        m_glWidget->orbitHyperbolicCamera(dx * sensitivity, dy * sensitivity);
    } else {
        // Il terzo argomento (Spin) resta 0.
        m_glWidget->markUserRotated();
        m_glWidget->addObjectRotation(dx * sensitivity, dy * sensitivity, 0.0f);
    }
    // Oltre la deadzone e' un trascinamento vero, non un clic: la vista e'
    // stata mossa dall'utente (stesso criterio del touch).
    return !clickPending;
}

bool InputHandler::handleTouch(QEvent* e)
{
    if (e->type() != QEvent::TouchBegin &&
        e->type() != QEvent::TouchUpdate &&
        e->type() != QEvent::TouchEnd) {
        return false;
    }

    QTouchEvent *touchEvent = static_cast<QTouchEvent *>(e);
    const auto &points = touchEvent->points();
    int pointCount = points.count();

    // =========================================================
    // 1. GESTIONE FINE TOCCO (TouchEnd) - TAP (Rotazione 90°)
    // =========================================================
    if (e->type() == QEvent::TouchEnd || pointCount == 0) {
        // Se siamo in 2D e il dito non è stato trascinato, è un TAP -> Ruota 90°
        if (m_glWidget->isFlatView() && m_isClickCandidate) {
            m_glWidget->rotateFlat90();
        }
        m_lastPointCount = 0;
        m_isClickCandidate = false;
        return true;
    }

    // =========================================================
    // 2. TRANSIZIONI (TouchBegin o cambio numero di dita)
    // =========================================================
    if (pointCount != m_lastPointCount || e->type() == QEvent::TouchBegin) {
        if (pointCount == 1) {
            m_lastTouchPos = points[0].position();
            m_pressPos = m_lastTouchPos.toPoint(); // Memorizza per la deadzone del Tap
            m_isClickCandidate = true;             // Parte come potenziale Tap
        }
        else if (pointCount == 2) {
            QPointF p1 = points[0].position();
            QPointF p2 = points[1].position();
            m_lastPinchDist = QLineF(p1, p2).length();
            m_lastPinchCentre = (p1 + p2) / 2.0;
            m_isClickCandidate = false;            // Con due dita si annulla il Tap
        }
        m_lastPointCount = pointCount;
        return true;
    }

    // =========================================================
    // 3. MOVIMENTO (TouchUpdate)
    // =========================================================

    // --- UN DITO: COME IL TASTO SINISTRO ---
    if (pointCount == 1) {
        QPointF currentPos = points[0].position();
        float dx = currentPos.x() - m_lastTouchPos.x();
        float dy = currentPos.y() - m_lastTouchPos.y();

        // Controllo "Deadzone": se il dito si muove oltre 10 pixel, non è più un Tap ma un trascinamento
        if (m_isClickCandidate && QLineF(currentPos, m_pressPos).length() > 10.0f) {
            m_isClickCandidate = false;
        }

        // IL GESTO DEL SINISTRO (dragView), con le sue regole: in 2D la
        // texture gira oltre la deadzone; in 3D/4D la superficie, l'oggetto o
        // la scena, bloccati a path in corsa; con MOUSE: PATH (e sempre a path
        // in corsa, ambiente selezionato) GIRA LA TRAIETTORIA del path della
        // camera (richiesta dell'utente, 2026-11-26: prima le dita non la
        // modellavano). Senza Maiuscole: verso W solo col mouse.
        if (dragView(true, false, dx, dy, false, m_isClickCandidate)) m_touchMovedView = true;
        m_lastTouchPos = currentPos;
    }

    // --- DUE DITA: IL PINCH COME LA ROTELLA, IL PUNTO MEDIO COME IL DESTRO ---
    else if (pointCount == 2) {
        QPointF p1 = points[0].position();
        QPointF p2 = points[1].position();
        float currentDist = QLineF(p1, p2).length();
        float deltaDist = currentDist - m_lastPinchDist;

        // IL PUNTO MEDIO DELLE DITA SPOSTA L'OGGETTO SELEZIONATO (richiesta
        // dell'utente, 2026-11-20: su iPhone e iPad non c'e' il tasto destro
        // del mouse): il gemello del trascinamento col destro, alle stesse
        // condizioni (GLWidget::objectDragActive), insieme al pinch che ne
        // cambia la scala. Il centro dell'oggetto segue le dita.
        // Alle stesse condizioni il punto medio fa SCORRERE LA SCENA (richiesta
        // dell'utente, 2026-11-22, gemello del destro: GLWidget::sceneDragActive)
        // e, in vista 2D, la TEXTURE (panFlatOnScreen). Il pinch resta lo zoom.
        const QPointF centre = (p1 + p2) / 2.0;
        const QPointF moved = centre - m_lastPinchCentre;
        m_lastPinchCentre = centre;
        // Il gesto del destro (dragView): anche lo SPOSTAMENTO DELLA
        // TRAIETTORIA, alle condizioni del mouse (richiesta dell'utente,
        // 2026-11-26), che viene prima dell'oggetto e della scena.
        if (!moved.isNull() && dragView(false, true, float(moved.x()), float(moved.y()), false, false))
            m_touchMovedView = true;

        if (std::abs(deltaDist) > 0.5f) {
            if (m_glWidget->isFlatView()) {
                // Modalità 2D: ZOOM fluido
                float currentZoom = m_glWidget->getFlatZoom();
                // Moltiplicatore per rendere lo zoom scalare (es. 0.005 è la sensibilità)
                float zoomFactor = 1.0f + (deltaDist * 0.005f);
                m_glWidget->setFlatZoom(currentZoom * zoomFactor);
                m_touchMovedView = true;
            } else if (m_glWidget->pathReshapeActive()) {
                // La TRAIETTORIA si stringe o si allarga, come con la rotella
                // (wheelView): dita che si allontanano = piu' vicino = piu'
                // stretta. Un pixel di pinch vale 5 unita' di rotella, lo
                // stesso rapporto dello zoom della camera qui sotto (0.05
                // contro 0.01). Come il trascinamento, non conta come vista
                // mossa: la traiettoria si scrive nei campi del path.
                m_glWidget->requestPathReshape(GLWidget::ReshapeScale, 0.0f, deltaDist * 5.0f);
            } else if (m_glWidget->objectZoomActive() || !m_glWidget->isPathAnimating()) {
                // Modalità 3D: ZOOM della camera (bloccato durante un path) o,
                // con un oggetto della scena selezionato, la sua scala.
                m_glWidget->zoomCamera(deltaDist * 0.05f);
                m_touchMovedView = true;
            }
        }
        m_lastPinchDist = currentDist;
    }

    return true;
}

bool InputHandler::handleWheel(QWheelEvent* event)
{
    return wheelView(float(event->angleDelta().y()), false);
}

bool InputHandler::wheelView(float delta, bool continuous)
{
    if (delta == 0) return false;

    // --- GESTIONE ZOOM 2D (TEXTURE) ---
    if (m_glWidget->isFlatView()) {
        float currentZoom = m_glWidget->getFlatZoom();

        // Sensibilità fissa e reattiva (15% per ogni "scatto" di rotellina).
        // Il pad in proporzione: 15% a scatto (120) di grilletto.
        float zoomFactor = continuous ? std::pow(1.15f, delta / 120.0f)
                                      : ((delta > 0) ? 1.15f : (1.0f / 1.15f));

        m_glWidget->setFlatZoom(currentZoom * zoomFactor);
        return true;
    }
    // MOUSE: PATH: la rotella scala la traiettoria (vedi handleMouseMove).
    else if (m_glWidget->pathReshapeActive()) {
        m_glWidget->requestPathReshape(GLWidget::ReshapeScale, 0.0f, delta);
        return true;
    }
    // --- GESTIONE ZOOM 3D / 4D ---
    // Bloccato mentre un path controlla la telecamera (lo zoom 2D sopra resta libero).
    // Con un oggetto della scena selezionato e' la sua scala, path o no.
    else if (m_glWidget->objectZoomActive() || !m_glWidget->isPathAnimating()) {
        float zoomSpeed = 0.01f;
        m_glWidget->zoomCamera(delta * zoomSpeed);
        return true;
    }
    return false;
}

bool InputHandler::handleGamepad(const GamepadState& pad, float dt)
{
    // Gli stick come trascinamenti di (dx, dy) pixel in questo passo: y del
    // pad verso l'alto, dello schermo verso il basso. Uno stick fermo non
    // trascina niente (la zona morta l'ha gia' azzerato).
    const float pixels = kPadDragPixelsPerSecond * dt;
    bool moved = false;
    if (!pad.leftStick.isNull())
        moved |= dragView(true, false, float(pad.leftStick.x()) * pixels, -float(pad.leftStick.y()) * pixels,
                          false, false);
    if (!pad.rightStick.isNull())
        moved |= dragView(false, true, float(pad.rightStick.x()) * pixels, -float(pad.rightStick.y()) * pixels,
                          false, false);
    const float wheel = (pad.rightTrigger - pad.leftTrigger) * kPadWheelPerSecond * dt;
    if (wheel != 0.0f) moved |= wheelView(wheel, true);
    return moved;
}
