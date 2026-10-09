# -*- coding: utf-8 -*-
"""«Грань-Н2» (Т1Б-Н2): детальный меш + аэродинамика по мешу.
Система спецификации: x вперёд, y вверх, z вправо (правая), начало — ЦМ, метры.
Orbiter .msh: X=z, Y=y, Z=x (левая система) — перестановка x<->z меняет и обход, и хиральность, поэтому
треугольники, обходящиеся против часовой снаружи в системе спецификации, в .msh становятся «по часовой» (лицевые D3D).
Запуск: python gen_lander.py  -> out/Lander.msh, out/lander_mesh.json
"""
import json, math, os
import numpy as np
from scipy.interpolate import PchipInterpolator
from scipy.special import ellipe

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = json.load(open(os.path.join(HERE, "..", "lander_gran_v2.json"), encoding="utf-8"))
G = SPEC["geometry"]
# Т1Б-А: нос короче на 0,215 м (вход в проём ангара 20,0 м). Сечения x ≤ +6 не трогаются; зона +6…+10 сжимается
# в (4 − 0,215)/4; кромочный монолит x ≥ +10 (притупление) переносится целиком на −0,215 м; кончик +11,0 → +10,785.
NOSE_DL = 0.215
def nose_map(x):
    if x <= 6.0:
        return x
    if x <= 10.0:
        return 6.0 + (x - 6.0) * (4.0 - NOSE_DL) / 4.0
    return x - NOSE_DL
G = dict(G); G["hull_sections"] = [dict(s, x=nose_map(s["x"])) for s in G["hull_sections"]]
X_TIP = nose_map(11.0)
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)

# ----------------------------------------------------------------------------- материалы
MATERIALS = [  # name, diffuse rgb, specular rgb + power, emissive
    ("lak_korpus",  (0.110, 0.118, 0.141), (0.35, 0.36, 0.40, 40), (0, 0, 0)),   # боразоно-циркониевый лак
    ("bor_dnische", (0.075, 0.068, 0.062), (0.10, 0.10, 0.10, 10), (0, 0, 0)),   # борная керамика днища, темнее
    ("boraz_nos",   (0.290, 0.275, 0.255), (0.15, 0.15, 0.15, 8), (0, 0, 0)),    # пористый боразон носа
    ("metall",      (0.360, 0.300, 0.225), (0.55, 0.50, 0.42, 50), (0, 0, 0)),   # Г: бериллиевая бронза с боразонным покрытием (рамы, опоры, обрамления, баки)
    ("katushka",    (0.788, 0.635, 0.290), (0.60, 0.50, 0.30, 40), (0, 0, 0)),   # корпус катушки
    ("svechenie",   (0.373, 0.831, 1.000), (0, 0, 0, 1), (0.373, 0.831, 1.000)), # функциональное свечение
    ("vnutr",       (0.040, 0.044, 0.052), (0.05, 0.05, 0.05, 5), (0, 0, 0)),    # внутренность чаш/срезов
    ("svechenie_dat", (0.373, 0.831, 1.000), (0, 0, 0, 1), (0.373, 0.831, 1.000)), # Г: фотонная полоса датчиков — светится всегда
]
# Г: текстуры (out/textures, 2 x 2 м на текстуру, UV — кубическая проекция по нормали грани)
TEXTURES = ["lak_korpus", "bor_dnische", "boraz_nos", "bronza_boraz", "katushka"]
MAT_TEX = {"lak_korpus": 1, "bor_dnische": 2, "boraz_nos": 3, "metall": 4, "katushka": 5}
TEX_DIR_ORB = "Tantra\\Lander\\"   # подкаталог Orbiter Textures (файлы .dds из out/textures)
UV_M = 2.0
def uv_tri(a, b, c):
    n = np.abs(np.cross(b - a, c - a)); k = int(np.argmax(n))
    ax = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[k]
    return [(p[ax[0]] / UV_M, -p[ax[1]] / UV_M) for p in (a, b, c)]
MAT = {m[0]: i + 1 for i, m in enumerate(MATERIALS)}
HEX = {m[0]: "#%02x%02x%02x" % tuple(int(255 * c) for c in m[1]) for m in MATERIALS}


class Group:
    def __init__(self, name, mat, purpose):
        self.name, self.mat, self.purpose = name, mat, purpose
        self.v, self.n, self.t = [], [], []

    def tri(self, a, b, c, na=None, nb=None, nc=None):
        a, b, c = np.asarray(a, float), np.asarray(b, float), np.asarray(c, float)
        fn = np.cross(b - a, c - a)
        ar = np.linalg.norm(fn)
        if ar < 1e-10:
            return
        fn = fn / ar
        k = len(self.v)
        for p, nn in ((a, na), (b, nb), (c, nc)):
            self.v.append(p)
            self.n.append(fn if nn is None else np.asarray(nn, float) / (np.linalg.norm(nn) + 1e-12))
        self.t.append((k, k + 1, k + 2))

    def quad(self, a, b, c, d, na=None, nb=None, nc=None, nd=None):
        self.tri(a, b, c, na, nb, nc)
        self.tri(a, c, d, na, nc, nd)

    def volume(self):
        V = 0.0
        for i, j, k in self.t:
            V += np.dot(self.v[i], np.cross(self.v[j], self.v[k])) / 6.0
        return V

    def flip(self):
        self.t = [(i, k, j) for i, j, k in self.t]
        self.n = [-n for n in self.n]


GROUPS = {}


def grp(name, mat, purpose):
    g = Group(name, MAT[mat], purpose)
    GROUPS[name] = g
    return g


def loft(rings, emit, smooth_cols=(), cap0=True, cap1=True, capgrp=None):
    """rings: список колец (N точек, замкнутых). emit(col)->Group. Возвращает список (группа, tri) — ориентацию правим по объёму."""
    R = [np.asarray(r, float) for r in rings]
    N = len(R[0])
    tris = []  # (group, a,b,c, na,nb,nc)
    # сглаженные нормали для smooth-колонок: усредняем по соседям кольца
    def vnorm(i, j):
        r = R[i]
        prev = R[max(i - 1, 0)][j]; nxt = R[min(i + 1, len(R) - 1)][j]
        tang_long = nxt - prev
        tang_ring = r[(j + 1) % N] - r[(j - 1) % N]
        return np.cross(tang_long, tang_ring)
    for i in range(len(R) - 1):
        for j in range(N):
            j2 = (j + 1) % N
            a, b, c, d = R[i][j], R[i + 1][j], R[i + 1][j2], R[i][j2]
            loft.cur_i = i
            g = emit(j)
            if j in smooth_cols:
                ns = [vnorm(i, j), vnorm(i + 1, j), vnorm(i + 1, j2), vnorm(i, j2)]
                tris.append((g, a, b, c, ns[0], ns[1], ns[2])); tris.append((g, a, c, d, ns[0], ns[2], ns[3]))
            else:
                tris.append((g, a, b, c, None, None, None)); tris.append((g, a, c, d, None, None, None))
    cg = capgrp or emit(0)
    for idx, flip in ((0, True), (len(R) - 1, False)):
        if (idx == 0 and not cap0) or (idx != 0 and not cap1):
            continue
        r = R[idx]; cen = r.mean(axis=0)
        for j in range(N):
            a, b = r[j], r[(j + 1) % N]
            tris.append((cg, cen, b, a, None, None, None) if not flip else (cg, cen, a, b, None, None, None))
    # ориентация: знак объёма всего тела
    V = sum(np.dot(t[1], np.cross(t[2], t[3])) for t in tris) / 6.0
    for g, a, b, c, na, nb, nc in tris:
        if V < 0:
            g.tri(a, c, b, None if na is None else -na, None if nc is None else -nc, None if nb is None else -nb)
        else:
            g.tri(a, b, c, na, nb, nc)
    return abs(V)


# ----------------------------------------------------------------------------- корпус
HS = G["hull_sections"]
xs = np.array([s["x"] for s in HS])
F = {k: PchipInterpolator(xs, [s[k] for s in HS]) for k in ("hw", "yt", "yb", "ch")}
hw = lambda x: float(F["hw"](x)); yt = lambda x: float(F["yt"](x)); yb = lambda x: float(F["yb"](x)); chf = lambda x: float(F["ch"](x))


def section(x):
    h, t, b, c = hw(x), yt(x), yb(x), chf(x)
    lo = min(c / 3.0, max(0.0, (t - c) - b))
    # обход вокруг оси x; рёбра: 0 верх, 1 верхн. фаска П, 2 борт П, 3 нижн. фаска П, 4 днище, 5 нижн. фаска Л, 6 борт Л, 7 верхн. фаска Л
    P = [np.array(q, float) for q in [(x, t, -(h - c)), (x, t, h - c), (x, t - c, h), (x, b + lo, h), (x, b, h - c), (x, b, -(h - c)), (x, b + lo, -h), (x, t - c, -h)]]
    # Ф: фаска R ≈ 4 см на каждом из 8 рёбер граней — полоска 2 × 4 см (геометрия) со сглаженными нормалями (вид скругления)
    out = []
    for k in range(8):
        pp, pc, pn = P[k - 1], P[k], P[(k + 1) % 8]
        d0 = min(CHAMF, 0.3 * np.linalg.norm(pc - pp)); d1 = min(CHAMF, 0.3 * np.linalg.norm(pn - pc))
        e0 = (pp - pc) / (np.linalg.norm(pp - pc) + 1e-12); e1 = (pn - pc) / (np.linalg.norm(pn - pc) + 1e-12)
        out += [tuple(pc + e0 * d0), tuple(pc + e1 * d1)]
    return out
CHAMF = 0.04


X_NOSE = nose_map(10.0)
hull_x = sorted(set(list(np.linspace(-9, X_NOSE, 48)) + [s["x"] for s in HS if s["x"] <= X_NOSE]))
nose_x = list(np.linspace(X_NOSE, X_TIP, 10))
g_hull = grp("korpus", "lak_korpus", "Корпус-трапеция, верх и борта, тёмный лак")
g_belly = grp("dnische_tzp", "bor_dnische", "Теплозащита днища (нижние фаски и днище корпуса, низ крыла)")
g_nose = grp("nos_poristy", "boraz_nos", "Лопаточный нос — пористая боразоновая вставка (аварийный вдув аргона)")
all_x = [x for x in hull_x if x < X_NOSE - 1e-6] + nose_x
NH = len([x for x in hull_x if x < X_NOSE - 1e-6])
# столбцы: 2k — полоска фаски ребра k, 2k+1 — грань k (0 верх, 1 фаска П, 2 борт П, 3 нижн. фаска П, 4 днище, 5, 6, 7)
emit_h = lambda j: g_nose if loft.cur_i >= NH else (g_belly if j in (6, 7, 8, 9, 10, 11, 12) else g_hull)
loft([section(x) for x in all_x], emit_h, smooth_cols=set(range(0, 16, 2)), capgrp=g_hull)        # единая замкнутая оболочка: корма — торец, нос — лопатка
for ti in range(len(g_hull.t)):   # торец носа x = 11 отнести к носу: переносим не нужно — capgrp только корма
    pass

# ----------------------------------------------------------------------------- крыло, концевые части, элевоны
def profile(x_le, x_te, y0, half_max, r_le, half_te, n_arc=10):
    """Замкнутый профиль в плоскости (x,y): скруглённая кромка R r_le, полка half_max, задняя кромка half_te."""
    c = x_le - x_te
    pts = []
    xm = x_le - max(0.28 * c, r_le + 0.05)
    up = [(x_te, y0 + half_te), (xm, y0 + half_max)]
    arc = [(x_le - r_le + r_le * math.cos(f), y0 + r_le * math.sin(f)) for f in np.linspace(math.pi / 2, -math.pi / 2, n_arc)]
    lo = [(xm, y0 - half_max), (x_te, y0 - half_te)]
    pts = up + arc + lo
    arc_cols = set(range(1, 1 + n_arc))   # колонки, где нормали сглаживаются (скругление кромки)
    lower_cols = set(range(1 + n_arc // 2, len(pts)))
    return pts, arc_cols, lower_cols


def wing_rings(stations, y0, half_fn, r_le, half_te):
    rings = []
    for z, x_le, x_te in stations:
        pts, arc_cols, lower_cols = profile(x_le, x_te, y0, half_fn(z), r_le, half_te)
        rings.append([(px, py, z) for px, py in pts])
    return rings, arc_cols, lower_cols


# Н3: гиперзвуковой профиль — плоское наветренное днище, выпуклый (клиновидно-двояковыпуклый) верх,
# максимум толщины на 35 % хорды, скруглённая кромка R, задняя кромка с толщиной под шарнир.
Y_WB = -0.90          # плоскость днища крыла
XI_MAX = 0.35
N_UP, N_ARC = 7, 9
def thick_frac(xi, t, t_te, r):
    """Высота верха над днищем на доле хорды xi (0 — кромка)."""
    t0 = 2 * r
    if xi <= XI_MAX:
        return t0 + (t - t0) * math.sin(0.5 * math.pi * xi / XI_MAX)
    return t_te + (t - t_te) * math.cos(0.5 * math.pi * (xi - XI_MAX) / (1 - XI_MAX))

def profile2(x_le, chord, t, r, t_te, xi_end=1.0, x_split=None):
    """Точки кольца: верх (от задней кромки к носку), дуга кромки, плоский низ. xi_end < 1 — срез по шарниру."""
    up = []
    for xi in np.linspace(xi_end, 0.06, N_UP):
        up.append((x_le - xi * chord, Y_WB + thick_frac(xi, t, t_te, r)))
    xc, yc = x_le - r, Y_WB + r
    arc = [(xc + r * math.cos(f), yc + r * math.sin(f)) for f in np.linspace(math.pi / 2, -math.pi / 2, N_ARC)]
    # Ф: низ — 3 точки (середина на 0,4 полной хорды, разбиение по шарниру элевона, конец) — одинаковая топология
    # у колец с вырезом и без: закрывает Т-стыки у торцов выреза элевона (было 20 открытых рёбер)
    x_mid, x_end = x_le - 0.4 * chord, x_le - xi_end * chord
    x_sp = x_split if x_split is not None else 0.5 * (x_mid + x_end)
    lo = [(x_mid, Y_WB), (x_sp, Y_WB), (x_end, Y_WB)]
    pts = up + arc + lo
    smooth = set(range(1, N_UP + N_ARC - 1))
    lower = set(range(N_UP + N_ARC // 2, len(pts) - 1))
    return pts, smooth, lower

WING_TC_ROOT, WING_TC_TIP, WING_TC_FILLET = 0.07, 0.045, 0.10
WING_R_ROOT, WING_R_TIP, TIP_R_END, TIP_T_END = 0.17, 0.09, 0.07, 0.16   # R — по нагреву (Саттон–Грейвс), толщина не меньше 2R + 0,02
def wing_tc(z):
    if z <= 3.0:
        return WING_TC_FILLET + (WING_TC_ROOT - WING_TC_FILLET) * (z - 2.6) / 0.4   # зализ к борту
    return WING_TC_ROOT + (WING_TC_TIP - WING_TC_ROOT) * (z - 3.0) / 4.7
def wing_r(z):
    return WING_R_ROOT + (WING_R_TIP - WING_R_ROOT) * max(0.0, min(1.0, (z - 3.0) / 4.7))


TAN = math.tan(math.radians(55))
W = G["wings"][1]
le = lambda z: 2.0 - (z - 3.0) * TAN
def te_w(z):  # исходная задняя кромка крыла
    return (2 - 10.8) + (z - 3.0) / 4.7 * ((2 - 4.7 * TAN - 3.18) - (2 - 10.8))
EL_Z0, EL_Z1, X_HINGE, X_EL_TE = 3.4, 7.0, -7.85, -8.75
EL_GAP = 0.04   # Г: было 0,005 — носок элевона при ±25° заходил в торец крыла
def wing_ring(z, x_le, x_te_full, x_cut, t, r, t_te):
    chord = x_le - x_te_full
    xs_ = X_HINGE if EL_Z0 - 1e-6 <= z <= EL_Z1 + 1e-6 and x_le - chord < X_HINGE < x_le else None   # Г: у элевона точка разбиения низа была впереди его носка
    pts, sm, lo = profile2(x_le, chord, t, r, t_te, (x_le - x_cut) / chord, xs_)
    return [(px, py, z) for px, py in pts], sm, lo

def wing_t(z):
    return max(wing_tc(z) * (le(z) - te_w(z)), 2 * wing_r(z) + 0.02)

WING_SEC = []   # сечения для страницы
for side, sg in (("L", -1), ("R", 1)):
    st = []
    for z in [2.6, 3.0, EL_Z0]:
        st.append((z, te_w(z)))
    st.append((EL_Z0, X_HINGE))
    for z in np.linspace(EL_Z0, EL_Z1, 5)[1:]:
        st.append((z, X_HINGE))
    st.append((EL_Z1, te_w(EL_Z1)))
    st.append((7.7, te_w(7.7)))
    rings = []
    for z, xc in st:
        rg, arc_cols, lower_cols = wing_ring(z, le(z), te_w(z), xc, wing_t(z), wing_r(z), 0.06)
        rings.append(rg)
    if sg < 0:
        rings = [[(p[0], p[1], -p[2]) for p in r] for r in rings]
    gw = grp("krylo_" + side, "lak_korpus", "Несущая плоскость Λ55°: плоское днище, выпуклый верх, t/c 7→4,5 %, кромка R 0,17→0,07 м, зализ к борту (неподвижная)")
    loft(rings, lambda j, gw=gw, lc=lower_cols: g_belly if j in lc else gw, smooth_cols=arc_cols, capgrp=gw)
    # концевая часть: тот же профиль, t 0,143 → 0,10 м, R 0,07 → 0,05 м
    tip_le0 = le(7.7); tip_te0 = te_w(7.7)
    rings = []
    for s in np.linspace(0, 1, 4):
        z = 7.7 + s * 1.3; xl = tip_le0 - s * 1.3 * TAN; xt = tip_te0 + s * ((tip_le0 - 1.3 * TAN - 1.4) - tip_te0)
        t = wing_t(7.7) + s * (TIP_T_END - wing_t(7.7)); r = WING_R_TIP + s * (TIP_R_END - WING_R_TIP)
        rg, arc_cols, _ = wing_ring(z, xl, xt, xt, t, r, 0.04)
        rings.append(rg)
    if sg < 0:
        rings = [[(p[0], p[1], -p[2]) for p in r] for r in rings]
    gt = grp("konc_" + side, "lak_korpus", "Концевая часть 1,3 м (тот же профиль): 0° дозвук/посадка, 60° киль (гиперзвук, вход), 90° ангар/баллистика")
    loft(rings, lambda j, gt=gt: gt, smooth_cols=arc_cols)
    # элевон: толщина у шарнира = толщине крыла на шарнире, плоский низ
    rings = []
    for z in np.linspace(EL_Z0 + 0.01, EL_Z1 - 0.01, 4):
        ch = le(z) - te_w(z); th = thick_frac((le(z) - X_HINGE) / ch, wing_t(z), 0.06, wing_r(z)) * 0.97
        rg, arc_cols, _ = wing_ring(z, X_HINGE - EL_GAP, X_EL_TE, X_EL_TE, th, 0.45 * th, 0.02)   # Г: щель 4 см у шарнира
        rings.append(rg)
    if sg < 0:
        rings = [[(p[0], p[1], -p[2]) for p in r] for r in rings]
    ge = grp("elevon_" + side, "lak_korpus", "Элевон 0,9 × 3,6 м, ±25° (минус — задняя кромка вверх), толщина у шарнира по профилю крыла")
    loft(rings, lambda j, ge=ge: ge, smooth_cols=arc_cols)
for z in (3.0, 5.35, 7.7, 9.0):
    if z <= 7.7:
        ch = le(z) - te_w(z); t = wing_t(z); r = wing_r(z)
    else:
        ch = 1.4; t = TIP_T_END; r = TIP_R_END
    pts, _, _ = profile2(0.0, ch, t, r, 0.06 if z <= 7.7 else 0.04)
    WING_SEC.append({"z": z, "chord": round(ch, 3), "t": round(t, 3), "tc_pct": round(100 * t / ch, 2), "R_le": round(r, 3),
                     "pts": [[round(-p[0], 3), round(p[1] - Y_WB, 3)] for p in pts]})


# ----------------------------------------------------------------------------- тела вращения
def lathe(g, axis_pt, ax, profile_rz, nseg=32, emitter=None, smooth=True):
    """profile_rz: [(s, r)] вдоль оси ax от axis_pt; поверхность вращения (открытая), нормали по порядку профиля (правим вручную)."""
    ax = np.asarray(ax, float); ax /= np.linalg.norm(ax)
    u = np.cross(ax, [0, 1, 0] if abs(ax[1]) < 0.9 else [1, 0, 0]); u /= np.linalg.norm(u)
    w = np.cross(ax, u)
    P = np.asarray(axis_pt, float)
    ang = np.linspace(0, 2 * math.pi, nseg + 1)
    for k in range(len(profile_rz) - 1):
        (s0, r0), (s1, r1) = profile_rz[k], profile_rz[k + 1]
        # нормаль профиля в меридиане
        ds, dr = s1 - s0, r1 - r0
        nm = (-dr, ds)  # (по оси, по радиусу) — наружу при обходе профиля с ростом s
        for i in range(nseg):
            a0, a1 = ang[i], ang[i + 1]
            e0 = u * math.cos(a0) + w * math.sin(a0); e1 = u * math.cos(a1) + w * math.sin(a1)
            p00 = P + ax * s0 + e0 * r0; p01 = P + ax * s0 + e1 * r0
            p10 = P + ax * s1 + e0 * r1; p11 = P + ax * s1 + e1 * r1
            n0 = ax * nm[0] + e0 * nm[1]; n1 = ax * nm[0] + e1 * nm[1]
            gg = emitter(k) if emitter else g
            if smooth:
                gg.quad(p00, p01, p11, p10, n0, n1, n1, n0)
            else:
                gg.quad(p00, p01, p11, p10)


def torus(g, c, ax, R, r, nu=36, nv=10):
    ax = np.asarray(ax, float); u = np.cross(ax, [0, 1, 0] if abs(ax[1]) < 0.9 else [1, 0, 0]); u /= np.linalg.norm(u); w = np.cross(ax, u)
    c = np.asarray(c, float)
    def P(a, b):
        e = u * math.cos(a) + w * math.sin(a)
        n = e * math.cos(b) + ax * math.sin(b)
        return c + e * R + n * r, n
    for i in range(nu):
        for j in range(nv):
            a0, a1 = 2 * math.pi * i / nu, 2 * math.pi * (i + 1) / nu
            b0, b1 = 2 * math.pi * j / nv, 2 * math.pi * (j + 1) / nv
            (p00, n00), (p10, n10), (p11, n11), (p01, n01) = P(a0, b0), P(a1, b0), P(a1, b1), P(a0, b1)
            g.quad(p00, p01, p11, p10, n00, n01, n11, n10)


def fix_orient(g, start, center):
    """Для открытых оболочек: развернуть треугольники с индексом >= start так, чтобы нормаль смотрела от center (по геометрии грани)."""
    for ti in range(start, len(g.t)):
        i, j, k = g.t[ti]
        a, b, c = g.v[i], g.v[j], g.v[k]
        fn = np.cross(b - a, c - a)
        if np.dot(fn, (a + b + c) / 3 - center) < 0:
            g.t[ti] = (i, k, j)


# маршевые: срез, внутренняя чаша со свечением горла, катушка
for side, zc in (("L", -1.8), ("R", 1.8)):
    gm = grp("marsh_" + side, "metall", "Маршевый мотор 450 кН: магнитное сопло, срез заподлицо с кормой, обрамление Ø1,96 м; УВТ ±15° — поворот кольца-апертуры в гнезде")
    gi = grp("marsh_" + side + "_srez", "vnutr", "Неглубокая апертура маршевого (тёмная), вместе с marsh_" + side)
    gg = grp("marsh_" + side + "_gorlo", "svechenie", "Свечение горла маршевого, вместе с marsh_" + side)
    gk = grp("marsh_" + side + "_katushka", "katushka", "Сверхпроводящая катушка среза, вместе с marsh_" + side)
    # магнитное сопло: срез заподлицо с кормой — обрамление-кольцо, неглубокая апертура, катушка в обрамлении
    c = np.array([-9.0, 0.2, zc])
    lathe(gm, c, (-1, 0, 0), [(0.0, 0.98), (0.015, 0.95), (0.015, 0.70), (0.0, 0.68)], smooth=False)  # обтекаемое боразоновое обрамление
    s0 = len(gi.t)
    lathe(gi, c, (-1, 0, 0), [(0.0, 0.68), (-0.16, 0.68)])                     # стенка апертуры (глубина 0,16 м)
    for ti in range(s0, len(gi.t)):
        i, j, k = gi.t[ti]; gi.t[ti] = (i, k, j)
    for q in range(s0 * 3, len(gi.n)):
        gi.n[q] = -gi.n[q]
    lathe(gg, c, (-1, 0, 0), [(-0.16, 0.68), (-0.16, 0.0)], smooth=False)      # горловина — светится только при работе
    fix_orient(gg, 0, c + np.array([1.0, 0, 0]))
    torus(gk, (-8.935, 0.2, zc), (1, 0, 0), 0.83, 0.075)

# подъёмные чаши, рамы, створки
# ----------------------------------------------------------------------------- Н3: масса и ЦМ (сборка по компоновке)
def _skin_centroid():
    A = M = 0.0
    for nm in ("korpus", "dnische_tzp", "nos_poristy", "krylo_L", "krylo_R", "konc_L", "konc_R"):
        g = GROUPS[nm]
        for i, j, k in g.t:
            a = 0.5 * np.linalg.norm(np.cross(g.v[j] - g.v[i], g.v[k] - g.v[i])); A += a; M += a * (g.v[i][0] + g.v[j][0] + g.v[k][0]) / 3
    return M / A
X_SKIN = _skin_centroid()
MP = json.load(open(os.path.join(HERE, "mass_params.json"), encoding="utf-8")) if os.path.exists(os.path.join(HERE, "mass_params.json")) else {}
M_LIFT = 5.0
# ============================================================================ Ф: бюджет масс по мешу (площади × толщины × плотности)
def _area_c(names, skip_inside=False):
    A = 0.0; Mx = np.zeros(3)
    for nm in names:
        g = GROUPS[nm]
        for i, j, k in g.t:
            c = (g.v[i] + g.v[j] + g.v[k]) / 3
            if skip_inside and abs(c[2]) < hw(c[0]) - 0.01 and yb(c[0]) < c[1] < yt(c[0]):
                continue   # часть корня крыла внутри корпуса — не обшивка
            a = 0.5 * np.linalg.norm(np.cross(g.v[j] - g.v[i], g.v[k] - g.v[i])); A += a; Mx += a * c
    return A, Mx / A
MSRC = {   # удельные величины и источники (то, что не канон, помечено «допущение»)
    "rho_skin": (4800.0, "кг/м³ — допущение: 50/50 по объёму c-BN 3480 и ZrB₂ 6090 (справочные плотности)"),
    "t_skin": (0.008, "м — 6–10 мм по спецификации Н2 (materials_ru), взято 8"),
    "rho_ins": (352.0, "кг/м³ — пористая теплоизоляция: аналог LI-2200 (NASA, 22 lb/ft³); плотность пористой борной керамики не задана"),
    "t_ins": (0.040, "м — внутренний барьер 40 мм (спецификация, materials_ru)"),
    "k_frame": (0.40, "доля массы горячего каркаса корпуса от массы обшивки — ДОПУЩЕНИЕ (шпангоуты/стрингеры не моделированы)"),
    "k_tank": (0.036, "масса бака / масса рабочего тела — по внешнему баку «Шаттла» (26,5 т / ~735 т, NASA)"),
    "k_ribs": (1.15, "нервюры крыла +15 % (как в расчёте прочности Н3)"),
}
RS, TSK, RI, TI, KF = (MSRC[k][0] for k in ("rho_skin", "t_skin", "rho_ins", "t_ins", "k_frame"))
A_HULL, C_HULL = _area_c(["korpus", "dnische_tzp", "nos_poristy"])
A_WING, C_WING = _area_c(["krylo_L", "krylo_R"], skip_inside=True)
A_TIP, C_TIP = _area_c(["konc_L", "konc_R"])
A_EL, C_EL = _area_c(["elevon_L", "elevon_R"])
m_hull_skin = A_HULL * TSK * RS / 1000; m_hull_ins = A_HULL * TI * RI / 1000; m_hull_frame = KF * m_hull_skin
# крыло: обшивка обеих поверхностей по площади меша + стенки/полки кессона и труба кромки (как в расчёте прочности), +15 % нервюры
def _wing_struct_t():
    zz = np.linspace(3.0, 7.7, 30)
    hweb = [thick_frac(0.15, wing_t(z), 0.06, wing_r(z)) + thick_frac(0.65, wing_t(z), 0.06, wing_r(z)) for z in zz]
    webs = float(np.trapezoid(hweb, zz)) * 0.006 * RS
    caps = 4 * 0.0030 * 4.7 * RS
    le_tube = 2 * math.pi * 0.13 * 0.012 * 5.9 * RS
    return (webs + caps + le_tube) / 1000
m_wing = MSRC["k_ribs"][0] * (A_WING * TSK * RS / 1000 + 2 * _wing_struct_t())
m_tip = MSRC["k_ribs"][0] * A_TIP * TSK * RS / 1000
m_el = MSRC["k_ribs"][0] * A_EL * TSK * RS / 1000
# баки (Ф): носовой — плоский бак под полом салона, кормовой — 2 цилиндра (одна ёмкость, общий коллектор)
TANK_F = dict(x0=1.70, x1=4.40, z=2.0, y0=None, y1=-0.55)       # y0 — по днищу + 0,05
TANK_A = dict(x0=-7.40, x1=-4.00, zc=1.30, yc=0.65, R=0.72)
_yb_f = max(yb(TANK_F["x0"]), yb(TANK_F["x1"])) + 0.05
TANK_F["y0"] = round(_yb_f, 3)
V_TF = (TANK_F["x1"] - TANK_F["x0"]) * 2 * TANK_F["z"] * (TANK_F["y1"] - TANK_F["y0"]) * 0.97   # стенки 3 %
V_TA = 2 * math.pi * TANK_A["R"] ** 2 * (TANK_A["x0"] - TANK_A["x1"]) * -1 * 0.97
RHO_AR = 1395.0   # жидкий аргон 87 K, NIST
X_TF = 0.5 * (TANK_F["x0"] + TANK_F["x1"]); X_TA = 0.5 * (TANK_A["x0"] + TANK_A["x1"])
CAP_F, CAP_A = V_TF * RHO_AR / 1000 * 0.97, V_TA * RHO_AR / 1000 * 0.97   # 3 % свободного объёма
X_ROWC = MP.get("x_rows", -2.50)   # центр рядов чаш = ЦМ висения (уточняется ниже; argon-перекачкой ЦМ ставится сюда)
GEAR_M = (0.92, 1.88)   # GEAR.md: 2,8 т; деление по долям статической нагрузки 33/67 % — допущение
MASS_FIXED = [  # id, т, x, примечание
    ("korpus_obshivka", round(m_hull_skin, 2), C_HULL[0], "Обшивка корпуса %.1f м² × 8 мм × 4800 кг/м³ (меш)" % A_HULL),
    ("korpus_bariera", round(m_hull_ins, 2), C_HULL[0], "Внутренний барьер 40 мм × 352 кг/м³ по всей площади корпуса"),
    ("korpus_karkas", round(m_hull_frame, 2), C_HULL[0], "Горячий каркас = 0,40 × обшивки (ДОПУЩЕНИЕ)"),
    ("nos_kromki", 0.7, 9.0, "Монолит носа и утолщения — из спецификации Н2, по мешу не пересчитано"),
    ("krylya", round(m_wing, 2), C_WING[0], "2 крыла: обшивка %.1f м² (обе поверхности, меш) × 8 мм + кессон + труба кромки, +15 %%" % A_WING),
    ("koncevye", round(m_tip, 2), C_TIP[0], "Концевые части %.1f м² × 8 мм, +15 %%" % A_TIP),
    ("elevony", 0.85, -8.22, "Элевоны: 0,85 т — значение до доводки Г (массы не меняются без решения пользователя); по исправленному мешу %.1f м² × 8 мм, +15 %% = %.2f т" % (A_EL, m_el)),
    ("schitok", 0.35, -7.75, "Щиток днища с приводом (Н3, не пересчитано)"),
    ("marsh", 3.6, -8.4, "2 маршевых (спецификация)"),
    ("chashi", 5.0, X_ROWC, "Узел 6 подъёмных чаш, рамы, створки (спецификация)"),
    ("energetika", 4.0, -3.0, "Генераторы, катушки, криокулеры — Ф: под грузовым отсеком x −2,0…−3,9 (место бывших баков)"),
    ("zaryady", 2.0, -3.0, "Ионные заряды — Ф: рядом с энергетикой"),
    ("tenevoy_ekran", 2.5, 0.5, "Теневой экран под салоном (спецификация)"),
    ("kabina", 2.1, 2.6, "Кабина, салон, кресла, системы (спецификация)"),
    ("shassi_PO", GEAR_M[0], 6.75, "ПО (GEAR.md, 2,8 т всего)"),
    ("shassi_osn", GEAR_M[1], -3.4, "Основные опоры"),
    ("baki", None, None, "Баки 3,6 % массы аргона (по ВБ «Шаттла»), магистрали и насосы перекачки 0,15 т (допущение)"),
]
M_PAY = 10.0; X_PAY = -1.5   # Ф: грузовой отсек x +1,0…−3,95 (был +1,5…−3,5)
DV_ASC, VE_ASC = 9.3, 40.0      # спецификация: Δv выхода на орбиту, скорость истечения
AR_ENTRY = 6.3                  # аргон на входе (спецификация; включает 0,5 т резерва вдува)
def mass_close():
    """Замыкание: m0 = сухая + ПН + аргон; аргон = m0·(1 − e^(−Δv/ve)) + 6,3; баки = 0,036 × аргон + 0,15."""
    ar = 18.3
    for _ in range(50):
        mt = 0.036 * ar + 0.15
        dry = sum(i[1] for i in MASS_FIXED if i[1] is not None) + mt
        m0 = dry + M_PAY + ar
        ar = m0 * (1 - math.exp(-DV_ASC / VE_ASC)) + AR_ENTRY
    return dry, ar, mt, m0
DRY, AR_TOT, M_TANKS, M0 = mass_close()
X_TANKS_STRUCT = (CAP_F * X_TF + CAP_A * X_TA) / (CAP_F + CAP_A)
MASS_FIXED[-1] = ("baki", round(M_TANKS, 2), X_TANKS_STRUCT, MASS_FIXED[-1][3])
M_FIX = sum(i[1] for i in MASS_FIXED) + M_PAY          # без аргона, с ПН
MX_FIX = sum(i[1] * i[2] for i in MASS_FIXED) + M_PAY * X_PAY
def cg_ar(mf, ma):
    """ЦМ при mf т в носовом и ma т в кормовом баке."""
    m = M_FIX + mf + ma
    return (MX_FIX + mf * X_TF + ma * X_TA) / m, m
X0_FIX = MX_FIX / M_FIX
ARGON = {"start": AR_TOT, "entry": AR_ENTRY, "landing": 0.5}
X_CG_E = cg_ar(0, AR_ENTRY)[0]          # по умолчанию (без питания) аргон в кормовом баке
X_LIFT = X_ROWC
X_TANK = X_TA
CG = {k: cg_ar(0, v) for k, v in ARGON.items()}
ROW_DX = 3.0
X_ROW_F, X_ROW_R = round(X_ROWC + ROW_DX, 2), round(X_ROWC - ROW_DX, 2)
print("Ф массы: сухая %.2f, аргон %.2f, m0 %.2f, X0 %.3f, баки F %.2f т @%.2f, A %.2f т @%.2f" % (DRY, AR_TOT, M0, X0_FIX, CAP_F, X_TF, CAP_A, X_TA))
print("ЦМ:", {k: (round(v[0], 3), round(v[1], 2)) for k, v in CG.items()})

for row, xc, yc in (("P", X_ROW_F, yb(X_ROW_F) - 0.13), ("Z", X_ROW_R, yb(X_ROW_R) - 0.13)):
    gr = grp("ryad_" + row, "metall", "Ряд %s: рама + 3 плоские кольца-апертуры Ø0,84×0,34 м, поворот 180° в нишу" % ("передний" if row == "P" else "задний"))
    gri = grp("ryad_" + row + "_chashi", "vnutr", "Внутренность чаш ряда, вместе с ryad_" + row)
    grg = grp("ryad_" + row + "_svet", "svechenie", "Свечение дна чаш ряда, вместе с ryad_" + row)
    ytop = yc + 0.17
    for zc in (-1.5, 0.0, 1.5):
        c = np.array([xc, ytop, zc])
        lathe(gr, c, (0, -1, 0), [(0.0, 0.42), (0.34, 0.42)])                 # кольцо катушки чаши Ø0,84 × 0,34
        lathe(gr, c, (0, -1, 0), [(0.34, 0.42), (0.34, 0.33)], smooth=False)  # плоский срез-кольцо
        s0 = len(gri.t)
        lathe(gri, c, (0, -1, 0), [(0.34, 0.33), (0.10, 0.33)])               # неглубокая апертура
        for ti in range(s0, len(gri.t)):
            i, j, k = gri.t[ti]; gri.t[ti] = (i, k, j)
        for q in range(s0 * 3, len(gri.n)):
            gri.n[q] = -gri.n[q]
        lathe(grg, c, (0, -1, 0), [(0.10, 0.33), (0.10, 0.0)], smooth=False)
        lathe(gr, c, (0, -1, 0), [(0.0, 0.34), (0.0, 0.0)], smooth=False)     # верхнее донце
        fix_orient(gr, len(gr.t) - 64, c + np.array([0, -0.2, 0]))
        fix_orient(grg, 0, c + np.array([0, 0.5, 0]))
    # рама-рычаг
    def box(g, c, d):
        c = np.asarray(c, float); h = np.asarray(d, float) / 2
        P = [c + h * np.array(s) for s in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
        for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(2,3,7,6),(1,2,6,5),(0,4,7,3)]:
            g.quad(*[P[i] for i in f])
    box(gr, (xc, ytop + 0.03, 0), (0.12, 0.06, 3.9))

# створки рядов, створки шасси, люк — конформные пластины по днищу/борту
def belly_plate(g, x0, x1, z0, z1, nx=6, th=0.035):
    xsamp = np.linspace(x0, x1, nx)
    bot = [[(x, yb(x) - 0.008, z) for z in (z0, z1)] for x in xsamp]
    rings = [[(x, yb(x) - 0.008, z0), (x, yb(x) - 0.008, z1), (x, yb(x) - 0.008 + th, z1), (x, yb(x) - 0.008 + th, z0)] for x in xsamp]
    loft(rings, lambda j: g)

g = grp("stvorka_P", "bor_dnische", "Створка переднего ряда чаш, петля по задней кромке, -95° висение"); belly_plate(g, X_ROW_F - 0.6, X_ROW_F + 0.6, -2.0, 2.0)
g = grp("stvorka_Z", "bor_dnische", "Створка заднего ряда чаш, петля по задней кромке, -95° висение"); belly_plate(g, X_ROW_R - 0.6, X_ROW_R + 0.6, -2.0, 2.0)
# Н3: щиток днища в корме — одна панель, петля по передней кромке, задняя кромка = нижняя кромка кормы; в укладке повторяет днище
FLAP_C, FLAP_W = 2.5, 5.0
FLAP_X1 = -8.74   # Г: было −8,98 (2,3 см до среза маршевых); хорда 2,5 м сохранена — щиток сдвинут вперёд на 0,24 м, зазор до катушек ≥ 5 см при УВТ ±15°
FLAP_X0 = FLAP_X1 + FLAP_C
FLAP_PIVOT = [FLAP_X0, yb(FLAP_X0) - 0.008, 0.0]
g = grp("schitok", "bor_dnische", "Щиток днища 2,5 × 5,0 м (борная керамика/боразон): 0° укладка, вниз до +35° — балансировка планирующего входа")
belly_plate(g, FLAP_X0, FLAP_X1, -FLAP_W / 2, FLAP_W / 2, nx=8, th=0.06)
# ----------------------------------------------------------------------------- шасси (GEAR.md): тяжёлое трёхопорное
# ПО x 6,8 (2 колеса), основные x −3,4, z ±2,6 (тележка 2 оси × 2 колеса). Колёса одинаковые Ø0,86 × 0,26 (лепестковые, МПУ).
# Уборка ВПЕРЁД +90° вокруг z: свободный выпуск весом, пружиной и набегающим потоком. Ниши — в корпусе (не в крыле).
def ybz(x, z):
    """Нижняя поверхность корпуса в точке (x, z): днище или нижняя фаска."""
    h, b, c = hw(x), yb(x), chf(x)
    lo = min(c / 3.0, max(0.0, (yt(x) - c) - b))
    za = abs(z)
    return b if za <= h - c else b + lo * (za - (h - c)) / c

def belly_plate_z(g, x0, x1, z0, z1, nx=8, nz=5, th=0.04):
    """Конформная створка: повторяет днище и фаску (заподлицо)."""
    xsmp = np.linspace(x0, x1, nx); zsmp = np.linspace(z0, z1, nz)
    rings = []
    for x in xsmp:
        lo_ = [(x, ybz(x, z) - 0.008, z) for z in zsmp]
        hi_ = [(x, ybz(x, z) - 0.008 + th, z) for z in zsmp[::-1]]
        rings.append(lo_ + hi_)
    loft(rings, lambda j: g)

GEAR = {  # pivot (шарнир уборки), L до оси тележки, тележка: оси по x, колёса по z
    # Т1Б-А: ПО — колёса Ø0,81 × 0,22 в прежней нише; основные — ось −3,60, стойка +0,26 м, тандем Ø1,14 × 0,41, шаг осей 1,24 м,
    # шарнир основных поднят на 0,3 м (y 0,4) при стойке длиннее на 0,3 м: в убранном виде нижнее колесо тандема выше нижней фаски
    # борта (иначе выходило на 0,17 м), грунт тот же; грунт основных y −2,75 (на 0,40 м ниже ПО — стоянка с наклоном носа вниз ~2,3°, угол касания кормой 21,7°)
    "N": {"piv": (6.75, -0.18, 0.0), "L": 1.74, "axles": (0.0,), "wz": (-0.17, 0.17), "wr": 0.405, "ww": 0.22},
    "L": {"piv": (-3.6, 0.4, -2.6), "L": 2.58, "axles": (-0.62, 0.62), "wz": (-0.25, 0.25), "wr": 0.57, "ww": 0.41},
    "R": {"piv": (-3.6, 0.4, 2.6), "L": 2.58, "axles": (-0.62, 0.62), "wz": (-0.25, 0.25), "wr": 0.57, "ww": 0.41},
}
WR, WW = 0.43, 0.26
DOORS = {"N": (6.55, 9.0 - 0.07, -0.5, 0.5), "L": (-3.75, 0.15, -3.15, -2.05), "R": (-3.75, 0.15, 2.05, 3.15)}

def closed_part(g, fn, center):
    s = len(g.t); fn(); fix_orient(g, s, np.asarray(center, float))

def cyl(g, a, b, r, nseg=16):
    a = np.asarray(a, float); b = np.asarray(b, float); ax = b - a; L = np.linalg.norm(ax)
    closed_part(g, lambda: lathe(g, a, ax / L, [(0.0, 0.0), (0.0, r), (L, r), (L, 0.0)], nseg=nseg, smooth=False), (a + b) / 2)

def wheel(g, c, WR=WR, WW=WW):
    c = np.asarray(c, float)
    prof = [(-WW / 2, 0.0), (-WW / 2, 0.16), (-WW / 2 + 0.02, 0.18), (-WW / 2 + 0.02, WR - 0.03), (-WW / 2 + 0.05, WR),
            (WW / 2 - 0.05, WR), (WW / 2 - 0.02, WR - 0.03), (WW / 2 - 0.02, 0.18), (WW / 2, 0.16), (WW / 2, 0.0)]
    closed_part(g, lambda: lathe(g, c, (0, 0, 1), prof, nseg=28, smooth=False), c)

for s, d in GEAR.items():
    nm = "shassi_" + s
    g = grp(nm, "metall", ("Передняя опора: стойка Ø0,30, ход 0,85 м, 2 колеса Ø0,81 × 0,22" if s == "N" else
                           "Основная опора %s: стойка Ø0,34, ход 0,85 м, тележка 2 оси × 2 колеса Ø1,14 × 0,41 (шаг осей 1,24 м), тормоза C/SiC" % s) +
            "; уборка вперёд 90°")
    gw_ = grp(nm + "_kolesa", "vnutr", "Колёса опоры %s (тёмные), вместе с %s" % (s, nm))
    p = np.array(d["piv"]); L = d["L"]
    axle_c = p + np.array([0, -L, 0])
    cyl(g, p + np.array([0, 0.12, 0]), p + np.array([0, -(L - 0.85), 0]), 0.17 if s != "N" else 0.15)   # цилиндр амортизатора
    cyl(g, p + np.array([0, -(L - 0.85), 0]), axle_c + np.array([0, 0.05, 0]), 0.12 if s != "N" else 0.10)  # шток
    cyl(g, p + np.array([0, 0, -0.32]), p + np.array([0, 0, 0.32]), 0.09)                           # ось шарнира уборки
    if len(d["axles"]) > 1:
        cyl(g, axle_c + np.array([d["axles"][0] - 0.12, 0, 0]), axle_c + np.array([d["axles"][-1] + 0.12, 0, 0]), 0.09)  # балка тележки
    for ax_ in d["axles"]:
        c0 = axle_c + np.array([ax_, 0, 0])
        cyl(g, c0 + np.array([0, 0, d["wz"][0] - d["ww"] / 2 + 0.03]), c0 + np.array([0, 0, d["wz"][1] + d["ww"] / 2 - 0.03]), 0.07)   # ось колёс
        for wz in d["wz"]:
            wheel(gw_, c0 + np.array([0, 0, wz]), d["wr"], d["ww"])
    x0, x1, z0, z1 = DOORS[s]
    gd = grp("stvorka_shassi_" + s, "bor_dnische", "Створка-полоз ниши %s: бериллиевая бронза + боразон, заподлицо; закрытая — аварийный полоз" % ("ПО" if s == "N" else s))
    belly_plate_z(gd, x0, x1, z0, z1)


# восьмигранный люк — левый борт
gh = grp("lyuk", "lak_korpus", "Восьмигранный люк, левый борт, петля по передней кромке, открытие 100°")
gh_rim = grp("lyuk_shov", "katushka", "Шов-кант люка (тонкий, функциональный указатель контура)")
HX, HY, HR = 1.0, 0.3, 0.68
oct_pts = [(HX + HR * math.cos(math.radians(22.5 + 45 * k)), HY + HR * math.sin(math.radians(22.5 + 45 * k))) for k in range(8)]
def side_pt(x, y, d):
    return np.array([x, y, -(hw(x) + d)])
cen_o, cen_i = side_pt(HX, HY, 0.012), side_pt(HX, HY, -0.005)
for k in range(8):
    (x0, y0), (x1, y1) = oct_pts[k], oct_pts[(k + 1) % 8]
    gh.tri(cen_o, side_pt(x1, y1, 0.012), side_pt(x0, y0, 0.012))
    gh_rim.quad(side_pt(x0, y0, 0.012), side_pt(x1, y1, 0.012), side_pt(x1, y1, -0.005), side_pt(x0, y0, -0.005))
fix_orient(gh, 0, cen_o + np.array([0, 0, 1.0]))
fix_orient(gh_rim, 0, side_pt(HX, HY, 0.0))

# фотонная полоса датчиков — по верху носовой части
gp = grp("fotonnaya_polosa", "svechenie_dat", "Фотонная полоса датчиков (вместо окон), свечение")
xs_p = np.linspace(7.0, X_NOSE, 16)
rings = [[(x, yt(x) + 0.004, -0.125), (x, yt(x) + 0.004, 0.125), (x, yt(x) + 0.018, 0.10), (x, yt(x) + 0.018, -0.10)] for x in xs_p]
loft(rings, lambda j: gp)

# двигатели ориентации (аргон), сопла заподлицо: восьмигранные выходы 0,12 м
RCS = []
def rcs_pad(name, x, y, z, nrm, thrust_dir):
    nrm = np.asarray(nrm, float); nrm /= np.linalg.norm(nrm)
    u = np.cross(nrm, [1, 0, 0] if abs(nrm[0]) < 0.9 else [0, 1, 0]); u /= np.linalg.norm(u); w = np.cross(nrm, u)
    c = np.array([x, y, z]) + nrm * 0.006
    g = GROUPS["rcs"]
    for k in range(8):
        a0, a1 = math.radians(22.5 + 45 * k), math.radians(22.5 + 45 * (k + 1))
        g.tri(c, c + 0.06 * (u * math.cos(a0) + w * math.sin(a0)), c + 0.06 * (u * math.cos(a1) + w * math.sin(a1)))
    fix_orient(g, len(g.t) - 8, c - nrm)
    RCS.append({"id": name, "pos": [round(x, 3), round(y, 3), round(z, 3)], "exhaust_dir": [round(v, 3) for v in thrust_dir]})
grp("rcs", "vnutr", "16 сопел ориентации (аргон), заподлицо")
s45 = math.sqrt(0.5)
for zz, sd in ((-0.35, "L"), (0.35, "R")):
    rcs_pad("N_verh_" + sd, 9.0, yt(9.0), zz, (0, 1, 0), (0, 1, 0))
    rcs_pad("N_niz_" + sd, 9.0, yb(9.0), zz, (0, -1, 0), (0, -1, 0))
    zs = 2.6 if sd == "R" else -2.6
    rcs_pad("K_verh_" + sd, -8.3, yt(-8.3), zs, (0, 1, 0), (0, 1, 0))
    rcs_pad("K_niz_" + sd, -8.3, yb(-8.3), zs, (0, -1, 0), (0, -1, 0))
for sd, sgn in (("L", -1), ("R", 1)):
    rcs_pad("N_bort_" + sd, 9.0, -0.15, sgn * hw(9.0), (0, 0, sgn), (0, 0, sgn))
    rcs_pad("K_bort_" + sd, -8.3, 0.3, sgn * hw(-8.3), (0, 0, sgn), (0, 0, sgn))
    rcs_pad("A_korma_" + sd, -9.0, 1.05, sgn * 0.5, (-1, 0, 0), (-1, 0, 0))
    rcs_pad("A_nos_" + sd, 9.6, -0.1, sgn * hw(9.6), (0, 0, sgn), (s45, 0, sgn * s45))   # скошенный канал: выход вперёд-вбок

# нормали шасси — по геометрии (оболочки уже ориентированы closed_part)
for name in [n for n in GROUPS if n.startswith("shassi_")]:
    g = GROUPS[name]
    for ti, (i, j, k) in enumerate(g.t):
        fn = np.cross(g.v[j] - g.v[i], g.v[k] - g.v[i]); fn /= np.linalg.norm(fn) + 1e-12
        if np.dot(fn, g.n[i]) < 0:
            g.n[i] = -g.n[i]; g.n[j] = -g.n[j]; g.n[k] = -g.n[k]
# то же для обечаек моторов и чаш (нормали lathe могли смотреть внутрь)
for name in list(GROUPS):
    g = GROUPS[name]
    if name.startswith(("marsh_", "ryad_")) and not name.endswith(("_srez", "_chashi")):
        for ti, (i, j, k) in enumerate(g.t):
            fn = np.cross(g.v[j] - g.v[i], g.v[k] - g.v[i]); fn /= np.linalg.norm(fn) + 1e-12
            for q in (i, j, k):
                if np.dot(fn, g.n[q]) < 0:
                    g.n[q] = -g.n[q]


# ----------------------------------------------------------------------------- Г: гнёзда маршевых в торце кормы
# Ось УВТ — в плоскости среза на уровне нижней кромки кормы; при ±15° верх кольца уходит вперёд до 0,45 м.
# Гнездо: цилиндр R 1,03 (кольцо Ø1,96 + зазор 5 см) глубиной 0,62 м, снизу ограничено плоскостью нижней кромки кормы.
# Массы считаются по исходному торцу (выше) — здесь меняется только сетка торца.
from shapely.geometry import Polygon as _Poly, Point as _Pt, box as _box2
from shapely import constrained_delaunay_triangles as _cdt
SOCK_R, SOCK_D, SOCK_YC = 1.03, 0.62, 0.2
SOCK_YCUT = yb(-9.0)
_X0 = hull_x[0]
_ti = [k for k, (i, j, kk) in enumerate(g_hull.t) if max(abs(g_hull.v[q][0] - _X0) for q in (i, j, kk)) < 1e-9]
_keep = [t for k, t in enumerate(g_hull.t) if k not in set(_ti)]
g_hull.t = _keep
_sec = _Poly([(p[2], p[1]) for p in section(_X0)])
_disk = lambda zc: _Pt(zc, SOCK_YC).buffer(SOCK_R, 64).intersection(_box2(-10, SOCK_YCUT, 10, 10))
_cap = _sec.difference(_disk(-1.8)).difference(_disk(1.8))
for tr in _cdt(_cap).geoms:
    c = list(tr.exterior.coords)[:3]
    P = [np.array([_X0, y, z]) for z, y in c]
    if np.cross(P[1] - P[0], P[2] - P[0])[0] > 0:
        P = [P[0], P[2], P[1]]
    g_hull.tri(*P)
SOCKETS = []
for zc in (-1.8, 1.8):
    dk = _disk(zc); ring = list(dk.exterior.coords)[:-1]
    xb = _X0 + SOCK_D
    for k in range(len(ring)):
        (z0, y0), (z1, y1) = ring[k], ring[(k + 1) % len(ring)]
        a, b = np.array([_X0, y0, z0]), np.array([_X0, y1, z1]); c_, d_ = b + [SOCK_D, 0, 0], a + [SOCK_D, 0, 0]
        mid = (a + c_) / 2; inward = np.array([0, SOCK_YC - mid[1], zc - mid[2]]) if abs(y0 - SOCK_YCUT) + abs(y1 - SOCK_YCUT) > 1e-6 else np.array([0, 1.0, 0])
        for T in ((a, b, c_), (a, c_, d_)):
            n = np.cross(T[1] - T[0], T[2] - T[0])
            g_hull.tri(*(T if np.dot(n, inward) > 0 else (T[0], T[2], T[1])))
    cen = np.array([xb, *dk.centroid.coords[0][::-1]])
    for k in range(len(ring)):
        (z0, y0), (z1, y1) = ring[k], ring[(k + 1) % len(ring)]
        T = (cen, np.array([xb, y0, z0]), np.array([xb, y1, z1]))
        n = np.cross(T[1] - T[0], T[2] - T[0])
        g_hull.tri(*(T if n[0] < 0 else (T[0], T[2], T[1])))
    SOCKETS.append({"zc": zc, "R": SOCK_R, "depth": SOCK_D, "y_cut": round(SOCK_YCUT, 3)})

# ----------------------------------------------------------------------------- Ф: баки аргона (внутренние группы, видны в разрезе)
# Т1Б-А: баки в меш не выводятся (в Orbiter не видны); ёмкости и ЦМ считаются по TANK_F / TANK_A выше
def _box(g, lo, hi):
    lo = np.asarray(lo, float); hi = np.asarray(hi, float); c = (lo + hi) / 2; h = (hi - lo) / 2
    P = [c + h * np.array(q) for q in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
    s0 = len(g.t)
    for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(2,3,7,6),(1,2,6,5),(0,4,7,3)]:
        g.quad(*[P[i] for i in f])
    fix_orient(g, s0, c)

# ----------------------------------------------------------------------------- анимации (ось, шарнир, режимы)
MOV = {m["group"]: m for m in G["movers"]}
# Н3: ряды чаш и створки — по ЦМ входа
MOV["row_F"]["pivot"] = [X_ROW_F, round(yb(X_ROW_F) + 0.11, 3), 0]; MOV["row_R"]["pivot"] = [X_ROW_R, round(yb(X_ROW_R) + 0.11, 3), 0]   # Г: ось ряда на 0,11 м выше днища — после поворота 180° ряд целиком над закрытой створкой
MOV["panel_F"]["pivot"] = [round(X_ROW_F - 0.6, 3), round(yb(X_ROW_F - 0.6) - 0.008, 3), 0]; MOV["panel_R"]["pivot"] = [round(X_ROW_R - 0.6, 3), round(yb(X_ROW_R - 0.6) - 0.008, 3), 0]   # Г: петля на наружной поверхности створки
FLAP_MODES = MP.get("flap_modes", {"hover": 0, "transition": 0, "cruise": 0, "entry": 20, "ballistic": 0, "stowed": 0})
# Т1Б-А: «glide» — дозвук, заход и посадка на полосу: концевые части 0°, щиток 10° (при касании ≤ 10°), шасси выпущено,
# чаши убраны, УВТ 0. Ряды/створки чаш, элевоны — как в «cruise» (0 / 180°).
GLIDE = {"tip_L": 0, "tip_R": 0, "elevon_L": 0, "elevon_R": 0, "main_L": 0, "main_R": 0, "row_F": 180, "row_R": 180, "panel_F": 0, "panel_R": 0}
FLAP_MODES = dict(FLAP_MODES, glide=10, entry=15)   # Т1Б-А balance_A: планирующий вход — щиток 15°, элевоны 0 (α 42,3°)
for _s in ("elevon_L", "elevon_R"):
    MOV[_s]["entry_deg"] = 0
ANIM = []
def anim(groups, pivot, axis, modes, lim, note):
    ANIM.append({"groups": groups, "pivot": pivot, "axis": axis, "modes": modes, "limits": lim, "note": note})
MODES = ["hover", "transition", "cruise", "entry", "ballistic", "glide", "stowed"]
def from_spec(sg, groups, note):
    m = MOV[sg]
    modes = {k: m.get(k + "_deg", m.get("entry_deg", 0) if k == "ballistic" else 0) for k in MODES}
    if sg in GLIDE:
        modes["glide"] = GLIDE[sg]
    anim(groups, m["pivot"], m["axis"], modes, [m.get("min_deg", min(modes.values())), m.get("max_deg", max(modes.values()))], note)
from_spec("tip_L", ["konc_L"], "концевая часть Л")
from_spec("tip_R", ["konc_R"], "концевая часть П")
def _el_c(z):   # центр скругления носка элевона в сечении z
    ch = le(z) - te_w(z); r = 0.45 * thick_frac((le(z) - X_HINGE) / ch, wing_t(z), 0.06, wing_r(z)) * 0.97
    return np.array([X_HINGE - EL_GAP - r, Y_WB + r, z])
for s, sg in (("L", -1), ("R", 1)):   # Г: ось элевона — через центры скругления носка у обоих торцов (носок вращается на месте)
    _c0, _c1 = _el_c(EL_Z0 + 0.01), _el_c(EL_Z1 - 0.01)
    _c0[2] *= sg; _c1[2] *= sg
    _a = (_c1 - _c0) * (1 if sg > 0 else -1); _a = _a / np.linalg.norm(_a)
    MOV["elevon_" + s]["pivot"] = [round(float(v), 4) for v in _c0]; MOV["elevon_" + s]["axis"] = [round(float(v), 5) for v in _a]
from_spec("elevon_L", ["elevon_L"], "элевон Л")
from_spec("elevon_R", ["elevon_R"], "элевон П")
for s in "LR":
    MOV["main_" + s]["pivot"] = [-9.0, round(yb(-9.0), 3), MOV["main_" + s]["pivot"][2]]   # Г: ось УВТ в плоскости среза на уровне нижней кромки кормы
    from_spec("main_" + s, ["marsh_" + s, "marsh_%s_srez" % s, "marsh_%s_gorlo" % s, "marsh_%s_katushka" % s], "УВТ маршевого " + s)
for r, sg in (("P", "row_F"), ("Z", "row_R")):
    from_spec(sg, ["ryad_" + r, "ryad_%s_chashi" % r, "ryad_%s_svet" % r], "ряд чаш " + r)
for r, sg in (("P", "panel_F"), ("Z", "panel_R")):
    from_spec(sg, ["stvorka_" + r], "створка ряда " + r)
anim(["schitok"], [round(v, 3) for v in FLAP_PIVOT], [0, 0, 1], FLAP_MODES, [0, 35], "щиток днища, вниз (плюс — задняя кромка вниз)")
# шасси: выпущено в висении (посадка), убрано в полёте; уборка назад (−90° вокруг z) — колёса помещаются в нишах днища
# шасси: выпущено в висении/посадке, убрано в полёте и в ангаре; уборка ВПЕРЁД (+90° вокруг z) — выпуск весом и потоком
gear_modes = {"hover": 0, "transition": 90, "cruise": 90, "entry": 90, "ballistic": 90, "glide": 0, "stowed": 90}
for s in "NLR":
    anim(["shassi_" + s, "shassi_%s_kolesa" % s], list(GEAR[s]["piv"]), [0, 0, 1], gear_modes, [0, 90],
         ("ПО" if s == "N" else "основная " + s) + ", уборка вперёд")
door_open = {"hover": 1, "transition": 0, "cruise": 0, "entry": 0, "ballistic": 0, "glide": 1, "stowed": 0}
for s, hz, sgn in (("N", -0.5, 1), ("L", -3.15, 1), ("R", 3.15, -1)):   # петля по внешней кромке, створка висит вниз
    x0, x1 = DOORS[s][:2]
    _ax = np.array([x1 - x0, ybz(x1, hz) - ybz(x0, hz), 0.0]); _ax /= np.linalg.norm(_ax)   # Г: петля вдоль днища
    anim(["stvorka_shassi_" + s], [round((x0 + x1) / 2, 3), round(ybz((x0 + x1) / 2, hz) - 0.008, 3), hz], [round(float(v), 4) for v in _ax],
         {k: sgn * 90 * v for k, v in door_open.items()}, sorted([0, sgn * 90]), "створка-полоз " + ("ПО" if s == "N" else s))
anim(["lyuk", "lyuk_shov"], [HX + HR, HY, -hw(HX + HR)], [0, 1, 0], {k: (100 if k == "stowed" else 0) for k in MODES}, [0, 100], "люк, открыт в ангаре")

# ----------------------------------------------------------------------------- проверки сетки
def edge_check(names):
    """Число открытых рёбер по позициям вершин (водонепроницаемость)."""
    from collections import Counter
    E = Counter()
    key = lambda p: tuple(np.round(p, 4))
    for nm in names:
        g = GROUPS[nm]
        for i, j, k in g.t:
            a, b, c = key(g.v[i]), key(g.v[j]), key(g.v[k])
            for e in ((a, b), (b, c), (c, a)):
                E[e] += 1
    open_e = sum(1 for (a, b), n in E.items() if E.get((b, a), 0) != n)
    return open_e, len(E)

checks = {
    "korpus+nos+dnische+krylya": edge_check(["korpus", "dnische_tzp", "nos_poristy", "krylo_L", "krylo_R"]), "konc_L": edge_check(["konc_L"]), "elevon_L": edge_check(["elevon_L"]),
}
vol_hull = sum(GROUPS[n].volume() for n in ("korpus", "dnische_tzp", "nos_poristy"))

# ----------------------------------------------------------------------------- вывод .msh (Orbiter) и json
ORDER = list(GROUPS)
def to_orb(p):
    return (p[2], p[1], p[0])

with open(os.path.join(OUT, "Lander.msh"), "w", newline="\r\n", encoding="ascii") as f:
    f.write("MSHX1\nGROUPS %d\n" % len(ORDER))
    for nm in ORDER:
        g = GROUPS[nm]
        tx = MAT_TEX.get(MATERIALS[g.mat - 1][0], 0)
        f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d ;%s\n" % (nm, g.mat, tx, len(g.v), len(g.t), nm))
        UVd = {}
        for i, j, k in g.t:
            for q, uv in zip((i, j, k), uv_tri(g.v[i], g.v[j], g.v[k])):
                UVd[q] = uv
        g.uv = [UVd.get(q, (0.0, 0.0)) for q in range(len(g.v))]
        for p, n, uv in zip(g.v, g.n, g.uv):
            P, N = to_orb(p), to_orb(n)
            f.write("%.3f %.3f %.3f %.4f %.4f %.4f %.4f %.4f\n" % (*P, *N, *uv))
        for t in g.t:
            f.write("%d %d %d\n" % t)
    f.write("MATERIALS %d\n" % len(MATERIALS))
    for m in MATERIALS:
        f.write(m[0] + "\n")
    for nm, d, s, e in MATERIALS:
        if nm == "svechenie":
            e = (0, 0, 0)   # Г: свечение срезов/чаш — только при работе: в меше погашено, включает модуль (emissive по режиму)
        f.write("MATERIAL %s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1 %.0f\n%.3f %.3f %.3f 1\n" % (nm, *d, *d, *s, *e))
    f.write("TEXTURES %d\n" % len(TEXTURES))
    for t in TEXTURES:
        f.write(TEX_DIR_ORB + t + ".dds\n")

NTRI = sum(len(g.t) for g in GROUPS.values())

# ----------------------------------------------------------------------------- аэродинамика по мешу
S_REF, L_REF = 168.0, 20.0
AERO_GROUPS = ["korpus", "dnische_tzp", "nos_poristy", "krylo_L", "krylo_R", "konc_L", "konc_R", "elevon_L", "elevon_R", "schitok"]

def rotmat(axis, deg):
    a = np.asarray(axis, float); a /= np.linalg.norm(a); t = math.radians(deg)
    K = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + math.sin(t) * K + (1 - math.cos(t)) * K @ K

def inside_hull(p):
    x, y, z = p
    if x < -9 or x > X_TIP:
        return False
    return abs(z) < hw(x) - 0.01 and yb(x) + 0.01 < y < yt(x) - 0.01

FACES = {}
for nm in AERO_GROUPS:
    g = GROUPS[nm]
    C, NA = [], []
    for i, j, k in g.t:
        a, b, c = g.v[i], g.v[j], g.v[k]
        cen = (a + b + c) / 3
        if nm.startswith("krylo") and inside_hull(cen):
            continue
        C.append(cen); NA.append(0.5 * np.cross(b - a, c - a))
    FACES[nm] = (np.array(C), np.array(NA))

def config_faces(tip_deg, elev_deg, flap_deg=0):
    C, NA = [], []
    for nm in AERO_GROUPS:
        c, na = FACES[nm]
        if nm == "schitok":
            if flap_deg <= 0:
                continue                      # в укладке — это днище
            keep = na[:, 1] < -0.5 * np.linalg.norm(na, axis=1)   # только нижняя (наветренная) поверхность
            c, na = c[keep], na[keep]
            Rm = rotmat([0, 0, 1], flap_deg); P = np.array(FLAP_PIVOT)
            c = (c - P) @ Rm.T + P; na = na @ Rm.T
        if nm == "dnische_tzp" and flap_deg > 0:      # днище в нише под щитком затенено
            keep = ~((c[:, 0] < FLAP_X0) & (np.abs(c[:, 2]) < FLAP_W / 2) & (na[:, 1] < 0))
            c, na = c[keep], na[keep]
        if nm.startswith("konc_"):
            s = nm[-1]; m = MOV["tip_" + s]
            Rm = rotmat(m["axis"], tip_deg); P = np.array(m["pivot"])
            c = (c - P) @ Rm.T + P; na = na @ Rm.T
        if nm.startswith("elevon_"):
            m = MOV["elevon_" + nm[-1]]
            Rm = rotmat(m["axis"], elev_deg); P = np.array(m["pivot"])
            c = (c - P) @ Rm.T + P; na = na @ Rm.T
        C.append(c); NA.append(na)
    return np.vstack(C), np.vstack(NA)

CPMAX = 1.84   # γ = 1,4, за прямым скачком (M ≫ 1)
CF = 0.0015    # турбулентное трение на гиперзвуке, отнесено к смоченной площади

def newton(alpha_deg, tip_deg=60, elev_deg=0, cg=None, flap_deg=0):
    cg = (X_CG_E, 0, 0) if cg is None else cg
    C, NA = config_faces(tip_deg, elev_deg, flap_deg)
    a = math.radians(alpha_deg)
    V = np.array([-math.cos(a), math.sin(a), 0.0])      # поток в осях аппарата
    A = np.linalg.norm(NA, axis=1); n = NA / A[:, None]
    s = -(n @ V)                                          # sin θ
    wet = s > 0
    Cp = np.where(wet, CPMAX * s ** 2, 0.0)
    Fv = -(Cp * A)[:, None] * n                           # сила / q
    Fv_f = CF * A.sum() * V                               # трение — вдоль потока
    Fsum = Fv.sum(axis=0) + Fv_f
    r = C - np.array(cg)
    M = np.cross(r, Fv).sum(axis=0)
    Lv = np.array([math.sin(a), math.cos(a), 0.0])
    CL = Fsum @ Lv / S_REF; CD = Fsum @ V / S_REF
    Cm = M[2] / (S_REF * L_REF)
    CN = Fv.sum(axis=0)[1]
    xcp = float(M[2] / CN) if abs(CN) > 1e-6 else float("nan")    # x ЦД по нормальной силе (относит. ЦМ)
    return float(CL), float(CD), float(Cm), float(xcp + cg[0])   # xcp — в осях спецификации

alphas = list(range(0, 91, 2))
HYP = {}
for tip in (0, 60, 90):
    for el in (-25, -10, 0, 10, 25):
        HYP["t%d_e%d" % (tip, el)] = [newton(a, tip, el) for a in alphas]

def trims(curve):
    """Балансировочные α: Cm = 0, с наклоном dCm/dα (на градус)."""
    out = []
    for i in range(len(alphas) - 1):
        c0, c1 = curve[i][2], curve[i + 1][2]
        if c0 == 0 or c0 * c1 < 0:
            t = alphas[i] + (alphas[i + 1] - alphas[i]) * c0 / (c0 - c1)
            out.append({"alpha": round(t, 1), "dCm_dalpha_per_deg": round((c1 - c0) / (alphas[i + 1] - alphas[i]), 5),
                        "stable": bool((c1 - c0) < 0)})
    return out

TRIM = {k: trims(v) for k, v in HYP.items()}
# точная баллистическая проверка α = 90°: перебор фиксированных установок
EXT_A = list(range(-20, 181, 5))
EXT = {"t90_e-25": [newton(a, 90, -25) for a in EXT_A], "t60_e0": [newton(a, 60, 0) for a in EXT_A]}
XCP = {a: round(newton(a, 60, 0)[3], 2) for a in (10, 20, 30, 40, 60, 90)}
BALL = []
for tip in (0, 30, 60, 90):
    for el in range(-25, 26, 5):
        CL, CD, Cm, xcp = newton(90, tip, el)
        CLm, CDm, Cmm, _ = newton(86, tip, el)
        BALL.append({"tip": tip, "elev": el, "Cm90": round(Cm, 5), "CD90": round(CD, 3), "xcp90": round(xcp, 2),
                     "dCm_dalpha": round((Cm - Cmm) / 4, 5)})

# площади
def planform(tip_deg):
    C, NA = config_faces(tip_deg, 0)
    return float(np.sum(np.clip(-NA[:, 1], 0, None)))      # проекция наветренных снизу на плоскость xz
S_plan0, S_plan60, S_plan90 = planform(0), planform(60), planform(90)
span0 = 18.0
AR = span0 ** 2 / S_plan0

# дозвук: Полхамус
Kp = 2 * math.pi * AR / (2 + math.sqrt(AR ** 2 + 4))       # Хелмбольд — потенциальная часть, dCL/dα при α→0
Kv_sharp = math.pi
Kv = 0.5 * math.pi                                         # скруглённая кромка R 0,17 м: вихревая доля примерно вдвое меньше
CD0_sub = 0.014 + 0.012     # трение + донное сопротивление тупой кормы (~12 м² торца, Cd_дон ≈ 0,17)
def polhamus(a_deg, kv):
    a = math.radians(a_deg)
    CL = Kp * math.sin(a) * math.cos(a) ** 2 + kv * math.sin(a) ** 2 * math.cos(a)
    CD = CD0_sub + CL * math.tan(a)
    return CL, CD
SUB = {"rounded": [polhamus(a, Kv) for a in alphas], "sharp": [polhamus(a, Kv_sharp) for a in alphas]}
def ldmax(curve):
    best = max(((c[0] / c[1]) if c[1] > 0 else 0, alphas[i]) for i, c in enumerate(curve))
    return best
LDsub = ldmax(SUB["rounded"])
# посадка без тяги: масса = сухая + полезная нагрузка + 0,5 т остатков
m_land = (SPEC["ttx"]["dry_t"] + SPEC["ttx"]["payload_t"] + 0.5) * 1000
def v_land(a_deg, kv):
    CL = polhamus(a_deg, kv)[0]
    return math.sqrt(2 * m_land * 9.81 / (1.225 * S_plan0 * CL))
VL = {"alpha14_rounded": v_land(14, Kv), "alpha14_sharp": v_land(14, Kv_sharp), "alpha18_rounded": v_land(18, Kv)}

# сверхзвук: линейная теория дельты
def sup(M, a_deg):
    beta = math.sqrt(M * M - 1); m = beta / TAN
    if m >= 1:
        CLa = 4 / beta
    else:
        CLa = 2 * math.pi / TAN / ellipe(1 - m * m)
    a = math.radians(a_deg)
    CL = CLa * a
    CDw = 4 * (0.4 / 10.8) ** 2 / beta * 2          # волновое по толщине (оценка, ×2 на корпус)
    CD = 0.02 + CDw + CL * math.tan(a)
    return CL, CD, CLa
SUP = {str(M): [sup(M, a)[:2] for a in alphas] for M in (1.5, 2.0, 3.0)}
SUP_CLa = {str(M): sup(M, 1)[2] for M in (1.5, 2.0, 3.0)}

# β
def beta_of(m_kg, CD):
    return m_kg / (CD * S_REF)
m_entry = (SPEC["ttx"]["dry_t"] + SPEC["ttx"]["payload_t"]) * 1000
hyp60 = HYP["t60_e0"]
CD20, CD40, CL20, CL40 = hyp60[10][1], hyp60[20][1], hyp60[10][0], hyp60[20][0]
CD90b = newton(90, 90, -25)[1]
CMP = [
    ("Площадь в плане, м² (концевые 0°)", SPEC["ttx"]["planform_area_m2"], round(S_plan0, 1)),
    ("Площадь в плане, концевые 90°, м²", "—", round(S_plan90, 1)),
    ("Удлинение λ = b²/S", "—", round(AR, 2)),
    ("Гиперзвук L/D при α 20° (концевые 60°)", 2.1, round(CL20 / CD20, 2)),
    ("Гиперзвук L/D при α 40°", 1.12, round(CL40 / CD40, 2)),
    ("CD баллистика α 90° (концевые 90°, элевоны −25°)", 1.3, round(CD90b, 3)),
    ("β баллистика, кг/м² (m = сухая + ПН = %.1f т)" % (m_entry / 1000), SPEC["ttx"]["beta_ball_kgm2"], round(beta_of(m_entry, CD90b))),
    ("β планирующий (α 40°), кг/м²", SPEC["ttx"]["beta_lift_kgm2"], round(beta_of(m_entry, CD40))),
    ("ЦД при α 90°, м от ЦМ (концевые 90°, элевоны 0°)", SPEC["ttx"]["ball_xcp_m"], round(newton(90, 90, 0)[3], 2)),
    ("Дозвук L/D max (Полхамус, скругл. кромка)", SPEC["ttx"]["ld_subsonic"], "%.2f при α %d°" % LDsub),
    ("Посадка без тяги, м/с (α 14°, m %.1f т)" % (m_land / 1000), SPEC["ttx"]["v_land_power_off_ms"], round(VL["alpha14_rounded"], 1)),
    ("Объём корпуса (замкнутая оболочка), м³", "—", round(vol_hull, 1)),
]

# ----------------------------------------------------------------------------- Н3: нагрев, прочность, объём крыла, балансировка
SIG = 5.670e-8; EPS = 0.85; RHO_PK, V_PK = 8.3e-5, 7000.0; T_LIM = 2400.0
LAM = 55.0
def sg_q(R, alpha=None):
    """Саттон–Грейвс, Вт/м²; ×cos^1,2 Λ; с alpha — эффективная стреловидность sinΛe = sinΛ·cosα (проверка)."""
    lam = LAM if alpha is None else math.degrees(math.asin(math.sin(math.radians(LAM)) * math.cos(math.radians(alpha))))
    return 1.74e-4 * math.sqrt(RHO_PK / R) * V_PK ** 3 * math.cos(math.radians(lam)) ** 1.2
T_eq = lambda q: (q / (EPS * SIG)) ** 0.25
HEAT = []
for nm, R in (("корень крыла z 3,0", wing_r(3.0)), ("середина z 5,35", wing_r(5.35)), ("конец крыла z 7,7", wing_r(7.7)), ("конец концевой части", TIP_R_END)):
    q = sg_q(R); q40 = sg_q(R, 40); q30 = sg_q(R, 30)
    HEAT.append({"place": nm, "R": round(R, 3), "q_MW": round(q / 1e6, 3), "T": round(T_eq(q)), "margin": round(T_LIM - T_eq(q)),
                 "T_a30": round(T_eq(q30)), "T_a40": round(T_eq(q40)), "margin_a40": round(T_LIM - T_eq(q40))})
# низ крыла: Таубер (1989), плоская пластина, ламинар и турбулент, x от передней кромки, hw/h0 ≈ 0,1
def tauber(x, a_deg, turb):
    phi = math.radians(a_deg)
    if turb:
        C = 2.2e-5 * math.cos(phi) ** 2.08 * math.sin(phi) ** 1.6 * x ** -0.2 * (1 - 1.11 * 0.1)
        return C * RHO_PK ** 0.8 * V_PK ** 3.7
    C = 2.53e-5 * math.cos(phi) ** 0.5 * math.sin(phi) * x ** -0.5 * (1 - 0.1)
    return C * RHO_PK ** 0.5 * V_PK ** 3.2
HEAT_LOW = [{"alpha": a, "x": x, "T_lam": round(T_eq(tauber(x, a, False))), "T_turb": round(T_eq(tauber(x, a, True)))}
            for a in (20, 30, 40) for x in (1.0, 3.0, 8.0)]

# материалы (НЕ канон: в DESIGN_LOCAL.md свойств боразонового композита нет — справочные аналоги, допущения)
MATP = {
    "rho": (4800.0, "кг/м³ — допущение: смесь 50/50 по объёму c-BN (3480, справочник) и ZrB₂ (6090, справочник)"),
    "E": (480e9, "Па — допущение по ZrB₂–SiC (Fahrenholtz и др., J. Am. Ceram. Soc. 90 (2007) 1347: 450–520 ГПа)"),
    "sig": (200e6, "Па — допущение: расчётный предел на растяжение при ~1500 K; у ZrB₂–SiC изгибная прочность 400–700 МПа при 1500 °C, ×0,4 на разброс Вейбулла и растяжение"),
    "tau": (100e6, "Па — допущение: 0,5 от расчётного предела на растяжение"),
    "FS": (1.5, "коэффициент безопасности 1,5 — авиационная норма (АП/FAR 25.303)"),
    "t_skin": (0.008, "м — обшивка 6–10 мм по спецификации Н2 (materials_ru), взята 8 мм"),
}
RHO_M, E_M, SIG_M, TAU_M, FS = (MATP[k][0] for k in ("rho", "E", "sig", "tau", "FS")); TS = MATP["t_skin"][0]
Z_ROOT = 3.0
def box_section(z, A_cap, t_web, xs0=0.15, xs1=0.65, n=24):
    """Кессон сечения z: плоская нижняя панель, выпуклая верхняя, стенки на xs0/xs1 хорды, 4 полки площадью A_cap."""
    ch = le(z) - te_w(z); t = wing_t(z); r = wing_r(z)
    xi = np.linspace(xs0, xs1, n); hu = np.array([thick_frac(v, t, 0.06, r) for v in xi]) - TS / 2
    dx = ch * (xs1 - xs0) / (n - 1)
    ys = list(np.full(n, TS / 2)) + list(hu) + [TS / 2, TS / 2, hu[0], hu[-1]]
    As = [TS * dx] * n + [TS * dx] * n + [A_cap] * 4
    h0, h1 = hu[0] - TS / 2, hu[-1] - TS / 2
    ys += [h0 / 2 + TS / 2, h1 / 2 + TS / 2]; As += [t_web * h0, t_web * h1]
    ys, As = np.array(ys), np.array(As)
    yc = (ys * As).sum() / As.sum()
    I = (As * (ys - yc) ** 2).sum() + t_web * (h0 ** 3 + h1 ** 3) / 12
    return {"chord": ch, "t": t, "I": I, "yc": yc, "ymax": max(hu.max() - yc, yc), "h_web": (h0, h1), "A": As.sum(), "box_w": ch * (xs1 - xs0)}

def wing_area_out(z0=Z_ROOT):
    zz = np.linspace(z0, 7.7, 40); ch = np.array([le(z) - te_w(z) for z in zz]); return float(np.trapezoid(ch, zz)), float(np.trapezoid(ch * zz, zz))
S_out, Sz_out = wing_area_out()
S_out += 1.3 * 0.5 * (3.18 + 1.4); Sz_out += 1.3 * 0.5 * (3.18 + 1.4) * 8.35
A_CAP, T_WEB = 0.0030, 0.006
sec = box_section(Z_ROOT, A_CAP, T_WEB)
# масса крыла за корнем (одна сторона): обшивки, стенки, полки, кромка-труба, нервюры +15 %
m_w = 1.15 * (2 * S_out * TS * RHO_M + 0.5 * (sec["h_web"][0] + sec["h_web"][1]) * 2 * T_WEB * 6.0 * RHO_M + 4 * A_CAP * 6.0 * RHO_M
              + 2 * math.pi * 0.13 * 0.012 * 5.9 * RHO_M)
z_w = Sz_out / S_out - Z_ROOT

def aero_root_loads(alpha, tip, n, mass_kg):
    """Нагрузки в корне правого крыла по ньютоновскому распределению меша при перегрузке n (нормальная сила = n·m·g)."""
    C, NA = config_faces(tip, 0)
    a = math.radians(alpha); Vv = np.array([-math.cos(a), math.sin(a), 0.0])
    A = np.linalg.norm(NA, axis=1); nn = NA / A[:, None]; s = -(nn @ Vv)
    F = -(np.where(s > 0, CPMAX * s ** 2, 0.0) * A)[:, None] * nn
    q = n * mass_kg * 9.81 / F[:, 1].sum()
    sel = C[:, 2] > Z_ROOT
    V = q * F[sel, 1].sum(); M = q * (F[sel, 1] * (C[sel, 2] - Z_ROOT)).sum()
    V -= n * m_w * 9.81; M -= n * m_w * 9.81 * z_w                            # разгрузка массой крыла
    return V, M, q
m_e, m_l = CG["entry"][1] * 1000, CG["landing"][1] * 1000
a_land = 7.0 ** 2 / (2 * 0.85 * 0.8)                                       # 7 м/с, ход 0,85 м, КПД амортизатора 0,8 (допущение)
n_land = 1 + a_land / 9.81
CASES = []
for nm, kind, par in (("Баллистический вход днищем, α 90°, 5,5 g (спецификация)", "aero", (90, 90, 5.5, m_e)),
                      ("Планирующий вход, α 40°, 2,5 g (допущение — норма манёвра)", "aero", (40, 60, 2.5, m_e)),
                      ("Выход из пике, дозвук, 2,5 g, посадочная масса (допущение; давление по площади равномерно)", "sub", (2.5, m_l)),
                      ("Посадка 7 м/с: инерция крыла, n %.2f (ход 0,85 м, КПД 0,8)" % n_land, "inert", (n_land,)),
                      ("Висение на чашах: инерция крыла 1 g × 1,5 (порывы, допущение)", "inert", (1.5,))):
    if kind == "aero":
        V, M, q = aero_root_loads(*par)
    elif kind == "sub":
        n, mm = par; p = n * mm * 9.81 / S_plan0
        V = p * S_out - n * m_w * 9.81; M = p * (Sz_out - Z_ROOT * S_out) - n * m_w * 9.81 * z_w
    else:
        V = -par[0] * m_w * 9.81; M = -par[0] * m_w * 9.81 * z_w
    Mu, Vu = abs(M) * FS, abs(V) * FS
    sig = Mu * sec["ymax"] / sec["I"]; tau = Vu / ((sec["h_web"][0] + sec["h_web"][1]) * T_WEB)
    CASES.append({"case": nm, "V_kN": round(V / 1e3, 1), "M_kNm": round(M / 1e3, 1), "sig_MPa": round(sig / 1e6, 1),
                  "tau_MPa": round(tau / 1e6, 1), "MS_sig": round(SIG_M / sig - 1, 2), "MS_tau": round(TAU_M / tau - 1, 2)})
# устойчивость сжатой обшивки: панель шириной 1/4 кессона, Kc 4, ν 0,2 (допущение)
b_pan = sec["box_w"] / 4
sig_cr = 4.0 * math.pi ** 2 * E_M / (12 * (1 - 0.2 ** 2)) * (TS / b_pan) ** 2
def vol_wing(z0=Z_ROOT, z1=7.7, xs0=0.0, xs1=1.0):
    zz = np.linspace(z0, z1, 30); vv = []
    for z in zz:
        ch = le(z) - te_w(z); xi = np.linspace(xs0, xs1, 40)
        vv.append(np.trapezoid([thick_frac(v, wing_t(z), 0.06, wing_r(z)) for v in xi], xi * ch))
    return 2 * float(np.trapezoid(vv, zz))
V_WING, V_BOX = vol_wing(), vol_wing(xs0=0.15, xs1=0.65)
RHO_LAR = 1395.0   # жидкий аргон при 87 K, справочник
TRIM_FLAP = {}
for fl in (0, 15, 25, 30, 35):
    cur = [newton(a, 60, 0, flap_deg=fl) for a in alphas]
    TRIM_FLAP["fl%d" % fl] = {"Cm": [round(c[2], 4) for c in cur], "trim": trims(cur), "LD": [round(c[0] / c[1], 3) if c[1] else 0 for c in cur]}
N3 = {"heat": HEAT, "heat_low": HEAT_LOW, "mat": {k: [v[0], v[1]] for k, v in MATP.items()}, "cases": CASES,
      "root": {"z": Z_ROOT, "chord": round(sec["chord"], 2), "t": round(sec["t"], 3), "tc_pct": round(100 * sec["t"] / sec["chord"], 1),
               "I_m4": round(sec["I"], 5), "A_cap_cm2": A_CAP * 1e4, "t_web_mm": T_WEB * 1e3, "t_skin_mm": TS * 1e3, "box": "0,15–0,65 хорды",
               "sig_cr_MPa": round(sig_cr / 1e6, 1), "b_panel_m": round(b_pan, 2)},
      "m_wing_t": round(m_w / 1000, 2), "z_w": round(z_w, 2), "V_wing_m3": round(V_WING, 2), "V_box_m3": round(V_BOX, 2),
      "argon_in_box_t": round(V_BOX * RHO_LAR / 1000, 1), "sections": WING_SEC, "flap": TRIM_FLAP,
      "cg": {k: {"x": round(float(v[0]), 2), "m_t": round(v[1], 2)} for k, v in CG.items()},
      "mass": [[i[0], i[1], round(float(i[2]), 2), i[3]] for i in MASS_FIXED] + [["chashi", M_LIFT, round(float(X_LIFT), 2), "Узел 6 подъёмных чаш (ряды x ЦМ ± 3,0)"],
                                                                                 ["argon", "18,3 / 6,3 / 0,5", round(float(X_TANK), 2), "Баки аргона — центр на ЦМ, выработка не сдвигает ЦМ"]],
      "rows": [X_ROW_F, X_ROW_R]}
T1 = 200e3
HOVER = []
for k, (xc, mt) in CG.items():
    W_ = mt * 1000 * 9.81; d = float(xc) - X_LIFT
    need_row = W_ * (3.0 + abs(d)) / 6.0
    HOVER.append({"phase": k, "x_cg": round(float(xc), 2), "m_t": mt, "TW": round(6 * T1 / W_, 2),
                  "TW_fail1": round(4 * T1 / W_, 2) if need_row <= 2 * T1 else 0})
N3["hover"] = HOVER
_allv = np.vstack([np.array(GROUPS[n].v) for n in ORDER if GROUPS[n].v and not n.startswith("shassi")])
N3["bbox_flight_m"] = [round(float(_allv[:, i].max() - _allv[:, i].min()), 2) for i in range(3)]
print("N3 heat", HEAT); print("N3 cases", CASES); print("N3 root", N3["root"], "m_w", N3["m_wing_t"], "V", V_WING, V_BOX); print("hover", HOVER)
print("flap trims", {k: v["trim"] for k, v in TRIM_FLAP.items()}); print("bbox", N3["bbox_flight_m"])

def r3(curve):
    return [[round(v, 4) for v in c] for c in curve]

aero = {
    "alphas": alphas, "S_ref": S_REF, "L_ref": L_REF, "Cp_max": CPMAX, "Cf": CF,
    "hyp": {k: r3(v) for k, v in HYP.items()}, "trim": TRIM, "ballistic_scan": BALL,
    "sub": {k: r3(v) for k, v in SUB.items()}, "sub_params": {"Kp": Kp, "Kv": Kv, "Kv_sharp": Kv_sharp, "AR": AR, "CD0": CD0_sub,
                                                            "LDmax": LDsub, "v_land": VL, "m_land_t": m_land / 1000},
    "sup": {k: r3(v) for k, v in SUP.items()}, "sup_CLa_per_rad": SUP_CLa,
    "areas": {"plan_tip0": S_plan0, "plan_tip60": S_plan60, "plan_tip90": S_plan90},
    "n3": N3, "compare": CMP, "ext_alphas": EXT_A, "ext": {k: r3(v) for k, v in EXT.items()}, "xcp": XCP,
}

# ----------------------------------------------------------------------------- шасси: проверки и ТТХ (расчёт — GEAR.md)
def gear_check():
    out = {}
    for s, d in GEAR.items():
        P = np.array(d["piv"]); Rm = rotmat([0, 0, 1], 90)
        V = np.vstack([np.array(GROUPS["shassi_" + s].v), np.array(GROUPS["shassi_%s_kolesa" % s].v)])
        Vr = (V - P) @ Rm.T + P
        outside = [p for p in Vr if not (abs(p[2]) < hw(p[0]) - 0.005 and ybz(p[0], p[2]) + 0.005 < p[1] < yt(p[0]) - 0.005)]
        x0, x1, z0, z1 = DOORS[s]
        in_well = all(x0 - 0.02 <= p[0] <= x1 + 0.02 and z0 - 0.02 <= p[2] <= z1 + 0.02 for p in Vr if p[1] < ybz(p[0], p[2]) + 0.5)
        W = np.array(GROUPS["shassi_%s_kolesa" % s].v)
        wheels_below = bool(np.all(W[:, 1] < np.array([ybz(p[0], p[2]) for p in W]) - 0.02))
        # створка открыта: плоскость y–x на петле; колесо не должно заходить за неё
        hz = (z0 if s != "R" else z1) if s != "N" else z0
        door_clear = float(np.min(np.abs(W[:, 2] - hz)) - 0.0)
        out[s] = {"retracted_outside_vertices": len(outside), "retracted_within_door_outline": bool(in_well),
                  "extended_wheels_below_hull": wheels_below, "wheel_to_open_door_plane_m": round(door_clear, 3),
                  "ground_y_m": round(float(W[:, 1].min()), 3)}
    return out
GEAR_CHECK = gear_check()
GEAR_TTX = [
    ("Схема", "трёхопорная с ПО; основные — тележки 2 оси × 2 колеса (тандем-спарка), ПО — спарка"),
    ("База / колея, м", "10,15 / 5,2"), ("Высота ЦМ над грунтом, м", "2,35"),
    ("Доля веса ПО / основных", "33 % / 67 %"),
    ("Угол опрокидывания (≤ 63°)", "54°"), ("Боковой уклон до опрокидывания без выравнивания", "35,6°"),
    ("Запас по моменту: уклон 10° + ветер 30 / 45 м/с, 41 т", "4,6 / 2,0"),
    ("Угол касания хвостом", "17,8° (посадка на полосу при α ≤ 14°)"),
    ("Расчётная вертикальная скорость", "7 м/с (палубный уровень), без подъёмной силы"),
    ("Ход амортизатора + обжатие колеса", "0,85 + 0,10 м"), ("Перегрузка при 7 м/с", "4,2 g (кресла полулёжа, ≤ 5 g)"),
    ("Аварийно 10 м/с (сминаемый картридж +0,35 м)", "5,8 g, однократно"),
    ("Сброс с высоты при срыве поля → 7 м/с", "Земля 2,5 м · Марс 6,6 м · Луна 15 м"),
    ("Колёса", "10 шт. Ø0,86 × 0,26 м, безвоздушные лепестковые (МПУ), лепестки — боразоновое волокно в борной керамике"),
    ("Колесо на 75 м/с", "1670 об/мин, обод 1330 g, кольцевое напряжение ρv² ≈ 13 МПа — запас большой; раскрутка мотором до касания"),
    ("Нагрузка на колесо: стат. осн. / ПО", "40 / 80 кН"), ("Нагрузка на колесо: касание 7 м/с, осн. / ПО", "254 / 339 кН"),
    ("Давление на грунт, осн. колесо: Земля / Марс / Луна", "0,48 / 0,18 / 0,08 МПа (ПО — вдвое больше)"),
    ("Тормоза", "8 (на основных), диски боразон / карбид бора, 2 независимых контура"),
    ("Энергия торможения с 75 м/с, 49 т", "138 МДж; 14,6 МДж на колесо (15 % — аэродинамика); теплопоглотитель 16 кг/колесо при ΔT 900 К, стоит 62 кг (запас ×3: прерванный взлёт, повтор)"),
    ("Пробег", "720 м при 0,4 g · 960 м при 0,3 g"),
    ("Выравнивание (активная подвеска)", "±0,35 м разноход стоек: поперёк 7,7°, вдоль 3,9° → на склоне 10° остаётся ≤ 2,5° поперёк"),
    ("Уборка / выпуск", "вперёд: 2 независимых электромеханических привода на стойку; аварийно — вес + пружина + набегающий поток, замки с двумя пиропатронами"),
    ("Масса шасси", "2,8 т (5,7 % посадочной): стойки 1,0; тележки 0,24; колёса 0,48; тормоза 0,50; приводы 0,21; выравнивание 0,09; створки-полозья 0,26; картриджи 0,06"),
]
GEAR_FAIL = [
    ("Одна основная не вышла", "Убрать все опоры, сесть на три закрытые створки-полоза (бериллиевая бронза + боразон) — симметрично; при работающих чашах — вертикально на чаши. Пробег на полозьях μ 0,25 ≈ 1150 м"),
    ("ПО не вышла", "Посадка на основные, нос опускается на створку-полоз ПО на ~40 м/с"),
    ("Отказ приводов уборки/выпуска", "Второй привод; затем свободный выпуск: вес, пружина, поток (уборка вперёд — поток помогает выпуску)"),
    ("Пробой амортизатора / превышение 7 м/с", "Сминаемый боразоновый сотовый картридж 0,35 м в стойке, до 10 м/с при 5,8 g"),
    ("Срыв поля на малой высоте", "Шасси выпущено всегда ниже 50 м; на Земле — безопасно до 2,5 м, выше — ограничение висения"),
    ("Повреждение колеса", "Безвоздушное: проколов нет, при потере 30 % лепестков катится; тележка — 4 колеса, оставшиеся 3 несут"),
    ("Отказ тормозного контура", "Второй контур; торможение элевонами (воздушный тормоз) и обратным УВТ маршевых"),
    ("Уклон > 10° или камень под опорой", "Активная подвеска ±0,35 м; висение и смена площадки"),
]


# ============================================================================ Ф: итоговые расчёты (балансировка, режимы ЦМ, шасси, Т/В, ангар, подвижные части)
from scipy.spatial import cKDTree
FIN = {}
R2 = lambda v, n=2: round(float(v), n)
# --- перекачка
MDOT_PUMP = 6 * 200e3 / 15e3          # кг/с: магистрали рассчитаны на полный расход 6 чаш (F = m'·v, v 15 км/с — спецификация)
M_TR = min(CAP_F, AR_ENTRY - 0.5)     # 0,5 т резерва вдува остаётся в кормовом баке
X_GLIDE, M_E = cg_ar(M_TR, AR_ENTRY - M_TR)
X_BALL = cg_ar(0, AR_ENTRY)[0]
need_16 = (-1.6 - X_BALL) * M_E / (X_TF - X_TA)
FIN["transfer"] = {"mdot_kg_s": R2(MDOT_PUMP, 1), "mdot_src": "6 × 200 кН / 15 км/с (спецификация: тяга чаши и скорость истечения) — магистраль под полный расход висения",
    "m_tr_t": R2(M_TR), "t_s_2pumps": R2(M_TR * 1000 / MDOT_PUMP, 0), "t_s_1pump": R2(2 * M_TR * 1000 / MDOT_PUMP, 0),
    "x_tank_f": R2(X_TF), "x_tank_a": R2(X_TA), "cap_f_t": R2(CAP_F), "cap_a_t": R2(CAP_A), "cap_total_t": R2(CAP_F + CAP_A),
    "argon_needed_t": R2(AR_TOT), "argon_shortfall_t": R2(AR_TOT - CAP_F - CAP_A),
    "x_glide": R2(X_GLIDE), "x_ball": R2(X_BALL), "m_entry_t": R2(M_E), "need_for_-1.6_t": R2(need_16)}
# --- слив без питания: давление насыщенных паров аргона (NIST): 100 K — 0,324 МПа, 87,3 K — 0,101 МПа
DP = 0.324e6 - 0.101e6; CD_V, D_L = 0.6, 0.10; A_L = math.pi * D_L ** 2 / 4
head_glide = RHO_AR * 9.81 * 1.0 * (X_TF - X_TA)          # осевая перегрузка <= 1 g к носу (ДОПУЩЕНИЕ)
head_ball = RHO_AR * 9.81 * 5.5 * (TANK_A["yc"] - TANK_A["R"] - TANK_F["y0"])   # 5,5 g к днищу, подъём на дно кормового бака
def q_drain(dp): return CD_V * A_L * math.sqrt(2 * RHO_AR * dp) if dp > 0 else 0.0
FIN["drain"] = {"dp_MPa": R2(DP / 1e6, 3), "head_glide_kPa": R2(head_glide / 1e3, 0), "head_ball_kPa": R2(head_ball / 1e3, 0),
    "q_glide_kg_s": R2(q_drain(DP - head_glide), 0), "q_ball_kg_s": R2(q_drain(DP - head_ball), 0),
    "t_glide_s": R2(M_TR * 1000 / max(q_drain(DP - head_glide), 1e-6), 0), "t_ball_s": R2(M_TR * 1000 / max(q_drain(DP - head_ball), 1e-6), 0),
    "basis": "носовой бак держится насыщенным при ~100 K (0,324 МПа), кормовой — 87,3 K (0,101 МПа), NIST; клапан слива нормально открытый (держится закрытым питанием); магистраль Ø0,10 м, Cd 0,6, осевая перегрузка в планировании ≤ 1 g — допущения"}
# --- балансировка
def trim_scan(cg_x, tip, el, fl, al=range(0, 91, 2)):
    al = list(al)
    cur = [newton(a, tip, el, cg=(cg_x, 0, 0), flap_deg=fl) for a in al]
    out = []
    for i in range(len(al) - 1):
        c0, c1 = cur[i][2], cur[i + 1][2]
        if c0 * c1 < 0:
            t = al[i] + (al[i + 1] - al[i]) * c0 / (c0 - c1)
            sl = (c1 - c0) / (al[i + 1] - al[i])
            d0 = newton(t + 1, tip, el, (cg_x - 1, 0, 0), fl)[2] - newton(t - 1, tip, el, (cg_x - 1, 0, 0), fl)[2]
            d1 = newton(t + 1, tip, el, (cg_x, 0, 0), fl)[2] - newton(t - 1, tip, el, (cg_x, 0, 0), fl)[2]
            xnp = cg_x - d1 / (d1 - d0) if abs(d1 - d0) > 1e-12 else float("nan")
            CL, CD, _, _ = newton(t, tip, el, (cg_x, 0, 0), fl)
            out.append({"alpha": R2(t, 1), "dCm_dalpha": round(sl, 5), "stable": bool(sl < 0), "x_np": R2(xnp), "margin_m": R2(cg_x - xnp),
                        "margin_pct": R2(100 * (cg_x - xnp) / 20.0, 1), "LD": R2(CL / CD), "CD": R2(CD, 3)})
    return out
GL = []
for fl in (0, 15, 25):
    for el in (-10, 0, 10):
        GL.append({"flap": fl, "elev": el, "trims": trim_scan(X_GLIDE, 60, el, fl)})
FIN["glide"] = GL
BL = []
for tip in (60, 90):
    for el in (-25, 0, 25):
        BL.append({"tip": tip, "elev": el, "trims": trim_scan(X_BALL, tip, el, 0, al=range(60, 122, 2))})
FIN["ballistic"] = BL
CDb = newton(90, 90, 0, (X_BALL, 0, 0))[1]
FIN["ballistic_beta"] = R2(M_E * 1000 / (CDb * S_REF), 0); FIN["ballistic_CD"] = R2(CDb, 3)
TR = []
for f in np.linspace(0, 1, 6):
    xc = X_GLIDE + f * (X_BALL - X_GLIDE)
    t = trim_scan(xc, 60, 0, 0, al=range(0, 122, 2))
    TR.append({"x_cg": R2(xc), "trims": [[q["alpha"], q["stable"]] for q in t]})
FIN["transition"] = TR
FIN["heat_work"] = [{"place": h["place"], "R": h["R"], "T_a30": h["T_a30"], "margin_a30": T_LIM - h["T_a30"], "T_a35": round(T_eq(sg_q(h["R"], 35))),
                     "margin_a35": round(T_LIM - T_eq(sg_q(h["R"], 35))), "T_a40": h["T_a40"], "margin_a40": h["margin_a40"]} for h in HEAT]
# --- режимы ЦМ
MODES_CG = [
    ("Старт (полный аргон, носовой бак полон)", *cg_ar(min(CAP_F, AR_TOT), AR_TOT - min(CAP_F, AR_TOT))),
    ("Орбита / по умолчанию без питания (6,3 т в кормовом)", *cg_ar(0, AR_ENTRY)),
    ("Вход — планирование (%.2f т перекачано вперёд)" % M_TR, X_GLIDE, M_E),
    ("Вход — баллистика (всё в кормовом)", X_BALL, M_E),
    ("Посадка (0,5 т в кормовом)", *cg_ar(0, 0.5)),
    ("Висение (перекачкой на центр рядов %.2f; при 6,3 т)" % X_ROWC, X_ROWC, M_E),
]
FIN["cg_modes"] = [{"mode": m, "x": R2(x), "m_t": R2(mm)} for m, x, mm in MODES_CG]
T_CUPS = 6 * 0.2; T_MAIN = 2 * 0.45
def tw(T, m): return R2(T * 1e6 / (m * 1000 * 9.81))
FIN["tw"] = [{"mode": m, "m_t": R2(mm), "TW_cups": tw(T_CUPS, mm), "TW_fail1": tw(4 * 0.2, mm), "TW_mains": tw(T_MAIN, mm),
              "TW_mars": tw(T_CUPS, mm * 3.71 / 9.81), "TW_moon": tw(T_CUPS, mm * 1.62 / 9.81)} for m, x, mm in MODES_CG]
# --- шасси (опоры GEAR.md: ПО x 6,75, основные x −3,4 z ±2,6, грунт y −2,35; y ЦМ = 0 — ДОПУЩЕНИЕ)
XN, XM, ZM, HG = 6.75, -3.4, 2.6, 2.35
def gear_eval(xc, m, h=HG):
    share = (xc - XM) / (XN - XM)
    P1, P2, Pc = np.array([XN, 0.0]), np.array([XM, ZM]), np.array([xc, 0.0])
    v1, v2 = P2 - P1, Pc - P1
    d = abs(v1[0] * v2[1] - v1[1] * v2[0]) / np.linalg.norm(v1)
    return {"x": R2(xc), "m_t": R2(m), "nose_share_pct": R2(100 * share, 1), "tipback_deg": R2(math.degrees(math.atan2(xc - XM, h)), 1),
            "turnover_deg": R2(math.degrees(math.atan2(h, d)), 1), "lat_slope_max_deg": R2(math.degrees(math.atan2(d, h)), 1),
            "slope10_x_eff": R2(xc - h * math.tan(math.radians(10))), "slope10_ok": bool(xc - h * math.tan(math.radians(10)) > XM),
            "main_wheel_static_kN": R2(m * 9.81 * (1 - share) / 8, 0), "nose_wheel_static_kN": R2(m * 9.81 * share / 2, 0),
            "main_wheel_7ms_kN": R2(m * 9.81 * n_land / 8, 0)}
GEAR_N = [dict(gear_eval(x, mm), mode=m) for m, x, mm in MODES_CG]
FIN["gear"] = {"rows": GEAR_N, "n_7ms": R2(n_land), "basis": "ход 0,85 м, КПД 0,8 (GEAR.md) — перегрузка от массы не зависит; касание 7 м/с — всё на 8 колёсах основных; угол опрокидывания по Raymer (≤ 63°); нос. доля норма 8–15 % (Raymer)",
               "sens_h": {"h_2.05": gear_eval(X_BALL, M_E, 2.05)["tipback_deg"], "h_2.65": gear_eval(X_BALL, M_E, 2.65)["tipback_deg"]}}
m_land_f = cg_ar(0, 0.5)[1] * 1000
FIN["v_land"] = {"m_t": R2(m_land_f / 1000), "v_a14": R2(math.sqrt(2 * m_land_f * 9.81 / (1.225 * S_plan0 * polhamus(14, Kv)[0])), 1),
                 "v_a18": R2(math.sqrt(2 * m_land_f * 9.81 / (1.225 * S_plan0 * polhamus(18, Kv)[0])), 1), "S_plan": R2(S_plan0, 1),
                 "v_a14_old_mass": R2(VL["alpha14_rounded"], 1), "m_old_t": R2(m_land / 1000)}
FIN["mass"] = [[i[0], i[1], R2(i[2]), i[3]] for i in MASS_FIXED] + [["gruz", M_PAY, X_PAY, "Полезная нагрузка"]]
FIN["mass_src"] = {k: [v[0], v[1]] for k, v in MSRC.items()}
FIN["mass_sum"] = {"dry_t": R2(DRY), "payload_t": M_PAY, "argon_t": R2(AR_TOT), "m0_t": R2(M0), "m_entry_t": R2(M_E), "m_land_t": R2(m_land_f / 1000),
                   "x0_fixed": R2(X0_FIX), "areas_m2": {"hull": R2(A_HULL, 1), "wings": R2(A_WING, 1), "tips": R2(A_TIP, 1), "elevons": R2(A_EL, 1)},
                   "wing_per_side_t": R2(m_wing / 2), "hull_struct_t": R2(m_hull_skin + m_hull_ins + m_hull_frame)}
# --- подвижные части по режимам
ANIM_SET = set(g for e in ANIM for g in e["groups"])
def moved(entry, mode):
    V = np.vstack([np.array(GROUPS[g].v) for g in entry["groups"] if GROUPS[g].v])
    deg = entry["modes"].get(mode, 0)
    if not deg:
        return V
    Rm = rotmat(entry["axis"], deg); P = np.array(entry["pivot"], float)
    return (V - P) @ Rm.T + P
def inside_p(p, tol):
    x, y, z = p
    if x < -9 + tol or x > X_TIP - tol: return False
    return abs(z) < hw(x) - tol and ybz(x, z) + tol < y < yt(x) - tol
OPEN = [tuple(DOORS[k]) for k in DOORS] + [(X_ROW_F - 0.6, X_ROW_F + 0.6, -2.0, 2.0), (X_ROW_R - 0.6, X_ROW_R + 0.6, -2.0, 2.0),
        (FLAP_X1, FLAP_X0, -FLAP_W / 2, FLAP_W / 2), (HX - HR - 0.1, HX + HR + 0.1, -9, -1.0)]
def in_open(p):
    in_sock = p[0] < hull_x[0] + SOCK_D + 0.05 and min(math.hypot(p[2] - zc, p[1] - SOCK_YC) for zc in (-1.8, 1.8)) < SOCK_R + 0.05   # гнёзда маршевых
    return in_sock or p[0] < -8.85 or any(a - 0.05 <= p[0] <= b + 0.05 and c - 0.05 <= p[2] <= d + 0.05 for a, b, c, d in OPEN)
MV = []; PAIRS = []; LEN = {}
STATIC = [np.array(GROUPS[n].v) for n in ORDER if GROUPS[n].v and n not in ANIM_SET]
for mode in MODES:
    Vs = []
    for e in ANIM:
        V = moved(e, mode); Vs.append(V)
        n_in = sum(1 for p in V if inside_p(p, 0.03)); n_out = sum(1 for p in V if not inside_p(p, -0.03))
        bad = sum(1 for p in V if inside_p(p, 0.03) and not in_open(p)) if (n_in and n_out) else 0
        MV.append({"mode": mode, "part": e["note"], "in": n_in, "out": n_out, "cross_outside_openings": bad})
    for i in range(len(ANIM)):
        ti = cKDTree(Vs[i])
        for j in range(i + 1, len(ANIM)):
            d = ti.query(Vs[j], k=1)[0].min()
            if d < 0.05:
                PAIRS.append({"mode": mode, "a": ANIM[i]["note"], "b": ANIM[j]["note"], "d_min_m": R2(d, 3)})
    AV = np.vstack(STATIC + Vs)
    LEN[mode] = {"x_min": R2(AV[:, 0].min(), 3), "x_max": R2(AV[:, 0].max(), 3), "L": R2(AV[:, 0].max() - AV[:, 0].min(), 3),
                 "W": R2(AV[:, 2].max() - AV[:, 2].min(), 3), "H": R2(AV[:, 1].max() - AV[:, 1].min(), 3)}
FIN["movers"] = {"by_mode": MV, "close_pairs": PAIRS, "method": "вершины подвижных групп после поворота по режиму; «внутри» — в контуре корпуса с допуском 3 см; пересечение обшивки засчитывается, если группа и внутри, и снаружи, а внутренние вершины не под проёмом (створки шасси, ряды чаш, щиток, люк, срезы маршевых); пары — мин. расстояние между вершинами < 5 см (по вершинам, не по граням — приближённо)"}
FIN["lengths"] = LEN
OPEN_L, ROOM_L = 20.0, 20.2
Vst = np.vstack(STATIC + [moved(e, "stowed") for e in ANIM])
def proj_len(theta):
    t = math.radians(theta); x = Vst[:, 0] * math.cos(t) - Vst[:, 1] * math.sin(t); return float(x.max() - x.min())
TILT = [{"deg": d, "proj_L": R2(proj_len(d), 3)} for d in (0, 5, 10, 20, 30, 45, 60, 75, 90)]
L_st = LEN["stowed"]["L"]
FIN["hangar"] = {"opening_m": OPEN_L, "room_m": ROOM_L, "L_by_mode": {k: v["L"] for k, v in LEN.items()}, "L_stowed": L_st,
    "tilt": TILT, "shorten_clear_0.10_each_side": R2(L_st + 0.2 - OPEN_L, 3), "shorten_clear_0.05_each_side": R2(L_st + 0.1 - OPEN_L, 3),
    "opening_clear_0.10": R2(L_st + 0.2, 3), "opening_clear_0.25": R2(L_st + 0.5, 3),
    "note": "наклон по тангажу: проекция на длину проёма = L·cosθ + H·sinθ — почти не уменьшается, пока θ не близок к 90° (вертикальный проход носом вниз — габарит по высоте помещения); рыскание в проёме требует ширины проёма — не задана"}
print("FIN transfer", FIN["transfer"]); print("FIN drain", FIN["drain"]); print("FIN glide", [(g["flap"], g["elev"], g["trims"]) for g in GL])
print("FIN ball", [(b["tip"], b["elev"], b["trims"]) for b in BL]); print("FIN trans", TR); print("FIN cg", FIN["cg_modes"]); print("FIN tw", FIN["tw"])
print("FIN gear", GEAR_N); print("FIN vland", FIN["v_land"]); print("FIN len", LEN); print("FIN tilt", TILT)
print("FIN movers bad", [m for m in MV if m["cross_outside_openings"]]); print("FIN pairs", PAIRS)

# json для браузера
groups_js = []
for nm in ORDER:
    g = GROUPS[nm]
    V = np.round(np.array(g.v), 4).flatten().tolist(); N = np.round(np.array(g.n), 3).flatten().tolist()
    groups_js.append({"name": nm, "mat": MATERIALS[g.mat - 1][0], "purpose": g.purpose, "v": V, "n": N, "uv": np.round(np.array(g.uv), 3).flatten().tolist(),
                      "i": [x for t in g.t for x in t], "ntri": len(g.t)})
layout = [p for p in G["primitives"] if p["role"] in ("cabin", "shield", "charges", "other") and p["id"] not in ("photon_strip",)]
json.dump({"materials": {m[0]: {"color": HEX[m[0]], "emissive": "#%02x%02x%02x" % tuple(int(255 * c) for c in m[3]),
                                "tex": (TEXTURES[MAT_TEX[m[0]] - 1] if m[0] in MAT_TEX else None)} for m in MATERIALS},
           "groups": groups_js, "anim": ANIM, "modes": MODES, "rcs": RCS, "layout": layout, "aero": aero,
           "gear": {"ttx": GEAR_TTX, "fail": GEAR_FAIL, "check": GEAR_CHECK}, "final": FIN, "stats": {"triangles": NTRI, "groups": len(ORDER), "open_edges": checks, "hull_volume_m3": vol_hull}},
          open(os.path.join(OUT, "lander_mesh.json"), "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))

# ----------------------------------------------------------------------------- сводка в консоль
print("треугольников:", NTRI, "групп:", len(ORDER))
print("открытые рёбра корпуса:", checks, "объём", round(vol_hull, 1))
for nm in ORDER:
    print("  %-22s %6d  %s" % (nm, len(GROUPS[nm].t), GROUPS[nm].purpose))
print("площадь в плане 0/60/90:", round(S_plan0, 1), round(S_plan60, 1), round(S_plan90, 1), "AR", round(AR, 2))
for k in ("t60_e0", "t60_e-25", "t60_e25", "t0_e0", "t90_e0", "t90_e-25"):
    print("trim", k, TRIM[k])
for row in CMP:
    print(" | ".join(str(x) for x in row))
print("баллистика (tip, elev, Cm90, dCm/dα, xcp):")
for b in BALL:
    if b["elev"] in (-25, -15, 0, 15, 25):
        print("  ", b)
print("xcp", XCP)
for k,v in EXT.items():
    print(k, [(a, round(c[2],3)) for a,c in zip(EXT_A, v)])
print("sub", aero["sub_params"])
print("sup CLa", SUP_CLa)

print('gear check:', GEAR_CHECK)
