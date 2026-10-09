# -*- coding: utf-8 -*-
"""Кабина пилотов «Грани» 25,4 м для Orbiter (вид из кабины, MESHVIS_VC): барабан на двоих (ось поперёк, R 1,10, ширина 3,0),
сплошной изогнутый экран 160°, ложементы «колени вверх» с нишей под ранец «Каркаса» и вкладышем для комбинезона, штурвалы,
рукояти МАРШ/ЧАШИ, шар курсора, пульт клавиш, фотонный вычислитель, МФД под правой рукой. Утверждено пользователем 2026-10-09
(мокап Tantra_Design/lander_cockpit_3d.html, вариант 3). Система спецификации: x вперёд, y вверх, z вправо, начало = начало меша
Lander.msh (= TLANDER), метры ×1,285 уже. Запуск: python gen_lander_cabin.py -> out/LanderCabin.msh, out/textures/cab_*.dds,
Orbitersdk/samples/TantraLander/orbiter/LanderCabinMesh.h"""
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out"); TEX = os.path.join(OUT, "textures")
HDR = os.path.join(HERE, "..", "..", "Orbitersdk", "samples", "TantraLander", "orbiter", "LanderCabinMesh.h")
os.makedirs(TEX, exist_ok=True)
D2R = math.pi / 180

# ------------------------------------------------------------------ the layout (as the mockup)
YF = -0.58                       # the deck (the salon floor −0,45 ×1,285)
RD, WD, CX, CY = 1.10, 3.00, 6.95, YF + 1.13          # the drum
BACK = 40 * D2R; BDX, BDY = -math.cos(BACK), math.sin(BACK)
SEATS = [-0.62, 0.62]            # the couches' centre lines (z): the commander left, the second pilot right
def hip(zc): return (CX - 0.05, CY - 0.55, zc)
def eye(zc): h = hip(zc); return (h[0] + BDX * 0.78 + 0.08, h[1] + BDY * 0.78 + 0.06, zc)
def along(h, d, off=0.0): return (h[0] + BDX * d - BDY * off, h[1] + BDY * d + BDX * off)

# ------------------------------------------------------------------ the mesh
class Grp:
    def __init__(s, name, mat, tex=0):
        s.name, s.mat, s.tex, s.v, s.n, s.uv, s.t = name, mat, tex, [], [], [], []
    def tri(s, a, b, c, uva=(0, 0), uvb=(0, 0), uvc=(0, 0), na=None, nb=None, nc=None):
        a, b, c = map(np.asarray, (a, b, c))
        fn = np.cross(b - a, c - a); L = np.linalg.norm(fn)
        if L < 1e-12: return
        fn /= L; k = len(s.v)
        for p, n, uv in ((a, na, uva), (b, nb, uvb), (c, nc, uvc)):
            s.v.append(p); s.n.append(fn if n is None else np.asarray(n) / np.linalg.norm(n)); s.uv.append(uv)
        s.t.append((k, k + 1, k + 2))
    def quad(s, a, b, c, d, uv=((0, 0), (1, 0), (1, 1), (0, 1))):
        s.tri(a, b, c, uv[0], uv[1], uv[2]); s.tri(a, c, d, uv[0], uv[2], uv[3])
G = {}
def grp(name, mat, tex=0):
    G[name] = Grp(name, mat, tex); return G[name]

def rotz(a):
    c, s = math.cos(a), math.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
def box(g, size, centre, R=np.eye(3)):
    sx, sy, sz = size; c = np.asarray(centre, float)
    P = [c + R @ np.array([sx / 2 * i, sy / 2 * j, sz / 2 * k]) for i in (-1, 1) for j in (-1, 1) for k in (-1, 1)]
    F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    for f in F:
        q = [P[i] for i in f]
        n = np.cross(q[1] - q[0], q[2] - q[0])
        if np.dot(n, (q[0] + q[2]) / 2 - c) < 0: q = q[::-1]
        g.quad(*q)
def cyl(g, a, b, r, nseg=16, caps=True):
    a, b = np.asarray(a, float), np.asarray(b, float); ax = b - a; L = np.linalg.norm(ax); ax /= L
    u = np.cross(ax, [0, 1, 0] if abs(ax[1]) < 0.9 else [1, 0, 0]); u /= np.linalg.norm(u); w = np.cross(ax, u)
    ring = [u * math.cos(2 * math.pi * k / nseg) + w * math.sin(2 * math.pi * k / nseg) for k in range(nseg)]
    for k in range(nseg):
        p0, p1 = ring[k], ring[(k + 1) % nseg]
        g.tri(a + r * p0, a + r * p1, b + r * p1, na=p0, nb=p1, nc=p1); g.tri(a + r * p0, b + r * p1, b + r * p0, na=p0, nb=p1, nc=p0)
        if caps: g.tri(b, b + r * p0, b + r * p1); g.tri(a, a + r * p1, a + r * p0)
def sph(g, c, r, nu=12, nv=8):
    c = np.asarray(c, float)
    P = lambda i, j: c + r * np.array([math.sin(math.pi * j / nv) * math.cos(2 * math.pi * i / nu), math.cos(math.pi * j / nv), math.sin(math.pi * j / nv) * math.sin(2 * math.pi * i / nu)])
    for i in range(nu):
        for j in range(nv):
            a, b, cc, d = P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1)
            g.tri(a, cc, b, na=a - c, nb=cc - c, nc=b - c); g.tri(a, d, cc, na=a - c, nb=d - c, nc=cc - c)
def panel(g, w, h, centre, look, uv=((0, 1), (1, 1), (1, 0), (0, 0)), lift=0.0):
    """a rectangle w × h facing the point «look» (spec), its top edge up; uv: bottom-left, bottom-right, top-right, top-left"""
    c = np.asarray(centre, float); n = np.asarray(look, float) - c; n /= np.linalg.norm(n)
    r = np.cross([0, 1, 0], n); r /= np.linalg.norm(r); up = np.cross(n, r)
    c = c + n * lift
    q = [c - r * w / 2 - up * h / 2, c + r * w / 2 - up * h / 2, c + r * w / 2 + up * h / 2, c - r * w / 2 + up * h / 2]
    g.quad(q[0], q[1], q[2], q[3], uv)
def ribbon(g, P, t0, t1, y0, y1, n, off=0.0, inward=True):
    for i in range(n):
        ta, tb = t0 + (t1 - t0) * i / n, t0 + (t1 - t0) * (i + 1) / n
        def pt(t, y):
            p, q = P(t), P(t + 1e-4); tx, tz = q[0] - p[0], q[1] - p[1]; L = math.hypot(tx, tz)
            return np.array([p[0] + tz / L * off, y, p[1] - tx / L * off])
        a, b, c, d = pt(ta, y0), pt(tb, y0), pt(tb, y1), pt(ta, y1)
        ua, ub = i / n, (i + 1) / n
        if inward: g.quad(a, d, c, b, ((ua, 1), (ua, 0), (ub, 0), (ub, 1)))
        else: g.quad(a, b, c, d, ((ua, 1), (ub, 1), (ub, 0), (ua, 0)))

# materials: name, diffuse, specular (power), emissive
MATS = [("metal", (0.46, 0.49, 0.51), (0.35, 0.35, 0.35, 20), (0, 0, 0)), ("dark", (0.20, 0.22, 0.24), (0.3, 0.3, 0.3, 15), (0, 0, 0)),
        ("cushion", (0.11, 0.12, 0.13), (0.05, 0.05, 0.05, 5), (0, 0, 0)), ("stick", (0.08, 0.09, 0.10), (0.4, 0.4, 0.4, 30), (0, 0, 0)),
        ("screen", (0, 0, 0), (0.2, 0.2, 0.2, 40), (1, 1, 1)), ("keylit", (0.06, 0.25, 0.16), (0.2, 0.2, 0.2, 20), (0.25, 0.82, 0.54)),
        ("keydark", (0.13, 0.15, 0.16), (0.2, 0.2, 0.2, 20), (0, 0, 0)), ("mfd", (0, 0, 0), (0, 0, 0, 1), (0, 0, 0)),
        ("red", (0.55, 0.06, 0.06), (0.3, 0.3, 0.3, 20), (0.35, 0, 0)), ("bronze", (0.54, 0.43, 0.23), (0.6, 0.55, 0.4, 40), (0, 0, 0))]
MI = {m[0]: i + 1 for i, m in enumerate(MATS)}
TEXTURES = ["cab_pano", "cab_yoke", "cab_phos", "cab_keys", "cab_metal"]
TI = {t: i + 1 for i, t in enumerate(TEXTURES)}

# ------------------------------------------------------------------ the drum (its inner wall and the end caps; the axial hatch left)
g = grp("drum_wall", MI["metal"], TI["cab_metal"])
N = 72
for i in range(N):
    a0, a1 = 2 * math.pi * i / N, 2 * math.pi * (i + 1) / N
    p = lambda a, z: np.array([CX + RD * math.cos(a), CY + RD * math.sin(a), z])
    A, B, C2, Dd = p(a0, -WD / 2), p(a1, -WD / 2), p(a1, WD / 2), p(a0, WD / 2)
    u0, u1 = i / N * 8, (i + 1) / N * 8
    g.quad(A, Dd, C2, B, ((u0, 0), (u0, 3), (u1, 3), (u1, 0)))                 # facing the axis (seen from inside)
gc = grp("drum_caps", MI["dark"])
for e, rin in ((-1, 0.42), (1, 0.10)):
    z = e * WD / 2
    for i in range(N):
        a0, a1 = 2 * math.pi * i / N, 2 * math.pi * (i + 1) / N
        P = [np.array([CX + r * math.cos(a), CY + r * math.sin(a), z]) for r, a in ((rin, a0), (RD, a0), (RD, a1), (rin, a1))]
        if e < 0: gc.quad(P[0], P[1], P[2], P[3])      # the left cap faces +z (inside)
        else: gc.quad(P[0], P[3], P[2], P[1])
# the deck inside the drum (a flat floor over the drum's bottom) and the hatch's rim on the left cap
gd = grp("drum_floor", MI["dark"], TI["cab_metal"])
yfl = CY - 0.95
xs = math.sqrt(RD ** 2 - (yfl - CY) ** 2)
gd.quad((CX - xs, yfl, -WD / 2), (CX - xs, yfl, WD / 2), (CX + xs, yfl, WD / 2), (CX + xs, yfl, -WD / 2), ((0, 0), (0, 3), (1.1, 3), (1.1, 0)))
gh = grp("drum_hatch_rim", MI["bronze"])
for i in range(36):
    a0, a1 = 2 * math.pi * i / 36, 2 * math.pi * (i + 1) / 36
    cyl(gh, (CX + 0.42 * math.cos(a0), CY + 0.42 * math.sin(a0), -WD / 2 + 0.02), (CX + 0.42 * math.cos(a1), CY + 0.42 * math.sin(a1), -WD / 2 + 0.02), 0.025, 6, False)

# ------------------------------------------------------------------ the continuous screen (160° in front of both, above the knees)
E0 = eye(SEATS[0])
P = lambda t: (6.40 + 0.95 * math.cos(t), 1.40 * math.sin(t))
ribbon(grp("pano_screen", MI["screen"], TI["cab_pano"]), P, -80 * D2R, 80 * D2R, E0[1] + 0.12, E0[1] + 0.70, 64)
ribbon(grp("pano_frame", MI["dark"]), P, -81 * D2R, 81 * D2R, E0[1] + 0.06, E0[1] + 0.76, 64, off=0.03)

# ------------------------------------------------------------------ the couches, the yokes, the levers, the MFDs
HUB, MFD, LEVER = {}, {}, {}
for zc in SEATS:
    s = "L" if zc < 0 else "R"
    h = hip(zc); e = eye(zc)
    gs = grp("seat_" + s, MI["dark"], TI["cab_metal"]); gi = grp("insert_" + s, MI["cushion"])
    box(gs, (0.40, 0.07, 0.62), (h[0] + 0.05, h[1] - 0.06, zc), rotz(30 * D2R))
    c = along(h, 0.46); cb = along(h, 0.46, 0.24)
    for k in (-1, 1): box(gs, (0.92, 0.30, 0.07), (c[0] - 0.04, c[1] - 0.04, zc + k * 0.30), rotz(-BACK))
    box(gs, (0.92, 0.05, 0.62), (cb[0], cb[1], zc), rotz(-BACK))
    box(gi, (0.86, 0.24, 0.50), ((c[0] + cb[0]) / 2, (c[1] + cb[1]) / 2 + 0.02, zc), rotz(-BACK))
    hr = along(h, 0.86, 0.10); box(gs, (0.07, 0.30, 0.66), (hr[0] + 0.02, hr[1] + 0.12, zc), rotz(-BACK + 0.1))
    box(gs, (0.45, 0.06, 0.40), (h[0] + 0.62, h[1] + 0.32, zc), rotz(-40 * D2R))
    for k in (-1, 1): box(gs, (0.46, 0.06, 0.09), (h[0] - 0.04, h[1] + 0.22, zc + k * 0.36), rotz(5 * D2R))
    cyl(gs, (h[0] + 0.10, yfl, zc), (h[0] + 0.10, h[1] - 0.08, zc), 0.05, 12)          # the column of the couch
    # the levers МАРШ, ЧАШИ and the cursor ball on the left armrest's front
    gl = grp("levers_" + s, MI["stick"]); lz = zc - 0.36
    for i in (0, 1):
        lx = h[0] + 0.14 + i * 0.07
        cyl(gl, (lx, h[1] + 0.26, lz), (lx + 0.03, h[1] + 0.40, lz), 0.014, 10)
        box(grp("lever_knob_%s%d" % (s, i), MI["keylit"] if i else MI["dark"]), (0.05, 0.035, 0.035), (lx + 0.035, h[1] + 0.415, lz))
    sph(gl, (h[0] + 0.02, h[1] + 0.27, lz), 0.028)
    LEVER[s] = (h[0] + 0.175, h[1] + 0.415, lz)
    # the yoke: a column from the drum's floor between the knees to the hub before the chest
    hub = np.array([h[0] + 0.33, h[1] + 0.58, zc]); base = np.array([h[0] + 0.50, yfl, zc])
    gy = grp("yoke_" + s, MI["stick"])
    cyl(gy, base, hub - np.array([0, 0.04, 0]), 0.035, 12)
    n = np.array(e) - hub; n /= np.linalg.norm(n); r = np.cross([0, 1, 0], n); r /= np.linalg.norm(r); up = np.cross(n, r)
    cyl(gy, hub - n * 0.025, hub + n * 0.025, 0.07, 20)
    for k in (-1, 1):
        g0 = hub + r * k * 0.16 - up * 0.06; g1 = hub + r * k * 0.19 - up * 0.16
        cyl(gy, hub, g0, 0.016, 8); cyl(gy, g0, g1, 0.022, 10)
        sph(grp("yoke_btn_%s%s" % (s, "R" if k > 0 else "L"), MI["red"] if k > 0 else MI["dark"]), g0 + up * 0.025, 0.014, 8, 6)
    panel(grp("yoke_screen_" + s, MI["screen"], TI["cab_yoke"]), 0.12, 0.06, hub + up * 0.012, np.array(e), lift=0.027)
    HUB[s] = hub
    # the MFD under the right hand: the commander's on the console's left, the second's outboard
    mp = np.array([e[0] + 0.72, e[1] - 0.15, zc + 0.38])
    panel(grp("mfd_bezel_" + s, MI["dark"]), 0.34, 0.40, mp, np.array(e), lift=-0.012)
    panel(grp("mfd_" + s, MI["mfd"]), 0.26, 0.26, mp + np.array([0, 0.03, 0]), np.array(e), uv=((0, 1), (1, 1), (1, 0), (0, 0)), lift=0.002)
    MFD[s] = mp
# the console between the couches, the keys (lit / dark), the photonic computer's phosphor
gk = grp("console", MI["dark"], TI["cab_metal"])
box(gk, (0.50, 0.40, 0.40), (CX - 0.05, CY - 0.62, 0))
KEYS = ["ЧАШИ", "МАРШ", "КРИО А", "КРИО Б", "НАКОП", "ФОТОН", "АНАЛОГ", "ПЕРЕК", "ВДУВ", "ШАССИ", "КИЛЬ", "ЛЮК"]
DARK = {3, 8, 10, 11}
kl, kd = grp("keys_lit", MI["keylit"], TI["cab_keys"]), grp("keys_dark", MI["keydark"], TI["cab_keys"])
KEYPOS = []
for i, lbl in enumerate(KEYS):
    r_, c_ = divmod(i, 4)
    x = CX - 0.28 + 0.40 * (r_ + 0.5) / 3; z = -0.16 + 0.32 * (c_ + 0.5) / 4; y = CY - 0.42
    u0, v0 = (i % 4) / 4, (i // 4) / 3
    uv = ((u0, v0 + 1 / 3), (u0 + 0.25, v0 + 1 / 3), (u0 + 0.25, v0), (u0, v0))
    gg = kd if i in DARK else kl
    k0 = len(gg.t)
    box(gg, (0.07, 0.025, 0.07), (x, y + 0.0125, z))
    # the label on the key's top: re-map the top face's uvs to its atlas cell (the top = the face with +y normal)
    for ti in range(k0, len(gg.t)):
        a, b, cc = gg.t[ti]
        if all(gg.n[q][1] > 0.9 for q in (a, b, cc)):
            for q in (a, b, cc):
                px, pz = gg.v[q][0], gg.v[q][2]
                fu = (pz - (z - 0.035)) / 0.07; fv = 1 - (px - (x - 0.035)) / 0.07
                gg.uv[q] = (u0 + 0.25 * fu, v0 + (1 / 3) * fv)
    KEYPOS.append((x, y + 0.025, z, lbl))
panel(grp("phosphor", MI["screen"], TI["cab_phos"]), 0.20, 0.20, (CX + 0.24, CY - 0.30, 0), np.array(E0) * 0 + np.array([E0[0], E0[1], 0]))
box(grp("phosphor_box", MI["dark"]), (0.06, 0.30, 0.16), (CX + 0.27, CY - 0.30, 0))

# ------------------------------------------------------------------ textures
def font(sz):
    for f in ("arialnb.ttf", "arialbd.ttf", "arial.ttf"):
        try: return ImageFont.truetype(os.path.join("C:/Windows/Fonts", f), sz)
        except OSError: pass
    return ImageFont.load_default()
def save(name, img):
    img.save(os.path.join(TEX, name + ".png")); img.convert("RGB").save(os.path.join(TEX, name + ".dds"))
rng = np.random.default_rng(3)
W, H = 2048, 512; hz = int(H * 0.56)
pano = Image.new("RGB", (W, H)); d = ImageDraw.Draw(pano)
for y in range(hz):
    t = y / hz
    c0, c1, c2 = np.array((13, 27, 51)), np.array((47, 79, 116)), np.array((215, 154, 98))
    col = c0 + (c1 - c0) * min(1, t / 0.55) if t < 0.55 else c1 + (c2 - c1) * (t - 0.55) / 0.45
    d.line([(0, y), (W, y)], fill=tuple(int(v) for v in col))
pts = [(0, hz)] + [(x, hz - 12 - 30 * abs(math.sin(x * 0.0032) * math.sin(x * 0.0011 + 1.3)) - 7 * math.sin(x * 0.045)) for x in range(0, W + 16, 16)] + [(W, hz)]
d.polygon(pts, fill=(26, 32, 37))
for y in range(hz, H):
    t = (y - hz) / (H - hz); v = int(43 - 23 * t); d.line([(0, y), (W, y)], fill=(v, v - 1, v - 4))
for i in range(-40, 41):
    d.line([(W / 2 + i * 8, hz), (W / 2 + i * 170, H)], fill=(70, 66, 54))
for k in range(1, 14):
    y = hz + (H - hz) * (k / 14) ** 2.2; d.line([(0, y), (W, y)], fill=(70, 66, 54))
for k in range(18):
    t = k / 18; y = hz + (H - hz) * t * t
    for sgn in (-1, 1):
        x = W / 2 + sgn * (6 + 280 * t * t); d.rectangle([x - 1, y - 1, x + 2 + 4 * t, y + 1 + 2 * t], fill=(255, 210, 138))
for _ in range(90):
    x, y = rng.random() * W, rng.random() * hz * 0.45; d.point((x, y), fill=(230, 230, 235))
save("cab_pano", pano)
yk = Image.new("RGB", (256, 128), (6, 16, 10)); d = ImageDraw.Draw(yk)
d.pieslice([78, 14, 178, 114], 180, 360, fill=(58, 110, 158)); d.pieslice([78, 14, 178, 114], 0, 180, fill=(107, 74, 43))
for k in range(-2, 3): d.line([(112, 64 - k * 14), (144, 64 - k * 14)], fill=(255, 255, 255), width=2)
d.line([(98, 64), (118, 64), (128, 72), (138, 64), (158, 64)], fill=(240, 192, 32), width=4)
f = font(18); d.text((8, 22), "V", font=f, fill=(168, 197, 106)); d.text((8, 46), "612", font=f, fill=(168, 197, 106))
d.text((196, 22), "H", font=f, fill=(168, 197, 106)); d.text((190, 46), "12,5", font=f, fill=(168, 197, 106)); d.text((8, 98), "n 1,3", font=f, fill=(168, 197, 106))
save("cab_yoke", yk)
ph = Image.new("RGB", (256, 256), (3, 16, 7)); d = ImageDraw.Draw(ph)
for k in range(3):
    pp = [(128 + (78 - k * 18) * math.sin(3 * t + k), 128 + (78 - k * 18) * math.sin(2 * t)) for t in np.linspace(0, 2 * math.pi, 400)]
    d.line(pp, fill=(127, 224, 160), width=2)
save("cab_phos", ph)
ks = Image.new("RGB", (512, 192), (0, 0, 0)); d = ImageDraw.Draw(ks); f = font(30)
for i, lbl in enumerate(KEYS):
    cx, cy = (i % 4) * 128 + 64, (i // 4) * 64 + 32
    tw = d.textlength(lbl, font=f); d.text((cx - tw / 2, cy - 17), lbl, font=f, fill=(223, 231, 226))
save("cab_keys", ks)
mt = (0.32 + 0.05 * rng.random((512, 512)))
for i in range(0, 512, 128): mt[i:i + 3, :] *= 0.45; mt[:, i:i + 3] *= 0.45
save("cab_metal", Image.fromarray((np.stack([mt, mt * 1.03, mt * 1.06], -1) * 255).clip(0, 255).astype(np.uint8)))

# ------------------------------------------------------------------ write the mesh (Orbiter: X = z, Y = y, Z = x; the index order kept - see gen_lander.py)
ORDER = list(G)
def O(p): return (p[2], p[1], p[0])
with open(os.path.join(OUT, "LanderCabin.msh"), "w", newline="\r\n", encoding="ascii") as fo:
    fo.write("MSHX1\nGROUPS %d\n" % len(ORDER))
    for nm in ORDER:
        g = G[nm]
        fo.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d ;%s\n" % (nm, g.mat, g.tex, len(g.v), len(g.t), nm))
        for p, n, uv in zip(g.v, g.n, g.uv):
            fo.write("%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*O(p), *O(n), uv[0], uv[1]))
        for t in g.t: fo.write("%d %d %d\n" % t)
    fo.write("MATERIALS %d\n" % len(MATS))
    for m in MATS: fo.write(m[0] + "\n")
    for nm, dc, sp, em in MATS:
        fo.write("MATERIAL %s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1 %.0f\n%.3f %.3f %.3f 1\n" % (nm, *dc, *dc, *sp, *em))
    fo.write("TEXTURES %d\n" % len(TEXTURES))
    for t in TEXTURES: fo.write("Tantra\\Lander\\%s.dds\n" % t)

# ------------------------------------------------------------------ the header: the groups, the drum, the eyes, the MFDs, the hands' targets
def V3(p): return "{%.4f, %.4f, %.4f}" % tuple(O(p))
L = ["// «Грань» 25,4 м: the pilots' cabin mesh LanderCabin.msh (the drum for two). GENERATED by",
     "// Tantra_Design/lander_mesh/gen_lander_cabin.py - do not edit. Orbiter frame (X right, Y up, Z forward), the hull mesh's origin.",
     "#pragma once", "", "namespace tantra::lander::cabin {", "",
     "constexpr int kGroupCount = %d;" % len(ORDER),
     "enum Grp { " + ", ".join("G_%s = %d" % (n, i) for i, n in enumerate(ORDER)) + " };",
     "// the drum turns about the lateral axis (Orbiter X) through kDrumC; everything in it but the walls of the hull turns",
     "constexpr double kDrumC[3] = %s, kDrumR = %.3f, kDrumW = %.3f;" % (V3((CX, CY, 0)), RD, WD),
     "constexpr int kDrumGroups[] = {" + ", ".join("G_%s" % n for n in ORDER) + "};",
     "// the pilots: the eyes (camera), the yoke hubs, the levers, the MFDs (left = commander, right = second pilot)",
     "constexpr double kEye[2][3] = {%s, %s};" % (V3(eye(SEATS[0])), V3(eye(SEATS[1]))),
     "constexpr double kHub[2][3] = {%s, %s};" % (V3(HUB["L"]), V3(HUB["R"])),
     "constexpr double kLever[2][3] = {%s, %s};" % (V3(LEVER["L"]), V3(LEVER["R"])),
     "constexpr double kMfd[2][3] = {%s, %s};" % (V3(MFD["L"]), V3(MFD["R"])),
     "constexpr double kScreenC[3] = %s;   // the panorama's middle (the default view)" % V3((6.40 + 0.95, E0[1] + 0.41, 0)),
     "constexpr double kHip[2][3] = {%s, %s};" % (V3(hip(SEATS[0])), V3(hip(SEATS[1]))),
     "", "}  // namespace tantra::lander::cabin"]
open(HDR, "w", encoding="utf-8").write("\n".join(L) + "\n")
print("ok", len(ORDER), "groups", sum(len(G[n].t) for n in ORDER), "tris;", ", ".join(ORDER))
