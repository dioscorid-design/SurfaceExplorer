// =========================================================
// LIBRERIA FUNZIONI GLOBALI (Disponibili in tutti gli script)
// =========================================================

const float PI  = 3.14159265359;
const float pi  = 3.14159265359;
const float TAU = 6.28318530718;
const float tau = 6.28318530718;
const float e   = 2.71828182846;

// --- HASH FUNCTIONS ---
float sys_hash(float n) {
    return fract(sin(n) * 1e4);
}

float sys_hash(vec2 p) {
    return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

float sys_hash(vec3 p) {
    return fract(1e4 * sin(17.0 * p.x + p.y * 0.1 + p.z * 0.01) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

// --- NOISE FUNCTIONS ---
float sys_noise(float x) {
    float i = floor(x);
    float f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(sys_hash(i), sys_hash(i + 1.0), u);
}

float sys_noise(vec2 x) {
    vec2 i = floor(x);
    vec2 f = fract(x);
    float a = sys_hash(i);
    float b = sys_hash(i + vec2(1.0, 0.0));
    float c = sys_hash(i + vec2(0.0, 1.0));
    float d = sys_hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float sys_noise(vec3 x) {
    const vec3 step = vec3(110, 241, 171);
    vec3 i = floor(x);
    vec3 f = fract(x);
    float n = dot(i, step);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix( sys_hash(n + dot(step, vec3(0, 0, 0))), sys_hash(n + dot(step, vec3(1, 0, 0))), u.x),
                   mix( sys_hash(n + dot(step, vec3(0, 1, 0))), sys_hash(n + dot(step, vec3(1, 1, 0))), u.x), u.y),
               mix(mix( sys_hash(n + dot(step, vec3(0, 0, 1))), sys_hash(n + dot(step, vec3(1, 0, 1))), u.x),
                   mix( sys_hash(n + dot(step, vec3(0, 1, 1))), sys_hash(n + dot(step, vec3(1, 1, 1))), u.x), u.y), u.z);
}

// --- FRACTAL NOISE (1D/2D/3D to 1D) ---
float NoiseW(float x, float y, float z, float octaves_in, float lacunarity, float gain) {
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
