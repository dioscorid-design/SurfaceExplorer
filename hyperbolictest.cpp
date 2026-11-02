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
    // in direzione d, con la traslazione di Moebius 0 -> p. La camera della 2.1
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
    check(after < 1e-12 && moved < 1e-5,
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
    testRenormalization();
    std::printf("\nesito: %s\n", g_failures == 0 ? "tutte le verifiche passate"
                                                 : format("%d verifiche FALLITE", g_failures).c_str());
    return g_failures == 0 ? 0 : 1;
}
