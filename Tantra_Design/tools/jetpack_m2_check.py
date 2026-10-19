# «Наплечный» M2 (the user, 2026-10-17: «УВТ универсальный лучше - m2 модель с увеличенными соплами и объёмом бака,
# и вспомогательным соплом с 10-градусным УВТ внизу в основании для горизонтальной скорости (или несколько); геометрия
# есть в УВТ»). The variants are the M1 pack's own parts scaled: the capsules (JetPod) by k, the shell's depth / height
# (the tank: the extra volume all metallic hydrogen, ~1.15 t/m3), the cruise nozzles - JetPod copies at 0.6 scale in the
# base pointing aft (+-10 deg vector). Thrust of a capsule ~ its exit area (k^2); exhaust 10 km/s (5 km/s on the air's
# «форсаж»: twice the thrust on the same power, twice the flow).
# Run: python jetpack_m2_check.py -> the table; jetpack_m2.json for the sketches' captions
import math, json, os
G = {"Земля": 9.81, "Марс": 3.71, "Луна": 1.62, "Титан": 1.35}
RHO = {"Земля": 1.225, "Марс": 0.020, "Луна": 0.0, "Титан": 5.4}
BODY, DRY1, FUEL1, POD1, VE = 104.0, 16.0, 12.0, 500.0, 10000.0
SHELL_V = 0.406 * 0.218 * 0.475 * 0.70                        # the shell's inner volume, m3 (JetShell's box, 70 % of it)
MH = 1150.0                                                     # kg/m3

class M2:
    def __init__(s, key, name, k_pod, k_depth, k_height, n_aux, boom_out, note):
        s.__dict__.update(locals()); del s.__dict__["s"]
        s.fuel = FUEL1 + SHELL_V * (k_depth * k_height - 1) * MH
        s.pod = POD1 * k_pod ** 2
        s.aux = 0.36 * POD1 if n_aux else 0.0                     # a 0.6-scale capsule
        s.dry = DRY1 + 2 * 1.5 * (k_pod ** 3 - 1) + 0.12 * (s.fuel - FUEL1) + 1.0 * n_aux + 0.4 * boom_out * 10
    def row(s):
        o = {}
        for w, g in G.items():
            m0 = BODY + s.dry + s.fuel; m1 = BODY + s.dry
            o[w] = dict(tw=2 * s.pod / (m0 * g), tw_f=4 * s.pod / (m0 * g), hover_min=VE / g * math.log(m0 / m1) / 60)
        # the cruise on Earth: the base nozzles push, the capsules hold her weight upright (no lean: no lift lost)
        F = s.n_aux * s.aux
        v = math.sqrt(2 * F / (RHO["Земля"] * 0.8)) if F else 0.0
        m0 = BODY + s.dry + s.fuel; m1 = BODY + s.dry
        t = VE / 9.81 * math.log(m0 / m1) * 9.81 * m0 / (9.81 * m0 + F) if F else 0   # the flow grows by the cruise nozzles' share
        o["cruise_v"] = v; o["range_km"] = v * t / 1000; o["dv_vac"] = VE * math.log(m0 / m1)
        o.update(fuel=s.fuel, dry=s.dry, pod=s.pod, aux=s.aux, n_aux=s.n_aux, name=s.name, note=s.note, mass=s.dry + s.fuel)
        return o

VARS = [
    M2("M1", "«Наплечный» (как есть)", 1.0, 1.0, 1.0, 0, 0.0, "исходный"),
    M2("a", "М2-а", 1.30, 1.35, 1.0, 1, 0.0, "капсулы ×1,3, бак глубже, одно сопло внизу по центру"),
    M2("b", "М2-б", 1.30, 1.20, 1.20, 2, 0.0, "капсулы ×1,3, бак глубже и ниже, два сопла по углам основания"),
    M2("c", "М2-в", 1.45, 1.50, 1.0, 2, 0.06, "капсулы ×1,45 на удлинённых балках, бак ×1,5, два сопла"),
    M2("d", "М2-г", 1.30, 1.50, 1.20, 2, 0.0, "капсулы ×1,3, бак ×1,8 (глубже и ниже), два сопла"),
]
res = {}
for v in VARS:
    o = v.row(); res[v.key] = o; e = o["Земля"]
    print(f"{v.key:>2} {v.name:24s} ранец {o['mass']:4.1f} кг (МВ {o['fuel']:4.1f}) | капсула {o['pod']:4.0f} Н, сопла {o['n_aux']}x{o['aux']:3.0f} Н"
          f" | Земля Т/В {e['tw']:.2f} (форсаж {e['tw_f']:.2f}) висение {e['hover_min']:.1f} мин | Марс {o['Марс']['hover_min']:.1f} мин, Луна {o['Луна']['hover_min']:.0f} мин"
          f" | крейсер {o['cruise_v']:.0f} м/с, {o['range_km']:.1f} км | dv {o['dv_vac']:.0f} м/с")
json.dump(res, open(os.path.join(os.path.dirname(__file__), "jetpack_m2.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
