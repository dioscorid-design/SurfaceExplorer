// gamepad_backend.mm - Il backend del pad (vedi gamepad.h). Su macOS, iPadOS e
// iOS il framework GameController, che riconosce i pad Xbox, PlayStation
// (DualShock 4, DualSense), Switch Pro e MFi. Altrove per ora nessun pad: il
// ripiego in fondo, compilato come C++ (CMakeLists.txt), cosi' Gamepad non ha
// #ifdef. Windows e Linux (SDL3) e Android (Java + JNI) verranno qui.
#include "gamepad.h"

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

// Il pad virtuale: solo nella build Debug per iPad e iPhone (QT_NO_DEBUG lo
// definisce Qt in ogni configurazione che non e' Debug).
#if defined(Q_OS_IOS) && !defined(QT_NO_DEBUG)
#define SE_VIRTUAL_PAD 1
#endif

struct GamepadBackend::Impl {
#ifdef SE_VIRTUAL_PAD
    GCVirtualController *virtualPad = nil;   // trattenuto
#endif
    // Gli osservatori delle notifiche (trattenuti: il file e' senza ARC).
    id connectObserver = nil;
    id disconnectObserver = nil;
    id currentObserver = nil;
    // Spento dal distruttore: una notifica gia' in coda sulla main queue non
    // chiama piu' un Gamepad distrutto.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

namespace {

// Gli stick e i grilletti di un controller: dal profilo esteso (DualSense,
// Xbox e simili) o, se non l'ha, per nome dal profilo fisico (il pad
// virtuale). Falso se gli manca qualcosa.
// I pulsanti possono mancare (il pad virtuale ha solo stick e grilletti, L3 e
// R3 non tutti i pad): nil, mai premuti.
struct Inputs {
    GCControllerDirectionPad *left = nil, *right = nil;
    GCControllerButtonInput *leftTrigger = nil, *rightTrigger = nil;
    GCControllerButtonInput *buttons[int(GamepadButton::Count)] = {};
};
bool inputsOf(GCController *c, Inputs *in)
{
    *in = Inputs();
    GCControllerButtonInput **b = in->buttons;
    if (GCExtendedGamepad *g = c.extendedGamepad) {
        in->left = g.leftThumbstick;
        in->right = g.rightThumbstick;
        in->leftTrigger = g.leftTrigger;
        in->rightTrigger = g.rightTrigger;
        b[int(GamepadButton::Cross)] = g.buttonA;
        b[int(GamepadButton::Circle)] = g.buttonB;
        b[int(GamepadButton::Square)] = g.buttonX;
        b[int(GamepadButton::Triangle)] = g.buttonY;
        b[int(GamepadButton::L1)] = g.leftShoulder;
        b[int(GamepadButton::R1)] = g.rightShoulder;
        b[int(GamepadButton::L3)] = g.leftThumbstickButton;
        b[int(GamepadButton::R3)] = g.rightThumbstickButton;
        b[int(GamepadButton::Up)] = g.dpad.up;
        b[int(GamepadButton::Down)] = g.dpad.down;
        b[int(GamepadButton::Left)] = g.dpad.left;
        b[int(GamepadButton::Right)] = g.dpad.right;
        b[int(GamepadButton::Options)] = g.buttonMenu;
        b[int(GamepadButton::Create)] = g.buttonOptions;
    } else {
        GCPhysicalInputProfile *p = c.physicalInputProfile;
        in->left = p.dpads[GCInputLeftThumbstick];
        in->right = p.dpads[GCInputRightThumbstick];
        in->leftTrigger = p.buttons[GCInputLeftTrigger];
        in->rightTrigger = p.buttons[GCInputRightTrigger];
        b[int(GamepadButton::Cross)] = p.buttons[GCInputButtonA];
        b[int(GamepadButton::Circle)] = p.buttons[GCInputButtonB];
        b[int(GamepadButton::Square)] = p.buttons[GCInputButtonX];
        b[int(GamepadButton::Triangle)] = p.buttons[GCInputButtonY];
        b[int(GamepadButton::L1)] = p.buttons[GCInputLeftShoulder];
        b[int(GamepadButton::R1)] = p.buttons[GCInputRightShoulder];
        b[int(GamepadButton::L3)] = p.buttons[GCInputLeftThumbstickButton];
        b[int(GamepadButton::R3)] = p.buttons[GCInputRightThumbstickButton];
        if (GCControllerDirectionPad *d = p.dpads[GCInputDirectionPad]) {
            b[int(GamepadButton::Up)] = d.up;
            b[int(GamepadButton::Down)] = d.down;
            b[int(GamepadButton::Left)] = d.left;
            b[int(GamepadButton::Right)] = d.right;
        }
        b[int(GamepadButton::Options)] = p.buttons[GCInputButtonMenu];
        b[int(GamepadButton::Create)] = p.buttons[GCInputButtonOptions];
    }
    return in->left && in->right && in->leftTrigger && in->rightTrigger;
}

// Il pad in uso: l'ultimo toccato (GCController.current), altrimenti il primo
// con due stick e i grilletti.
GCController *activeController(Inputs *in)
{
    GCController *current = GCController.current;
    if (current && inputsOf(current, in)) return current;
    for (GCController *c in GCController.controllers) {
        if (inputsOf(c, in)) return c;
    }
    return nil;
}

} // namespace

GamepadBackend::GamepadBackend(std::function<void()> changed)
    : d(std::make_unique<Impl>())
{
    @autoreleasepool {
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        NSOperationQueue *main = [NSOperationQueue mainQueue];
        const std::shared_ptr<bool> alive = d->alive;
        void (^handler)(NSNotification *) = ^(NSNotification *) {
            if (*alive && changed) changed();
        };
        d->connectObserver = [[nc addObserverForName:GCControllerDidConnectNotification
                                              object:nil queue:main usingBlock:handler] retain];
        d->disconnectObserver = [[nc addObserverForName:GCControllerDidDisconnectNotification
                                                 object:nil queue:main usingBlock:handler] retain];
        // Con due pad collegati, quello in uso cambia toccandone un altro.
        d->currentObserver = [[nc addObserverForName:GCControllerDidBecomeCurrentNotification
                                              object:nil queue:main usingBlock:handler] retain];
    }
}

GamepadBackend::~GamepadBackend()
{
    *d->alive = false;
    setVirtual(false);
    @autoreleasepool {
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        for (id observer : { d->connectObserver, d->disconnectObserver, d->currentObserver }) {
            if (!observer) continue;
            [nc removeObserver:observer];
            [observer release];
        }
    }
}

GamepadState GamepadBackend::read() const
{
    GamepadState s;
    @autoreleasepool {
        Inputs in;
        if (GCController *c = activeController(&in)) {
            s.connected = true;
            s.name = c.vendorName ? QString::fromNSString(c.vendorName) : QStringLiteral("Game controller");
            s.leftStick = QPointF(in.left.xAxis.value, in.left.yAxis.value);
            s.rightStick = QPointF(in.right.xAxis.value, in.right.yAxis.value);
            s.leftTrigger = in.leftTrigger.value;
            s.rightTrigger = in.rightTrigger.value;
            for (int k = 0; k < int(GamepadButton::Count); ++k) {
                if (in.buttons[k] && in.buttons[k].pressed) s.buttons |= 1u << k;
            }
        }
    }
    return s;
}

bool GamepadBackend::virtualAvailable()
{
#ifdef SE_VIRTUAL_PAD
    return true;
#else
    return false;
#endif
}

bool GamepadBackend::virtualOn() const
{
#ifdef SE_VIRTUAL_PAD
    return d->virtualPad != nil;
#else
    return false;
#endif
}

void GamepadBackend::setVirtual(bool on)
{
#ifdef SE_VIRTUAL_PAD
    if (on == (d->virtualPad != nil)) return;
    @autoreleasepool {
        if (on) {
            // Gli stessi comandi che usa l'app: due stick e due grilletti. Il
            // collegamento arriva come per un pad vero (notifica -> changed).
            GCVirtualControllerConfiguration *config = [[[GCVirtualControllerConfiguration alloc] init] autorelease];
            config.elements = [NSSet setWithObjects:GCInputLeftThumbstick, GCInputRightThumbstick, GCInputLeftTrigger,
                                                    GCInputRightTrigger, nil];
            d->virtualPad = [[GCVirtualController alloc] initWithConfiguration:config];
            [d->virtualPad connectWithReplyHandler:nil];
        } else {
            [d->virtualPad disconnect];
            [d->virtualPad release];
            d->virtualPad = nil;
        }
    }
#else
    Q_UNUSED(on);
#endif
}

#else

// Nessun pad su questa piattaforma (per ora).
struct GamepadBackend::Impl {};

GamepadBackend::GamepadBackend(std::function<void()>)
    : d(std::make_unique<Impl>())
{
}

GamepadBackend::~GamepadBackend() = default;

GamepadState GamepadBackend::read() const
{
    return GamepadState();
}

bool GamepadBackend::virtualAvailable()
{
    return false;
}

bool GamepadBackend::virtualOn() const
{
    return false;
}

void GamepadBackend::setVirtual(bool)
{
}

#endif
