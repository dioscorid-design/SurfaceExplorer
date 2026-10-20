// camerapaths.h - I path della camera (4D e 3D): orologi, tempi, posa del
// path 4D e la camera che ne segue la traiettoria.
//
// Lo stato del MOTO, non della scena: le equazioni, la vista (tangente o
// centro) e la velocita' dei path sono della scena (SceneState, li legge da
// li'). La camera di un path al tempo t ha UNA implementazione, applyCameraAt,
// che usano sia il tick dal vivo sia il recorder video (advance): e' il
// contratto del recorder, "il frame i mostra cio' che mostrerebbe lo schermo
// al tempo equivalente". L'orchestrazione dei tasti (Departure, viste,
// master) resta a MainWindow.
#pragma once

#include <QObject>
#include <functional>

class GLWidget;
class QTimer;
struct SceneState;

class CameraPaths : public QObject
{
    Q_OBJECT
public:
    enum Path { Path4D, Path3D };
    // Chi girava quando i path sono stati fermati (dialogo modale, menu...).
    struct Paused { bool path4D = false; bool path3D = false; };

    // `recording`: vero durante il REC. Il timer resta ATTIVO (lo stato deve
    // restare vero per tasti, esclusivita' e gestori) ma il tempo lo avanza
    // SOLO il loop del recorder, col dt virtuale del frame: il tick dal vivo
    // e' un no-op.
    CameraPaths(GLWidget *view, const SceneState &scene,
                std::function<bool()> recording, QObject *parent = nullptr);

    bool isRunning(Path p) const;
    bool anyRunning() const { return isRunning(Path4D) || isRunning(Path3D); }
    void start(Path p);
    void stop(Path p);
    Paused pause();                     // ferma i path in corsa, dice quali
    void resume(const Paused &paused);
    int interval(Path p) const;         // ms fra due tick

    float time(Path p) const;
    void resetTimes();                  // t = 0 per entrambi
    // Sessione nuova (load di un record, reset della scena): t = 0, e il
    // prossimo Departure e' di nuovo il PRIMO (orientamento neutro).
    void resetSession();

    // Orientamento 4D (omega/phi/psi) della superficie all'avvio del path 4D:
    // il tick applica le compensazioni -gamma/-beta RELATIVE a questa base,
    // cosi' l'orientamento accumulato dal moto GO non viene azzerato a ogni
    // Departure. Solo il PRIMO Departure 4D della sessione parte da neutro.
    void captureBase4D();
    // Il PRIMO Departure della sessione (3D o 4D) azzera la rotazione spaziale
    // di default (neutralizeDefaultRotationForPath, lo fa chi chiama); dai
    // successivi si conserva l'orientamento accumulato (es. dal moto GO).
    // Vero una volta per sessione.
    bool takeFirstStart();

    // Un tick dal vivo (no-op durante il REC).
    void tick(Path p);
    // Avanza il path di `ticks` tick del suo timer (1 = un tick dal vivo; il
    // recorder passa la frazione del suo frame) e porta la camera li'.
    void advance(Path p, float ticks);
    // La camera del path al tempo t: UNICA implementazione (tick e video).
    void applyCameraAt(Path p, float t);

private:
    void applyCamera4DAt(float t);
    void applyCamera3DAt(float t);
    QTimer *timer(Path p) const { return p == Path4D ? m_timer4D : m_timer3D; }

    GLWidget *m_view;
    const SceneState &m_scene;
    std::function<bool()> m_recording;
    QTimer *m_timer4D = nullptr;
    QTimer *m_timer3D = nullptr;
    float m_t4D = 0.0f;
    float m_t3D = 0.0f;
    float m_baseOmega = 0.0f;
    float m_basePhi = 0.0f;
    float m_basePsi = 0.0f;
    bool m_started4DOnce = false;
    bool m_anyStartedOnce = false;
};
