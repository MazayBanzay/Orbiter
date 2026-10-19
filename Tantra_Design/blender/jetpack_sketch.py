# Модульный ранец - 10 эскизов (the user, 2026-10-16: «неэстетично, неустойчиво конструкцией, не похоже на 30 век»).
# No struts: the hex modules dock straight into a load-carrying yoke - a streamlined blade, a closed frame, a shell - that
# sits on «Наплечный»'s core through one wide pitch bearing; the capsules (its own pods) ride the same yoke. Sketch style:
# a light studio, Freestyle outlines (not on the honeycomb's small cells). Layouts: tools/jetpack_modular_layout.py SKETCH.
#   blender --background astronavigator_suit.blend --python jetpack_sketch.py -> renders\sk_<key>_<rear|top|fold>.png
#   (env SK_ONLY=1,4 renders only those)
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
from jetpack_modular_layout import SKETCH, hex_verts
LOG = open(os.path.join(OUT, "sk_log.txt"), "w", encoding="utf-8")
KEEP = ("JetShell", "JetVessel", "JetNiche", "JetMark", "JetPorts", "JetCoupling")
for o in bpy.data.objects:
    if o.name.startswith("Jet") and not o.name.startswith(KEEP): o.hide_render = True; o.hide_viewport = True

def bbox(objs):
    ps = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    return Vector((min(p.x for p in ps), min(p.y for p in ps), min(p.z for p in ps))), Vector((max(p.x for p in ps), max(p.y for p in ps), max(p.z for p in ps)))
CORE = [o for o in bpy.data.objects if o.name.startswith(KEEP) and o.type == 'MESH']
C0, C1 = bbox(CORE); Y0 = (C0.y + C1.y) / 2; ZTOP = C1.z
PODS = {}
for nm in ("JetPodL", "JetPodR"):
    p = bpy.data.objects[nm]; a, b = bbox([p]); PODS[1 if (a.x + b.x) > 0 else -1] = (p, (a + b) / 2, b - a)

sc = bpy.context.scene
NOLINE = bpy.data.collections.new("SK_noline"); sc.collection.children.link(NOLINE)
mats = {}
def mat(name, col, metal=0.1, rough=0.4, emit=None):
    if name in mats: return mats[name]
    m = bpy.data.materials.new("SK_" + name); m.use_nodes = True
    bs = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bs.inputs["Base Color"].default_value = (*col, 1); bs.inputs["Metallic"].default_value = metal; bs.inputs["Roughness"].default_value = rough
    mats[name] = m; return m
SHELL = mat("shell", (0.90, 0.91, 0.92), 0.05, 0.28); JOINT = mat("joint", (0.42, 0.44, 0.47), 0.8, 0.3)
RED = mat("red", (0.78, 0.09, 0.07), 0.1, 0.35); CELL = mat("cell", (0.03, 0.035, 0.04), 0.3, 0.5)
HUB = mat("hub", (0.62, 0.64, 0.66), 0.9, 0.22); PU = mat("pu", (0.95, 0.50, 0.10), 0.1, 0.4)

parts = []
def obj(name, bm, m, smooth=False, noline=False):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    if smooth:
        for pl in me.polygons: pl.use_smooth = True
    o = bpy.data.objects.new(name, me); (NOLINE if noline else sc.collection).objects.link(o); parts.append(o); return o

# ---- shapes ----
def spline(pts, n):
    """Catmull-Rom through pts, n+1 samples"""
    P = [Vector(p) for p in pts]; P = [P[0] * 2 - P[1]] + P + [P[-1] * 2 - P[-2]]
    out = []
    segs = len(P) - 3
    for i in range(n + 1):
        t = i / n * segs; k = min(int(t), segs - 1); u = t - k
        p0, p1, p2, p3 = P[k], P[k + 1], P[k + 2], P[k + 3]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3))
    return out

def sweep(name, pts, chord, thick, m, n=60, k=28, ex=3.0, closed=False, up=(0, 0, 1)):
    """a streamlined beam: a superellipse section (chord across the path, horizontal; thick vertical), sized along the
    path by chord(t), thick(t) (callables or numbers); rounded ends"""
    cf = chord if callable(chord) else (lambda t, c=chord: c); tf = thick if callable(thick) else (lambda t, h=thick: h)
    if closed: pts = list(pts) + [pts[0], pts[1], pts[2]]
    C = spline(pts, n)
    if closed:
        L = len(pts) - 3; C = C[: int(round(n * (L) / (len(pts) - 1))) + 1]
    bm = bmesh.new(); rings = []
    U = Vector(up)
    for i, c in enumerate(C):
        t = i / (len(C) - 1)
        tan = (C[min(i + 1, len(C) - 1)] - C[max(i - 1, 0)]).normalized()
        w = U.cross(tan); w = w.normalized() if w.length > 1e-6 else Vector((0, 1, 0)); h = tan.cross(w).normalized()
        end = 1.0 if closed else min(1.0, (min(t, 1 - t) * (len(C) - 1) / 3.0) ** 0.5 + 0.25)
        a, b = cf(t) / 2 * end, tf(t) / 2 * end
        ring = []
        for j in range(k):
            th = 2 * math.pi * j / k; cs, sn = math.cos(th), math.sin(th)
            ring.append(bm.verts.new(c + w * (a * math.copysign(abs(cs) ** (2 / ex), cs)) + h * (b * math.copysign(abs(sn) ** (2 / ex), sn))))
        rings.append(ring)
    for i in range(len(rings) - (0 if closed else 1)):
        r0, r1 = rings[i], rings[(i + 1) % len(rings)]
        for j in range(k): bm.faces.new((r0[j], r0[(j + 1) % k], r1[(j + 1) % k], r1[j]))
    if not closed:
        for r, rev in ((rings[0], True), (rings[-1], False)):
            ctr = bm.verts.new(sum((v.co for v in r), Vector()) / k)
            for j in range(k):
                f = (r[j], r[(j + 1) % k], ctr); bm.faces.new(f[::-1] if rev else f)
    return obj(name, bm, m, smooth=True)

def blob(name, c, r, m, rot=None):
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=1.0)
    M = Matrix.Translation(Vector(c)) @ (rot or Matrix.Identity(4)) @ Matrix.Diagonal((*r, 1))
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts); return obj(name, bm, m, smooth=True)

def cyl(name, c, r, h, m, axis=(0, 0, 1), seg=32, noline=False):
    bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r, depth=h)
    R = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ R, verts=bm.verts); return obj(name, bm, m, smooth=True, noline=noline)

def hexmod(name, c, s, rot, roll=0.0, th=0.055, tiers=1, pu=False, M_extra=None):
    """a hex module, chamfered: the white housing, the red band, the honeycomb of its rotors top and bottom"""
    R = s / math.sqrt(3); ch = 0.012
    M = Matrix.Translation(Vector(c)) @ Matrix.Rotation(math.radians(roll), 4, 'Y') @ Matrix.Rotation(math.radians(rot), 4, 'Z')
    if M_extra is not None: M = M_extra @ M
    for t in range(tiers):
        z0 = -t * (th + 0.02)
        bm = bmesh.new()
        def ringv(r, z): return [bm.verts.new(M @ Vector((r * math.cos(math.radians(60 * k)), r * math.sin(math.radians(60 * k)), z0 + z))) for k in range(6)]
        L = [ringv(R - ch, th / 2), ringv(R, th / 2 - ch), ringv(R, -th / 2 + ch), ringv(R - ch, -th / 2)]
        bm.faces.new(L[0]); bm.faces.new(L[-1][::-1])
        for a, b in zip(L, L[1:]):
            for k in range(6): bm.faces.new((a[k], b[k], b[(k + 1) % 6], a[(k + 1) % 6]))
        obj(name + "h%d" % t, bm, SHELL)
        bm = bmesh.new()                                        # the red band round its waist
        for zz in ((-0.006, 0.006),):
            lo = [bm.verts.new(M @ Vector(((R + 0.002) * math.cos(math.radians(60 * k)), (R + 0.002) * math.sin(math.radians(60 * k)), z0 + zz[0]))) for k in range(6)]
            hi = [bm.verts.new(M @ Vector(((R + 0.002) * math.cos(math.radians(60 * k)), (R + 0.002) * math.sin(math.radians(60 * k)), z0 + zz[1]))) for k in range(6)]
            for k in range(6): bm.faces.new((lo[k], lo[(k + 1) % 6], hi[(k + 1) % 6], hi[k]))
        obj(name + "r%d" % t, bm, RED, noline=True)
        bm = bmesh.new(); cr = 0.021; dx = cr * math.sqrt(3) + 0.006; dy = 1.5 * cr + 0.0035; Ri = R - ch - 0.01
        j = 0; v = -Ri
        while v <= Ri:
            u = -Ri + (dx / 2 if j % 2 else 0)
            while u <= Ri:
                hx = [(u + cr * math.cos(math.pi / 6 + a * math.pi / 3), v + cr * math.sin(math.pi / 6 + a * math.pi / 3)) for a in range(6)]
                def inside(px, py):
                    for k in range(6):
                        a1 = math.radians(60 * k + 30);
                        if px * math.cos(a1) + py * math.sin(a1) > Ri * math.cos(math.radians(30)): return False
                    return True
                if all(inside(*p) for p in hx):
                    for sd in (1, -1):
                        vv = [bm.verts.new(M @ Vector((px, py, z0 + sd * (th / 2 + 0.0015)))) for px, py in hx]
                        bm.faces.new(vv if sd > 0 else vv[::-1])
                u += dx
            v += dy; j += 1
        obj(name + "c%d" % t, bm, CELL, noline=True)
    if pu:
        blob(name + "pu", M @ Vector((0, 0, th / 2 + 0.018)), (0.05, 0.05, 0.018), PU)

def pod(sd, at, scale=1.0, M_extra=None):
    P, Pc, _ = PODS[sd]
    o = P.copy(); o.data = P.data.copy(); sc.collection.objects.link(o); parts.append(o)
    o.hide_render = False; o.hide_viewport = False
    at = Vector(at) - Vector((0, 0, PODS[sd][2].z * scale / 2 - 0.035))     # `at` is where its top docks: it hangs below
    M = Matrix.Translation(at) @ Matrix.Scale(scale, 4) @ Matrix.Translation(-Pc)
    o.matrix_world = (M_extra or Matrix.Identity(4)) @ M @ P.matrix_world
    return o

def mir(p, sd): return Vector((sd * p[0], p[1], p[2]))
def Y(y): return Y0 + y

def bearing(z=None):
    """the pitch bearing on the core's top: a wide drum across her back with red rims"""
    z = z or ZTOP + 0.045
    cyl("brg", (0, Y0, z), 0.055, 0.22, HUB, axis=(1, 0, 0))
    for sd in (1, -1): cyl("brgr%d" % sd, (sd * 0.112, Y0, z), 0.058, 0.012, RED, axis=(1, 0, 0), noline=True)
    blob("brgf", (0, Y0, z - 0.02), (0.20, 0.12, 0.05), SHELL)          # its fairing into the core
    return z

def kit(dist=False):
    """the atmo kit's power unit and hydrogen tank: a streamlined pod under the core (for dist: only the tank)"""
    w = (C1.x - C0.x) * 0.48; dy = (C1.y - C0.y) * 0.5
    h = 0.04 if dist else 0.06
    blob("kit", (0, Y0 + 0.01, C0.z - h * 0.5), (w, dy * 1.05, h), SHELL)          # a flat shell hugging the core's base
    cyl("kitr", (0, Y0 + 0.01, C0.z - h * 0.5), dy * 1.05, 0.012, RED, noline=True)
    parts[-1].scale = (w / dy * 1.01, 1.0, 1.0)

def modules(S, sd, M_extra=None):
    for j, c in enumerate(S["cells"]):
        hexmod("m%d_%d" % (sd, j), mir((c[0], Y(c[1]), c[2]), sd), S["s"], c[3] if sd > 0 else 180 - c[3], sd * c[4],
               tiers=2 if S.get("coax") else 1, pu=S.get("dist", False), M_extra=M_extra)

# ---- the ten ----
def k1(S, fold=False):           # Лезвие
    z = bearing()
    for sd in (1, -1):
        pts = [mir(p, sd) for p in ((0.0, Y(0.0), z), (0.22, Y(0.03), z + 0.07), (0.50, Y(0.05), 1.605), (0.80, Y(0.06), 1.565), (0.92, Y(0.06), 1.54))]
        sweep("bl%d" % sd, pts, lambda t: 0.22 - 0.10 * t, lambda t: 0.055 - 0.02 * t, SHELL)
        modules(S, sd); pod(sd, mir((S["pod"][0], Y(S["pod"][1]), 1.58), sd))
        blob("pf%d" % sd, mir((S["pod"][0], Y(S["pod"][1]), 1.585), sd), (0.06, 0.08, 0.04), SHELL)

def cassette(S, sd, double=False, z=None):
    c = (S["pod"][0], Y(S["pod"][1]), S["cells"][0][2])
    blob("hubc%d" % sd, mir((c[0], c[1], c[2] + 0.035), sd), (0.085, 0.085, 0.045), SHELL)     # the cassette's hub over the capsule
    cyl("hubr%d" % sd, mir((c[0], c[1], c[2] + 0.035), sd), 0.088, 0.012, RED, noline=True)
    # six short ribs from the hub to the modules' inner corners (in the cells' plane - the cassette's own frame)
    for j, cc in enumerate(S["cells"]):
        a = mir((cc[0], Y(cc[1]), cc[2] + 0.02), sd); b = mir((c[0], c[1], c[2] + 0.03), sd)
        sweep("rib%d_%d" % (sd, j), [b, b.lerp(a, 0.5), a.lerp(b, 0.25)], 0.035, 0.02, SHELL, n=8, k=12)
    pod(sd, mir((c[0], c[1], c[2] + 0.02), sd))

def k2(S, fold=False, double=False):     # Кассета-7 (and 10 with the double beam)
    z = bearing()
    for sd in (1, -1):
        c = S["pod"]
        for dy in ((-0.05, 0.05) if double else (0.0,)):
            pts = [mir(p, sd) for p in ((0.0, Y(dy), z), (0.18, Y(dy), z + 0.05), (c[0] - 0.10, Y(c[1] + dy * 0.6), c[2] + 0.06), (c[0], Y(c[1] + dy * 0.3), c[2] + 0.04))]
            sweep("bm%d%d" % (sd, int(dy * 100)), pts, lambda t: 0.10 - 0.03 * t, lambda t: 0.06 - 0.02 * t, SHELL)
        modules(S, sd); cassette(S, sd)

def k3(S, fold=False):           # Гексарама
    z = bearing(); zf = 1.565
    loop = [(0.0, 0.42), (0.40, 0.42), (0.62, 0.42), (0.80, 0.26), (0.92, 0.13), (0.80, 0.0), (0.62, -0.13), (0.30, -0.13),
            (0.18, -0.02), (0.0, 0.04)]
    pts = [Vector((x, Y(y), zf)) for x, y in loop] + [Vector((-x, Y(y), zf)) for x, y in reversed(loop[1:-1])]
    sweep("frame", pts, 0.06, 0.07, SHELL, n=140, k=20, closed=True)
    sweep("spine", [(0, Y0, z), (0, Y(0.2), zf + 0.02), (0, Y(0.42), zf)], 0.08, 0.06, SHELL, n=20)
    for sd in (1, -1):
        modules(S, sd); pod(sd, mir((S["pod"][0], Y(S["pod"][1]), S["pod"][2]), sd))

def k4(S, fold=False):           # Веер
    z = bearing()
    for sd in (1, -1):
        pv = (S["pod"][0], Y(S["pod"][1]))
        sweep("bl%d" % sd, [mir(p, sd) for p in ((0.0, Y0, z), (0.14, Y(0.01), z + 0.03), (pv[0], pv[1], 1.60))], 0.12, 0.06, SHELL, n=24)
        cyl("pv%d" % sd, mir((pv[0], pv[1], 1.575), sd), 0.055, 0.09, HUB)
        cyl("pvr%d" % sd, mir((pv[0], pv[1], 1.61), sd), 0.058, 0.012, RED, noline=True)
        cells = S["cells"]
        for j, c in enumerate(cells):
            ang = c[3] if not fold else 104
            cx = pv[0] + 0.33 * math.cos(math.radians(ang)); cy = pv[1] + 0.33 * math.sin(math.radians(ang))
            zz = c[2] + (0.0 if not fold else 0.06 * j - 0.02)
            hexmod("m%d_%d" % (sd, j), mir((cx, cy, zz), sd), S["s"], ang if sd > 0 else 180 - ang)
            a = mir((pv[0], pv[1], zz + 0.01), sd); b = mir((cx, cy, zz + 0.01), sd)        # its fan rib
            sweep("rib%d_%d" % (sd, j), [a, a.lerp(b, 0.5), a.lerp(b, 0.8)], lambda t: 0.06 - 0.03 * t, 0.025, SHELL, n=10, k=12)
        pod(sd, mir((pv[0], pv[1] - Y0, S["pod"][2]), sd) + Vector((0, Y0, 0)))

def k5(S, fold=False):           # Панцирь
    z = bearing()
    blob("cara", (0, Y0, z + 0.005), (0.24, 0.16, 0.05), SHELL)
    for sd in (1, -1):
        blob("lobe%d" % sd, mir((0.42, Y(0.0), 1.545), sd), (0.38, 0.29, 0.05), SHELL, rot=Matrix.Rotation(sd * math.radians(-4), 4, 'Y'))
        modules(S, sd); pod(sd, mir((S["pod"][0], Y(S["pod"][1]), S["pod"][2]), sd))

def k6(S, fold=False):           # Чешуя-чайка
    z = bearing()
    for sd in (1, -1):
        pts = [mir(p, sd) for p in ((0.0, Y0, z), (0.22, Y(0.06), 1.62), (0.48, Y(0.08), 1.64), (0.70, Y(0.07), 1.68), (0.90, Y(0.05), 1.70))]
        sweep("gw%d" % sd, pts, lambda t: 0.14 - 0.07 * t, lambda t: 0.06 - 0.025 * t, SHELL)
        for j, c in enumerate(S["cells"]):                     # each scale hangs from the gull arm by a faired pylon
            a = mir((c[0], Y(c[1]), c[2] + 0.03), sd); b = mir((c[0], Y(0.07), 1.62 + 0.06 * (c[0] - 0.36) / 0.43), sd)
            sweep("py%d_%d" % (sd, j), [a, a.lerp(b, 0.5), b], 0.08, 0.025, SHELL, n=8, k=12)
        modules(S, sd); pod(sd, mir((S["pod"][0], Y(S["pod"][1]), S["pod"][2]), sd))
        a = mir((S["pod"][0], Y(S["pod"][1]), S["pod"][2] + 0.08), sd); b = mir((S["pod"][0], Y(0.07), 1.645), sd)
        sweep("pp%d" % sd, [a, a.lerp(b, 0.5), b], 0.07, 0.03, SHELL, n=8, k=12)

def k7(S, fold=False): k2(S, fold)
def k10(S, fold=False): k2(S, fold, double=True)

def k8(S, fold=False):           # Трилистник-гондола
    z = bearing()
    for sd in (1, -1):
        c = S["pod"]; mark = len(parts)
        root = mir((0.17, Y0, z + 0.04), sd)
        sweep("bl%d" % sd, [mir(p, sd) for p in ((0.0, Y0, z), (0.17, Y0, z + 0.04), (0.40, Y0, 1.62), (c[0], Y(c[1]), 1.60))],
              lambda t: 0.14 - 0.04 * t, 0.06, SHELL)
        cyl("tw%d" % sd, root, 0.045, 0.06, HUB, axis=(1, 0, 0))
        modules(S, sd)
        blob("nac%d" % sd, mir((c[0], Y(c[1]), c[2] + 0.04), sd), (0.11, 0.11, 0.06), SHELL)       # the gondola's hub
        cyl("nacr%d" % sd, mir((c[0], Y(c[1]), c[2] + 0.04), sd), 0.113, 0.012, RED, noline=True)
        for j, cc in enumerate(S["cells"]):
            a = mir((c[0], Y(c[1]), c[2] + 0.03), sd); b = mir((cc[0], Y(cc[1]), cc[2] + 0.02), sd)
            sweep("rb%d_%d" % (sd, j), [a, a.lerp(b, 0.5), a.lerp(b, 0.75)], lambda t: 0.08 - 0.03 * t, 0.025, SHELL, n=8, k=12)
        pod(sd, mir((c[0], Y(c[1]), c[2] + 0.02), sd))
        if fold:
            M = Matrix.Translation(Vector((0, 0.17, 0))) @ Matrix.Translation(root) @ Matrix.Rotation(sd * math.radians(95), 4, 'Y') @ Matrix.Translation(-root)
            for o in parts[mark:]: o.matrix_world = M @ o.matrix_world

def k9(S, fold=False):           # Крыло Исиды
    z = bearing()
    for sd in (1, -1):
        pts = [mir(p, sd) for p in ((0.0, Y0, z), (0.25, Y(0.07), 1.68), (0.55, Y(0.13), 1.68), (0.82, Y(0.19), 1.60), (1.00, Y(0.22), 1.50))]
        sweep("wg%d" % sd, pts, lambda t: 0.30 - 0.20 * t, lambda t: 0.05 - 0.02 * t, SHELL)
        for j, c in enumerate(S["cells"]):
            a = mir((c[0], Y(c[1]), c[2] + 0.03), sd); b = a + Vector((0, 0, 0.07 if j < 2 else 0.09))
            sweep("py%d_%d" % (sd, j), [a, a.lerp(b, 0.5), b], 0.10, 0.025, SHELL, n=6, k=12)
        modules(S, sd); pod(sd, mir((S["pod"][0], Y(S["pod"][1]), S["pod"][2]), sd))


# ---- v2 (the three chosen): open blades - two spars with the cells between them; all fold down along her back ----
def fold_side(sd, mark, root, deg=95):
    M = Matrix.Translation(Vector((0, 0.15, 0))) @ Matrix.Translation(root) @ Matrix.Rotation(sd * math.radians(deg), 4, 'Y') @ Matrix.Translation(-root)
    for o in parts[mark:]: o.matrix_world = M @ o.matrix_world

def clips(S, sd, yf, ya):
    """each cell's fore and aft clip into the spars: small faired blocks from its corner to the spar"""
    R = S["s"] / math.sqrt(3)
    for j, c in enumerate(S["cells"]):
        for yy, sg in ((yf, -1), (ya, 1)):
            vtx = Y(c[1]) + sg * R; mid = (vtx + yy) / 2
            blob("cl%d_%d_%d" % (sd, j, sg + 1), mir((c[0], mid, c[2] + 0.01), sd), (0.03, abs(yy - vtx) / 2 + 0.012, 0.018), SHELL)

def k1b(S, fold=False):          # Лезвие v2: the open blade
    z = bearing()
    for sd in (1, -1):
        mark = len(parts); root = mir((0.16, Y0, z + 0.03), sd)
        yf, ya = Y(-0.20), Y(0.27)
        lead = [mir(p, sd) for p in ((0.0, Y(0.0), z), (0.16, Y(-0.10), z + 0.03), (0.40, Y(-0.20), 1.585), (0.70, Y(-0.20), 1.575), (0.86, Y(-0.08), 1.56), (0.88, Y(0.05), 1.555))]
        trail = [mir(p, sd) for p in ((0.0, Y(0.08), z), (0.16, Y(0.18), z + 0.02), (0.40, Y(0.27), 1.575), (0.70, Y(0.27), 1.57), (0.86, Y(0.16), 1.56), (0.88, Y(0.05), 1.555))]
        sweep("le%d" % sd, lead, lambda t: 0.075 - 0.02 * t, lambda t: 0.06 - 0.015 * t, SHELL, n=70)
        sweep("te%d" % sd, trail, lambda t: 0.05 - 0.015 * t, lambda t: 0.035 - 0.01 * t, SHELL, n=70)
        cyl("rt%d" % sd, root, 0.05, 0.07, HUB, axis=(1, 0, 0)); cyl("rtr%d" % sd, root, 0.053, 0.012, RED, axis=(1, 0, 0), noline=True)
        px = S["pod"][0]
        sweep("rib%d" % sd, [mir((px, Y(-0.20), 1.585), sd), mir((px, Y(0.04), 1.60), sd), mir((px, Y(0.27), 1.575), sd)], 0.05, 0.045, SHELL, n=16)
        blob("pn%d" % sd, mir((px, Y(0.04), 1.585), sd), (0.06, 0.07, 0.05), SHELL)
        clips(S, sd, yf, ya)
        modules(S, sd); pod(sd, mir((px, Y(S["pod"][1]), 1.56), sd))
        if fold: fold_side(sd, mark, root)

def k9b(S, fold=False):          # Исида v2: the open wing with the elbow
    z = bearing()
    for sd in (1, -1):
        mark = len(parts); root = mir((0.15, Y0, z + 0.03), sd)
        ex, ez = 0.68, 1.635                                       # the elbow
        lead = [mir(p, sd) for p in ((0.0, Y(0.0), z), (0.15, Y(-0.08), z + 0.05), (0.32, Y(-0.17), 1.67), (0.55, Y(-0.16), 1.665), (ex, Y(-0.12), ez))]
        trail = [mir(p, sd) for p in ((0.0, Y(0.08), z), (0.15, Y(0.16), z + 0.03), (0.32, Y(0.23), 1.62), (0.55, Y(0.23), 1.615), (ex, Y(0.19), ez - 0.03))]
        sweep("le%d" % sd, lead, lambda t: 0.08 - 0.015 * t, lambda t: 0.06 - 0.01 * t, SHELL, n=60)
        sweep("te%d" % sd, trail, lambda t: 0.05 - 0.01 * t, lambda t: 0.035, SHELL, n=60)
        # the outer link: down 20° from the elbow, its own spars closing at the tip
        tip = (1.00, Y(0.08), ez - 0.13)
        sweep("le2%d" % sd, [mir(p, sd) for p in ((ex, Y(-0.12), ez), (0.84, Y(-0.10), ez - 0.06), tip)], lambda t: 0.065 - 0.03 * t, lambda t: 0.05 - 0.02 * t, SHELL, n=30)
        sweep("te2%d" % sd, [mir(p, sd) for p in ((ex, Y(0.19), ez - 0.03), (0.84, Y(0.20), ez - 0.09), tip)], lambda t: 0.04 - 0.015 * t, 0.03, SHELL, n=30)
        # the elbow: a drum across the wing (its fold/vector joint) with the capsule under it
        cyl("el%d" % sd, mir((ex, Y(0.035), ez - 0.01), sd), 0.05, 0.36, SHELL, axis=(0, 1, 0))
        cyl("elr%d" % sd, mir((ex, Y(0.035), ez - 0.01), sd), 0.053, 0.014, RED, axis=(0, 1, 0), noline=True)
        cyl("rt%d" % sd, root, 0.05, 0.07, HUB, axis=(1, 0, 0)); cyl("rtr%d" % sd, root, 0.053, 0.012, RED, axis=(1, 0, 0), noline=True)
        clips(S, sd, Y(-0.165), Y(0.23))
        modules(S, sd); pod(sd, mir((ex, Y(S["pod"][1]), ez - 0.05), sd))
        if fold: fold_side(sd, mark, root, 100)

def k8b(S, fold=False):          # Трилистник-гондола v2
    z = bearing()
    for sd in (1, -1):
        c = S["pod"]; mark = len(parts)
        root = mir((0.17, Y0, z + 0.04), sd)
        sweep("bl%d" % sd, [mir(p, sd) for p in ((0.0, Y0, z), (0.17, Y0, z + 0.04), (0.42, Y0, 1.625), (c[0], Y(c[1]), 1.605))],
              lambda t: 0.15 - 0.05 * t, lambda t: 0.065 - 0.01 * t, SHELL)
        cyl("tw%d" % sd, root, 0.05, 0.07, HUB, axis=(1, 0, 0)); cyl("twr%d" % sd, root, 0.053, 0.012, RED, axis=(1, 0, 0), noline=True)
        modules(S, sd)
        cz = c[2] + 0.035
        blob("nac%d" % sd, mir((c[0], Y(c[1]), cz), sd), (0.105, 0.105, 0.055), SHELL)
        cyl("nacr%d" % sd, mir((c[0], Y(c[1]), cz - 0.01), sd), 0.107, 0.012, RED, noline=True)
        R = S["s"] / math.sqrt(3); d = math.hypot(S["cells"][0][0] - c[0], S["cells"][0][1] - c[1])
        for j, ang in enumerate((0, 120, 240)):                   # the Y frame in the slots between the cells (not over them)
            a = mir((c[0], Y(c[1]), cz), sd)
            u = Vector((sd * math.cos(math.radians(ang)), math.sin(math.radians(ang)), 0))
            b = a + u * (d + R * 0.55); b.z = c[2] + 0.005
            sweep("st%d_%d" % (sd, j), [a, a.lerp(b, 0.5), b], lambda t: 0.05 - 0.03 * t, lambda t: 0.05 - 0.025 * t, SHELL, n=14, k=16)
        pod(sd, mir((c[0], Y(c[1]), c[2] + 0.02), sd))
        if fold: fold_side(sd, mark, root)

BUILD = {"1": k1, "2": k2, "3": k3, "4": k4, "5": k5, "6": k6, "7": k7, "8": k8, "9": k9, "10": k10, "1b": k1b, "8b": k8b, "9b": k9b}

# ---- the studio ----
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x, sc.render.resolution_y = 900, 760
sc.render.film_transparent = True
sc.render.use_freestyle = not os.environ.get("SK_NOFS"); sc.render.line_thickness_mode = 'ABSOLUTE'; sc.render.line_thickness = 1.25
vl = sc.view_layers[0]; vl.use_freestyle = True; fs = vl.freestyle_settings
ls = fs.linesets[0] if len(fs.linesets) else fs.linesets.new("SK")
ls.select_by_collection = True; ls.collection = NOLINE; ls.collection_negation = 'EXCLUSIVE'
ls.select_silhouette = True; ls.select_border = True; ls.select_crease = True
if ls.linestyle is None: ls.linestyle = bpy.data.linestyles.new("SK_line")
ls.linestyle.color = (0.10, 0.11, 0.13); ls.linestyle.thickness = 1.3
try: sc.view_settings.view_transform = 'Standard'
except Exception: pass
fl = bpy.data.meshes.new("SK_floor"); fl.from_pydata([(-9, -9, 0), (9, -9, 0), (9, 9, 0), (-9, 9, 0)], [], [(0, 1, 2, 3)])
fo = bpy.data.objects.new("SK_floor", fl); NOLINE.objects.link(fo); fl.materials.append(mat("floor", (0.93, 0.93, 0.91), 0, 0.9))
for nm, en, rot, ang in (("SK_key", 3.0, (42, 0, 145), 0.35), ("SK_fill", 1.0, (65, 0, -40), 0.6)):
    sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = ang; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
    so.rotation_euler = tuple(math.radians(a) for a in rot)
if sc.world is None: sc.world = bpy.data.worlds.new("SK_world")
sc.world.use_nodes = True
bg = sc.world.node_tree.nodes.get("Background")
if bg: bg.inputs[0].default_value = (0.94, 0.94, 0.93, 1); bg.inputs[1].default_value = 0.9
cd = bpy.data.cameras.new("SK_cam"); cam = bpy.data.objects.new("SK_cam", cd); sc.collection.objects.link(cam); sc.camera = cam

def shoot(fname, half, view):
    if view == "side":
        cd.type = 'ORTHO'; cd.ortho_scale = 1.5
        cam.location = Vector((4.0, 0.25, 1.30)); tgt = Vector((0, 0.25, 1.30))
    elif view == "top":
        cd.type = 'ORTHO'; cd.ortho_scale = max(1.6, 2 * half + 0.35)
        cam.location = Vector((0, 0.25, 4.0)); tgt = Vector((0, 0.25, 0))
    else:
        cd.type = 'PERSP'; cd.lens = 50
        tgt = Vector((0, 0.18, 1.32)); d = Vector((1.30, 2.35, 1.35)) * (0.92 + 0.42 * half)
        cam.location = tgt + d
    cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(OUT, fname); bpy.ops.render.render(write_still=True)

ONLY = os.environ.get("SK_ONLY", "")
try:
    for key, S in SKETCH.items():
        if ONLY and key not in ONLY.split(","): continue
        half = max(max(v[0] for c in S["cells"] for v in hex_verts(c[:2], S["s"], c[3])), S["pod"][0] + 0.08)
        shots = [("rear", False), ("top", False)] + ([("side", False)] if key.endswith("b") else []) + ([("fold", True)] if S.get("fold") else [])
        for view, fold in shots:
            import time; t0 = time.time(); print("build", key, view, file=LOG); LOG.flush()
            BUILD[key](S, fold); kit(S.get("dist", False))
            print("built", len(parts), "objects", round(time.time() - t0, 1), "s", file=LOG); LOG.flush()
            shoot("sk_%s_%s.png" % (key, view), 0.55 if fold else half, view if view in ("top", "side") else "rear")
            print("rendered", key, view, file=LOG); LOG.flush()
            for o in parts: bpy.data.objects.remove(o, do_unlink=True)
            parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
