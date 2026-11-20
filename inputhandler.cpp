#include "inputhandler.h"
#include "glwidget.h"
#include "surfaceengine.h"
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
    m_lastMousePos = event->pos();

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
    int dx = event->position().x() - m_lastMousePos.x();
    int dy = event->position().y() - m_lastMousePos.y();

    // DEADZONE, PRIMA DEL RAMO DI VISTA: superati 5 px col sinistro premuto non
    // e' piu' un clic secco ma un trascinamento. Viveva DENTRO il ramo 2D, e in
    // 3D/4D quindi non veniva mai spenta: ruotare l'oggetto col mouse contava
    // come clic, e GLWidget non emetteva userMovedView() -- cioe' ruotare col
    // mouse non sporcava la scena e l'avviso "vuoi salvare?" non compariva.
    if (event->buttons() & Qt::LeftButton) {
        if ((event->pos() - m_pressPos).manhattanLength() > 5)
            m_isClickCandidate = false;
    }

    // =========================================================
    // --- GESTIONE VISTA 2D (FLAT) ---
    // =========================================================
    if (m_glWidget->isFlatView()) {

        // TASTO SINISTRO: ROTAZIONE CONTINUA
        if (event->buttons() & Qt::LeftButton) {
            if (!m_isClickCandidate) {
                float sensitivity = 0.5f;
                m_glWidget->addFlatRotation(dx * sensitivity);
                if (m_mouseGestureActive) m_mouseMovedView = true;
            }
        }
        // TASTO DESTRO: SPOSTAMENTO (PAN)
        else if (event->buttons() & Qt::RightButton) {
            float currentZoom = m_glWidget->getFlatZoom();
            // Lo spostamento scala con lo zoom: più sei vicino, meno il mouse sposta l'immagine
            float panSens = 0.002f / currentZoom;

            QVector2D currentPan = m_glWidget->getFlatPan();
            m_glWidget->setFlatPan(currentPan.x() - dx * panSens, currentPan.y() + dy * panSens);
            if (m_mouseGestureActive && (dx != 0 || dy != 0)) m_mouseMovedView = true;
        }
    }

    // =========================================================
    // --- GESTIONE VISTA 3D / 4D  ---
    // =========================================================
    // Il mouse sulla TRAIETTORIA del path della camera (tasto MOUSE: PATH, vedi
    // GLWidget::setPathReshapeGate), in corsa o fermo. Sinistro: rotazione,
    // con Maiuscole (Shift) verso W; destro: spostamento. Nessuna deadzone:
    // anche i primi pixel muovono.
    else if (m_glWidget->pathReshapeActive()) {
        if (dx != 0 || dy != 0) {
            if (event->buttons() & Qt::LeftButton) {
                const bool towardW = event->modifiers() & Qt::ShiftModifier;
                m_glWidget->requestPathReshape(towardW ? GLWidget::ReshapeRotateW : GLWidget::ReshapeRotate,
                                               float(dx), float(dy));
            } else if (event->buttons() & Qt::RightButton) {
                m_glWidget->requestPathReshape(GLWidget::ReshapeTranslate, float(dx), float(dy));
            }
        }
    }
    // IL DESTRO SPOSTA L'OGGETTO SELEZIONATO (GLWidget::dragObjectOnScreen):
    // il suo centro resta sotto il puntatore. Alle condizioni dei tasti del
    // dock 3D, path in corsa compreso; non sull'ambiente ne' su un oggetto
    // fisso, non durante il REC (objectDragActive). Nessuna deadzone, come
    // sulla traiettoria.
    else if ((event->buttons() & Qt::RightButton) && m_glWidget->objectDragActive()) {
        if ((dx != 0 || dy != 0) && m_glWidget->dragObjectOnScreen(float(dx), float(dy)) && m_mouseGestureActive)
            m_mouseMovedView = true;
    }
    // Mentre un path anima la telecamera (MOUSE: SCENE) la rotazione manuale
    // 3D/4D e' bloccata (la vista 2D del dock Script sopra resta invece libera).
    else if (!m_glWidget->isPathAnimating()) {
        // TASTO SINISTRO: Precession + Nutation (Invariato)
        if (event->buttons() & Qt::LeftButton) {
            float sensitivity = 0.3f;
            bool isCameraMode = false;

            if (isCameraMode) {
                m_glWidget->addCameraRotation(dx * sensitivity, dy * sensitivity);
            } else if (m_glWidget->dragOrbitsHyperbolicCamera()) {
                // Scena in H3: gira il mondo attorno al centro, cielo compreso.
                m_glWidget->orbitHyperbolicCamera(dx * sensitivity, dy * sensitivity);
                if (m_mouseGestureActive && !m_isClickCandidate) m_mouseMovedView = true;
            } else {
                // Passiamo 0.0f come terzo argomento (Spin)
                m_glWidget->markUserRotated();
                m_glWidget->addObjectRotation(dx * sensitivity, dy * sensitivity, 0.0f);
                // Oltre la deadzone e' un trascinamento vero, non un clic: la
                // vista e' stata mossa dall'utente (stesso criterio del touch).
                if (m_mouseGestureActive && !m_isClickCandidate) m_mouseMovedView = true;
            }
        }
    }

    m_lastMousePos = event->pos();
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

    // --- UN DITO: ROTAZIONE CONTINUA ---
    if (pointCount == 1) {
        QPointF currentPos = points[0].position();
        float dx = currentPos.x() - m_lastTouchPos.x();
        float dy = currentPos.y() - m_lastTouchPos.y();

        // Controllo "Deadzone": se il dito si muove oltre 10 pixel, non è più un Tap ma un trascinamento
        if (m_isClickCandidate && QLineF(currentPos, m_pressPos).length() > 10.0f) {
            m_isClickCandidate = false;
        }

        if (m_glWidget->isFlatView()) {
            // Modalità 2D: Trascinamento = ROTAZIONE CONTINUA
            if (!m_isClickCandidate) {
                float sensitivity = 0.5f;
                // Usa lo spostamento orizzontale (dx) per ruotare, come fa il click sinistro del mouse
                m_glWidget->addFlatRotation(dx * sensitivity);
                m_touchMovedView = true;
            }
        } else if (!m_glWidget->isPathAnimating()) {
            // Modalità 3D/4D: Rotazione standard dell'oggetto (bloccata durante un path)
            float rotSens = 0.3f;
            if (m_glWidget->dragOrbitsHyperbolicCamera()) {
                // Scena in H3: gira il mondo attorno al centro, cielo compreso.
                m_glWidget->orbitHyperbolicCamera(dx * rotSens, dy * rotSens);
            } else {
                m_glWidget->markUserRotated();
                m_glWidget->addObjectRotation(dx * rotSens, dy * rotSens, 0.0f);
            }
            // Oltre la deadzone e' un trascinamento vero, non un tap: la vista
            // e' stata mossa dall'utente.
            if (!m_isClickCandidate) m_touchMovedView = true;
        }
        m_lastTouchPos = currentPos;
    }

    // --- DUE DITA: ZOOM, E SPOSTAMENTO DELL'OGGETTO SELEZIONATO ---
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
        const QPointF centre = (p1 + p2) / 2.0;
        const QPointF moved = centre - m_lastPinchCentre;
        m_lastPinchCentre = centre;
        if (!m_glWidget->isFlatView() && m_glWidget->objectDragActive() && !moved.isNull()
            && m_glWidget->dragObjectOnScreen(float(moved.x()), float(moved.y())))
            m_touchMovedView = true;

        if (std::abs(deltaDist) > 0.5f) {
            if (m_glWidget->isFlatView()) {
                // Modalità 2D: ZOOM fluido
                float currentZoom = m_glWidget->getFlatZoom();
                // Moltiplicatore per rendere lo zoom scalare (es. 0.005 è la sensibilità)
                float zoomFactor = 1.0f + (deltaDist * 0.005f);
                m_glWidget->setFlatZoom(currentZoom * zoomFactor);
                m_touchMovedView = true;
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
    float delta = event->angleDelta().y();
    if (delta == 0) return false;

    // --- GESTIONE ZOOM 2D (TEXTURE) ---
    if (m_glWidget->isFlatView()) {
        float currentZoom = m_glWidget->getFlatZoom();

        // Sensibilità fissa e reattiva (15% per ogni "scatto" di rotellina)
        float zoomFactor = (delta > 0) ? 1.15f : (1.0f / 1.15f);

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
