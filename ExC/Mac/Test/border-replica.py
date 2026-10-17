#!/usr/bin/env python3
#
# border-replica.py - Replica in float32 della terza e quarta passata dei lati
# coperti del bordo (GLWidget::buildSharedEdgePipeline, glwidget.cpp: test dei
# campioni e pulizia dei tratti corti), sui dati che l'audit rilegge dalla GPU.
#
# L'audit (./ExC/Mac/Test/scenario-test.sh --scenario-only border-audit[:filtro])
# lascia per ogni superficie border-audit/<percorso>.bin: tabella delle parti,
# flag dei lati coperti calcolati dalla GPU e campioni (lati + griglia 64 x 64
# dell'interno). Questo script rifa' il test dei lati coperti sugli stessi
# campioni e confronta il risultato coi flag della GPU, campione per campione:
#  - se differiscono, la replica e lo shader non dicono la stessa cosa (va
#    sistemato uno dei due prima di fidarsi di qualunque esperimento);
#  - se coincidono, la replica e' il banco di prova per cambiare la regola:
#    si modifica qui, si guarda l'effetto su TUTTA la libreria in un colpo, e
#    solo dopo si porta nel GLSL (le simulazioni in float64 non vedevano errori
#    che il float32 della GPU fa: replicare sempre in float32).
#
# Uso:
#   python3 -I ExC/Mac/Test/border-replica.py <file.bin | cartella border-audit>
#   DEBUG=parte,lato,campione python3 -I ... file.bin   chi copre quel campione
# Lati: 0 v = v_min, 1 v = v_max, 2 u = u_min, 3 u = u_max; campioni 0..256.
# Per un .bin delle 25 patch del Calabi-Yau servono alcuni minuti.

import glob
import os
import struct
import sys

import numpy as np

f32 = np.float32
DEBUG = tuple(int(x) for x in os.environ['DEBUG'].split(',')) if os.environ.get('DEBUG') else None
NAMES = ['v=vmin', 'v=vmax', 'u=umin', 'u=umax']


def load(path):
    b = open(path, 'rb').read()
    magic, count, N, G = struct.unpack_from('<4i', b, 0)
    assert magic == 0x41424553, 'non e\' un file dell\'audit del bordo'
    o = 16
    table = np.frombuffer(b, dtype='<f4', count=count * 8, offset=o).reshape(count, 2, 4)
    o += count * 32
    flags = np.frombuffer(b, dtype='u1', count=count * 4 * (N + 1), offset=o).reshape(count, 4, N + 1)
    o += count * 4 * (N + 1)
    pv = 8 * (N + 1) + (G + 1) * (G + 1)
    s = np.frombuffer(b, dtype='<f4', count=count * pv * 4, offset=o).reshape(count, pv, 4)
    edges = s[:, :8 * (N + 1)].reshape(count, 4, N + 1, 2, 4)   # [parte, lato, m, (punto, appena dentro)]
    grid = s[:, 8 * (N + 1):].reshape(count, G + 1, G + 1, 4)
    return dict(count=count, N=N, G=G, table=table, flags=flags.astype(bool), edges=edges, grid=grid)


def tri_dist(P, a, b, c):
    """pointTriangle dello shader, vettoriale: distanza e (s, t) del punto piu' vicino."""
    e0 = b - a; e1 = c - a; v = P - a
    m00 = (e0 * e0).sum(-1); m01 = (e0 * e1).sum(-1); m11 = (e1 * e1).sum(-1)
    r0 = (v * e0).sum(-1); r1 = (v * e1).sum(-1)
    det = m00 * m11 - m01 * m01
    ok = np.abs(det) > 1e-30
    sdet = np.where(ok, det, f32(1))
    s = (r0 * m11 - r1 * m01) / sdet
    t = (r1 * m00 - r0 * m01) / sdet
    inside = ok & (s >= 0) & (t >= 0) & (s + t <= 1)
    din = np.linalg.norm(a + s[..., None] * e0 + t[..., None] * e1 - P, axis=-1)
    best = np.full(din.shape, np.inf, dtype=f32)
    bs = np.zeros(din.shape, f32); bt = np.zeros(din.shape, f32)
    for (p0, p1, kind) in ((a, b, 0), (b, c, 1), (c, a, 2)):
        d = p1 - p0
        x = np.clip(((P - p0) * d).sum(-1) / np.maximum((d * d).sum(-1), 1e-30), 0, 1)
        dd = np.linalg.norm(p0 + x[..., None] * d - P, axis=-1)
        if kind == 0: ss, tt = x, np.zeros_like(x)
        elif kind == 1: ss, tt = 1 - x, x
        else: ss, tt = np.zeros_like(x), 1 - x
        m = dd < best
        best = np.where(m, dd, best); bs = np.where(m, ss, bs); bt = np.where(m, tt, bt)
    best = np.where(inside, din, best)
    bs = np.where(inside, s, bs); bt = np.where(inside, t, bt)
    return best.astype(f32), bs, bt


def bilerp(grid, g):
    G = grid.shape[0] - 1
    gi = np.clip(np.floor(g[..., 0]).astype(int), 0, G - 1)
    gj = np.clip(np.floor(g[..., 1]).astype(int), 0, G - 1)
    t = np.clip(g - np.stack([gi, gj], -1), 0, 1).astype(f32)
    x00 = grid[gi, gj]; x10 = grid[gi + 1, gj]; x01 = grid[gi, gj + 1]; x11 = grid[gi + 1, gj + 1]
    a = x00 + (x10 - x00) * t[..., 0:1]
    b = x01 + (x11 - x01) * t[..., 0:1]
    return a + (b - a) * t[..., 1:2]


def on_sheet(grid_j, diag_j_ab, g0, Q, G, own_part, own_q):
    """onSheet dello shader: Q giace sul foglio della parte j vicino a g0?"""
    g = g0.copy()
    tq = f32(1e-3) * max(f32(1), np.linalg.norm(Q))
    for it in range(6):
        gi = int(np.clip(np.floor(g[0]), 0, G - 1)); gj = int(np.clip(np.floor(g[1]), 0, G - 1))
        tt = np.clip(g - np.array([gi, gj], dtype=f32), 0, 1).astype(f32)
        x00 = grid_j[gi, gj]; x10 = grid_j[gi + 1, gj]; x01 = grid_j[gi, gj + 1]; x11 = grid_j[gi + 1, gj + 1]
        Ji = x10 - x00 + ((x11 - x01) - (x10 - x00)) * tt[1]
        Jj = x01 - x00 + ((x11 - x10) - (x01 - x00)) * tt[0]
        dg = max(np.linalg.norm(x11 - x00), np.linalg.norm(x10 - x01))
        lo = x00 + (x10 - x00) * tt[0]; hi = x01 + (x11 - x01) * tt[0]
        r = Q - (lo + (hi - lo) * tt[1])
        if np.linalg.norm(r) < tq + max(f32(0.06) * dg, f32(0.06) * diag_j_ab):
            if not (own_part and abs(g[0] - own_q[0]) < 1.5 and abs(g[1] - own_q[1]) < 1.5):
                return True
        a11 = Ji @ Ji; a12 = Ji @ Jj; a22 = Jj @ Jj
        det = a11 * a22 - a12 * a12
        if it == 5 or det <= 1e-12 * max(a11 * a22, 1e-30):
            break
        d = np.array([a22 * (Ji @ r) - a12 * (Jj @ r), a11 * (Jj @ r) - a12 * (Ji @ r)], dtype=f32) / det
        g = np.clip(g + np.clip(d, -16, 16), 0, G).astype(f32)
    return False


GLUE = os.environ.get('GLUE', '1') != '0'


def seg_dist(P, L):
    """Distanza di P dalla polilinea L (punti consecutivi)."""
    a = L[:-1]; d = L[1:] - a
    x = np.clip(((P - a) * d).sum(-1) / np.maximum((d * d).sum(-1), 1e-30), 0, 1)
    return float(np.min(np.linalg.norm(a + x[:, None] * d - P, axis=-1)))


def own_cell(side, f, G):
    if side == 0: return np.stack([f, np.zeros_like(f)], -1)
    if side == 1: return np.stack([f, np.full_like(f, G)], -1)
    if side == 2: return np.stack([np.zeros_like(f), f], -1)
    return np.stack([np.full_like(f, G), f], -1)


def covered(D):
    count, N, G = D['count'], D['N'], D['G']
    edges, grid = D['edges'], D['grid']
    out = np.zeros((count, 4, N + 1), dtype=bool)
    X00 = grid[:, :-1, :-1]; X10 = grid[:, 1:, :-1]; X01 = grid[:, :-1, 1:]; X11 = grid[:, 1:, 1:]
    diag = np.maximum(np.linalg.norm(X11 - X00, axis=-1), np.linalg.norm(X10 - X01, axis=-1))
    ci, cj = np.meshgrid(np.arange(G), np.arange(G), indexing='ij')
    M = np.arange(N + 1)
    for p in range(count):
        for side in range(4):
            E = edges[p, side, :, 0]
            Ein = edges[p, side, :, 1]
            T = E[np.minimum(M + 1, N)] - E[np.maximum(M - 1, 0)]
            Tn = np.linalg.norm(T, axis=-1, keepdims=True)
            T = np.where(Tn ** 2 > 1e-30, T / np.where(Tn > 0, Tn, 1), 0).astype(f32)
            inward = Ein - E
            inward = inward - (inward * T).sum(-1, keepdims=True) * T
            In = np.linalg.norm(inward, axis=-1, keepdims=True)
            inward = np.where(In ** 2 > 1e-30, inward / np.where(In > 0, In, 1), 0).astype(f32)
            tol = (f32(1e-3) * np.maximum(f32(1), np.linalg.norm(E, axis=-1))).astype(f32)
            own = own_cell(side, (M / N * G).astype(f32), G).astype(f32)
            ci0 = np.clip(np.floor(own[:, 0] + 0.5).astype(int), 0, G - 1)
            cj0 = np.clip(np.floor(own[:, 1] + 0.5).astype(int), 0, G - 1)
            R = np.maximum(20 * tol, 3 * (tol + f32(0.06) * diag[p, ci0, cj0]))
            R = R + np.linalg.norm(bilerp(grid[p], own) - E, axis=-1)
            span = np.minimum(M + 1, N) - np.maximum(M - 1, 0)
            step = np.maximum(np.linalg.norm(E[np.minimum(M + 1, N)] - E[np.maximum(M - 1, 0)], axis=-1) / span, 1e-30)

            found = np.zeros(N + 1, bool)
            # Polo
            seglen = np.linalg.norm(E[1:] - E[:-1], axis=-1).sum()
            found |= seglen < tol
            # Cuciture col lato opposto, diritte e ritorte, qui e accanto
            opp = edges[p, side ^ 1, :, 0]
            for B in (opp, opp[::-1]):
                on = np.linalg.norm(E - B, axis=-1) < tol
                left = np.concatenate([[False], on[:-1]]); right = np.concatenate([on[1:], [False]])
                found |= on & (left | right)
            # Incollato all'orlo opposto con uno scorrimento qualunque: il
            # campione sta sulla polilinea del lato opposto, e cosi' un vicino.
            if GLUE:
                on = np.array([seg_dist(E[m], opp) < tol[m] for m in range(N + 1)])
                left = np.concatenate([[False], on[:-1]]); right = np.concatenate([on[1:], [False]])
                found |= on & (left | right)
            # Lato ripiegato su se' stesso
            rev_on = np.linalg.norm(E - E[::-1], axis=-1) < tol
            ia = Ein - E
            for m in range(N + 1):
                if found[m] or not rev_on[m]:
                    continue
                for mm in (m - 1, m + 1):
                    if mm < 0 or mm > N or not rev_on[mm] or N - mm == mm:
                        continue
                    a_ = ia[mm] - (ia[mm] @ T[m]) * T[m]
                    b_ = ia[N - mm] - (ia[N - mm] @ T[m]) * T[m]
                    if a_ @ b_ < 0.5 * np.linalg.norm(a_) * np.linalg.norm(b_):
                        found[m] = True

            for j in range(count):
                if found.all():
                    break
                Pb = E[:, None, None, :]
                d1, s1, t1 = tri_dist(Pb, X00[j][None], X10[j][None], X01[j][None])
                d2, s2, t2 = tri_dist(Pb, X01[j][None], X10[j][None], X11[j][None])
                dist = np.minimum(d1, d2)
                margin = f32(0.06) * diag[j][None]
                if j == p:   # sulla propria parte non oltre un quarto del passo del lato
                    margin = np.minimum(margin, f32(0.25) * step[:, None, None])
                cand = dist < tol[:, None, None] + margin
                cand &= ~found[:, None, None]
                if j == p:
                    du = ci[None] + 0.5 - own[:, 0, None, None]
                    dv = cj[None] + 0.5 - own[:, 1, None, None]
                    cand &= ~((np.abs(du) < 1.5) & (np.abs(dv) < 1.5))
                for (m, a, b) in np.argwhere(cand):
                    if found[m]:
                        continue
                    P = E[m]
                    if d1[m, a, b] <= d2[m, a, b]:
                        hit = np.array([a + s1[m, a, b], b + t1[m, a, b]], dtype=f32)
                    else:
                        hit = np.array([a + s2[m, a, b] + t2[m, a, b], b + 1 - s2[m, a, b]], dtype=f32)
                    if j == p:
                        q = np.arange(1, 17, dtype=f32)[:, None] / 16
                        x = bilerp(grid[p], own[m] + (hit - own[m]) * q)
                        if (np.linalg.norm(x - P, axis=-1) < R[m]).all():
                            continue   # stesso foglio
                    corners = np.stack([X00[j, a, b], X10[j, a, b], X01[j, a, b], X11[j, a, b]])
                    w = corners - P
                    w = w - (w @ T[m])[:, None] * T[m]
                    L = np.linalg.norm(w, axis=-1)
                    if not ((L > max(0.25 * L.max(), tol[m])) & ((w @ inward[m]) < 0.5 * L)).any():
                        continue   # parte nella stessa direzione
                    k = int(np.clip(np.ceil(diag[j, a, b] / step[m]), 1, 32))
                    for mq in (max(m - k, 0), min(m + k, N)):
                        if 2 * abs(mq - m) < k:
                            continue
                        if on_sheet(grid[j], diag[j, a, b], hit, E[mq], G, j == p, own[mq]):
                            found[m] = True
                            if DEBUG == (p, side, m):
                                print(f'   DEBUG parte {p} lato {side} campione {m}: coperto dalla parte {j}, '
                                      f'cella {a},{b}, distanza {dist[m, a, b]:.4g}, diagonale {diag[j, a, b]:.4g}, '
                                      f'R {R[m]:.4g}, campione vicino {mq}')
                            break
            out[p, side] = smooth(found, N)
    return out


SMOOTH = 6


def smooth(raw, N):
    """Quarta passata: chiusura (buchi < SMOOTH fra due coperti) poi apertura
    (tratti coperti < SMOOTH tolti), lungo il lato."""
    def raw_at(m):
        return 0 <= m <= N and bool(raw[m])

    def closed_at(m):
        if raw_at(m):
            return True
        dl = dr = 0
        for a in range(1, SMOOTH):
            if raw_at(m - a):
                dl = a
                break
        for b in range(1, SMOOTH):
            if raw_at(m + b):
                dr = b
                break
        return dl > 0 and dr > 0 and dl + dr <= SMOOTH

    out = np.zeros(N + 1, bool)
    for m in range(N + 1):
        if not closed_at(m):
            continue
        n = 1
        a = 1
        while a < SMOOTH and m - a >= 0 and closed_at(m - a):
            n += 1
            a += 1
        b = 1
        while b < SMOOTH and m + b <= N and closed_at(m + b):
            n += 1
            b += 1
        out[m] = n >= SMOOTH
    return out


def runs(eff):
    r = []; m = 0; N = len(eff) - 1
    while m <= N:
        if eff[m]:
            e = m
            while e < N and eff[e + 1]:
                e += 1
            r.append((m, e)); m = e + 1
        else:
            m += 1
    return r


def main():
    target = sys.argv[1]
    files = sorted(glob.glob(os.path.join(target, '*.bin'))) if os.path.isdir(target) else [target]
    bad = 0
    for path in files:
        D = load(path)
        c = covered(D)
        diff = int((c != D['flags']).sum())
        bad += diff > 0
        print(f'{os.path.basename(path)[:-4]} (parti {D["count"]}): '
              + ('replica = GPU' if diff == 0 else f'REPLICA DIVERSA DALLA GPU in {diff} campioni'))
        if diff:
            for p in range(D['count']):
                for s in range(4):
                    if (c[p, s] != D['flags'][p, s]).any():
                        print(f'   parte {p} {NAMES[s]}: GPU {runs(D["flags"][p, s])[:8]} replica {runs(c[p, s])[:8]}')
        sys.stdout.flush()
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
