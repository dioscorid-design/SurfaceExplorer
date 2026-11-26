// cameradrive.cpp - La guida libera della camera. Vedi cameradrive.h.
#include "cameradrive.h"
#include "gamepad.h"

#include "glwidget.h"

#include <QGenericMatrix>
#include <QMatrix4x4>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
// IL VEICOLO. Velocita' in unita' della scena al secondo (in H3 convertite in
// raggi di curvatura, come gli scatti del dock 3D), angoli in radianti al
// secondo. I comandi danno un OBIETTIVO; la velocita' vera lo raggiunge con la
// costante di tempo (inerzia): approach() e' esatta per qualunque passo.
// TARATE SULLA SCENA D'APERTURA (camera a 4 unita' da una superficie larga
// 2-4): la prima taratura (crociera fino a 3, virate a 57 gradi al secondo,
// cloche lineare) era incontrollabile (segnalato dall'utente, 2026-11-21). Le
// velocita' di avanzamento si scalano coi tasti + e - (DriveControls::speedScale).
constexpr float kMaxCruise = 1.2f;      // la crociera piu' alta, avanti o indietro
constexpr float kThrottleRate = 0.4f;   // quanto cambia la crociera tenendo W o S, al secondo
constexpr float kWheelStep = 0.05f;     // uno scatto di rotella
constexpr float kBoost = 3.0f;          // Maiuscole: velocita' (non le virate) per tre
constexpr float kSpeedTau = 0.6f;
constexpr float kBrakeTau = 0.2f;       // Spazio: si ferma in fretta
constexpr float kTurnRate = 0.5f;       // virata e beccheggio coi tasti (~29 gradi al secondo)
constexpr float kStickRate = 0.7f;      // a cloche piena (~40 gradi al secondo)
constexpr float kRollRate = 0.8f;
constexpr float kTurnTau = 0.2f;
constexpr float kLiftRate = 0.3f;
constexpr float kLiftTau = 0.2f;
constexpr float kWRate = 0.25f;         // l'osservatore 4D verso W
// La quota del piano di sezione con un passo dieci volte piu' fine, come i
// tasti P+/P- (GLWidget::virtualMove: kSectionPMul contro obsSpeed).
constexpr float kSectionScale = 0.1f;
constexpr float kWTau = 0.2f;

float approach(float v, float target, float tau, float h)
{
    return target + (v - target) * std::exp(-h / tau);
}

QVector3D approach(const QVector3D &v, const QVector3D &target, float tau, float h)
{
    return target + (v - target) * std::exp(-h / tau);
}

float axis(bool plus, bool minus) { return float(plus) - float(minus); }

// La risposta della cloche: quadratica, fine vicino al centro e piena solo al
// bordo (lineare, mezzo raggio dava gia' meta' della virata piu' veloce).
float stickCurve(float v) { return v * std::abs(v); }

QQuaternion rotationOf(const QMatrix4x4 &m)
{
    QMatrix3x3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r(i, j) = m(i, j);
    return QQuaternion::fromRotationMatrix(r).normalized();
}

// Fra due camere vicine di H3: a = 0 la prima, 1 la seconda, in mezzo la
// traslazione e la rotazione che portano dall'una all'altra, a frazione a, nel
// riferimento della prima.
H3::Matrix interpolateH3(const H3::Matrix &from, const H3::Matrix &to, float a)
{
    const H3::Matrix d = H3::inverse(from) * to;
    const H3::Vec4 p = d * H3::origin();
    const H3::Vec3 v = H3::logAtOrigin(p);
    const H3::Matrix r = H3::inverse(H3::translationTo(p)) * d;   // fissa l'origine
    QMatrix3x3 m;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m(i, j) = float(r.m[i][j]);
    QVector3D ax;
    float angleDeg = 0.0f;
    QQuaternion::fromRotationMatrix(m).getAxisAndAngle(&ax, &angleDeg);
    H3::Matrix part = H3::translation({v.x * a, v.y * a, v.z * a});
    if (std::abs(angleDeg) > 1e-6f && ax.lengthSquared() > 1e-12f)
        part = part * H3::rotation({ax.x(), ax.y(), ax.z()}, qDegreesToRadians(double(angleDeg)) * a);
    return H3::renormalized(from * part);
}

bool sameH3(const H3::Matrix &a, const H3::Matrix &b, double tolerance)
{
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::abs(a.m[i][j] - b.m[i][j]) > tolerance) return false;
    return true;
}
} // namespace

bool DriveControls::any() const
{
    // Le Maiuscole da sole non comandano nulla: non prendono il posto del volo.
    return throttleUp || throttleDown || yawLeft || yawRight || pitchUp || pitchDown || rollLeft
           || rollRight || liftUp || liftDown || wPlus || wMinus || brake || stick || rollStick
           || wheelSteps != 0.0f || touchLeft || touchRight || brakeTap || pad || padBrake || padWPlus
           || padWMinus;
}

void DriveControls::setPad(const GamepadState &state)
{
    pad = state.active();
    padLeftX = float(state.leftStick.x());
    padLeftY = float(state.leftStick.y());
    padRightX = float(state.rightStick.x());
    padRightY = float(state.rightStick.y());
    padLift = state.rightTrigger - state.leftTrigger;
    padBrake = state.held(GamepadButton::Cross);
    padWMinus = state.held(GamepadButton::L1);
    padWPlus = state.held(GamepadButton::R1);
    padBoost = state.held(GamepadButton::L3);
}

QRectF CameraDrive::touchButtonRect(int button, const QSizeF &view)
{
    constexpr qreal kSize = 52.0, kGap = 10.0, kMargin = 14.0;
    // Sempre quattro posti: i tastini di W, quando ci sono, non spostano gli altri.
    const qreal column = TouchButtonCount * kSize + (TouchButtonCount - 1) * kGap;
    const qreal top = std::max(kMargin, (view.height() - column) / 2.0);
    return QRectF(view.width() - kMargin - kSize, top + button * (kSize + kGap), kSize, kSize);
}

int CameraDrive::touchButtonAt(const QPointF &pos, const QSizeF &view, int count)
{
    for (int b = 0; b < std::min(count, int(TouchButtonCount)); ++b) {
        // Un po' piu' largo del disegno: il dito non e' un puntatore.
        if (touchButtonRect(b, view).adjusted(-6, -6, 6, 6).contains(pos)) return b;
    }
    return -1;
}

CameraDrive::CameraDrive(GLWidget *view, std::function<bool()> recording, std::function<bool()> movesW,
                         QObject *parent)
    : QObject(parent), m_view(view), m_recording(std::move(recording)), m_movesW(std::move(movesW))
{
    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &CameraDrive::tick);
}

bool CameraDrive::isOn() const { return m_timer->isActive(); }

bool CameraDrive::hyperbolic() const { return m_view->hyperbolicSpace(); }

bool CameraDrive::replaying() const
{
    return isOn() && trackEnd() > m_view->sceneTime() + kStep;
}

void CameraDrive::start()
{
    if (isOn()) return;
    const float t = m_view->sceneTime();
    if (!m_owns || m_track.empty() || m_trackHyperbolic != hyperbolic()) {
        // Un volo nuovo, da fermi, dalla camera di adesso.
        m_track.clear();
        m_loadedFlight = QJsonObject();
        m_trackHyperbolic = hyperbolic();
        m_track.push_back(stateFromView(t, State()));
    } else if (t >= trackEnd()) {
        // Oltre il volo: si riparte da fermi da dove e' la camera, con un
        // taglio se nel frattempo l'ha spostata qualcuno.
        const State now = stateFromView(t, State());
        const State &last = m_track.back();
        const bool samePose = m_trackHyperbolic ? sameH3(now.h3, last.h3, 1e-6)
                                                : (now.pos - last.pos).length() < 1e-4f
                                                      && std::abs(QQuaternion::dotProduct(now.rot, last.rot)) > 1.0f - 1e-6f;
        if (t > last.t || !samePose) m_track.push_back(now);
        if (!samePose) m_changedSinceLoad = true;
    }
    // Dentro il volo: lo si rifa' da T (applyAt qui sotto).
    m_owns = true;
    m_controls.releaseAll();
    m_droveSinceStart = false;
    m_view->setDriveCamera(true);
    apply(stateAt(t));
    m_clock.restart();
    m_timer->start();
}

void CameraDrive::stop()
{
    if (!isOn()) return;
    m_timer->stop();
    m_controls.releaseAll();
    const float t = m_view->sceneTime();
    // Dentro il volo registrato (lo si stava rifacendo) e' una pausa, come lo
    // Stop di un path: il volo resta intero.
    if (trackEnd() > t + kStep) {
        emit stepped();
        return;
    }
    // Alla fine, dove si guidava: il veicolo si ferma dov'e', e oltre questo T
    // il volo resta fermo (cursore e REC, finche' non si riaccende).
    truncateAt(t);
    State rest = stateAt(t);
    rest.t = t;
    rest.speed = rest.cruise = rest.lift = rest.wSpeed = 0.0f;
    rest.turn = QVector3D();
    m_track.push_back(rest);
    m_current = rest;
    emit stepped();
}

void CameraDrive::release()
{
    m_timer->stop();
    m_controls.releaseAll();
    m_track.clear();
    m_loadedFlight = QJsonObject();
    m_changedSinceLoad = false;
    m_owns = false;
    m_written = false;
    m_view->setDriveCamera(false);
}

void CameraDrive::tick()
{
    // Durante il REC il tempo lo avanza il loop del recorder (advanceSceneBy):
    // qui niente, e l'orologio riparte a ogni tick, cosi' a fine REC il primo
    // passo dal vivo e' di un tick e non della durata della registrazione.
    if (m_recording && m_recording()) {
        m_clock.restart();
        return;
    }
    // Un tick al massimo di 0.1 s: un'app bloccata un istante non fa un salto.
    const double dt = std::min(double(m_clock.restart()) / 1000.0, 0.1);
    advance(dt, true);
}

void CameraDrive::advance(double dt, bool live)
{
    if (m_track.empty() || dt <= 0.0) return;
    adoptExternalMove();
    const float t0 = m_view->sceneTime();
    if (live && m_controls.any()) {
        m_changedSinceLoad = true;
        if (!m_droveSinceStart) {
            m_droveSinceStart = true;
            emit userDrove();
        }
        // Dentro il volo registrato il pilota ne prende il posto: da qui si riscrive.
        if (trackEnd() > t0 + kStep) truncateAt(t0);
    }
    m_view->advanceSceneTime(float(dt));
    const float t1 = m_view->sceneTime();
    extendTo(t1, live ? &m_controls : nullptr);
    apply(stateAt(t1));
}

void CameraDrive::seekTo(float target)
{
    if (m_track.empty()) return;
    target = std::max(0.0f, target);
    adoptExternalMove();
    extendTo(target, nullptr);
    m_view->setSceneTime(target);
    apply(stateAt(target));
}

void CameraDrive::applyAt(float t)
{
    if (m_track.empty()) return;
    extendTo(t, nullptr);
    apply(stateAt(t));
}

CameraDrive::State CameraDrive::stateFromView(float t, const State &motion) const
{
    State s = motion;
    s.t = t;
    if (hyperbolic()) {
        s.h3 = m_view->hyperbolicCamera();
    } else {
        const QMatrix4x4 m = m_view->cameraPlacement();
        s.pos = m.column(3).toVector3D();
        s.rot = rotationOf(m);
    }
    s.observerW = m_view->observerPos().w();
    s.sectionP = m_view->crossSectionP();
    return s;
}

CameraDrive::State CameraDrive::stateAt(float t) const
{
    if (m_track.empty()) return m_current;
    if (t <= m_track.front().t) return m_track.front();
    // Il primo campione oltre t: con due campioni allo stesso T (un taglio) si
    // prende il secondo, lo stato dopo il taglio.
    const auto next = std::upper_bound(m_track.begin(), m_track.end(), t,
                                       [](float v, const State &s) { return v < s.t; });
    if (next == m_track.end()) return m_track.back();
    const State &a = *(next - 1);
    const State &b = *next;
    const float span = b.t - a.t;
    const float f = span > 0.0f ? (t - a.t) / span : 1.0f;
    if (f <= 0.0f) return a;
    State s = interpolate(a, b, f);
    s.t = t;
    return s;
}

CameraDrive::State CameraDrive::interpolate(const State &a, const State &b, float f) const
{
    State s = a;
    const auto mix = [f](float x, float y) { return x + (y - x) * f; };
    s.t = mix(a.t, b.t);
    s.pos = a.pos + (b.pos - a.pos) * f;
    s.rot = QQuaternion::slerp(a.rot, b.rot, f);
    if (m_trackHyperbolic) s.h3 = interpolateH3(a.h3, b.h3, f);
    s.observerW = mix(a.observerW, b.observerW);
    s.sectionP = mix(a.sectionP, b.sectionP);
    s.speed = mix(a.speed, b.speed);
    s.cruise = mix(a.cruise, b.cruise);
    s.turn = a.turn + (b.turn - a.turn) * f;
    s.lift = mix(a.lift, b.lift);
    s.wSpeed = mix(a.wSpeed, b.wSpeed);
    return s;
}

void CameraDrive::appendStep(const DriveControls &c)
{
    State s = m_track.back();
    const float h = kStep;
    const float boost = (c.boost || c.padBoost) ? kBoost : 1.0f;
    const bool brake = c.brake || c.padBrake;
    // La scala delle velocita': cambiandola, la crociera la segue (un -
    // rallenta subito). Senza comandi (REC, cursore) resta quella del volo.
    const float scale = &c == &m_controls ? c.speedScale : m_appliedScale;
    if (scale != m_appliedScale) {
        s.cruise *= scale / m_appliedScale;
        m_appliedScale = scale;
    }

    // I COMANDI -> gli obiettivi, poi l'inerzia.
    // Lo stick sinistro a tocco come W e S, a meta' o del tutto (in su = piu' veloce);
    // lo stick sinistro del pad lo stesso.
    const float throttle = axis(c.throttleUp, c.throttleDown) + (c.touchLeft ? -c.touchLeftY : 0.0f)
                           + (c.pad ? c.padLeftY : 0.0f);
    s.cruise = std::clamp(s.cruise + (c.wheelSteps * kWheelStep + throttle * kThrottleRate * h) * scale,
                          -kMaxCruise * scale, kMaxCruise * scale);
    if (brake || c.brakeTap) s.cruise = 0.0f;
    s.speed = approach(s.speed, s.cruise * boost, brake ? kBrakeTau : kSpeedTau, h);
    // La cloche: il mouse verso l'alto (y negativo a schermo) alza il muso,
    // verso destra vira a destra (rotazione negativa attorno all'alto).
    // Gli stick a tocco con gli stessi versi: il sinistro vira, il destro
    // beccheggia (in su = muso in su) e rolla (a destra = rollio a destra).
    // Gli stick del pad come quelli a tocco, gia' curvati (y del pad in su).
    const QVector3D turnTarget(
        axis(c.pitchUp, c.pitchDown) * kTurnRate - (c.stick ? stickCurve(c.stickY) * kStickRate : 0.0f)
            - (c.touchRight ? stickCurve(c.touchRightY) * kStickRate : 0.0f)
            + (c.pad ? c.padRightY * kStickRate : 0.0f),
        axis(c.yawLeft, c.yawRight) * kTurnRate - (c.stick ? stickCurve(c.stickX) * kStickRate : 0.0f)
            - (c.touchLeft ? stickCurve(c.touchLeftX) * kStickRate : 0.0f)
            - (c.pad ? c.padLeftX * kStickRate : 0.0f),
        axis(c.rollLeft, c.rollRight) * kRollRate - (c.rollStick ? stickCurve(c.rollX) * kRollRate : 0.0f)
            - (c.touchRight ? stickCurve(c.touchRightX) * kRollRate : 0.0f)
            - (c.pad ? c.padRightX * kRollRate : 0.0f));
    s.turn = approach(s.turn, turnTarget, kTurnTau, h);
    // Su e giu': R e F, o i grilletti del pad (R2 su, L2 giu', a meta' o del tutto).
    const float liftInput = std::clamp(axis(c.liftUp, c.liftDown) + (c.pad ? c.padLift : 0.0f), -1.0f, 1.0f);
    s.lift = approach(s.lift, liftInput * kLiftRate * boost * scale, kLiftTau, h);
    const bool w = m_movesW && m_movesW();
    s.wSpeed = approach(s.wSpeed, w ? axis(c.wPlus || c.padWPlus, c.wMinus || c.padWMinus) * kWRate * boost * scale : 0.0f, kWTau, h);

    // IL MOTO, nel riferimento della camera: prima gira, poi avanza.
    const float turnRate = s.turn.length();
    const float angle = turnRate * h;
    m_contact = false;
    if (!m_trackHyperbolic) {
        if (angle > 1e-9f)
            s.rot = (s.rot * QQuaternion::fromAxisAndAngle(s.turn / turnRate, qRadiansToDegrees(angle))).normalized();
        const QVector3D move = s.rot.rotatedVector(QVector3D(0.0f, s.lift, -s.speed)) * h;
        if (m_controls.collisions) {
            // GLI URTI: la camera scivola sulle superfici invece di attraversarle
            // (GLWidget::slideDriveCamera), e una superficie in moto la spinge,
            // anche ferma. L'urto toglie al veicolo la velocita' contro la
            // superficie: restano quelle del moto vero, in avanti e in su (di
            // lato il veicolo non va; quel tratto lo ha gia' fatto).
            const QVector3D to = m_view->slideDriveCamera(s.pos, move, kCollisionRadius, &m_contact);
            if (m_contact) {
                const QVector3D v = s.rot.conjugated().rotatedVector(to - s.pos) / h;
                s.speed = -v.z();
                s.lift = v.y();
            }
            s.pos = to;
        } else {
            s.pos += move;
        }
    } else {
        const double k = 1.0 / m_view->hyperbolicCurvatureRadius();
        H3::Matrix turned = s.h3;
        if (angle > 1e-9f) {
            const QVector3D a = s.turn / turnRate;
            turned = H3::renormalized(turned * H3::rotation({a.x(), a.y(), a.z()}, double(angle)));
        }
        // Lo spostamento nel riferimento della camera, in raggi di curvatura.
        QVector3D step(0.0f, float(double(s.lift) * h * k), float(-double(s.speed) * h * k));
        if (m_controls.collisions) {
            // GLI URTI IN H3: lo scivolamento nella carta di Klein della camera
            // (GLWidget::slideDriveCameraH3); come in R3, l'urto lascia al
            // veicolo la sola velocita' del moto vero.
            step = m_view->slideDriveCameraH3(turned, step, &m_contact);
            if (m_contact) {
                s.speed = float(-double(step.z()) / (h * k));
                s.lift = float(double(step.y()) / (h * k));
            }
        }
        const H3::Matrix moved = H3::renormalized(turned * H3::translation({ step.x(), step.y(), step.z() }));
        const double before = H3::distance(H3::origin(), turned * H3::origin());
        const double after = H3::distance(H3::origin(), moved * H3::origin());
        if (after > GLWidget::maxHyperbolicCameraDistance() && after > before) {
            // Il bordo della regione della camera (come gli scatti del dock 3D):
            // il veicolo si ferma, girarsi si puo' sempre.
            s.h3 = turned;
            s.speed = s.cruise = s.lift = 0.0f;
        } else {
            s.h3 = moved;
        }
    }
    // Verso W: la quota della sezione o l'osservatore 4D, come i tasti P+/P-.
    if (m_view->cutsSection()) s.sectionP += s.wSpeed * kSectionScale * h;
    else                       s.observerW += s.wSpeed * h;

    s.t = m_track.back().t + h;
    m_track.push_back(s);
}

void CameraDrive::extendTo(float t, const DriveControls *live)
{
    static const DriveControls none;
    while (m_track.back().t < t - 1e-6f) {
        appendStep(live ? *live : none);
        // La rotella e il doppio tocco si consumano al primo passo.
        if (live) {
            m_controls.wheelSteps = 0.0f;
            m_controls.brakeTap = false;
        }
    }
}

void CameraDrive::truncateAt(float t)
{
    if (m_track.empty() || t >= m_track.back().t) return;
    t = std::max(t, m_track.front().t);
    const State at = stateAt(t);
    const auto next = std::upper_bound(m_track.begin(), m_track.end(), t,
                                       [](float v, const State &s) { return v < s.t; });
    m_track.erase(next, m_track.end());
    if (m_track.empty() || m_track.back().t < t) m_track.push_back(at);
}

void CameraDrive::apply(const State &s)
{
    if (m_trackHyperbolic) {
        m_view->setDriveHyperbolicCamera(s.h3);
    } else {
        QMatrix4x4 placement;
        placement.translate(s.pos);
        placement.rotate(s.rot);
        m_view->setEuclideanCameraPlacement(placement);
    }
    m_view->setObserverW(s.observerW);
    m_view->setCrossSectionP(s.sectionP);
    m_current = s;
    m_written = true;
    emit stepped();
}

void CameraDrive::adoptExternalMove()
{
    if (!m_written || m_track.empty() || m_trackHyperbolic != hyperbolic()) return;
    const State now = stateFromView(m_view->sceneTime(), m_current);
    const bool moved = (m_trackHyperbolic ? !sameH3(now.h3, m_current.h3, 1e-6)
                                          : (now.pos - m_current.pos).length() > 1e-4f
                                                || std::abs(QQuaternion::dotProduct(now.rot, m_current.rot)) < 1.0f - 1e-6f)
                       || std::abs(now.observerW - m_current.observerW) > 1e-5f
                       || std::abs(now.sectionP - m_current.sectionP) > 1e-5f;
    if (!moved) return;
    // Un taglio: la traccia da qui riparte dalla camera com'e', con le
    // velocita' di prima.
    truncateAt(now.t);
    m_track.push_back(now);
    m_current = now;
    m_changedSinceLoad = true;
}

// =============================================================================
// IL VOLO NEL FILE (toJson / loadJson)
// =============================================================================

namespace {
// Le tolleranze dello sfoltimento: un campione si toglie se l'interpolazione
// fra i vicini lo rifa' entro queste (unita' della scena, radianti, W).
constexpr float kPosTolerance = 3e-4f;
constexpr float kRotTolerance = 2e-4f;
constexpr float kWTolerance = 3e-4f;
// Un campione almeno ogni due secondi, anche in linea retta.
constexpr int kMaxGap = 120;

double rounded(double v) { return std::round(v * 1e6) / 1e6; }

// L'angolo di una rotazione piccola, dalla parte vettoriale (acos vicino a 1
// non distingue gli angoli sotto il millesimo di radiante, in float).
float rotationAngle(const QQuaternion &q)
{
    return 2.0f * std::atan2(q.vector().length(), std::abs(q.scalar()));
}

// La parte di rotazione di un'isometria di H3 che fissa l'origine, come
// quaternione, e il suo angolo in doppia precisione.
QQuaternion h3Rotation(const H3::Matrix &r)
{
    QMatrix3x3 m;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m(i, j) = float(r.m[i][j]);
    return QQuaternion::fromRotationMatrix(m).normalized();
}

double h3RotationAngle(const H3::Matrix &r)
{
    const double c = (r.m[0][0] + r.m[1][1] + r.m[2][2] - 1.0) / 2.0;
    const double sx = r.m[2][1] - r.m[1][2], sy = r.m[0][2] - r.m[2][0], sz = r.m[1][0] - r.m[0][1];
    return std::atan2(0.5 * std::sqrt(sx * sx + sy * sy + sz * sz), c);
}

// Il riferimento di una camera di H3 come lo scrive il file: la posizione dal
// centro in unita' della scena (k raggi per unita') e la rotazione portata nel
// centro, come GLWidget::hyperbolicPlacement e hyperbolicCameraFromPlacement.
void h3ToPlacement(const H3::Matrix &m, double k, QVector3D *pos, QQuaternion *rot)
{
    const H3::Vec4 p = m * H3::origin();
    const H3::Vec3 v = H3::logAtOrigin(p);
    *pos = QVector3D(float(v.x / k), float(v.y / k), float(v.z / k));
    *rot = h3Rotation(H3::inverse(H3::translationTo(p)) * m);
}

H3::Matrix h3FromPlacement(const QVector3D &pos, const QQuaternion &rot, double k)
{
    H3::Matrix r = H3::Matrix::identity();
    const QMatrix3x3 m = rot.toRotationMatrix();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = m(i, j);
    return H3::renormalized(H3::translation({pos.x() * k, pos.y() * k, pos.z() * k}) * r);
}
} // namespace

float CameraDrive::poseError(const State &x, const State &y) const
{
    float e = 0.0f;
    if (m_trackHyperbolic) {
        const double k = 1.0 / m_view->hyperbolicCurvatureRadius();
        const H3::Matrix d = H3::inverse(x.h3) * y.h3;
        const H3::Vec4 p = d * H3::origin();
        const double dist = H3::distance(H3::origin(), p) / k;
        const double angle = h3RotationAngle(H3::inverse(H3::translationTo(p)) * d);
        e = float(std::max(dist / kPosTolerance, std::abs(angle) / kRotTolerance));
    } else {
        e = std::max((x.pos - y.pos).length() / kPosTolerance,
                     rotationAngle(x.rot.conjugated() * y.rot) / kRotTolerance);
    }
    return std::max({ e, std::abs(x.observerW - y.observerW) / kWTolerance,
                      std::abs(x.sectionP - y.sectionP) / kWTolerance });
}

QJsonArray CameraDrive::poseToJson(const State &s) const
{
    QVector3D pos = s.pos;
    QQuaternion rot = s.rot;
    if (m_trackHyperbolic) h3ToPlacement(s.h3, 1.0 / m_view->hyperbolicCurvatureRadius(), &pos, &rot);
    return { rounded(s.t), rounded(pos.x()), rounded(pos.y()), rounded(pos.z()),
             rounded(rot.x()), rounded(rot.y()), rounded(rot.z()), rounded(rot.scalar()),
             rounded(s.observerW), rounded(s.sectionP) };
}

CameraDrive::State CameraDrive::poseFromJson(const QJsonArray &a) const
{
    State s;
    s.t = float(a.at(0).toDouble());
    s.pos = QVector3D(float(a.at(1).toDouble()), float(a.at(2).toDouble()), float(a.at(3).toDouble()));
    s.rot = QQuaternion(float(a.at(7).toDouble()), float(a.at(4).toDouble()), float(a.at(5).toDouble()),
                        float(a.at(6).toDouble())).normalized();
    if (m_trackHyperbolic) s.h3 = h3FromPlacement(s.pos, s.rot, 1.0 / m_view->hyperbolicCurvatureRadius());
    s.observerW = float(a.at(8).toDouble());
    s.sectionP = float(a.at(9).toDouble());
    return s;
}

QJsonObject CameraDrive::toJson() const
{
    if (!m_owns || m_track.empty()) return QJsonObject();
    // Caricato e mai toccato: com'era nel file (la coda che si allunga da sola
    // rifacendolo e' la stessa fisica senza comandi, la rifa' il prossimo load).
    if (!m_loadedFlight.isEmpty() && !m_changedSinceLoad) return m_loadedFlight;

    // LO SFOLTIMENTO, in avanti: dall'ultimo campione tenuto si salta finche'
    // l'interpolazione rifa' tutti quelli in mezzo entro la tolleranza.
    const std::vector<State> &tr = m_track;
    const int n = int(tr.size());
    std::vector<int> keep{ 0 };
    int anchor = 0;
    for (int j = 1; j < n; ++j) {
        if (tr[j].t <= tr[j - 1].t) {
            // Un taglio: i due campioni allo stesso T restano tutti e due.
            if (keep.back() != j - 1) keep.push_back(j - 1);
            keep.push_back(j);
            anchor = j;
            continue;
        }
        if (j + 1 >= n) break;
        bool ok = j + 1 - anchor <= kMaxGap && tr[j + 1].t > tr[j].t;
        const float span = tr[j + 1].t - tr[anchor].t;
        for (int k = anchor + 1; ok && k <= j; ++k)
            ok = poseError(tr[k], interpolate(tr[anchor], tr[j + 1], (tr[k].t - tr[anchor].t) / span)) <= 1.0f;
        if (!ok) {
            keep.push_back(j);
            anchor = j;
        }
    }
    if (keep.back() != n - 1) keep.push_back(n - 1);

    QJsonArray poses;
    for (int i : keep) poses.append(poseToJson(tr[i]));
    const State &last = tr.back();
    QJsonObject end;
    end[QStringLiteral("speed")] = rounded(last.speed);
    end[QStringLiteral("cruise")] = rounded(last.cruise);
    end[QStringLiteral("turn")] = QJsonArray{ rounded(last.turn.x()), rounded(last.turn.y()), rounded(last.turn.z()) };
    end[QStringLiteral("lift")] = rounded(last.lift);
    end[QStringLiteral("wSpeed")] = rounded(last.wSpeed);
    QJsonObject flight;
    flight[QStringLiteral("version")] = 1;
    flight[QStringLiteral("space")] = m_trackHyperbolic ? QStringLiteral("H3") : QStringLiteral("R3");
    flight[QStringLiteral("poses")] = poses;
    flight[QStringLiteral("end")] = end;
    return flight;
}

bool CameraDrive::loadJson(const QJsonObject &flight)
{
    if (flight.value(QStringLiteral("version")).toInt() != 1) return false;
    const bool h3 = flight.value(QStringLiteral("space")).toString() == QLatin1String("H3");
    if (h3 != hyperbolic()) return false;
    const QJsonArray poses = flight.value(QStringLiteral("poses")).toArray();
    if (poses.isEmpty()) return false;

    // Le pose del file, in ordine di T.
    const bool wasHyperbolic = m_trackHyperbolic;
    m_trackHyperbolic = h3;   // poseFromJson legge nello spazio del volo
    std::vector<State> keys;
    for (const QJsonValue &v : poses) {
        const QJsonArray a = v.toArray();
        if (a.size() < 10 || (!keys.empty() && float(a.at(0).toDouble()) < keys.back().t)) {
            m_trackHyperbolic = wasHyperbolic;
            return false;
        }
        keys.push_back(poseFromJson(a));
    }

    m_timer->stop();
    m_controls.releaseAll();
    // IL RICAMPIONAMENTO al passo fisso, con la stessa interpolazione della
    // traccia: i campioni cadono sui tratti fra le pose del file.
    m_track.clear();
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const State &a = keys[i];
        const State &b = keys[i + 1];
        m_track.push_back(a);
        const float span = b.t - a.t;
        if (span <= 0.0f) continue;   // un taglio
        for (int m = 1;; ++m) {
            const float t = a.t + float(m) * kStep;
            if (t >= b.t - 0.25f * kStep) break;
            State s = interpolate(a, b, (t - a.t) / span);
            s.t = t;
            m_track.push_back(s);
        }
    }
    m_track.push_back(keys.back());
    deriveVelocities();
    // Lo stato del veicolo alla fine: prosegue da li' (cursore, REC, guida).
    const QJsonObject end = flight.value(QStringLiteral("end")).toObject();
    State &last = m_track.back();
    last.speed = float(end.value(QStringLiteral("speed")).toDouble());
    last.cruise = float(end.value(QStringLiteral("cruise")).toDouble());
    const QJsonArray turn = end.value(QStringLiteral("turn")).toArray();
    last.turn = QVector3D(float(turn.at(0).toDouble()), float(turn.at(1).toDouble()), float(turn.at(2).toDouble()));
    last.lift = float(end.value(QStringLiteral("lift")).toDouble());
    last.wSpeed = float(end.value(QStringLiteral("wSpeed")).toDouble());

    m_owns = true;
    m_written = false;
    m_current = m_track.front();
    m_loadedFlight = flight;
    m_changedSinceLoad = false;
    m_view->setDriveCamera(true);
    return true;
}

void CameraDrive::deriveVelocities()
{
    const double k = m_trackHyperbolic ? 1.0 / m_view->hyperbolicCurvatureRadius() : 1.0;
    for (size_t i = 0; i + 1 < m_track.size(); ++i) {
        State &a = m_track[i];
        const State &b = m_track[i + 1];
        const float dt = b.t - a.t;
        if (dt <= 0.0f) {
            // L'ultimo campione prima di un taglio: le velocita' di quello prima.
            if (i > 0) {
                const State &before = m_track[i - 1];
                a.speed = before.speed;
                a.cruise = before.cruise;
                a.turn = before.turn;
                a.lift = before.lift;
                a.wSpeed = before.wSpeed;
            }
            continue;
        }
        QVector3D v;
        QQuaternion turn;
        if (m_trackHyperbolic) {
            const H3::Matrix d = H3::inverse(a.h3) * b.h3;
            const H3::Vec4 p = d * H3::origin();
            const H3::Vec3 l = H3::logAtOrigin(p);
            v = QVector3D(float(l.x / k), float(l.y / k), float(l.z / k)) / dt;
            turn = h3Rotation(H3::inverse(H3::translationTo(p)) * d);
        } else {
            v = a.rot.conjugated().rotatedVector(b.pos - a.pos) / dt;
            turn = (a.rot.conjugated() * b.rot).normalized();
        }
        a.speed = a.cruise = -v.z();
        a.lift = v.y();
        QVector3D axis;
        float angleDeg = 0.0f;
        turn.getAxisAndAngle(&axis, &angleDeg);
        a.turn = axis.lengthSquared() > 1e-12f ? axis.normalized() * qDegreesToRadians(angleDeg) / dt : QVector3D();
        a.wSpeed = ((b.observerW - a.observerW) + (b.sectionP - a.sectionP) / kSectionScale) / dt;
    }
}
