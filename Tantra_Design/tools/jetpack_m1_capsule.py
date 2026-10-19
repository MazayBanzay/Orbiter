# «Наплечный» M1: the twin-chamber capsule with the air «форсаж», its feed line and the boom (the user, 2026-10-17:
# «двухкамерные капсулы могут и форсаж иметь - просчитать теплоустойчивость материала и размер; толщина штанги только
# для прочности - водород не пар, включи математику»).
# Run: python jetpack_m1_capsule.py -> the numbers; jetpack_m1_capsule.json for the sketches' captions
import math, json, os
R_H2, GAM = 4124.0, 1.40                      # J/kg/K, the hot H2's gamma (a fair mean over 1000..4000 K)
CP = GAM * R_H2 / (GAM - 1)                   # ~14.4 kJ/kg/K
GAMMA_F = math.sqrt(GAM) * (2 / (GAM + 1)) ** ((GAM + 1) / (2 * (GAM - 1)))
POD_W, POD_EXIT_O, POD_EXIT_I = 0.094, 0.8 * 0.047 * 2, 0.38 * 0.047 * 2   # the capsule's width; its annular exit, outer/inner diameter

def chamber(F, ve, p0):
    """one chamber: the stagnation temperature for the exhaust ve at the nozzle's expansion, the throat, the flow"""
    mdot = F / ve
    T0 = (ve / 0.93) ** 2 / (2 * CP)            # 0.93: what an expansion ratio ~37 leaves of the full enthalpy drop
    cstar = math.sqrt(R_H2 * T0) / GAMMA_F
    At = mdot * cstar / p0
    return dict(F=F, ve=ve, mdot=mdot, T0=T0, cstar=cstar, At=At, dt=math.sqrt(4 * At / math.pi), p0=p0)

def wall(ch, Tw=None):
    """the heat into the wall at the throat (Bartz, simplified for H2) and what the feed takes away as coolant"""
    # Bartz: h = 0.026 / dt^0.2 * (mu^0.2 cp / Pr^0.6) * (p0 / c*)^0.8 ; H2 at ~2500 K film: mu 6e-5 Pa s, Pr 0.7
    mu, Pr = 6e-5, 0.7
    Tw = Tw or min(1500.0, 0.85 * ch["T0"])     # the regeneratively cooled wall: ~1500 K, or under the gas if the gas is colder
    h = 0.026 / ch["dt"] ** 0.2 * (mu ** 0.2 * CP / Pr ** 0.6) * (ch["p0"] / ch["cstar"]) ** 0.8
    Taw = Tw + 0.9 * (ch["T0"] - Tw)            # the adiabatic wall temperature (recovery 0.9)
    q = h * (Taw - Tw)
    A_hot = math.pi * ch["dt"] * 4.5 * ch["dt"] + math.pi * (2.4 * ch["dt"]) * 3.0 * ch["dt"]   # the throat and convergent, the chamber
    Q = q * A_hot * 0.55                        # the flux falls off away from the throat
    dT_cool = Q / (ch["mdot"] * CP)             # the feed hydrogen's rise as it cools the chamber
    return dict(h=h, q=q, Q=Q, dT_cool=dT_cool, Taw=Taw)

out = {}
# ---- the feed line: metallic hydrogen delivered as a dense metastable slurry, ~1100 kg/m3 ----
rho_f, v_line, p_feed = 1100.0, 4.0, 12e6
for tag, mdot in (("номинал", 0.05), ("форсаж", 0.20)):
    A = mdot / (rho_f * v_line); d = math.sqrt(4 * A / math.pi)
    out["line_" + tag] = dict(mdot=mdot, d_mm=d * 1000)
d_line = out["line_форсаж"]["d_mm"] / 1000
t_line = p_feed * (d_line / 2) / (500e6 / 2)   # a titanium alloy line, the allowable half its strength
out["line_wall_mm"] = t_line * 1000
# ---- the boom: a cantilever 0.35 m from the hinge to the capsule; the capsule's «форсаж» 1 kN, its gimbal's side push 0.34 kN,
# the capsule's own 1.5 kg at 4 g; Ti-6Al-4V (900 MPa) or a carbon composite; safety 2 ----
L = 0.35; F_ax = 1000.0; F_side = 1000 * math.sin(math.radians(20)); F_in = 1.5 * 9.81 * 4
M = math.hypot(F_ax + F_in, F_side) * L
sig = 900e6 / 2
for tag, (D, t) in (("круглая 40 мм", (0.040, 0.0)), ("коробчатая 60×40", (0.0, 0.0))):
    pass
D = 0.040; t_req = M / (sig * math.pi * (D / 2) ** 2)          # a thin tube: Z = pi r^2 t
b, hh = 0.040, 0.060; t_box = M / (sig * hh * (b + hh / 3))    # a thin box section (b wide, h deep): Z = t h (b + h/3)
out["boom"] = dict(M=M, tube40_t_mm=t_req * 1000, box_t_mm=t_box * 1000, M1_section="M1: ~57×74 мм")
m_tube = 4500 * math.pi * D * max(t_req, 0.0008) * L
out["boom"]["mass_tube_kg"] = m_tube
# what really sets the wall: stiffness (the capsule must not wave on the gimbal's push) and a knock (min gauge 1 mm)
E_ti = 110e9; tb = 0.001
I_box = tb * hh ** 2 * (3 * b + hh) / 6
out["boom"]["defl_mm"] = (F_ax + F_in) * L ** 3 / (3 * E_ti * I_box) * 1000
out["boom"]["box_mass_kg"] = 4500 * 2 * (b + hh) * tb * L
out["boom"]["f_hz"] = (1 / (2 * math.pi)) * math.sqrt(3 * E_ti * I_box / L ** 3 / (1.5 + 0.24 * 4500 * 2 * (b + hh) * tb * L))
# ---- the twin-chamber capsule: two chambers of 250 N (10 km/s); the «форсаж» 500 N each at 5 km/s (the same power) ----
nom = chamber(250.0, 10000.0, 3.0e6)
fors = chamber(500.0, 5000.0, 3.0e6)
fors["p0"] = fors["mdot"] * fors["cstar"] / nom["At"]          # the same throat: the chamber pressure rises instead
fors["At"] = nom["At"]; fors["dt"] = nom["dt"]
for k, ch in (("nominal", nom), ("forsazh", fors)):
    ch["P_jet"] = 0.5 * ch["F"] * ch["ve"]
    ch.update(wall(ch))
    out[k] = ch
# the exit: each chamber gets half the annulus
A_exit = math.pi / 4 * (POD_EXIT_O ** 2 - POD_EXIT_I ** 2) / 2
out["eps"] = A_exit / nom["At"]
out["chamber_d_mm"] = 2.4 * nom["dt"] * 1000; out["chamber_len_mm"] = 3.0 * nom["dt"] * 1000
out["pair_fits"] = 2 * (2.4 * nom["dt"] + 0.012) < POD_W
# the materials: the wall temperature they allow, and where each goes
out["materials"] = [
    ("горло и камера", "рений с иридиевым покрытием, внутри — регенеративное охлаждение подачей", "держит 2200–2400 K; стенка ~1500 K"),
    ("вставка горла", "кубический нитрид бора (боразон) поверх рения", "в водороде (без кислорода) до ~1600 K, лучше всех держит эрозию"),
    ("центральный конус", "HfB₂–SiC (сверхвысокотемпературная керамика), вершина с боразоном; охлаждается отбором водорода", "в воздухе (догорание) поверхность ~1800 K: HfB₂ держит до ~2300 K"),
    ("корпус капсулы", "титан под теплоизоляцией (аэрогель на основе нитрида бора)", "снаружи ~350–400 K — не греет скафандр"),
]
for k in ("nominal", "forsazh"):
    ch = out[k]
    print(f"{k:8s}: {ch['F']:.0f} Н, {ch['ve'] / 1000:.0f} км/с, расход {ch['mdot'] * 1000:.0f} г/с, T0 {ch['T0']:.0f} K, p0 {ch['p0'] / 1e6:.1f} МПа, горло {ch['dt'] * 1000:.1f} мм,"
          f" мощность струи {ch['P_jet'] / 1e6:.2f} МВт | тепло в горло {ch['q'] / 1e6:.0f} МВт/м², всего {ch['Q'] / 1e3:.0f} кВт, подача греется на {ch['dT_cool']:.0f} K")
print(f"камера Ø{out['chamber_d_mm']:.0f}×{out['chamber_len_mm']:.0f} мм, две в капсуле Ø94: {'помещаются' if out['pair_fits'] else 'НЕ помещаются'}; степень расширения на полкольца {out['eps']:.0f}")
print(f"топливопровод: номинал Ø{out['line_номинал']['d_mm']:.1f} мм, форсаж Ø{out['line_форсаж']['d_mm']:.1f} мм, стенка {out['line_wall_mm']:.2f} мм при 12 МПа")
print(f"штанга: момент у шарнира {M:.0f} Н·м -> труба Ø40 стенка {t_req * 1000:.2f} мм или короб 40×60 стенка {t_box * 1000:.2f} мм; по жёсткости и удару короб 40×60 стенка 1 мм:"
      f" прогиб {out['boom']['defl_mm']:.1f} мм под 1 кН, масса {out['boom']['box_mass_kg']:.2f} кг, собственная частота с капсулой {out['boom']['f_hz']:.0f} Гц")
json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "jetpack_m1_capsule.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
