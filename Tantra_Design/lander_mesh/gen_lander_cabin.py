# -*- coding: utf-8 -*-
"""Кабина пилотов «Грани» 25,4 м для Orbiter (вид из кабины, MESHVIS_VC), раскладка v10, утверждённая пользователем
2026-10-09 (мокап Tantra_Design/lander_cockpit_3d.html; проверка scratchpad ergo/layout.py, view.py):
 - барабан на двоих (ось поперёк, R 1,10, ширина 3,0) поворачивается по тангажу вслед за ощущаемым ускорением;
 - большой обзорный экран: сплошная дуга 160° от колен (0,53; между пилотами 0,30) до 1,50. В Orbiter он показывает
   настоящий вид наружу из глаза: в сетке это проём, стенка барабана за ним вырезана по лучам из глаз пилотов;
 - модуль пилота (ложемент, штурвал, бортовой пульт с МАРШ/ЧАШИ, панели с МФД) качается по крену ±15° на оси y 0,67;
 - у каждого: МФД Orbiter НАВ-2 и НАВ-1 снаружи от штурвала, МФД корабля с 12 клавишами систем с внутренней стороны;
 - тумба с переключателем режимов между ложементами.
Система спецификации: x вперёд, y вверх, z вправо, начало = начало меша Lander.msh, метры (×1,285 уже). Запуск:
python gen_lander_cabin.py -> out/LanderCabin.msh, out/textures/cab_*.dds, Orbitersdk/samples/TantraLander/orbiter/LanderCabinMesh.h"""
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out"); TEX = os.path.join(OUT, "textures")
HDR = os.path.join(HERE, "..", "..", "Orbitersdk", "samples", "TantraLander", "orbiter", "LanderCabinMesh.h")
os.makedirs(TEX, exist_ok=True)
D2R = math.pi / 180

# ------------------------------------------------------------------ the layout (as the approved mockup v10)
YF = -0.58                                   # the cabin deck under the drum
RD, WD, CX, CY = 1.10, 3.00, 6.95, YF + 1.13  # the drum: radius, width, the axis (x, y)
YFL = -0.45                                  # the drum's floor
DEP = (6.36, 0.58)                           # the design eye point (every stature: the couch slides along its back)
ROLL_Y = 0.67                                # the modules' roll axes (along x) at y 0,67, z = ±0,62
ZC = 0.62
STATURE = 1.74                               # the couch drawn at its middle setting
BACK = 40 * D2R
BV = np.array([-math.cos(BACK), math.sin(BACK)]); NF = np.array([math.sin(BACK), math.cos(BACK)])
SCR = {"nav1": (-35, 2, 0.62), "nav2": (-66, 2, 0.60), "ship": (42, 1, 0.62)}   # azimuth (inboard +), elevation, distance
PANEL = [(-84, -21, 0.64, -12.3, 16.8), (23, 62, 0.64, -11.4, 13.9)]
T0, T1, Y_TOP = -80 * D2R, 80 * D2R, 1.50
def screen_bottom(z):
    a = abs(z)
    if a <= 0.22: return 0.30
    if a >= 0.30: return 0.53
    f = (a - 0.22) / 0.08; return 0.30 + 0.23 * f * f * (3 - 2 * f)
def P(t): return (6.40 + 0.95 * math.cos(t), 1.40 * math.sin(t))
def eye(s): return np.array([DEP[0], DEP[1], s * ZC])
def at_eye(s, a):
    az, e, d = a[0] * D2R, a[1] * D2R, a[2]
    return np.array([DEP[0] + d * math.cos(e) * math.cos(az), DEP[1] + d * math.sin(e), s * ZC - s * d * math.cos(e) * math.sin(az)])

# ------------------------------------------------------------------ the mesh
class Grp:
    def __init__(s, name, mat, tex=0):
        s.name, s.mat, s.tex, s.v, s.n, s.uv, s.t = name, mat, tex, [], [], [], []
    def tri(s, a, b, c, uva=(0, 0), uvb=(0, 0), uvc=(0, 0), na=None, nb=None, nc=None):
        a, b, c = map(lambda q: np.asarray(q, float), (a, b, c))
        fn = np.cross(b - a, c - a); L = np.linalg.norm(fn)
        if L < 1e-12: return
        fn /= L; k = len(s.v)
        for p, n, uv in ((a, na, uva), (b, nb, uvb), (c, nc, uvc)):
            s.v.append(p); s.n.append(fn if n is None else np.asarray(n, float) / np.linalg.norm(n)); s.uv.append(uv)
        s.t.append((k, k + 1, k + 2))
    def quad4(s, a, b, c, d, uv):
        a, b, c, d = map(lambda q: np.asarray(q, float), (a, b, c, d))
        fn = np.cross(b - a, c - a); fn /= np.linalg.norm(fn); k = len(s.v)
        for p, t in zip((a, b, c, d), uv): s.v.append(p); s.n.append(fn); s.uv.append(t)
        s.t.append((k, k + 1, k + 2)); s.t.append((k, k + 2, k + 3))
    def quad(s, a, b, c, d, uv=((0, 0), (1, 0), (1, 1), (0, 1))):
        s.tri(a, b, c, uv[0], uv[1], uv[2]); s.tri(a, c, d, uv[0], uv[2], uv[3])
G = {}
def grp(name, mat, tex=0):
    if name not in G: G[name] = Grp(name, mat, tex)
    return G[name]

def rotz(a):
    c, s = math.cos(a), math.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
def frame_to(c, look):
    """the axes of a face at c turned to the point look: r (the viewer's right), up, n (towards the viewer)"""
    n = np.asarray(look, float) - np.asarray(c, float); n /= np.linalg.norm(n)
    r = np.cross([0, 1, 0], n); r /= np.linalg.norm(r); up = np.cross(n, r)
    return r, up, n
def box(g, size, centre, R=np.eye(3), front_uv=None):
    """a box; R columns = its local axes; front_uv: uv of the local +z face (bottom-left, bottom-right, top-right, top-left)"""
    sx, sy, sz = size; c = np.asarray(centre, float)
    Pt = lambda i, j, k: c + R @ np.array([sx / 2 * i, sy / 2 * j, sz / 2 * k])
    faces = {"+z": (Pt(-1, -1, 1), Pt(1, -1, 1), Pt(1, 1, 1), Pt(-1, 1, 1)), "-z": (Pt(1, -1, -1), Pt(-1, -1, -1), Pt(-1, 1, -1), Pt(1, 1, -1)),
             "+x": (Pt(1, -1, 1), Pt(1, -1, -1), Pt(1, 1, -1), Pt(1, 1, 1)), "-x": (Pt(-1, -1, -1), Pt(-1, -1, 1), Pt(-1, 1, 1), Pt(-1, 1, -1)),
             "+y": (Pt(-1, 1, 1), Pt(1, 1, 1), Pt(1, 1, -1), Pt(-1, 1, -1)), "-y": (Pt(-1, -1, -1), Pt(1, -1, -1), Pt(1, -1, 1), Pt(-1, -1, 1))}
    for k, q in faces.items():
        uv = front_uv if (k == "+z" and front_uv) else ((0, 1), (1, 1), (1, 0), (0, 0))
        g.quad(q[0], q[1], q[2], q[3], uv)
def facing_box(g, size, centre, look, front_uv=None):
    r, up, n = frame_to(centre, look); box(g, size, centre, np.stack([r, up, n], 1), front_uv)
def cyl(g, a, b, r, nseg=16, caps=True):
    a, b = np.asarray(a, float), np.asarray(b, float); ax = b - a; L = np.linalg.norm(ax); ax /= L
    u = np.cross(ax, [0, 1, 0] if abs(ax[1]) < 0.9 else [1, 0, 0]); u /= np.linalg.norm(u); w = np.cross(ax, u)
    ring = [u * math.cos(2 * math.pi * k / nseg) + w * math.sin(2 * math.pi * k / nseg) for k in range(nseg)]
    for k in range(nseg):
        p0, p1 = ring[k], ring[(k + 1) % nseg]
        g.tri(a + r * p0, a + r * p1, b + r * p1, na=p0, nb=p1, nc=p1); g.tri(a + r * p0, b + r * p1, b + r * p0, na=p0, nb=p1, nc=p0)
        if caps: g.tri(b, b + r * p0, b + r * p1); g.tri(a, a + r * p1, a + r * p0)
def torus(g, c, axis, R, r, nu=32, nv=10):
    c = np.asarray(c, float); ax = np.asarray(axis, float); ax /= np.linalg.norm(ax)
    u = np.cross(ax, [0, 1, 0] if abs(ax[1]) < 0.9 else [1, 0, 0]); u /= np.linalg.norm(u); w = np.cross(ax, u)
    def p(i, j):
        a, b = 2 * math.pi * i / nu, 2 * math.pi * j / nv
        rad = u * math.cos(a) + w * math.sin(a)
        return c + rad * (R + r * math.cos(b)) + ax * r * math.sin(b), rad * math.cos(b) + ax * math.sin(b)
    for i in range(nu):
        for j in range(nv):
            (A, nA), (B, nB), (C, nC), (D, nD) = p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1)
            g.tri(A, B, C, na=nA, nb=nB, nc=nC); g.tri(A, C, D, na=nA, nb=nC, nc=nD)
def sph(g, c, r, nu=12, nv=8):
    c = np.asarray(c, float)
    Q = lambda i, j: c + r * np.array([math.sin(math.pi * j / nv) * math.cos(2 * math.pi * i / nu), math.cos(math.pi * j / nv), math.sin(math.pi * j / nv) * math.sin(2 * math.pi * i / nu)])
    for i in range(nu):
        for j in range(nv):
            a, b, cc, d = Q(i, j), Q(i + 1, j), Q(i + 1, j + 1), Q(i, j + 1)
            g.tri(a, b, cc, na=a - c, nb=b - c, nc=cc - c); g.tri(a, cc, d, na=a - c, nb=cc - c, nc=d - c)
def quad_facing(g, w, h, centre, look, uv=((0, 1), (1, 1), (1, 0), (0, 0)), lift=0.0, four=False):
    """a w × h rectangle at centre turned to look; returns its corners: top-left, top-right, bottom-left, bottom-right"""
    r, up, n = frame_to(centre, look); c = np.asarray(centre, float) + n * lift
    bl, br, tr, tl = c - r * w / 2 - up * h / 2, c + r * w / 2 - up * h / 2, c + r * w / 2 + up * h / 2, c - r * w / 2 + up * h / 2
    (g.quad4 if four else g.quad)(bl, br, tr, tl, uv)
    return tl, tr, bl, br

# materials: name, diffuse, specular (r, g, b, power), emissive
MATS = [("metal", (0.46, 0.49, 0.51), (0.35, 0.35, 0.35, 20), (0, 0, 0)), ("dark", (0.20, 0.22, 0.24), (0.3, 0.3, 0.3, 15), (0, 0, 0)),
        ("cushion", (0.11, 0.12, 0.13), (0.05, 0.05, 0.05, 5), (0, 0, 0)), ("stick", (0.08, 0.09, 0.10), (0.4, 0.4, 0.4, 30), (0, 0, 0)),
        ("screen", (0, 0, 0), (0.2, 0.2, 0.2, 40), (1, 1, 1)), ("keylit", (0.06, 0.25, 0.16), (0.2, 0.2, 0.2, 20), (0.25, 0.82, 0.54)),
        ("mfd", (1, 1, 1), (0, 0, 0, 1), (1, 1, 1)), ("red", (0.55, 0.06, 0.06), (0.3, 0.3, 0.3, 20), (0.35, 0, 0)),
        ("bronze", (0.54, 0.43, 0.23), (0.6, 0.55, 0.4, 40), (0, 0, 0)), ("panel", (0.22, 0.24, 0.26), (0.25, 0.25, 0.25, 12), (0, 0, 0)),
        ("label", (0.9, 0.9, 0.9), (0.1, 0.1, 0.1, 10), (0.55, 0.55, 0.55))]
MI = {m[0]: i + 1 for i, m in enumerate(MATS)}
TEXTURES = [("cab_metal", ""), ("cab_yoke", ""), ("cab_lbl", "D"), ("cab_ship", "D")] + [("cab_mfd%d" % i, "") for i in range(4)]
TI = {t[0]: i + 1 for i, t in enumerate(TEXTURES)}
LBL_W, LBL_H, LBL_CW, LBL_CH = 512, 256, 64, 32          # the Orbiter MFDs' button labels: 4 MFDs × 15 cells of 64 × 32
SHIP_W, SHIP_H = 1024, 512                               # the ships' MFDs: commander left half, second pilot right half
SHIP_SCR = (512, 340)                                    # each half: the screen 512 × 340 at the top, the 12 key cells below
SHIP_KEY = (64, 32)

def lbl_uv(cell):
    col, row = cell % (LBL_W // LBL_CW), cell // (LBL_W // LBL_CW)
    u0, v0, u1, v1 = col * LBL_CW / LBL_W, row * LBL_CH / LBL_H, (col + 1) * LBL_CW / LBL_W, (row + 1) * LBL_CH / LBL_H
    return ((u0, v1), (u1, v1), (u1, v0), (u0, v0))
def ship_key_rect(p, k):
    x0 = p * 512 + (k % 8) * SHIP_KEY[0]; y0 = 352 + (k // 8) * (SHIP_KEY[1] + 8)
    return x0, y0, x0 + SHIP_KEY[0], y0 + SHIP_KEY[1]
def ship_uv_rect(x0, y0, x1, y1):
    u0, v0, u1, v1 = x0 / SHIP_W, y0 / SHIP_H, x1 / SHIP_W, y1 / SHIP_H
    return ((u0, v1), (u1, v1), (u1, v0), (u0, v0))

# ------------------------------------------------------------------ the screen (a hole) and what is cut behind it
def through_screen(E, Q):
    """does the segment from the eye E to Q pass through the view screen (the arc, t ±80°, y from the bottom edge to the top)"""
    d = Q - E
    lo, hi = 0.0, 1.0
    def f(s):
        p = E + d * s; t = math.asin(max(-1.0, min(1.0, p[2] / 1.40))); return p[0] - (6.40 + 0.95 * math.cos(t))
    if f(hi) < 0 or f(lo) > 0: return False
    for _ in range(40):
        m = (lo + hi) / 2
        if f(m) < 0: lo = m
        else: hi = m
    p = E + d * hi; t = math.asin(max(-1.0, min(1.0, p[2] / 1.40)))
    return abs(t) <= 81 * D2R and screen_bottom(p[2]) - 0.02 <= p[1] <= Y_TOP + 0.02
def seen_through(Q):
    return any(through_screen(eye(s), Q) for s in (-1, 1))

# the drum's wall (seen from inside), cut where the pilots look out through the screen
g = grp("drum_wall", MI["metal"], TI["cab_metal"])
NA, NZ = 120, 12
for i in range(NA):
    a0, a1 = 2 * math.pi * i / NA, 2 * math.pi * (i + 1) / NA
    for j in range(NZ):
        z0, z1 = -WD / 2 + WD * j / NZ, -WD / 2 + WD * (j + 1) / NZ
        p = lambda a, z: np.array([CX + RD * math.cos(a), CY + RD * math.sin(a), z])
        cen = p((a0 + a1) / 2, (z0 + z1) / 2)
        if seen_through(cen) or any(seen_through(p(a, z)) for a in (a0, a1) for z in (z0, z1)): continue
        A, B, C2, Dd = p(a0, z0), p(a1, z0), p(a1, z1), p(a0, z1)
        u0, u1 = i / NA * 8, (i + 1) / NA * 8
        g.quad(A, Dd, C2, B, ((u0, z0), (u0, z1), (u1, z1), (u1, z0)))         # its face towards the axis
gc = grp("drum_caps", MI["dark"])
for e, rin in ((-1, 0.42), (1, 0.10)):
    z = e * WD / 2
    for i in range(NA):
        a0, a1 = 2 * math.pi * i / NA, 2 * math.pi * (i + 1) / NA
        for k in range(6):
            r0, r1 = rin + (RD - rin) * k / 6, rin + (RD - rin) * (k + 1) / 6
            Q = [np.array([CX + r * math.cos(a), CY + r * math.sin(a), z]) for r, a in ((r0, a0), (r1, a0), (r1, a1), (r0, a1))]
            if seen_through(sum(Q) / 4) or any(seen_through(q) for q in Q): continue
            if e < 0: gc.quad(Q[0], Q[1], Q[2], Q[3])                          # the left cap faces +z (inside)
            else: gc.quad(Q[0], Q[3], Q[2], Q[1])
gh = grp("drum_hatch_rim", MI["bronze"])
torus(gh, (CX, CY, -WD / 2 + 0.03), (0, 0, 1), 0.42, 0.025, 40, 6)
xs = math.sqrt(RD ** 2 - (YFL - CY) ** 2)
grp("drum_floor", MI["dark"], TI["cab_metal"]).quad((CX - xs, YFL, -WD / 2), (CX - xs, YFL, WD / 2), (CX + xs, YFL, WD / 2), (CX + xs, YFL, -WD / 2),
                                                   ((0, 0), (0, 3), (1.1, 3), (1.1, 0)))
# the screen's frame: the top and the bottom edges, the ends
gf = grp("screen_frame", MI["dark"])
N = 96
def arc_pt(t, y, off=0.0):
    p, q = P(t), P(t + 1e-4); tx, tz = q[0] - p[0], q[1] - p[1]; L = math.hypot(tx, tz)
    return np.array([p[0] + tz / L * off, y, p[1] - tx / L * off])
for i in range(N):
    ta, tb = T0 + (T1 - T0) * i / N, T0 + (T1 - T0) * (i + 1) / N
    za, zb = P(ta)[1], P(tb)[1]
    for (ya0, ya1, yb0, yb1) in ((Y_TOP, Y_TOP + 0.035, Y_TOP, Y_TOP + 0.035), (screen_bottom(za) - 0.035, screen_bottom(za), screen_bottom(zb) - 0.035, screen_bottom(zb))):
        A, B, C2, Dd = arc_pt(ta, ya0), arc_pt(tb, yb0), arc_pt(tb, yb1), arc_pt(ta, ya1)
        gf.quad(A, B, C2, Dd)                                                   # towards the pilots (the arc's inside)
for t in (T0, T1):
    a, b = (t - 1.2 * D2R, t) if t < 0 else (t, t + 1.2 * D2R)
    y0 = screen_bottom(P(t)[1]) - 0.035
    A, B, C2, Dd = arc_pt(a, y0), arc_pt(b, y0), arc_pt(b, Y_TOP + 0.035), arc_pt(a, Y_TOP + 0.035)
    gf.quad(A, B, C2, Dd)

# ------------------------------------------------------------------ the pedestal and the mode switch (on the drum)
gp = grp("pedestal", MI["dark"], TI["cab_metal"])
box(gp, (0.44, 0.16 - YFL, 0.40), (6.70, (0.16 + YFL) / 2, 0))
gm = grp("mode_switch", MI["stick"])
cyl(gm, (6.66, 0.16, 0), (6.66, 0.22, 0), 0.035, 16)
cyl(grp("mode_pointer", MI["keylit"]), (6.66, 0.22, 0), (6.68, 0.25, 0), 0.012, 8)
MODE = (6.66, 0.20, 0.0)
# the drum's end bearings and their stands (on the hull - they do not turn)
gs_ = grp("drum_supports", MI["dark"], TI["cab_metal"])
for e in (-1, 1):
    torus(grp("drum_bearings", MI["bronze"]), (CX, CY, e * (WD / 2 + 0.07)), (0, 0, 1), 0.26, 0.055, 32, 10)
    box(gs_, (0.22, CY - YF, 0.10), (CX, (YF + CY) / 2, e * (WD / 2 + 0.16)))

# ------------------------------------------------------------------ the pilots' modules (they roll ±15° about x at y 0,67)
def pose(S):
    hip = np.array(DEP) - BV * 0.406 * S - NF * 0.09
    sh = hip + BV * 0.288 * S + NF * 0.06
    knee = hip + 0.245 * S * np.array([math.cos(45 * D2R), math.sin(45 * D2R)])
    return dict(hip=hip, sh=sh, knee=knee, reach=0.386 * S, half=0.1295 * S)
P0 = pose(STATURE)
INFO = {}
KEYS = {-1: ["М1", "М2", "РЯД П", "РЯД З", "РСУ", "ФОРСАЖ", "ШАССИ", "КИЛЬ", "ЛЮК", "ЩИТОК", "АВАР", "АП"],
        1: ["НАКОП", "ШИНА", "КРИО А", "КРИО Б", "ФОТОН", "АНАЛОГ", "ПЕРЕК А", "ПЕРЕК Б", "ПЕРЕКР", "ВДУВ", "РЕЗЕРВ", "РЛС"]}
for s in (-1, 1):
    tag = "K" if s < 0 else "2"; zc = s * ZC; E = eye(s); pi = 0 if s < 0 else 1
    hx, hy = P0["hip"]
    along = lambda d, off=0.0: (hx + BV[0] * d - BV[1] * off, hy + BV[1] * d + BV[0] * off)
    # the couch: the pan, the back's sides and plate with the pack's niche (the insert for the coverall), the headrest,
    # the shin rest, the column on the slide
    gc_ = grp("couch_" + tag, MI["dark"], TI["cab_metal"]); gi = grp("insert_" + tag, MI["cushion"])
    box(gc_, (0.40, 0.07, 0.62), (hx + 0.05, hy - 0.06, zc), rotz(30 * D2R))
    bl = 0.96; c = along(bl / 2); cb = along(bl / 2, 0.24)
    for k in (-1, 1): box(gc_, (bl, 0.30, 0.07), (c[0] - 0.04, c[1] - 0.04, zc + k * 0.30), rotz(-BACK))
    box(gc_, (bl, 0.05, 0.62), (cb[0], cb[1], zc), rotz(-BACK))
    box(gi, (bl - 0.06, 0.24, 0.50), ((c[0] + cb[0]) / 2, (c[1] + cb[1]) / 2 + 0.02, zc), rotz(-BACK))
    hr = along(0.47 * STATURE + 0.02, 0.10); box(gc_, (0.07, 0.30, 0.66), (hr[0], hr[1], zc), rotz(-BACK + 0.1))
    kn = P0["knee"]; ft = (kn[0] + 0.246 * STATURE * math.cos(-50 * D2R), kn[1] + 0.246 * STATURE * math.sin(-50 * D2R))
    box(gc_, (0.246 * STATURE, 0.06, 0.40), ((kn[0] + ft[0]) / 2 + 0.03, (kn[1] + ft[1]) / 2 - 0.10, zc), rotz(-50 * D2R))
    cyl(gc_, (hx + 0.10, YFL, zc), (hx + 0.10, hy - 0.08, zc), 0.05, 12)
    box(gc_, (0.36, 0.04, 0.20), (hx + 0.10, YFL + 0.02, zc))
    # the roll shaft from the back's top to the bearing behind it (the bearing is on the drum)
    top = (hx + BV[0] * 0.96, hy + BV[1] * 0.96)
    cyl(gc_, (5.99, ROLL_Y, zc), (top[0], ROLL_Y, zc), 0.05, 12)
    torus(grp("roll_bearing_" + tag, MI["bronze"]), (5.99, ROLL_Y, zc), (1, 0, 0), 0.20, 0.035, 32, 8)
    # the yoke: the column from the floor, the hub before the chest (its screen to the eye), the horns, «АП ОТКЛ.» on the
    # inboard horn, the yaw thumb keys
    zo = zc + s * P0["half"]; zg = zc + s * 0.19; want = 0.75 * P0["reach"]; dz = zg - zo; dy = 0.44 - P0["sh"][1]
    gx = P0["sh"][0] + math.sqrt(max(0, want * want - dz * dz - dy * dy))
    hub = np.array([gx - 0.02, 0.52, zc])
    gy = grp("yoke_" + tag, MI["stick"])
    cyl(gy, (7.28, YFL, zc), hub + np.array([0.02, -0.05, 0]), 0.035, 12)
    r, up, n = frame_to(hub, E)
    cyl(gy, hub - n * 0.025, hub + n * 0.025, 0.07, 20)
    grips = {}
    for k in (-1, 1):
        g0 = hub + r * k * 0.16 - up * 0.06; g1 = hub + r * k * 0.19 - up * 0.16
        cyl(gy, hub, g0, 0.016, 8); cyl(gy, g0, g1, 0.022, 10)
        inboard = (k > 0) == (s < 0)            # the viewer's right is +z: the commander's inboard (+z) is k = +1
        sph(grp("yoke_btn_" + tag, MI["red"] if inboard else MI["dark"]), g0 + up * 0.025 + n * 0.01, 0.014, 8, 6)
        grips[k] = (g0 + g1) / 2
        if inboard: INFO[("grip", pi)] = (g0, g1)
    quad_facing(grp("yoke_screen_" + tag, MI["screen"], TI["cab_yoke"]), 0.12, 0.06, hub + up * 0.012 + n * 0.027, E)
    # the outboard console: МАРШ (the afterburner detent at the end; the cursor knob under the thumb) and ЧАШИ
    box(grp("console_" + tag, MI["dark"], TI["cab_metal"]), (0.36, 0.12, 0.16), (6.80, -0.06, s * 1.05))
    gl = grp("levers_" + tag, MI["stick"])
    for zz, lit in ((s * 1.03, True), (s * 0.95, False)):
        cyl(gl, (6.90, 0.0, zz), (6.93, 0.23, zz), 0.012, 10)
        box(grp("lever_knob_" + tag, MI["keylit"] if lit else MI["dark"]), (0.05, 0.035, 0.035), (6.935, 0.245, zz))
    sph(grp("cursor_" + tag, MI["red"]), (6.942, 0.27, s * 1.03), 0.011, 8, 6)
    # the panels: bands around the eye, the MFDs set in them
    gpn = grp("panel_" + tag, MI["panel"], TI["cab_metal"]); glip = grp("panel_lip_" + tag, MI["dark"])
    def ept(az, e, r):
        return at_eye(s, (az, e, r))
    for (a0, a1, rr, e0, e1) in PANEL:
        n_ = max(2, round((a1 - a0) / 3))
        for i in range(n_):
            aa, ab = a0 + (a1 - a0) * i / n_, a0 + (a1 - a0) * (i + 1) / n_
            A, B, C2, Dd = ept(aa, e0, rr), ept(ab, e0, rr), ept(ab, e1, rr), ept(aa, e1, rr)
            # face the eye: for the commander the azimuth grows to +z (his right); for the second pilot to -z
            if s < 0: gpn.quad(A, B, C2, Dd, ((aa / 60, e0 / 30), (ab / 60, e0 / 30), (ab / 60, e1 / 30), (aa / 60, e1 / 30)))
            else: gpn.quad(B, A, Dd, C2, ((ab / 60, e0 / 30), (aa / 60, e0 / 30), (aa / 60, e1 / 30), (ab / 60, e1 / 30)))
            L0, L1, L2, L3 = ept(aa, e1, rr - 0.06), ept(ab, e1, rr - 0.06), ept(ab, e1, rr + 0.02), ept(aa, e1, rr + 0.02)
            if s < 0: glip.quad(L0, L3, L2, L1)
            else: glip.quad(L1, L2, L3, L0)
    # the two Orbiter MFDs: the bezel, the square screen group (Orbiter draws the MFD there), 12 side + 3 bottom buttons
    # whose faces carry the labels (the dynamic texture cab_lbl)
    for j, key in enumerate(("nav1", "nav2")):
        c = at_eye(s, SCR[key]); mi = pi * 2 + j          # MFD index 0..3: К НАВ-1, К НАВ-2, 2 НАВ-1, 2 НАВ-2
        r, up, n = frame_to(c, E); R = np.stack([r, up, n], 1)
        box(grp("mfd_bezel_" + tag, MI["dark"]), (0.31, 0.31, 0.04), c, R)
        sc = c + up * 0.02 + n * 0.021
        tl, tr, bl_, br = quad_facing(grp("mfd_%d" % mi, MI["mfd"], TI["cab_mfd%d" % mi]), 0.21, 0.21, sc, sc + n, uv=((0, 1), (1, 1), (1, 0), (0, 0)), four=True)
        gb = grp("mfd_btn_%d" % mi, MI["label"], TI["cab_lbl"])
        cols = {}
        for side in (-1, 1):
            for i in range(6):
                bc = c + r * side * 0.13 + up * (0.105 - i * 0.034) + n * 0.026
                box(gb, (0.04, 0.024, 0.012), bc, R, front_uv=lbl_uv(mi * 15 + (0 if side < 0 else 6) + i))
            x_ = side * 0.13
            cols[side] = [c + r * (x_ - 0.02) + up * 0.117 + n * 0.033, c + r * (x_ + 0.02) + up * 0.117 + n * 0.033,
                          c + r * (x_ - 0.02) + up * (-0.077) + n * 0.033, c + r * (x_ + 0.02) + up * (-0.077) + n * 0.033]
        for i in range(3):
            bc = c + r * (-0.06 + i * 0.06) + up * (-0.125) + n * 0.026
            box(gb, (0.05, 0.02, 0.012), bc, R, front_uv=lbl_uv(mi * 15 + 12 + i))
        bot = [c + r * (-0.085) + up * (-0.115) + n * 0.033, c + r * 0.085 + up * (-0.115) + n * 0.033,
               c + r * (-0.085) + up * (-0.135) + n * 0.033, c + r * 0.085 + up * (-0.135) + n * 0.033]
        INFO[("mfd", mi)] = dict(screen=(tl, tr, bl_, br), left=cols[-1], right=cols[1], bottom=bot)
    # the ship's MFD: the bezel, the screen (its half of cab_ship), the 12 system keys (their cells in cab_ship)
    c = at_eye(s, SCR["ship"]); r, up, n = frame_to(c, E); R = np.stack([r, up, n], 1)
    box(grp("ship_bezel_" + tag, MI["dark"]), (0.40, 0.27, 0.04), c, R)
    sc = c + n * 0.021
    tl, tr, bl_, br = quad_facing(grp("ship_screen_" + tag, MI["screen"], TI["cab_ship"]), 0.30, 0.20, sc, sc + n,
                                  uv=ship_uv_rect(pi * 512, 0, pi * 512 + SHIP_SCR[0], SHIP_SCR[1]))
    gk = grp("ship_keys_" + tag, MI["label"], TI["cab_ship"])
    kcols = {}
    for side in (-1, 1):
        for i in range(6):
            k = (0 if side < 0 else 6) + i
            bc = c + r * side * 0.175 + up * (0.085 - i * 0.034) + n * 0.026
            box(gk, (0.04, 0.024, 0.012), bc, R, front_uv=ship_uv_rect(*ship_key_rect(pi, k)))
        x_ = side * 0.175
        kcols[side] = [c + r * (x_ - 0.02) + up * 0.097 + n * 0.033, c + r * (x_ + 0.02) + up * 0.097 + n * 0.033,
                       c + r * (x_ - 0.02) + up * (-0.097) + n * 0.033, c + r * (x_ + 0.02) + up * (-0.097) + n * 0.033]
    INFO[("ship", pi)] = dict(screen=(tl, tr, bl_, br), left=kcols[-1], right=kcols[1])
    INFO[("hub", pi)] = hub
    INFO[("hubn", pi)] = hub + frame_to(hub, E)[2]
    INFO[("hip", pi)] = np.array([hx, hy, zc])
    INFO[("march", pi)] = np.array([6.935, 0.245, s * 1.03])

# ------------------------------------------------------------------ textures
def font(sz, bold=True):
    for f in (("arialnb.ttf", "arialbd.ttf", "arial.ttf") if bold else ("arial.ttf",)):
        try: return ImageFont.truetype(os.path.join("C:/Windows/Fonts", f), sz)
        except OSError: pass
    return ImageFont.load_default()
def dds_xrgb(path, img):
    """an uncompressed X8R8G8B8 DDS, one level (Orbiter's D3D9 redraws a dynamic texture through an XRGB surface)"""
    import struct
    im = img.convert("RGB"); w, h = im.size
    px = np.asarray(im, np.uint8)
    bgrx = np.zeros((h, w, 4), np.uint8); bgrx[..., 0] = px[..., 2]; bgrx[..., 1] = px[..., 1]; bgrx[..., 2] = px[..., 0]
    hdr = struct.pack("<4s7I44x8I5I", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000, h, w, w * 4, 0, 0,
                      32, 0x40, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0, 0x1000, 0, 0, 0, 0)
    with open(path, "wb") as fo: fo.write(hdr); fo.write(bgrx.tobytes())
def save(name, img, dyn=False):
    img.save(os.path.join(TEX, name + ".png"))
    if dyn: dds_xrgb(os.path.join(TEX, name + ".dds"), img)
    else: img.convert("RGB").save(os.path.join(TEX, name + ".dds"))
rng = np.random.default_rng(3)
mt = (0.32 + 0.05 * rng.random((512, 512)))
for i in range(0, 512, 128): mt[i:i + 3, :] *= 0.45; mt[:, i:i + 3] *= 0.45
save("cab_metal", Image.fromarray((np.stack([mt, mt * 1.03, mt * 1.06], -1) * 255).clip(0, 255).astype(np.uint8)))
yk = Image.new("RGB", (256, 128), (6, 16, 10)); d = ImageDraw.Draw(yk)
d.pieslice([78, 14, 178, 114], 180, 360, fill=(58, 110, 158)); d.pieslice([78, 14, 178, 114], 0, 180, fill=(107, 74, 43))
for k in range(-2, 3): d.line([(112, 64 - k * 14), (144, 64 - k * 14)], fill=(255, 255, 255), width=2)
d.line([(98, 64), (118, 64), (128, 72), (138, 64), (158, 64)], fill=(240, 192, 32), width=4)
f = font(18); d.text((8, 22), "V", font=f, fill=(168, 197, 106)); d.text((196, 22), "H", font=f, fill=(168, 197, 106)); d.text((8, 98), "n", font=f, fill=(168, 197, 106))
save("cab_yoke", yk)
# the dynamic ones start dark with their labels (the module redraws them)
lb = Image.new("RGBA", (LBL_W, LBL_H), (20, 24, 26, 255)); d = ImageDraw.Draw(lb); f = font(18)
for mi in range(4):
    for i, t in enumerate(["PWR", "SEL", "MNU"]):
        cell = mi * 15 + 12 + i; col, row = cell % 8, cell // 8
        tw = d.textlength(t, font=f); d.text((col * 64 + 32 - tw / 2, row * 32 + 6), t, font=f, fill=(220, 226, 222, 255))
save("cab_lbl", lb, True)
sp_ = Image.new("RGBA", (SHIP_W, SHIP_H), (2, 10, 6, 255)); d = ImageDraw.Draw(sp_); f = font(16)
for pi_, keys in ((0, KEYS[-1]), (1, KEYS[1])):
    for k, t in enumerate(keys):
        x0, y0, x1, y1 = ship_key_rect(pi_, k); d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=(30, 36, 38, 255))
        tw = d.textlength(t, font=f); d.text(((x0 + x1) / 2 - tw / 2, y0 + 7), t, font=f, fill=(200, 210, 205, 255))
save("cab_ship", sp_, True)
for i in range(4): save("cab_mfd%d" % i, Image.new("RGB", (64, 64), (0, 0, 0)))

# ------------------------------------------------------------------ the groups' sets for the animations
STATIC = ["drum_supports", "drum_bearings"]
MODULE = {t: [n for n in G if n.endswith("_" + t) and not n.startswith("roll_bearing")] for t in ("K", "2")}
for mi in range(4):
    t = "K" if mi < 2 else "2"
    MODULE[t] += [n for n in ("mfd_%d" % mi, "mfd_btn_%d" % mi) if n not in MODULE[t]]
DRUM = [n for n in G if n not in STATIC and n not in MODULE["K"] and n not in MODULE["2"]]

# ------------------------------------------------------------------ write the mesh (Orbiter: X = z, Y = y, Z = x; the index order kept)
ORDER = list(G)
def O(p): return (p[2], p[1], p[0])
with open(os.path.join(OUT, "LanderCabin.msh"), "w", newline="\r\n", encoding="ascii") as fo:
    fo.write("MSHX1\nGROUPS %d\n" % len(ORDER))
    for nm in ORDER:
        g = G[nm]
        fo.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d ;%s\n" % (nm, g.mat, g.tex, len(g.v), len(g.t), nm))
        for p, n, uv in zip(g.v, g.n, g.uv):
            fo.write("%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*O(p), *O(n), uv[0], uv[1]))
        for t in g.t: fo.write("%d %d %d\n" % (t[0], t[2], t[1]))   # left-handed Orbiter: the mirror reverses the order
    fo.write("MATERIALS %d\n" % len(MATS))
    for m in MATS: fo.write(m[0] + "\n")
    for nm, dc, sp, em in MATS:
        fo.write("MATERIAL %s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1 %.0f\n%.3f %.3f %.3f 1\n" % (nm, *dc, *dc, *sp, *em))
    fo.write("TEXTURES %d\n" % len(TEXTURES))
    for t, fl in TEXTURES: fo.write("Tantra\\Lander\\%s.dds%s\n" % (t, (" " + fl) if fl else ""))

# ------------------------------------------------------------------ the header
def V3(p): return "{%.4f, %.4f, %.4f}" % tuple(O(np.asarray(p, float)))
def Q4(q): return "{" + ", ".join(V3(p) for p in q) + "}"
def ids(names): return ", ".join("G_%s" % n for n in names)
dirK = at_eye(-1, (6, 17, 1.0)) - eye(-1); dir2 = at_eye(1, (6, 17, 1.0)) - eye(1)
L = ["// «Грань» 25,4 м: the pilots' cabin mesh LanderCabin.msh (the drum for two, layout v10). GENERATED by",
     "// Tantra_Design/lander_mesh/gen_lander_cabin.py - do not edit. Orbiter frame (X right, Y up, Z forward), the hull mesh's origin.",
     "#pragma once", "", "namespace tantra::lander::cabin {", "",
     "constexpr int kGroupCount = %d;" % len(ORDER),
     "enum Grp { " + ", ".join("G_%s = %d" % (n, i) for i, n in enumerate(ORDER)) + " };",
     "// the drum turns about the lateral axis (Orbiter X) through kDrumC; the pilots' modules roll about Z through kRollRef[p]",
     "constexpr double kDrumC[3] = %s;" % V3((CX, CY, 0)),
     "constexpr unsigned kDrumGroups[] = {%s};" % ids(DRUM),
     "constexpr unsigned kModuleGroups[2][%d] = {{%s}, {%s}};" % (max(len(MODULE["K"]), len(MODULE["2"])), ids(MODULE["K"]), ids(MODULE["2"])),
     "constexpr int kModuleCount[2] = {%d, %d};" % (len(MODULE["K"]), len(MODULE["2"])),
     "constexpr double kRollRef[2][3] = {%s, %s};" % (V3((6.80, ROLL_Y, -ZC)), V3((6.80, ROLL_Y, ZC))),
     "// the pilots (0 commander left, 1 second pilot right): the eyes (the camera), the default look (forward, 17° up)",
     "constexpr double kEye[2][3] = {%s, %s};" % (V3(eye(-1)), V3(eye(1))),
     "constexpr double kLook[2][3] = {%s, %s};" % (V3(dirK / np.linalg.norm(dirK)), V3(dir2 / np.linalg.norm(dir2))),
     "constexpr double kHub[2][3] = {%s, %s};" % (V3(INFO[("hub", 0)]), V3(INFO[("hub", 1)])),
     "constexpr double kMode[3] = %s;   // the mode switch on the pedestal" % V3(MODE),
     "// the Orbiter MFDs 0..3 (commander НАВ-1, НАВ-2, second pilot НАВ-1, НАВ-2): the screen group and the click quads",
     "// (top-left, top-right, bottom-left, bottom-right): the left buttons 0..5, the right 6..11, the bottom PWR SEL MNU",
     "constexpr int kMfdGroup[4] = {%s};" % ", ".join("G_mfd_%d" % i for i in range(4)),
     "constexpr int kMfdBtnGroup[4] = {%s};" % ", ".join("G_mfd_btn_%d" % i for i in range(4)),
     "constexpr double kMfdLeft[4][4][3] = {%s};" % ", ".join(Q4(INFO[("mfd", i)]["left"]) for i in range(4)),
     "constexpr double kMfdRight[4][4][3] = {%s};" % ", ".join(Q4(INFO[("mfd", i)]["right"]) for i in range(4)),
     "constexpr double kMfdBottom[4][4][3] = {%s};" % ", ".join(Q4(INFO[("mfd", i)]["bottom"]) for i in range(4)),
     "// the button labels' texture cab_lbl (dynamic, mesh texture %d): %d × %d, cells %d × %d, MFD m button b -> cell m·15 + b" % (TI["cab_lbl"], LBL_W, LBL_H, LBL_CW, LBL_CH),
     "constexpr int kLblTex = %d, kLblW = %d, kLblH = %d, kLblCW = %d, kLblCH = %d;" % (TI["cab_lbl"], LBL_W, LBL_H, LBL_CW, LBL_CH),
     "// the ships' MFDs (0 commander, 1 second pilot): the screen and the key columns (keys 0..5 left, 6..11 right) as click quads",
     "constexpr int kShipScreenGroup[2] = {G_ship_screen_K, G_ship_screen_2};",
     "constexpr double kShipScreen[2][4][3] = {%s};" % ", ".join(Q4(INFO[("ship", p)]["screen"]) for p in range(2)),
     "constexpr double kShipLeft[2][4][3] = {%s};" % ", ".join(Q4(INFO[("ship", p)]["left"]) for p in range(2)),
     "constexpr double kShipRight[2][4][3] = {%s};" % ", ".join(Q4(INFO[("ship", p)]["right"]) for p in range(2)),
     "// their texture cab_ship (dynamic, mesh texture %d): %d × %d; pilot p's screen at x p·512, 512 × 340; key k's cell" % (TI["cab_ship"], SHIP_W, SHIP_H),
     "// at x p·512 + (k mod 8)·64, y 352 + (k div 8)·40, 64 × 32",
     "constexpr int kShipTex = %d, kShipW = %d, kShipH = %d, kShipScrW = %d, kShipScrH = %d;" % (TI["cab_ship"], SHIP_W, SHIP_H, SHIP_SCR[0], SHIP_SCR[1]),
     "// OrbiterCrew (the person inside, as in «Тантра»): each Orbiter MFD's screen has its own texture slot - ShipView's",
     "// MfdBank binds the MFD's surface there; the screens' corners (top-left, top-right, bottom-left, bottom-right)",
     "constexpr int kMfdTex[4] = {%s};" % ", ".join(str(TI["cab_mfd%d" % i]) for i in range(4)),
     "constexpr double kMfdScreen[4][4][3] = {%s};" % ", ".join(Q4(INFO[("mfd", i)]["screen"]) for i in range(4)),
     "// the seats: the hips (stature 1,74, the couch at its middle) and the facing (+Z); the hands' targets: the inboard",
     "// yoke grip (its two ends), the hub and a point 1 m on toward the eye (the hub's facing), the МАРШ knob",
     "constexpr double kHip[2][3] = {%s, %s};" % (V3(INFO[("hip", 0)]), V3(INFO[("hip", 1)])),
     "constexpr double kGrip[2][2][3] = {%s, %s};" % ("{" + V3(INFO[("grip", 0)][0]) + ", " + V3(INFO[("grip", 0)][1]) + "}", "{" + V3(INFO[("grip", 1)][0]) + ", " + V3(INFO[("grip", 1)][1]) + "}"),
     "constexpr double kHubN[2][3] = {%s, %s};" % (V3(INFO[("hubn", 0)]), V3(INFO[("hubn", 1)])),
     "constexpr double kMarch[2][3] = {%s, %s};" % (V3(INFO[("march", 0)]), V3(INFO[("march", 1)])),
     "constexpr double kDrumFloorY = %.4f, kDrumHatch[3] = %s, kDrumHatchR = 0.42;   // the drum's floor; its axial hatch (left end)" % (YFL, V3((CX, CY, -WD / 2))),
     "", "}  // namespace tantra::lander::cabin"]
open(HDR, "w", encoding="utf-8").write("\n".join(L) + "\n")
print("ok", len(ORDER), "groups", sum(len(G[n].t) for n in ORDER), "tris; drum", len(DRUM), "module", len(MODULE["K"]), len(MODULE["2"]))
print("drum wall facets kept:", len(G["drum_wall"].t) // 2, "of", NA * NZ, "; caps", len(G["drum_caps"].t) // 2, "of", 2 * NA * 6)
