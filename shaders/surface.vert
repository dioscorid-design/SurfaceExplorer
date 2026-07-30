#version 440

// INPUT
layout(location = 0) in vec4 vertex;
layout(location = 1) in vec4 normal;
layout(location = 2) in vec2 texCoord;

// OUTPUT
layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec4 v_normal;
layout(location = 2) out vec2 v_texCoord;
layout(location = 3) out float v_light4D;
layout(location = 4) out float v_spec4D;

layout(std140, binding = 0) uniform SceneUBO {
    mat4 u_mvpMatrix;
    mat4 u_mvMatrix;
    mat4 u_mMatrix;
    vec4 u_dummyZero;
    vec4 u_observerPos;
    vec4 u_cameraPos4D;
    vec4 u_mathParams;
    vec4 u_mathParams2;
    vec3 color;
    float alpha;
    vec3 u_col1;
    float u_lightIntensity;
    vec3 u_col2;
    float u_zoom;
    vec2 u_center;
    float u_rotation;
    float u_omega;
    float u_phi;
    float u_psi;
    float u_time;
    int u_projMode;
    int u_lightingMode;
    int u_renderMode;
    int u_isFlat;
    int useTexture;
    int useSpecular;
    float u_min;
    float u_max;
    float v_min;
    float v_max;
    int u_hasExplicitW;
    // Da qui in poi il blocco deve rispecchiare la coda di UboData (glwidget.h):
    // std140 permette di dichiarare solo un PREFISSO del blocco, ma non di
    // saltare campi in mezzo. u_meshIndex sta dopo i limiti x/y/z, quindi questi
    // vanno dichiarati anche se il vertex shader non li usa.
    int u_raySteps;
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    float z_min;
    float z_max;
    // MULTI-MESH: indice della parte in corso di disegno (0 se mesh singola).
    // Permette allo script di distinguere il ramo.
    float u_meshIndex;
} ubuf;

float sq(float x) { return x*x; }

// NB: safePow(base, expo) — pow() sicura per basi negative — è definita e iniettata
// da GLWidget (createVertexShaderSource) PRIMA di questo template, ed è quella che il
// traduttore usa al posto di pow(. Qui non va ridefinita.

// Bonifica NaN/Inf da una posizione calcolata: equazioni numericamente delicate
// (sqrt/acos/pow con argomenti al limite, es. il preset "Fresnel") possono uscire
// dal dominio valido su GPU con precisione diversa (tipico Android Mali/Adreno vs
// desktop/Metal) e produrre NaN/Inf -> i vertici schizzano all'infinito e la mesh
// "esplode" in triangoli. Sostituiamo i componenti non finiti con 0, così un vertice
// degenere collassa in un punto finito invece di sparare via.
// NB: usiamo il test `!(abs <= 1e6)` invece di isnan()/isinf(): è FALSO per ogni
// confronto con NaN (quindi !false=true -> scartato) e cattura anche Inf/valori
// enormi, restando affidabile anche sui driver Android con fast-math che ignorano
// isnan()/isinf().
vec4 sanitizePos(vec4 p) {
    bvec4 ok = lessThanEqual(abs(p), vec4(1.0e6));
    return mix(vec4(0.0), p, vec4(ok));
}

// --- MATRICI DI ROTAZIONE 4D ---
vec4 rotateXW(vec4 p, float t) {
    float c = cos(t); float s = sin(t);
    return vec4(p.x*c + p.w*s, p.y, p.z, -p.x*s + p.w*c);
}
vec4 rotateYW(vec4 p, float t) {
    float c = cos(t); float s = sin(t);
    return vec4(p.x, p.y*c + p.w*s, p.z, -p.y*s + p.w*c);
}
vec4 rotateZW(vec4 p, float t) {
    float c = cos(t); float s = sin(t);
    return vec4(p.x, p.y, p.z*c + p.w*s, -p.z*s + p.w*c);
}

// --- STEREOGRAFICA SU IPERSFERA (S^3) ---
// Unica implementazione: la usano sia project4D() (normali e derivate) sia il
// calcolo della posizione finale in main(). Erano due copie identiche della
// stessa formula, e una divergenza fra loro sfalserebbe le normali rispetto
// alla geometria.
//
// Il raggio proiettato e' sqrt((1+w)/(1-w)) e deve CRESCERE all'infinito verso
// l'antipode della camera. Il clamp storico agiva sul denominatore
// (denom < 0.05 -> 0.05), quindi oltre w = 0.95 (18.2 gradi dall'antipode) il
// raggio DECRESCEVA fino a 0: la calotta antipodale veniva ripiegata verso la
// camera con le facce invertite, e il punto di fuga dei tunnel restava
// scoperto (buco tondo sullo sfondo volando dentro una superficie che
// circonda l'antipode). Qui il clamp e' MONOTONO e agisce sul risultato:
// sotto w = 0.95 la formula e' algebricamente identica a quella storica,
// quindi nessuna figura cambia aspetto; cambia solo cio' che prima era
// ripiegato.
//
// Il tetto 12 non e' arbitrario: il far plane vale 25 * distanza della camera
// (glwidget.cpp), e fra i preset in stereografica la camera piu' vicina e'
// quella di Clifford Torus a 0.5, cioe' far plane 12.5. Con 12 nessun preset
// della libreria puo' vedere la calotta antipodale sparire oltre il far plane,
// che sarebbe l'unica regressione possibile rispetto al vecchio clamp (il
// quale, ripiegando, teneva tutto entro 6.24 e quindi non tagliava mai).
vec3 stereoS3(vec4 p) {
    float r = length(p);
    if (r < 0.0001) return vec3(0.0);

    vec4 pNorm = p / r;
    float sxy = length(pNorm.xyz);
    float rad = min(sqrt((1.0 + pNorm.w) / max(1.0 - pNorm.w, 1.0e-6)), 12.0);
    vec3 dir = (sxy > 1.0e-6) ? pNorm.xyz / sxy : vec3(0.0, 0.0, 1.0);

    return dir * (r * rad);
}

// --- PROIEZIONE 4D -> 3D CON OSSERVATORE ---
vec3 project4D(vec4 p) {
    if (ubuf.u_projMode == 0) {
        return p.xyz;
    }
    else if (ubuf.u_projMode == 2) {
        return stereoS3(p);
    }

    vec4 obs = ubuf.u_observerPos;
    float denom = obs.w - p.w;
    if (abs(denom) < 0.0001) {
        return p.xyz * 10.0;
    }

    float wFactor = obs.w / denom;
    float x = (p.x - obs.x) * wFactor + obs.x;
    float y = (p.y - obs.y) * wFactor + obs.y;
    float z = (p.z - obs.z) * wFactor + obs.z;
    return vec3(x, y, z);
}

// --- FUNZIONE UTENTE ---
vec4 getRawPosition(float u, float v, float w) {
#ifdef CUSTOM_MESH
    // SE SIAMO IN MODALITÀ GEODESIC FLOW, PRENDIAMO IL VERTICE DIRETTAMENTE DAL BUFFER
    return vertex;
#else
    // ALTRIMENTI FACCIAMO I CALCOLI CLASSICI
    float A = ubuf.u_mathParams.x;
    float B = ubuf.u_mathParams.y;
    float C = ubuf.u_mathParams.z;
    float D = ubuf.u_mathParams2.x;
    float E = ubuf.u_mathParams2.y;
    float F = ubuf.u_mathParams2.z;
    float s = ubuf.u_mathParams.w;
    float t = ubuf.u_time;

    float x = %X_EQ%;
    float y = %Y_EQ%;
    float z = %Z_EQ%;
    float p = %W_EQ%;
    return sanitizePos(vec4(x, y, z, p));
#endif
}

// --- FUNZIONE INIETTATA DA C++ ---
float getExplicitVal(float a, float b) {
    // Qui il C++ inietterà: "float u=a; float v=b; return (sin(u)*cos(v));" o simile
    %EXPLICIT_BODY%
}

vec4 getRawPositionAuto(float r1, float r2) {
    // 0=W(u,v), 1=U(v,w), 2=V(u,w)
    int mode = %CONSTRAINT_MODE%;

    float u, v, w;

    if (mode == 1) { // Constraint U
        v = r1;
        w = r2;
        u = getExplicitVal(v, w); // Calcola U
    }
    else if (mode == 2) { // Constraint V
        u = r1;
        w = r2;
        v = getExplicitVal(u, w); // Calcola V
    }
    else { // Constraint W (Default)
            u = r1;
            v = r2;
            // Se c'è un'equazione esplicita W, la calcola, altrimenti 0
            w = (ubuf.u_hasExplicitW != 0) ? getExplicitVal(u, v) : 0.0;
        }

    return getRawPosition(u, v, w);
}

// Funzione completa (Solo Object Rotation)
vec4 getRotatedPoint4D(float u, float v) {
    vec4 p = getRawPositionAuto(u, v);

    if (ubuf.u_omega != 0.0) p = rotateXW(p, ubuf.u_omega);
    if (ubuf.u_phi   != 0.0) p = rotateYW(p, ubuf.u_phi);
    if (ubuf.u_psi   != 0.0) p = rotateZW(p, ubuf.u_psi);

    return p;
}

// Estrae i due vettori ortonormali che formano il piano tangente in 4D
void getTangentPlane(float u, float v, out vec4 Tu, out vec4 Tv) {
    float eps = 0.0005;
    vec4 Tu_raw = getRotatedPoint4D(u + eps, v) - getRotatedPoint4D(u - eps, v);
    vec4 Tv_raw = getRotatedPoint4D(u, v + eps) - getRotatedPoint4D(u, v - eps);

    float lenU = length(Tu_raw);
    float lenV = length(Tv_raw);

    // FIX UNIVERSALE: Limite matematico (Soglia molto stretta, agisce solo sul vertice esatto)
    if (lenU < 1.0e-6 || lenV < 1.0e-6) {
        float tiny = 0.001;

        // Esploriamo la pendenza spostandoci microscopicamente su V (preserva la simmetria)
        Tu_raw = getRotatedPoint4D(u + eps, v + tiny) - getRotatedPoint4D(u - eps, v + tiny);
        Tv_raw = getRotatedPoint4D(u, v + tiny + eps) - getRotatedPoint4D(u, v + tiny - eps);

        lenU = length(Tu_raw);
        lenV = length(Tv_raw);

        // Fallback estremo sull'altro asse se la singolarità era invertita
        if (lenU < 1.0e-6 || lenV < 1.0e-6) {
            Tu_raw = getRotatedPoint4D(u + tiny + eps, v) - getRotatedPoint4D(u + tiny - eps, v);
            Tv_raw = getRotatedPoint4D(u + tiny, v + eps) - getRotatedPoint4D(u + tiny, v - eps);
            lenU = length(Tu_raw);
        }
    }

    Tu = (lenU > 1.0e-8) ? Tu_raw / lenU : vec4(1.0, 0.0, 0.0, 0.0);

    vec4 Tv_ortho = Tv_raw - Tu * dot(Tv_raw, Tu);
    float lenV_ortho = length(Tv_ortho);
    Tv = (lenV_ortho > 1.0e-8) ? Tv_ortho / lenV_ortho : vec4(0.0, 1.0, 0.0, 0.0);
}

vec4 calculate4DNormal_Slice(float u, float v) {
    float eps = 0.0005;
    vec4 P = getRotatedPoint4D(u, v);

    vec4 dPu = getRotatedPoint4D(u + eps, v) - getRotatedPoint4D(u - eps, v);
    vec4 dPv = getRotatedPoint4D(u, v + eps) - getRotatedPoint4D(u, v - eps);

    float lenU = length(dPu);
    float lenV = length(dPv);

    if (lenU < 1.0e-6 || lenV < 1.0e-6) {
        float tiny = 0.001;
        dPu = getRotatedPoint4D(u + eps, v + tiny) - getRotatedPoint4D(u - eps, v + tiny);
        dPv = getRotatedPoint4D(u, v + tiny + eps) - getRotatedPoint4D(u, v + tiny - eps);

        if (length(dPu) < 1.0e-6 || length(dPv) < 1.0e-6) {
            dPu = getRotatedPoint4D(u + tiny + eps, v) - getRotatedPoint4D(u + tiny - eps, v);
            dPv = getRotatedPoint4D(u + tiny, v + eps) - getRotatedPoint4D(u + tiny, v - eps);
        }
    }

    vec3 dU = length(dPu.xyz) > 1.0e-8 ? normalize(dPu.xyz) : vec3(1.0, 0.0, 0.0);
    vec3 dV = length(dPv.xyz) > 1.0e-8 ? normalize(dPv.xyz) : vec3(0.0, 1.0, 0.0);

    vec3 raw_N = cross(dU, dV);
    vec3 N_visual;
    if (dot(raw_N, raw_N) < 1.0e-8) {
        N_visual = vec3(0.0, 0.0, 1.0);
    } else {
        N_visual = normalize(raw_N);
    }

    float sliceFactor = 0.0;

    if (ubuf.u_hasExplicitW != 0) {
        // CASO 1: Volume esplicito (usa la casella w=h(u,v))
        float w_base = getExplicitVal(u, v);
        vec4 P_w = getRawPosition(u, v, w_base + eps);

        if (ubuf.u_omega != 0.0) P_w = rotateXW(P_w, ubuf.u_omega);
        if (ubuf.u_phi   != 0.0) P_w = rotateYW(P_w, ubuf.u_phi);
        if (ubuf.u_psi   != 0.0) P_w = rotateZW(P_w, ubuf.u_psi);

        vec4 dPw = P_w - P;
        vec3 dW = normalize(dPw.xyz);
        sliceFactor = dot(N_visual, dW);
    } else {
            // CASO 2: Superficie 4D pura
            float wTilt = length(vec2(dPu.w, dPv.w));
            float totalLength = length(dPu) + length(dPv) + 0.0001;

            sliceFactor = wTilt / totalLength;
            sliceFactor = smoothstep(0.0, 0.4, sliceFactor);
        }

    return vec4(N_visual, abs(sliceFactor));
}

vec3 calculateFinalNormal(float u, float v) {
    float eps = 0.001;
    vec3 pU_plus  = project4D(getRotatedPoint4D(u + eps, v));
    vec3 pU_minus = project4D(getRotatedPoint4D(u - eps, v));
    // ---> FIX: Moltiplichiamo per 500 per evitare l'underflow nel cross product!
    vec3 tangentU = (pU_plus - pU_minus) * 500.0;

    vec3 pV_plus  = project4D(getRotatedPoint4D(u, v + eps));
    vec3 pV_minus = project4D(getRotatedPoint4D(u, v - eps));
    vec3 tangentV = (pV_plus - pV_minus) * 500.0;

    vec3 N = cross(tangentU, tangentV);

    // Se la normale collassa (siamo sul polo / singolarità)
    if (dot(N, N) < 1.0e-8) {
        // Strategia: spostiamoci LONTANO dal polo (offset grande, ASIMMETRICO),
        // poi facciamo differenze in avanti standard nel nuovo punto.
        // Proviamo le 4 direzioni cardinali: la prima che produce una
        // normale non degenere vince.
        float pole_off = 0.05;
        float eps2     = 0.005;

        vec3 p0, pU, pV;

        // Tentativo 1: offset in +u
        p0 = project4D(getRotatedPoint4D(u + pole_off, v));
        pU = project4D(getRotatedPoint4D(u + pole_off + eps2, v));
        pV = project4D(getRotatedPoint4D(u + pole_off, v + eps2));
        N  = cross(pU - p0, pV - p0);

        // Tentativo 2: offset in -u
        if (dot(N, N) < 1.0e-8) {
            p0 = project4D(getRotatedPoint4D(u - pole_off, v));
            pU = project4D(getRotatedPoint4D(u - pole_off + eps2, v));
            pV = project4D(getRotatedPoint4D(u - pole_off, v + eps2));
            N  = cross(pU - p0, pV - p0);
        }

        // Tentativo 3: offset in +v
        if (dot(N, N) < 1.0e-8) {
            p0 = project4D(getRotatedPoint4D(u, v + pole_off));
            pU = project4D(getRotatedPoint4D(u + eps2, v + pole_off));
            pV = project4D(getRotatedPoint4D(u, v + pole_off + eps2));
            N  = cross(pU - p0, pV - p0);
        }

        // Tentativo 4: offset in -v
        if (dot(N, N) < 1.0e-8) {
            p0 = project4D(getRotatedPoint4D(u, v - pole_off));
            pU = project4D(getRotatedPoint4D(u + eps2, v - pole_off));
            pV = project4D(getRotatedPoint4D(u, v - pole_off + eps2));
            N  = cross(pU - p0, pV - p0);
        }

        if (dot(N, N) < 1.0e-8) return vec3(0.0, 0.0, 1.0);
    }

    return -normalize(N);
}

void main() {
    // --- BYPASS PER FLAT VIEW (TEXTURE PREVIEW) ---
    if (ubuf.u_isFlat != 0) {
        gl_Position = ubuf.u_mvpMatrix * vec4(vertex.xyz, 1.0);
        v_pos = vertex.xyz;
        v_normal = vec4(0.0, 0.0, 1.0, 0.0);
        v_texCoord = vec2(texCoord.x, 1.0 - texCoord.y);
        v_light4D = 1.0;
        return;
    }

    float u = vertex.x;
    float v = vertex.y;

    // 1. Calcolo Posizione 4D Reale
    vec4 pRot = getRotatedPoint4D(u, v);
    vec4 obs = ubuf.u_observerPos;

    if (abs(obs.w - pRot.w) < 0.1) {
        obs.w += 0.1;
    }

    // 2. Proiezione 4D -> 3D
    vec3 finalPos3D;
    if (ubuf.u_projMode == 0) {
        finalPos3D = pRot.xyz;
    } else if (ubuf.u_projMode == 2) {
        // --- STEREOGRAFICA SU IPERSFERA (S^3) ---
        finalPos3D = stereoS3(pRot);
    } else {
        // --- PROSPETTIVA CENTRALE ---
        float denom = obs.w - pRot.w;
        if (abs(denom) < 0.15) {
            denom = (denom >= 0.0) ? 0.15 : -0.15;
        }
        float wFactor = obs.w / denom;
        finalPos3D = obs.xyz + (pRot.xyz - obs.xyz) * wFactor;
    }

#ifdef CUSTOM_MESH
    // SE SIAMO NEL GEODESIC FLOW, PRENDIAMO LA NORMALE DAL BUFFER
    vec3 finalNormal3D = normal.xyz;
#else
    // ALTRIMENTI LA CALCOLIAMO CON LA MATEMATICA
    vec3 finalNormal3D = calculateFinalNormal(u, v);
#endif

    // 3. CALCOLO ILLUMINAZIONE 4D E SPECULARE
    v_spec4D = 0.0; // Valore di default

    if (ubuf.u_lightingMode == 1) { // DIRECTIONAL LIGHT (Sostituisce la vecchia Radial)
        vec4 P = pRot;
        vec4 Tu, Tv;
        getTangentPlane(u, v, Tu, Tv);

        // Direzione fissa della luce all'infinito (Il nostro "Sole 4D")
        vec4 lightDir4D = normalize(vec4(1.0, 1.0, 1.0, 0.5));
        vec4 viewDir4D = normalize(ubuf.u_cameraPos4D - P);

        // Diffuse 4D
        vec4 L_norm = lightDir4D - Tu * dot(lightDir4D, Tu) - Tv * dot(lightDir4D, Tv);
        float intensity = length(L_norm);

        // Illuminazione omogenea, niente attenuazione
        v_light4D = clamp(0.2 + 0.8 * intensity, 0.1, 1.5);

        // Specular 4D
        vec4 H = normalize(lightDir4D + viewDir4D);
        vec4 H_norm = H - Tu * dot(H, Tu) - Tv * dot(H, Tv);
        float specAngle = length(H_norm);

        // Riflesso
        v_spec4D = pow(specAngle, 32.0) * 0.5;

    } else if (ubuf.u_lightingMode == 2) { // OBSERVER
        vec4 P = pRot;
        vec4 Tu, Tv;
        getTangentPlane(u, v, Tu, Tv);

        vec4 toObserver = ubuf.u_observerPos - P;
        float dist = length(toObserver); // ECCO LA VARIABILE CHE MANCAVA!

        if (dist < 0.001) {
            v_light4D = 1.0;
            v_spec4D = 0.0;
        } else {
            vec4 lightDir4D = toObserver / dist;
            vec4 viewDir4D = normalize(ubuf.u_cameraPos4D - P);

            // Diffuse 4D
            vec4 L_norm = lightDir4D - Tu * dot(lightDir4D, Tu) - Tv * dot(lightDir4D, Tv);
            float intensity = length(L_norm);

            // 1. Attenuazione più morbida (Evita di sparare la luce al 300% quando vicino)
            float attenuation = 2.0 / (1.0 + 0.2 * dist + 0.02 * dist * dist);

            // 2. Clamp di sicurezza sulla luce diffusa
            v_light4D = clamp(0.2 + 0.8 * intensity * attenuation, 0.1, 1.5);

            // Specular 4D
            vec4 H = normalize(lightDir4D + viewDir4D);
            vec4 H_norm = H - Tu * dot(H, Tu) - Tv * dot(H, Tv);
            float specAngle = length(H_norm);

            // 3. Riduciamo l'esponente da 64 a 32
            v_spec4D = pow(specAngle, 32.0) * attenuation * 0.5;
        }

    } else if (ubuf.u_lightingMode == 3) { // SLICE
            vec4 P = pRot;
            vec4 sliceData = calculate4DNormal_Slice(u, v);
            vec3 N_local = sliceData.xyz;
            float sliceIntensity = sliceData.w;

            vec3 lightPos = ubuf.u_observerPos.xyz;
            vec3 L = normalize(lightPos - pRot.xyz);

            float halfLambert = dot(N_local, L) * 0.5 + 0.5;
            float diff = halfLambert * halfLambert;

            // Sfumiamo dolcemente anche l'alone luminoso 4D
            float rim = smoothstep(0.0, 1.0, 1.0 - sliceIntensity);
            rim = pow(rim, 2.0);

            // Mix finale molto più bilanciato e morbido
            v_light4D = 0.2 + 0.7 * diff + rim * 0.7;

            // Specular locale più elegante
            vec3 viewDir3D = normalize(ubuf.u_cameraPos4D.xyz - pRot.xyz);
            vec3 H = normalize(L + viewDir3D);
            float specAngle = max(dot(N_local, H), 0.0);
            v_spec4D = pow(specAngle, 48.0) * 0.4;
    } else {
        v_light4D = 1.0;
        v_spec4D = 0.0;
    }

    v_pos = vec3(ubuf.u_mvMatrix * vec4(finalPos3D, 1.0));
    v_normal = ubuf.u_mvMatrix * vec4(finalNormal3D, 0.0);
    v_texCoord = vec2(texCoord.x, 1.0 - texCoord.y);

    vec4 pos = ubuf.u_mvpMatrix * vec4(finalPos3D, 1.0);

    // >>> FIX SFARFALLIO (Z-Fighting 4D Tie-Breaker) <<<
    pos.z -= clamp(pRot.w, -1.0, 1.0) * 0.0005 * pos.w;

    pos += ubuf.u_dummyZero * 0.000001;
    gl_Position = pos;
}
