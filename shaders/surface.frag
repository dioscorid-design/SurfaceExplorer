#version 440

// --- INPUT DAL VERTEX SHADER ---
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec4 v_normal;
layout(location = 2) in vec2 v_texCoord;
layout(location = 3) in float v_light4D;
layout(location = 4) in float v_spec4D;

// --- OUTPUT ---
layout(location = 0) out vec4 FragColor;

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
} ubuf;

// --- PLACEHOLDER PER CODICE TEXTURE CUSTOM ---
%CUSTOM_CODE%

// --- MAIN (LOGICA DI ILLUMINAZIONE CALIBRATA) ---
void main() {

    if (ubuf.u_isFlat != 0) {
        vec3 texColor = getCustomColor(v_texCoord);
        FragColor = vec4(texColor, 1.0);
        return;
    }

    // Scompattiamo i parametri matematici
    // Scompattiamo i parametri matematici
    float A = ubuf.u_mathParams.x;
    float B = ubuf.u_mathParams.y;
    float C = ubuf.u_mathParams.z;
    float D = ubuf.u_mathParams2.x;
    float E = ubuf.u_mathParams2.y;
    float F = ubuf.u_mathParams2.z;
    float s = ubuf.u_mathParams.w;

    // Se siamo in modalità Wireframe (2) o Bordi (5), usa colore piatto senza luci
    if (ubuf.u_renderMode == 2 || ubuf.u_renderMode == 5) {
        FragColor = vec4(ubuf.color, ubuf.alpha);
        return;
    }

    // --- 1. SETUP VETTORI CON SAFETY CHECKS ---
    vec3 N;
    float lenN = length(v_normal.xyz);
    if (lenN > 0.0001) {
        N = normalize(v_normal.xyz);
    } else {
        N = vec3(0.0, 0.0, 1.0);
    }

    vec3 V;
    float lenPos = length(v_pos);
    if (lenPos > 0.0001) {
        V = normalize(-v_pos);     // Vista
    } else {
        V = vec3(0.0, 0.0, 1.0);
    }

    vec3 L = vec3(0.0, 0.0, 1.0);   // Luce frontale
    vec3 H = normalize(L + V);      // Vettore mediano

    // --- 2. ILLUMINAZIONE DIFFUSA ---
    float diff = abs(dot(N, L));
    diff = 0.3 + 0.7 * diff;

    // --- 3. ILLUMINAZIONE SPECULARE (PHONG) CALIBRATA ---
    float spec = 0.0;
    if (ubuf.useSpecular != 0) {
        float specAngle = abs(dot(N, H));
        specAngle = clamp(specAngle, 0.0, 1.0);
        spec = pow(specAngle, 64.0);
        spec *= 0.3;
    }

    // --- 4. MIX ILLUMINAZIONE 4D E SPECULARE ----
    // Applica l'intensità globale alla luce diffusa base
    float lightingMix = diff * ubuf.u_lightIntensity;

    if (ubuf.useSpecular != 0) {
        // Applica l'intensità globale anche alla componente speculare
        spec *= ubuf.u_lightIntensity;
    }

    if (ubuf.u_lightingMode > 0) {
        lightingMix *= v_light4D;

        spec *= 0.2;
        // Mantiene un lieve riflesso 3D per percepire il volume globale

        // Aggiunge la luce speculare nativa del 4D se il tasto Phong è premuto
        if (ubuf.useSpecular != 0) {
            // Anche la speculare 4D è soggetta allo slider della luce!
            spec += v_spec4D * 0.8 * ubuf.u_lightIntensity;
        }
    }

    // --- 5. COMPOSIZIONE COLORE ---
    vec3 finalRGB = ubuf.color;
    if (ubuf.useTexture != 0) {
        vec4 tex = vec4(getCustomColor(v_texCoord), 1.0);
        finalRGB *= tex.rgb;
    }

    finalRGB *= lightingMix;
    finalRGB += vec3(spec);

    // ---> FIX: TONE MAPPING SOLO PER LUCI 4D <---
    // Applichiamo la protezione anti-bruciatura solo se siamo in Radial, Observer o Slice (u_lightingMode > 0)
    // così la modalità 3D base torna alla sua luminosità originale brillante!
    if (ubuf.u_lightingMode > 0) {
        finalRGB = vec3(1.0) - exp(-finalRGB * 1.5); // Portato a 1.5 per mantenere un po' più di luminosità
    }

    FragColor = vec4(finalRGB, ubuf.alpha);
}
