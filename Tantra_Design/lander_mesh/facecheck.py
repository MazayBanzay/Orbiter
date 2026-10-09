# -*- coding: utf-8 -*-
"""Проверка подвижных частей ПО ГРАНЯМ: пересечение треугольников (ребро–грань в обе стороны) и минимальный зазор
(вершина–грань в обе стороны) во всех режимах и в крайних положениях. Запуск: python facecheck.py [gen_dir] [out_json]"""
import sys, os, io, json, runpy, math
import numpy as np

GEN = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
OUTJ = sys.argv[2] if len(sys.argv) > 2 else os.path.join(GEN, "out", "face_check.json")
cwd = os.getcwd(); os.chdir(GEN); so = sys.stdout; sys.stdout = io.StringIO()
NS = runpy.run_path(os.path.join(GEN, "gen_lander.py"), run_name="gen")
sys.stdout = so; os.chdir(cwd)
GROUPS, ANIM, MODES, rotmat = NS["GROUPS"], NS["ANIM"], NS["MODES"], NS["rotmat"]
OPEN = NS["OPEN"]


def tris_of(names):
    T = []
    for n in names:
        g = GROUPS[n]
        if g.t:
            V = np.array(g.v); T.append(V[np.array(g.t)])
    return np.concatenate(T) if T else np.zeros((0, 3, 3))


def pose(T, e, deg):
    if not deg:
        return T
    R = rotmat(e["axis"], deg); P = np.array(e["pivot"], float)
    return (T - P) @ R.T + P


def seg_tri(p0, p1, A, B, C):
    """Пересечение отрезков p0–p1 с треугольниками ABC (векторно, попарно). Возвращает маску и точки."""
    e1, e2 = B - A, C - A; d = p1 - p0
    h = np.cross(d, e2); a = np.einsum("ij,ij->i", e1, h)
    ok = np.abs(a) > 1e-12; f = np.where(ok, 1.0 / np.where(ok, a, 1), 0)
    s = p0 - A; u = f * np.einsum("ij,ij->i", s, h)
    q = np.cross(s, e1); v = f * np.einsum("ij,ij->i", d, q); t = f * np.einsum("ij,ij->i", e2, q)
    m = ok & (u >= 1e-9) & (v >= 1e-9) & (u + v <= 1 - 1e-9) & (t > 1e-9) & (t < 1 - 1e-9)
    return m, p0 + t[:, None] * d


def pt_tri(P, A, B, C):
    """Расстояние точка–треугольник (Ericson), векторно."""
    ab, ac, ap = B - A, C - A, P - A
    d1 = np.einsum("ij,ij->i", ab, ap); d2 = np.einsum("ij,ij->i", ac, ap)
    bp = P - B; d3 = np.einsum("ij,ij->i", ab, bp); d4 = np.einsum("ij,ij->i", ac, bp)
    cp = P - C; d5 = np.einsum("ij,ij->i", ab, cp); d6 = np.einsum("ij,ij->i", ac, cp)
    va = d3 * d6 - d5 * d4; vb = d5 * d2 - d1 * d6; vc = d1 * d4 - d3 * d2
    den = np.where(np.abs(va + vb + vc) < 1e-18, 1e-18, va + vb + vc)
    v = vb / den; w = vc / den
    Q = A + ab * v[:, None] + ac * w[:, None]
    # рёбра и вершины — общий случай через проекции на отрезки
    def seg(P, X, Y):
        d = Y - X; t = np.clip(np.einsum("ij,ij->i", P - X, d) / np.maximum(np.einsum("ij,ij->i", d, d), 1e-18), 0, 1)
        return X + t[:, None] * d
    inside = (va >= 0) & (vb >= 0) & (vc >= 0)
    cands = [seg(P, A, B), seg(P, B, C), seg(P, C, A)]
    dist = np.min([np.linalg.norm(P - c, axis=1) for c in cands], axis=0)
    di = np.linalg.norm(P - Q, axis=1)
    return np.where(inside, np.minimum(di, dist), dist)


def pairs(TA, TB, pad):
    loA, hiA = TA.min(1) - pad, TA.max(1) + pad; loB, hiB = TB.min(1), TB.max(1)
    gl, gh = loA.min(0), hiA.max(0)
    selB = np.where(np.all(hiB >= gl, 1) & np.all(loB <= gh, 1))[0]
    out_i, out_j = [], []
    for s in range(0, len(TA), 400):
        la, ha = loA[s:s + 400], hiA[s:s + 400]
        m = np.all(ha[:, None, :] >= loB[selB][None], 2) & np.all(la[:, None, :] <= hiB[selB][None], 2)
        i, j = np.nonzero(m); out_i.append(i + s); out_j.append(selB[j])
    return (np.concatenate(out_i), np.concatenate(out_j)) if out_i else (np.zeros(0, int), np.zeros(0, int))


def in_open(p):
    return p[0] < -8.85 or any(a - 0.05 <= p[0] <= b + 0.05 and c - 0.05 <= p[2] <= d + 0.05 for a, b, c, d in OPEN)


def check(TA, TB, pad=0.10):
    i, j = pairs(TA, TB, pad)
    if len(i) == 0:
        return 0, 0, None
    A, B = TA[i], TB[j]
    hits = []
    for X, Y in ((A, B), (B, A)):
        for k in range(3):
            m, P = seg_tri(X[:, k], X[:, (k + 1) % 3], Y[:, 0], Y[:, 1], Y[:, 2])
            hits.append(P[m])
    H = np.concatenate(hits)
    n_out = int(sum(1 for p in H if not in_open(p)))
    dmin = 1e9
    for X, Y in ((A, B), (B, A)):
        for k in range(3):
            dmin = min(dmin, float(pt_tri(X[:, k], Y[:, 0], Y[:, 1], Y[:, 2]).min()))
    return len(H), n_out, dmin


ANIM_SET = set(g for e in ANIM for g in e["groups"])
HULL = ["korpus", "dnische_tzp", "nos_poristy", "krylo_L", "krylo_R"]
STAT = tris_of(HULL)
BASE = [tris_of(e["groups"]) for e in ANIM]
# положения: режимы + крайние положения пределов (остальные части — в режиме висения/полёта)
POS = [(m, {i: e["modes"].get(m, 0) for i, e in enumerate(ANIM)}) for m in MODES]
for i, e in enumerate(ANIM):
    for lim in e["limits"]:
        if lim not in e["modes"].values():
            base = min(MODES, key=lambda m: abs(e["modes"].get(m, 0) - lim))   # режим, в котором часть работает ближе всего к пределу
            d = {k: ee["modes"].get(base, 0) for k, ee in enumerate(ANIM)}; d[i] = lim
            POS.append(("%s, %s %+g°" % (base, e["note"], lim), d))
rows = []; prs = []
for name, ang in POS:
    TT = [pose(BASE[i], e, ang[i]) for i, e in enumerate(ANIM)]
    for i, e in enumerate(ANIM):
        n, nout, d = check(TT[i], STAT)
        rows.append({"pos": name, "part": e["note"], "deg": ang[i], "x_all": n, "x_outside_openings": nout,
                     "gap_to_hull_m": None if d is None else round(d, 3)})
    for i in range(len(ANIM)):
        for j in range(i + 1, len(ANIM)):
            n, nout, d = check(TT[i], TT[j], pad=0.30)
            if d is not None and (d < 0.30 or n):
                prs.append({"pos": name, "a": ANIM[i]["note"], "b": ANIM[j]["note"], "x": n, "gap_m": round(d, 3)})
res = {"method": "по граням: пересечение = отрезок-ребро одного треугольника пробивает другой (в обе стороны); зазор = min расстояние вершина–грань в обе стороны (ребро–ребро не считается — зазор может быть завышен не более чем на половину длины ребра у почти параллельных рёбер); корпус = korpus+dnische+nos+krylo; «вне проёмов» — точки пересечения не в контуре ниш/проёмов/среза кормы x < −8,85",
       "rows": rows, "pairs": prs}
json.dump(res, open(OUTJ, "w", encoding="utf-8"), ensure_ascii=False, indent=0)
bad = [r for r in rows if r["x_outside_openings"]]
print("пересечения вне проёмов:", len(bad))
for r in bad: print("  ", r)
for p in prs: print(" pair", p)
