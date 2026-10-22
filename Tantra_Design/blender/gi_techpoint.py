# The technical point's light transport for the D3D11 client's real-time radiosity (Enlighten's scheme: the transport between
# surface patches precomputed once, the light itself computed every frame - lights_gi.hlsl). Run in Blender for its BVH ray
# caster:  blender -b --python gi_techpoint.py   (inputs: Meshes\TVehicles\TechPoint.msh and Tantra_Design\base\techpoint_gi.json
# from build_techpoint.py; output: Meshes\TVehicles\TechPoint.gi; log: gi_techpoint.log beside this script).
#   Patches: the surfaces cut into cells (0.5 m in the rooms, 1 m outside, the Moon's ground 0.75 m in the hangar and 2 m in
#     the yard), by 26 normal directions. Each patch: its centre, normal, area, diffuse albedo, its room (0 outside).
#   Patch -> patch: 192 cosine-distributed rays from each patch: the share of its hemisphere each other patch fills (the
#     form factor F_ij: E_i = sum F_ij B_j).
#   Lamp -> patch: the building's lamps' visibility from each patch in their reach (16 shadow rays: to points over a panel,
#     to a spot's or a point's lamp) - the lamps' light itself (on/off, flux, colour) the client computes each frame.
#   Probes: a grid per room (0.5 m, the hangar 0.75 m), 256 rays each over the sphere: which patches it sees in which
#     direction - its ambient cube each frame from the patches' radiosity. A probe inside a solid (back faces seen) or
#     outside the room (the open sky seen) takes the nearest good probe's list.
#   Portals (the airlocks' door leaves, the gate): left out of the BVH; a ray that crosses one gets two entries - the leaf's
#     face (counted while it is shut) and what lies behind (counted while it is open) - weighted by the open fraction the
#     module sends every frame (D3D11Lights SetPortals). Two crossings deep.
import json, math, os, random, struct, time, traceback
from mathutils import Vector
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__)); ORB = os.path.abspath(os.path.join(HERE, "..", ".."))
MSH = os.path.join(ORB, "Meshes", "TVehicles", "TechPoint.msh")
INP = os.path.join(ORB, "Tantra_Design", "base", "techpoint_gi.json")
OUT = os.path.join(ORB, "Meshes", "TVehicles", "TechPoint.gi")
LOG = open(os.path.join(HERE, "gi_techpoint.log"), "w")
def log(*a):
    LOG.write(" ".join(str(x) for x in a) + "\n"); LOG.flush()

R_PATCH, R_PROBE, S_LAMP = 192, 256, 16
random.seed(1977)

try:
    T0 = time.time()
    J = json.load(open(INP))
    MAT = J["materials"]
    # ---- the mesh
    L = open(MSH, encoding="latin-1").read().splitlines()
    i = 0; groups = []
    while i < len(L):
        s = L[i]
        if s.startswith("LABEL"):
            lab = s.split(None, 1)[1]; mi = int(L[i + 1].split()[1]) - 1; nv, nf = map(int, L[i + 3].split()[1:3])
            V = [tuple(map(float, L[i + 4 + k].split()[:3])) for k in range(nv)]
            F = [tuple(map(int, L[i + 4 + nv + k].split())) for k in range(nf)]
            groups.append((lab, mi, V, F)); i += 4 + nv + nf
        elif s.startswith("MATERIALS"): break
        else: i += 1
    door_lab = {g["label"] for g in J["groups"] if g["door"]}
    verts, polys, tri_alb, tri_n, tri_cell = [], [], [], [], []
    GR = J["grids"]
    def in_grid(p, eps=0.0):
        for g in GR:
            if all(g["lo"][k] - eps <= p[k] <= g["hi"][k] + eps for k in range(3)): return g
        return None
    def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
    def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    def unit(a):
        l = math.sqrt(dot(a, a)) or 1.0; return (a[0] / l, a[1] / l, a[2] / l)
    skipped = 0
    for lab, mi, V, F in groups:
        m = MAT[mi]
        if m["glass"] or m["emit"] or lab in door_lab: skipped += len(F); continue
        for a, b, c in F:
            pa, pb, pc = V[a], V[b], V[c]
            n = cross(sub(pb, pa), sub(pc, pa)); ar = 0.5 * math.sqrt(dot(n, n))
            if ar < 1e-7: continue
            k = len(verts); verts += [pa, pb, pc]; polys.append((k, k + 1, k + 2))
            tri_alb.append(m["albedo"]); tri_n.append(unit(n))
            ctr = ((pa[0] + pb[0] + pc[0]) / 3, (pa[1] + pb[1] + pc[1]) / 3, (pa[2] + pb[2] + pc[2]) / 3)
            tri_cell.append(0.5 if in_grid(ctr, 0.3) else 1.0)
    nMesh = len(polys)
    # the Moon's ground: the hangar's floor (0.75 m cells) and the yard round the point (2 m), each cell a quad (+y)
    ground_cell = {}
    def ground_rect(x0, z0, x1, z1, cell, skip):
        nx, nz = max(1, int(round((x1 - x0) / cell))), max(1, int(round((z1 - z0) / cell)))
        cx, cz = (x1 - x0) / nx, (z1 - z0) / nz
        for ix in range(nx):
            for iz in range(nz):
                a0, b0, a1, b1 = x0 + ix * cx, z0 + iz * cz, x0 + (ix + 1) * cx, z0 + (iz + 1) * cz
                mx, mz = 0.5 * (a0 + a1), 0.5 * (b0 + b1)
                if any(f[0] <= mx <= f[2] and f[1] <= mz <= f[3] for f in skip): continue
                k = len(verts); verts.extend([(a0, 0.0, b0), (a0, 0.0, b1), (a1, 0.0, b1), (a1, 0.0, b0)])
                for tri in ((k, k + 1, k + 2), (k, k + 2, k + 3)):
                    ground_cell[len(polys)] = (cell, ix, iz, x0, z0)
                    polys.append(tri); tri_alb.append([J["regolith"]] * 3); tri_n.append((0.0, 1.0, 0.0)); tri_cell.append(-1.0)
    hf = J["hangar_floor"]; ground_rect(hf[0], hf[1], hf[2], hf[3], 0.75, [])
    y = J["yard"]; ground_rect(y[0], y[1], y[2], y[3], 2.0, J["footprints"])
    log("triangles: mesh %d (left out: glass, lamps, screens, door leaves %d), ground %d" % (nMesh, skipped, len(polys) - nMesh))
    bvh = BVHTree.FromPolygons([Vector(v) for v in verts], polys, all_triangles=True, epsilon=0.0)

    # ---- the patches: the triangles' area sampled into (cell, normal direction) keys
    DIRS = []
    for x in (-1, 0, 1):
        for yy in (-1, 0, 1):
            for z in (-1, 0, 1):
                if x or yy or z: DIRS.append(unit((x, yy, z)))
    def bucket(n): return max(range(26), key=lambda k: dot(n, DIRS[k]))
    tri_bucket = [bucket(n) for n in tri_n]
    def key_of(t, p):
        c = tri_cell[t]
        if c < 0:   # a ground quad: its own cell
            cell, ix, iz, x0, z0 = ground_cell[t]; return ("g", cell, ix, iz, x0, z0)
        return (c, tri_bucket[t], math.floor(p[0] / c), math.floor(p[1] / c), math.floor(p[2] / c))
    P = {}          # key -> [area, sum p, sum n, sum albedo, samples[(p, n)]]
    home = []
    for t, (a, b, c) in enumerate(polys):
        pa, pb, pc = verts[a], verts[b], verts[c]
        n = tri_n[t]; ar = 0.5 * math.sqrt(dot(cross(sub(pb, pa), sub(pc, pa)), cross(sub(pb, pa), sub(pc, pa))))
        cell = abs(tri_cell[t]) if tri_cell[t] > 0 else 2.0
        ns = 1 if tri_cell[t] < 0 else max(1, min(400, int(math.ceil(ar / (cell * cell / 6.0)))))
        ctr = ((pa[0] + pb[0] + pc[0]) / 3, (pa[1] + pb[1] + pc[1]) / 3, (pa[2] + pb[2] + pc[2]) / 3)
        home.append(key_of(t, ctr))
        for s in range(ns):
            if ns == 1: q = ctr
            else:
                u, v = random.random(), random.random()
                if u + v > 1: u, v = 1 - u, 1 - v
                q = (pa[0] + (pb[0] - pa[0]) * u + (pc[0] - pa[0]) * v, pa[1] + (pb[1] - pa[1]) * u + (pc[1] - pa[1]) * v, pa[2] + (pb[2] - pa[2]) * u + (pc[2] - pa[2]) * v)
            k = key_of(t, q); w = ar / ns
            e = P.get(k)
            if e is None: e = P[k] = [0.0, [0, 0, 0], [0, 0, 0], [0, 0, 0], []]
            e[0] += w
            for j in range(3): e[1][j] += q[j] * w; e[2][j] += n[j] * w; e[3][j] += tri_alb[t][j] * w
            if len(e[4]) < 64: e[4].append((q, n))
            elif random.random() < 64.0 / (len(e[4]) + 1): e[4][random.randrange(64)] = (q, n)
    keys = list(P.keys()); pid = {k: i for i, k in enumerate(keys)}
    patches = []
    for k in keys:
        a, sp, sn, sa, smp = P[k]
        patches.append({"p": tuple(x / a for x in sp), "n": unit(sn), "alb": tuple(x / a for x in sa), "area": a, "smp": smp, "room": 0})
    tri_home = [pid.get(h, -1) for h in home]
    # the portals' leaves: two patches each (a face towards each side)
    PO = J["portals"]
    leaf = []
    for po in PO:
        ax = po["axis"]; u0, u1 = po["u"]; v0, v1 = po["v"]; c = po["c"]
        pair = []
        for s in (-1, 1):
            n = [0.0, 0.0, 0.0]; n[ax] = float(s)
            cc = [0.0, 0.0, 0.0]; cc[ax] = c + s * po["half"]; cc[1] = 0.5 * (v0 + v1)
            if ax == 2: cc[0] = 0.5 * (u0 + u1)
            else: cc[2] = 0.5 * (u0 + u1)
            smp = []
            for _ in range(32):
                q = list(cc); q[1] = v0 + random.random() * (v1 - v0)
                if ax == 2: q[0] = u0 + random.random() * (u1 - u0)
                else: q[2] = u0 + random.random() * (u1 - u0)
                smp.append((tuple(q), tuple(n)))
            pair.append(len(patches))
            patches.append({"p": tuple(cc), "n": tuple(n), "alb": tuple(po["albedo"]), "area": (u1 - u0) * (v1 - v0), "smp": smp, "room": 0, "leaf": True})
        leaf.append(pair)
    log("patches: %d (+ %d portal leaves) in %.1f s" % (len(patches) - 2 * len(PO), 2 * len(PO), time.time() - T0))
    if len(patches) > 65535: raise RuntimeError("too many patches")

    WIN = J["windows"]
    def seg_box(o, d, tmax, b):
        t0, t1 = 0.0, tmax
        for k in range(3):
            lo, hi = min(b[k], b[k + 3]), max(b[k], b[k + 3])
            if abs(d[k]) < 1e-12:
                if o[k] < lo or o[k] > hi: return False
                continue
            ta, tb = (lo - o[k]) / d[k], (hi - o[k]) / d[k]
            if ta > tb: ta, tb = tb, ta
            t0, t1 = max(t0, ta), min(t1, tb)
            if t0 > t1: return False
        return True
    def crossings(o, d, tmax):
        out = []
        for pi_, po in enumerate(PO):
            ax = po["axis"]
            if abs(d[ax]) < 1e-9: continue
            t = (po["c"] - o[ax]) / d[ax]
            if t <= 1e-4 or t >= tmax: continue
            q = (o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t)
            u = q[0] if ax == 2 else q[2]
            if po["u"][0] <= u <= po["u"][1] and po["v"][0] <= q[1] <= po["v"][1]: out.append((t, pi_))
        out.sort()
        return out[:2]
    def cond(p, open_): return ((p + 1) << 1) | (1 if open_ else 0)
    def lookup(t, loc):
        k = key_of(t, (loc[0], loc[1], loc[2])) if t >= 0 else None
        i_ = pid.get(k, -1) if k is not None else -1
        if i_ < 0 and 0 <= t < len(tri_home): i_ = tri_home[t]
        return i_
    def trace(o, d):
        """what a ray from o along d meets: [(patch | -1, tag)], back face?, escaped?"""
        loc, nrm, idx, dist = bvh.ray_cast(Vector(o), Vector(d), 500.0)
        tmax = dist if loc is not None else 1e9
        back = loc is not None and (nrm.x * d[0] + nrm.y * d[1] + nrm.z * d[2]) > 0
        hp = -1 if loc is None or back else lookup(idx, loc)
        cr = crossings(o, d, tmax)
        if not cr:
            esc = loc is None and not any(seg_box(o, d, 1e3, w) for w in WIN)
            return [(hp, 0)], back, esc
        res = []
        side = lambda pi_: leaf[pi_][0] if d[PO[pi_]["axis"]] > 0 else leaf[pi_][1]
        p1 = cr[0][1]
        res.append((side(p1), cond(p1, False)))
        if len(cr) == 1: res.append((hp, cond(p1, True)))
        else:
            p2 = cr[1][1]
            res.append((side(p2), cond(p1, True) | (cond(p2, False) << 4)))
            res.append((hp, cond(p1, True) | (cond(p2, True) << 4)))
        return res, back and len(cr) == 0, False
    def cos_dir(n):
        r1, r2 = random.random(), random.random()
        ph = 2 * math.pi * r1; sr = math.sqrt(r2)
        a = (1, 0, 0) if abs(n[0]) < 0.9 else (0, 1, 0)
        t = unit(cross(a, n)); b = cross(n, t)
        x, yv, z = sr * math.cos(ph), sr * math.sin(ph), math.sqrt(max(0.0, 1 - r2))
        return (t[0] * x + b[0] * yv + n[0] * z, t[1] * x + b[1] * yv + n[1] * z, t[2] * x + b[2] * yv + n[2] * z)

    # ---- patch -> patch
    T1 = time.time()
    PP = []
    for ip, pa in enumerate(patches):
        acc = {}; esc = 0
        smp = pa["smp"]
        if not pa.get("leaf") and not in_grid(pa["p"], 0.03):           # outside every room: its own light only (the Sun's
            pa["pp"] = []; continue                                        # outdoors; what it sends into the rooms is gathered there)
        for r in range(R_PATCH):
            q, n = smp[r % len(smp)]
            o = (q[0] + n[0] * 0.003, q[1] + n[1] * 0.003, q[2] + n[2] * 0.003)
            res, back, e = trace(o, cos_dir(n))
            esc += e
            for hp, tag in res:
                if hp < 0 or hp == ip: continue
                acc[(hp, tag)] = acc.get((hp, tag), 0) + 1
        pa["pp"] = [(hp, tag, c / R_PATCH) for (hp, tag), c in acc.items()]
        g = in_grid(pa["p"], 0.03)
        if g and not pa.get("leaf") and esc / R_PATCH < 0.2: pa["room"] = g["room"]
        if pa["room"] == 0 and not pa.get("leaf"): pa["pp"] = []           # (the outer face of a room's wall: as outside)
        if ip % 2000 == 0: log("  patch rays %d / %d (%.0f s)" % (ip, len(patches), time.time() - T1))
    log("patch -> patch: %d entries in %.1f s" % (sum(len(p["pp"]) for p in patches), time.time() - T1))

    # ---- lamp -> patch visibility
    T2 = time.time()
    LA = J["lamps"]
    for la in LA:
        if la["type"] == "rect":
            la["n"] = unit(cross(la["ex"], la["ey"])); la["range"] = math.sqrt(la["lumen"] / (2 * math.pi))
    nLV = 0
    for ip, pa in enumerate(patches):
        pa["lv"] = []
        p = pa["p"]; rad = math.sqrt(pa["area"]) * 0.75
        for il, la in enumerate(LA):
            c = la["c"]; v = sub(p, c); d = math.sqrt(dot(v, v))
            if d > la["range"] + rad: continue
            if la["type"] == "rect":
                if dot(v, la["n"]) < 0.01 - rad: continue
            elif la["type"] == "spot":
                if d > 1e-3 and dot(v, la["dir"]) / d < la["cosOut"] - 0.15: continue
            acc = {}
            smp = pa["smp"]
            for s in range(S_LAMP):
                q, n = smp[(s * 7) % len(smp)]
                if la["type"] == "rect":
                    u_, v_ = 2 * random.random() - 1, 2 * random.random() - 1
                    tgt = tuple(c[k] + la["ex"][k] * u_ + la["ey"][k] * v_ + la["n"][k] * 0.02 for k in range(3))
                else: tgt = tuple(c)
                o = (q[0] + n[0] * 0.01, q[1] + n[1] * 0.01, q[2] + n[2] * 0.01)
                dv = sub(tgt, o); ln = math.sqrt(dot(dv, dv))
                if ln < 1e-4: continue
                dd = (dv[0] / ln, dv[1] / ln, dv[2] / ln)
                if dot(dd, n) <= 0: continue
                reach = ln - (0.03 if la["type"] == "rect" else 0.15)
                if reach > 0:
                    loc, _, _, _ = bvh.ray_cast(Vector(o), Vector(dd), reach)
                    if loc is not None: continue
                cr = crossings(o, dd, ln)
                tag = 0
                if len(cr) >= 1: tag = cond(cr[0][1], True)
                if len(cr) >= 2: tag |= cond(cr[1][1], True) << 4
                acc[tag] = acc.get(tag, 0) + 1
            for tag, cnt in acc.items():
                pa["lv"].append((il, tag, cnt / S_LAMP)); nLV += 1
        if ip % 2000 == 0: log("  lamp rays %d / %d (%.0f s)" % (ip, len(patches), time.time() - T2))
    log("lamp -> patch: %d entries in %.1f s" % (nLV, time.time() - T2))

    # ---- the probes
    T3 = time.time()
    def sphere(nr):
        out = []; ga = math.pi * (3 - math.sqrt(5))
        for k in range(nr):
            yv = 1 - 2 * (k + 0.5) / nr; r = math.sqrt(max(0, 1 - yv * yv)); th = ga * k
            out.append((r * math.cos(th), yv, r * math.sin(th)))
        return out
    SPH = sphere(R_PROBE)
    probes = []; grids_out = []
    for g in GR:
        lo, hi, dims = g["lo"], g["hi"], g["dims"]
        cs = [(hi[k] - lo[k]) / dims[k] for k in range(3)]
        base = len(probes); gp = []
        for iz in range(dims[2]):
            for iy in range(dims[1]):
                for ix in range(dims[0]):
                    p = (lo[0] + (ix + 0.5) * cs[0], lo[1] + (iy + 0.5) * cs[1], lo[2] + (iz + 0.5) * cs[2])
                    # a random turn of the sphere per probe (no common pattern between neighbours)
                    a1, a2 = random.random() * 2 * math.pi, random.random() * 2 * math.pi
                    c1, s1, c2, s2 = math.cos(a1), math.sin(a1), math.cos(a2), math.sin(a2)
                    acc = {}; back = 0; esc = 0
                    for d0 in SPH:
                        d1 = (d0[0] * c1 - d0[2] * s1, d0[1], d0[0] * s1 + d0[2] * c1)
                        d = (d1[0], d1[1] * c2 - d1[2] * s2, d1[1] * s2 + d1[2] * c2)
                        res, bk, e = trace(p, d)
                        back += bk; esc += e
                        for hp, tag in res:
                            if hp < 0: continue
                            a = acc.get((hp, tag))
                            if a is None: a = acc[(hp, tag)] = [0, 0.0, 0.0, 0.0]
                            a[0] += 1; a[1] += d[0]; a[2] += d[1]; a[3] += d[2]
                    ok = back / R_PROBE < 0.15 and esc / R_PROBE < 0.25
                    ents = [(hp, tag, unit((a[1], a[2], a[3])), 4.0 * a[0] / R_PROBE) for (hp, tag), a in acc.items()]
                    probes.append({"p": p, "ok": ok, "e": ents, "ijk": (ix, iy, iz)})
        # the bad ones: the nearest good probe's list (in this grid)
        good = [k for k in range(base, len(probes)) if probes[k]["ok"]]
        nbad = 0
        for k in range(base, len(probes)):
            if probes[k]["ok"] or not good: continue
            pk = probes[k]["p"]
            j = min(good, key=lambda j: dot(sub(probes[j]["p"], pk), sub(probes[j]["p"], pk)))
            probes[k]["ref"] = j; nbad += 1                                   # (its list shared, not copied)
        grids_out.append((lo, hi, dims, g["room"], base))
        log("  grid room %d: %d probes (%d inside solids / outside, filled) %.0f s" % (g["room"], len(probes) - base, nbad, time.time() - T3))
    log("probes: %d, %d entries in %.1f s" % (len(probes), sum(len(p["e"]) for p in probes), time.time() - T3))

    # ---- the clusters (Enlighten's: the form factors and the probes reach clusters of patches - a plane's 2 x 2 m in a room,
    # 4 x 4 m outside -, each frame a cluster's radiosity the area-weighted mean of its patches'): ~10x fewer entries
    def ckey(ip, pa):
        if pa.get("leaf"): return ("leaf", ip)
        c = 2.0 if pa["room"] > 0 else 4.0
        q = pa["p"]
        return (pa["room"], bucket(pa["n"]), math.floor(q[0] / c), math.floor(q[1] / c), math.floor(q[2] / c))
    cmap = {}; pc = []
    for ip, pa in enumerate(patches):
        k = ckey(ip, pa)
        if k not in cmap: cmap[k] = len(cmap)
        pc.append(cmap[k])
    order = sorted(range(len(patches)), key=lambda ip: (pc[ip], ip))
    nC = len(cmap); first = [0] * nC; cnt = [0] * nC
    for pos, ip in enumerate(order):
        c = pc[ip]
        if cnt[c] == 0: first[c] = pos
        cnt[c] += 1
    if nC > 65535: raise RuntimeError("too many clusters")
    for pa in patches:
        acc = {}
        for hp, tag, w in pa["pp"]:
            k = (pc[hp], tag); acc[k] = acc.get(k, 0.0) + w
        pc_ = [(c, tag, w) for (c, tag), w in acc.items()]
        kept = [e for e in pc_ if e[2] >= 1.5 / R_PATCH]                    # one ray's hit is noise: dropped, its energy kept
        if len(kept) >= 8:
            k_ = sum(e[2] for e in pc_) / max(sum(e[2] for e in kept), 1e-9); pc_ = [(c, t, w * k_) for c, t, w in kept]
        pa["pc"] = pc_
    for pr in probes:
        if "ref" in pr: continue
        acc = {}
        for hp, tag, d, w in pr["e"]:
            k = (pc[hp], tag); a = acc.get(k)
            if a is None: a = acc[k] = [0.0, 0.0, 0.0, 0.0]
            a[0] += w; a[1] += d[0] * w; a[2] += d[1] * w; a[3] += d[2] * w
        ec = [(c, tag, unit((a[1], a[2], a[3])), a[0]) for (c, tag), a in acc.items()]
        kept = [e for e in ec if e[3] >= 1.5 * 4.0 / R_PROBE]
        if len(kept) >= 8:
            k_ = sum(e[3] for e in ec) / max(sum(e[3] for e in kept), 1e-9); ec = [(c, t, d, w * k_) for c, t, d, w in kept]
        pr["ec"] = ec
    log("clusters: %d; patch -> cluster %d entries, probe -> cluster %d" % (nC, sum(len(pa["pc"]) for pa in patches), sum(len(pr.get("ec", ())) for pr in probes)))
    # ---- write (version 2)
    def oct16(d):
        l1 = abs(d[0]) + abs(d[1]) + abs(d[2]); x, yv = d[0] / l1, d[2] / l1
        if d[1] < 0: x, yv = (1 - abs(yv)) * (1 if x >= 0 else -1), (1 - abs(x)) * (1 if yv >= 0 else -1)
        qx, qy = int(round((x * 0.5 + 0.5) * 65535)), int(round((yv * 0.5 + 0.5) * 65535))
        return (qx & 0xFFFF) | ((qy & 0xFFFF) << 16)
    with open(OUT, "wb") as f:
        nPP = sum(len(p["pc"]) for p in patches); nLVt = sum(len(p["lv"]) for p in patches); nPE = sum(len(p.get("ec", ())) for p in probes)
        f.write(b"TPGI" + struct.pack("<10i", 2, len(patches), nPP, len(LA), nLVt, len(PO), len(grids_out), len(probes), nPE, nC))
        ofs_pp = ofs_lv = 0
        for ip in order:
            pa = patches[ip]
            f.write(struct.pack("<10f i 4I", *pa["p"], *pa["n"], *pa["alb"], pa["area"], pa["room"], ofs_pp, len(pa["pc"]), ofs_lv, len(pa["lv"])))
            ofs_pp += len(pa["pc"]); ofs_lv += len(pa["lv"])
        for ip in order:
            for c, tag, w in patches[ip]["pc"]: f.write(struct.pack("<If", c | (tag << 16), w))
        for c in range(nC): f.write(struct.pack("<2I", first[c], cnt[c]))
        for la in LA:
            ty = {"point": 0, "spot": 1, "rect": 2}[la["type"]]
            dr = la.get("n") if ty == 2 else la.get("dir", (0, -1, 0))
            ex = la.get("ex", (0, 0, 0)); ey = la.get("ey", (0, 0, 0))
            f.write(struct.pack("<i13f", ty, *la["c"], *dr, *ex, *ey, la["range"]))
        for ip in order:
            for il, tag, v in patches[ip]["lv"]: f.write(struct.pack("<If", il | (tag << 16), v))
        for po in PO: f.write(struct.pack("<i5f", po["axis"], po["c"], po["u"][0], po["u"][1], po["v"][0], po["v"][1]))
        for lo, hi, dims, room, base in grids_out: f.write(struct.pack("<6f5i", *lo, *hi, *dims, room, base))
        ofs = 0
        for pr in probes:
            if "ref" not in pr: pr["ofs"] = ofs; ofs += len(pr["ec"])
        for pr in probes:
            src = probes[pr["ref"]] if "ref" in pr else pr
            f.write(struct.pack("<3I", src["ofs"], len(src["ec"]), 0 if "ref" in pr else 1))
        for pr in probes:
            if "ref" in pr: continue
            for c, tag, d, w in pr["ec"]: f.write(struct.pack("<IIf", c | (tag << 16), oct16(d), w))
    log("written %s (%.1f MB) in %.0f s total" % (OUT, os.path.getsize(OUT) / 1e6, time.time() - T0))
    rooms = {}
    for pa in patches: rooms[pa["room"]] = rooms.get(pa["room"], 0) + 1
    log("patches per room:", rooms)
except Exception:
    log(traceback.format_exc())
LOG.close()
