# BUILD of the new suit type (make_suit2.py, from suit_plate_bend.py): writes astronavigator_suit2.blend.
# BEND STUDY (suit_plate_bend.py, from suit_plate_2g.py): the arcs off the helmet, no flank plates, the lower tier
# as nesting lames (a lobster tail) on their own bones, the lugs replaced by a sliding link - rendered upright and bent.
# The new suit type's cuirass, form 2г (the user's cuirass: wrap + two tiers + posture, the profile stood up), refined
# (the user, 2026-10-16): the arcs even, smooth and kinematic; the flank between the plate and the hip ring protected;
# the chest seams neat. Renders only; deformations of HIS mesh. Three variants А / Б / В.
#   blender -b astronavigator_suit.blend --python suit_plate_2g.py -> Tantra_Design/suit_mockups/g2_*.png
# The arcs: flat bands (a flat profile - the techno-frame, no tubes), symmetric, over the trapezius from the plate's top to
#   the pack's top, a knuckle at each end (the plate swings on them; the shoulders shrug and the arms rise under them).
#   А one band 40 mm; Б two bands 18 mm with ties (a ladder); В one band widening to a shoulder pad at the top.
# The flank (no protection there): А one curved side plate from the plate's side edge round to the pack, on the hip
#   ring; Б two lames over each other (she bends sideways); В the lower tier itself carried round to the side.
# The seams: the step a rolled lip of the upper tier over the lower one (no groove, no studs); the side seams engraved
#   fine lines with countersunk screws at the ends (А, Б) or none (В: the width taken in at the flank plates).
import bpy, bmesh, math, os, traceback
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.environ.get("SUIT2_OUT") or os.path.join(HERE, "..", "suit_mockups"); os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
try:
    cu = bpy.data.objects["Cuirass"]; mark = bpy.data.objects["MarkChest"]; arcs = bpy.data.objects["ShoulderArcs"]
    PLATE = bpy.data.materials["SuitPlate"]; FRAME = bpy.data.materials["Frame"]
    def mk(name, col, rough):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; return m
    LINE = mk("gb_line", (0.08, 0.08, 0.09), 0.8)
    Z0, Z1, HW = 1.036, 1.436, 0.188
    def sstep(a, b, x): t = min(1.0, max(0.0, (x - a) / (b - a))); return t * t * (3 - 2 * t)
    def front0(u, v): return -0.215 - 0.071 * v + 0.047 * u * u
    # form 2г
    V_STEP, STEP, TUCK, WRAP, NARROW, SCOOP, V_WRAP_TOP, LEAN, POST = 0.66, 0.016, 0.006, 0.09, 0.022, 0.030, 0.72, 0.055, 1.0
    def delta(u, v, wrap_bottom=0.0):
        a = abs(u); sg = 1.0 if u >= 0 else -1.0
        dy = POST * (-0.022 * math.exp(-((v - 0.58) / 0.20) ** 2) + 0.020 * sstep(0.82, 1.0, v) + 0.020 * (1 - sstep(0.0, 0.32, v)))
        up = sstep(V_STEP - 0.02, V_STEP + 0.02, v)
        dy += -STEP * up * (0.0 if FIT['nostep'] else 1.0) + TUCK * (1 - up) * (1 - v / V_STEP)
        w = sstep(0.55, 1.0, a) * (1 - sstep(V_WRAP_TOP - 0.12, V_WRAP_TOP, v))
        w2 = wrap_bottom * sstep(0.55, 1.0, a) * (1 - sstep(0.0, 0.45, v))          # В: the lower tier further round
        dx = -sg * NARROW * w; dy += WRAP * w * w + w2 * w2
        sc_ = sstep(0.62, 1.0, a) * sstep(0.62, 1.0, v)
        dx += -sg * SCOOP * sc_; dy += 0.6 * SCOOP * sc_
        dy += LEAN * (v - 0.5)
        dy += FITF(u, v) if FIT['mode'] else PLATE_IN   # the plate to the chest: fitted (FIT_MODE) or a rigid shift
        return dx, dy
    PLATE_IN = float(os.environ.get("PLATE_IN", "0.020"))                                  # the user, 2026-10-18: «НЕ МЕНЯТЬ ГЕОМЕТРИЕЙ, просто приблизить»
    HUGZ, HUGD = [], []
    # ---- the fit to the chest (the user, 2026-10-18: «форму как раз надо правильно анатомически подстроить под удобство
    #      и защиту»): the measured body front vs the plate's inner face on a grid (|x|, z); the plate's own features
    #      (tier step, wrap, lean) kept - the step measured out and put back; between the breasts one vault, no cups.
    #      z  per height only;  zt  taut over the hollows (a rigid sheet, the lower hull);  xzt  taut by height and width
    FIT = {"bend": os.environ.get("FIT_BEND", "step"), "bot": float(os.environ.get("FIT_BOT", "1")), "mode": os.environ.get("FIT_MODE", ""), "clear": float(os.environ.get("FIT_CLEAR", "0.012")),
           "keel": float(os.environ.get("FIT_KEEL", "0")), "nostep": False, "xs": [], "zs": [], "H": None}
    def FITF(u, v):
        if FIT["H"] is None: return 0.0
        xs, zs, H = FIT["xs"], FIT["zs"], FIT["H"]
        xa = abs(u) * HW; za = Z0 + (Z1 - Z0) * v
        # the chest only: the belly lames and the flanks keep the form tried in the game (they pressed into the body)
        wgt = (1 - sstep(0.13, 0.19, xa)) if FIT["mode"] == "lin" else sstep(1.12, 1.22, za) * (1 - sstep(0.10, 0.17, xa))
        x = min(max(xa, xs[0]), 0.10); z = min(max(za, zs[0]), zs[-1])
        i = min(int((x - xs[0]) / (xs[1] - xs[0])), len(xs) - 2); j = min(int((z - zs[0]) / (zs[1] - zs[0])), len(zs) - 2)
        a = (x - xs[i]) / (xs[1] - xs[0]); b = (z - zs[j]) / (zs[1] - zs[0])
        return wgt * ((H[j][i] * (1 - a) + H[j][i + 1] * a) * (1 - b) + (H[j + 1][i] * (1 - a) + H[j + 1][i + 1] * a) * b)
    def lower_hull(ts, ys):
        """the greatest convex function under the points: a rigid sheet laid over them from the front (-y)"""
        hull = []
        for p in zip(ts, ys):
            while len(hull) >= 2 and (hull[-1][0] - hull[-2][0]) * (p[1] - hull[-2][1]) - (hull[-1][1] - hull[-2][1]) * (p[0] - hull[-2][0]) <= 0: hull.pop()
            hull.append(p)
        out = []
        for t in ts:
            for k in range(len(hull) - 1):
                if hull[k][0] <= t <= hull[k + 1][0]:
                    w = (t - hull[k][0]) / (hull[k + 1][0] - hull[k][0]); out.append(hull[k][1] + (hull[k + 1][1] - hull[k][1]) * w); break
        return out
    def smooth(a, r):
        return [sum(a[max(0, k - r):k + r + 1]) / len(a[max(0, k - r):k + r + 1]) for k in range(len(a))]
    def measure_fit():
        mode, clear = FIT["mode"], FIT["clear"]; FIT["nostep"] = True; FIT["H"] = None
        dg = bpy.context.evaluated_depsgraph_get()
        def verts(n):
            o = bpy.data.objects[n]; e = o.evaluated_get(dg); me = e.to_mesh(); vs = [o.matrix_world @ q.co for q in me.vertices]; e.to_mesh_clear(); return vs
        cv = verts("Cuirass"); bv = [q for q in verts("SuitBody") if q.y < 0.0]
        xs = [0.025 * k for k in range(7)]; zs = [Z0 + 0.02 * k for k in range(int((Z1 - Z0) / 0.02) + 1)]
        G = [[None] * len(xs) for _ in zs]; YP = [[None] * len(xs) for _ in zs]
        for j, z in enumerate(zs):
            v = (z - Z0) / (Z1 - Z0)
            for i, x in enumerate(xs):
                gs, ps = [], []
                for sx in ((x, -x) if x > 0 else (0.0,)):
                    cb = [q.y for q in cv if abs(q.x - sx) < 0.014 and abs(q.z - z) < 0.013]
                    bb = [q.y for q in bv if abs(q.x - sx) < 0.014 and abs(q.z - z) < 0.013]
                    cb = [y for y in cb if y < min(cb) + 0.035] if cb else cb   # the front sheet, not the wrap's back edge
                    if cb and bb:
                        yp = max(cb) + delta(sx / HW, v)[1]; gs.append(min(bb) - yp); ps.append(yp)
                if gs: G[j][i] = min(gs); YP[j][i] = sum(ps) / len(ps)
        for row in (G, YP):                               # holes: the nearest value along the row, then the column
            for r in row:
                for i in range(len(r)):
                    if r[i] is None: r[i] = next((r[k] for d in range(1, len(r)) for k in (i - d, i + d) if 0 <= k < len(r) and r[k] is not None), None)
            for i in range(len(xs)):
                for j in range(len(zs)):
                    if row[j][i] is None: row[j][i] = next((row[k][i] for d in range(1, len(zs)) for k in (j - d, j + d) if 0 <= k < len(zs) and row[k][i] is not None), 0.0)
        if mode == "lin":
            # the user's line (2026-10-18): two flat planes in profile - the collar to the bust line, the bust line back
            # to the belt; the tiers keep their form, only tilted; as close as the body allows (clear everywhere)
            wx = [1 - sstep(0.13, 0.19, x) for x in xs]     # the flanks follow by this share (FITF): their limit / share
            # x 0.15 left out: that column catches the armpit and the arm's front (A-pose), not the chest
            hmax = [min((G[j][i] - clear) / wx[i] for i in range(len(xs) - 1)) for j in range(len(zs))]
            if FIT["bend"] == "apex": jb = min(range(len(zs)), key=lambda j: hmax[j] if 1.18 < zs[j] < 1.36 else 9)
            else: jb = min(range(len(zs)), key=lambda j: abs(zs[j] - float(FIT["bend"] if FIT["bend"] != "step" else Z0 + (Z1 - Z0) * V_STEP)))
            best = None
            for k in range(40):
                hb = hmax[jb] - 0.002 * k
                ht = min([hb + (hmax[j] - hb) * (zs[-1] - zs[jb]) / (zs[j] - zs[jb]) for j in range(jb + 1, len(zs))] or [hb])
                hl = min([hb + (hmax[j] - hb) * (zs[jb] - zs[0]) / (zs[jb] - zs[j]) for j in range(0, jb)] or [hb])
                hl = hb + (hl - hb) * FIT["bot"]
                hz = [hb + (ht - hb) * (zs[j] - zs[jb]) / (zs[-1] - zs[jb]) if j >= jb else hb + (hl - hb) * (zs[jb] - zs[j]) / (zs[jb] - zs[0]) for j in range(len(zs))]
                if any(h > m + 1e-6 for h, m in zip(hz, hmax)): continue
                sc_ = sum(hz)
                if best is None or sc_ > best[0]: best = (sc_, hz, zs[jb], hb, ht, hl)
            hz = best[1]; FIT["line"] = "bend z %.2f  H bend %+.3f top %+.3f bottom %+.3f" % best[2:]
            H = [[hz[j]] * len(xs) for j in range(len(zs))]
        elif mode in ("z", "zt"):
            hz = smooth([min(G[j][:5]) - clear for j in range(len(zs))], 2)
            hz = [min(h, min(G[j][:5]) - clear) if mode == "zt" else h for j, h in enumerate(hz)]
            if mode == "zt":
                e = lower_hull(zs, [YP[j][0] + hz[j] for j in range(len(zs))]); hz = [e[j] - YP[j][0] for j in range(len(zs))]
            H = [[hz[j]] * len(xs) for j in range(len(zs))]
        else:                                             # xzt
            H = [[G[j][i] - clear for i in range(len(xs))] for j in range(len(zs))]
            for i in range(len(xs)):                      # taut by height, column by column
                e = lower_hull(zs, [YP[j][i] + H[j][i] for j in range(len(zs))])
                for j in range(len(zs)): H[j][i] = e[j] - YP[j][i]
            NI = 5                                        # taut by width over |x| <= 0.10 (one vault across the breasts)
            for j in range(len(zs)):
                ts = [-xs[i] for i in range(NI, 0, -1)] + xs[:NI + 1]; ys = [YP[j][abs(k)] + H[j][abs(k)] for k in range(-NI, NI + 1)]
                e = lower_hull(ts, ys)
                for i in range(NI + 1): H[j][i] = e[NI + i] - YP[j][i]
            for j in range(len(zs)):                      # a light smoothing, never into the clearance
                sm = smooth(H[j], 1); H[j] = [min(a, b) for a, b in zip(sm, H[j])]
        if FIT["keel"]:                                   # the keel: a ridge down the middle, out from the fit
            for j, z in enumerate(zs):
                w = sstep(Z0 + 0.06, Z0 + 0.14, z) * (1 - sstep(Z1 - 0.06, Z1 - 0.01, z))
                for i, x in enumerate(xs): H[j][i] -= FIT["keel"] * math.exp(-(x / 0.035) ** 2) * w
        FIT.update(xs=xs, zs=zs, H=H, nostep=False)
        rep_ = ["mode %s clear %.3f keel %.3f %s" % (mode, clear, FIT["keel"], FIT.get("line", ""))]
        for j, z in enumerate(zs):
            rep_.append("z %.2f  H " % z + " ".join("%+.3f" % h for h in H[j]) + "   gap left " + " ".join("%+.3f" % (G[j][i] - H[j][i]) for i in range(len(xs))))
        return rep_
    def HUG(v):
        if not HUGZ: return 0.0
        z = Z0 + (Z1 - Z0) * v
        if z <= HUGZ[0]: return HUGD[0]
        if z >= HUGZ[-1]: return HUGD[-1]
        for k in range(len(HUGZ) - 1):
            if HUGZ[k] <= z <= HUGZ[k + 1]:
                t = (z - HUGZ[k]) / (HUGZ[k + 1] - HUGZ[k]); return HUGD[k] + (HUGD[k + 1] - HUGD[k]) * t
        return 0.0
    def measure_hug(clear=0.012):
        dg = bpy.context.evaluated_depsgraph_get()
        def verts(n):
            o = bpy.data.objects[n]; e = o.evaluated_get(dg); me = e.to_mesh(); vs = [o.matrix_world @ q.co for q in me.vertices]; e.to_mesh_clear(); return vs
        cv = verts("Cuirass"); bv = verts("SuitBody")
        zs = [Z0 + 0.02 * k for k in range(int((Z1 - Z0) / 0.02) + 1)]; raw = []
        for z in zs:
            v = (z - Z0) / (Z1 - Z0); g = 1e9
            for x in (-0.15, -0.10, -0.05, 0.0, 0.05, 0.10, 0.15):
                cb = [q.y for q in cv if abs(q.x - x) < 0.02 and abs(q.z - z) < 0.015]
                bb = [q.y for q in bv if abs(q.x - x) < 0.02 and abs(q.z - z) < 0.015 and q.y < 0.0]
                if cb and bb: g = min(g, min(bb) - max(cb) - delta(x / HW, v)[1])
            raw.append(g if g < 1e8 else 0.0)
        sm = [sum(raw[max(0, k - 2):k + 3]) / len(raw[max(0, k - 2):k + 3]) for k in range(len(raw))]   # smooth: no ripples
        HUGZ[:] = zs; HUGD[:] = [g - clear for g in sm]
        return list(zip(zs, HUGD))
    def spt(u, v, out=0.002, wb=0.0):
        dx, dy = delta(u, v, wb); return Vector((u * HW + dx, front0(u, v) + dy - out, Z0 + (Z1 - Z0) * v))

    def link(o): sc.collection.objects.link(o); return o
    def mesh_obj(name, bm, m, smooth=True):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        me.materials.append(m); return link(bpy.data.objects.new(name, me))
    def catmull(pts, n=12):
        P = [pts[0]] + pts + [pts[-1]]; out = []
        for i in range(1, len(P) - 2):
            p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
            for k in range(n):
                t = k / n
                out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
        out.append(pts[-1]); return out
    def band(name, path, width, thick, m, widths=None):
        """a flat band swept along the path: its width across (x), its thickness along the path's normal in the y-z plane"""
        bm = bmesh.new(); rings = []
        for i, p in enumerate(path):
            t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
            side = Vector((1, 0, 0)); nrm = t.cross(side).normalized()
            if nrm.z < 0: nrm = -nrm
            w = (widths[i] if widths else width) / 2; h = thick / 2
            ring = [bm.verts.new(p + side * sx * w + nrm * sz * h) for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            rings.append(ring)
        for a, b in zip(rings, rings[1:]):
            for k in range(4): bm.faces.new((a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]))
        bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
        o = mesh_obj(name, bm, m)
        bv = o.modifiers.new("b", 'BEVEL'); bv.width = min(thick, width) * 0.35; bv.segments = 3
        return o
    def knuckle(name, c, r=0.011, L=0.05):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=r, radius2=r, depth=L)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector((1, 0, 0))).to_matrix())
        bmesh.ops.translate(bm, vec=c, verts=bm.verts); return mesh_obj(name, bm, FRAME)
    def thin(name, pts, r, m):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 2; cd.use_fill_caps = True
        sp = cd.splines.new('POLY'); sp.points.add(len(pts) - 1)
        for q, c in zip(sp.points, pts): q.co = (*c, 1)
        cd.materials.append(m); return link(bpy.data.objects.new(name, cd))
    def screw(name, c):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=0.004, radius2=0.004, depth=0.0015)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector((0, -1, 0))).to_matrix())
        bmesh.ops.translate(bm, vec=c, verts=bm.verts); return mesh_obj(name, bm, LINE)
    def patch(name, f, nu, nv, thick, m):
        bm = bmesh.new(); g = [[bm.verts.new(f(i / nu, j / nv)) for i in range(nu + 1)] for j in range(nv + 1)]
        for j in range(nv):
            for i in range(nu): bm.faces.new((g[j][i], g[j][i + 1], g[j + 1][i + 1], g[j + 1][i]))
        o = mesh_obj(name, bm, m); s = o.modifiers.new("t", 'SOLIDIFY'); s.thickness = thick; s.offset = 0.0
        b = o.modifiers.new("b", 'BEVEL'); b.width = 0.003; b.segments = 2; b.limit_method = 'ANGLE'; return o

    # ---- the parts ----
    def arcs_path(sg):
        f = spt(sg * 0.62, 0.985, 0.0)                     # the plate's top, a little in from the corner
        # (the user: «дуги близко к шлему») lower, over the shoulder's slope, out from the helmet's rim (4-5 cm)
        return catmull([f + Vector((0, 0.012, 0.004)), Vector((sg * 0.150, f.y + 0.065, 1.485)), Vector((sg * 0.168, -0.06, 1.505)),
                        Vector((sg * 0.168, 0.035, 1.503)), Vector((sg * 0.150, 0.112, 1.498))], 10)
    STEEL_L = mk("gb_steel_light", (0.62, 0.63, 0.65), 0.35)     # a light metal (the user: «А, но не чёрная»)
    REDS = bpy.data.materials["MarkingRed"]
    def arcs_A(mat=None, stripe=False):
        objs = []
        for sg in (-1, 1):
            p = arcs_path(sg); objs.append(band("arc%+d" % sg, p, 0.040, 0.010, mat or FRAME))
            # the flange down its outer edge: an L section - from the side it reads as a profile, not a flat strap
            objs.append(band("arcfl%+d" % sg, [q + Vector((sg * 0.0175, 0, -0.008)) for q in p[1:-1]], 0.005, 0.022, mat or FRAME))
            if stripe: objs.append(band("arcred%+d" % sg, [q + Vector((0, 0, 0.0052)) for q in p[2:-2]], 0.008, 0.0015, REDS))
            objs += [knuckle("kf%+d" % sg, p[0]), knuckle("kb%+d" % sg, p[-1])]
        return objs
    def arcs_B():
        objs = []
        for sg in (-1, 1):
            p = arcs_path(sg)
            for dx in (-0.016, 0.016):
                objs.append(band("arc%+d%+.3f" % (sg, dx), [q + Vector((dx, 0, 0)) for q in p], 0.016, 0.008, FRAME))
            for k in (len(p) // 3, 2 * len(p) // 3):            # the ties across the pair
                objs.append(band("tie%+d_%d" % (sg, k), [p[k] + Vector((-0.02, 0, 0)), p[k] + Vector((0.02, 0, 0))], 0.012, 0.007, FRAME))
            objs += [knuckle("kf%+d" % sg, p[0], L=0.055), knuckle("kb%+d" % sg, p[-1], L=0.055)]
        return objs
    def arcs_C():
        objs = []
        for sg in (-1, 1):
            p = arcs_path(sg); n = len(p)
            ws = [0.034 + 0.030 * math.sin(math.pi * i / (n - 1)) ** 2 for i in range(n)]   # a pad over the shoulder
            objs.append(band("arc%+d" % sg, p, 0.04, 0.010, PLATE, ws))
            objs.append(band("arcspine%+d" % sg, [q + Vector((0, 0, 0.006)) for q in p], 0.010, 0.004, FRAME))
            objs += [knuckle("kf%+d" % sg, p[0]), knuckle("kb%+d" % sg, p[-1])]
        return objs
    def flank_round(sg, z0, z1, a0, a1, out):
        """a surface round her flank: angle a from her front (0) to her back (pi), on an ellipse round the waist"""
        def f(s, t):
            a = a0 + (a1 - a0) * s; z = z0 + (z1 - z0) * t
            rx, ry = 0.205 + out, 0.165 + out
            return Vector((sg * rx * math.sin(a), -0.03 - ry * math.cos(a), z))
        return f
    def flank_A():
        objs = []
        for sg in (-1, 1):
            objs.append(patch("flank%+d" % sg, flank_round(sg, 1.035, 1.215, 0.95, 2.35, 0.0), 16, 8, 0.008, PLATE))
            f = flank_round(sg, 1.035, 1.215, 0.95, 2.35, 0.0)
            objs.append(thin("flankrim%+d" % sg, [f(s / 20, 1.0) for s in range(21)], 0.004, FRAME))
        return objs
    def flank_B():
        objs = []
        for sg in (-1, 1):
            for k, (z0, z1, o) in enumerate(((1.035, 1.13, 0.0), (1.11, 1.215, 0.006))):   # the upper lame over the lower
                f = flank_round(sg, z0, z1, 0.95, 2.30, o)
                objs.append(patch("lame%+d_%d" % (sg, k), f, 16, 5, 0.007, PLATE))
                objs.append(thin("lamerim%+d_%d" % (sg, k), [f(s / 20, 0.0) for s in range(21)], 0.0035, FRAME))
        return objs
    def seams_lip():
        objs = [thin("lip", [spt(-0.96 + 1.92 * k / 40, V_STEP - 0.012, 0.004) for k in range(41)], 0.0045, PLATE)]   # the rolled lip
        return objs
    def seams_engraved():
        objs = []
        for sg in (-1, 1):
            u = sg * 0.48
            objs.append(thin("eng%+d" % sg, [spt(u, 0.07 + (V_STEP - 0.1) * k / 20, 0.0006) for k in range(21)], 0.0011, LINE))
            for v in (0.10, V_STEP - 0.06): objs.append(screw("scr%+d_%.2f" % (sg, v), spt(u, v, 0.0008)))
        return objs

    def plate(wb=0.0):
        me = cu.data.copy(); bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=2, use_grid_fill=True)
        M = cu.matrix_world; Mi = M.inverted()
        for vt in bm.verts:
            w = M @ vt.co
            u = max(-1.0, min(1.0, w.x / HW)); v = max(0.0, min(1.0, (w.z - Z0) / (Z1 - Z0)))
            dx, dy = delta(u, v, wb); vt.co = Mi @ Vector((w.x + dx, w.y + dy, w.z))
        bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = True
        o = cu.copy(); o.data = me; link(o)
        # the marking onto the new face: the front's y at the marking measured on both meshes (it sat inside the plate)
        def front_at(mesh):
            ys = [(M @ q.co).y for q in mesh.vertices if abs((M @ q.co).x) < 0.03 and abs((M @ q.co).z - 1.37) < 0.025]
            return min(ys) if ys else None
        new_f, old_f = front_at(me), front_at(cu.data)
        m2 = mark.copy(); link(m2)
        if mark.type == 'MESH':                         # bent with the plate (a rigid shift sank it into a tilted face)
            m2.data = mark.data.copy(); MM = m2.matrix_world.copy(); MMi = MM.inverted()
            for q in m2.data.vertices:
                w = MM @ q.co; u = max(-1.0, min(1.0, w.x / HW)); v = max(0.0, min(1.0, (w.z - Z0) / (Z1 - Z0)))
                dx, dy = delta(u, v); q.co = MMi @ Vector((w.x + dx, w.y + dy, w.z))
            ys = [(MM @ q.co).y for q in m2.data.vertices if abs((MM @ q.co).x) < 0.03 and abs((MM @ q.co).z - 1.37) < 0.025]
            if ys and new_f is not None: m2.location.y += (new_f - 0.0008) - min(ys)     # its face just proud, its depth in the plate
        elif new_f is not None and old_f is not None: m2.location.y += new_f - old_f
        return [o, m2]

    # ---- the bending: every rigid part on its own bone; the lower tier as lames nesting upwards under each other ----
    arm = bpy.data.objects["Astronavigator"]; lugs = bpy.data.objects["PlateLugs"]
    NAMES = {}
    def nice(o, base):
        k = NAMES.get(base, 0); NAMES[base] = k + 1; o.name = "T2%s%d" % (base, k); return o
    def to_bone(o, bone):
        """skin-ready: curves to meshes, the modifiers applied, one vertex group of this bone at full weight, the armature"""
        base = "".join(ch for ch in o.name.split(".")[0] if ch.isalpha()) or "Part"
        dg = bpy.context.evaluated_depsgraph_get()
        if o.type == 'CURVE':
            me = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); mats = list(o.data.materials)
            n = bpy.data.objects.new("tmp", me); sc.collection.objects.link(n); bpy.data.objects.remove(o); o = n
            me.materials.clear()
            for m in mats: me.materials.append(m)
        else:
            for md in list(o.modifiers):
                if md.type == 'ARMATURE': o.modifiers.remove(md)
            dg = bpy.context.evaluated_depsgraph_get()
            me = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); o.modifiers.clear(); o.data = me
        o.parent = None; o.vertex_groups.clear()
        vg = o.vertex_groups.new(name=bone); vg.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
        md = o.modifiers.new("Armature", 'ARMATURE'); md.object = arm
        return nice(o, base)
    def upper_tier():
        objs = plate()                                        # the whole 2г plate, then its lower part cut away
        o = objs[0]; me = o.data; M = o.matrix_world
        bm = bmesh.new(); bm.from_mesh(me)
        cut = [f for f in bm.faces if (M @ f.calc_center_median()).z < Z0 + (Z1 - Z0) * (V_STEP - 0.10)]   # lower: a deeper overlap
        bmesh.ops.delete(bm, geom=cut, context='FACES'); bm.to_mesh(me); bm.free()
        for x in objs: to_bone(x, "Spine1")
        return objs
    LAMES = [(V_STEP - 0.28, V_STEP + 0.09, 0.004, "Spine"), (V_STEP - 0.50, V_STEP - 0.19, 0.011, "LowerBack"), (0.0, V_STEP - 0.40, 0.018, "Hips")]   # deep overlaps
    def lames():
        objs = []
        for k, (v0, v1, inset, bone) in enumerate(LAMES):     # each lower lame inside the one above: it slides up under it
            f = lambda a, b, v0=v0, v1=v1, inset=inset: spt(-0.97 + 1.94 * a, v0 + (v1 - v0) * b, -inset)
            o = patch("lame%d" % k, f, 24, 6, 0.007, PLATE); objs.append(to_bone(o, bone))
            rim = thin("lamerim%d" % k, [f(i / 30, 0.0) + Vector((0, -0.003, 0)) for i in range(31)], 0.0035, PLATE)
            objs.append(to_bone(rim, bone))
        for sg in (-1, 1):                                     # the sliding link to the hip ring (the rigid lugs are gone)
            a = spt(sg * 0.40, 0.02, -0.02); b = Vector((sg * 0.08, -0.175, 1.0))
            objs.append(to_bone(band("link%+d" % sg, [a, (a + b) / 2 + Vector((0, -0.004, 0)), b], 0.022, 0.006, FRAME), "Hips"))
            objs.append(to_bone(knuckle("lk%+d" % sg, b, 0.009, 0.03), "Hips"))
        return objs
    def arcs_on(fn):
        objs = fn()
        for o in objs: to_bone(o, "Spine1")
        return objs
    def lip():
        return [to_bone(o, "Spine1") for o in seams_lip()]
    VARS = [("Aw", "А: лента белая", lambda: upper_tier() + lames() + arcs_on(lambda: arcs_A(PLATE)) + lip()),
            ("Ag", "А: лента светлый металл", lambda: upper_tier() + lames() + arcs_on(lambda: arcs_A(STEEL_L)) + lip()),
            ("Ar", "А: лента белая, красная полоса", lambda: upper_tier() + lames() + arcs_on(lambda: arcs_A(PLATE, True)) + lip())]
    VARS_OLD = [("A", "А: лента", lambda: upper_tier() + lames() + arcs_on(arcs_A) + lip()),
            ("B", "Б: двойные ленты", lambda: upper_tier() + lames() + arcs_on(arcs_B) + lip()),
            ("V", "В: лента-наплечник", lambda: upper_tier() + lames() + arcs_on(arcs_C) + lip())]
    BEND = float(os.environ.get("BEND", "10"))                  # deg on each of LowerBack, Spine, Spine1
    arm.data.pose_position = 'POSE'                         # (the file keeps the rest position on)
    def pose(deg):
        for b in ("LowerBack", "Spine", "Spine1"):
            pb = arm.pose.bones[b]; pb.rotation_mode = 'XYZ'; pb.rotation_euler = (math.radians(deg), 0, 0)
        bpy.context.view_layer.update()

    # ---- the new suit type: build, hide the prototype's chest parts (kept in the file), save as a new .blend ----
    arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    if FIT["mode"]:
        open(os.path.join(OUT, "suit2_fit_%s.txt" % os.environ.get("FIT_TAG", FIT["mode"])), "w").write(chr(10).join(measure_fit()))
    built = upper_tier() + lames() + arcs_on(lambda: arcs_A(STEEL_L)) + lip()
    for n in ("Cuirass", "MarkChest", "ShoulderArcs", "PlateLugs"):
        bpy.data.objects[n].hide_render = True; bpy.data.objects[n].hide_viewport = True
    dst = os.environ.get("SUIT2_BLEND") or os.path.join(HERE, "astronavigator_suit2.blend")   # a trial build: elsewhere
    bpy.ops.wm.save_as_mainfile(filepath=dst, copy=True)
    open(os.path.join(OUT, "suit2_built.txt"), "w", encoding="utf-8").write(chr(10).join(sorted(o.name for o in built)))
    if os.environ.get("SHOT"):                          # a side view of the trial (after the save: the file keeps its own scene)
        sc.render.engine = 'BLENDER_WORKBENCH'; sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'MATERIAL'
        sh.show_shadows = True; sh.show_cavity = True; sc.render.resolution_x, sc.render.resolution_y = 1000, 1200
        cd = bpy.data.cameras.new("shot"); cd.lens = 85; co = bpy.data.objects.new("shot", cd); sc.collection.objects.link(co); sc.camera = co
        P = Vector((3.0, -0.10, 1.22)); T = Vector((0.0, -0.10, 1.20))
        co.location = P; co.rotation_mode = 'QUATERNION'; co.rotation_quaternion = (T - P).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.environ["SHOT"]; bpy.ops.render.render(write_still=True)
except Exception:
    open(os.path.join(OUT, "suit2_error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
