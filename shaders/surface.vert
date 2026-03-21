#ifdef GL_ES
// Se siamo su iPad/iOS
precision highp float;
precision highp int;

// Se siamo su Desktop
#define lowp
#define mediump
#define highp
#endif



// INPUT
layout(location = 0) in vec3 vertex;
layout(location = 1) in vec4 normal;
layout(location = 2) in vec2 texCoord;

// OUTPUT
out vec3 v_pos;
out vec4 v_normal;
out vec2 v_texCoord;
out float v_light4D;
out float v_spec4D;

// --- DUMMY PER LOCATION 0 (CRUCIALE) ---
uniform vec4 u_dummyZero;

// --- UNIFORM RINOMINATE (Devono coincidere col main) ---
uniform mat4 u_mvpMatrix;      // EX matrix
uniform mat4 u_mvMatrix;       // EX modelView
uniform mat4 u_mMatrix;        // EX model

// --- NUOVO FLAG PER IL FIX 2D ---
uniform bool u_isFlat;

// UNIFORM 4D
uniform float u_omega;
uniform float u_phi;
uniform float u_psi;
uniform int u_projMode;

// ILLUMINAZIONE 4D
uniform int u_lightingMode;    // 0=Standard, 1=Radial, 2=Observer
uniform vec4 u_observerPos;
uniform vec4 u_cameraPos4D;

// UNIFORM MATEMATICI (Vettore Unico)
uniform vec4 u_mathParams;
uniform vec4 u_mathParams2;
uniform float u_time;

// COSTANTI
uniform float u_min;
uniform float u_max;
uniform float v_min;
uniform float v_max;
uniform bool u_hasExplicitW;

const float PI = 3.14159265359;
const float pi = 3.14159265359;

float sq(float x) { return x*x; }

// --- AGGIUNTA NOISE 3D ---
float sys_hash(float n) { return fract(sin(n) * 1e4); }
float sys_hash(vec3 p) { return fract(1e4 * sin(17.0 * p.x + p.y * 0.1 + p.z * 0.01) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }

float sys_noise(vec3 x) {
    const vec3 step = vec3(110, 241, 171);
    vec3 i = floor(x);
    vec3 f = fract(x);
    float n = dot(i, step);
    vec3 u = f * f * (3.0 - 2.0 * f);

    // Aggiorna TUTTE le chiamate a hash() in sys_hash() qui sotto
    return mix(mix(mix( sys_hash(n + dot(step, vec3(0, 0, 0))), sys_hash(n + dot(step, vec3(1, 0, 0))), u.x),
                   mix( sys_hash(n + dot(step, vec3(0, 1, 0))), sys_hash(n + dot(step, vec3(1, 1, 0))), u.x), u.y),
               mix(mix( sys_hash(n + dot(step, vec3(0, 0, 1))), sys_hash(n + dot(step, vec3(1, 0, 1))), u.x),
                   mix( sys_hash(n + dot(step, vec3(0, 1, 1))), sys_hash(n + dot(step, vec3(1, 1, 1))), u.x), u.y), u.z);
}

float NoiseW(float x, float y, float z, float octaves_in, float lacunarity, float gain) {
    // Convertiamo il float in int per il ciclo
    int octaves = int(octaves_in);

    float total = 0.0;
    float frequency = 1.0;
    float amplitude = 1.0;
    float maxAmplitude = 0.0;
    vec3 p = vec3(x, y, z);

    for (int i = 0; i < octaves; i++) {
        total += sys_noise(p * frequency) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    if (maxAmplitude > 0.0) return total / maxAmplitude;
    return total;
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

// --- PROIEZIONE 4D -> 3D CON OSSERVATORE ---
vec3 project4D(vec4 p) {
    if (u_projMode == 0) {
        return p.xyz;
    }
    else if (u_projMode == 2) {
        // --- STEREOGRAFICA SU IPERSFERA (S^3) ---
        float r = length(p);
        if (r < 0.0001) return vec3(0.0);

        vec4 pNorm = p / r;
        float denom = 1.0 - pNorm.w;
        if (denom < 0.05) denom = 0.05;

        return pNorm.xyz * (r / denom);
    }

    vec4 obs = u_observerPos;
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
    float A = u_mathParams.x;
    float B = u_mathParams.y;
    float C = u_mathParams.z;
    float D = u_mathParams2.x;
    float E = u_mathParams2.y;
    float F = u_mathParams2.z;
    float s = u_mathParams.w;
    float t = u_time;

    float x = %X_EQ%;
    float y = %Y_EQ%;
    float z = %Z_EQ%;
    float p = %W_EQ%;
    return vec4(x, y, z, p);
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
        w = u_hasExplicitW ? getExplicitVal(u, v) : 0.0;
    }

    return getRawPosition(u, v, w);
}

// Funzione completa (Solo Object Rotation)
vec4 getRotatedPoint4D(float u, float v) {
    vec4 p = getRawPositionAuto(u, v);

    if (u_omega != 0.0) p = rotateXW(p, u_omega);
    if (u_phi   != 0.0) p = rotateYW(p, u_phi);
    if (u_psi   != 0.0) p = rotateZW(p, u_psi);

    return p;
}

// Estrae i due vettori ortonormali che formano il piano tangente in 4D
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

    if (u_hasExplicitW) {
        // CASO 1: Volume esplicito (usa la casella w=h(u,v))
        float w_base = getExplicitVal(u, v);
        vec4 P_w = getRawPosition(u, v, w_base + eps);

        if (u_omega != 0.0) P_w = rotateXW(P_w, u_omega);
        if (u_phi   != 0.0) P_w = rotateYW(P_w, u_phi);
        if (u_psi   != 0.0) P_w = rotateZW(P_w, u_psi);

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
    vec3 tangentU = pU_plus - pU_minus;

    vec3 pV_plus  = project4D(getRotatedPoint4D(u, v + eps));
    vec3 pV_minus = project4D(getRotatedPoint4D(u, v - eps));
    vec3 tangentV = pV_plus - pV_minus;

    vec3 N = cross(tangentU, tangentV);

    // Se la normale collassa (siamo sul polo / singolarità)
    if (dot(N, N) < 1.0e-12) {
        // FIX ARTEFATTO: Ci allontaniamo di un millimetro dal polo in diagonale.
        // Essendo una superficie continua, la normale "appena fuori" dal buco
        // è un'approssimazione geometricamente perfetta per tappare la singolarità.
        float offset = 0.01;
        vec3 p1 = project4D(getRotatedPoint4D(u + offset, v + offset));
        vec3 p2 = project4D(getRotatedPoint4D(u - offset, v + offset));
        vec3 p3 = project4D(getRotatedPoint4D(u, v - offset));

        N = cross(p1 - p3, p2 - p3);

        // Fallback estremo di sicurezza
        if (dot(N, N) < 1.0e-12) return vec3(0.0, 0.0, 1.0);
    }

    return -normalize(N);
}

void main() {
    // --- BYPASS PER FLAT VIEW (TEXTURE PREVIEW) ---
    if (u_isFlat) {
        gl_Position = u_mvpMatrix * vec4(vertex, 1.0);
        v_pos = vertex;
        v_normal = vec4(0.0, 0.0, 1.0, 0.0);
        v_texCoord = texCoord;
        v_light4D = 1.0;
        return;
    }

    float u = vertex.x;
    float v = vertex.y;

    // 1. Calcolo Posizione 4D Reale
    vec4 pRot = getRotatedPoint4D(u, v);
    vec4 obs = u_observerPos;

    if (abs(obs.w - pRot.w) < 0.1) {
        obs.w += 0.1;
    }

    // 2. Proiezione 4D -> 3D
    vec3 finalPos3D;
    if (u_projMode == 0) {
        finalPos3D = pRot.xyz;
    } else if (u_projMode == 2) {
        // --- STEREOGRAFICA SU IPERSFERA (S^3) ---
        float r = length(pRot);
        if (r < 0.0001) {
            finalPos3D = vec3(0.0);
        } else {
            vec4 pNorm = pRot / r;
            float denom = 1.0 - pNorm.w;
            if (denom < 0.05) denom = 0.05;
            finalPos3D = pNorm.xyz * (r / denom);
        }
    } else {
        // --- PROSPETTIVA CENTRALE ---
        float denom = obs.w - pRot.w;
        if (abs(denom) < 0.15) {
            denom = (denom >= 0.0) ? 0.15 : -0.15;
        }
        float wFactor = obs.w / denom;
        finalPos3D = obs.xyz + (pRot.xyz - obs.xyz) * wFactor;
    }

    vec3 finalNormal3D = calculateFinalNormal(u, v);

    // 3. CALCOLO ILLUMINAZIONE 4D E SPECULARE
    v_spec4D = 0.0; // Valore di default

    if (u_lightingMode == 1) { // DIRECTIONAL LIGHT (Sostituisce la vecchia Radial)
        vec4 P = pRot;
        vec4 Tu, Tv;
        getTangentPlane(u, v, Tu, Tv);

        // Direzione fissa della luce all'infinito (Il nostro "Sole 4D")
        vec4 lightDir4D = normalize(vec4(1.0, 1.0, 1.0, 0.5));
        vec4 viewDir4D = normalize(u_cameraPos4D - P);

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

    } else if (u_lightingMode == 2) { // OBSERVER
        vec4 P = pRot;
        vec4 Tu, Tv;
        getTangentPlane(u, v, Tu, Tv);

        vec4 toObserver = u_observerPos - P;
        float dist = length(toObserver); // ECCO LA VARIABILE CHE MANCAVA!

        if (dist < 0.001) {
            v_light4D = 1.0;
            v_spec4D = 0.0;
        } else {
            vec4 lightDir4D = toObserver / dist;
            vec4 viewDir4D = normalize(u_cameraPos4D - P);

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

            // 3. Riduciamo l'esponente da 64 a 32 per un riflesso più largo e meno duro
            // e dimezziamo la forza totale (* 0.5) per non sovrastare il colore
            v_spec4D = pow(specAngle, 32.0) * attenuation * 0.5;
        }

    } else if (u_lightingMode == 3) { // SLICE
            vec4 P = pRot;
            vec4 sliceData = calculate4DNormal_Slice(u, v);
            vec3 N_local = sliceData.xyz;
            float sliceIntensity = sliceData.w;

            vec3 lightPos = u_observerPos.xyz;
            vec3 L = normalize(lightPos - pRot.xyz);

            float halfLambert = dot(N_local, L) * 0.5 + 0.5;
            float diff = halfLambert * halfLambert;

            // Sfumiamo dolcemente anche l'alone luminoso 4D
            float rim = smoothstep(0.0, 1.0, 1.0 - sliceIntensity);
            rim = pow(rim, 2.0);

            // Mix finale molto più bilanciato e morbido
            v_light4D = 0.2 + 0.7 * diff + rim * 0.7;

            // Specular locale più elegante
            vec3 viewDir3D = normalize(u_cameraPos4D.xyz - pRot.xyz);
            vec3 H = normalize(L + viewDir3D);
            float specAngle = max(dot(N_local, H), 0.0);
            v_spec4D = pow(specAngle, 48.0) * 0.4;
    } else {
        v_light4D = 1.0;
        v_spec4D = 0.0;
    }

    v_pos = vec3(u_mvMatrix * vec4(finalPos3D, 1.0));
    v_normal = u_mvMatrix * vec4(finalNormal3D, 0.0);
    v_texCoord = texCoord;

    vec4 pos = u_mvpMatrix * vec4(finalPos3D, 1.0);

    // >>> FIX SFARFALLIO (Z-Fighting 4D Tie-Breaker) <<<
    pos.z -= clamp(pRot.w, -1.0, 1.0) * 0.0005 * pos.w;

    pos += u_dummyZero * 0.000001;
    gl_Position = pos;
}
