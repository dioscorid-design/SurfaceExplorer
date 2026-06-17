// =====================================================================
// implicit.glsl — Libreria di solver per variabili definite IMPLICITAMENTE
// =====================================================================
// Iniettata in geodesic.glsl (placeholder /*%IMPLICIT_LIB%*/) PRIMA di
// evalMetric, così il corpo metrico dello script può chiamare queste
// funzioni, es.:
//
//     float r   = kerrR_U(U, A, B);        // A = massa M, B = spin a
//     float gpp = kerrGpp(U, A, B);        // slice equatoriale di Kerr
//
// GLSL non ha puntatori a funzione: niente solver "di una f arbitraria".
// Offriamo invece helper generici riusabili + funzioni specifiche e
// collaudate per le metriche note (Kerr in forma chiusa, Kruskal via
// Newton 1D). Per aggiungere un caso nuovo si scrive una funzione qui.
// Tutte le funzioni sono difensive: niente divisioni per zero, niente
// sqrt di argomenti negativi, niente overflow di exp.
// =====================================================================

// ---------------------------------------------------------------------
// HELPER GENERICI
// ---------------------------------------------------------------------

// Radice NON negativa di a*X^2 + b*X + c = 0 (X = incognita, qui in genere
// X = r^2). Restituisce la radice "+"; se il discriminante è negativo per
// errore numerico lo clampa a 0. Pensata per i casi (come Kerr) in cui la
// radice fisica è quella positiva.
float solveQuadraticPos(float a, float b, float c) {
    if (abs(a) < 1e-12) {
        // Degenera in lineare b*X + c = 0
        if (abs(b) < 1e-12) return 0.0;
        return max(-c / b, 0.0);
    }
    float disc = b * b - 4.0 * a * c;
    disc = max(disc, 0.0);
    float s = sqrt(disc);
    // Radice "+": per a>0 e c<=0 (caso tipico r^2) dà il valore >= 0.
    return (-b + s) / (2.0 * a);
}

// Newton-Raphson generico per la sola famiglia Kruskal-like (f e f' note
// dal chiamante). GLSL non permette callback, quindi i solver "newtoniani"
// specifici implementano f/f' al loro interno; questo è solo lo schema di
// clamp che tutti condividono per la stima iniziale.
float clampPositive(float x, float lo) {
    return max(x, lo);
}

// ---------------------------------------------------------------------
// KERR — IMBUTO EQUATORIALE come SUPERFICIE PARAMETRICA (t = cost, theta = pi/2)
// ---------------------------------------------------------------------
// La fetta spaziale equatoriale di Kerr (ponte di Einstein-Rosen rotante) e' una
// SUPERFICIE DI RIVOLUZIONE che disegniamo PARAMETRICAMENTE, NON come flusso
// geodetico metrico. Motivo: per Kerr (a != 0) non esiste una coordinata
// regolare sull'orizzonte con r esplicito (esiste solo per a = 0, la coord.
// altezza di Flamm), e ogni inversione r(U) dentro il loop geodetico e' o
// singolare (carta r) o costosa/imprecisa. La superficie di rivoluzione invece
// ha r come parametro ESPLICITO: niente inversione, niente singolarita' di
// coordinate, niente artefatti. L'embedding e' FEDELE: R(r) e z(r) realizzano la
// metrica intrinseca della fetta (non e' un imbuto decorativo).
//
// Geometria della fetta (Boyer-Lindquist, equatore, dt = 0):
//     g_rr(r) = r^2 / Delta ,   Delta = r^2 - 2Mr + a^2
//     g_pp(r) = r^2 + a^2 + 2 M a^2 / r
// Superficie di rivoluzione (R(r) cosV, R(r) sinV, z(r)) con R = sqrt(g_pp):
//     ds^2 = (z'^2 + R'^2) dr^2 + R^2 dV^2  ==  g_rr dr^2 + g_pp dV^2
// se z'(r) = sqrt(g_rr - R'(r)^2). Il parametro radiale (campo U nei dock) E' r,
// e parte dall'orizzonte r_+ = kerrUmin (gola, z = 0). Niente frame dragging:
// g_t_phi vive nel settore temporale e su dt = 0 non compare. M = costante A,
// a = spin = costante B (0 <= a <= M). A a = 0 e' l'imbuto di Flamm esatto.

// r implicito dalle coordinate di Kerr (forma CHIUSA, niente iterazione):
//     r^4 - (x^2+y^2+z^2 - a^2) r^2 - a^2 z^2 = 0  -> quadratica in R = r^2.
// Tenuta perche' e' la base; sull'equatore (z = 0) si riduce a r = sqrt(x^2+y^2).
float solveKerrR(float x, float y, float z, float a) {
    float rho2 = x * x + y * y + z * z;
    float a2   = a * a;
    float R = solveQuadraticPos(1.0, -(rho2 - a2), -a2 * z * z); // R = r^2 >= 0
    return sqrt(max(R, 0.0));
}

// Orizzonte esterno r_+ = M + sqrt(M^2 - a^2) (reale per a <= M): e' la GOLA
// dell'imbuto, da cui parte il parametro radiale. A a = 0 vale 2M (gola di
// Flamm); a a = M vale M (orizzonte estremale degenere, collo infinito).
float kerrUmin(float M, float a) {
    float disc = max(M * M - a * a, 0.0);
    return M + sqrt(disc);
}

// g_rr = r^2/Delta. DIVERGE alla gola (Delta -> 0): e' la tangente verticale
// dell'imbuto (come 1/(1-2M/r) per Flamm), non un errore. Serve solo dentro
// l'integrale di z(r); il parametro r parte da r_+ e non vi passa mai sotto.
// Delta clampato difensivamente.
//
// NB: la fetta e' la SOLA regione esterna fisica r >= r_+. NON c'e' una seconda
// bocca speculare: la simmetria z(-r) = -z(r) vale per Schwarzschild (due regioni
// asintotiche), NON per Kerr. La continuazione a r < 0 di Kerr esiste ma NON e'
// immergibile in R^3 euclideo (vedi docs/kerr-regione-r-negativo.md): non la
// disegniamo. Per questo gli helper assumono r >= r_+ > 0.
float kerrGrr(float r, float M, float a) {
    float rr = max(r, 1e-4);
    float Delta = rr * rr - 2.0 * M * rr + a * a;
    Delta = (Delta < 1e-6) ? 1e-6 : Delta;
    return rr * rr / Delta;
}

// Parte angolare g_pp = r^2 + a^2 + 2Ma^2/r. Il raggio AREALE del cerchio phi e'
// R = sqrt(g_pp). g_pp esplode come 1/r vicino a r = 0 (ring singularity), ma r
// parte da r_+ > 0, ben lontano.
float kerrGpp(float r, float M, float a) {
    float rr = max(r, 1e-4);
    return rr * rr + a * a + 2.0 * M * a * a / rr;
}

// Raggio areale R(r) = sqrt(g_pp): raggio del cerchio di coordinata phi (la
// coordinata radiale dell'imbuto di rivoluzione). Esplicito in r.
float kerrRadius(float r, float M, float a) {
    return sqrt(max(kerrGpp(r, M, a), 0.0));
}

// ---------------------------------------------------------------------
// ERGOSFERA DI KERR — DUE SUPERFICI COORDINATE (per il ray marching)
// ---------------------------------------------------------------------
// Diversamente dall'imbuto equatoriale qui NON immergiamo la metrica: le due
// superfici sono le SUPERFICI COORDINATE r = const(theta) di Kerr in coordinate
// di Boyer-Lindquist, disegnate come SDF implicite nello spazio (x,y,z) cartesiano.
// r implicito da solveKerrR(x,y,z,a); cos(theta) = z / r.
//
//   Orizzonte degli eventi:  r = r_+ = M + sqrt(M^2 - a^2)            (sfera)
//   Limite statico (ergosfera esterna): r = M + sqrt(M^2 - a^2 cos^2 theta)
// Coincidono ai poli (cos^2=1) e sono massimamente separate all'equatore
// (cos=0 -> r = 2M). L'ergosfera e' la regione tra le due. M = A, a = B (0<=a<=M).

// Raggio dell'orizzonte degli eventi esterno (sfera coordinata). Reale per a<=M.
float kerrHorizonR(float M, float a) {
    return M + sqrt(max(M * M - a * a, 0.0));
}

// Raggio del limite statico in funzione di cos(theta) = z/r (schiacciato ai poli).
float kerrStaticLimitR(float ct, float M, float a) {
    return M + sqrt(max(M * M - a * a * ct * ct, 0.0));
}

// SDF implicita (segno: <0 dentro la superficie coordinata, >0 fuori) dell'ORIZZONTE.
// Da usare come campo Inner:= (superficie opaca in fondo).
float kerrHorizonSDF(vec3 p, float M, float a) {
    float r = solveKerrR(p.x, p.y, p.z, a);
    return r - kerrHorizonR(M, a);
}

// SDF implicita del LIMITE STATICO (ergosfera). Da usare come superficie esterna
// trasparente (corpo dello script ray marching).
float kerrStaticLimitSDF(vec3 p, float M, float a) {
    float r  = solveKerrR(p.x, p.y, p.z, a);
    float ct = p.z / max(r, 1e-4);
    return r - kerrStaticLimitR(ct, M, a);
}

// R'(r) per differenze finite centrate (per l'embedding).
float kerrRprime(float r, float M, float a) {
    float h  = 1e-3;
    float lo = max(r - h, 0.0);
    float hi = r + h;
    return (kerrRadius(hi, M, a) - kerrRadius(lo, M, a)) / (hi - lo);
}

// Profilo verticale z(r) dell'imbuto. Superficie di rivoluzione
// (R(r)cosV, R(r)sinV, z(r)) con R = kerrRadius:
//     z'(r) = sqrt( g_rr(r) - R'(r)^2 ),   z(r_+) = 0.
// Il parametro e' r ESPLICITO (campo U dei dock = r); parte dalla gola r_+ =
// kerrUmin e cresce verso fuori. La quantita' sotto radice e' >= 0 sul dominio
// fisico (imbuto immergibile in R^3 da r_+ in poi), clampata a 0 per sicurezza.
//
// PROBLEMA NUMERICO E SUA CURA. Alla gola g_rr = r^2/Delta -> infinito come
// 1/(r-r_+): l'integrando ~ 1/sqrt(r-r_+) e' una singolarita' INTEGRABILE che il
// trapezio uniforme cattura male. Cambio di variabile r = r_+ + s^2 (dr = 2s ds):
// sqrt(g_rr)*2s -> finito alla gola => integrando REGOLARE, niente epsilon.
// Verificato contro Flamm esatto a a = 0 (z = 2 sqrt(2M(r-2M))): errore < 0.4%
// uniforme con N = 128. A a = 0 e' l'imbuto di Flamm; a a -> M il collo diventa
// infinito (z grande gia' vicino a r_+ = M): troncare il dominio dall'alto.
// UNA SOLA falda (r >= r_+): per Kerr non esiste la bocca speculare a z < 0
// (vedi nota in kerrGrr e docs/kerr-regione-r-negativo.md).
float kerrEmbedZ(float U, float M, float a) {
    const int N = 128;
    float Umin = kerrUmin(M, a);
    float r_   = max(U, Umin);         // parametro radiale, sempre >= r_+ (gola)
    if (r_ - Umin < 1e-9) return 0.0;  // sulla gola: z = 0

    float smax = sqrt(r_ - Umin);
    float ds   = smax / float(N);

    // Integrando in s: f(s) = sqrt(g_rr - R'^2) * (dr/ds), con dr/ds = 2s.
    // A s = 0 NON e' zero: g_rr ~ 1/s^2 e f(0) = 2 r_+ / sqrt(r_+ - r_-) (finito).
    // Lo valutiamo a s = ds (gia' regolare grazie al cambio variabile) e usiamo
    // quel valore anche come estremo sinistro: evita il caso speciale su r_-.
    float integrand_prev;
    {
        float r0  = Umin + ds * ds;
        float rp0 = kerrRprime(r0, M, a);
        integrand_prev = sqrt(max(kerrGrr(r0, M, a) - rp0 * rp0, 0.0)) * 2.0 * ds;
    }
    float acc = 0.5 * integrand_prev * ds;   // primo sotto-intervallo [0, ds]
    for (int i = 2; i <= N; ++i) {
        float s  = ds * float(i);
        float r  = Umin + s * s;
        float rp = kerrRprime(r, M, a);
        float integrand = sqrt(max(kerrGrr(r, M, a) - rp * rp, 0.0)) * 2.0 * s;
        acc += 0.5 * (integrand_prev + integrand) * ds;  // trapezio in s
        integrand_prev = integrand;
    }
    return acc;
}

// ---------------------------------------------------------------------
// KRUSKAL–SZEKERES (r implicito dalle coordinate di Kruskal X, T)
// ---------------------------------------------------------------------
// Sulla carta di Kruskal r(X,T) è definito da:
//
//     (r/2M - 1) * exp(r/2M) = X^2 - T^2   (chiamiamo K = X^2 - T^2)
//
// con r >= 2M per K >= 0 (esterno/orizzonte). La funzione a sinistra è
// strettamente crescente in r: Newton converge in poche iterazioni.
//   f(r)  = (r/2M - 1) * exp(r/2M) - K
//   f'(r) = (r / (4 M^2)) * exp(r/2M)
// Per K < 0 (interno) la radice è r < 2M; gestita con la stessa iterazione
// partendo da una stima sotto l'orizzonte. exp è clampato per non esplodere.
float solveKruskalR(float K, float M) {
    float twoM = 2.0 * M;
    // Stima iniziale: vicino all'orizzonte per K piccolo, cresce per K grande.
    // r0 ~ 2M*(1 + ln(1+|K|)) tiene l'argomento di exp in un intervallo sano.
    float r = twoM * (1.0 + log(1.0 + abs(K)));
    r = max(r, 0.05 * twoM); // mai a zero esatto

    for (int i = 0; i < 24; ++i) {
        float e = exp(clamp(r / twoM, -60.0, 60.0)); // anti-overflow
        float f  = (r / twoM - 1.0) * e - K;
        float df = (r / (twoM * twoM)) * e;          // f'(r)
        if (abs(df) < 1e-12) break;
        float step = f / df;
        // Limita il passo per stabilità quando exp è grande.
        step = clamp(step, -twoM, twoM);
        r -= step;
        r = max(r, 1e-4);
        if (abs(step) < 1e-6) break;
    }
    return r;
}

// NB: la slice temporale T0 NON è una funzione di libreria: si scrive
// direttamente nello script usando le costanti/slider del dock, es.
//   T0 = min(B, 0.95) * sin(C * t)
// (B = ampiezza < 1, C = velocità). Così i parametri sono regolabili dalla UI e
// il clamp di sicurezza |T0|<1 resta visibile nello script. Vedi le note Kruskal.

// r(U) sulla slice T = T0: r = solveKruskalR(U^2 - T0^2, M). Comodo per gli
// embedding che vogliono il raggio della sezione angolare.
float kruskalR_U(float U, float M, float T0) {
    return solveKruskalR(U * U - T0 * T0, M);
}

// g_XX(U) = (32 M^3 / r) * exp(-r/2M), il fattore radiale ESATTO di Kruskal.
float kruskalGxx(float U, float M, float T0) {
    float r = kruskalR_U(U, M, T0);
    return (32.0 * M * M * M / r) * exp(clamp(-r / (2.0 * M), -60.0, 60.0));
}

// ---------------------------------------------------------------------
// EMBEDDING ISOMETRICO DELLA SLICE DI KRUSKAL
// ---------------------------------------------------------------------
// Una superficie di rivoluzione (r(U)cosV, r(U)sinV, z(U)) ha metrica indotta
//     ds^2 = [ z'(U)^2 + r'(U)^2 ] dU^2 + r(U)^2 dV^2.
// Per REALIZZARE la metrica di Kruskal g_XX dU^2 + r^2 dV^2 serve quindi
//     z'(U) = sqrt( g_XX(U) - r'(U)^2 )      (la parte angolare r^2 è già esatta)
//     z(U)  = integral_0^U sqrt( g_XX(s) - r'(s)^2 ) ds.
// r'(U) e l'integrale non hanno forma chiusa (r è già implicito): li calcoliamo
// numericamente. La quantità sotto radice è >= 0 sul dominio fisico (verificato),
// ma la clampiamo a 0 per sicurezza numerica vicino alla gola.

// r'(U) per differenze finite centrate.
float kruskalRprime(float U, float M, float T0) {
    float h = 1e-3;
    float lo = max(U - h, 0.0);
    float hi = U + h;
    return (kruskalR_U(hi, M, T0) - kruskalR_U(lo, M, T0)) / (hi - lo);
}

// Profilo verticale z(U) = integrale del profilo isometrico, regola del trapezio
// con N campioni da 0 a U. N=64 è un buon compromesso qualità/costo per vertice.
float kruskalEmbedZ(float U, float M, float T0) {
    const int N = 64;
    float sign = (U < 0.0) ? -1.0 : 1.0;   // simmetria delle due bocche
    float Uabs = abs(U);
    float ds = Uabs / float(N);
    if (ds < 1e-9) return 0.0;

    float integrand_prev;
    {
        float rp = kruskalRprime(0.0, M, T0);
        integrand_prev = sqrt(max(kruskalGxx(0.0, M, T0) - rp * rp, 0.0));
    }
    float acc = 0.0;
    for (int i = 1; i <= N; ++i) {
        float s = ds * float(i);
        float rp = kruskalRprime(s, M, T0);
        float integrand = sqrt(max(kruskalGxx(s, M, T0) - rp * rp, 0.0));
        acc += 0.5 * (integrand_prev + integrand) * ds;  // trapezio
        integrand_prev = integrand;
    }
    return sign * acc;
}
