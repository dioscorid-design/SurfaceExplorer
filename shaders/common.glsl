// --- MATEMATICA CONDIVISA (Rotazione + Zoom + Pan) ---
// Usiamo nomi con _underscore per non creare conflitti con le variabili utente
float _rad = radians(u_rotation);
float _c = cos(_rad);
float _s = sin(_rad);

vec2 _centered = raw_uv - 0.5;
// Rotazione standard 2D
vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;

float _scale = pow(2.0, -u_zoom);
vec2 _shift = u_center * 0.5;

// Trasformazione Finale
vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;

// --- VARIABILI DI COMODO PER UTENTE (Legacy Support) ---
float u = uv.x;
float v = uv.y;

float x = uv.x * 1000.0;
float y = uv.y * 1000.0;
