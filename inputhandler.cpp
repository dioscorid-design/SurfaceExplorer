#include "inputhandler.h"
#include "glwidget.h"
#include "surfaceengine.h"
#include "cameradrive.h"
#include "gamepad.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

InputHandler::InputHandler(GLWidget* parentWidget)
    : QObject(parentWidget), m_glWidget(parentWidget)
{
    m_isClickCandidate = false;
}

DriveControls* InputHandler::driveControls() const
{
    return m_glWidget->driveActive() ? m_glWidget->driveControls() : nullptr;
}

namespace {
// LA CLOCHE DELLA GUIDA: lo scostamento in pixel dal punto premuto, in frazioni
// del raggio (CameraDrive::kStickRadius, o kTouchStickRadius per gli stick a
// tocco), con una zona morta di 4 pixel al centro (la mano non sta mai ferma
// del tutto) e il pieno oltre il raggio.
float stickAxis(qreal pixels, float radius = CameraDrive::kStickRadius)
{
    constexpr float kDeadZone = 4.0f;
    const float d = float(pixels);
    if (std::abs(d) <= kDeadZone) return 0.0f;
    const float v = (d - std::copysign(kDeadZone, d)) / (radius - kDeadZone);
    return std::clamp(v, -1.0f, 1.0f);
}

// Il comando tenuto da un tastino a tocco.
bool *touchButtonControl(DriveControls *c, int button)
{
    switch (button) {
    case CameraDrive::TouchLiftUp:   return &c->liftUp;
    case CameraDrive::TouchLiftDown: return &c->liftDown;
    case CameraDrive::TouchWPlus:    return &c->wPlus;
    case CameraDrive::TouchWMinus:   return &c->wMinus;
    default:                         return nullptr;
    }
}
}

void InputHandler::handleMousePress(QMouseEvent* event)
{
    m_lastMousePos = event->position();

    // LA GUIDA LIBERA: il sinistro prende la cloche, il destro il rollio, dal
    // punto premuto. Niente gesto della vista: la camera la muove la guida.
    if (DriveControls* c = driveControls()) {
        if (event->button() == Qt::LeftButton) {
            c->stick = true;
            c->stickX = c->stickY = 0.0f;
            m_drivePress = event->position();
        } else if (event->button() == Qt::RightButton) {
            c->rollStick = true;
            c->rollX = 0.0f;
            m_driveRollPress = event->position();
        }
        return;
    }

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
    // La guida: la cloche lasciata torna al centro.
    if (DriveControls* c = driveControls()) {
        if (event->button() == Qt::LeftButton) {
            c->stick = false;
            c->stickX = c->stickY = 0.0f;
        } else if (event->button() == Qt::RightButton) {
            c->rollStick = false;
            c->rollX = 0.0f;
        }
        if (event->buttons() == Qt::NoButton) m_mouseGestureActive = false;
        return;
    }

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
    // La guida: la cloche (sinistro) e il rollio (destro) seguono lo
    // scostamento dal punto premuto.
    if (DriveControls* c = driveControls()) {
        if (c->stick) {
            c->stickX = stickAxis(event->position().x() - m_drivePress.x());
            c->stickY = stickAxis(event->position().y() - m_drivePress.y());
        }
        if (c->rollStick) c->rollX = stickAxis(event->position().x() - m_driveRollPress.x());
        m_lastMousePos = event->position();
        return;
    }

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
    // con Maiuscole (Shift) verso W; destro: spostamento. Nessuna deadzone:
    // anche i primi pixel muovono. Non conta come vista mossa: la traiettoria
    // si scrive nei campi del path.
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
    // LA GUIDA A TOCCO: le dita sono stick e tastini, la scena non gira.
    if (DriveControls* c = driveControls()) {
        if (e->type() == QEvent::TouchCancel) {
            releaseDriveTouches(c);
            return true;
        }
        if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd) {
            handleDriveTouch(static_cast<QTouchEvent*>(e), c);
            return true;
        }
    }
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

    // --- DUE DITA: ZOOM, E SPOSTAMENTO (OGGETTO SELEZIONATO, SCENA, TEXTURE 2D) ---
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
        if (!moved.isNull()) {
            const float mx = float(moved.x()), my = float(moved.y());
            bool panned = false;
            if (m_glWidget->isFlatView())              panned = m_glWidget->panFlatOnScreen(mx, my);
            else if (m_glWidget->objectDragActive())   panned = m_glWidget->dragObjectOnScreen(mx, my);
            else if (m_glWidget->sceneDragActive())    panned = m_glWidget->dragSceneOnScreen(mx, my);
            if (panned) m_touchMovedView = true;
        }

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
    const float delta = float(event->angleDelta().y());
    if (delta == 0) return false;

    // La guida: la rotella e' l'acceleratore (uno scatto = 120). Non e' uno
    // zoom: la vista mossa la segnala la guida (CameraDrive::userDrove). La
    // coda d'inerzia del trackpad (ScrollMomentum, dopo aver alzato le dita)
    // non conta: continuava ad accelerare da sola.
    if (DriveControls* c = driveControls()) {
        if (event->phase() != Qt::ScrollMomentum) c->wheelSteps += delta / 120.0f;
        return false;
    }
    return wheelView(delta, false);
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

bool InputHandler::handleKey(QKeyEvent* event, bool pressed)
{
    DriveControls* c = driveControls();
    if (!c) return false;
    bool* key = nullptr;
    switch (event->key()) {
    case Qt::Key_W:     key = &c->throttleUp; break;
    case Qt::Key_S:     key = &c->throttleDown; break;
    case Qt::Key_A:
    case Qt::Key_Left:  key = &c->yawLeft; break;
    case Qt::Key_D:
    case Qt::Key_Right: key = &c->yawRight; break;
    case Qt::Key_Up:    key = &c->pitchUp; break;
    case Qt::Key_Down:  key = &c->pitchDown; break;
    case Qt::Key_Q:     key = &c->rollLeft; break;
    case Qt::Key_E:     key = &c->rollRight; break;
    case Qt::Key_R:     key = &c->liftUp; break;
    case Qt::Key_F:     key = &c->liftDown; break;
    case Qt::Key_X:     key = &c->wPlus; break;
    case Qt::Key_Z:     key = &c->wMinus; break;
    case Qt::Key_Space: key = &c->brake; break;
    case Qt::Key_Shift: key = &c->boost; break;
    case Qt::Key_Escape:
        if (pressed && !event->isAutoRepeat()) m_glWidget->requestDriveExit();
        return true;
    // + e -: tutte le velocita' per due o a meta' (anche = per il + senza
    // Maiuscole delle tastiere americane, e il tastierino).
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Minus:
        if (pressed && !event->isAutoRepeat()) {
            const bool up = event->key() != Qt::Key_Minus;
            c->speedScale = std::clamp(up ? c->speedScale * 2.0f : c->speedScale * 0.5f,
                                       DriveControls::kMinSpeedScale, DriveControls::kMaxSpeedScale);
        }
        return true;
    default:
        return false;
    }
    // La ripetizione automatica (premuto/rilasciato a raffica, su macOS) non
    // cambia lo stato: il tasto e' premuto finche' non si rilascia davvero.
    if (!event->isAutoRepeat()) *key = pressed;
    return true;
}

void InputHandler::releaseDriveTouches(DriveControls* c)
{
    for (auto it = m_driveTouches.constBegin(); it != m_driveTouches.constEnd(); ++it)
        if (bool* held = touchButtonControl(c, it->role - kDriveButton)) *held = false;
    m_driveTouches.clear();
    c->touchLeft = c->touchRight = false;
    c->touchLeftX = c->touchLeftY = c->touchRightX = c->touchRightY = 0.0f;
}

void InputHandler::handleDriveTouch(QTouchEvent* event, DriveControls* c)
{
    if (!m_driveTouchClock.isValid()) m_driveTouchClock.start();
    const qint64 now = m_driveTouchClock.elapsed();
    const QSizeF view = m_glWidget->size();
    // Un tocco breve e fermo; due a poca distanza di tempo e di spazio sono il
    // doppio tocco che frena.
    constexpr qint64 kTapMs = 250, kDoubleTapMs = 350;
    constexpr qreal kTapMove = 12.0, kDoubleTapDistance = 50.0;
    for (const QEventPoint &p : event->points()) {
        const int id = p.id();
        const QPointF pos = p.position();
        if (p.state() == QEventPoint::Pressed) {
            DriveTouch t;
            t.pressPos = pos;
            t.pressMs = now;
            const int button = CameraDrive::touchButtonAt(pos, view, m_glWidget->driveTouchButtons());
            bool taken = false;
            for (const DriveTouch &other : std::as_const(m_driveTouches)) {
                if (other.role == (pos.x() < view.width() / 2.0 ? kDriveLeft : kDriveRight)) taken = true;
            }
            if (button >= 0) {
                t.role = kDriveButton + button;
                if (bool* held = touchButtonControl(c, button)) *held = true;
            } else if (!taken && pos.x() < view.width() / 2.0) {
                t.role = kDriveLeft;
                c->touchLeft = true;
                c->touchLeftOrigin = pos;
                c->touchLeftX = c->touchLeftY = 0.0f;
            } else if (!taken) {
                t.role = kDriveRight;
                c->touchRight = true;
                c->touchRightOrigin = pos;
                c->touchRightX = c->touchRightY = 0.0f;
            }
            // Il secondo tocco di un doppio tocco (sugli stick, non sui tastini).
            if (button < 0 && m_driveLastTapMs >= 0 && now - m_driveLastTapMs < kDoubleTapMs
                && QLineF(pos, m_driveLastTapPos).length() < kDoubleTapDistance) {
                c->brakeTap = true;
                m_driveLastTapMs = -1;
            }
            m_driveTouches.insert(id, t);
            continue;
        }
        auto it = m_driveTouches.find(id);
        if (it == m_driveTouches.end()) continue;
        DriveTouch &t = it.value();
        if (QLineF(pos, t.pressPos).length() > kTapMove) t.moved = true;
        if (p.state() == QEventPoint::Released) {
            if (t.role == kDriveLeft) {
                c->touchLeft = false;
                c->touchLeftX = c->touchLeftY = 0.0f;
            } else if (t.role == kDriveRight) {
                c->touchRight = false;
                c->touchRightX = c->touchRightY = 0.0f;
            } else if (bool* held = touchButtonControl(c, t.role - kDriveButton)) {
                *held = false;
            }
            if ((t.role == kDriveLeft || t.role == kDriveRight) && !t.moved && now - t.pressMs < kTapMs) {
                m_driveLastTapMs = now;
                m_driveLastTapPos = pos;
            }
            m_driveTouches.erase(it);
            continue;
        }
        // Il dito si muove: lo stick segue lo scostamento dal punto d'appoggio.
        if (t.role == kDriveLeft) {
            c->touchLeftX = stickAxis(pos.x() - t.pressPos.x(), CameraDrive::kTouchStickRadius);
            c->touchLeftY = stickAxis(pos.y() - t.pressPos.y(), CameraDrive::kTouchStickRadius);
        } else if (t.role == kDriveRight) {
            c->touchRightX = stickAxis(pos.x() - t.pressPos.x(), CameraDrive::kTouchStickRadius);
            c->touchRightY = stickAxis(pos.y() - t.pressPos.y(), CameraDrive::kTouchStickRadius);
        }
    }
    // Fine del tocco: nessun dito resta, qualunque cosa abbia perso l'evento.
    if (event->type() == QEvent::TouchEnd) releaseDriveTouches(c);
}

bool InputHandler::handleGamepad(const GamepadState& pad, float dt)
{
    // LA GUIDA LIBERA: il pad e' un suo comando (DriveControls::setPad), non gira la vista.
    if (DriveControls* c = driveControls()) { c->setPad(pad); return false; }

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
