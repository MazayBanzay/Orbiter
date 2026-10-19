# «Наплечный» M2 with four capsules and no heat shields (the user, 2026-10-17: «без экранов - выход сопла, смена
# расположения, или 4 УВТ типа стрекоза / Х-образные: 2 вектор и 2 вертикаль-тангаж-баланс? 7 примеров»).
# The upper pair is M1's own УВТ, untouched: 2 x 500 N at (+-0.597, 0.175, 1.471), the exit 0.118 m below the pod's
# centre, pitch gimbal +-20 deg - the vector. The lower pair: the same capsules on booms of their own from the lumbar
# block, in seven places - the lift, the pitch trim, the balance. The heat (tools/jetpack_m2_heat.py's model: the plume,
# the body's own shadow, no shields) is judged against M1 itself: what M1's two capsules already put on her legs is the
# accepted level; a layout passes a body point if it is within 1.15 of that, or under 4 kW/m2 anyway.
# The hover trim: the throttles of the two pairs (their sum carrying her weight, 163 kg), the upper pair's gimbal
# (+-20) and the lower pair's (where it has one) must zero the pitch moment about the CM, (0, 0.09, 1.07), leaving a
# forward/aft residual she can lean against (< 150 N).
# Run: python jetpack_m2_quad.py -> the table; jetpack_m2_quad.json; the layouts for blender/jetpack_m2quad.py
import math, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jetpack_m2_heat as H
CM = (0.0, 0.09, 1.07); W = 163.0 * 9.81; POD_F = 500.0; POD_HALF = 0.118
UPPER = [(0.597, 0.175, 1.471), (-0.597, 0.175, 1.471)]
BODY = H.BODY + [H.HAND_STAND]

def rot_out_aft(sg, out, aft):
    """the pod's axis: down, canted out (sign sg) and aft - the jet's direction; also the exit below the centre"""
    return H.jet_dir(sg, out, aft)

def pod_nozzle(pos, out, aft, F=POD_F):
    sg = 1 if pos[0] >= 0 else -1
    d = rot_out_aft(sg, out, aft)
    return dict(exit=H.add(pos, H.mul(d, POD_HALF)), dir=d, F=F, pos=pos, out=out, aft=aft, shield=None)

def layout(key, name, lower, note, gim_l=0.0, out=0.0, aft=0.0, boom="kidney"):
    return dict(key=key, name=name, lower=lower, note=note, gim_l=gim_l, out=out, aft=aft, boom=boom)

LAYOUTS = [
    layout("0", "М1 как есть (эталон)", [], "две капсулы на плечах; то, что уже принято - эталон нагрева"),
    layout("1", "Тандем на штанге", [(0.85, 0.175, 1.471), (-0.85, 0.175, 1.471)],
           "штанги удлинены, вторая капсула снаружи первой на той же оси; струи дальше от ног, чем у М1; кардан у всех четырёх", gim_l=20, boom="extend"),
    layout("2", "Стрекоза высокая, вертикальная", [(0.65, 0.45, 1.471), (-0.65, 0.45, 1.471)],
           "вторая пара на штангах назад от крышки, на высоте плеч; капсулы вертикальные - линия тяги далеко за ЦМ", gim_l=20, boom="lid"),
    layout("3", "Стрекоза высокая, маршевая", [(0.65, 0.45, 1.471), (-0.65, 0.45, 1.471)],
           "то же, капсулы наклонены на 38° назад - линия тяги через ЦМ; в висении задняя пара на малом газу, в крейсере - маршевая", gim_l=10, aft=38, boom="lid"),
    layout("4", "Х в плане, высокий", [(0.78, 0.40, 1.471), (-0.78, 0.40, 1.471)],
           "задняя пара шире и дальше, с отвалом 15° наружу и 35° назад: Х из четырёх струй сверху, все далеко от тела", gim_l=10, out=15, aft=35, boom="lid"),
    layout("5", "Низ позади, маршевый", [(0.45, 0.55, 0.92), (-0.45, 0.55, 0.92)],
           "нижняя пара за бёдрами с наклоном 60° назад - маршевая, струя уходит от ног; висение только на верхней паре", gim_l=10, aft=60, boom="kidney"),
    layout("6", "Стрекоза: низ у бёдер", [(0.62, 0.11, 1.00), (-0.62, 0.11, 1.00)],
           "буквально по схеме: нижняя пара вбок от боковых панелей в плоскости ЦМ, кардан у всех; струи рядом с ногами", gim_l=20, out=10),
    layout("7", "Х в плане, низкий", [(0.60, -0.06, 1.00), (-0.60, -0.06, 1.00)],
           "нижняя пара вперёд от плоскости ЦМ: пары по разные стороны ЦМ, тангаж чистым дросселем; струи у рук и ног", out=10),
]

def nozzles_of(L):
    up = [pod_nozzle(p, 0, 0) for p in UPPER]
    lo = [pod_nozzle(p, L["out"], L["aft"]) for p in L["lower"]]
    return up, lo

def trim(up, lo, gim_l):
    """hover: the pairs' throttles and gimbals that carry the weight and zero the pitch moment; the forward residual"""
    def force(nz, t, phi):
        d = nz["dir"]; a = math.radians(phi)
        dj = H.unit((d[0], d[1] * math.cos(a) - d[2] * math.sin(a), d[1] * math.sin(a) + d[2] * math.cos(a)))   # the gimbal about x
        return H.mul(dj, -nz["F"] * t)
    best = None
    phis_l = range(-int(gim_l), int(gim_l) + 1, 4) if gim_l else [0]
    for pu in range(-20, 21, 2):
        for pl in phis_l:
            for tu in [i / 20 for i in range(0, 21)]:
                # the lower throttle that makes the lift the weight (or none without a lower pair)
                Fu = [force(nz, tu, pu) for nz in up]; lift_u = sum(f[2] for f in Fu)
                if lo:
                    unit_l = [force(nz, 1.0, pl) for nz in lo]; lift_1 = sum(f[2] for f in unit_l)
                    tl = (W - lift_u) / lift_1 if lift_1 > 1 else 9
                    if not 0 <= tl <= 1: continue
                    Fl = [H.mul(f, tl) for f in unit_l]
                else:
                    if abs(lift_u - W) > 0.03 * W: continue
                    tl, Fl = 0.0, []
                M = sum(H.cross(H.sub(nz["exit"], CM), f)[0] for nz, f in zip(up + lo, Fu + Fl))
                fy = sum(f[1] for f in Fu + Fl)
                score = abs(M) + 0.2 * abs(fy)
                if best is None or score < best[0]: best = (score, M, fy, pu, pl, tu, tl)
    if best is None: return None
    _, M, fy, pu, pl, tu, tl = best
    return dict(M=M, fy=fy, gim_u=pu, gim_l=pl, t_u=tu, t_l=tl, lean=math.degrees(math.atan2(abs(fy), W)), ok=abs(M) < 15 and abs(fy) < 0.21 * W)   # the lean she holds against the residual, <= 12 deg

def analyse(L, base=None):
    up, lo = nozzles_of(L)
    rows = []
    for name, P in BODY:
        q, hit = H.heat_at(P, up + lo, H.PACK_BOXES)
        rows.append([name, q / 1e3, hit])
    tr = trim(up, lo, L["gim_l"])
    out = dict(rows=rows, trim=tr, lift_max=sum(-nz["dir"][2] * nz["F"] for nz in up + lo))
    if base:
        verdicts = []
        for (name, q, hit), (_, qb, _) in zip(rows, base["rows"]):
            v = "ПОПАДАНИЕ" if hit else "ок" if q <= max(4.0, 1.15 * qb) else "ЭКРАН" if q <= 10 else "ОЖОГ"
            verdicts.append(v)
        out["verdicts"] = verdicts
        out["heat_ok"] = all(v == "ок" for v in verdicts)
        out["ratio"] = max(q / max(qb, 4.0) for (_, q, _), (_, qb, _) in zip(rows, base["rows"]))   # against M1, the 4 kW/m2 floor counted as fine
    return out

if __name__ == "__main__":
    res = {}; base = analyse(LAYOUTS[0])
    T_air, E_air = H.plume_T, H.EPS
    H.plume_T = lambda s_: 1000.0 if s_ < 0.3 else max(500.0, 1000.0 - (s_ - 0.3) * 600.0); H.EPS = 0.05
    vac = analyse(LAYOUTS[0]); H.plume_T, H.EPS = T_air, E_air
    print("М1 в вакууме (без догорания): " + " · ".join("%s %.1f" % (r[0], r[1]) for r in vac["rows"]))
    res["0vac"] = dict(name="М1 в вакууме", rows=vac["rows"])
    for L in LAYOUTS:
        A = analyse(L, base if L["key"] != "0" else None); res[L["key"]] = dict(name=L["name"], note=L["note"], **A)
        tr = A["trim"]
        trs = "не висит на Земле" if tr is None else "верх %+d° газ %d %%, низ %+d° газ %d %%, крен корпуса %.0f° %s" % (tr["gim_u"], 100 * tr["t_u"], tr["gim_l"], 100 * tr["t_l"], tr["lean"], "ок" if tr["ok"] else "НЕТ")
        print("%s %-26s подъём %4.0f Н (Т/В Земля %.2f) | тримм: %s | тепло: %s" % (L["key"], L["name"], A["lift_max"], A["lift_max"] / W, trs,
              "эталон" if L["key"] == "0" else ("ок, не хуже М1 (×%.2f)" % A["ratio"] if A["heat_ok"] else "ХУЖЕ М1 (×%.2f)" % A["ratio"])))
        print("     " + " · ".join("%s %.1f%s" % (r[0], r[1], "" if L["key"] == "0" else " " + v) for r, v in zip(A["rows"], A.get("verdicts", [""] * len(A["rows"])))))
    json.dump(res, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "jetpack_m2_quad.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
