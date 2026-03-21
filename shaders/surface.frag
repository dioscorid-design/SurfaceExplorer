
#ifdef GL_ES
// Se siamo su iPad/iOS
precision highp float;
precision highp int;
#else
// Se siamo su Desktop
#define lowp
#define mediump
#define highp
#endif



// --- INPUT DAL VERTEX SHADER ---
in vec3 v_pos;
in vec4 v_normal;
in vec2 v_texCoord;
in float v_light4D;
in float v_spec4D;

// --- OUTPUT ---
out vec4 FragColor;

// --- UNIFORMS (Variabili Globali) ---
uniform vec3 color;
uniform float alpha;
uniform float u_lightIntensity;
uniform bool useTexture;
uniform bool useSpecular;
uniform sampler2D textureSampler;
uniform int u_renderMode;      // 0=Solid, 1=Phong, 2=Wireframe
uniform int u_lightingMode;    // 0=3D, 1=Radial, 2=Observer

uniform bool u_isFlat;

// Uniforms per Texture Procedurali e Frattali
uniform float u_zoom;
uniform float u_rotation;
uniform vec2 u_center;
uniform vec3 u_col1;
uniform vec3 u_col2;

// Uniforms Matematici
uniform vec4 u_mathParams;
uniform vec4 u_mathParams2;
uniform vec4 u_dummyZero;

uniform float u_min;
uniform float u_max;
uniform float v_min;
uniform float v_max;

// --- FUNZIONI DI UTILITÀ (Noise) ---
// Rinominate in 'sys_' per lasciare 'hash' e 'noise' libere per gli script utente
float sys_hash(float n) { return fract(sin(n) * 1e4); }
float sys_hash(vec2 p) { return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }

float sys_noise(float x) {
    float i = floor(x);
    float f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(sys_hash(i), sys_hash(i + 1.0), u); // Usa sys_hash
}

float sys_noise(vec2 x) {
    vec2 i = floor(x);
    vec2 f = fract(x);
    float a = sys_hash(i);                 // Usa sys_hash
    float b = sys_hash(i + vec2(1.0, 0.0));
    float c = sys_hash(i + vec2(0.0, 1.0));
    float d = sys_hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// --- PLACEHOLDER PER CODICE TEXTURE CUSTOM ---
%CUSTOM_CODE%

// --- MAIN (LOGICA DI ILLUMINAZIONE CALIBRATA) ---
void main() {

    if (u_isFlat) {
        vec3 texColor = getCustomColor(v_texCoord);
        FragColor = vec4(texColor, 1.0);
        return;
    }

    // Scompattiamo i parametri matematici
    float A = u_mathParams.x;
    float B = u_mathParams.y;
    float C = u_mathParams.z;
    float D = u_mathParams2.x;
     float E = u_mathParams2.y;
    float F = u_mathParams2.z;
    float s = u_mathParams.w;

    // Se siamo in modalità Wireframe (2) o Bordi (5), usa colore piatto senza luci
    if (u_renderMode == 2 || u_renderMode == 5) {
        FragColor = vec4(color, alpha);
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
    if (useSpecular) {
        float specAngle = abs(dot(N, H));
        specAngle = clamp(specAngle, 0.0, 1.0);
        spec = pow(specAngle, 64.0);
        spec *= 0.3;
    }

    // --- 4. MIX ILLUMINAZIONE 4D E SPECULARE ----
    // Applica l'intensità globale alla luce diffusa base
    float lightingMix = diff * u_lightIntensity;

    if (useSpecular) {
        // Applica l'intensità globale anche alla componente speculare
        spec *= u_lightIntensity;
    }

    if (u_lightingMode > 0) {
        // Se c'è una luce 4D attiva, la usiamo come "potenziometro" per moltiplicare
        // la luce della scena, illuminandola o scurendola senza perdere le ombre
        lightingMix *= v_light4D;

        spec *= 0.2;
        // Mantiene un lieve riflesso 3D per percepire il volume globale

        // Aggiunge la luce speculare nativa del 4D se il tasto Phong è premuto
        if (useSpecular) {
            // Anche la speculare 4D è soggetta allo slider della luce!
            spec += v_spec4D * 0.8 * u_lightIntensity;
        }
    }

    // --- 5. COMPOSIZIONE COLORE ---
    vec3 finalRGB = color;
    if (useTexture) {
        vec4 tex = vec4(getCustomColor(v_texCoord), 1.0);
        finalRGB *= tex.rgb;
    }

    finalRGB *= lightingMix;
    finalRGB += vec3(spec);

    // ---> FIX: TONE MAPPING SOLO PER LUCI 4D <---
    // Applichiamo la protezione anti-bruciatura solo se siamo in Radial, Observer o Slice (u_lightingMode > 0)
    // così la modalità 3D base torna alla sua luminosità originale brillante!
    if (u_lightingMode > 0) {
        finalRGB = vec3(1.0) - exp(-finalRGB * 1.5); // Portato a 1.5 per mantenere un po' più di luminosità
    }

    FragColor = vec4(finalRGB, alpha);
}
