// drivecollision.cpp - Gli urti della guida libera: la sfera della camera
// contro i triangoli delle superfici, e lo scivolamento. Vedi drivecollision.h.
#include "drivecollision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// I conti in doppia precisione: le equazioni del contatto con gli spigoli
// sottraggono prodotti di lunghezze al quadrato.
struct V {
    double x = 0.0, y = 0.0, z = 0.0;
};
inline V vec(const QVector3D &a) { return { a.x(), a.y(), a.z() }; }
inline QVector3D qvec(const V &a) { return QVector3D(float(a.x), float(a.y), float(a.z)); }
inline V operator+(const V &a, const V &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V operator-(const V &a, const V &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V operator-(const V &a) { return { -a.x, -a.y, -a.z }; }
inline V operator*(const V &a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline double dot(const V &a, const V &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V cross(const V &a, const V &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline double norm(const V &a) { return std::sqrt(dot(a, a)); }

// Il punto del triangolo abc piu' vicino a p (Ericson, Real-Time Collision
// Detection, 5.1.5): le regioni dei vertici, degli spigoli, poi l'interno.
V closestOnTriangle(const V &p, const V &a, const V &b, const V &c)
{
    const V ab = b - a, ac = c - a, ap = p - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;
    const V bp = p - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));
    const V cp = p - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double denom = 1.0 / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

// Il punto q del piano del triangolo (normale n) cade dentro il triangolo?
bool insideTriangle(const V &q, const V &a, const V &b, const V &c, const V &n)
{
    return dot(cross(b - a, q - a), n) >= 0.0 && dot(cross(c - b, q - b), n) >= 0.0
           && dot(cross(a - c, q - c), n) >= 0.0;
}

// L'ENTRATA: la radice minore di A t^2 + B t + C = 0, se cade in [0, tmax].
// La sfera entra nella zona di contatto alla prima radice ed esce alla seconda;
// l'uscita non e' un urto.
bool entryRoot(double A, double B, double C, double tmax, double *t)
{
    if (std::abs(A) < 1e-30) return false;   // moto nullo, o parallelo allo spigolo: ci pensano i vertici
    const double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return false;
    const double s = std::sqrt(disc);
    const double r1 = (-B - s) / (2.0 * A), r2 = (-B + s) / (2.0 * A);
    const double first = std::min(r1, r2);
    if (first < 0.0 || first > tmax) return false;
    *t = first;
    return true;
}

// IL PRIMO CONTATTO della sfera (centro p, raggio r, moto d) col triangolo abc,
// se viene prima di *best: aggiorna *best e la normale (dalla superficie verso
// il centro). Le due facce del triangolo valgono uguale.
bool sweepTriangle(const V &p, const V &d, double r, const V &a, const V &b, const V &c, double *best, V *normal)
{
    V nrm = cross(b - a, c - a);
    const double area = norm(nrm);
    if (area < 1e-24) return false;   // degenere (un polo): i triangoli accanto hanno gli stessi lati
    nrm = nrm * (1.0 / area);
    const double dd = dot(d, d);

    // GIA' A CONTATTO all'inizio del moto. La distanza da un convesso lungo una
    // retta e' una funzione convessa: se all'inizio non cala, non calera' piu'.
    // Verso il triangolo, l'urto e' subito; via da lui, nessun urto.
    const V q0 = closestOnTriangle(p, a, b, c);
    const V off = p - q0;
    const double dist0 = norm(off);
    if (dist0 < r) {
        const V n0 = dist0 > 1e-12 ? off * (1.0 / dist0) : (dot(p - a, nrm) >= 0.0 ? nrm : -nrm);
        if (dot(d, n0) < -1e-9 * std::sqrt(dd)) {
            *best = 0.0;
            *normal = n0;
            return true;
        }
        return false;
    }

    // LA FACCIA: il primo istante in cui la sfera tocca il piano, se il punto
    // toccato sta dentro il triangolo (e' il primo contatto possibile).
    const double s0 = dot(p - a, nrm);
    const V side = s0 >= 0.0 ? nrm : -nrm;
    const double vd = dot(d, side);
    if (vd < 0.0 && std::abs(s0) >= r) {
        const double t = (std::abs(s0) - r) / -vd;
        if (t <= *best) {
            const V q = p + d * t - side * r;
            if (insideTriangle(q, a, b, c, nrm)) {
                *best = t;
                *normal = side;
                return true;
            }
        }
    }

    // I VERTICI e GLI SPIGOLI (Fauerby, Improved Collision detection and
    // Response, 2003): la sfera contro un punto e contro un segmento.
    bool hit = false;
    const auto vertex = [&](const V &x) {
        const V px = p - x;
        double t = 0.0;
        if (entryRoot(dd, 2.0 * dot(d, px), dot(px, px) - r * r, *best, &t)) {
            *best = t;
            *normal = (p + d * t - x) * (1.0 / r);
            hit = true;
        }
    };
    vertex(a);
    vertex(b);
    vertex(c);
    const auto edge = [&](const V &x0, const V &x1) {
        const V e = x1 - x0, base = x0 - p;
        const double el2 = dot(e, e), ed = dot(e, d), eb = dot(e, base);
        if (el2 < 1e-24) return;
        // A t^2 + B t + C = 0: la distanza dalla retta dello spigolo e' r. Si
        // moltiplica per -1 perche' A sia positivo: le radici non cambiano.
        const double A = -(el2 * -dd + ed * ed);
        const double B = -(el2 * 2.0 * dot(d, base) - 2.0 * ed * eb);
        const double C = -(el2 * (r * r - dot(base, base)) + eb * eb);
        double t = 0.0;
        if (!entryRoot(A, B, C, *best, &t)) return;
        const double f = (ed * t - eb) / el2;   // dove sullo spigolo, 0..1
        if (f < 0.0 || f > 1.0) return;
        const V q = x0 + e * f;
        *best = t;
        *normal = (p + d * t - q) * (1.0 / r);
        hit = true;
    };
    edge(a, b);
    edge(b, c);
    edge(c, a);
    return hit;
}

inline bool overlaps(const QVector3D &alo, const QVector3D &ahi, const QVector3D &blo, const QVector3D &bhi)
{
    return alo.x() <= bhi.x() && ahi.x() >= blo.x() && alo.y() <= bhi.y() && ahi.y() >= blo.y()
           && alo.z() <= bhi.z() && ahi.z() >= blo.z();
}

// Per ogni triangolo di una parte dei blocchi che toccano l'ingombro dato:
// f(a, b, c). Griglia: le due meta' della cella, come
// SurfaceEngine::generateParametricGrid (p0 p1 p2 e p2 p1 p3), senza quelle
// tagliate; lista: i triangoli degli indici.
template <typename F>
void forTriangles(const DriveColliderPart &part, const QVector3D &lo, const QVector3D &hi, F &&f)
{
    if (!overlaps(lo, hi, part.lo, part.hi)) return;
    if (!part.indices.empty()) {
        for (const DriveColliderPart::Tile &tile : part.tiles) {
            if (!overlaps(lo, hi, tile.lo, tile.hi)) continue;
            for (int k = tile.i0; k < tile.i1; ++k) {
                const unsigned int a = part.indices[3 * k], b = part.indices[3 * k + 1], c = part.indices[3 * k + 2];
                const size_t n = part.points.size();
                if (a < n && b < n && c < n && part.valid[a] && part.valid[b] && part.valid[c])
                    f(part.points[a], part.points[b], part.points[c]);
            }
        }
        return;
    }
    const int stride = part.nv + 1;
    const bool cuts = !part.cut.empty();
    for (const DriveColliderPart::Tile &tile : part.tiles) {
        if (!overlaps(lo, hi, tile.lo, tile.hi)) continue;
        for (int i = tile.i0; i < tile.i1; ++i) {
            for (int j = tile.j0; j < tile.j1; ++j) {
                const int p0 = i * stride + j, p1 = (i + 1) * stride + j;
                const int p2 = p0 + 1, p3 = p1 + 1;
                const unsigned char cut = cuts ? part.cut[i * part.nv + j] : 0;
                const bool v1 = part.valid[p1] && part.valid[p2];
                if (v1 && part.valid[p0] && !(cut & 1)) f(part.points[p0], part.points[p1], part.points[p2]);
                if (v1 && part.valid[p3] && !(cut & 2)) f(part.points[p2], part.points[p1], part.points[p3]);
            }
        }
    }
}

inline void grow(QVector3D &lo, QVector3D &hi, const QVector3D &x)
{
    lo = QVector3D(std::min(lo.x(), x.x()), std::min(lo.y(), x.y()), std::min(lo.z(), x.z()));
    hi = QVector3D(std::max(hi.x(), x.x()), std::max(hi.y(), x.y()), std::max(hi.z(), x.z()));
}

// I SEI TETRAEDRI DI UNA CELLA (Kuhn, lungo la diagonale 0-7; vertice c =
// x + 2y + 4z): gli stessi in tutte le celle, quindi le facce delle celle
// vicine si dividono allo stesso modo e i triangoli combaciano.
constexpr int kTets[6][4] = { { 0, 1, 3, 7 }, { 0, 1, 5, 7 }, { 0, 2, 3, 7 },
                              { 0, 2, 6, 7 }, { 0, 4, 5, 7 }, { 0, 4, 6, 7 } };

// Lo zero del campo dentro un tetraedro: un triangolo (un vertice da una
// parte, tre dall'altra) o due (due e due), coi punti dove il campo,
// interpolato lungo gli spigoli, si annulla.
template <typename F>
void polygonizeTet(const QVector3D *p, const float *v, const int *t, F &f)
{
    int in[4], out[4];
    int ni = 0, no = 0;
    for (int a = 0; a < 4; ++a) {
        if (v[t[a]] < 0.0f) in[ni++] = t[a];
        else out[no++] = t[a];
    }
    if (ni == 0 || ni == 4) return;
    const auto zero = [&](int a, int b) { return p[a] + (p[b] - p[a]) * (v[a] / (v[a] - v[b])); };
    if (ni == 1) {
        f(zero(in[0], out[0]), zero(in[0], out[1]), zero(in[0], out[2]));
    } else if (ni == 3) {
        f(zero(out[0], in[0]), zero(out[0], in[1]), zero(out[0], in[2]));
    } else {
        const QVector3D q00 = zero(in[0], out[0]), q01 = zero(in[0], out[1]);
        const QVector3D q11 = zero(in[1], out[1]), q10 = zero(in[1], out[0]);
        f(q00, q01, q11);
        f(q00, q11, q10);
    }
}

// I triangoli dello zero del campo nelle celle della griglia che toccano
// l'ingombro dato. Le celle con un nodo fuori (>= 1e29) non ne hanno.
template <typename F>
void forFieldTriangles(const DriveFieldGrid &g, const QVector3D &lo, const QVector3D &hi, F &&f)
{
    if (g.empty()) return;
    const float h = g.cell();
    const QVector3D base = g.center - QVector3D(g.half, g.half, g.half);
    const auto range = [&](float a, float b, float origin, int &i0, int &i1) {
        i0 = std::max(0, int(std::floor((a - origin) / h)));
        i1 = std::min(g.n - 2, int(std::floor((b - origin) / h)));
    };
    int i0, i1, j0, j1, k0, k1;
    range(lo.x(), hi.x(), base.x(), i0, i1);
    range(lo.y(), hi.y(), base.y(), j0, j1);
    range(lo.z(), hi.z(), base.z(), k0, k1);
    const int n = g.n;
    for (int i = i0; i <= i1; ++i) {
        for (int j = j0; j <= j1; ++j) {
            for (int k = k0; k <= k1; ++k) {
                float v[8];
                int neg = 0;
                bool outside = false;
                for (int c = 0; c < 8; ++c) {
                    const int idx = ((i + (c & 1)) * n + (j + ((c >> 1) & 1))) * n + (k + ((c >> 2) & 1));
                    v[c] = g.values[size_t(idx)];
                    if (!(v[c] < 1e29f)) outside = true;   // anche NaN
                    if (v[c] < 0.0f) ++neg;
                }
                if (outside || neg == 0 || neg == 8) continue;
                QVector3D p[8];
                for (int c = 0; c < 8; ++c)
                    p[c] = base + QVector3D(float(i + (c & 1)), float(j + ((c >> 1) & 1)), float(k + ((c >> 2) & 1))) * h;
                for (const auto &t : kTets) polygonizeTet(p, v, t, f);
            }
        }
    }
}

constexpr float kHuge = std::numeric_limits<float>::max();

// La scala piu' piccola della posa (le colonne del 3x3): la sfera nello spazio
// dell'oggetto non deve essere piu' piccola di quella vera.
float minScale(const QMatrix4x4 &m)
{
    const float sx = m.column(0).toVector3D().length();
    const float sy = m.column(1).toVector3D().length();
    const float sz = m.column(2).toVector3D().length();
    return std::min({ sx, sy, sz });
}
} // namespace

void DriveCollider::finalize()
{
    // Blocchi di 8 x 8 celle; nelle liste di 64 triangoli consecutivi (gli
    // indici della mesh custom vanno per righe della griglia: sono vicini).
    constexpr int kTile = 8;
    constexpr int kListTile = 64;
    const QVector3D farBox(kHuge, kHuge, kHuge);
    lo = farBox;
    hi = -farBox;
    for (DriveColliderPart &part : parts) {
        part.tiles.clear();
        part.lo = farBox;
        part.hi = -farBox;
        if (!part.indices.empty()) {
            const int triangles = int(part.indices.size() / 3);
            for (int t0 = 0; t0 < triangles; t0 += kListTile) {
                DriveColliderPart::Tile tile;
                tile.i0 = t0;
                tile.i1 = std::min(t0 + kListTile, triangles);
                tile.lo = farBox;
                tile.hi = -farBox;
                bool any = false;
                for (int k = 3 * tile.i0; k < 3 * tile.i1; ++k) {
                    const unsigned int v = part.indices[k];
                    if (v >= part.points.size() || !part.valid[v]) continue;
                    grow(tile.lo, tile.hi, part.points[v]);
                    any = true;
                }
                if (!any) continue;
                grow(part.lo, part.hi, tile.lo);
                grow(part.lo, part.hi, tile.hi);
                part.tiles.push_back(tile);
            }
        } else {
            const int stride = part.nv + 1;
            for (int ti = 0; ti < part.nu; ti += kTile) {
                for (int tj = 0; tj < part.nv; tj += kTile) {
                    DriveColliderPart::Tile tile;
                    tile.i0 = ti;
                    tile.i1 = std::min(ti + kTile, part.nu);
                    tile.j0 = tj;
                    tile.j1 = std::min(tj + kTile, part.nv);
                    tile.lo = farBox;
                    tile.hi = -farBox;
                    bool any = false;
                    for (int i = tile.i0; i <= tile.i1; ++i) {
                        for (int j = tile.j0; j <= tile.j1; ++j) {
                            const int k = i * stride + j;
                            if (!part.valid[k]) continue;
                            grow(tile.lo, tile.hi, part.points[k]);
                            any = true;
                        }
                    }
                    if (!any) continue;
                    grow(part.lo, part.hi, tile.lo);
                    grow(part.lo, part.hi, tile.hi);
                    part.tiles.push_back(tile);
                }
            }
        }
        if (part.tiles.empty()) continue;
        grow(lo, hi, part.lo);
        grow(lo, hi, part.hi);
    }
    if (!field.empty()) {
        const QVector3D h(field.half, field.half, field.half);
        grow(lo, hi, field.center - h);
        grow(lo, hi, field.center + h);
    }
}

bool DriveCollider::sweep(const QVector3D &p, const QVector3D &d, float r, float *t, QVector3D *n) const
{
    if (d.lengthSquared() < 1e-24f) return false;
    const QVector3D end = p + d;
    const QVector3D rr(r, r, r);
    const QVector3D slo = QVector3D(std::min(p.x(), end.x()), std::min(p.y(), end.y()), std::min(p.z(), end.z())) - rr;
    const QVector3D shi = QVector3D(std::max(p.x(), end.x()), std::max(p.y(), end.y()), std::max(p.z(), end.z())) + rr;
    if (!overlaps(slo, shi, lo, hi)) return false;
    const V pp = vec(p), dd = vec(d);
    double best = 1.0;
    V normal;
    bool hit = false;
    const auto test = [&](const QVector3D &a, const QVector3D &b, const QVector3D &c) {
        if (sweepTriangle(pp, dd, double(r), vec(a), vec(b), vec(c), &best, &normal)) hit = true;
    };
    for (const DriveColliderPart &part : parts) forTriangles(part, slo, shi, test);
    forFieldTriangles(field, slo, shi, test);
    if (!hit) return false;
    *t = float(best);
    *n = qvec(normal);
    return true;
}

float DriveCollider::distance(const QVector3D &p) const
{
    double best = kHuge;
    const V pp = vec(p);
    const QVector3D farBox(kHuge, kHuge, kHuge);
    const auto test = [&](const QVector3D &a, const QVector3D &b, const QVector3D &c) {
        best = std::min(best, norm(pp - closestOnTriangle(pp, vec(a), vec(b), vec(c))));
    };
    for (const DriveColliderPart &part : parts) forTriangles(part, -farBox, farBox, test);
    // Il campo: le celle entro mezza unita' (le distanze che contano sono quelle vicine).
    const QVector3D nearBox(0.5f, 0.5f, 0.5f);
    forFieldTriangles(field, p - nearBox, p + nearBox, test);
    return float(best);
}

bool DriveCollider::pushOut(QVector3D &p, float r) const
{
    const QVector3D rr(r, r, r);
    const QVector3D lo = p - rr, hi = p + rr;
    if (empty() || !overlaps(lo, hi, this->lo, this->hi)) return false;
    V pp = vec(p);
    bool moved = false;
    const auto test = [&](const QVector3D &a, const QVector3D &b, const QVector3D &c) {
        const V q = closestOnTriangle(pp, vec(a), vec(b), vec(c));
        const V off = pp - q;
        const double d = norm(off);
        // Il centro proprio sulla superficie: da che parte, non si sa.
        if (d >= double(r) || d < 1e-9) return;
        pp = q + off * (double(r) * 1.001 / d);
        moved = true;
    };
    for (const DriveColliderPart &part : parts) forTriangles(part, lo, hi, test);
    forFieldTriangles(field, lo, hi, test);
    if (moved) p = qvec(pp);
    return moved;
}

void DriveCollider::forEachTriangle(const QVector3D &lo, const QVector3D &hi,
                                    const std::function<void(const QVector3D &, const QVector3D &, const QVector3D &)> &f) const
{
    if (empty() || !overlaps(lo, hi, this->lo, this->hi)) return;
    for (const DriveColliderPart &part : parts) forTriangles(part, lo, hi, f);
    forFieldTriangles(field, lo, hi, f);
}

QVector3D drivePushOut(const std::vector<DrivePlacedCollider> &colliders, const QVector3D &from, float radius,
                       bool *pushed)
{
    QVector3D pos = from;
    bool any = false;
    // Qualche giro: uscendo da una superficie si puo' entrare in un'altra vicina (una piega).
    for (int pass = 0; pass < 3; ++pass) {
        bool moved = false;
        for (const DrivePlacedCollider &pc : colliders) {
            if (!pc.collider || pc.collider->empty()) continue;
            bool invertible = false;
            const QMatrix4x4 inv = pc.model.inverted(&invertible);
            const float scale = minScale(pc.model);
            if (!invertible || scale < 1e-12f) continue;
            QVector3D p = inv.map(pos);
            if (!pc.collider->pushOut(p, std::max(radius, pc.minRadius) / scale)) continue;
            pos = pc.model.map(p);
            moved = true;
        }
        if (!moved) break;
        any = true;
    }
    if (pushed) *pushed = any;
    return pos;
}

QVector3D driveSlide(const std::vector<DrivePlacedCollider> &colliders, const QVector3D &from, const QVector3D &move,
                     float radius, bool *contact)
{
    // LE SUPERFICI CHE SI MUOVONO (un toro sul suo path, segnalato dall'utente
    // il 2026-11-26): una parete che avanza entra nella sfera della camera fra
    // un passo e l'altro. Prima del moto la camera torna a un raggio, dalla
    // parte in cui sta: la parete la spinge, anche ferma.
    bool touched = false;
    QVector3D pos = drivePushOut(colliders, from, radius, &touched);
    QVector3D rem = move;
    // Qualche rimbalzo: in una piega o contro uno spigolo il moto che resta
    // dopo il primo contatto ne incontra un altro.
    constexpr int kBounces = 4;
    int bounce = 0;
    for (; bounce < kBounces; ++bounce) {
        const float len = rem.length();
        if (len < 1e-7f) break;
        float bestT = 1.0f;
        QVector3D bestN;
        bool hit = false;
        for (const DrivePlacedCollider &pc : colliders) {
            if (!pc.collider || pc.collider->empty()) continue;
            bool invertible = false;
            const QMatrix4x4 inv = pc.model.inverted(&invertible);
            const float scale = minScale(pc.model);
            if (!invertible || scale < 1e-12f) continue;
            float t = 1.0f;
            QVector3D n;
            const float r = std::max(radius, pc.minRadius);
            if (!pc.collider->sweep(inv.map(pos), inv.mapVector(rem), r / scale, &t, &n) || t > bestT) continue;
            // La normale torna nel mondo con l'inversa trasposta della posa.
            const QVector3D nw = inv.transposed().mapVector(n).normalized();
            if (nw.isNull()) continue;
            bestT = t;
            bestN = nw;
            hit = true;
        }
        if (!hit) {
            pos += rem;
            rem = QVector3D();
            break;
        }
        touched = true;
        // Fino al contatto, un soffio prima: la sfera resta appena staccata.
        const float travel = len * bestT;
        const float skin = 0.01f * radius;
        pos += rem * (std::max(0.0f, travel - skin) / len);
        // Il resto del moto, senza la parte contro la superficie.
        rem *= 1.0f - bestT;
        rem -= bestN * QVector3D::dotProduct(rem, bestN);
    }
    // E dopo: lo scivolamento finisce sempre a un raggio da tutto.
    bool pushedAfter = false;
    pos = drivePushOut(colliders, pos, radius, &pushedAfter);
    if (contact) *contact = touched || pushedAfter;
    return pos;
}

float driveDistance(const std::vector<DrivePlacedCollider> &colliders, const QVector3D &p)
{
    float best = kHuge;
    for (const DrivePlacedCollider &pc : colliders) {
        if (!pc.collider || pc.collider->empty()) continue;
        bool invertible = false;
        const QMatrix4x4 inv = pc.model.inverted(&invertible);
        const float scale = minScale(pc.model);
        if (!invertible || scale < 1e-12f) continue;
        best = std::min(best, pc.collider->distance(inv.map(p)) * scale);
    }
    return best;
}
