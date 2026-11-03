// hyperbolicspace.cpp - Lo spazio iperbolico H3: vedi hyperbolicspace.h.

#include "hyperbolicspace.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace H3 {

namespace {

double len3(const Vec3 &v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 spatial(const Vec4 &p) { return {p.x, p.y, p.z}; }

Vec4 add(const Vec4 &a, const Vec4 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
Vec4 scaled(const Vec4 &a, double s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }

Vec4 column(const Matrix &a, int c) { return {a.m[0][c], a.m[1][c], a.m[2][c], a.m[3][c]}; }
void setColumn(Matrix &a, int c, const Vec4 &v)
{
    a.m[0][c] = v.x; a.m[1][c] = v.y; a.m[2][c] = v.z; a.m[3][c] = v.w;
}

// Norma di un vettore TANGENTE (<v,v> >= 0 sui tangenti all'iperboloide).
double tangentNorm(const Vec4 &v) { return std::sqrt(std::max(dot(v, v), 0.0)); }

// Componente di b tangente in a, cioe' ortogonale ad a nel prodotto di
// Minkowski: <b + <a,b> a, a> = <a,b> - <a,b> = 0 perche' <a,a> = -1.
Vec4 tangentAt(const Vec4 &a, const Vec4 &b) { return add(b, scaled(a, dot(a, b))); }

} // namespace

Matrix Matrix::identity()
{
    Matrix r;
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0;
    return r;
}

Matrix operator*(const Matrix &a, const Matrix &b)
{
    Matrix r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

Vec4 operator*(const Matrix &a, const Vec4 &p)
{
    const double v[4] = {p.x, p.y, p.z, p.w};
    double r[4];
    for (int i = 0; i < 4; ++i)
        r[i] = a.m[i][0] * v[0] + a.m[i][1] * v[1] + a.m[i][2] * v[2] + a.m[i][3] * v[3];
    return {r[0], r[1], r[2], r[3]};
}

double dot(const Vec4 &a, const Vec4 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z - a.w * b.w;
}

Vec4 origin() { return {0.0, 0.0, 0.0, 1.0}; }

Vec4 expAtOrigin(const Vec3 &v)
{
    const double r = len3(v);
    const double s = r > 0.0 ? std::sinh(r) / r : 1.0;
    return {v.x * s, v.y * s, v.z * s, std::cosh(r)};
}

Vec3 logAtOrigin(const Vec4 &p)
{
    // La distanza da asinh della parte spaziale (= sinh d), non da acosh(w):
    // vicino all'origine acosh perde meta' delle cifre.
    const Vec3 xyz = spatial(p);
    const double s = len3(xyz);
    if (s == 0.0) return {};
    const double k = std::asinh(s) / s;
    return {xyz.x * k, xyz.y * k, xyz.z * k};
}

double distance(const Vec4 &p, const Vec4 &q)
{
    // <p-q,p-q> = 2 cosh d - 2 = 4 sinh^2(d/2): stabile anche per punti vicini,
    // dove acosh(-<p,q>) darebbe zero o rumore.
    const Vec4 d = add(p, scaled(q, -1.0));
    return 2.0 * std::asinh(0.5 * std::sqrt(std::max(dot(d, d), 0.0)));
}

double angleAt(const Vec4 &a, const Vec4 &b, const Vec4 &c)
{
    Vec4 u = tangentAt(a, b);
    Vec4 v = tangentAt(a, c);
    u = scaled(u, 1.0 / tangentNorm(u));
    v = scaled(v, 1.0 / tangentNorm(v));
    // 2 atan2(|u-v|, |u+v|): preciso anche vicino a 0 e a pi, dove acos non lo e'.
    return 2.0 * std::atan2(tangentNorm(add(u, scaled(v, -1.0))), tangentNorm(add(u, v)));
}

Matrix rotation(const Vec3 &axis, double angle)
{
    const double n = len3(axis);
    const double x = axis.x / n, y = axis.y / n, z = axis.z / n;
    const double c = std::cos(angle), s = std::sin(angle), t = 1.0 - c;
    Matrix r = Matrix::identity();
    r.m[0][0] = t * x * x + c;     r.m[0][1] = t * x * y - s * z; r.m[0][2] = t * x * z + s * y;
    r.m[1][0] = t * x * y + s * z; r.m[1][1] = t * y * y + c;     r.m[1][2] = t * y * z - s * x;
    r.m[2][0] = t * x * z - s * y; r.m[2][1] = t * y * z + s * x; r.m[2][2] = t * z * z + c;
    return r;
}

Matrix translation(const Vec3 &v)
{
    return translationTo(expAtOrigin(v));
}

Matrix translationTo(const Vec4 &p)
{
    // La spinta (boost) lungo la geodetica da 0 a p, scritta con le sole
    // coordinate di p: blocco spaziale I + q q^T / (1 + w) (q = parte spaziale),
    // poi q nella quarta colonna e nella quarta riga, w in fondo. Niente
    // funzioni trigonometriche: manda l'origine esattamente in p.
    const double k = 1.0 / (1.0 + p.w);
    const double q[3] = {p.x, p.y, p.z};
    Matrix r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r.m[i][j] = (i == j ? 1.0 : 0.0) + k * q[i] * q[j];
        r.m[i][3] = q[i];
        r.m[3][i] = q[i];
    }
    r.m[3][3] = p.w;
    return r;
}

Matrix inverse(const Matrix &m)
{
    // J M^T J: la trasposta, col segno cambiato dove UNO solo dei due indici
    // e' la quarta coordinata.
    Matrix r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = ((i == 3) != (j == 3) ? -1.0 : 1.0) * m.m[j][i];
    return r;
}

Matrix renormalized(const Matrix &m)
{
    // Colonne di un'isometria: la quarta e' un punto (<c,c> = -1), le altre tre
    // una base ortonormale del suo tangente. Si sottraggono le proiezioni (con
    // <c3,c3> = -1 la proiezione su c3 si somma) e si normalizza.
    Matrix r;
    Vec4 c3 = column(m, 3);
    c3 = scaled(c3, 1.0 / std::sqrt(-dot(c3, c3)));
    setColumn(r, 3, c3);
    Vec4 done[3];
    for (int k = 0; k < 3; ++k) {
        Vec4 c = column(m, k);
        c = add(c, scaled(c3, dot(c, c3)));
        for (int j = 0; j < k; ++j) c = add(c, scaled(done[j], -dot(c, done[j])));
        done[k] = scaled(c, 1.0 / std::sqrt(dot(c, c)));
        setColumn(r, k, done[k]);
    }
    return r;
}

double isometryError(const Matrix &m)
{
    double worst = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const double g = dot(column(m, i), column(m, j));
            const double want = i != j ? 0.0 : (i == 3 ? -1.0 : 1.0);
            worst = std::max(worst, std::abs(g - want));
        }
    return worst;
}

void splitAlongZ(const Matrix &m, Matrix &k, double &distance, Matrix &kp)
{
    // k porta l'asse z sulla direzione di m * origine (rotazione minima;
    // mezzo giro attorno a x se la direzione e' -z), poi kp e' quello che resta:
    // fissa l'origine, quindi e' una rotazione.
    const Vec4 c = m * origin();
    const Vec3 d = spatial(c);
    const double s = len3(d);
    distance = std::asinh(s);
    k = Matrix::identity();
    if (s > 1e-12) {
        const Vec3 u = {d.x / s, d.y / s, d.z / s};
        const Vec3 axis = {-u.y, u.x, 0.0};   // z x u
        const double sa = len3(axis);
        if (sa > 1e-12) k = rotation(axis, std::atan2(sa, u.z));
        else if (u.z < 0.0) k = rotation({1.0, 0.0, 0.0}, std::acos(-1.0));
    }
    kp = inverse(translation({0.0, 0.0, distance})) * inverse(k) * m;
}

double honeycomb435HalfSide()
{
    // Facce x = a e y = a: normali (1,0,0,a) e (0,1,0,a) / sqrt(1 - a^2), angolo
    // diedro interno alpha con cos(alpha) = a^2 / (1 - a^2).
    const double c = std::cos(2.0 * std::acos(-1.0) / 5.0);   // cos 72
    return std::sqrt(c / (1.0 + c));
}

Honeycomb honeycomb435(double maxDistance)
{
    const double a = honeycomb435HalfSide();
    const double s = std::sqrt(1.0 - a * a);
    // Le sei riflessioni nelle facce del cubo centrale: X - 2 <X, n> n, con n
    // la normale unitaria (spaziale) della faccia: il piano x_i / w = +-a.
    Matrix reflections[6];
    for (int f = 0; f < 6; ++f) {
        const int axis = f / 2;
        const double sign = (f % 2 == 0) ? 1.0 : -1.0;
        double n[4] = {0.0, 0.0, 0.0, a / s};
        n[axis] = sign / s;
        const double j[4] = {1.0, 1.0, 1.0, -1.0};   // <n, X> = sum j_k n_k X_k
        Matrix r = Matrix::identity();
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 4; ++k) r.m[i][k] -= 2.0 * n[i] * j[k] * n[k];
        reflections[f] = r;
    }
    // I vertici del cubo centrale, nella carta di Klein (+-a, +-a, +-a), e i
    // suoi dodici spigoli (vertici che differiscono in una coordinata sola).
    Vec4 cubeVertices[8];
    for (int v = 0; v < 8; ++v)
        cubeVertices[v] = fromKlein({(v & 1) ? a : -a, (v & 2) ? a : -a, (v & 4) ? a : -a});

    Honeycomb h;
    // Punti gia' visti (centri delle celle, vertici): si riconoscono per
    // distanza, cercando solo nelle caselle vicine di una griglia sulla carta di
    // Poincare' (lati di 1e-3: i punti distinti distano almeno un lato o mezza
    // diagonale del cubo, piu' di 0.5 raggi). Due liste, due griglie.
    const double same = 1e-6;
    using Key = std::tuple<long, long, long>;
    auto keyOf = [](const Vec4 &p, int dx, int dy, int dz) {
        const Vec3 b = toPoincare(p);
        return Key(std::lround(b.x * 1e3) + dx, std::lround(b.y * 1e3) + dy, std::lround(b.z * 1e3) + dz);
    };
    auto lookup = [&](const std::map<Key, std::vector<int>> &grid, const std::vector<Vec4> &points,
                      const Vec4 &p) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto it = grid.find(keyOf(p, dx, dy, dz));
                    if (it == grid.end()) continue;
                    for (int k : it->second)
                        if (distance(points[k], p) < same) return k;
                }
        return -1;
    };
    // Le celle in ampiezza: la vicina della cella g oltre la sua faccia f e'
    // g * r_f. Si riconosce dal centro (g * origine); la matrice si raddrizza
    // per non accumulare errori lungo le catene di riflessioni.
    std::vector<Vec4> centers;
    std::map<Key, std::vector<int>> centerGrid;
    auto addCell = [&](const Matrix &g) {
        h.cells.push_back(g);
        centers.push_back(g * origin());
        centerGrid[keyOf(centers.back(), 0, 0, 0)].push_back(int(centers.size()) - 1);
    };
    addCell(Matrix::identity());
    for (size_t i = 0; i < h.cells.size(); ++i) {
        for (const Matrix &r : reflections) {
            const Matrix g = renormalized(h.cells[i] * r);
            const Vec4 center = g * origin();
            if (distance(origin(), center) > maxDistance || lookup(centerGrid, centers, center) >= 0) continue;
            addCell(g);
        }
    }
    std::map<Key, std::vector<int>> vertexGrid;
    auto vertexIndex = [&](const Vec4 &p) {
        const int k = lookup(vertexGrid, h.vertices, p);
        if (k >= 0) return k;
        h.vertices.push_back(p);
        vertexGrid[keyOf(p, 0, 0, 0)].push_back(int(h.vertices.size()) - 1);
        return int(h.vertices.size()) - 1;
    };
    std::set<std::pair<int, int>> edges;
    for (const Matrix &g : h.cells) {
        int idx[8];
        for (int v = 0; v < 8; ++v) idx[v] = vertexIndex(g * cubeVertices[v]);
        for (int v = 0; v < 8; ++v)
            for (int bit = 1; bit < 8; bit <<= 1)
                if (!(v & bit)) edges.insert({std::min(idx[v], idx[v | bit]), std::max(idx[v], idx[v | bit])});
    }
    h.edges.assign(edges.begin(), edges.end());
    return h;
}

Vec3 toKlein(const Vec4 &p) { return {p.x / p.w, p.y / p.w, p.z / p.w}; }

Vec4 fromKlein(const Vec3 &k)
{
    const double w = 1.0 / std::sqrt(1.0 - (k.x * k.x + k.y * k.y + k.z * k.z));
    return {k.x * w, k.y * w, k.z * w, w};
}

Vec3 toPoincare(const Vec4 &p)
{
    const double s = 1.0 / (1.0 + p.w);
    return {p.x * s, p.y * s, p.z * s};
}

Vec4 fromPoincare(const Vec3 &b)
{
    const double r2 = b.x * b.x + b.y * b.y + b.z * b.z;
    const double s = 1.0 / (1.0 - r2);
    return {2.0 * b.x * s, 2.0 * b.y * s, 2.0 * b.z * s, (1.0 + r2) * s};
}

} // namespace H3
