// scenestate.h - I tipi della SCENA: cio' che un preset descrive e il Save
// scrive, lato interfaccia (testi dei campi, scelte, moti, slot di script e
// texture, colori dello sfondo, FOV). Il motore resta la copia unica di colori
// e alpha globali, MeshPart, stato 4D, inquadrature e marcher.
// MainWindow ne tiene il valore corrente (m_scene) e li chiama coi nomi
// storici (MainWindow::SceneState & co.); le classi estratte da MainWindow li
// usano da qui.
#pragma once

#include <QColor>
#include <QHash>
#include <QString>

// Costanti DISCRETE dichiarate dallo script con "A := int(min,max);".
// Chiave = lettera maiuscola (A..F, S); assente = costante continua.
// Al rilascio dello slider / Enter nel campo il valore scatta all'intero
// piu' vicino dentro [min,max]. Vedi applyDiscreteConstants().
struct DiscreteRange { int lo; int hi; };

// I testi del dock Equations COSI' COME SONO SCRITTI.
struct EquationTexts {
    QString x, y, z, p;                         // superficie X/Y/Z/P
    QString u, v, w;                            // composizione U, V, W
    QString explicitU, explicitV, explicitW;    // vincoli
    QString geoU, geoV, geoW;                   // flusso geodetico: punto iniziale
    QString geoDU, geoDV, geoDW;                // ...direzione iniziale
    QString conform;                            // ...fattore conforme
};
using EqField = QString EquationTexts::*;

// I quattro campi del Ray Marching, lo SCRITTO (l'applicato sta nel motore).
struct ImplicitTexts {
    QString equation;       // equazione implicita, sotto-tab 3D
    QString crossSection;   // equazione a 4 variabili, sotto-tab Cross Section
    QString texture;        // colore (campo Texture)
    QString displacement;   // rilievo (campo Variations)
};
using RmField = QString ImplicitTexts::*;

// Le costanti A..F, S: i testi dei campi (numeri o espressioni a cascata).
struct ConstantTexts { QString a, b, c, d, e, f, s; };
using ConstField = QString ConstantTexts::*;

// Dominio u/v/w, taglio x/y/z del Ray Marching e campi dei path 4D e 3D.
struct LimitTexts {
    QString uMin, uMax, vMin, vMax, wMin, wMax;     // dominio
    QString xMin, xMax, yMin, yMax, zMin, zMax;     // taglio RM
    // I valori di avvio e di reset: dominio di lavoro 0..2pi, 0..2pi, 0..1
    // (a campi vuoti il primo Run dell'utente li leggerebbe cosi' come
    // sono), taglio vuoto = nessun taglio.
    void resetDomain() { uMin = "0"; uMax = "6.28318"; vMin = "0"; vMax = "6.28318";
                         wMin = "0"; wMax = "1"; }
    void clearSpaceCut() { xMin = xMax = yMin = yMax = zMin = zMax = QString(); }
};
struct PathTexts  { QString x, y, z, p, alpha, beta, gamma;         // path 4D
                    QString x3D, y3D, z3D, roll3D; };               // path 3D

// Vista di un path: tangente al moto o verso il centro.
enum CameraPathMode {
    ModeTangential,
    ModeCentered
};

// ----------------------------------------------------------
// LA SCENA: le sedi dello stato lato MainWindow in UN valore
// ----------------------------------------------------------
// Ognuna e' documentata in mainwindow.h, nella sezione dei suoi setter. Riunite qui perche' il load diventi un'assegnazione, il Save la
// sua traduzione e il reset la scena di default (punto 5 del refactoring).
// Nel MOTORE restano, copia unica, colori/alpha/luce, MeshPart, stato 4D,
// inquadrature e marcher.
struct SceneState {
    EquationTexts eq;         // dock Equations, lo SCRITTO (l'applicato: m_eqApplied)
    ImplicitTexts rm;         // i quattro campi Ray Marching
    ConstantTexts constants;  // costanti A..F, S (testi, anche a cascata)
    LimitTexts lim;           // dominio u/v/w e taglio x/y/z
    PathTexts path;           // path 4D e 3D
    int steps = 100;          // Steps (parametrico) / Ray Steps (RM)

    // Scelte
    bool implicitMode = false;          // Parametric / Implicit (vista: tabModeSelector)
    bool crossSectionTab = false;       // sotto-tab RM 3D / Cross Section
    bool implicitShell = true;          // Shell / Solid (vista: due coppie di radio)
    int renderMode = 0;                 // Base / Phong / Wireframe, globale
    bool meshScopeAll = true;           // ambito multi-mesh All / Mesh
    bool surfaceTextureState = false;   // texture di superficie accesa (intenzione)
    int lightingMode4D = 0;

    // Moti della camera
    CameraPathMode pathViewMode4D = ModeTangential;   // vista del path 4D (pushView)
    CameraPathMode pathViewMode3D = ModeTangential;   // vista del path 3D (pushView3D)
    int pathSpeed3D = 10;               // unita' degli slider, 1..100
    int pathSpeed4D = 10;
    QString lastCameraMotion;           // ultimo moto camera avviato (Save: activeMotion)

    // Dock Script: lo scritto per modulo e l'applicato della superficie
    QString surfaceScriptText;
    QString surfaceScriptApplied;
    QString surfaceTextureScriptText;
    QString bgTextureScriptText;
    QString soundScriptText;

    // Texture: codice applicato e ancore della Library
    QString surfaceTextureCode;
    QString bgTextureCode;
    QString textureLibName;
    QString bgTextureLibName;
    QString soundLibName;

    // Costanti dichiarate dallo script: discrete ("A := int(1,6)") e minimi
    QHash<QString, DiscreteRange> discreteConsts;
    QHash<QString, float> minConsts;

    // SFONDO: colore pieno e colori u_col1/u_col2 della sua texture (il
    // motore li consuma: setBackgroundColor, setBackgroundTexColors).
    // Quelli della texture di SUPERFICIE invece vivono solo nel motore
    // (vedi surfaceTexColor), come il colore e l'alpha globali.
    QColor bgColor;
    QColor bgTexColor1 = Qt::white;
    QColor bgTexColor2 = Qt::black;
    // FOV dei path, uno solo (lo slider e' unico). Applicato SOLO dentro
    // CameraPaths::applyCameraAt (quindi anche nei video, che passano di li');
    // fuori dalle path la proiezione resta al default 45 (lo zoom fuori
    // path ha gia' i suoi comandi, e un reset non deve rimpicciolire la
    // superficie). Persistito come "cameraFov" e, per le build precedenti,
    // "fov3D"/"fov4D".
    float fov = 45.0f;
};
