# «Соты-блоки»: ten technical variants checked over the flight modes (the user, 2026-10-16: «технические вариации сотового
# варианта с разными проверками режимов, плюс температурные режимы и учёт УВТ-сопел»). Momentum theory for the cells, the
# power unit's limits by the air's heat, the energy by the air's oxygen, the boost's thrust vector control (УВТ).
# Run: python jetpack_blocks_check.py  -> the table on stdout and renders data in jetpack_blocks.json (the mock-ups' captions)
import math, json, os
G = 9.81; BODY = 104.0; FUEL = 20.0; MH = 216e6; H2AIR = 120e6
# the places: g, the air's density, its temperature (K), oxygen in it
ENV = {"Земля +15°C": (9.81, 1.225, 288, True), "Земля +50°C": (9.81, 1.093, 323, True), "Земля −40°C": (9.81, 1.514, 233, True),
       "Земля 3 км": (9.81, 0.909, 269, True), "Титан": (1.35, 5.4, 94, False), "Венера 50 км": (8.87, 1.59, 350, False),
       "Венера 42 км": (8.87, 2.8, 400, False)}

def power_share(T):                       # the power unit: full to 320 K of air, half at 400 K, none at 460 K (its cooling)
    if T <= 320: return 1.0
    if T <= 400: return 1.0 - 0.5 * (T - 320) / 80
    return max(0.0, 0.5 * (460 - T) / 60)

def energy(o2):                           # J/kg of metallic hydrogen through the motors
    return ((0.60 * H2AIR if o2 else 0.0) + 0.30 * MH) * 0.95

class V:
    def __init__(s, key, name, blocks, R, x, z, gim, gim_side, P_block, fm, boost, tvc, extra_mass, sail, note,
                 halves=True, hands=0.0, cold_heat=3e3, pods_own=False):
        s.__dict__.update(locals()); del s.__dict__["s"]
        s.A_block = 0.60 * 2.598 * R * R if blocks > 0 else 0.0
        s.A = s.A_block * blocks + hands
        s.mass_pack = 22 + blocks * (6 + 55 * s.A_block) + 2 * 2.5 + extra_mass   # core + blocks (frame + cells by area) + boost
        s.m = BODY + s.mass_pack + FUEL

    def thrust(s, rho, share):           # all blocks at full power, N
        if rho <= 0: return 0.0
        per = (s.fm * s.P_block * share * math.sqrt(2 * rho * s.A_block)) ** (2 / 3)
        return per * s.blocks + (s.hands and (s.fm * 15e3 * share * math.sqrt(2 * rho * s.hands)) ** (2 / 3) or 0)

    def hover_power(s, rho, g):
        W = s.m * g
        return W ** 1.5 / (s.fm * math.sqrt(2 * rho * s.A))

    def check(s):
        out = {}
        for env, (g, rho, T, o2) in ENV.items():
            sh = power_share(T)
            Tmax = s.thrust(rho, sh); W = s.m * g
            Ph = s.hover_power(rho, g) + (s.cold_heat if T < 150 else 0.0)
            Pmax = s.P_block * s.blocks * sh + (15e3 if s.hands else 0)
            hover_ok = Ph <= Pmax
            hours = FUEL * energy(o2) / Ph / 3600 if hover_ok else 0.0
            out[env] = dict(tw=Tmax / W, boost_tw=(Tmax + 2 * s.boost) / W, hover_kw=Ph / 1e3, hours=hours, ok=hover_ok, share=sh)
        g, rho = 9.81, 1.225; W = s.m * g
        out["downwash"] = math.sqrt(W / (2 * rho * s.A))
        # the top speed at the gimbal's full tilt (Earth): cells' ram drag sqrt(rho*A*T) per block + the body's 0.8 m2
        th = math.radians(s.gim); T = W / math.cos(th); mdot = s.blocks * math.sqrt(rho * s.A_block * T / max(1, s.blocks))
        Fx = T * math.sin(th); a = 0.5 * rho * (0.8 + s.sail)
        out["vmax"] = (-mdot + math.sqrt(mdot * mdot + 4 * a * Fx)) / (2 * a)
        # the roll: the differential thrust (within 0.2 W either way) at the blocks' arm, the side gimbal, the boost's УВТ;
        # her roll inertia (body with arms ~4 kg m2, the blocks at their arm, the core) - the time to roll 15 deg after the
        # cells' 0.15 s answer
        Mc = 0.2 * W * abs(s.x) * (1 if s.blocks >= 2 else 0.3) + W * 0.45 * math.sin(math.radians(s.gim_side)) * 0.5
        Mc += 2 * s.boost * math.sin(math.radians(s.tvc)) * (abs(s.x) + 0.1)
        I = 4.0 + s.blocks * (6 + 55 * s.A_block) * s.x ** 2 + 22 * 0.1 ** 2 + (2 * 3.0 * (abs(s.x) + 0.1) ** 2 if s.pods_own else 0)
        out["t15"] = 0.15 + math.sqrt(2 * math.radians(15) / (Mc / I))
        # a 10 m/s side gust on her and the pack (sail ~0.7 m2 + the blocks' edges): the drift it pushes against the
        # side acceleration the pack can give (the side gimbal and leaning on it, to 25 deg)
        a_g = 0.5 * rho * 100 * 1.1 * (0.7 + s.sail) / s.m
        out["gust_margin"] = G * math.tan(math.radians(min(25, s.gim_side + 10))) / a_g
        # the failures (Earth): one power unit (each block on its other bus: half power), one whole block
        Th = s.thrust(rho, 0.5) if s.halves else (s.thrust(rho, 1.0) * (s.blocks - 1) / s.blocks)
        out["pu_fail_tw"] = (Th + 2 * s.boost) / W
        if s.blocks >= 4: out["block_fail"] = "держит (диагональ)"
        elif s.blocks == 1: out["block_fail"] = "катастрофа"
        else:
            # the other block at +x, the boosts: the failed side's at -x balances it - the boost at least W/2 there
            need = W / 2
            have = s.boost if s.pods_own else 0.0          # the pods on the lost block go with it
            out["block_fail"] = "держит" if have >= need else ("на дожиге с УВТ: снижение %.1f м/с²" % ((W - 2 * have) / s.m) if have > 0 else "катастрофа")
        out["width"] = 2 * (abs(s.x) + s.R) if s.blocks else 0.9
        out["mass"] = s.mass_pack
        return out

VARS = [
    V("1", "Б-19 базовый", 2, 0.24, 0.46, 1.42, 35, 15, 150e3, 0.65, 600, 0, 0, 0.10, "капсулы дожига на блоках"),
    V("2", "Б-37 крупный", 2, 0.32, 0.52, 1.42, 35, 15, 150e3, 0.65, 600, 0, 4, 0.13, "больше площадь, меньше мощность"),
    V("3", "Б-19 над плечами", 2, 0.24, 0.42, 1.66, 35, 15, 150e3, 0.65, 600, 0, 1, 0.10, "руки в чистом воздухе"),
    V("4", "Б-рейки", 2, 0.25, 0.40, 1.28, 30, 20, 150e3, 0.65, 600, 0, 2, 0.20, "узкие продольные блоки у боков"),
    V("5", "Б-4 четыре блока", 4, 0.17, 0.40, 1.24, 30, 12, 75e3, 0.65, 600, 0, 4, 0.10, "плечи + бёдра, диагональный отказ"),
    V("6", "Б-19 + капсулы УВТ 1 кН", 2, 0.24, 0.46, 1.42, 35, 15, 150e3, 0.65, 1000, 20, 6, 0.12, "капсулы «Наплечного» на своих балках, УВТ ±20°", pods_own=True),
    V("7", "Б-моно спинной", 1, 0.42, 0.0, 1.35, 25, 10, 300e3, 0.65, 600, 0, 0, 0.12, "один блок за спиной"),
    V("8", "Б-19 соосный", 2, 0.24, 0.46, 1.42, 35, 15, 150e3, 0.75, 600, 0, 5, 0.12, "два слоя роторов, FM 0,75"),
    V("9", "Б-37 + капсулы УВТ 1 кН", 2, 0.32, 0.52, 1.42, 35, 15, 150e3, 0.65, 1000, 20, 9, 0.15, "крупные блоки + капсулы на балках", pods_own=True),
    V("10", "Б-19 + кисти", 2, 0.24, 0.46, 1.42, 35, 15, 150e3, 0.65, 600, 0, 4, 0.12, "+2 малых блока на руках", hands=0.025),
]
res = {}
for v in VARS:
    c = v.check(); res[v.key] = dict(name=v.name, note=v.note, **{k: c[k] for k in c})
    e = c["Земля +15°C"]
    print(f"{v.key:>2} {v.name:18s} {v.mass_pack:4.0f}кг ш{c['width']:.2f} | Земля: Т/В {e['tw']:.2f} (+дожиг {e['boost_tw']:.2f}) висение {e['hover_kw']:3.0f} кВт {e['hours']:.1f} ч"
          f" | поток {c['downwash']:3.0f} м/с vmax {c['vmax']:4.1f} | порыв запас ×{c['gust_margin']:.1f} крен 15° за {c['t15']:.2f} с | отказ ЭБ Т/В {c['pu_fail_tw']:.2f} | блок: {c['block_fail']}")
    for env in ("Земля +50°C", "Земля −40°C", "Земля 3 км", "Титан", "Венера 50 км", "Венера 42 км"):
        x = c[env]
        print(f"      {env:13s} Т/В {x['tw']:5.2f} +дожиг {x['boost_tw']:5.2f}  висение {x['hover_kw']:4.0f} кВт {'%.1f ч' % x['hours'] if x['ok'] else 'НЕТ'}  мощн. {100 * x['share']:.0f}%")
json.dump(res, open(os.path.join(os.path.dirname(__file__), "jetpack_blocks.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
