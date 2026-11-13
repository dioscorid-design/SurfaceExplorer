// hyperbolictest.cpp - Test del nucleo matematico di H3 (hyperbolicspace.h).
// Programma a se' (niente finestra ne' GPU): lo compila la build dei test
// (build/Tests/hyperbolic-test) e lo lancia ExC/Mac/Test/hyperbolic-test.sh.
// Ogni verifica confronta il nucleo con una formula chiusa della geometria
// iperbolica; le ultime lo legano a cio' che l'app gia' disegna (lo sfondo
// Poincare' Ball) e misurano la deriva della matrice della camera.
// Esce con 0 se tutte le verifiche passano, 1 altrimenti.

#include "hyperbolicspace.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace H3;

namespace {

int g_failures = 0;

std::string format(const char *f, ...)
{
    char buf[512];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buf, sizeof buf, f, args);
    va_end(args);
    return buf;
}

void check(bool ok, const std::string &what)
{
    std::printf("%s%s\n", ok ? "OK      " : "FALLITO ", what.c_str());
    if (!ok) ++g_failures;
}

void info(const std::string &what) { std::printf("        %s\n", what.c_str()); }

const double kPi = 3.14159265358979323846;
double degrees(double r) { return r * 180.0 / kPi; }

std::mt19937 g_rng(20261102);
double uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(g_rng); }

Vec3 mul(const Vec3 &v, double s) { return {v.x * s, v.y * s, v.z * s}; }
Vec4 add4(const Vec4 &a, const Vec4 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
Vec4 scale4(const Vec4 &a, double s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
Vec3 sub(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot3(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double len3(const Vec3 &v) { return std::sqrt(dot3(v, v)); }
Vec3 normalized(const Vec3 &v) { return mul(v, 1.0 / len3(v)); }
Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 randomDir()
{
    std::normal_distribution<double> n(0.0, 1.0);
    return normalized({n(g_rng), n(g_rng), n(g_rng)});
}
Vec3 randomVec(double maxLen) { return mul(randomDir(), uniform(0.0, maxLen)); }

Matrix randomIsometry(double maxDist)
{
    return rotation(randomDir(), uniform(-kPi, kPi)) * translation(randomVec(maxDist))
         * rotation(randomDir(), uniform(-kPi, kPi));
}

double diff(const Matrix &a, const Matrix &b)
{
    double worst = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) worst = std::max(worst, std::abs(a.m[i][j] - b.m[i][j]));
    return worst;
}

// Differenza relativa alla grandezza delle coordinate (crescono come e^d).
double relDiff(const Vec4 &a, const Vec4 &b)
{
    const double d = std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z),
                               std::abs(a.w - b.w)});
    return d / std::max(1.0, std::abs(b.w));
}

double diff3(const Vec3 &a, const Vec3 &b) { return len3(sub(a, b)); }

// Gli angoli di un triangolo nei suoi vertici.
double angleSum(const Vec4 &a, const Vec4 &b, const Vec4 &c)
{
    return angleAt(a, b, c) + angleAt(b, c, a) + angleAt(c, a, b);
}

// La traslazione lungo la geodetica da x a y che porta x in y.
Matrix transvection(const Vec4 &x, const Vec4 &y)
{
    const Matrix tx = translationTo(x);
    return tx * translation(logAtOrigin(inverse(tx) * y)) * inverse(tx);
}

// Angolo di una rotazione attorno all'origine (blocco spaziale di r), da
// atan2(seno, coseno): preciso anche per angoli piccoli.
double rotationAngle(const Matrix &r)
{
    const double c = 0.5 * (r.m[0][0] + r.m[1][1] + r.m[2][2] - 1.0);
    const Vec3 axis = {r.m[2][1] - r.m[1][2], r.m[0][2] - r.m[2][0], r.m[1][0] - r.m[0][1]};
    return std::atan2(0.5 * len3(axis), c);
}

// Addizione di Moebius nella palla di Poincare': la traslazione che porta 0 in p.
Vec3 mobiusAdd(const Vec3 &p, const Vec3 &x)
{
    const double px = dot3(p, x), pp = dot3(p, p), xx = dot3(x, x);
    const double den = 1.0 + 2.0 * px + pp * xx;
    const Vec3 num = {(1.0 + 2.0 * px + xx) * p.x + (1.0 - pp) * x.x,
                      (1.0 + 2.0 * px + xx) * p.y + (1.0 - pp) * x.y,
                      (1.0 + 2.0 * px + xx) * p.z + (1.0 - pp) * x.z};
    return mul(num, 1.0 / den);
}

Vec3 randomInBall(double maxRadius) { return mul(randomDir(), uniform(0.0, maxRadius)); }

Matrix roundedToFloat(Matrix m)
{
    for (auto &row : m.m)
        for (double &v : row) v = double(float(v));
    return m;
}

// ---------------------------------------------------------------------------

void testExpLog()
{
    double surface = 0.0, dist = 0.0, back = 0.0;
    for (double r : {1e-7, 0.3, 2.0, 8.0, 15.0})
        for (int i = 0; i < 20; ++i) {
            const Vec3 v = mul(randomDir(), r);
            const Vec4 p = expAtOrigin(v);
            surface = std::max(surface, std::abs(dot(p, p) + 1.0) / (p.w * p.w));
            dist = std::max(dist, std::abs(distance(origin(), p) - r) / r);
            back = std::max(back, diff3(logAtOrigin(p), v) / r);
        }
    check(surface < 1e-14, format("exp: il punto sta sull'iperboloide (100 punti fino a distanza 15, errore %.1e)", surface));
    check(dist < 1e-9, format("exp: il punto dista |v| dall'origine (errore relativo %.1e)", dist));
    check(back < 1e-12, format("log(exp(v)) = v (errore relativo %.1e)", back));
}

void testIsometries()
{
    double rot = 0.0, tra = 0.0, fixes = 0.0, image = 0.0, undo = 0.0, inv = 0.0, add = 0.0;
    for (int i = 0; i < 50; ++i) {
        const Matrix r = rotation(randomDir(), uniform(-kPi, kPi));
        rot = std::max(rot, isometryError(r));
        fixes = std::max(fixes, relDiff(r * origin(), origin()));

        const Vec3 v = randomVec(5.0);
        const Matrix t = translation(v);
        tra = std::max(tra, isometryError(t));
        image = std::max(image, relDiff(t * origin(), expAtOrigin(v)));
        undo = std::max(undo, diff(t * translation(mul(v, -1.0)), Matrix::identity()));

        const Matrix m = randomIsometry(2.0) * randomIsometry(2.0);
        inv = std::max(inv, diff(inverse(m) * m, Matrix::identity()));

        const Vec3 u = randomDir();
        const double a = uniform(-3.0, 3.0), b = uniform(-3.0, 3.0);
        add = std::max(add, diff(translation(mul(u, a)) * translation(mul(u, b)), translation(mul(u, a + b))));
    }
    check(rot < 1e-14 && fixes < 1e-15, format("rotazioni: isometrie che fissano l'origine (errore %.1e)", rot));
    check(tra < 1e-11, format("traslazioni fino a distanza 5: isometrie (errore %.1e)", tra));
    check(image < 1e-14, format("la traslazione di v porta l'origine in exp(v) (errore %.1e)", image));
    check(undo < 1e-11, format("la traslazione di -v annulla quella di v (errore %.1e)", undo));
    check(inv < 1e-11, format("inversa: J M^T J * M = I (errore %.1e)", inv));
    check(add < 1e-10, format("traslazioni lungo la stessa geodetica si sommano (errore %.1e)", add));
}

void testDistanceInvariance()
{
    double worst = 0.0;
    for (int i = 0; i < 200; ++i) {
        const Vec4 p = expAtOrigin(randomVec(3.0)), q = expAtOrigin(randomVec(3.0));
        const Matrix m = randomIsometry(2.0);
        worst = std::max(worst, std::abs(distance(m * p, m * q) - distance(p, q)));
    }
    check(worst < 1e-9, format("le isometrie conservano le distanze (200 coppie, errore %.1e)", worst));
}

void testCharts()
{
    double klein = 0.0, poincare = 0.0, relation = 0.0;
    for (int i = 0; i < 100; ++i) {
        const Vec4 p = expAtOrigin(randomVec(5.0));
        klein = std::max(klein, relDiff(fromKlein(toKlein(p)), p));
        poincare = std::max(poincare, relDiff(fromPoincare(toPoincare(p)), p));
        const Vec3 b = toPoincare(p);
        relation = std::max(relation, diff3(toKlein(p), mul(b, 2.0 / (1.0 + dot3(b, b)))));
    }
    check(klein < 1e-10 && poincare < 1e-10,
          format("carte di Klein e di Poincare': andata e ritorno (errore %.1e, %.1e)", klein, poincare));
    check(relation < 1e-14, format("Klein = 2 P / (1 + |P|^2) (errore %.1e)", relation));

    // Una geodetica che non passa per l'origine: dritta in Klein, curva in Poincare'.
    double kleinBend = 0.0, poincareBend = 0.0;
    for (int i = 0; i < 20; ++i) {
        const Matrix m = translation(mul(randomDir(), uniform(0.5, 2.0))) * rotation(randomDir(), 1.0);
        Vec3 k[4], b[4];
        const double s[4] = {-2.0, -0.5, 1.0, 3.0};
        for (int j = 0; j < 4; ++j) {
            const Vec4 g = m * expAtOrigin({s[j], 0.0, 0.0});
            k[j] = toKlein(g);
            b[j] = toPoincare(g);
        }
        for (int j = 2; j < 4; ++j) {
            const Vec3 ka = sub(k[1], k[0]), kb = sub(k[j], k[0]);
            const Vec3 ba = sub(b[1], b[0]), bb = sub(b[j], b[0]);
            kleinBend = std::max(kleinBend, len3(cross(ka, kb)) / (len3(ka) * len3(kb)));
            poincareBend = std::max(poincareBend, len3(cross(ba, bb)) / (len3(ba) * len3(bb)));
        }
    }
    check(kleinBend < 1e-12, format("Klein: i punti di una geodetica sono allineati (seno %.1e)", kleinBend));
    info(format("in Poincare' la stessa geodetica e' un arco (seno fino a %.2f)", poincareBend));
}

void testEquilateralTriangles()
{
    // Lato a, angolo alpha con cos(alpha) = cosh a / (1 + cosh a). Si costruisce
    // con due lati e l'angolo in A; il terzo lato e gli altri due angoli devono
    // tornare da soli.
    double side = 0.0, angles = 0.0;
    for (double a : {0.5, 1.0, 2.0, 4.0}) {
        const double alpha = std::acos(std::cosh(a) / (1.0 + std::cosh(a)));
        const Vec4 A = origin();
        const Vec4 B = expAtOrigin({a, 0.0, 0.0});
        const Vec4 C = expAtOrigin({a * std::cos(alpha), a * std::sin(alpha), 0.0});
        side = std::max(side, std::abs(distance(B, C) - a) / a);
        angles = std::max({angles, std::abs(angleAt(B, C, A) - alpha), std::abs(angleAt(C, A, B) - alpha)});
        info(format("triangolo equilatero di lato %.1f: angoli di %.2f gradi, somma %.2f (180 nel piano)",
                    a, degrees(alpha), degrees(angleSum(A, B, C))));
    }
    check(side < 1e-12 && angles < 1e-12,
          format("triangoli equilateri: terzo lato e angoli dalla formula chiusa (errore %.1e, %.1e)", side, angles));
}

void testHolonomy()
{
    // Gauss-Bonnet: chi gira un triangolo geodetico trasportando una direzione
    // torna ruotato di un angolo pari all'area = pi - somma degli angoli. Le
    // traslazioni lungo i lati SONO il trasporto parallelo.
    double fixes = 0.0, angle = 0.0;
    for (int i = 0; i < 20; ++i) {
        const Vec4 A = expAtOrigin(randomVec(1.5)), B = expAtOrigin(randomVec(1.5)), C = expAtOrigin(randomVec(1.5));
        const Matrix loop = transvection(C, A) * transvection(B, C) * transvection(A, B);
        const Matrix atOrigin = inverse(translationTo(A)) * loop * translationTo(A);
        fixes = std::max(fixes, relDiff(loop * A, A));
        const double turned = rotationAngle(atOrigin);
        const double deficit = kPi - angleSum(A, B, C);
        angle = std::max(angle, std::abs(turned - deficit));
        if (i == 0)
            info(format("giro di un triangolo con angoli %.1f + %.1f + %.1f gradi: si torna ruotati di %.2f gradi",
                        degrees(angleAt(A, B, C)), degrees(angleAt(B, C, A)), degrees(angleAt(C, A, B)),
                        degrees(turned)));
    }
    check(fixes < 1e-12, format("il giro A -> B -> C -> A riporta in A (errore %.1e)", fixes));
    check(angle < 1e-10, format("rotazione del giro = 180 gradi - somma degli angoli (20 triangoli, errore %.1e)", angle));
}

void testParallelism()
{
    // La geodetica perpendicolare all'asse x a distanza d, vista dall'origine:
    // le estremita' stanno all'angolo di parallelismo P(d), tan P = 1 / sinh d.
    double worst = 0.0, closest = 0.0;
    for (double d : {0.5, 1.0, 2.0, 3.0}) {
        const Matrix m = translation({d, 0.0, 0.0});
        const Vec4 far = m * expAtOrigin({0.0, 40.0, 0.0});
        const double seen = std::atan2(far.y, far.x);
        const double expected = std::atan(1.0 / std::sinh(d));
        worst = std::max(worst, std::abs(seen - expected));
        closest = std::max(closest, std::abs(distance(origin(), m * origin()) - d));
        info(format("una retta a distanza %.1f copre %.1f gradi del campo visivo (180 nello spazio euclideo)",
                    d, degrees(2.0 * seen)));
    }
    check(worst < 1e-12 && closest < 1e-12,
          format("rette viste dall'origine: estremita' all'angolo di parallelismo (errore %.1e)", worst));
}

void testPoincareBallBackground()
{
    // Lo sfondo Poincare' Ball (bgBallUV in glwidget.cpp) campiona il cielo in
    // normalize(2(1 + p.d) p + (1 - |p|^2) d): l'estremita' della geodetica da p
    // in direzione d, con la traslazione di Moebius 0 -> p. La camera delle scene H3
    // dovra' guidarlo con la stessa matrice: qui le due cose devono coincidere.
    double moebius = 0.0, sky = 0.0;
    for (int i = 0; i < 200; ++i) {
        const Vec3 p = randomInBall(0.9), x = randomInBall(0.9), d = randomDir();
        const Matrix t = translationTo(fromPoincare(p));
        moebius = std::max(moebius, diff3(toPoincare(t * fromPoincare(x)), mobiusAdd(p, x)));
        const Vec3 end = normalized(toPoincare(t * expAtOrigin(mul(d, 30.0))));
        const Vec3 shader = normalized({2.0 * (1.0 + dot3(p, d)) * p.x + (1.0 - dot3(p, p)) * d.x,
                                        2.0 * (1.0 + dot3(p, d)) * p.y + (1.0 - dot3(p, p)) * d.y,
                                        2.0 * (1.0 + dot3(p, d)) * p.z + (1.0 - dot3(p, p)) * d.z});
        sky = std::max(sky, diff3(end, shader));
    }
    check(moebius < 1e-12, format("translationTo = addizione di Moebius nella palla (200 coppie, errore %.1e)", moebius));
    check(sky < 1e-9, format("lo sfondo Poincare' Ball vede le estremita' delle geodetiche del nucleo (errore %.1e)", sky));
}

void testSplitAlongZ()
{
    // m = k * spinta lungo z * kp, anche nei casi limite (centro nell'origine,
    // centro sull'asse -z).
    double rebuild = 0.0, rotations = 0.0, dist = 0.0;
    for (int i = 0; i < 60; ++i) {
        Matrix m = randomIsometry(3.0);
        if (i == 0) m = rotation(randomDir(), 1.0);
        if (i == 1) m = translation({0.0, 0.0, -2.0}) * rotation(randomDir(), 0.5);
        Matrix k, kp;
        double r = 0.0;
        splitAlongZ(m, k, r, kp);
        rebuild = std::max(rebuild, diff(k * translation({0.0, 0.0, r}) * kp, m));
        rotations = std::max({rotations, relDiff(k * origin(), origin()), relDiff(kp * origin(), origin()),
                              isometryError(k), isometryError(kp)});
        dist = std::max(dist, std::abs(r - distance(origin(), m * origin())));
    }
    check(rebuild < 1e-10 && rotations < 1e-12 && dist < 1e-12,
          format("scomposizione rotazione * spinta lungo z * rotazione (errore %.1e, %.1e, %.1e)", rebuild, rotations, dist));
}

// IL CALCOLO DELLO SHADER IN FLOAT, nei due modi. Un punto lontano r dal centro
// dell'oggetto ma vicino alla camera (0.05) nasce dalla differenza di numeri
// grandi come e^r: col prodotto diretto della matrice (com'era) in float si
// perde quasi tutto gia' a r = 5; con la spinta scritta in sinh(R - r) e
// cosh(R - r) (surface.vert, hyperbolicPoint) resta l'errore della direzione
// dal centro, 1e-7 * sinh(r). Restituisce l'errore relativo alla distanza dalla camera.
struct FloatErrors { double direct = 0.0, split = 0.0; };
FloatErrors shaderFloatErrors(double r)
{
    FloatErrors e;
    const double gap = 0.05;
    for (int i = 0; i < 200; ++i) {
        // Direzione qualunque dal centro; la camera sta sul suo prolungamento, a gap dal punto.
        const Vec3 dir = randomDir();
        const Vec3 jit = mul(randomDir(), 1e-3);
        const Vec3 v = mul(normalized({dir.x + jit.x, dir.y + jit.y, dir.z + jit.z}), r);
        const Matrix m = inverse(translation(mul(dir, r + gap)));   // centro -> camera
        const Vec4 exact = m * expAtOrigin(v);
        const Vec3 ek = toKlein(exact);

        float mf[4][4];
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) mf[a][b] = float(m.m[a][b]);
        const float vf[3] = {float(v.x), float(v.y), float(v.z)};
        const float rf = std::sqrt(vf[0] * vf[0] + vf[1] * vf[1] + vf[2] * vf[2]);
        const float sf = std::sinh(rf) / rf;
        const float x[4] = {vf[0] * sf, vf[1] * sf, vf[2] * sf, std::cosh(rf)};
        float y[4];
        for (int a = 0; a < 4; ++a) y[a] = mf[a][0] * x[0] + mf[a][1] * x[1] + mf[a][2] * x[2] + mf[a][3] * x[3];
        e.direct = std::max(e.direct, diff3({y[0] / y[3], y[1] / y[3], y[2] / y[3]}, ek) / gap);

        Matrix k, kp;
        double dist = 0.0;
        splitAlongZ(m, k, dist, kp);
        float wv[3];
        for (int a = 0; a < 3; ++a) wv[a] = float(kp.m[a][0]) * vf[0] + float(kp.m[a][1]) * vf[1] + float(kp.m[a][2]) * vf[2];
        const float w[3] = {wv[0] / rf, wv[1] / rf, wv[2] / rf};
        const float onePlusZ = w[2] < 0.0f ? (w[0] * w[0] + w[1] * w[1]) / (1.0f - w[2]) : 1.0f + w[2];
        const float R = float(dist), sr = std::sinh(rf);
        const float z[4] = {sr * w[0], sr * w[1], std::sinh(R - rf) + onePlusZ * std::cosh(R) * sr,
                            std::cosh(R - rf) + onePlusZ * std::sinh(R) * sr};
        float q[3];
        for (int a = 0; a < 3; ++a) q[a] = float(k.m[a][0]) * z[0] + float(k.m[a][1]) * z[1] + float(k.m[a][2]) * z[2];
        e.split = std::max(e.split, diff3({q[0] / z[3], q[1] / z[3], q[2] / z[3]}, ek) / gap);
    }
    return e;
}

void testShaderPrecision()
{
    for (double r : {2.0, 4.0, 6.0, 8.0}) {
        const FloatErrors e = shaderFloatErrors(r);
        info(format("oggetto grande %.0f raggi, camera a 0.05 dalla superficie: errore %.1e col prodotto diretto, %.1e con la spinta in sinh(R - r)",
                    r, e.direct, e.split));
    }
    const FloatErrors at6 = shaderFloatErrors(6.0);
    check(at6.split < 1e-3 && at6.direct > 1e-2,
          format("in float, a 6 raggi dal centro dell'oggetto: la spinta in sinh(R - r) resta sotto 1e-3 (%.1e), il prodotto diretto no (%.1e)",
                 at6.split, at6.direct));
}

void testLightCosines()
{
    // Le due formule della luce del fragment shader (surface.frag, HYPERBOLIC),
    // N tangente in P:
    //   prospettiva, camera nell'origine: coseno con la geodetica verso la
    //     camera = -N.w / |P.xyz|; qui contro la tangente di quella geodetica
    //     costruita col nucleo;
    //   ortogonale, la vista da fuori: luce euclidea sulla palla di Poincare'
    //     (centrata nell'origine), con la normale del modello
    //     N.xyz - P.xyz N.w / (1 + P.w). Dev'essere perpendicolare
    //     all'immagine x / (1 + w) di ogni tangente della superficie (ogni
    //     vettore tangente perpendicolare a N): qui con le differenze finite
    //     lungo le geodetiche.
    double persp = 0.0, outside = 0.0;
    const auto poincare = [](const Vec4 &q) { return Vec3{q.x / (1.0 + q.w), q.y / (1.0 + q.w), q.z / (1.0 + q.w)}; };
    for (int i = 0; i < 200; ++i) {
        const Vec4 p = expAtOrigin(randomVec(4.0));
        const Vec4 a = {uniform(-1, 1), uniform(-1, 1), uniform(-1, 1), uniform(-1, 1)};
        const Vec4 n = add4(a, scale4(p, dot(a, p)));   // tangente in p
        const double nn = std::sqrt(dot(n, n));
        // Verso la camera: la tangente in p della geodetica che va nell'origine.
        const Vec4 toEye = add4(origin(), scale4(p, dot(origin(), p)));
        const double refP = dot(n, toEye) / (nn * std::sqrt(dot(toEye, toEye)));
        const double lenXYZ = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        persp = std::max(persp, std::abs(-n.w / (nn * lenXYZ) - refP));
        // Vista da fuori: due tangenti della superficie, a caso.
        const Vec3 nm = sub({n.x, n.y, n.z}, mul({p.x, p.y, p.z}, n.w / (1.0 + p.w)));
        for (int j = 0; j < 2; ++j) {
            const Vec4 b = {uniform(-1, 1), uniform(-1, 1), uniform(-1, 1), uniform(-1, 1)};
            Vec4 t = add4(b, scale4(p, dot(b, p)));
            t = add4(t, scale4(n, -dot(t, n) / dot(n, n)));
            const double tl = std::sqrt(dot(t, t));
            constexpr double eps = 1e-5;
            const Vec3 ahead = poincare(add4(scale4(p, std::cosh(eps)), scale4(t, std::sinh(eps) / tl)));
            const Vec3 behind = poincare(add4(scale4(p, std::cosh(eps)), scale4(t, -std::sinh(eps) / tl)));
            const Vec3 dt = sub(ahead, behind);
            outside = std::max(outside, std::abs(dot3(nm, dt)) / (len3(nm) * len3(dt)));
        }
    }
    check(persp < 1e-9 && outside < 1e-6,
          format("luce in H3: in prospettiva il coseno del fragment shader coincide con la geodetica (errore %.1e); "
                 "nella vista da fuori la normale del modello e' perpendicolare alla superficie nella palla "
                 "(coseno %.1e)", persp, outside));
}

// ---------------------------------------------------------------------------
// IL RAY MARCHING IN H3 (raymarch_template.txt, ramo HYPERBOLIC): le sue
// formule trascritte in float, come le esegue la GPU, contro il nucleo in
// double. La posa e' quella delle mesh (GLWidget::hyperbolicObjectMatrices):
// il punto p del modello va nel riferimento della camera (o della palla nella
// vista da fuori) come k * spinta di R * exp(M p), M = kp * rotazione * sigma.

struct F3 { float x = 0, y = 0, z = 0; };
Vec3 add3(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
F3 f3(const Vec3 &v) { return {float(v.x), float(v.y), float(v.z)}; }
Vec3 d3(const F3 &v) { return {v.x, v.y, v.z}; }
F3 fadd(F3 a, F3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
F3 fsub(F3 a, F3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
F3 fmul(F3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float fdot(F3 a, F3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float flen(F3 a) { return std::sqrt(fdot(a, a)); }
F3 fnorm(F3 a) { return fmul(a, 1.0f / flen(a)); }
F3 fcross(F3 a, F3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Matrice 3x3 in float, m[riga][colonna].
struct F33 { float m[3][3] = {}; };
F3 fapply(const F33 &a, F3 v)
{
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
            a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}
F3 fapplyT(const F33 &a, F3 v)
{
    return {a.m[0][0] * v.x + a.m[1][0] * v.y + a.m[2][0] * v.z,
            a.m[0][1] * v.x + a.m[1][1] * v.y + a.m[2][1] * v.z,
            a.m[0][2] * v.x + a.m[1][2] * v.y + a.m[2][2] * v.z};
}
F3 fcol(const F33 &a, int c) { return {a.m[0][c], a.m[1][c], a.m[2][c]}; }

// Cio' che lo shader riceve: M (u_mMatrix), k (u_mvMatrix), R e l'inversa di M.
struct RmPose {
    F33 M, K, Minv;
    float R = 0.0f;
    double sigma = 0.0;
    Matrix m;             // il centro dell'oggetto nel riferimento di chi guarda (double)
    double rot[3][3] = {};
};

RmPose rmPose(const Matrix &m, const Matrix &rot, double sigma)
{
    RmPose p;
    p.m = m;
    p.sigma = sigma;
    Matrix k, kp;
    double dist = 0.0;
    splitAlongZ(m, k, dist, kp);
    double M[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            p.rot[i][j] = rot.m[i][j];
            double s = 0.0;
            for (int l = 0; l < 3; ++l) s += kp.m[i][l] * rot.m[l][j];
            M[i][j] = s * sigma;
            p.K.m[i][j] = float(k.m[i][j]);
        }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            p.M.m[i][j] = float(M[i][j]);
            p.Minv.m[i][j] = float(M[j][i] / (sigma * sigma));   // M = sigma * rotazione
        }
    p.R = float(dist);
    return p;
}

// Il punto del modello nel riferimento di chi guarda, in double.
Vec4 rmExact(const RmPose &pose, const Vec3 &p)
{
    Vec3 v;
    v.x = pose.sigma * (pose.rot[0][0] * p.x + pose.rot[0][1] * p.y + pose.rot[0][2] * p.z);
    v.y = pose.sigma * (pose.rot[1][0] * p.x + pose.rot[1][1] * p.y + pose.rot[1][2] * p.z);
    v.z = pose.sigma * (pose.rot[2][0] * p.x + pose.rot[2][1] * p.y + pose.rot[2][2] * p.z);
    return pose.m * expAtOrigin(v);
}

float rmLogLength(F3 y, F3 &dir)
{
    const float m = std::max(std::abs(y.x), std::max(std::abs(y.y), std::abs(y.z)));
    if (m < 1.0e-30f) { dir = {0.0f, 0.0f, 1.0f}; return 0.0f; }
    const F3 yn = fmul(y, 1.0f / m);
    const float ln = flen(yn);
    dir = fmul(yn, 1.0f / ln);
    return std::log(m) + std::log(ln + std::sqrt(ln * ln + 1.0f / (m * m)));
}

// h3RayAt in prospettiva: il raggio di direzione d (unitaria, nella camera) a
// distanza s dalla camera, nel modello.
F3 rmRayPerspective(const RmPose &pose, F3 d, float s)
{
    const F3 dk = fapplyT(pose.K, d);
    const float lateral = dk.x * dk.x + dk.y * dk.y;
    const float oneMinusDz = dk.z > 0.0f ? lateral / (1.0f + dk.z) : 1.0f - dk.z;
    const float sh = std::sinh(s);
    const F3 y = {sh * dk.x, sh * dk.y, std::sinh(s - pose.R) - oneMinusDz * sh * std::cosh(pose.R)};
    F3 dir;
    const float rho = rmLogLength(y, dir);
    return fapply(pose.Minv, fmul(dir, rho));
}

// h3RayAt nella vista da fuori: la retta (bx, by, -u) della palla, a lunghezza
// iperbolica a dal punto di mezzo.
F3 rmRayOutside(const RmPose &pose, float bx, float by, float a)
{
    const float c = std::sqrt(1.0f - bx * bx - by * by);
    const F3 base = fapplyT(pose.K, {bx, by, 0.0f});
    const F3 dirZ = fapplyT(pose.K, {0.0f, 0.0f, 1.0f});
    const float tanhHalfR = std::tanh(0.5f * pose.R);
    const float chh = std::cosh(0.5f * pose.R);
    const float sech2HalfR = 1.0f / (chh * chh);
    const float h = 0.5f * c * a;
    const float u = c * std::tanh(h);
    const float ch = std::cosh(h);
    const float oneMinusB2 = c * c / (ch * ch);
    const F3 centre = {0.0f, 0.0f, tanhHalfR};
    const F3 dl = fsub(fsub(base, fmul(dirZ, u)), centre);
    const float dl2 = fdot(dl, dl);
    const F3 x = fmul(fsub(fmul(dl, sech2HalfR), fmul(centre, dl2)),
                      1.0f / std::max(dl2 + oneMinusB2 * sech2HalfR, 1.0e-30f));
    const float lx = flen(x);
    const F3 dir = lx > 1.0e-30f ? fmul(x, 1.0f / lx) : F3{0.0f, 0.0f, 1.0f};
    const float rho = 2.0f * std::atanh(std::min(lx, 0.9999999f));
    return fapply(pose.Minv, fmul(dir, rho));
}

// h3ShadingVectors: vista e luce negli assi del modello, nel punto p di normale n.
void rmShading(const RmPose &pose, bool outside, F3 p, F3 n, F3 light, F3 &viewM, F3 &lightM)
{
    const F33 &M = pose.M, &K = pose.K;
    const float R = pose.R, chR = std::cosh(R), shR = std::sinh(R);
    const F3 v = fapply(M, p);
    const float r = flen(v);
    const F3 w = r > 1.0e-6f ? fmul(v, 1.0f / r) : F3{0.0f, 0.0f, 1.0f};
    const float ez = w.z < 0.0f ? (w.x * w.x + w.y * w.y) / (1.0f - w.z) : 1.0f + w.z;
    const float sr = std::sinh(r), cr = std::cosh(r), cRr = std::cosh(R - r), sRr = std::sinh(R - r);
    const F3 Pxyz = fapply(K, {sr * w.x, sr * w.y, sRr + ez * chR * sr});
    const float Pw = cRr + ez * shR * sr;
    const F3 radXyz = fapply(K, {cr * w.x, cr * w.y, ez * chR * cr - cRr});
    const float radW = ez * shR * cr - sRr;
    const float lp = flen(Pxyz);
    const F3 dHat = lp > 1.0e-12f ? fmul(Pxyz, 1.0f / lp) : F3{0.0f, 0.0f, -1.0f};
    F3 cols[3];
    for (int i = 0; i < 3; ++i) {
        const F3 u = fnorm(fcol(M, i));
        const float a = fdot(u, w);
        const F3 up = fsub(u, fmul(w, a));
        const F3 Xxyz = fadd(fmul(radXyz, a), fapply(K, {up.x, up.y, chR * up.z}));
        const float Xw = a * radW + shR * up.z;
        cols[i] = outside ? fnorm(fsub(Xxyz, fmul(Pxyz, Xw / (1.0f + Pw))))
                          : fsub(Xxyz, fmul(dHat, fdot(Xxyz, dHat) * (1.0f - 1.0f / Pw)));
    }
    const F3 nl = fnorm(fapply(M, n));
    const float nr = fdot(nl, w);
    const F3 nt = fsub(nl, fmul(w, nr));
    const float k = r > 1.0e-6f ? r / sr : 1.0f;
    const F3 nTrue = fnorm(fapplyT(M, fadd(fmul(w, nr), fmul(nt, k))));
    const F3 axis = fcross(nTrue, n);
    const float c = fdot(nTrue, n);
    const auto rotateTo = [&](F3 x) {
        return fadd(fadd(fmul(x, c), fcross(axis, x)), fmul(axis, fdot(axis, x) / (1.0f + c)));
    };
    const auto toModel = [&](F3 x) { return F3{fdot(cols[0], x), fdot(cols[1], x), fdot(cols[2], x)}; };
    const F3 viewV = outside ? F3{0.0f, 0.0f, 1.0f} : fmul(dHat, -1.0f);
    viewM = rotateTo(toModel(viewV));
    lightM = rotateTo(toModel(light));
}

// Una posa a caso: camera entro 4 raggi dal centro della scena, oggetto entro 2,
// rotazione qualunque, sigma fra 0.2 e 1 raggi per unita' del modello.
struct RmScene { RmPose perspective, outside; Matrix camera, place; double sigma; };
RmScene randomRmScene()
{
    RmScene s;
    s.camera = translation(randomVec(4.0)) * rotation(randomDir(), uniform(-kPi, kPi));
    s.place = translation(randomVec(2.0));
    const Matrix rot = rotation(randomDir(), uniform(-kPi, kPi));
    s.sigma = uniform(0.2, 1.0);
    s.perspective = rmPose(inverse(s.camera) * s.place, rot, s.sigma);
    // La vista da fuori guarda dal centro della scena con l'orientamento della camera.
    const Matrix viewer = inverse(translationTo(s.camera * origin())) * s.camera;
    s.outside = rmPose(inverse(viewer) * s.place, rot, s.sigma);
    return s;
}

void testRayMarching()
{
    // 1. L'INVERSA DELLA POSA: un punto del modello mandato nella camera col
    // nucleo, poi riportato nel modello da h3RayAt lungo il suo raggio. Errore
    // in unita' del modello, punti entro 2 unita' dal centro dell'oggetto.
    double perspErr = 0.0, outsideErr = 0.0;
    for (int i = 0; i < 400; ++i) {
        const RmScene sc = randomRmScene();
        const Vec3 p = randomVec(2.0);
        const Vec4 q = rmExact(sc.perspective, p);
        const Vec3 qx = {q.x, q.y, q.z};
        const double s = std::asinh(len3(qx));
        if (s < 0.05) continue;   // sul punto della camera la direzione non c'e'
        perspErr = std::max(perspErr, diff3(d3(rmRayPerspective(sc.perspective, f3(normalized(qx)), float(s))), p));
        // Vista da fuori: il punto nella palla, la sua retta e la lunghezza dal punto di mezzo.
        const Vec3 b = toPoincare(rmExact(sc.outside, p));
        const double c = std::sqrt(1.0 - b.x * b.x - b.y * b.y);
        const double a = 2.0 / c * std::atanh(-b.z / c);
        outsideErr = std::max(outsideErr, diff3(d3(rmRayOutside(sc.outside, float(b.x), float(b.y), float(a))), p));
    }
    check(perspErr < 1e-3 && outsideErr < 1e-3,
          format("ray marching in H3, in float: il raggio riporta il punto colpito nel modello (prospettiva %.1e, "
                 "vista da fuori %.1e unita' del modello)", perspErr, outsideErr));

    // 2. LA LUNGHEZZA LUNGO LA RETTA DELLA PALLA: u = c tanh(c a / 2) e' la
    // retta percorsa a lunghezza iperbolica a (metrica 2 |du| / (1 - |b|^2)).
    double arcErr = 0.0;
    for (int i = 0; i < 50; ++i) {
        const double bx = uniform(-0.6, 0.6), by = uniform(-0.6, 0.6);
        const double c = std::sqrt(1.0 - bx * bx - by * by);
        const double a = uniform(-6.0, 6.0);
        const double uEnd = c * std::tanh(0.5 * c * a);
        // Simpson sulla metrica lungo la retta.
        const int steps = 4000;
        const auto metric = [c](double u) { return 2.0 / (c * c - u * u); };
        double length = metric(0.0) + metric(uEnd);
        for (int j = 1; j < steps; ++j) length += (j % 2 ? 4.0 : 2.0) * metric(uEnd * j / steps);
        length *= uEnd / (3.0 * steps);
        arcErr = std::max(arcErr, std::abs(length - a));
    }
    check(arcErr < 1e-6, format("vista da fuori: la retta del pixel nella palla e' percorsa a lunghezza iperbolica "
                                "(errore %.1e raggi)", arcErr));

    // 3. IL LOGARITMO NON ALLUNGA: due punti del raggio a distanza iperbolica ds
    // stanno nel modello a meno di ds / sigma (i passi del marcher sono sicuri).
    double worstRatio = 0.0;
    for (int i = 0; i < 300; ++i) {
        const RmScene sc = randomRmScene();
        const F3 d = f3(randomDir());
        const float s = float(uniform(0.1, 8.0)), ds = float(uniform(0.01, 0.5));
        const double gap = diff3(d3(rmRayPerspective(sc.perspective, d, s + ds)), d3(rmRayPerspective(sc.perspective, d, s)));
        worstRatio = std::max(worstRatio, gap * sc.sigma / ds);
    }
    check(worstRatio <= 1.0 + 1e-3,
          format("i passi del marcher: lungo il raggio il punto del modello si sposta al piu' di t "
                 "(rapporto massimo %.6f)", worstRatio));

    // 4. LA SFERA DELL'OGGETTO (h3SetupRay): in prospettiva l'intervallo di s in
    // cui il raggio sta entro rho dal centro, agli estremi distanza rho; nella
    // vista da fuori l'immagine della sfera nella palla, una sfera euclidea.
    double perspBound = 0.0, outsideBound = 0.0;
    int perspHits = 0, outsideHits = 0;
    for (int i = 0; i < 400; ++i) {
        const RmScene sc = randomRmScene();
        const double rho = uniform(0.2, 2.0);
        const Vec4 centre = sc.perspective.m * origin();
        // Un raggio verso un punto vicino al centro, cosi' che spesso la incontri.
        const Vec3 towards = logAtOrigin(translationTo(centre) * expAtOrigin(randomVec(2.0 * rho)));
        if (len3(towards) < 1e-6) continue;
        const Vec3 d = normalized(towards);
        {
            const RmPose &pose = sc.perspective;
            const F3 dk = fapplyT(pose.K, f3(d));
            const float lateral = dk.x * dk.x + dk.y * dk.y;
            const float oneMinusDz = dk.z > 0.0f ? lateral / (1.0f + dk.z) : 1.0f - dk.z;
            const float onePlusDz = dk.z < 0.0f ? lateral / (1.0f - dk.z) : 1.0f + dk.z;
            const float eR = std::exp(-pose.R), shR = std::sinh(pose.R);
            const float aMinusB = eR + shR * oneMinusDz, aPlusB = eR + shR * onePlusDz;
            const float q = std::cosh(float(rho)) / std::sqrt(aMinusB * aPlusB);
            if (q > 1.0f) {
                const float s0 = 0.5f * std::log(aPlusB / aMinusB), h = std::acosh(q);
                for (float s : {s0 - h, s0 + h}) {
                    const Vec4 at = add4(scale4(origin(), std::cosh(double(s))), scale4({d.x, d.y, d.z, 0.0}, std::sinh(double(s))));
                    perspBound = std::max(perspBound, std::abs(distance(at, centre) - rho));
                }
                ++perspHits;
            }
        }
        {
            const RmPose &pose = sc.outside;
            const Vec4 centreOut = pose.m * origin();
            const Vec3 bc = toPoincare(centreOut);
            // Un pixel attorno all'immagine del centro.
            const float bx = float(bc.x + uniform(-0.3, 0.3) * (1.0 - dot3(bc, bc)));
            const float by = float(bc.y + uniform(-0.3, 0.3) * (1.0 - dot3(bc, bc)));
            const float lateral = bx * bx + by * by;
            if (lateral >= 0.999f) continue;
            const float R = pose.R;
            const float z1 = std::tanh(0.5f * (R - float(rho))), z2 = std::tanh(0.5f * (R + float(rho)));
            const F3 q = fsub({bx, by, 0.0f}, fmul(fcol(pose.K, 2), 0.5f * (z1 + z2)));
            const float rb = 0.5f * (z2 - z1);
            const float disc = q.z * q.z - fdot(q, q) + rb * rb;
            if (disc > 0.0f) {
                for (float u : {q.z - std::sqrt(disc), q.z + std::sqrt(disc)}) {
                    const Vec4 at = fromPoincare({bx, by, -double(u)});
                    outsideBound = std::max(outsideBound, std::abs(distance(at, centreOut) - rho));
                }
                ++outsideHits;
            }
        }
    }
    check(perspBound < 1e-3 && outsideBound < 1e-3 && perspHits > 100 && outsideHits > 100,
          format("la sfera dell'oggetto lungo il raggio: agli estremi dell'intervallo la distanza dal centro e' "
                 "il raggio della sfera (prospettiva %.1e su %d raggi, vista da fuori %.1e su %d)",
                 perspBound, perspHits, outsideBound, outsideHits));

    // 5. LA LUCE: il prodotto con n_model (il gradiente nel modello) dei vettori
    // di h3ShadingVectors e' il coseno vero di H3 fra la normale della
    // superficie e la luce della camera trasportata lungo il raggio (in
    // prospettiva), o la luce euclidea sulla palla (vista da fuori). La normale
    // vera: perpendicolare alle immagini delle tangenti, con le differenze finite.
    double perspLight = 0.0, outsideLight = 0.0;
    const Vec3 lightCam = normalized({1.0, 1.5, 1.0});
    for (int i = 0; i < 300; ++i) {
        const RmScene sc = randomRmScene();
        const Vec3 p = randomVec(2.0);
        const Vec3 n = randomDir();
        Vec3 t1 = cross(n, randomDir());
        t1 = normalized(t1);
        const Vec3 t2 = cross(n, t1);
        for (int view = 0; view < 2; ++view) {
            const RmPose &pose = view == 0 ? sc.perspective : sc.outside;
            const Vec4 Q = rmExact(pose, p);
            constexpr double eps = 1e-5;
            const auto image = [&](const Vec3 &t) {
                return scale4(add4(rmExact(pose, add3(p, mul(t, eps))), scale4(rmExact(pose, sub(p, mul(t, eps))), -1.0)),
                              0.5 / eps);
            };
            const Vec4 a1 = image(t1), a2 = image(t2), an = image(n);
            // La normale vera: an reso tangente in Q e perpendicolare a a1, a2 (Gram-Schmidt di Minkowski).
            Vec4 N = add4(an, scale4(Q, dot(an, Q)));
            const Vec4 b1 = scale4(a1, 1.0 / std::sqrt(dot(a1, a1)));
            Vec4 b2 = add4(a2, scale4(b1, -dot(a2, b1)));
            b2 = scale4(b2, 1.0 / std::sqrt(dot(b2, b2)));
            N = add4(N, scale4(b1, -dot(N, b1)));
            N = add4(N, scale4(b2, -dot(N, b2)));
            N = scale4(N, 1.0 / std::sqrt(dot(N, N)));
            double cosLight, cosView;
            const Vec3 qx = {Q.x, Q.y, Q.z};
            if (view == 0) {
                const double sh = len3(qx), ch = Q.w;
                const Vec3 d = mul(qx, 1.0 / sh);
                const double ld = dot3(lightCam, d);
                const Vec3 lperp = sub(lightCam, mul(d, ld));
                const Vec4 L = {ld * ch * d.x + lperp.x, ld * ch * d.y + lperp.y, ld * ch * d.z + lperp.z, ld * sh};
                cosLight = dot(N, L);
                cosView = -dot(N, {ch * d.x, ch * d.y, ch * d.z, sh});
            } else {
                const Vec3 nm = normalized(sub({N.x, N.y, N.z}, mul(qx, N.w / (1.0 + Q.w))));
                cosLight = dot3(nm, lightCam);
                cosView = nm.z;
            }
            F3 viewM, lightM;
            rmShading(pose, view == 1, f3(p), f3(n), f3(lightCam), viewM, lightM);
            const double err = std::max(std::abs(fdot(f3(n), lightM) - cosLight), std::abs(fdot(f3(n), viewM) - cosView));
            (view == 0 ? perspLight : outsideLight) = std::max(view == 0 ? perspLight : outsideLight, err);
        }
    }
    check(perspLight < 1e-3 && outsideLight < 1e-3,
          format("luce del ray marching in H3: i coseni con n_model sono quelli veri (prospettiva %.1e, "
                 "vista da fuori %.1e)", perspLight, outsideLight));
}

void testHoneycomb()
{
    // Il cubo centrale: angolo diedro di 72 gradi (fra le facce x = a e y = a,
    // nel punto (a, a, 0) dello spigolo: le direzioni verso (a, 0, 0) e (0, a, 0)
    // stanno nelle facce e sono perpendicolari allo spigolo), lato dalla formula
    // di Coxeter per {p,q,r} = {4,3,5}: cosh(lato/2) = cos(pi/4) sin(pi/5) / sin(pi/10).
    const double a = honeycomb435HalfSide();
    const double dihedral = angleAt(fromKlein({a, a, 0.0}), fromKlein({a, 0.0, 0.0}), fromKlein({0.0, a, 0.0}));
    const double side = distance(fromKlein({a, a, a}), fromKlein({a, a, -a}));
    const double coxeter = 2.0 * std::acosh(std::cos(kPi / 4) * std::sin(kPi / 5) / std::sin(kPi / 10));
    check(std::abs(degrees(dihedral) - 72.0) < 1e-9 && std::abs(side - coxeter) < 1e-9,
          format("cubo del reticolo {4,3,5}: angolo diedro %.6f gradi, lato %.6f (Coxeter %.6f)",
                 degrees(dihedral), side, coxeter));

    const Honeycomb h = honeycomb435(2.5);
    const double av = honeycomb435HalfSide();
    std::vector<std::vector<int>> cellVertices;   // per ogni cella i suoi 8 vertici
    auto indexOf = [&](const Vec4 &p) {
        for (size_t k = 0; k < h.vertices.size(); ++k)
            if (distance(h.vertices[k], p) < 1e-6) return int(k);
        return -1;
    };
    for (const Matrix &g : h.cells) {
        std::vector<int> idx;
        for (int v = 0; v < 8; ++v)
            idx.push_back(indexOf(g * fromKlein({(v & 1) ? av : -av, (v & 2) ? av : -av, (v & 4) ? av : -av})));
        cellVertices.push_back(idx);
    }
    // Attorno agli spigoli e ai vertici del cubo centrale (cella 0) il reticolo e' completo.
    int edgesOk = 0, verticesOk = 0;
    for (const auto &e : h.edges) {
        const auto &c0 = cellVertices[0];
        if (std::find(c0.begin(), c0.end(), e.first) == c0.end() || std::find(c0.begin(), c0.end(), e.second) == c0.end())
            continue;
        int around = 0;
        for (const auto &cv : cellVertices)
            if (std::find(cv.begin(), cv.end(), e.first) != cv.end() && std::find(cv.begin(), cv.end(), e.second) != cv.end())
                ++around;
        if (around == 5) ++edgesOk;
    }
    for (int v : cellVertices[0]) {
        int degree = 0, cells = 0;
        for (const auto &e : h.edges) degree += (e.first == v || e.second == v);
        for (const auto &cv : cellVertices) cells += std::find(cv.begin(), cv.end(), v) != cv.end();
        if (degree == 12 && cells == 20) ++verticesOk;
    }
    double sideError = 0.0;
    for (const auto &e : h.edges)
        sideError = std::max(sideError, std::abs(distance(h.vertices[e.first], h.vertices[e.second]) - coxeter));
    info(format("reticolo entro 2.5 raggi dal centro: %d cubi, %d vertici, %d spigoli",
                int(h.cells.size()), int(h.vertices.size()), int(h.edges.size())));
    // Il riferimento di uno spigolo (il disegno a istanze): l'origine in un
    // estremo, l'asse z sull'altro, alla distanza del lato; un'isometria.
    double frameError = 0.0;
    for (const auto &e : h.edges) {
        const Matrix f = frameTowards(h.vertices[e.first], h.vertices[e.second]);
        frameError = std::max({frameError, relDiff(f * origin(), h.vertices[e.first]),
                               relDiff(f * expAtOrigin({0.0, 0.0, honeycomb435EdgeLength()}), h.vertices[e.second]),
                               isometryError(f)});
    }
    check(frameError < 1e-10, format("riferimento degli spigoli: un estremo nell'origine, l'altro sull'asse z (errore %.1e)",
                                     frameError));
    // La cella che contiene un punto, camminando di faccia in faccia: il punto
    // sta nel cubo (Klein entro a) ed e' la cella col centro piu' vicino (le
    // celle di un reticolo regolare sono le sue regioni di Voronoi).
    int cellsOk = 0;
    for (int i = 0; i < 200; ++i) {
        const Vec4 p = expAtOrigin(randomVec(1.8));
        const Matrix g = honeycomb435CellOf(Matrix::identity(), p);
        const Vec3 k = toKlein(inverse(g) * p);
        const bool inside = std::max({std::abs(k.x), std::abs(k.y), std::abs(k.z)}) <= av + 1e-12;
        double nearest = 1e300;
        for (const Matrix &c : h.cells) nearest = std::min(nearest, distance(c * origin(), p));
        if (inside && std::abs(distance(g * origin(), p) - nearest) < 1e-9) ++cellsOk;
    }
    check(cellsOk == 200, format("cella che contiene un punto: trovata camminando, e' la piu' vicina (%d/200)", cellsOk));
    check(edgesOk == 12 && verticesOk == 8 && sideError < 1e-9,
          format("reticolo {4,3,5}: 5 cubi attorno a ogni spigolo del cubo centrale (%d/12), 12 spigoli e 20 cubi "
                 "attorno a ogni suo vertice (%d/8), spigoli tutti uguali (scarto %.1e)", edgesOk, verticesOk, sideError));
}

void testRenormalization()
{
    double before = 0.0, after = 0.0, moved = 0.0;
    std::normal_distribution<double> n(0.0, 1.0);
    for (int i = 0; i < 50; ++i) {
        const Matrix m = randomIsometry(3.0);
        Matrix noisy = m;
        for (auto &row : noisy.m)
            for (double &v : row) v *= 1.0 + 1e-7 * n(g_rng);
        const Matrix fixed = renormalized(noisy);
        before = std::max(before, isometryError(noisy));
        after = std::max(after, isometryError(fixed));
        moved = std::max(moved, distance(fixed * origin(), m * origin()));
    }
    // La posizione si sposta meno di quanto la matrice era uscita dal gruppo.
    check(after < 1e-12 && moved < before,
          format("raddrizzamento: errore da %.1e a %.1e, posizione spostata di %.1e", before, after, moved));

    // La deriva della camera: avanti di 0.01 e giro di 0.02 radianti per frame,
    // un cerchio di raggio atanh(1/2). Con curvatura 1 (giro = passo) sarebbe un
    // oriciclo, che va all'infinito: in H3 chi sterza troppo poco si allontana.
    const Matrix step = translation({0.0, 0.0, -0.01}) * rotation({0.0, 1.0, 0.0}, 0.02);
    const Matrix stepF = roundedToFloat(step);
    Matrix camD = Matrix::identity(), camF = Matrix::identity();
    double far = 0.0;
    for (int frame = 1; frame <= 216000; ++frame) {
        camD = camD * step;
        camF = roundedToFloat(camF * stepF);
        far = std::max(far, distance(origin(), camD * origin()));
        if (frame == 3600)
            info(format("dopo un minuto a 60 fps: deriva %.1e in float, %.1e in double", isometryError(camF),
                        isometryError(camD)));
    }
    info(format("dopo un'ora: deriva %.1e in float, %.1e in double; la camera resta entro %.3f dall'origine",
                isometryError(camF), isometryError(camD), far));
    check(isometryError(camD) < 1e-9 && isometryError(renormalized(camF)) < 1e-12 && far < 1.2,
          "camera in double per un'ora senza raddrizzare; quella in float si raddrizza");
}

} // namespace

int main()
{
    std::printf("TEST DEL NUCLEO DI H3 (hyperbolicspace.h)\n\n");
    testExpLog();
    testIsometries();
    testDistanceInvariance();
    testCharts();
    testEquilateralTriangles();
    testHolonomy();
    testParallelism();
    testPoincareBallBackground();
    testSplitAlongZ();
    testShaderPrecision();
    testLightCosines();
    testRayMarching();
    testHoneycomb();
    testRenormalization();
    std::printf("\nesito: %s\n", g_failures == 0 ? "tutte le verifiche passate"
                                                 : format("%d verifiche FALLITE", g_failures).c_str());
    return g_failures == 0 ? 0 : 1;
}
