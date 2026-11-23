#include "gamepad.h"

#include <QGuiApplication>
#include <QTimer>
#include <algorithm>
#include <cmath>

Gamepad::Gamepad(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kStepMs);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &Gamepad::poll);
    m_backend = std::make_unique<GamepadBackend>([this] { refreshConnection(); });
    // Un pad gia' collegato all'avvio (su macOS di solito arriva comunque
    // come notifica, a ciclo degli eventi partito).
    refreshConnection();
}

Gamepad::~Gamepad() = default;

GamepadState Gamepad::shaped(const GamepadState &raw)
{
    GamepadState s = raw;
    const auto stick = [](const QPointF &v) {
        const double r = std::hypot(v.x(), v.y());
        if (r <= double(kStickDeadzone)) return QPointF();
        const double out = std::min(1.0, (r - double(kStickDeadzone)) / (1.0 - double(kStickDeadzone)));
        return v * (out * out / r);
    };
    const auto trigger = [](float t) {
        if (t <= kTriggerDeadzone) return 0.0f;
        return std::min(1.0f, (t - kTriggerDeadzone) / (1.0f - kTriggerDeadzone));
    };
    s.leftStick = stick(raw.leftStick);
    s.rightStick = stick(raw.rightStick);
    s.leftTrigger = trigger(raw.leftTrigger);
    s.rightTrigger = trigger(raw.rightTrigger);
    if (!s.connected) {
        s.leftStick = s.rightStick = QPointF();
        s.leftTrigger = s.rightTrigger = 0.0f;
        s.buttons = 0;
    }
    return s;
}

void Gamepad::refreshConnection()
{
    if (m_testRaw) return;
    const GamepadState raw = m_backend->read();
    if (raw.connected == m_connected && raw.name == m_name) return;
    m_connected = raw.connected;
    m_name = raw.name;
    m_state = shaped(raw);
    m_lastButtons = 0;
    if (m_connected) {
        m_clock.start();
        m_timer->start();
    } else {
        m_timer->stop();
    }
    emit connectionChanged(m_connected, m_name);
}

void Gamepad::poll()
{
    const float dt = std::min(kMaxStep, float(m_clock.restart()) / 1000.0f);
    step(dt);
}

void Gamepad::step(float dt)
{
    GamepadState raw;
    if (m_testRaw) {
        raw = *m_testRaw;
    } else {
        raw = m_backend->read();
        // Scollegato senza notifica (o un altro pad in uso): si riallinea.
        if (raw.connected != m_connected || raw.name != m_name) refreshConnection();
        // App in secondo piano: il sistema smette di aggiornare il pad e gli
        // assi resterebbero all'ultimo valore. Fermi.
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
            raw.leftStick = raw.rightStick = QPointF();
            raw.leftTrigger = raw.rightTrigger = 0.0f;
            raw.buttons = 0;
        }
    }
    m_state = shaped(raw);
    if (!m_state.connected) return;
    emit stepped(dt);
    const quint32 before = m_lastButtons, now = m_state.buttons;
    m_lastButtons = now;
    for (int b = 0; b < int(GamepadButton::Count); ++b) {
        const quint32 bit = 1u << b;
        if ((now & bit) && !(before & bit)) emit buttonPressed(GamepadButton(b));
        else if (!(now & bit) && (before & bit)) emit buttonReleased(GamepadButton(b));
    }
}

void Gamepad::setTestState(const std::optional<GamepadState> &raw)
{
    m_testRaw = raw;
    if (raw) {
        m_timer->stop();
        m_state = shaped(*raw);
        return;
    }
    // Di nuovo il backend: si riparte dal suo stato.
    m_connected = false;
    m_name.clear();
    m_state = GamepadState();
    refreshConnection();
}
