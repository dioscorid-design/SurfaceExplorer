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
#include <QRegularExpression>
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

// LE IMPLICITE: la griglia del campo, nodi per lato e meta' lato in unita'
// della scena (per la scala delle velocita' della guida: scene piu' grandi,
// cubo piu' grande). Cella ~0.034, poco piu' del raggio della camera.
constexpr int kFieldNodes = 48;
constexpr float kFieldHalf = 0.8f;

// IL MAIN DELLA SONDA DELLE IMPLICITE, in coda alla parte del ray marcher che
// calcola il campo (rawField, map): un nodo per invocazione, nello spazio
// dell'oggetto, col campo che il marcher disegna. Pieno: map(), il solido
// tagliato dal cubo. Guscio: lo zero del campo grezzo dentro il cubo (il
// guscio, spesso un centesimo, la griglia non lo vedrebbe; la camera e' piu'
// grossa di lui), fuori dal cubo 1e30, nessuna superficie.
QString fieldProbeMain()
{
    return QStringLiteral(R"(
layout(local_size_x = 64) in;
// (centro, meta' lato) e (nodi per lato, 0, 0, 0)
layout(std430, binding = %1) readonly buffer DriveProbeGrid { vec4 g[]; } probeGrid;
layout(std430, binding = %2) writeonly buffer DriveProbeField { float f[]; } probeField;

void main() {
    vec4 c = probeGrid.g[0];
    int n = int(probeGrid.g[1].x + 0.5);
    int id = int(gl_GlobalInvocationID.x);
    if (id >= n * n * n) return;
    int i = id / (n * n);
    int rest = id - i * n * n;
    int j = rest / n;
    int k = rest - j * n;
    vec3 pos = c.xyz + (vec3(float(i), float(j), float(k)) * (2.0 / float(n - 1)) - 1.0) * c.w;
    float value;
    if (ubuf.u_renderMode == 1) {
        vec3 bMin = vec3(ubuf.x_min, ubuf.y_min, ubuf.z_min);
        vec3 bMax = vec3(ubuf.x_max, ubuf.y_max, ubuf.z_max);
        vec3 q = abs(pos - (bMax + bMin) * 0.5) - (bMax - bMin) * 0.5;
        value = (max(q.x, max(q.y, q.z)) > 0.0) ? 1.0e30 : rawField(pos);
    } else {
        value = map(pos);
    }
    probeField.f[id] = value;
}
)").arg(kBindParts).arg(kBindPoints);
}

// La parte del fragment del ray marcher che calcola il campo: tutto cio' che
// precede le normali (dichiarazioni, rawField, map, mapInner), con ingressi e
// uscite del fragment ridotti a variabili globali. Il resto (marcia, luci,
// texture, main) usa cio' che un compute non ha.
QString fieldLibrary(const QString &fragment)
{
    const int cut = fragment.indexOf(QLatin1String("vec3 calcNormal(vec3 pos)"));
    if (cut < 0) return QString();
    QString src = fragment.left(cut);
    static const QRegularExpression ioRe(QStringLiteral(R"(layout\s*\(\s*location\s*=\s*\d+\s*\)\s*(in|out)\s+)"));
    src.remove(ioRe);
    return src;
}

// La lettura e' arrivata: i nodi nelle parti del collider.
void finishProbe(DriveProbe &p, int serial)
{
    p.reading = false;
    if (serial != p.serial) return;   // il vertex e' cambiato nel frattempo: nodi di un'altra superficie
    const QByteArray &data = p.result.data;
    if (p.kind == DriveProbe::Implicit) {
        // Il campo nella griglia della lettura.
        DriveCollider collider;
        collider.field = p.pendingGrid;
        const size_t count = size_t(p.pendingGrid.n) * size_t(p.pendingGrid.n) * size_t(p.pendingGrid.n);
        if (size_t(data.size()) / sizeof(float) < count) return;
        const auto *values = reinterpret_cast<const float *>(data.constData());
        collider.field.values.assign(values, values + count);
        collider.finalize();
        p.collider = std::move(collider);
        ++p.arrived;
        return;
    }
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
    return m_driveCamera && m_driveControls && m_driveControls->collisions && !m_isFlatView;
}

bool GLWidget::buildDriveProbePipeline(SurfaceObject &o, DriveProbe &probe)
{
    QShader shader;
    QString error;
    probe.cutCompiled = false;
    if (probe.kind == DriveProbe::Implicit) {
        const QString lib = fieldLibrary(probe.source);
        if (lib.isEmpty() || !TubeCurveSampler::bakeCompute(rhi(), lib + fieldProbeMain(), &shader, &error)) {
            qWarning() << "DRIVE: sonda degli urti del ray marching non compilata, la superficie non ferma la camera:"
                       << error;
            return false;
        }
    }
    if (probe.kind != DriveProbe::Implicit) {
        const QString lib = vertexAsComputeLibrary(probe.source, QStringLiteral("DRIVE_PROBE_COMPUTE"),
                                                   QStringLiteral("surfaceVertexMain"), QStringLiteral("g_driveProbePosition"));
        if (lib.isEmpty()) return false;
        // Il taglio con la pipeline; se non compilasse nel compute, la sonda va
        // senza (i buchi restano pieni per gli urti, la superficie urta comunque).
        if (probe.kind != DriveProbe::Custom && !probe.cutout.trimmed().isEmpty()) {
            const QString src = lib + QStringLiteral("\nbool cutHere(float u, float v) {\n")
                                + generateGlslHelperVars(probe.cutout) + probe.cutout + QStringLiteral("\n}\n")
                                + probeMain(probe.kind, true);
            probe.cutCompiled = TubeCurveSampler::bakeCompute(rhi(), src, &shader, &error);
            if (!probe.cutCompiled)
                qWarning() << "DRIVE: il taglio non compila nella sonda degli urti, i suoi buchi urtano:" << error;
        }
        if (!probe.cutCompiled && !TubeCurveSampler::bakeCompute(rhi(), lib + probeMain(probe.kind, false), &shader, &error)) {
            qWarning() << "DRIVE: sonda degli urti non compilata, la superficie non ferma la camera:" << error;
            return false;
        }
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
    if (probe.kind == DriveProbe::Implicit) {
        // Il campo puo' leggere la texture della superficie (i rilievi): la sua, come il marcher.
        b.append(B::sampledTexture(1, B::ComputeStage, o.surfaceTexture ? o.surfaceTexture : m_dummyTexture, m_sampler));
        b.append(B::bufferLoad(kBindParts, B::ComputeStage, probe.parts));
        b.append(B::bufferStore(kBindPoints, B::ComputeStage, probe.points));
        probe.srb->setBindings(b.cbegin(), b.cend());
        return;
    }
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
    if (o.engineMode == ModeImplicit) return runDriveFieldProbe(o, cb, resourceUpdates);
    // Le superfici del vertex parametrico: equazioni, script, tubi, mesh custom.
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

// LA SONDA DELLE IMPLICITE: il campo del ray marcher su una griglia attorno
// alla camera (DriveFieldGrid), nello spazio dell'oggetto. Si rilegge quando
// cambia il campo (costanti, tempo, sezione 4D, cubo di taglio, pieno o
// guscio) o quando la camera esce dal terzo centrale del cubo: il cubo nuovo
// le sta attorno, e finche' non arriva vale quello di prima.
QRhiResourceUpdateBatch *GLWidget::runDriveFieldProbe(SurfaceObject &o, QRhiCommandBuffer *cb,
                                                      QRhiResourceUpdateBatch *resourceUpdates)
{
    if (!o.ubo || !rhi()->isFeatureSupported(QRhi::Compute)) return resourceUpdates;
    if (!o.driveProbe) o.driveProbe = std::make_shared<DriveProbe>();
    DriveProbe &probe = *o.driveProbe;
    // UNA PIPELINE NUOVA DEL MARCHER (equazione, script, rilievi, sezione):
    // la sonda nasce dal suo sorgente, e il campo di prima non vale piu'.
    if (probe.kind != DriveProbe::Implicit || probe.implicitGen != o.implicitPipelineGen) {
        releaseProbePipeline(probe);
        probe.kind = DriveProbe::Implicit;
        probe.implicitGen = o.implicitPipelineGen;
        probe.source = createImplicitFragmentShader(o, 0);
        probe.cutout.clear();
        probe.failed = false;
        probe.key.clear();
        probe.collider = DriveCollider();
        ++probe.serial;
    }
    if (probe.failed) return resourceUpdates;
    if (probe.reading) {
        update();
        return resourceUpdates;
    }

    // IL CUBO attorno alla camera, nello spazio dell'oggetto.
    QVector3D cam;
    float scale = 1.0f;
    if (!driveCameraInModel(o, &cam, &scale)) return resourceUpdates;
    const float half = kFieldHalf * std::max(1.0f, m_driveControls ? m_driveControls->speedScale : 1.0f) / scale;
    const DriveFieldGrid &current = probe.collider.field;
    const QVector3D off = cam - current.center;
    const bool recentre = current.empty() || std::abs(current.half - half) > 1e-4f * half
                          || std::max({ std::abs(off.x()), std::abs(off.y()), std::abs(off.z()) }) > half / 3.0f;
    const QVector3D center = recentre ? cam : current.center;

    // GLI INGRESSI DEL CAMPO, oltre alla pipeline: il cubo, costanti, tempo,
    // rotazioni 4D e quota della sezione, gli orologi dei rilievi, pieno o
    // guscio, il cubo di taglio.
    const std::vector<QVector4D> table{ QVector4D(center, half), QVector4D(float(kFieldNodes), 0.0f, 0.0f, 0.0f) };
    QByteArray key(reinterpret_cast<const char *>(table.data()), int(table.size() * sizeof(QVector4D)));
    const auto add = [&key](const void *field, size_t bytes) {
        key.append(reinterpret_cast<const char *>(field), qsizetype(bytes));
    };
    add(&m_uboData.mathParams, sizeof(QVector4D));
    add(&m_uboData.mathParams2, 3 * sizeof(float));
    add(&m_uboData.time, sizeof(float));
    add(&m_uboData.omega, 3 * sizeof(float));
    add(&m_uboData.dummyZero, sizeof(QVector4D));
    add(&m_uboData.renderMode, sizeof(int));
    add(&m_uboData.x_min, 6 * sizeof(float));   // x_min .. z_max
    if (key == probe.key) return resourceUpdates;

    if (!probe.pipeline && !buildDriveProbePipeline(o, probe)) {
        probe.failed = true;
        return resourceUpdates;
    }
    const quint32 nodes = quint32(kFieldNodes) * quint32(kFieldNodes) * quint32(kFieldNodes);
    if (!ensureStorage(rhi(), probe.parts, probe.partsCapacity, quint32(table.size()))
        || !ensureStorage(rhi(), probe.points, probe.pointsCapacity, (nodes + 3) / 4)) {
        releaseProbePipeline(probe);
        probe.failed = true;
        return resourceUpdates;
    }
    setDriveProbeBindings(o, probe);
    probe.srb->updateResources();

    if (!resourceUpdates) resourceUpdates = rhi()->nextResourceUpdateBatch();
    resourceUpdates->uploadStaticBuffer(probe.parts, 0, quint32(table.size() * sizeof(QVector4D)), table.data());
    cb->beginComputePass(resourceUpdates);
    cb->setComputePipeline(probe.pipeline);
    const QRhiCommandBuffer::DynamicOffset dyn(0, 0);
    cb->setShaderResources(probe.srb, 1, &dyn);
    cb->dispatch(int((nodes + 63) / 64), 1, 1);
    cb->endComputePass();

    QRhiResourceUpdateBatch *rb = rhi()->nextResourceUpdateBatch();
    probe.result = {};
    const int serial = probe.serial;
    probe.result.completed = [this, keep = o.driveProbe, serial] {
        QMetaObject::invokeMethod(this, [keep, serial] {
            keep->result.completed = nullptr;
            finishProbe(*keep, serial);
        }, Qt::QueuedConnection);
    };
    rb->readBackBuffer(probe.points, 0, nodes * 4u, &probe.result);
    cb->resourceUpdate(rb);
    probe.reading = true;
    probe.key = key;
    probe.pendingGrid = DriveFieldGrid();
    probe.pendingGrid.center = center;
    probe.pendingGrid.half = half;
    probe.pendingGrid.n = kFieldNodes;
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

bool GLWidget::driveColliderUsable(const SurfaceObject &o) const
{
    if (!o.driveProbe || o.driveProbe->collider.empty() || !isOnStage(o)) return false;
    // Il collider di un vertex di prima (un'altra superficie caricata a guida
    // spenta) non vale: finche' la sonda non rilegge, niente urti.
    return o.engineMode == ModeImplicit
               ? o.driveProbe->kind == DriveProbe::Implicit && o.driveProbe->implicitGen == o.implicitPipelineGen
               : o.driveProbe->source.constData() == o.driveVertexSource.constData();
}

bool GLWidget::driveCameraInModel(SurfaceObject &o, QVector3D *cam, float *unit)
{
    if (m_hyperbolic) {
        // In H3 il punto p del modello sta in P exp(sigma p) (P la posa
        // dell'oggetto, hyperbolicObjectPlacement; sigma i raggi per unita' del
        // modello): la camera, all'indietro.
        int index = -1;
        for (size_t k = 0; k < m_objects.size(); ++k)
            if (m_objects[k].get() == &o) index = int(k);
        const double sigma = double(o.scale) * m_h3RadiiPerSceneUnit;
        if (index < 0 || sigma < 1e-12) return false;
        const H3::Vec3 v = H3::logAtOrigin(H3::inverse(hyperbolicObjectPlacement(index)) * (m_h3Camera * H3::origin()));
        *cam = QVector3D(float(v.x / sigma), float(v.y / sigma), float(v.z / sigma));
        *unit = o.scale;
        return true;
    }
    bool invertible = false;
    const QMatrix4x4 inv = o.model.inverted(&invertible);
    const float scale = std::min({ o.model.column(0).toVector3D().length(), o.model.column(1).toVector3D().length(),
                                   o.model.column(2).toVector3D().length() });
    if (!invertible || scale < 1e-12f) return false;
    *cam = inv.map(m_view.inverted().column(3).toVector3D());
    *unit = scale;
    return true;
}

std::vector<DrivePlacedCollider> GLWidget::driveColliders() const
{
    std::vector<DrivePlacedCollider> colliders;
    for (const auto &obj : m_objects) {
        const SurfaceObject &o = *obj;
        if (!driveColliderUsable(o)) continue;
        // Le implicite: la camera resta dove il ray marcher le disegna ancora
        // (CameraDrive::kMarcherNear davanti a lei, nello spazio dell'oggetto),
        // piu' il suo raggio.
        float minRadius = 0.0f;
        if (o.engineMode == ModeImplicit) {
            const float scale = std::min({ o.model.column(0).toVector3D().length(),
                                           o.model.column(1).toVector3D().length(),
                                           o.model.column(2).toVector3D().length() });
            minRadius = CameraDrive::kMarcherNear * scale + CameraDrive::kCollisionRadius;
        }
        colliders.push_back({ &o.driveProbe->collider, o.model, minRadius });
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

// =============================================================================
// GLI URTI IN H3
// =============================================================================
// LA CARTA DI KLEIN DELLA CAMERA. Il vertex porta il punto p dell'oggetto in
// P exp(sigma p) e disegna triangoli GEODETICI (sullo schermo la carta di
// Klein, dove le geodetiche sono rette). Nella carta di Klein centrata sulla
// camera quei triangoli sono triangoli piatti, e una palla iperbolica attorno
// alla camera e' una palla vera, di raggio tanh: la distanza dalla camera alla
// superficie e' esatta. Il passo della guida e' corto (centesimi di raggio):
// lungo di lui la carta e' quella del punto di partenza a meno di un soffio.
// Cosi' urto e scivolamento sono quelli euclidei (driveSlide), sui soli
// triangoli vicini alla camera, portati nella carta a ogni passo.

float GLWidget::driveH3Radius() const
{
    // Il raggio della camera in raggi di curvatura; almeno kCollisionRadius
    // raggi, tre volte il near della guida in H3 (0.01 nella carta di Klein).
    return std::max(CameraDrive::kCollisionRadius * float(m_h3RadiiPerSceneUnit), CameraDrive::kCollisionRadius);
}

std::vector<DrivePlacedCollider> GLWidget::driveKleinColliders(const H3::Matrix &camera, float reach,
                                                               std::vector<DriveCollider> &store)
{
    std::vector<DrivePlacedCollider> placed;
    store.clear();
    store.reserve(m_objects.size());
    const float rho = driveH3Radius();
    const H3::Matrix cameraInv = H3::inverse(camera);
    const H3::Vec4 cameraPoint = camera * H3::origin();
    for (size_t index = 0; index < m_objects.size(); ++index) {
        SurfaceObject &o = *m_objects[index];
        if (!driveColliderUsable(o)) continue;
        const double sigma = double(o.scale) * m_h3RadiiPerSceneUnit;
        if (sigma < 1e-12) continue;
        const H3::Matrix place = hyperbolicObjectPlacement(int(index));
        const H3::Matrix toCamera = cameraInv * place;
        // La camera nello spazio del modello: i triangoli entro la sua portata
        // (il logaritmo non allunga le distanze, quindi la scatola li prende tutti).
        const H3::Vec3 vc = H3::logAtOrigin(H3::inverse(place) * cameraPoint);
        const QVector3D pc(float(vc.x / sigma), float(vc.y / sigma), float(vc.z / sigma));
        // Le implicite: la camera resta dove il ray marcher le disegna ancora
        // (kMarcherNear unita' del modello lungo il raggio, piu' il suo raggio).
        const float minR = o.engineMode == ModeImplicit ? CameraDrive::kMarcherNear * float(sigma) + rho : rho;
        const float half = (std::max(reach, minR + reach - rho)) / float(sigma);
        const QVector3D box(half, half, half);
        DriveCollider klein;
        DriveColliderPart part;
        const auto toKlein = [&](const QVector3D &p) {
            const H3::Vec4 x = toCamera * H3::expAtOrigin({ p.x() * sigma, p.y() * sigma, p.z() * sigma });
            return QVector3D(float(x.x / x.w), float(x.y / x.w), float(x.z / x.w));
        };
        o.driveProbe->collider.forEachTriangle(pc - box, pc + box,
                                               [&](const QVector3D &a, const QVector3D &b, const QVector3D &c) {
            const unsigned int first = unsigned(part.points.size());
            part.points.push_back(toKlein(a));
            part.points.push_back(toKlein(b));
            part.points.push_back(toKlein(c));
            part.indices.insert(part.indices.end(), { first, first + 1, first + 2 });
        });
        if (part.indices.empty()) continue;
        part.valid.assign(part.points.size(), 1);
        klein.parts.push_back(std::move(part));
        klein.finalize();
        store.push_back(std::move(klein));
        placed.push_back({ &store.back(), QMatrix4x4(), std::tanh(minR) });
    }
    return placed;
}

QVector3D GLWidget::slideDriveCameraH3(const H3::Matrix &camera, const QVector3D &step, bool *contact)
{
    if (contact) *contact = false;
    const float len = step.length();
    const float rho = driveH3Radius();
    std::vector<DriveCollider> store;
    // Anche fermi: una superficie in moto spinge la camera (drivePushOut, in driveSlide).
    const std::vector<DrivePlacedCollider> placed = driveKleinColliders(camera, rho + len, store);
    if (placed.empty()) return step;
    // Il passo nella carta: dalla camera (l'origine) al punto a distanza len.
    const QVector3D end = len > 1e-9f ? step * (std::tanh(len) / len) : QVector3D();
    const QVector3D k = driveSlide(placed, QVector3D(), end, std::tanh(rho), contact);
    const float kl = k.length();
    if (kl < 1e-9f) return QVector3D();
    return k * (float(std::atanh(std::min(double(kl), 0.999999))) / kl);
}

float GLWidget::driveSurfaceDistanceH3()
{
    // La distanza iperbolica dalla camera alla superficie piu' vicina (entro un
    // raggio di curvatura), in unita' della scena.
    std::vector<DriveCollider> store;
    const std::vector<DrivePlacedCollider> placed = driveKleinColliders(m_h3Camera, 1.0f, store);
    float best = 1e30f;
    for (const DrivePlacedCollider &pc : placed) best = std::min(best, pc.collider->distance(QVector3D()));
    if (best >= 1.0f) return 1e30f;
    return float(std::atanh(double(best)) / m_h3RadiiPerSceneUnit);
}
