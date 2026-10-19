# «Наплечный» M2 - where the main (cruise) engine may go without burning her (the user, 2026-10-17: «зад сожжёт с такой
# схемой - логику включи и расчёты тепла вокруг сопел»; five layouts after the references: the UMMU turbopack, the
# Off the Grid packs). The capsules (УВТ) stay M1's: 2 x 500 N at (+-0.60, 0.175, 1.47), pitch gimbal +-20 deg.
#
# The jet: metallic hydrogen relaxed to hot H2, 10 km/s. In air it burns on: the plume ~2000 K over its first half
# metre, ~900 K at 1.5 m; it spreads at ~12 deg half-angle from the exit radius. The heat on her:
#   - direct impingement: anything inside the cone (+5 cm) - a burn-through, forbidden;
#   - radiation: the plume as a thin emitter (eps 0.15 - H2O bands) plus the hot nozzle lip (900 K, eps 0.7); the flux
#     at a body point is summed over the plume's segments at the worst orientation of the surface. The suit's white
#     outer layer takes <= 4 kW/m2 for minutes (it stays under ~440 K); 4..10 needs a heat shield between; more burns;
#   - the ground: a jet pointed down reaches the ground when she is low and splashes back round her legs - so every
#     main engine here is cut below ~3 m and she lands on the capsules («форсаж»: 2 x 1 kN in air).
# The balance: the main thrust must act through the centre of mass or be trimmed - by its own gimbal (+-10 deg), by
# the capsules' pitch gimbal (+-20 deg: the moment they can add is small, their own thrust already sits 9 cm behind
# the CM), or, with four nozzles, by throttling the fore and aft pairs against each other.
# Her and the pack: 104 + 53 kg; the CM at (0, 0.085, 1.07) (her 1.00 m, the pack's 53 kg at y 0.25, z 1.2).
# Run: python jetpack_m2_heat.py -> the table; jetpack_m2_heat.json (the sketches' captions); the layouts for Blender.
import math, json, os
SIGMA = 5.67e-8
CM = (0.0, 0.085, 1.07)
CAPS = [(0.60, 0.175, 1.47), (-0.60, 0.175, 1.47)]; CAP_F = 500.0; CAP_GIM = 20.0
BODY = [("ягодицы", (0.0, 0.12, 0.95)), ("поясница", (0.0, 0.10, 1.12)), ("бедро сзади", (0.10, 0.09, 0.65)), ("бедро сбоку", (0.20, 0.0, 0.75)),
        ("икра", (0.10, 0.08, 0.30)), ("голень низ", (0.10, 0.06, 0.08)), ("кисть в полёте", (0.22, -0.28, 1.15)), ("шлем сзади", (0.0, 0.12, 1.65))]
HAND_STAND = ("кисть стоя", (0.30, 0.02, 0.85))     # the engine never runs on the ground: shown for the record

def unit(v):
    l = math.sqrt(sum(a * a for a in v)) or 1.0; return tuple(a / l for a in v)
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def mul(a, k): return tuple(x * k for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def dist(a, b): return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))

def jet_dir(sg, out, aft):
    """the jet's direction: down, canted `out` deg outboard (sign sg) and `aft` deg aft"""
    o, a = math.radians(out), math.radians(aft)
    return unit((sg * math.sin(o) * math.cos(a), math.sin(a), -math.cos(o) * math.cos(a)))

def exit_radius(F): return 0.065 * math.sqrt(F / 1200.0)        # the exit of a 1.2 kN nozzle ~13 cm across

def plume_T(s): return 1800.0 if s < 0.4 else max(900.0, 1800.0 - (s - 0.4) * 820.0)   # the afterburning sheath, then mixing
EPS = 0.10                                                       # a small hydrogen flame: H2O bands only, optically thin
# what stands between her and a plume: the pack's own body (the tank section, the lumbar block, the kidney panels) and
# each layout's heat shields - axis-aligned boxes; a line of sight that passes through one is blocked
PACK_BOXES = [(-0.205, 0.205, 0.11, 0.35, 1.20, 1.53), (-0.165, 0.165, 0.11, 0.346, 0.96, 1.21), (0.16, 0.33, 0.12, 0.31, 0.95, 1.18), (-0.33, -0.16, 0.12, 0.31, 0.95, 1.18)]
def inside(p, b): return b[0] <= p[0] <= b[1] and b[2] <= p[1] <= b[3] and b[4] <= p[2] <= b[5]
def blocked(P, c, boxes):
    for k in range(1, 16):
        q = add(P, mul(sub(c, P), k / 16))
        if any(inside(q, b) for b in boxes): return True
    return False

def heat_at(P, nozzles, boxes):
    """(radiant flux W/m2 at the worst orientation of the surface; True if inside a plume)"""
    q, hit = 0.0, False
    for nz in nozzles:
        E, d, r0 = nz["exit"], nz["dir"], exit_radius(nz["F"])
        s = 0.0
        while s <= 1.5:
            c = add(E, mul(d, s)); r = r0 + s * math.tan(math.radians(12))
            dd = dist(P, c)
            if dd < r + 0.05: hit = True
            if not blocked(P, c, boxes):
                q += EPS * SIGMA * plume_T(s) ** 4 * (2 * math.pi * r * 0.02) / (4 * math.pi * max(dd, 0.05) ** 2)
            s += 0.02
        if not blocked(P, E, boxes): q += 0.7 * SIGMA * 900.0 ** 4 * (2 * math.pi * r0 * 0.06) / (4 * math.pi * max(dist(P, E), 0.05) ** 2)   # the hot lip
    return q, hit

def ground_impact(nz):
    """where the jet's axis meets the ground when she hovers 2 m up: the splash radius round her feet"""
    d = nz["dir"]
    if d[2] >= -0.05: return None
    t = (nz["exit"][2] + 2.0) / -d[2]; p = add(nz["exit"], mul(d, t))
    return math.hypot(p[0], p[1])

def trim(nozzles, differential=False):
    """the pitch balance: the main thrust's moment about the CM, and what trims it (its own gimbal, the capsules' pitch
    gimbal, or the fore/aft throttle split); returns (moment N m, the fix, ok)"""
    def moment(nz, gim=0.0, k=1.0):
        d = nz["dir"]; a = math.radians(gim)   # its own gimbal: tilt the jet aft (+) or forward (-) about x
        dj = unit((d[0], d[1] * math.cos(a) - d[2] * math.sin(a), d[1] * math.sin(a) + d[2] * math.cos(a)))
        F = mul(dj, -nz["F"] * k)
        return cross(sub(nz["exit"], CM), F)[0]
    def caps(phi):
        a = math.radians(phi); F = (0.0, -CAP_F * math.sin(a), CAP_F * math.cos(a))
        return sum(cross(sub(c, CM), F)[0] for c in CAPS)
    M0 = sum(moment(nz) for nz in nozzles)
    best = None
    gims = [g for g in range(-10, 11, 2)]
    for g in gims:
        if differential:
            for a in [i / 10 for i in range(0, 11)]:        # the fore pair's share (two pairs, the total thrust kept)
                M = sum(moment(nz, g, 2 * a if nz["pair"] == "fore" else 2 * (1 - a)) for nz in nozzles)
                for phi in range(-20, 21, 1):
                    tot = M + caps(phi)
                    if best is None or abs(tot) < abs(best[0]): best = (tot, g, phi, a)
        else:
            M = sum(moment(nz, g) for nz in nozzles)
            for phi in range(-20, 21, 1):
                tot = M + caps(phi)
                if best is None or abs(tot) < abs(best[0]): best = (tot, g, phi, None)
    tot, g, phi, a = best
    fix = "кардан маршевого %+d°, капсулы %+d°" % (g, phi) + ("" if a is None else ", передняя пара %d %%" % round(100 * a))
    return M0, fix, abs(tot) < 15

def shield_boxes(nz):
    """the layout's heat shields as boxes: a shroud plate on the body's side of a nozzle (hanging 0.30 m below its lip,
    0.30 m fore-aft), or the skirt under the block (a plate between the jet and her seat and thighs), or the apron"""
    x, y, z = nz["exit"]; r0 = exit_radius(nz["F"]); sg = 1 if x >= 0 else -1
    if nz["shield"] == "inboard":
        xi = abs(x) - r0 - 0.02; L = nz.get("shield_len", 0.30)
        return [(min(sg * xi, sg * (xi - 0.012)), max(sg * xi, sg * (xi - 0.012)), y - 0.16, y + 0.16, z - L, z + 0.10)]
    if nz["shield"] == "skirt": return [(-0.21, 0.21, y - 0.15, y - 0.138, z - 0.32, z + 0.07)]
    if nz["shield"] == "apron": return [(-0.21, 0.21, 0.24, 0.252, 0.70, 0.97)]
    return []

def make(key, name, ref, nozzles, note, differential=False):
    return dict(key=key, name=name, ref=ref, nozzles=nozzles, note=note, differential=differential)

def N(x, y, z, out, aft, F, pair=None, shield=None, shield_len=0.30):
    sg = 1 if x > 0 else -1 if x < 0 else 1
    return dict(exit=(x, y, z), dir=jet_dir(sg, out, aft), F=F, out=out, aft=aft, pair=pair, shield=shield, shield_len=shield_len)

LAYOUTS = [
    make("1", "Четыре угловых", "UMMU: сопла по углам рамы",
         [N(0.31, 0.15, 0.965, 40, 30, 350, "fore", "inboard", 0.55), N(-0.31, 0.15, 0.965, 40, 30, 350, "fore", "inboard", 0.55),
          N(0.31, 0.32, 0.965, 40, 48, 350, "aft", "inboard", 0.55), N(-0.31, 0.32, 0.965, 40, 48, 350, "aft", "inboard", 0.55)],
         "4 × 350 Н на рельсах нижней рамы у бёдер, отвал 40° наружу, щитки до колен; тангаж — дросселированием пар. Струи идут вдоль ног: икры ~15 кВт/м² при любом отвале", differential=True),
    make("2", "Бедренные", "Off the Grid: сопла из нижнего блока у бёдер",
         [N(0.255, 0.215, 0.895, 30, 46, 650, shield="inboard", shield_len=0.55), N(-0.255, 0.215, 0.895, 30, 46, 650, shield="inboard", shield_len=0.55)],
         "2 × 650 Н под боковыми панелями, отвал 30° наружу и 46° назад, кардан ±10°, щитки до колен. Срез в 20 см от бедра: ноги греются при любом отвале"),
    make("3", "Консоли", "Off the Grid: широкий силовой блок с боковыми выносами",
         [N(0.44, 0.17, 0.965, 40, 38, 650, shield="inboard", shield_len=0.55), N(-0.44, 0.17, 0.965, 40, 38, 650, shield="inboard", shield_len=0.55)],
         "2 × 650 Н на коротких консолях за линией рук, отвал 40° наружу и 38° назад (линия тяги через центр масс), кардан ±10°, щитки-крылья до колен"),
    make("4", "Крейсерский", "сопло назад из задней стенки блока на высоте центра масс",
         [N(0.0, 0.36, 1.07, 0, 90, 1200, shield="apron")],
         "1 × 1200 Н горизонтально назад на высоте центра масс: тело перед срезом, струя за ним, снизу фартук; подъём только капсулами (на Земле — форсаж, 2 кН)"),
    make("5", "Жало с экраном", "Off the Grid: один большой выход снизу",
         [N(0.0, 0.34, 0.90, 0, 52, 1200, shield="skirt")],
         "1 × 1200 Н под блоком, вынесен назад коротким каналом и повёрнут на 52° назад (иначе не триммируется), юбка-экран между струёй и ногами"),
]

def analyse(L):
    boxes = list(PACK_BOXES)
    for nz in L["nozzles"]: boxes += shield_boxes(nz)
    L["boxes"] = boxes
    rows = []
    for name, P in BODY:
        q, hit = heat_at(P, L["nozzles"], boxes)
        verdict = "ПОПАДАНИЕ" if hit else "ОЖОГ" if q > 10e3 else "экран" if q > 4e3 else "ок"
        rows.append((name, q / 1e3, verdict))
    worst = max(rows, key=lambda r: (r[2] == "ПОПАДАНИЕ", r[1]))
    M0, fix, ok = trim(L["nozzles"], L["differential"])
    F = sum(nz["F"] for nz in L["nozzles"])
    lift = sum(-nz["dir"][2] * nz["F"] for nz in L["nozzles"]); push = sum(nz["dir"][1] * nz["F"] for nz in L["nozzles"])
    splash = [ground_impact(nz) for nz in L["nozzles"]]; splash = [s for s in splash if s is not None]
    return dict(rows=rows, worst=worst, M0=M0, fix=fix, trim_ok=ok, F=F, lift=lift, push=push, splash=min(splash) if splash else None,
                heat_ok=all(r[2] == "ок" for r in rows), heat_shield=all(r[2] in ("ок", "экран") for r in rows))

if __name__ == "__main__":
    out = {}
    for L in LAYOUTS:
        A = analyse(L); out[L["key"]] = dict(name=L["name"], ref=L["ref"], note=L["note"], **{k: v for k, v in A.items() if k != "rows"}, rows=A["rows"])
        print("%s %-16s F %4.0f Н: подъём %4.0f, назад %4.0f | момент %+4.0f Н·м -> %s %s | струя у земли (с 2 м) %s | тепло: %s"
              % (L["key"], L["name"], A["F"], A["lift"], A["push"], A["M0"], A["fix"], "ок" if A["trim_ok"] else "НЕ ТРИММИРУЕТСЯ",
                 "—" if A["splash"] is None else "%.1f м от ног" % A["splash"], "ок" if A["heat_ok"] else "с экраном" if A["heat_shield"] else "ЖЖЁТ"))
        print("     " + " · ".join("%s %.1f %s" % r for r in A["rows"]))
    json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "jetpack_m2_heat.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
