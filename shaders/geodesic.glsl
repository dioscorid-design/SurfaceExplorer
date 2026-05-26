#version 450 core

// Configurazione locale dei thread
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) buffer OutputGrid {
    vec4 grid[];
};

// Tempo come uniform, niente più re-bake per frame ===
layout(std140, binding = 1) uniform TimeParams {
    float u_time;
};
#define t u_time
#define iTime u_time

// Costanti iniettate dal C++
const float uMin = /*%U_MIN%*/;
const float uMax = /*%U_MAX%*/;
const float vMin = /*%V_MIN%*/;
const float vMax = /*%V_MAX%*/;
const int numU = /*%NUM_U%*/;
const int numV = /*%NUM_V%*/;

// Definiamo un valore di errore sicuro che non richieda funzioni speciali
const float SAFE_ERR = 99999.0;

// Equazioni iniettate
float evalLambda(float U, float V, float W) { return /*%LAMBDA_EQ%*/; }
vec4 evalS(float U, float V, float W) { return vec4(/*%X_EQ%*/, /*%Y_EQ%*/, /*%Z_EQ%*/, /*%P_EQ%*/); }
vec3 getInitialPos(float u) { return vec3(/*%U_EQ%*/, /*%V_EQ%*/, /*%W_EQ%*/); }
vec3 getInitialVel(float u) { return vec3(/*%DU_EQ%*/, /*%DV_EQ%*/, /*%DW_EQ%*/); }

float getGammaConformal(vec4 S_ij, int k, int i, int j,
                        float g11, float g12, float g13, float g22, float g23, float g33,
                        float g_inv11, float g_inv12, float g_inv13, float g_inv22, float g_inv23, float g_inv33,
                        vec4 Su, vec4 Sv, vec4 Sw,
                        float Lu, float Lv, float Lw, float L_val,
                        float G1, float G2, float G3)
{
    // Calcolo Gamma originale
    float gamma_orig = 0.0;
    if (k == 1) gamma_orig = g_inv11 * dot(S_ij, Su) + g_inv12 * dot(S_ij, Sv) + g_inv13 * dot(S_ij, Sw);
    else if (k == 2) gamma_orig = g_inv12 * dot(S_ij, Su) + g_inv22 * dot(S_ij, Sv) + g_inv23 * dot(S_ij, Sw);
    else if (k == 3) gamma_orig = g_inv13 * dot(S_ij, Su) + g_inv23 * dot(S_ij, Sv) + g_inv33 * dot(S_ij, Sw);

    // Delta di Kronecker
    float d_ki = (k == i) ? 1.0 : 0.0;
    float d_kj = (k == j) ? 1.0 : 0.0;

    // Componenti Gradiente L
    float L_i = (i == 1) ? Lu : ((i == 2) ? Lv : Lw);
    float L_j = (j == 1) ? Lu : ((j == 2) ? Lv : Lw);

    // Selezione componente tensore metrico g_ij
    float g_ij_val = 0.0;
    if (i==1 && j==1) g_ij_val = g11;
    else if (i==2 && j==2) g_ij_val = g22;
    else if (i==3 && j==3) g_ij_val = g33;
    else if ((i==1 && j==2) || (i==2 && j==1)) g_ij_val = g12;
    else if ((i==1 && j==3) || (i==3 && j==1)) g_ij_val = g13;
    else if ((i==2 && j==3) || (i==3 && j==2)) g_ij_val = g23;

    // Selezione G_k
    float G_k = (k == 1) ? G1 : ((k == 2) ? G2 : G3);

    // Correzione Conforme
    float correction = (d_ki * L_j + d_kj * L_i - g_ij_val * G_k) / (2.0 * L_val);

    return gamma_orig + correction;
}

vec3 getAccelerations(vec3 p, vec3 vel) {
    float h = 0.05;

    // Gradiente del fattore conforme Lambda
    float L_val = evalLambda(p.x, p.y, p.z);
    if (L_val <= 1e-6) return vec3(SAFE_ERR);

    float Lu = (evalLambda(p.x + h, p.y, p.z) - evalLambda(p.x - h, p.y, p.z)) / (2.0 * h);
    float Lv = (evalLambda(p.x, p.y + h, p.z) - evalLambda(p.x, p.y - h, p.z)) / (2.0 * h);
    float Lw = (evalLambda(p.x, p.y, p.z + h) - evalLambda(p.x, p.y, p.z - h)) / (2.0 * h);

    // Derivate Prime (4D)
    vec4 Su = (evalS(p.x + h, p.y, p.z) - evalS(p.x - h, p.y, p.z)) / (2.0 * h);
    vec4 Sv = (evalS(p.x, p.y + h, p.z) - evalS(p.x, p.y - h, p.z)) / (2.0 * h);
    vec4 Sw = (evalS(p.x, p.y, p.z + h) - evalS(p.x, p.y, p.z - h)) / (2.0 * h);

    // Derivate Seconde Pure e Miste
    vec4 Suu = (evalS(p.x + h, p.y, p.z) - 2.0 * evalS(p.x, p.y, p.z) + evalS(p.x - h, p.y, p.z)) / (h * h);
    vec4 Svv = (evalS(p.x, p.y + h, p.z) - 2.0 * evalS(p.x, p.y, p.z) + evalS(p.x, p.y - h, p.z)) / (h * h);
    vec4 Sww = (evalS(p.x, p.y, p.z + h) - 2.0 * evalS(p.x, p.y, p.z) + evalS(p.x, p.y, p.z - h)) / (h * h);
    vec4 Suv = (evalS(p.x + h, p.y + h, p.z) - evalS(p.x + h, p.y - h, p.z) - evalS(p.x - h, p.y + h, p.z) + evalS(p.x - h, p.y - h, p.z)) / (4.0 * h * h);
    vec4 Suw = (evalS(p.x + h, p.y, p.z + h) - evalS(p.x + h, p.y, p.z - h) - evalS(p.x - h, p.y, p.z + h) + evalS(p.x - h, p.y, p.z - h)) / (4.0 * h * h);
    vec4 Svw = (evalS(p.x, p.y + h, p.z + h) - evalS(p.x, p.y + h, p.z - h) - evalS(p.x, p.y - h, p.z + h) + evalS(p.x, p.y - h, p.z - h)) / (4.0 * h * h);

    // Tensore Metrico g_ij
    float g11 = dot(Su, Su), g12 = dot(Su, Sv), g13 = dot(Su, Sw);
    float                    g22 = dot(Sv, Sv), g23 = dot(Sv, Sw);
    float                                       g33 = dot(Sw, Sw);

    float g_inv11 = 0.0, g_inv12 = 0.0, g_inv13 = 0.0;
    float g_inv22 = 0.0, g_inv23 = 0.0, g_inv33 = 0.0;

    // FALLBACK INTELLIGENTE 2D/3D
    if (g33 < 1e-6 && abs(g13) < 1e-6 && abs(g23) < 1e-6) {
        float det2D = g11 * g22 - g12 * g12;
        if (abs(det2D) < 1e-8) return vec3(SAFE_ERR);
        float invDet2D = 1.0 / det2D;
        g_inv11 = g22 * invDet2D;
        g_inv12 = -g12 * invDet2D;
        g_inv22 = g11 * invDet2D;
    } else {
        float det = g11*(g22*g33 - g23*g23) - g12*(g12*g33 - g13*g23) + g13*(g12*g23 - g13*g22);
        if (abs(det) < 1e-6) return vec3(SAFE_ERR);
        float invDet = 1.0 / det;
        g_inv11 = (g22*g33 - g23*g23) * invDet;
        g_inv12 = (g13*g23 - g12*g33) * invDet;
        g_inv13 = (g12*g23 - g13*g22) * invDet;
        g_inv22 = (g11*g33 - g13*g13) * invDet;
        g_inv23 = (g12*g13 - g11*g23) * invDet;
        g_inv33 = (g11*g22 - g12*g12) * invDet;
    }

    float G1 = g_inv11 * Lu + g_inv12 * Lv + g_inv13 * Lw;
    float G2 = g_inv12 * Lu + g_inv22 * Lv + g_inv23 * Lw;
    float G3 = g_inv13 * Lu + g_inv23 * Lv + g_inv33 * Lw;

    vec3 acc;
    for (int k = 1; k <= 3; ++k) {
        float GC_uu = getGammaConformal(Suu, k, 1, 1, g11, g12, g13, g22, g23, g33, g_inv11, g_inv12, g_inv13, g_inv22, g_inv23, g_inv33, Su, Sv, Sw, Lu, Lv, Lw, L_val, G1, G2, G3);
        float GC_vv = getGammaConformal(Svv, k, 2, 2, g11, g12, g13, g22, g23, g33, g_inv11, g_inv12, g_inv13, g_inv22, g_inv23, g_inv33, Su, Sv, Sw, Lu, Lv, Lw, L_val, G1, G2, G3);
        float GC_ww = getGammaConformal(Sww, k, 3, 3, g11, g12, g13, g22, g23, g33, g_inv11, g_inv12, g_inv13, g_inv22, g_inv23, g_inv33, Su, Sv, Sw, Lu, Lv, Lw, L_val, G1, G2, G3);
        float GC_uv = getGammaConformal(Suv, k, 1, 2, g11, g12, g13, g22, g23, g33, g_inv11, g_inv12, g_inv13, g_inv22, g_inv23, g_inv33, Su, Sv, Sw, Lu, Lv, Lw, L_val, G1, G2, G3);
        float GC_uw = getGammaConformal(Suw, k, 1, 3, g11, g12, g13, g22, g23, g33, g_inv11, g_inv12, g_inv13, g_inv22, g_inv23, g_inv33, Su, Sv, Sw, Lu, Lv, Lw, L_val, G1, G2, G3);
        float GC_vw = getGammaConformal(Svw, k, 2, 3, g11, g12, g13, g22, g23, g33, g_inv11, g_inv12, g_inv13, g_inv22, g_inv23, g_inv33, Su, Sv, Sw, Lu, Lv, Lw, L_val, G1, G2, G3);

        float a_k = -(GC_uu * vel.x * vel.x +
                      GC_vv * vel.y * vel.y +
                      GC_ww * vel.z * vel.z +
                      2.0 * GC_uv * vel.x * vel.y +
                      2.0 * GC_uw * vel.x * vel.z +
                      2.0 * GC_vw * vel.y * vel.z);

        if (k == 1) acc.x = a_k;
        else if (k == 2) acc.y = a_k;
        else acc.z = a_k;
    }

    if (length(acc) > 1000.0) return vec3(SAFE_ERR);
    return acc;
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i > uint(numU)) return;

    float actual_u = uMin + float(i) * (uMax - uMin) / float(numU);
    vec3 startPos = getInitialPos(actual_u);
    vec3 startVel = getInitialVel(actual_u);

    float dv = (numV > 0) ? (vMax - vMin) / float(numV) : 0.01;
    int j_split = 0;
    while (j_split < numV && (vMin + float(j_split) * dv) < 0.0) {
        j_split++;
    }

    uint stride = uint(numV + 1);
    grid[i * stride + uint(j_split)] = evalS(startPos.x, startPos.y, startPos.z);

    float domainExtent = max(max(abs(uMin), abs(uMax)), max(abs(vMin), abs(vMax)));
    float safeLim = max(50.0, domainExtent * 3.0);
    float max_safe_dt = 0.01;

    // ===================================================
    // INTEGRAZIONE AVANTI (Da j_split verso numV)
    // ===================================================
    vec3 p = startPos;
    vec3 v = startVel;
    bool crash = false;
    float current_v_time = 0.0;

    for(int j = j_split + 1; j <= numV; ++j) {
        float target_v_time = float(j - j_split) * dv; // Distanza percorsa
        float dt_total = target_v_time - current_v_time;

        // Calcolo Dinamico dei Sub-Steps
        int sub_steps = max(1, int(ceil(abs(dt_total) / max_safe_dt)));
        float dt_step = dt_total / float(sub_steps);

        const float SENTINEL_THRESHOLD = SAFE_ERR * 0.5;  // 49999.5
        const float VEL_LIMIT          = 1000.0;          // // <

        for (int k = 0; k < sub_steps; ++k) {
            if (crash) break;

            vec3 k1_v = getAccelerations(p, v);
            if (any(greaterThan(abs(k1_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k1_p = v;

            vec3 k2_v = getAccelerations(p + (k1_p * dt_step * 0.5), v + (k1_v * dt_step * 0.5));
            if (any(greaterThan(abs(k2_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k2_p = v + (k1_v * dt_step * 0.5);

            vec3 k3_v = getAccelerations(p + (k2_p * dt_step * 0.5), v + (k2_v * dt_step * 0.5));
            if (any(greaterThan(abs(k3_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k3_p = v + (k2_v * dt_step * 0.5);

            vec3 k4_v = getAccelerations(p + (k3_p * dt_step), v + (k3_v * dt_step));
            if (any(greaterThan(abs(k4_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k4_p = v + (k3_v * dt_step);

            p += (k1_p + 2.0 * k2_p + 2.0 * k3_p + k4_p) * (dt_step / 6.0);
            v += (k1_v + 2.0 * k2_v + 2.0 * k3_v + k4_v) * (dt_step / 6.0);

            if (any(isnan(p)) || any(isinf(p)) || length(p) > safeLim ||
                any(isnan(v)) || any(isinf(v)) || length(v) > VEL_LIMIT) crash = true;       // <
        }

        current_v_time = target_v_time;
        grid[i * stride + uint(j)] = crash ? vec4(SAFE_ERR) : evalS(p.x, p.y, p.z);
    }

    // ===================================================
    // INTEGRAZIONE INDIETRO (Da j_split verso 0)
    // ===================================================
    p = startPos;
    v = -startVel; // Inversione della velocità per calcolare il passato
    crash = false;
    current_v_time = 0.0;

    for(int j = j_split - 1; j >= 0; --j) {
        float target_v_time = float(j_split - j) * dv; // Distanza percorsa (sempre positiva grazie a -startVel)
        float dt_total = target_v_time - current_v_time;

        // Calcolo Dinamico dei Sub-Steps
        int sub_steps = max(1, int(ceil(abs(dt_total) / max_safe_dt)));
        float dt_step = dt_total / float(sub_steps);

        const float SENTINEL_THRESHOLD = SAFE_ERR * 0.5;  // 49999.5
        const float VEL_LIMIT          = 1000.0;          // // <

        for (int k = 0; k < sub_steps; ++k) {
            if (crash) break;

            vec3 k1_v = getAccelerations(p, v);
            if (any(greaterThan(abs(k1_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k1_p = v;

            vec3 k2_v = getAccelerations(p + (k1_p * dt_step * 0.5), v + (k1_v * dt_step * 0.5));
            if (any(greaterThan(abs(k2_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k2_p = v + (k1_v * dt_step * 0.5);

            vec3 k3_v = getAccelerations(p + (k2_p * dt_step * 0.5), v + (k2_v * dt_step * 0.5));
            if (any(greaterThan(abs(k3_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k3_p = v + (k2_v * dt_step * 0.5);

            vec3 k4_v = getAccelerations(p + (k3_p * dt_step), v + (k3_v * dt_step));
            if (any(greaterThan(abs(k4_v), vec3(SENTINEL_THRESHOLD)))) { crash = true; break; }  // <
            vec3 k4_p = v + (k3_v * dt_step);

            p += (k1_p + 2.0 * k2_p + 2.0 * k3_p + k4_p) * (dt_step / 6.0);
            v += (k1_v + 2.0 * k2_v + 2.0 * k3_v + k4_v) * (dt_step / 6.0);

            if (any(isnan(p)) || any(isinf(p)) || length(p) > safeLim ||
                any(isnan(v)) || any(isinf(v)) || length(v) > VEL_LIMIT) crash = true;       // <
        }

        current_v_time = target_v_time;
        grid[i * stride + uint(j)] = crash ? vec4(SAFE_ERR) : evalS(p.x, p.y, p.z);
    }
}
