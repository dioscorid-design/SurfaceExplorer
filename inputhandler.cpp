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

    // =========================================================
    // --- GESTIONE VISTA 2D (FLAT) ---
    // =========================================================
    if (m_glWidget->isFlatView()) {

        // TASTO SINISTRO: ROTAZIONE CONTINUA
        if (event->buttons() & Qt::LeftButton) {
            int dist = (event->pos() - m_pressPos).manhattanLength();
            if (dist > 5) {
                m_isClickCandidate = false;
            }

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
    else {
        // TASTO SINISTRO: Precession + Nutation (Invariato)
        if (event->buttons() & Qt::LeftButton) {
            float sensitivity = 0.3f;
            bool isCameraMode = false;

            if (isCameraMode) {
                m_glWidget->addCameraRotation(dx * sensitivity, dy * sensitivity);
            } else {
                // Passiamo 0.0f come terzo argomento (Spin)
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

    // >>> 1. RIMOSSO IL BLOCCO: Ora il touch funziona sempre, sia in 3D che in 4D <<<
    // if (!m_glWidget->is4DActive()) return false;

    QTouchEvent *touchEvent = static_cast<QTouchEvent *>(e);
    const auto &points = touchEvent->points();
    int pointCount = points.count();

    // Reset quando si alzano le dita
    if (e->type() == QEvent::TouchEnd || pointCount == 0) {
        m_lastPointCount = 0;
        return true;
    }

    // Gestione transizioni (appena tocco lo schermo o cambio numero di dita)
    if (pointCount != m_lastPointCount || e->type() == QEvent::TouchBegin) {
        if (pointCount == 1) {
            m_lastTouchPos = points[0].position();
        }
        else if (pointCount == 2) {
            // Inizializza la distanza per il Pinch
            QPointF p1 = points[0].position();
            QPointF p2 = points[1].position();
            m_lastPinchDist = QLineF(p1, p2).length();
        }
        m_lastPointCount = pointCount;
        return true;
    }

    // =========================================================
    // A. 1 DITO: ROTAZIONE (Uguale al Mouse Left-Drag)
    // =========================================================
    if (pointCount == 1) {
        QPointF currentPos = points[0].position();
        float dx = currentPos.x() - m_lastTouchPos.x();
        float dy = currentPos.y() - m_lastTouchPos.y();

        // Sensibilità touch (spesso serve un po' più bassa del mouse)
        float rotSens = 0.3f;

        // Recuperiamo la modalità di rotazione corrente (Oggetto vs Camera)
        // per mantenere coerenza con il comportamento Desktop
        bool isCameraMode = false;

        if (isCameraMode) {
            m_glWidget->addCameraRotation(dx * rotSens, dy * rotSens);
        } else {
            // Rotazione Oggetto (Precessione/Nutazione)
            m_glWidget->addObjectRotation(dx * rotSens, dy * rotSens, 0.0f);
        }

        m_lastTouchPos = currentPos;
    }
    // =========================================================
    // B. 2 DITA: PINCH TO ZOOM
    // =========================================================
    else if (pointCount == 2) {
        QPointF p1 = points[0].position();
        QPointF p2 = points[1].position();

        // Calcola la distanza attuale tra le dita
        float currentDist = QLineF(p1, p2).length();

        // Calcola la differenza rispetto al frame precedente
        // delta > 0: Allargo le dita (Zoom In / Avvicino)
        // delta < 0: Stringo le dita (Zoom Out / Allontano)
        float delta = currentDist - m_lastPinchDist;

        // Sensibilità Zoom
        float zoomSens = 0.05f;

        // Applica lo zoom (usiamo la stessa funzione della rotella del mouse)
        if (std::abs(delta) > 0.5f) { // Piccola soglia per evitare tremolii
            m_glWidget->zoomCamera(delta * zoomSens);
        }

        // Aggiorna la distanza per il prossimo frame
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
    else {
        float zoomSpeed = 0.01f;
        m_glWidget->zoomCamera(delta * zoomSpeed);
    }
}
