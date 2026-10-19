# Модульный ранец (the user, 2026-10-16): «Наплечный» с УВТ - постоянная база, атмосферный режим - подключаемый
# атмо-кит из шестигранных сот-модулей. Общая кинематика: дуга УВТ сверху несёт капсулы на концах, соты
# пристёгиваются к той же дуге - один карданный наклон (тангаж) на всё, поворот каждого плеча дуги (крен/рыскание),
# вектор сот и капсул всегда параллелен; одна водородная система на всё.
# Run: python jetpack_modular_check.py  -> the table on stdout, the data in jetpack_modular.json
import math, json, os
from jetpack_modular_layout import LAYOUTS, extents, SKETCH, sketch_extents
BODY = 104.0                                  # she in the suit, kg
BASE_DRY, BASE_FUEL = 16.0 + 2.0, 12.0        # «Наплечный» (JetPack.h DRY 16) + the reinforced arc with its clips
POD_F, POD_VE = 500.0, 10000.0                # a capsule: N at 10 km/s (JetPack.h POD_F, ISP)
POD_P = POD_F * POD_VE / 2                    # its jet power, 2.5 MW
FORCE_VE = 5000.0                             # «форсаж» in air: the same power at half the jet speed -> twice the thrust
KIT_FUEL = 20.0                               # the kit's metallic hydrogen (the power unit; the capsules can draw on it)
MH, H2AIR = 216e6, 120e6
ENV = {"Земля +15°C": (9.81, 1.225, 288, True), "Земля +50°C": (9.81, 1.093, 323, True), "Земля −40°C": (9.81, 1.514, 233, True),
       "Земля 3 км": (9.81, 0.909, 269, True), "Титан": (1.35, 5.4, 94, False), "Венера 50 км": (8.87, 1.59, 350, False),
       "Венера 42 км": (8.87, 2.8, 400, False), "Марс": (3.71, 0.020, 210, False)}

def power_share(T):                           # the power unit's cooling: full to 320 K, half at 400 K, none at 460 K
    if T <= 320: return 1.0
    if T <= 400: return 1.0 - 0.5 * (T - 320) / 80
    return max(0.0, 0.5 * (460 - T) / 60)

def energy(o2): return ((0.60 * H2AIR if o2 else 0.0) + 0.30 * MH) * 0.95   # J per kg of the kit's hydrogen at the rotors

class V:
    """n hex modules per side (flat-to-flat s, m), their centres xs (m out from the spine), at height z; the capsule at xc"""
    def __init__(s, key, name, n, size, xs, z, P, xc, note, fm=0.65, coax=False, dist=False, fold=False, fold_w=None, over_head=False, extra=0.0):
        s.__dict__.update(locals()); del s.__dict__["s"]
        R = size / math.sqrt(3)
        s.A_mod = 0.60 * 2.598 * R * R                         # the rotors' share of the hexagon
        s.A = 2 * n * s.A_mod
        cells = 2 * n * (1.5 + 55 * s.A_mod * (1.5 if coax else 1.0) + 0.5)        # frame + rotors + the clip
        pu = (2 * n * (s.P / (2 * n)) / 10e3) if dist else (2.0 + s.P / 15e3)      # the power unit(s): 15 kW/kg, small ones 10
        s.kit = cells + pu + 3.0 + KIT_FUEL + (2.0 if fold else 0.0) + extra               # + the tank, the hydrogen, the hinges
        s.m = BODY + BASE_DRY + BASE_FUEL + s.kit
        if key.startswith("S"): s.inner, half = sketch_extents(SKETCH[key[1:]])
        else: s.inner, half = extents(LAYOUTS[key])                                 # from the mock-ups' layout
        s.width = 2 * half

    def T_mod(s, rho, share):
        if rho <= 0: return 0.0
        return (s.fm * s.P / (2 * s.n) * share * math.sqrt(2 * rho * s.A_mod)) ** (2 / 3)

    def check(s):
        o = {}
        for env, (g, rho, T, o2) in ENV.items():
            sh = power_share(T); W = s.m * g
            Tc = 2 * s.n * s.T_mod(rho, sh)
            Ph = W ** 1.5 / (s.fm * math.sqrt(2 * rho * s.A)) + (3e3 if T < 150 else 0)
            ok = Ph <= s.P * sh
            o[env] = dict(tw=Tc / W, boost_tw=(Tc + 2 * POD_F) / W, force_tw=(Tc + 4 * POD_F) / W, hover_kw=Ph / 1e3,
                          hours=KIT_FUEL * energy(o2) / Ph / 3600 if ok else 0.0, ok=ok, share=sh)
        # the kit off: the base alone
        for env, g in (("Луна без кита", 1.62), ("Марс без кита", 3.71)):
            o[env] = dict(tw=2 * POD_F / ((BODY + BASE_DRY + BASE_FUEL) * g))
        g, rho = 9.81, 1.225; W = s.m * g
        o["downwash"] = math.sqrt(W / (2 * rho * s.A))
        o["disc_load"] = W / s.A
        # the top speed: the arc tilted 35°, the cells' ram drag, the body's 0.8 m2 + the modules' edges; and with the capsules
        a = 0.5 * rho * (0.8 + 0.02 * 2 * s.n)
        for tag, extra in (("vmax", 0.0), ("vmax_boost", 2 * POD_F)):
            th = math.radians(35); T = W / math.cos(th)
            mdot = math.sqrt(rho * s.A * T)
            Fx = T * math.sin(th) + extra * math.sin(th)
            o[tag] = (-mdot + math.sqrt(mdot * mdot + 4 * a * Fx)) / (2 * a)
        # the roll: the arms' differential thrust (±0.2 W) at their mean arm + the arms' twist (±15°) - all on one gimbal,
        # the capsules ride along (their ±20° of УВТ add); her roll inertia with the modules and the capsules out there
        xm = sum(s.xs) / len(s.xs)
        Mc = 0.2 * W * xm + 2 * POD_F * math.sin(math.radians(20)) * s.xc + W * 0.45 * math.sin(math.radians(15)) * 0.5
        m_mod = (s.kit - KIT_FUEL - 5) / (2 * s.n)
        I = 4.0 + sum(2 * m_mod * x * x for x in s.xs) + 2 * 3.0 * s.xc ** 2 + (s.kit * 0.3 if s.over_head else 0)
        o["t15"] = 0.15 + math.sqrt(2 * math.radians(15) / (Mc / I))
        # the failures (Earth, hover W/2 per arm)
        T1 = s.T_mod(rho, 1.0) * 1.15                          # a module at its 15 % overload
        need = W / 2
        side_one = (s.n - 1) * T1 + POD_F                      # one module lost: its arm's others + the capsule (normal)
        o["mod_fail"] = side_one / need
        o["mod_fail_f"] = ((s.n - 1) * T1 + 2 * POD_F) / need  # the same with the capsule on «форсаж»
        o["arm_fail"] = 2 * POD_F / need                       # the whole arm's cells lost: its capsule on «форсаж» alone
        if s.dist: pu = (2 * s.n - 1) * T1 / W                # one small power unit lost = one module lost
        else:      pu = 2 * s.n * s.T_mod(rho, 0.5) / W        # the central unit's half (two halves, two buses)
        o["pu_fail"] = pu
        o["pu_fail_f"] = pu + 4 * POD_F / W
        o["force_s"] = BASE_FUEL / (2 * 2 * POD_F / FORCE_VE)  # both capsules on «форсаж», s of the base's own fuel
        o["mass_kit"] = s.kit; o["mass_all"] = BASE_DRY + BASE_FUEL + s.kit; o["width"] = s.width; o["inner"] = s.inner
        o["fold_w"] = s.fold_w or s.width
        return o

# xc - the capsule at the arc's end; the arc over the shoulders at z ~1.50 (the figure's shoulders 1.42)
def lay(key, name, P, note, **kw):
    L = LAYOUTS[key]
    return V(key, name, len(L["cells"]), L["s"], [abs(c[0]) for c in L["cells"]], L["z"], P, L["cap"][0], note, **kw)

VARS = [
    lay("1", "Дуга-2: по одному крупному", 300e3, "минимальный кит: по большому соту на плечо, капсула снаружи"),
    lay("2", "Дуга-4: пара в линию", 300e3, "два сота на плечо вдоль дуги, капсула на конце"),
    lay("3", "Трилистник", 300e3, "три сота вокруг капсулы - тяга и вектор в одной точке"),
    lay("4", "Розетка", 300e3, "шесть сот кольцом, капсула в центральной ячейке"),
    lay("5", "Полоса", 300e3, "четыре сота в линию - большая площадь, широко"),
    lay("6", "Нимб над головой", 300e3, "соты над шлемом, капсулы на концах дуги", over_head=True),
    lay("7", "Соосный трилистник", 300e3, "трилистник в два яруса роторов", fm=0.75, coax=True),
    lay("8", "Складной трилистник", 300e3, "трилистник на шарнире: в шаге сложен вдоль спины", fold=True, fold_w=0.70),
    lay("9", "Розетка, свои ЭБ", 300e3, "как 4, но у каждого сота свой малый энергоблок", dist=True),
    lay("10", "Трилистник лёгкий 200 кВт", 200e3, "трилистник с меньшим энергоблоком: легче, ниже запас"),
]
res = {}
for v in VARS:
    c = v.check(); res[v.key] = dict(name=v.name, note=v.note, **c)
    e = c["Земля +15°C"]
    print(f"{v.key:>2} {v.name:26s} кит {c['mass_kit']:4.1f} кг, всё {c['mass_all']:4.1f} | ш {c['width']:.2f} (слож {c['fold_w']:.2f}) внутр.край {c['inner']:+.2f}"
          f" | Т/В {e['tw']:.2f} +капс {e['boost_tw']:.2f} +форс {e['force_tw']:.2f} | вис {e['hover_kw']:3.0f} кВт {e['hours']:.1f} ч | поток {c['downwash']:3.0f} м/с")
    print(f"     vmax {c['vmax']:4.1f}/{c['vmax_boost']:4.1f} м/с | крен15 {c['t15']:.2f} с | отказ сота: плечо {c['mod_fail']:.2f}/{c['mod_fail_f']:.2f}"
          f" | плечо целиком {c['arm_fail']:.2f} | ЭБ {c['pu_fail']:.2f}/{c['pu_fail_f']:.2f} | форсаж {c['force_s']:.0f} с")
    print("     " + " | ".join(f"{k} {x['tw']:.2f}{'' if x.get('ok', True) else '✗'}" for k, x in c.items() if isinstance(x, dict) and k not in ("Земля +15°C",)))
def report(vs, fname):
    out = {}
    for v in vs:
        c = v.check(); out[v.key] = dict(name=v.name, note=v.note, **c)
        e = c["Земля +15°C"]
        print(f"{v.key:>3} {v.name:26s} кит {c['mass_kit']:4.1f} всё {c['mass_all']:5.1f} | ш {c['width']:.2f} | Т/В {e['tw']:.2f}/{e['boost_tw']:.2f}/{e['force_tw']:.2f}"
              f" вис {e['hours']:.1f} ч | +50 {c['Земля +50°C']['tw']:.2f} 3км {c['Земля 3 км']['tw']:.2f} | крен {c['t15']:.2f} | сот {c['mod_fail']:.2f} плечо {c['arm_fail']:.2f} ЭБ {c['pu_fail']:.2f}/{c['pu_fail_f']:.2f}")
    json.dump(out, open(os.path.join(os.path.dirname(__file__), fname), "w", encoding="utf-8"), ensure_ascii=False, indent=1)

print("---- эскизы ----")
SK = []
for k, S in SKETCH.items():
    SK.append(V("S" + k, S["name"], len(S["cells"]), S["s"], [abs(c[0]) for c in S["cells"]], S["cells"][0][2], 300e3, S["pod"][0], S["note"],
                fm=0.75 if S.get("coax") else 0.65, coax=S.get("coax", False), dist=S.get("dist", False), fold=S.get("fold", False),
                fold_w=0.70 if S.get("fold") else None, extra=S["extra"]))
report(SK, "jetpack_sketch.json")
json.dump(res, open(os.path.join(os.path.dirname(__file__), "jetpack_modular.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
