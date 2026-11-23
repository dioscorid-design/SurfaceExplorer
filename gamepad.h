// gamepad.h - IL CONTROLLER DI GIOCO (2.0; richiesta dell'utente, 2026-11-22):
// un pad come il DualSense muove la vista e l'oggetto come il mouse (vedi
// InputHandler::handleGamepad). Qui lo stato del pad e il suo passo; il
// backend della piattaforma sta in gamepad_backend.mm (GameController su
// macOS, iPadOS e iOS; altrove per ora nessun pad).
//
// IL PASSO. Con un pad collegato un timer a ~60 Hz legge il backend, ripulisce
// lo stato (zona morta, curva) ed emette stepped(dt); senza pad il timer e'
// fermo. Il pad e' un comando dal vivo come mouse e tastiera: niente orologi
// suoi, e durante il REC non comanda (GLWidget::applyGamepadStep).
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

class QTimer;

struct GamepadState {
    bool connected = false;
    QString name;              // il nome che da' il sistema ("DualSense Wireless Controller")
    // Gli stick, -1..1: x verso destra, y verso l'alto (la convenzione di GameController).
    QPointF leftStick, rightStick;
    // I grilletti (L2, R2), 0..1.
    float leftTrigger = 0.0f, rightTrigger = 0.0f;

    // Qualche asse fuori dal riposo. Sullo stato ripulito (Gamepad::shaped):
    // fuori dalla zona morta, cioe' un gesto vero.
    bool active() const
    {
        return !leftStick.isNull() || !rightStick.isNull() || leftTrigger != 0.0f || rightTrigger != 0.0f;
    }
};

// IL BACKEND DELLA PIATTAFORMA (gamepad_backend.mm). `changed` arriva quando un
// pad si collega o si scollega; read() da' lo stato grezzo del pad in uso
// (connected falso se non ce n'e').
class GamepadBackend
{
public:
    explicit GamepadBackend(std::function<void()> changed);
    ~GamepadBackend();
    GamepadState read() const;
    // Il pad virtuale (vedi Gamepad::virtualControllerAvailable).
    static bool virtualAvailable();
    bool virtualOn() const;
    void setVirtual(bool on);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

class Gamepad : public QObject
{
    Q_OBJECT
    friend class ScenarioTest;   // stato finto e passi a mano (setTestState, step)
public:
    // LA ZONA MORTA. Uno stick lasciato andare non torna esattamente a zero
    // (qualche centesimo, di piu' sui pad consumati): sotto questo raggio non
    // conta. Senza, la vista girerebbe da sola e la scena risulterebbe mossa
    // dall'utente ("vuoi salvare?") senza che nessuno la tocchi.
    static constexpr float kStickDeadzone = 0.15f;
    static constexpr float kTriggerDeadzone = 0.05f;
    // Il passo del timer (ms), e il piu' lungo che si applica in una volta (s):
    // dopo una pausa (finestra trascinata, app sospesa) la vista non salta.
    static constexpr int kStepMs = 16;
    static constexpr float kMaxStep = 0.05f;

    explicit Gamepad(QObject *parent = nullptr);
    ~Gamepad() override;

    // Lo stato dell'ultimo passo, ripulito (shaped).
    const GamepadState &state() const { return m_state; }
    bool isConnected() const { return m_connected; }

    // IL PAD VIRTUALE (richiesta dell'utente, 2026-11-22: provare il pad
    // senza averlo). Solo nella build Debug per iPad e iPhone: il sistema
    // disegna sullo schermo due stick e due grilletti (GCVirtualController) e
    // l'app lo vede come un controller vero, collegamento compreso. Lo accende
    // la voce "Virtual Controller" del menu in alto a sinistra; non si ricorda
    // fra un avvio e l'altro. Altrove non c'e' (available falso, set non fa
    // niente).
    static bool virtualControllerAvailable() { return GamepadBackend::virtualAvailable(); }
    bool virtualControllerOn() const { return m_backend->virtualOn(); }
    void setVirtualControllerOn(bool on) { m_backend->setVirtual(on); }

    // LA PULIZIA. Stick: zona morta RADIALE (in diagonale come in asse), il
    // resto riscalato a 0..1 perche' appena fuori si parta da zero, poi al
    // quadrato: piccoli spostamenti per i ritocchi, tutta corsa per girare in
    // fretta. Grilletti: sotto soglia zero, il resto riscalato.
    static GamepadState shaped(const GamepadState &raw);

signals:
    // Un pad si e' collegato (col suo nome) o scollegato.
    void connectionChanged(bool connected, const QString &name);
    // Un passo del pad: dt secondi dall'ultimo, lo stato in state(). Solo con
    // un pad collegato.
    void stepped(float dt);

private:
    // Il backend ha visto collegarsi o scollegarsi un pad.
    void refreshConnection();
    void poll();
    // Legge lo stato (backend, o quello finto dei test), lo ripulisce ed
    // emette stepped(dt).
    void step(float dt);
    // I TEST: uno stato grezzo finto al posto del backend (il timer si ferma,
    // i passi li da' il test con step); nullopt torna al backend.
    void setTestState(const std::optional<GamepadState> &raw);

    std::unique_ptr<GamepadBackend> m_backend;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    GamepadState m_state;
    bool m_connected = false;
    QString m_name;
    std::optional<GamepadState> m_testRaw;
};
