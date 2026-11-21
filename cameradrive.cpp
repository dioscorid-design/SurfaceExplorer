// cameradrive.cpp - La guida libera della camera. Vedi cameradrive.h.
#include "cameradrive.h"

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
           || wheelSteps != 0.0f;
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
    // Il veicolo si ferma dov'e': oltre questo T il volo resta fermo (cursore
    // e REC, finche' non si riaccende).
    const float t = m_view->sceneTime();
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
    State s = a;
    s.t = t;
    const auto mix = [f](float x, float y) { return x + (y - x) * f; };
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
    const float boost = c.boost ? kBoost : 1.0f;
    // La scala delle velocita': cambiandola, la crociera la segue (un -
    // rallenta subito). Senza comandi (REC, cursore) resta quella del volo.
    const float scale = &c == &m_controls ? c.speedScale : m_appliedScale;
    if (scale != m_appliedScale) {
        s.cruise *= scale / m_appliedScale;
        m_appliedScale = scale;
    }

    // I COMANDI -> gli obiettivi, poi l'inerzia.
    s.cruise = std::clamp(s.cruise + (c.wheelSteps * kWheelStep
                                      + axis(c.throttleUp, c.throttleDown) * kThrottleRate * h) * scale,
                          -kMaxCruise * scale, kMaxCruise * scale);
    if (c.brake) s.cruise = 0.0f;
    s.speed = approach(s.speed, s.cruise * boost, c.brake ? kBrakeTau : kSpeedTau, h);
    // La cloche: il mouse verso l'alto (y negativo a schermo) alza il muso,
    // verso destra vira a destra (rotazione negativa attorno all'alto).
    const QVector3D turnTarget(
        axis(c.pitchUp, c.pitchDown) * kTurnRate - (c.stick ? stickCurve(c.stickY) * kStickRate : 0.0f),
        axis(c.yawLeft, c.yawRight) * kTurnRate - (c.stick ? stickCurve(c.stickX) * kStickRate : 0.0f),
        axis(c.rollLeft, c.rollRight) * kRollRate - (c.rollStick ? stickCurve(c.rollX) * kRollRate : 0.0f));
    s.turn = approach(s.turn, turnTarget, kTurnTau, h);
    s.lift = approach(s.lift, axis(c.liftUp, c.liftDown) * kLiftRate * boost * scale, kLiftTau, h);
    const bool w = m_movesW && m_movesW();
    s.wSpeed = approach(s.wSpeed, w ? axis(c.wPlus, c.wMinus) * kWRate * boost * scale : 0.0f, kWTau, h);

    // IL MOTO, nel riferimento della camera: prima gira, poi avanza.
    const float turnRate = s.turn.length();
    const float angle = turnRate * h;
    if (!m_trackHyperbolic) {
        if (angle > 1e-9f)
            s.rot = (s.rot * QQuaternion::fromAxisAndAngle(s.turn / turnRate, qRadiansToDegrees(angle))).normalized();
        s.pos += s.rot.rotatedVector(QVector3D(0.0f, s.lift, -s.speed)) * h;
    } else {
        const double k = 1.0 / m_view->hyperbolicCurvatureRadius();
        H3::Matrix turned = s.h3;
        if (angle > 1e-9f) {
            const QVector3D a = s.turn / turnRate;
            turned = H3::renormalized(turned * H3::rotation({a.x(), a.y(), a.z()}, double(angle)));
        }
        const H3::Matrix moved = H3::renormalized(
            turned * H3::translation({0.0, double(s.lift) * h * k, -double(s.speed) * h * k}));
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
        // La rotella si consuma al primo passo.
        if (live) m_controls.wheelSteps = 0.0f;
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
}
