// drivecollision.h - GLI URTI DELLA GUIDA LIBERA (2.1): la camera non
// attraversa le superfici, ci scivola sopra.
//
// LA SUPERFICIE COME LA VEDE L'URTO. La CPU non sa dove sta una superficie
// parametrica: la sua griglia porta i soli parametri (u, v), e la posizione la
// calcola il vertex shader (equazioni o script, rotazioni 4D, proiezione). Per
// non rifare quei conti in una seconda copia, che divergerebbe, li fa la GPU con
// lo STESSO vertex ridotto a compute (GLWidget::runDriveProbe, come la passata
// dei lati coperti del bordo): un punto per nodo della griglia disegnata, nello
// spazio dell'oggetto, riletto qui. La posa dell'oggetto (o.model) si applica
// dopo, sulla CPU: un oggetto che si muove rigido non chiede una nuova lettura.
// I triangoli sono quelli del disegno (SurfaceEngine::generateParametricGrid):
// la camera si ferma sulla superficie che si vede.
//
// LA CAMERA E' UNA SFERA di raggio CameraDrive::kCollisionRadius, piu' grande
// del piano near della guida: toccando la superficie non la si vede tagliata.
// Le superfici sono fogli a due facce, senza un dentro: l'urto e' CONTINUO,
// fra la posizione prima e dopo il passo (una sfera che scorre), cosi' un
// passo lungo non salta un foglio di spessore zero.
//
// LO SCIVOLAMENTO (driveSlide): al contatto la camera si ferma un soffio prima,
// la parte del moto contro la superficie si toglie e il resto prosegue lungo
// di lei, per qualche rimbalzo (spigoli, pieghe). Il moto della superficie non
// spinge la camera: una superficie che la investe la attraversa, la camera e'
// bloccata solo nel suo moto verso la superficie.
#pragma once

#include <rhi/qrhi.h>
#include <QByteArray>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <utility>
#include <vector>

// Una parte della superficie (MeshPart), nello spazio dell'oggetto, come la
// disegna la GPU. Di solito la griglia dei nodi (superfici e tubi); per una
// mesh custom (flusso geodetico) i nodi sono i suoi vertici e i triangoli
// quelli dei suoi indici, che saltano i quad coi punti morti.
struct DriveColliderPart {
    int nu = 0, nv = 0;                 // celle in u e in v (griglia)
    std::vector<QVector3D> points;      // griglia: (nu + 1) * (nv + 1), indice i * (nv + 1) + j
    std::vector<unsigned char> valid;   // 0 = punto fuori (oltre il tetto della stereografica, non finito)
    // I BUCHI DEL TAGLIO (//CUTOUT_BEGIN, cutHere del fragment): per cella, bit
    // 0 il primo triangolo tagliato, bit 1 il secondo. Vuoto = nessun taglio.
    std::vector<unsigned char> cut;
    // Lista di triangoli (mesh custom): tre indici nei punti per triangolo.
    // Non vuota = la parte non e' una griglia.
    std::vector<unsigned int> indices;
    // Blocchi col loro ingombro: si guardano solo i triangoli dei blocchi che
    // il moto della sfera tocca.
    struct Tile {
        QVector3D lo, hi;
        int i0 = 0, i1 = 0, j0 = 0, j1 = 0;   // griglia: celle [i0, i1) x [j0, j1); lista: triangoli [i0, i1)
    };
    std::vector<Tile> tiles;
    QVector3D lo, hi;
};

struct DriveCollider {
    std::vector<DriveColliderPart> parts;
    QVector3D lo, hi;   // l'ingombro di tutte le parti
    bool empty() const { return parts.empty(); }
    // Dopo aver riempito punti e validi: i blocchi e gli ingombri.
    void finalize();

    // La sfera di raggio r che parte da p e si sposta di d (spazio
    // dell'oggetto): il primo contatto, a frazione *t del moto (0..1), con la
    // normale *n (unitaria, dalla superficie verso il centro della sfera). Una
    // sfera che all'inizio tocca gia' un triangolo urta a t = 0 solo se si
    // muove verso di lui; allontanandosi e' libera.
    bool sweep(const QVector3D &p, const QVector3D &d, float r, float *t, QVector3D *n) const;
    // La distanza del punto dalla superficie (i triangoli validi); molto grande se vuota.
    float distance(const QVector3D &p) const;
};

// Un collider al suo posto nel mondo: la posa dell'oggetto.
struct DrivePlacedCollider {
    const DriveCollider *collider = nullptr;
    QMatrix4x4 model;
};

// LO SCIVOLAMENTO: dove arriva la camera (centro di una sfera di raggio
// `radius`) che da `from` vuole spostarsi di `move`, fra i collider dati.
// *contact = qualche urto nel passo.
QVector3D driveSlide(const std::vector<DrivePlacedCollider> &colliders, const QVector3D &from,
                     const QVector3D &move, float radius, bool *contact = nullptr);
// La distanza del punto dalla superficie piu' vicina fra i collider.
float driveDistance(const std::vector<DrivePlacedCollider> &colliders, const QVector3D &p);

// LA SONDA DELLA GPU di un oggetto (GLWidget::runDriveProbe): la pipeline nata
// dal suo vertex, i buffer, la lettura in corso e l'ultimo collider arrivato.
// La tiene SurfaceObject::driveProbe; le risorse le libera
// GLWidget::releaseDriveProbe con quelle dell'oggetto.
struct DriveProbe {
    // Che cosa disegna il vertex: superficie (u, v), tubo attorno a una curva,
    // mesh custom (i vertici sono posizioni).
    enum Kind { Surface, Tube, Custom };
    Kind kind = Surface;
    QString source;                   // il vertex da cui e' nata la pipeline
    QString cutout;                   // e il corpo del taglio (cutHere), compilato con lei
    bool cutCompiled = false;         // la pipeline valuta il taglio (se no, i suoi buchi urtano)
    int serial = 0;                   // cresce a ogni vertex nuovo: una lettura del vertex di prima si scarta
    bool failed = false;              // non compilata: si riprova solo con un vertex nuovo
    QRhiComputePipeline *pipeline = nullptr;
    QRhiShaderResourceBindings *srb = nullptr;
    QRhiBuffer *parts = nullptr;      // la tabella delle parti
    QRhiBuffer *points = nullptr;     // i nodi, un vec4 ciascuno
    QRhiBuffer *vertices = nullptr;   // mesh custom: le posizioni 4D dei vertici
    quint32 partsCapacity = 0, pointsCapacity = 0, verticesCapacity = 0;   // in vec4
    QByteArray key;                   // gli ingressi della lettura in corso o dell'ultima
    bool reading = false;
    QRhiReadbackResult result;
    // La lettura in corso: celle (nu, nv) delle parti, se hanno il taglio, e
    // per la mesh custom i suoi triangoli.
    struct PartLayout {
        int nu = 0, nv = 0;
        bool cut = false;
    };
    std::vector<PartLayout> layout;
    std::vector<unsigned int> indices;
    DriveCollider collider;           // l'ultimo arrivato
    int arrived = 0;                  // quante letture sono arrivate (scenari)
};
