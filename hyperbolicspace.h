// hyperbolicspace.h - Lo spazio iperbolico H3: punti, isometrie, mappa
// esponenziale. E' il nucleo matematico delle scene dentro H3 viste da un
// abitante: la camera e le pose degli oggetti sono isometrie di questo file.
//
// MODELLO: l'iperboloide di R^{3,1}. Un punto e' (x,y,z,w) con
// <p,p> = x^2 + y^2 + z^2 - w^2 = -1 e w > 0; l'origine e' (0,0,0,1). Le
// isometrie sono le matrici di Lorentz (M^T J M = J, J = diag(1,1,1,-1)) che
// non scambiano le due falde: si compongono col prodotto, come le matrici
// affini della scena euclidea, e l'inversa costa una trasposizione.
// Curvatura -1: le lunghezze sono in raggi di curvatura (chi usa un'altra
// scala divide prima).
//
// Tutto in double: la camera accumula un prodotto per frame e in float la
// matrice esce dal gruppo (deriva misurata da hyperbolic-test). Si passa a
// float solo alla fine, verso la GPU.
//
// PRECISIONE: le coordinate crescono come e^d con la distanza d
// dall'origine, quindi anche gli errori: per questo la camera resta
// nell'origine e si muove il mondo.
//
// Le carte per lo schermo:
//   Klein      p.xyz / p.w         le geodetiche sono rette (raster esatto)
//   Poincare'  p.xyz / (1 + p.w)   conforme; e' quella dello sfondo Poincare' Ball
// Viste dall'origine le due danno la stessa DIREZIONE: cio' che vede una
// camera nell'origine non dipende dalla carta.
//
// Test: hyperbolictest.cpp (ExC/Mac/Test/hyperbolic-test.sh).
#pragma once

#include <utility>
#include <vector>

namespace H3 {

struct Vec3 { double x = 0, y = 0, z = 0; };
struct Vec4 { double x = 0, y = 0, z = 0, w = 0; };

// Matrice 4x4, m[riga][colonna]. Agisce sui punti come colonne: M * p.
struct Matrix {
    double m[4][4] = {};
    static Matrix identity();
};

Matrix operator*(const Matrix &a, const Matrix &b);
Vec4 operator*(const Matrix &a, const Vec4 &p);

// Prodotto di Minkowski <a,b> = ax bx + ay by + az bz - aw bw.
double dot(const Vec4 &a, const Vec4 &b);
Vec4 origin();

// Mappa esponenziale nell'origine: il punto raggiunto partendo dall'origine
// lungo la direzione di v per una distanza |v|. logAtOrigin e' l'inversa.
Vec4 expAtOrigin(const Vec3 &v);
Vec3 logAtOrigin(const Vec4 &p);

double distance(const Vec4 &p, const Vec4 &q);
// Angolo in a fra le geodetiche che vanno da a verso b e da a verso c.
double angleAt(const Vec4 &a, const Vec4 &b, const Vec4 &c);

// ISOMETRIE
// Rotazione attorno all'origine (asse qualunque, non nullo; angolo in radianti).
Matrix rotation(const Vec3 &axis, double angle);
// Traslazione lungo la geodetica per l'origine di direzione v, di |v|: porta
// l'origine in expAtOrigin(v) e trasporta parallelamente lungo quella geodetica.
Matrix translation(const Vec3 &v);
// La traslazione che porta l'origine in p (la stessa di translation(logAtOrigin(p))).
Matrix translationTo(const Vec4 &p);
// Inversa di un'isometria: J M^T J.
Matrix inverse(const Matrix &m);
// Riporta nel gruppo una matrice che ne e' uscita per gli arrotondamenti
// (Gram-Schmidt di Minkowski sulle colonne, a partire dalla quarta, che e' la
// posizione M * origine e cosi' cambia meno di tutte).
Matrix renormalized(const Matrix &m);
// Quanto m e' lontana dal gruppo: max |(M^T J M - J)_ij|. Zero per un'isometria esatta.
double isometryError(const Matrix &m);
// m = k * translation(0, 0, distance) * kp, con k e kp rotazioni e distance >= 0
// (= distanza dall'origine di m * origine). Serve allo shader: la spinta lungo z
// si scrive con sinh(R - r) e cosh(R - r), dove la cancellazione e' esatta,
// invece di sottrarre numeri grandi come e^r in float.
void splitAlongZ(const Matrix &m, Matrix &k, double &distance, Matrix &kp);

// IL RETICOLO {4,3,5} (le pareti della scena H3): cubi con angoli diedri di
// 72 gradi, cinque attorno a ogni spigolo, venti attorno a ogni vertice. Il cubo
// centrale, nella carta di Klein, e' [-a, a]^3 con cos(72) = a^2 / (1 - a^2); gli
// altri sono le sue immagini per le riflessioni nelle facce (isometrie di Lorentz).
struct Honeycomb {
    std::vector<Matrix> cells;                 // l'isometria che porta il cubo centrale in ciascuna cella
    std::vector<Vec4> vertices;                // senza ripetizioni
    std::vector<std::pair<int, int>> edges;    // indici in vertices, senza ripetizioni
};
double honeycomb435HalfSide();                 // a: meta' lato del cubo centrale nella carta di Klein
double honeycomb435EdgeLength();               // il lato di ogni cubo, in raggi di curvatura
// La riflessione nella faccia `face` del cubo centrale: 0/1 = x = +a/-a, 2/3 = y, 4/5 = z.
Matrix honeycomb435Reflection(int face);
// Le celle col centro entro maxDistance dall'origine, coi loro vertici e spigoli.
Honeycomb honeycomb435(double maxDistance);
// La cella che contiene p, cercata camminando di faccia in faccia da `start`
// (di solito quella del fotogramma prima: uno o due passi). Al piu' maxSteps.
Matrix honeycomb435CellOf(const Matrix &start, const Vec4 &p, int maxSteps = 200);

// Il riferimento di un segmento geodetico: l'isometria che porta l'origine in a
// e la direzione +z dell'origine verso b (b = frame * expAtOrigin(0, 0, d(a,b))).
Matrix frameTowards(const Vec4 &a, const Vec4 &b);

// CARTE
Vec3 toKlein(const Vec4 &p);
Vec4 fromKlein(const Vec3 &k);       // |k| < 1
Vec3 toPoincare(const Vec4 &p);
Vec4 fromPoincare(const Vec3 &b);    // |b| < 1

} // namespace H3
