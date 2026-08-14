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

    // Se premo il sinistro, salvo la posizione per capire se sarà un Click o un Drag
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_isClickCandidate = true;
    }
}

void InputHandler::handleMouseRelease(QMouseEvent* event)
{
    // Solo in 2D e solo se rilascio il tasto sinistro
    if (m_glWidget->isFlatView() && event->button() == Qt::LeftButton) {

        // Se m_isClickCandidate è ancora true, significa che NON abbiamo trascinato
        // Quindi è un click secco -> Ruota di 90°
        if (m_isClickCandidate) {
            m_glWidget->rotateFlat90();
        }
    }
}

void InputHandler::handleMouseMove(QMouseEvent* event)
{
    int dx = event->position().x() - m_lastMousePos.x();
    int dy = event->position().y() - m_lastMousePos.y();

    // DEADZONE, PRIMA DEL RAMO DI VISTA: superati 5 px col sinistro premuto non
    // e' piu' un clic secco ma un trascinamento. Viveva DENTRO il ramo 2D, e in
    // 3D/4D quindi non veniva mai spenta: al release wasClickWithoutDrag()
    // rispondeva "clic" anche dopo aver ruotato l'oggetto, e GLWidget non
    // emetteva userMovedView() -- cioe' ruotare col mouse non sporcava la scena
    // e l'avviso "vuoi salvare?" non compariva (lo zoom si', perche' la
    // rotellina emette il segnale direttamente).
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
            }
        }
        // TASTO DESTRO: SPOSTAMENTO (PAN)
        else if (event->buttons() & Qt::RightButton) {
            float currentZoom = m_glWidget->getFlatZoom();
            // Lo spostamento scala con lo zoom: più sei vicino, meno il mouse sposta l'immagine
            float panSens = 0.002f / currentZoom;

            QVector2D currentPan = m_glWidget->getFlatPan();
            m_glWidget->setFlatPan(currentPan.x() - dx * panSens, currentPan.y() + dy * panSens);
        }
    }

    // =========================================================
    // --- GESTIONE VISTA 3D / 4D  ---
    // =========================================================
    // Mentre un path anima la telecamera, la rotazione manuale 3D/4D e' bloccata
    // (la vista 2D del dock Script sopra resta invece libera).
    else if (!m_glWidget->isPathAnimating()) {
        // TASTO SINISTRO: Precession + Nutation (Invariato)
        if (event->buttons() & Qt::LeftButton) {
            float sensitivity = 0.3f;
            bool isCameraMode = false;

            if (isCameraMode) {
                m_glWidget->addCameraRotation(dx * sensitivity, dy * sensitivity);
            } else {
                // Passiamo 0.0f come terzo argomento (Spin)
                m_glWidget->markUserRotated();
                m_glWidget->addObjectRotation(dx * sensitivity, dy * sensitivity, 0.0f);
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
            m_glWidget->markUserRotated();
            m_glWidget->addObjectRotation(dx * rotSens, dy * rotSens, 0.0f);
            // Oltre la deadzone e' un trascinamento vero, non un tap: la vista
            // e' stata mossa dall'utente.
            if (!m_isClickCandidate) m_touchMovedView = true;
        }
        m_lastTouchPos = currentPos;
    }

    // --- DUE DITA: SOLO ZOOM ---
    else if (pointCount == 2) {
        QPointF p1 = points[0].position();
        QPointF p2 = points[1].position();
        float currentDist = QLineF(p1, p2).length();
        float deltaDist = currentDist - m_lastPinchDist;

        if (std::abs(deltaDist) > 0.5f) {
            if (m_glWidget->isFlatView()) {
                // Modalità 2D: ZOOM fluido
                float currentZoom = m_glWidget->getFlatZoom();
                // Moltiplicatore per rendere lo zoom scalare (es. 0.005 è la sensibilità)
                float zoomFactor = 1.0f + (deltaDist * 0.005f);
                m_glWidget->setFlatZoom(currentZoom * zoomFactor);
                m_touchMovedView = true;
            } else if (!m_glWidget->isPathAnimating()) {
                // Modalità 3D: ZOOM della camera (bloccato durante un path)
                m_glWidget->zoomCamera(deltaDist * 0.05f);
                m_touchMovedView = true;
            }
        }
        m_lastPinchDist = currentDist;
    }

    return true;
}

void InputHandler::handleWheel(QWheelEvent* event)
{
    float delta = event->angleDelta().y();
    if (delta == 0) return;

    // --- GESTIONE ZOOM 2D (TEXTURE) ---
    if (m_glWidget->isFlatView()) {
        float currentZoom = m_glWidget->getFlatZoom();

        // Sensibilità fissa e reattiva (15% per ogni "scatto" di rotellina)
        float zoomFactor = (delta > 0) ? 1.15f : (1.0f / 1.15f);

        m_glWidget->setFlatZoom(currentZoom * zoomFactor);
    }
    // --- GESTIONE ZOOM 3D / 4D ---
    // Bloccato mentre un path controlla la telecamera (lo zoom 2D sopra resta libero).
    else if (!m_glWidget->isPathAnimating()) {
        float zoomSpeed = 0.01f;
        m_glWidget->zoomCamera(delta * zoomSpeed);
    }
}
