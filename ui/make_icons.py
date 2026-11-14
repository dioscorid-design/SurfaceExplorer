#!/usr/bin/env python3
"""
make_icons.py - Le icone del foglio di stile (uistylemanager.cpp): la spunta
delle checkbox e le frecce degli spinbox, in PNG a 1x, 2x e 3x.

Perche' PNG su file e non SVG in linea ("data:image/svg+xml;utf8,..."): il
foglio di stile di Qt non carica gli URL data:, quindi la spunta non c'era mai
stata (checkbox accesa = quadrato blu pieno). Le frecce erano triangoli fatti
coi bordi trasparenti, che con Qt 6.12 uscivano rettangoli pieni. Un SVG su
file funziona sul Mac, ma solo grazie al plugin qsvg dell'installazione di Qt:
il progetto non collega Qt Svg, e su iOS (Qt statico) non ci sarebbe. Le PNG
le legge QtGui da solo, e le varianti @2x/@3x le sceglie Qt secondo lo schermo.

Solo libreria standard: niente PIL. Ogni pixel e' campionato 8x8 volte
(antialiasing). Rilanciare dopo ogni modifica delle forme o dei colori:

    python3 ui/make_icons.py

e poi, se si aggiunge un file, update_resources.py (STATIC_FILES).
"""
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SUB = 8   # campioni per lato di ogni pixel


def write_png(path, w, h, rgba_rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rgba_rows)
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def render(path, w, h, scale, inside, rgb):
    """inside(x, y) in unita' logiche (0..w, 0..h): True se il punto e' coperto."""
    W, H = round(w * scale), round(h * scale)
    rows = []
    for py in range(H):
        row = []
        for px in range(W):
            hit = 0
            for sy in range(SUB):
                for sx in range(SUB):
                    x = (px + (sx + 0.5) / SUB) / scale
                    y = (py + (sy + 0.5) / SUB) / scale
                    hit += inside(x, y)
            a = round(255 * hit / (SUB * SUB))
            row += [rgb[0], rgb[1], rgb[2], a]
        rows.append(row)
    write_png(path, W, H, rows)


def triangle(a, b, c):
    def side(p, q, x, y):
        return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0])
    def inside(x, y):
        d1, d2, d3 = side(a, b, x, y), side(b, c, x, y), side(c, a, x, y)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))
    return inside


def stroke(points, width):
    """Spezzata con estremi e giunzioni tonde (stroke-linecap/linejoin round)."""
    r = width / 2
    def dist(x, y, p, q):
        vx, vy = q[0] - p[0], q[1] - p[1]
        t = max(0.0, min(1.0, ((x - p[0]) * vx + (y - p[1]) * vy) / (vx * vx + vy * vy)))
        return math.hypot(x - p[0] - t * vx, y - p[1] - t * vy)
    def inside(x, y):
        return any(dist(x, y, points[i], points[i + 1]) <= r for i in range(len(points) - 1))
    return inside


ON, OFF, WHITE = (0xE0, 0xE0, 0xE0), (0x66, 0x66, 0x66), (0xFF, 0xFF, 0xFF)
UP = triangle((0, 5), (4, 0), (8, 5))     # 8x5 logici, come i vecchi bordi 4+4 x 5
DOWN = triangle((0, 0), (8, 0), (4, 5))
CHECK = stroke([(3, 8.5), (6.5, 12), (13, 4)], 2.5)   # la spunta disegnata dal vecchio SVG

ICONS = [
    # nome, larghezza, altezza, forma, colore, scale
    ("spin-up", 8, 5, UP, ON, (1, 2)),
    ("spin-down", 8, 5, DOWN, ON, (1, 2)),
    ("spin-up-off", 8, 5, UP, OFF, (1, 2)),
    ("spin-down-off", 8, 5, DOWN, OFF, (1, 2)),
    ("check", 16, 16, CHECK, WHITE, (1, 2, 3)),   # 3x: le checkbox ci sono anche su iPhone
]

if __name__ == "__main__":
    for name, w, h, shape, rgb, scales in ICONS:
        for s in scales:
            suffix = "" if s == 1 else f"@{s}x"
            path = os.path.join(HERE, f"{name}{suffix}.png")
            render(path, w, h, s, shape, rgb)
            print(os.path.relpath(path, os.path.dirname(HERE)))
