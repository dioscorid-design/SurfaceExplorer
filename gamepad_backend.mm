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
struct Inputs {
    GCControllerDirectionPad *left = nil, *right = nil;
    GCControllerButtonInput *leftTrigger = nil, *rightTrigger = nil;
};
bool inputsOf(GCController *c, Inputs *in)
{
    if (GCExtendedGamepad *g = c.extendedGamepad) {
        *in = { g.leftThumbstick, g.rightThumbstick, g.leftTrigger, g.rightTrigger };
    } else {
        GCPhysicalInputProfile *p = c.physicalInputProfile;
        *in = { p.dpads[GCInputLeftThumbstick], p.dpads[GCInputRightThumbstick], p.buttons[GCInputLeftTrigger],
                p.buttons[GCInputRightTrigger] };
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
