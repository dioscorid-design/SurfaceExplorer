// glwidget_drive.cpp - GLWidget: gli urti della guida libera (2.1). La sonda
// che rilegge dalla GPU le superfici come le disegna, e lo scivolamento della
// camera fra loro. La geometria dell'urto e' in drivecollision.h; il passo
// della guida che la usa e' CameraDrive::appendStep.
// Parte della classe GLWidget divisa per argomento, come glwidget_object.cpp.
#include "glwidget.h"
#include "cameradrive.h"
#include "drivecollision.h"
#include "tubecurvesampler.h"

#include <QDebug>
#include <QVarLengthArray>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
// Le celle per lato di una parte, al massimo: oltre, la sonda campiona la
// griglia piu' rada (le griglie del disegno arrivano di rado fin qui).
constexpr int kProbeMaxCells = 256;

// I binding della sonda, lontani da quelli del vertex: 0 il blocco della
// parte, 2 la sezione trasportata dei tubi (3 e 4, i lati del bordo, nel
// vertex senza bordo non ci sono).
constexpr int kBindParts = 5;
constexpr int kBindPoints = 6;
constexpr int kBindVertices = 7;

DriveProbe::Kind kindOf(const QString &vertexSource)
{
    if (vertexSource.contains(QLatin1String("#define IS_TUBE"))) return DriveProbe::Tube;
    if (vertexSource.contains(QLatin1String("#define CUSTOM_MESH"))) return DriveProbe::Custom;
    return DriveProbe::Surface;
}

// IL MAIN DELLA SONDA, in coda al vertex ridotto a libreria. Una passata per
// parte, col blocco della parte (offset dinamico, come i disegni): un nodo
// per invocazione, nello spazio dell'oggetto, coi conti del vertex.
//   - superficie: getRotatedPoint4D e projectToScene, come main();
//   - tubo: tubeScenePoint, il punto dell'anello come emitTubeVertex (con la
//     sezione trasportata, se la passata dei tubi c'e');
//   - mesh custom: i vertici sono posizioni (getRawPosition rende `vertex`).
// w: 1 il nodo vale, 0 oltre il tetto della stereografica (le corde che vi
// arrivano il fragment le scarta: sarebbero muri invisibili); + 2 e + 4 i due
// triangoli della cella tagliati da cutHere, nel baricentro.
QString probeMain(DriveProbe::Kind kind, bool cutout)
{
    QString s = QStringLiteral(R"(
layout(local_size_x = 64) in;
// In testa (parti, 0, 0, 0); per parte (u_min, u_max, v_min, v_max) e
// (indice della mesh, celle in u, celle in v, primo nodo).
layout(std430, binding = %1) readonly buffer DriveProbeParts { vec4 p[]; } probeParts;
layout(std430, binding = %2) writeonly buffer DriveProbePoints { vec4 q[]; } probePoints;
)").arg(kBindParts).arg(kBindPoints);
    if (kind == DriveProbe::Custom)
        s += QStringLiteral("layout(std430, binding = %1) readonly buffer DriveProbeVertices { vec4 v[]; } probeVertices;\n")
                 .arg(kBindVertices);
    s += QStringLiteral(R"(
// La parte di questa passata: quella dell'indice di mesh del blocco.
int probePart() {
    int count = int(probeParts.p[0].x + 0.5);
    if (count <= 1) return 0;
    for (int k = 0; k < count; ++k)
        if (abs(probeParts.p[2 + 2 * k].x - ubuf.u_meshIndex) < 0.5) return k;
    return -1;
}

void main() {
    int part = probePart();
    if (part < 0) return;
    vec4 d = probeParts.p[1 + 2 * part];
    vec4 info = probeParts.p[2 + 2 * part];
    int nu = int(info.y + 0.5);
    int nv = int(info.z + 0.5);
    int local = int(gl_GlobalInvocationID.x);
    if (local >= (nu + 1) * (nv + 1)) return;
    int i = local / (nv + 1);
    int j = local - i * (nv + 1);
    // Come i vertici della CPU (SurfaceEngine::generateParametricGrid).
    float u = d.x + float(i) / float(max(nu, 1)) * (d.y - d.x);
    float v = d.z + float(j) / float(max(nv, 1)) * (d.w - d.z);
    vec3 q;
    float flags = 1.0;
)");
    switch (kind) {
    case DriveProbe::Surface:
        s += QStringLiteral(R"(
    vec4 pRot = getRotatedPoint4D(u, v);
    q = projectToScene(pRot);
    if (ubuf.u_projMode == 2 && stereoBeyondCap(pRot) > 0.5) flags = 0.0;
)");
        break;
    case DriveProbe::Tube:
        s += QStringLiteral("    q = tubeScenePoint(u, v);\n");
        break;
    case DriveProbe::Custom:
        s += QStringLiteral(R"(
    vertex = probeVertices.v[local];
    vec4 pRot = getRotatedPoint4D(vertex.x, vertex.y);
    q = projectToScene(pRot);
    if (ubuf.u_projMode == 2 && stereoBeyondCap(pRot) > 0.5) flags = 0.0;
)");
        break;
    }
    if (cutout) {
        s += QStringLiteral(R"(
    // I buchi del taglio, nel baricentro dei due triangoli della cella (il
    // fragment lo valuta in ogni pixel: qui il buco ha la grana della griglia).
    if (i < nu && j < nv) {
        float u1 = d.x + float(i + 1) / float(nu) * (d.y - d.x);
        float v1 = d.z + float(j + 1) / float(nv) * (d.w - d.z);
        if (cutHere((2.0 * u + u1) / 3.0, (2.0 * v + v1) / 3.0)) flags += 2.0;
        if (cutHere((u + 2.0 * u1) / 3.0, (v + 2.0 * v1) / 3.0)) flags += 4.0;
    }
)");
    }
    s += QStringLiteral("    probePoints.q[int(info.w + 0.5) + local] = vec4(q, flags);\n}\n");
    return s;
}

template <typename T>
void destroyResource(T *&r)
{
    if (!r) return;
    r->destroy();
    delete r;
    r = nullptr;
}

void releaseProbePipeline(DriveProbe &p)
{
    destroyResource(p.pipeline);
    destroyResource(p.srb);
    destroyResource(p.parts);
    destroyResource(p.points);
    destroyResource(p.vertices);
    p.partsCapacity = p.pointsCapacity = p.verticesCapacity = 0;
}

// Un buffer di almeno `needed` vec4: si rifa' piu' grande quando non basta.
bool ensureStorage(QRhi *rhi, QRhiBuffer *&buffer, quint32 &capacity, quint32 needed)
{
    if (buffer && needed <= capacity) return true;
    destroyResource(buffer);
    capacity = std::max({ needed, 2 * capacity, quint32(64) });
    buffer = rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, capacity * 16u);
    return buffer->create();
}

// La lettura e' arrivata: i nodi nelle parti del collider.
void finishProbe(DriveProbe &p, int serial)
{
    p.reading = false;
    if (serial != p.serial) return;   // il vertex e' cambiato nel frattempo: nodi di un'altra superficie
    const QByteArray &data = p.result.data;
    const auto *nodes = reinterpret_cast<const QVector4D *>(data.constData());
    const size_t available = size_t(data.size()) / sizeof(QVector4D);
    DriveCollider collider;
    size_t offset = 0;
    for (const DriveProbe::PartLayout &l : p.layout) {
        const size_t count = size_t(l.nu + 1) * size_t(l.nv + 1);
        if (offset + count > available) break;
        DriveColliderPart part;
        part.nu = l.nu;
        part.nv = l.nv;
        part.points.resize(count);
        part.valid.resize(count);
        if (l.cut) part.cut.assign(size_t(l.nu) * size_t(l.nv), 0);
        for (size_t k = 0; k < count; ++k) {
            const QVector4D &q = nodes[offset + k];
            const int flags = int(std::lround(q.w()));
            const bool finite = std::isfinite(q.x()) && std::isfinite(q.y()) && std::isfinite(q.z())
                                && std::abs(q.x()) < 1e7f && std::abs(q.y()) < 1e7f && std::abs(q.z()) < 1e7f;
            part.points[k] = q.toVector3D();
            part.valid[k] = (finite && (flags & 1)) ? 1 : 0;
            if (l.cut) {
                const int i = int(k) / (l.nv + 1), j = int(k) % (l.nv + 1);
                if (i < l.nu && j < l.nv) part.cut[size_t(i) * size_t(l.nv) + size_t(j)] = (unsigned char)((flags >> 1) & 3);
            }
        }
        offset += count;
        collider.parts.push_back(std::move(part));
    }
    // La mesh custom: i triangoli dei suoi indici (una parte sola).
    if (p.kind == DriveProbe::Custom && collider.parts.size() == 1) collider.parts.front().indices = p.indices;
    collider.finalize();
    p.collider = std::move(collider);
    ++p.arrived;
}
} // namespace

bool GLWidget::driveCollisionsOn() const
{
    return m_driveCamera && m_driveControls && m_driveControls->collisions && !m_hyperbolic && !m_isFlatView;
}

bool GLWidget::buildDriveProbePipeline(SurfaceObject &o, DriveProbe &probe)
{
    const QString lib = vertexAsComputeLibrary(probe.source, QStringLiteral("DRIVE_PROBE_COMPUTE"),
                                               QStringLiteral("surfaceVertexMain"), QStringLiteral("g_driveProbePosition"));
    if (lib.isEmpty()) return false;

    // Il taglio con la pipeline; se non compilasse nel compute, la sonda va
    // senza (i buchi restano pieni per gli urti, la superficie urta comunque).
    QShader shader;
    QString error;
    probe.cutCompiled = false;
    if (probe.kind != DriveProbe::Custom && !probe.cutout.trimmed().isEmpty()) {
        const QString src = lib + QStringLiteral("\nbool cutHere(float u, float v) {\n") + generateGlslHelperVars(probe.cutout)
                            + probe.cutout + QStringLiteral("\n}\n") + probeMain(probe.kind, true);
        probe.cutCompiled = TubeCurveSampler::bakeCompute(rhi(), src, &shader, &error);
        if (!probe.cutCompiled)
            qWarning() << "DRIVE: il taglio non compila nella sonda degli urti, i suoi buchi urtano:" << error;
    }
    if (!probe.cutCompiled && !TubeCurveSampler::bakeCompute(rhi(), lib + probeMain(probe.kind, false), &shader, &error)) {
        qWarning() << "DRIVE: sonda degli urti non compilata, la superficie non ferma la camera:" << error;
        return false;
    }
    if (!ensureStorage(rhi(), probe.parts, probe.partsCapacity, 64)
        || !ensureStorage(rhi(), probe.points, probe.pointsCapacity, 4096)
        || (probe.kind == DriveProbe::Custom && !ensureStorage(rhi(), probe.vertices, probe.verticesCapacity, 4096))) {
        releaseProbePipeline(probe);
        return false;
    }
    probe.srb = rhi()->newShaderResourceBindings();
    setDriveProbeBindings(o, probe);
    probe.pipeline = rhi()->newComputePipeline();
    probe.pipeline->setShaderResourceBindings(probe.srb);
    probe.pipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, shader));
    if (!probe.srb->create() || !probe.pipeline->create()) {
        qWarning() << "DRIVE: pipeline della sonda degli urti non creata.";
        releaseProbePipeline(probe);
        return false;
    }
    return true;
}

void GLWidget::setDriveProbeBindings(SurfaceObject &o, DriveProbe &probe)
{
    using B = QRhiShaderResourceBinding;
    QVarLengthArray<QRhiShaderResourceBinding, 5> b;
    // Il blocco della parte, quello dei disegni (o.ubo, che puo' essere
    // rifatto piu' grande: si riaggancia a ogni passata).
    b.append(B::uniformBufferWithDynamicOffset(0, B::ComputeStage, o.ubo, sizeof(UboData)));
    if (probe.kind == DriveProbe::Tube)
        b.append(B::sampledTexture(2, B::ComputeStage, o.tubeFrameTex ? o.tubeFrameTex : m_dummyTexture,
                                   o.tubeFrameSampler ? o.tubeFrameSampler : m_sampler));
    b.append(B::bufferLoad(kBindParts, B::ComputeStage, probe.parts));
    b.append(B::bufferStore(kBindPoints, B::ComputeStage, probe.points));
    if (probe.kind == DriveProbe::Custom) b.append(B::bufferLoad(kBindVertices, B::ComputeStage, probe.vertices));
    probe.srb->setBindings(b.cbegin(), b.cend());
}

QRhiResourceUpdateBatch *GLWidget::runDriveProbe(SurfaceObject &o, QRhiCommandBuffer *cb,
                                                 QRhiResourceUpdateBatch *resourceUpdates)
{
    if (!driveCollisionsOn()) return resourceUpdates;
    // Le superfici del vertex parametrico: equazioni, script, tubi, mesh
    // custom. Il ray marching (implicite) non ha nodi da leggere.
    const bool probeable = o.engineMode == ModeParametric && o.engine && o.ubo && !o.driveVertexSource.isEmpty()
                           && !o.engine->getMeshParts().empty() && rhi()->isFeatureSupported(QRhi::Compute);
    if (!probeable) {
        if (o.driveProbe && !o.driveProbe->collider.empty()) o.driveProbe->collider = DriveCollider();
        return resourceUpdates;
    }
    if (!o.driveProbe) o.driveProbe = std::make_shared<DriveProbe>();
    DriveProbe &probe = *o.driveProbe;

    // UN VERTEX NUOVO (un'altra superficie, equazioni cambiate) o un altro
    // taglio: pipeline nuova, e il collider di prima non vale piu'. La copia
    // tiene vivo il testo di prima: lo stesso puntatore e' lo stesso vertex,
    // senza confrontarlo a ogni frame.
    const DriveProbe::Kind kind = kindOf(o.driveVertexSource);
    const QString cutout = kind == DriveProbe::Custom ? QString() : o.engine->getCutoutCodeGLSL();
    if (probe.source.constData() != o.driveVertexSource.constData() || probe.cutout != cutout) {
        if (probe.source != o.driveVertexSource || probe.cutout != cutout) {
            releaseProbePipeline(probe);
            probe.failed = false;
            probe.key.clear();
            probe.collider = DriveCollider();
            ++probe.serial;
        }
        probe.source = o.driveVertexSource;
        probe.cutout = cutout;
        probe.kind = kind;
    }
    if (probe.failed) return resourceUpdates;
    if (probe.reading) {
        // La lettura arriva in un frame successivo: finche' non arriva se ne
        // chiedono altri (una scena ferma non ne disegnerebbe).
        update();
        return resourceUpdates;
    }

    // LA TABELLA DELLE PARTI: dominio, mesh, celle, primo nodo. La mesh
    // custom e' una parte sola coi suoi vertici in fila (nv = 0).
    const std::vector<MeshPart> &meshParts = o.engine->getMeshParts();
    std::vector<QVector4D> table;
    table.reserve(1 + 2 * meshParts.size());
    table.emplace_back(float(meshParts.size()), 0.0f, 0.0f, 0.0f);
    std::vector<DriveProbe::PartLayout> layout;
    std::vector<quint32> partNodes;
    quint32 total = 0;
    std::vector<QVector4D> customVertices;
    for (const MeshPart &mp : meshParts) {
        int nu = std::clamp(mp.numU, 1, kProbeMaxCells);
        int nv = std::clamp(mp.numV, 1, kProbeMaxCells);
        if (kind == DriveProbe::Custom) {
            const std::vector<Vertex> &verts = o.engine->getVertices();
            customVertices.reserve(verts.size());
            for (const Vertex &vx : verts) customVertices.push_back(vx.position);
            nu = std::max(0, int(verts.size()) - 1);
            nv = 0;
        }
        table.emplace_back(mp.uMin, mp.uMax, mp.vMin, mp.vMax);
        table.emplace_back(float(mp.meshIndex), float(nu), float(nv), float(total));
        layout.push_back({ nu, nv, false });
        const quint32 nodes = quint32(nu + 1) * quint32(nv + 1);
        partNodes.push_back(nodes);
        total += nodes;
        if (kind == DriveProbe::Custom) break;   // una parte sola
    }
    if (kind == DriveProbe::Custom) table[0].setX(1.0f);
    if (total == 0) return resourceUpdates;

    // GLI INGRESSI della posizione dei nodi, oltre al vertex: tabella,
    // costanti, tempo, rotazioni 4D, osservatore e proiezione, P dell'oggetto;
    // per i tubi gli spessori, per la mesh custom i suoi vertici. Uguali: la
    // lettura di prima vale ancora. La posa dell'oggetto no: si applica sulla
    // CPU (driveColliders).
    QByteArray key(reinterpret_cast<const char *>(table.data()), int(table.size() * sizeof(QVector4D)));
    const auto add = [&key](const void *field, size_t bytes) {
        key.append(reinterpret_cast<const char *>(field), qsizetype(bytes));
    };
    add(&m_uboData.mathParams, sizeof(QVector4D));
    add(&m_uboData.mathParams2, 3 * sizeof(float));   // la .w e' la scala dei pixel delle catture
    add(&m_uboData.hasExplicitW, sizeof(int));
    add(&m_uboData.u_meshCount, sizeof(float));
    add(&m_uboData.time, sizeof(float));
    add(&m_uboData.omega, 3 * sizeof(float));          // omega, phi, psi
    add(&m_uboData.projMode, sizeof(int));
    add(&m_uboData.observerPos, sizeof(QVector4D));
    const float objectP = m_uboData.dummyZero.w();
    add(&objectP, sizeof(float));
    if (kind == DriveProbe::Tube) {
        add(&o.tubeRadius, sizeof(float));
        for (const MeshPart &mp : meshParts) add(&mp.tubeThickness, sizeof(float));
    }
    if (kind == DriveProbe::Custom) {
        const std::vector<unsigned int> &idx = o.engine->getIndices();
        const size_t hv = qHashBits(customVertices.data(), customVertices.size() * sizeof(QVector4D));
        const size_t hi = qHashBits(idx.data(), idx.size() * sizeof(unsigned int));
        add(&hv, sizeof(hv));
        add(&hi, sizeof(hi));
    }
    if (key == probe.key) return resourceUpdates;

    if (!probe.pipeline && !buildDriveProbePipeline(o, probe)) {
        probe.failed = true;
        return resourceUpdates;
    }
    for (DriveProbe::PartLayout &l : layout) l.cut = probe.cutCompiled;
    // I buffer crescono con la superficie; i binding seguono loro e o.ubo.
    if (!ensureStorage(rhi(), probe.parts, probe.partsCapacity, quint32(table.size()))
        || !ensureStorage(rhi(), probe.points, probe.pointsCapacity, total)
        || (kind == DriveProbe::Custom
            && !ensureStorage(rhi(), probe.vertices, probe.verticesCapacity, quint32(customVertices.size())))) {
        releaseProbePipeline(probe);
        probe.failed = true;
        return resourceUpdates;
    }
    setDriveProbeBindings(o, probe);
    probe.srb->updateResources();

    if (!resourceUpdates) resourceUpdates = rhi()->nextResourceUpdateBatch();
    resourceUpdates->uploadStaticBuffer(probe.parts, 0, quint32(table.size() * sizeof(QVector4D)), table.data());
    if (kind == DriveProbe::Custom)
        resourceUpdates->uploadStaticBuffer(probe.vertices, 0, quint32(customVertices.size() * sizeof(QVector4D)),
                                            customVertices.data());
    // Una passata per parte, col blocco della parte (offset come nei disegni:
    // con una parte sola il blocco 0).
    cb->beginComputePass(resourceUpdates);
    cb->setComputePipeline(probe.pipeline);
    for (size_t k = 0; k < partNodes.size(); ++k) {
        const quint32 ofs = meshParts.size() <= 1 ? 0u : quint32(k) * o.uboBlockStride;
        const QRhiCommandBuffer::DynamicOffset dyn(0, ofs);
        cb->setShaderResources(probe.srb, 1, &dyn);
        cb->dispatch(int((partNodes[k] + 63) / 64), 1, 1);
    }
    cb->endComputePass();

    // LA LETTURA arriva dentro un frame successivo e si chiude fuori da render,
    // in coda. La lambda tiene viva la sonda finche' non arriva (un oggetto
    // tolto nel frattempo), poi si stacca: e' lei a rompere il giro.
    QRhiResourceUpdateBatch *rb = rhi()->nextResourceUpdateBatch();
    probe.result = {};
    const int serial = probe.serial;
    probe.result.completed = [this, keep = o.driveProbe, serial] {
        QMetaObject::invokeMethod(this, [keep, serial] {
            keep->result.completed = nullptr;
            finishProbe(*keep, serial);
        }, Qt::QueuedConnection);
    };
    rb->readBackBuffer(probe.points, 0, total * 16u, &probe.result);
    cb->resourceUpdate(rb);
    probe.reading = true;
    probe.key = key;
    probe.layout = std::move(layout);
    if (kind == DriveProbe::Custom) probe.indices = o.engine->getIndices();
    else probe.indices.clear();
    update();
    return nullptr;
}

void GLWidget::releaseDriveProbe(SurfaceObject &o)
{
    if (!o.driveProbe) return;
    releaseProbePipeline(*o.driveProbe);
    o.driveProbe->key.clear();
    o.driveProbe->failed = false;
}

std::vector<DrivePlacedCollider> GLWidget::driveColliders() const
{
    std::vector<DrivePlacedCollider> colliders;
    for (const auto &obj : m_objects) {
        const SurfaceObject &o = *obj;
        if (!o.driveProbe || o.driveProbe->collider.empty() || !isOnStage(o)) continue;
        // Il collider di un vertex di prima (un'altra superficie caricata a
        // guida spenta) non vale: finche' la sonda non rilegge, niente urti.
        if (o.driveProbe->source.constData() != o.driveVertexSource.constData()) continue;
        colliders.push_back({ &o.driveProbe->collider, o.model });
    }
    return colliders;
}

QVector3D GLWidget::slideDriveCamera(const QVector3D &from, const QVector3D &move, float radius, bool *contact) const
{
    return driveSlide(driveColliders(), from, move, radius, contact);
}

float GLWidget::driveSurfaceDistance(const QVector3D &p) const
{
    return driveDistance(driveColliders(), p);
}
