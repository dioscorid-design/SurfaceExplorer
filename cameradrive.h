// cameradrive.h - LA GUIDA LIBERA (2.1): la camera vola nella scena a sei gradi
// di liberta' (avanti, su/giu', beccheggio, imbardata, rollio, e verso W nel
// 4D), comandata da tastiera e mouse, con l'inerzia di un veicolo. Funziona con
// una superficie sola e nelle scene, in R3 e in H3.
//
// IL VOLO REGISTRATO. Il REC non va in tempo reale e il cursore T salta: per
// entrambi la camera deve essere una funzione di T. Mentre si guida, lo stato
// del veicolo (posa e velocita') si scrive su una traccia, un campione ogni
// passo fisso della fisica (kStep, lo stesso dal vivo e nel REC): la camera al
// tempo T e' la traccia interpolata a T (applyAt). Il tick dal vivo, il loop
// del recorder (MainWindow::advanceSceneBy) e il cursore passano tutti da
// advance/seekTo: e' il contratto del recorder, "il frame i mostra cio' che
// mostrerebbe lo schermo al tempo equivalente".
//   - Oltre la fine della traccia il veicolo prosegue con la sua fisica: dal
//     vivo coi comandi tenuti, nel REC e nel cursore senza comandi (cio' che
//     farebbe lo schermo senza toccare nulla). Ogni passo si aggiunge.
//   - Dentro la traccia (dopo un RESET, che riporta T a 0, o col cursore) la
//     camera rifa' il volo; un comando toccato dal vivo ne prende il posto: la
//     traccia si tronca a T e da li' si riscrive.
//   - Una camera spostata da altri (RESET View, cambio di inquadratura) e'
//     un taglio: due campioni allo stesso T.
//
// IL MOTO DELLA CAMERA DELL'AMBIENTE. La guida e' il terzo, esclusivo col path
// e con le rotazioni dell'ambiente (MainWindow): accesa avanza T. Spenta (DRIVE
// di nuovo, Esc, master STOP) resta la padrona della camera, come un path
// fermo: il cursore T e il RESET muovono ancora il volo. La lascia quando
// parte un altro moto della camera, o con una scena nuova (release).
#pragma once

#include "hyperbolicspace.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQuaternion>
#include <QVector3D>
#include <functional>
#include <vector>

class GLWidget;
class QTimer;

// I COMANDI IN USO: li scrive InputHandler (tasti premuti, trascinamenti,
// rotella), li legge il passo della guida. Dal vivo soltanto: il REC e il
// cursore guidano senza comandi.
struct DriveControls {
    // Tastiera
    bool throttleUp = false, throttleDown = false;   // W / S: la velocita' di crociera
    bool yawLeft = false, yawRight = false;          // A / D, frecce sinistra e destra
    bool pitchUp = false, pitchDown = false;         // frecce su e giu'
    bool rollLeft = false, rollRight = false;        // Q / E
    bool liftUp = false, liftDown = false;           // R / F: su e giu' nel riferimento della camera
    bool wPlus = false, wMinus = false;              // X / Z: verso W (4D)
    bool brake = false;                              // Spazio
    bool boost = false;                              // Maiuscole: tutto piu' veloce
    // Mouse. La CLOCHE (sinistro): lo scostamento dal punto premuto, in
    // frazioni di kStickRadius pixel, -1..1; da' la velocita' di virata (x) e
    // di beccheggio (y, verso l'alto = muso in su). Il ROLLIO (destro): lo
    // scostamento orizzontale.
    bool stick = false;
    float stickX = 0.0f, stickY = 0.0f;
    bool rollStick = false;
    float rollX = 0.0f;
    // Rotella: scatti dell'acceleratore non ancora consumati (anche frazioni,
    // il trackpad ne manda di piccole).
    float wheelSteps = 0.0f;
    // LA SCALA DELLE VELOCITA' (tasti + e -, per due): avanti, su/giu' e verso
    // W, per scene piu' grandi o piu' piccole. Le virate no. Non e' un tasto
    // tenuto: resta quando i comandi si rilasciano.
    float speedScale = 1.0f;
    static constexpr float kMinSpeedScale = 1.0f / 16.0f;
    static constexpr float kMaxSpeedScale = 16.0f;

    // Qualche comando in uso: dentro la traccia, il pilota prende il posto del volo registrato.
    bool any() const;
    void releaseAll()
    {
        const float scale = speedScale;
        *this = DriveControls();
        speedScale = scale;
    }
};

class CameraDrive : public QObject
{
    Q_OBJECT
    friend class ScenarioTest;   // legge lo stato del volo (stateAt)
public:
    // Il passo fisso della fisica (s) e il raggio della cloche (pixel).
    static constexpr float kStep = 1.0f / 60.0f;
    static constexpr float kStickRadius = 200.0f;

    // LO STATO DEL VEICOLO in un istante. La posa e' quella della camera
    // (camera -> mondo; guarda lungo -z, alto +y) nello spazio della traccia:
    // in R3 posizione e orientamento, in H3 l'isometria della camera. Le
    // velocita' sono nel riferimento della camera.
    struct State {
        float t = 0.0f;                 // T della scena
        QVector3D pos;                  // R3
        QQuaternion rot;                // R3
        H3::Matrix h3 = H3::Matrix::identity();   // H3
        float observerW = 0.0f;         // W dell'osservatore 4D (parametrico)
        float sectionP = 0.0f;          // quota del piano di sezione (ray marching, scene 3D)
        float speed = 0.0f;             // avanti, unita' della scena al secondo
        float cruise = 0.0f;            // la velocita' di crociera chiesta (W/S, rotella)
        QVector3D turn;                 // rad/s attorno agli assi della camera: x beccheggio, y virata, z rollio
        float lift = 0.0f;              // su/giu', unita' al secondo
        float wSpeed = 0.0f;            // verso W, unita' al secondo
    };

    // `recording`: vero durante il REC (il tick dal vivo e' un no-op: il tempo
    // lo avanza il loop del recorder). `movesW`: il 4D si puo' attraversare
    // (le stesse condizioni dei tasti P+/P- del dock 4D).
    CameraDrive(GLWidget *view, std::function<bool()> recording, std::function<bool()> movesW,
                QObject *parent = nullptr);

    DriveControls &controls() { return m_controls; }

    // DRIVE acceso: la guida comanda la camera e T corre.
    bool isOn() const;
    // La guida e' il moto della camera (accesa, o spenta con un volo).
    bool ownsCamera() const { return m_owns; }
    // Accende: dalla camera di adesso, o dal volo se T vi cade dentro.
    void start();
    // Spegne: la camera resta dov'e', il volo resta (ownsCamera).
    void stop();
    // Un altro moto prende la camera, o una scena nuova: spenta e senza volo.
    void release();

    // IL PASSO: T avanza di dt, la traccia si allunga fino a li' e la camera
    // va al nuovo T. `live`: dal tick, coi comandi; falso dal loop del REC.
    void advance(double dt, bool live);
    // Il cursore T: la camera al tempo target, la traccia allungata senza
    // comandi se serve.
    void seekTo(float target);
    // La camera della traccia al tempo t, senza toccare T (RESET dei moti: t = 0).
    void applyAt(float t);

    // Fin dove arriva la traccia (T), e se T sta rifacendo il volo registrato.
    bool hasTrack() const { return !m_track.empty(); }
    float trackEnd() const { return m_track.empty() ? 0.0f : m_track.back().t; }
    const std::vector<State> &track() const { return m_track; }
    bool replaying() const;
    // Lo stato a cui e' la camera (l'ultimo applicato): velocita' e W per l'indicatore.
    const State &current() const { return m_current; }

signals:
    // Il pilota ha usato un comando: la vista e' stata mossa dall'utente
    // (l'avviso "vuoi salvare?"). Una volta per accensione.
    void userDrove();
    // La camera e' andata a un nuovo stato (indicatore sulla vista).
    void stepped();

private:
    void tick();
    // Lo stato della camera com'e' ora nella vista (velocita' da `motion`).
    State stateFromView(float t, const State &motion) const;
    // Lo stato al tempo t, interpolato fra i campioni della traccia.
    State stateAt(float t) const;
    // Un passo di fisica dall'ultimo campione, coi comandi dati.
    void appendStep(const DriveControls &c);
    // Allunga la traccia fino a t (senza comandi oltre i primi).
    void extendTo(float t, const DriveControls *live);
    // Tronca la traccia a t (il campione a t interpolato, se manca).
    void truncateAt(float t);
    // Scrive lo stato nella vista.
    void apply(const State &s);
    // Qualcuno ha spostato la camera dall'ultima scrittura: un taglio.
    void adoptExternalMove();
    bool hyperbolic() const;

    GLWidget *m_view;
    std::function<bool()> m_recording;
    std::function<bool()> m_movesW;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    DriveControls m_controls;
    std::vector<State> m_track;
    State m_current;
    bool m_written = false;           // m_current e' stato scritto nella vista
    bool m_trackHyperbolic = false;   // lo spazio della traccia
    bool m_owns = false;
    bool m_droveSinceStart = false;
    // La scala delle velocita' dell'ultimo passo coi comandi: cambiandola la
    // crociera cambia con lei.
    float m_appliedScale = 1.0f;
};
