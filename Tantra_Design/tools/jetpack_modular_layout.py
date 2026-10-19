# Модульный ранец: where each hex module and the capsule sit (right arm; the left one mirrors it). Shared by
# jetpack_modular_check.py (the numbers) and blender/jetpack_modular.py (the mock-ups), so both see the same pack.
# Plan coordinates: x out from the spine, y back from the pack's plane (+ behind her), metres; z the plates' height.
# A hexagon: flat-to-flat s, a vertex at angle `rot` (deg); its neighbours lie s away at rot+30+60k.
import math

def hex_verts(c, s, rot):
    R = s / math.sqrt(3)
    return [(c[0] + R * math.cos(math.radians(rot + 60 * k)), c[1] + R * math.sin(math.radians(rot + 60 * k))) for k in range(6)]

def ring(cx, cy, d, angles):
    return [(cx + d * math.cos(math.radians(a)), cy + d * math.sin(math.radians(a))) for a in angles]

def trefoil(s, inner=0.24, gap=0.035):
    """three hexes round the capsule, sharing its point: one inboard, two outboard fore and aft"""
    R = s / math.sqrt(3); d = R + gap
    xc = inner + R + d
    return dict(s=s, rot=0, cells=ring(xc, 0.0, d, (180, 60, -60)), cap=(xc, 0.0))

def rosette(s, inner=0.22, gap=0.01):
    """six hexes in a ring, the capsule in the middle cell"""
    R = s / math.sqrt(3); d = s + gap
    xc = inner + R + d * math.cos(math.radians(30))
    return dict(s=s, rot=0, cells=ring(xc, 0.0, d, (30, 90, 150, 210, 270, 330)), cap=(xc, 0.0))

def line(s, x0, n, gap=0.01, cap_gap=0.07):
    """n hexes in a row along the arc (flat sides facing each other), the capsule at the end"""
    R = s / math.sqrt(3)
    cells = [(x0 + i * (s + gap), 0.0) for i in range(n)]
    return dict(s=s, rot=30, cells=cells, cap=(cells[-1][0] + s / 2 + cap_gap + 0.05, 0.0))

def single(s, x0, cap_gap=0.07):
    R = s / math.sqrt(3)
    return dict(s=s, rot=0, cells=[(x0, 0.0)], cap=(x0 + R + cap_gap + 0.05, 0.0))

def nimbus(s):
    """a canopy over the helmet (her head at y ~ -0.25 from the pack's plane): the arc rises over the head"""
    g = s + 0.01; h = g * math.cos(math.radians(30))
    cells = [(h / 2 + 0.0, -0.25 + g / 2), (h / 2, -0.25 - g / 2), (h * 1.5, -0.25), (h * 1.5, -0.25 + g)]
    R = s / math.sqrt(3)
    return dict(s=s, rot=30 - 30, cells=cells, cap=(h * 1.5 + R + 0.12, -0.10))

Z_PLATE, Z_NIMBUS = 1.56, 1.98
LAYOUTS = {
    "1": dict(single(0.46, 0.42), z=Z_PLATE),
    "2": dict(line(0.34, 0.36, 2), z=Z_PLATE),
    "3": dict(trefoil(0.28), z=Z_PLATE),
    "4": dict(rosette(0.20), z=Z_PLATE),
    "5": dict(line(0.26, 0.31, 4), z=Z_PLATE),
    "6": dict(nimbus(0.30), z=Z_NIMBUS, over_head=True),
    "7": dict(trefoil(0.24), z=Z_PLATE, coax=True),
    "8": dict(trefoil(0.28), z=Z_PLATE, fold=True),
    "9": dict(rosette(0.20), z=Z_PLATE, dist=True),
    "10": dict(trefoil(0.28), z=Z_PLATE, light=True),
}

def extents(L):
    """the inner edge (closest to the spine) and the half-width (cells' vertices or the capsule ~0.075 round)"""
    vs = [v for c in L["cells"] for v in hex_verts(c, L["s"], L["rot"])]
    inner = min(abs(v[0]) for v in vs) if not L.get("over_head") else min(v[0] for v in vs)
    half = max(max(v[0] for v in vs), L["cap"][0] + 0.075)
    return inner, half

# ---- the sketches (the user, 2026-10-16: «неэстетично, неустойчиво конструкцией, не похоже на 30 век - эскизы»):
# no struts - the modules dock straight into a load-carrying yoke (a blade, a frame, a shell) that sits on the core
# through one wide pitch bearing. Each cell: (x, y, z, rot_deg, roll_deg); y relative to the pack's plane. pod: (x, y, z).
def _ring(c, d, s, angles, z):
    return [(c[0] + d * math.cos(math.radians(a)), c[1] + d * math.sin(math.radians(a)), z, 0, 0) for a in angles]
def _fan(pv, r, angles, z0):
    return [(pv[0] + r * math.cos(math.radians(a)), pv[1] + r * math.sin(math.radians(a)), z0 + 0.012 * i, a, 0) for i, a in enumerate(angles)]
RING6 = (30, 90, 150, 210, 270, 330)
SKETCH = {
    "1": dict(name="Лезвие", s=0.28, cells=[(0.34, 0.04, 1.53, 30, 0), (0.73, 0.06, 1.50, 30, 0)], pod=(0.535, 0.05, 1.50), extra=4.0,
              note="несущая дуга - лезвие обтекаемого сечения, соты-кассеты под ним, капсула между ними"),
    "2": dict(name="Кассета-7", s=0.20, cells=_ring((0.55, 0.0), 0.21, 0.20, RING6, 1.55), pod=(0.55, 0.0, 1.53), extra=3.0,
              note="сот из семи: шесть модулей вокруг капсулы, кассета надевается на балку УВТ"),
    "3": dict(name="Гексарама", s=0.26, cells=[(0.42, 0.0, 1.55, 0, 0), (0.645, 0.13, 1.55, 0, 0), (0.42, 0.26, 1.55, 0, 0)],
              pod=(0.88, 0.13, 1.50), extra=6.0, note="замкнутая шестигранная рама - жёсткая на кручение, соты внутри"),
    "4": dict(name="Веер", s=0.25, cells=_fan((0.28, 0.03), 0.33, (-40, 8, 56, 104), 1.54), pod=(0.28, 0.03, 1.47), extra=4.0,
              note="четыре сота на одной оси над капсулой: в шаге сложены стопкой за спиной", fold=True),
    "5": dict(name="Панцирь", s=0.22, cells=[(0.30, -0.06, 1.575, 0, 0), (0.49, 0.05, 1.575, 0, 0), (0.30, 0.16, 1.575, 0, 0), (0.49, -0.17, 1.575, 0, 0)],
              pod=(0.68, -0.01, 1.47), extra=7.0, note="сотовая чешуя заподлицо в обтекаемых крыльях-панцирях"),
    "6": dict(name="Чешуя-чайка", s=0.25, cells=[(0.36, 0.02, 1.53, 0, -6), (0.577, 0.145, 1.57, 0, -6), (0.794, 0.02, 1.61, 0, -6)],
              pod=(0.577, -0.105, 1.48), extra=4.0, note="соты ступенькой вверх по дуге-чайке: поперечное V, устойчивость по крену"),
    "7": dict(name="Соосная кассета", s=0.18, cells=_ring((0.50, 0.0), 0.19, 0.18, RING6, 1.56), pod=(0.50, 0.0, 1.53), extra=3.5, coax=True,
              note="кассета-7 в два яруса роторов: уже и тише"),
    "8": dict(name="Трилистник-гондола", s=0.28, cells=_ring((0.60, 0.0), 0.197, 0.28, (180, 60, -60), 1.55), pod=(0.60, 0.0, 1.53), extra=4.0,
              fold=True, note="три сота и капсула в одной гондоле, складывается вниз вдоль спины"),
    "9": dict(name="Крыло Исиды", s=0.24, cells=[(0.33, 0.07, 1.58, 30, 0), (0.58, 0.13, 1.57, 30, 0), (0.83, 0.19, 1.48, 30, 0)],
              pod=(0.99, 0.22, 1.42), extra=5.0, note="сотовое крыло с опущенной законцовкой, капсулы на концах"),
    "10": dict(name="Кассета-7, двойная балка", s=0.20, cells=_ring((0.55, 0.0), 0.21, 0.20, RING6, 1.55), pod=(0.55, 0.0, 1.53), extra=5.0, dist=True,
               note="две балки, у каждого сота свой энергоблок"),
}

# ---- v2 of the three chosen (the user, 2026-10-16: «№8 перспективный, ещё лезвие и Исида - нужна доработка»):
# the blade and the wing open up - two streamlined spars fore and aft with the cells between them (nothing over the
# cells' intake); the Isis wing gets an elbow carrying the capsule; all three fold down along her back.
SKETCH["1b"] = dict(name="Лезвие v2 (открытое)", s=0.28, cells=[(0.29, 0.04, 1.55, 30, 0), (0.67, 0.04, 1.55, 30, 0)], pod=(0.48, 0.04, 1.55),
                    extra=4.0, fold=True, note="две кромки-лонжерона, соты между ними, капсула под нервюрой")
SKETCH["9b"] = dict(name="Исида v2 (с локтем)", s=0.24, cells=[(0.29, 0.02, 1.58, 30, 0), (0.54, 0.02, 1.58, 30, 0), (0.84, 0.05, 1.535, 30, -20)],
                    pod=(0.68, 0.03, 1.56), extra=5.5, fold=True, note="открытое крыло из двух звеньев, капсула в локте")
SKETCH["8b"] = dict(name="Трилистник-гондола v2", s=0.28, cells=_ring((0.62, 0.0), 0.197, 0.28, (180, 60, -60), 1.55), pod=(0.62, 0.0, 1.55),
                    extra=4.0, fold=True, note="гондола дальше от плеча, обтекатель-звезда, корневой шарнир")

# 8c - the game's trefoil (2026-10-17, the user's in-game test: the arm ran right over the inboard cell's intake and that
# cell sat against her side): turned 60 deg - one cell outboard, two inboard fore and aft, the arm comes in along the
# slot between them; nothing over an intake. The cells 7 cm off the centre: the slot 0.12 m wide takes the arm,
# the gondola's hub and the capsule clear the cells' inner corners
SKETCH["8c"] = dict(name="Трилистник-гондола (игра)", s=0.28, cells=_ring((0.63, 0.06), 0.2317, 0.28, (0, 120, 240), 1.55), pod=(0.63, 0.06, 1.55),
                    extra=4.0, fold=True, note="рычаг в щели между сотами, один сот наружу, два внутрь")

def sketch_extents(S):
    vs = [v for c in S["cells"] for v in hex_verts(c[:2], S["s"], c[3])]
    return min(abs(v[0]) for v in vs), max(max(v[0] for v in vs), S["pod"][0] + 0.075)
