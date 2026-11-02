// hyperbolicspace.cpp - Lo spazio iperbolico H3: vedi hyperbolicspace.h.

#include "hyperbolicspace.h"

#include <algorithm>
#include <cmath>

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
