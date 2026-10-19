# 30th-century jet pack concepts on metallic hydrogen, simple visuals (the user, 2026-10-17): A ejector capsules,
# B the Coanda arc, C the MHD honeycomb. Helpers from jetpack_sketch.py; renders\c30_<A|B|C>_<rear|side>.png
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


# ==== 30th-century concepts on metallic hydrogen (the user, 2026-10-17: «что ты пристал к вентиляторам - простой визуал
# лучших вариантов, мет. водород топливо»). The lift through her centre of mass, the vector by the base's capsules (УВТ). ====
GLOW = bpy.data.materials.new("C30_glow"); GLOW.use_nodes = True
_bs = next(n for n in GLOW.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
_bs.inputs["Base Color"].default_value = (0.2, 0.75, 1.0, 1)
try: _bs.inputs["Emission Color"].default_value = (0.25, 0.8, 1.0, 1); _bs.inputs["Emission Strength"].default_value = 6.0
except Exception: _bs.inputs["Emission"].default_value = (0.25, 0.8, 1.0, 1)

def hextube(name, c, s_out, s_in, h, m, rot=0.0, inner=CELL):
    """an open hexagonal duct (vertical axis): the white walls, a dark bore"""
    bm = bmesh.new(); Ro, Ri = s_out / math.sqrt(3), s_in / math.sqrt(3); c = Vector(c)
    def ring(R, z): return [bm.verts.new(c + Vector((R * math.cos(rot + k * math.pi / 3), R * math.sin(rot + k * math.pi / 3), z))) for k in range(6)]
    ot, ob, it, ib = ring(Ro, h / 2), ring(Ro, -h / 2), ring(Ri, h / 2), ring(Ri, -h / 2)
    for k in range(6):
        j = (k + 1) % 6
        bm.faces.new((ob[k], ob[j], ot[j], ot[k])); bm.faces.new((ot[k], ot[j], it[j], it[k])); bm.faces.new((ib[k], ib[j], ob[j], ob[k]))
    itc, ibc = [v.co.copy() for v in it], [v.co.copy() for v in ib]
    obj(name, bm, m)
    bm = bmesh.new(); it2, ib2 = [bm.verts.new(v) for v in itc], [bm.verts.new(v) for v in ibc]
    for k in range(6):
        j = (k + 1) % 6; bm.faces.new((it2[k], it2[j], ib2[j], ib2[k]))
    obj(name + "in", bm, inner, noline=True)

def hexplate(name, c, s, m, rot=0.0, noline=True):
    bm = bmesh.new(); R = s / math.sqrt(3); c = Vector(c)
    bm.faces.new([bm.verts.new(c + Vector((R * math.cos(rot + k * math.pi / 3), R * math.sin(rot + k * math.pi / 3), 0))) for k in range(6)])
    obj(name, bm, m, noline=noline)

def arc_pts(sd, xe, ze, rise=0.07, z0=None, y=0.0):
    z0 = z0 or ZTOP + 0.045
    return [mir(p, sd) for p in ((0.0, Y(y), z0), (0.20, Y(y), z0 + rise), (0.45, Y(y), z0 + rise * 0.8), (xe, Y(y), ze))]

def capsule_at(sd, x, y, ztop):
    pod(sd, mir((x, Y(y), ztop), sd))
    blob("cj%d" % sd, mir((x, Y(y), ztop + 0.03), sd), (0.05, 0.05, 0.035), HUB)

def cA():   # Эжектор-капсулы: the capsules in hexagonal ejector shrouds, the shroud's intake ring open round the capsule
    z = bearing()
    for sd in (1, -1):
        sweep("arc%d" % sd, arc_pts(sd, 0.70, 1.58), lambda t: 0.13 - 0.05 * t, lambda t: 0.06 - 0.015 * t, SHELL)
        capsule_at(sd, 0.70, 0.0, 1.565)
        zc = 1.565 - PODS[sd][2].z + 0.035                       # the capsule's exit
        hextube("ej%d" % sd, mir((0.70, Y(0.0), zc - 0.10), sd), 0.21, 0.18, 0.22, SHELL)     # the mixing duct below the nozzle
        hextube("ejl%d" % sd, mir((0.70, Y(0.0), zc + 0.015), sd), 0.22, 0.19, 0.02, RED)      # the bell-mouth lip (red)
        for k in range(3):                                        # three thin pylons from the capsule's collar to the shroud
            a = math.radians(90 + 120 * k)
            p0 = mir((0.70 + 0.05 * math.cos(a), Y(0.05 * math.sin(a)), zc + 0.08), sd); p1 = mir((0.70 + 0.10 * math.cos(a), Y(0.10 * math.sin(a)), zc + 0.01), sd)
            sweep("py%d%d" % (sd, k), [p0, p0.lerp(p1, 0.5), p1], 0.02, 0.012, HUB, n=6, k=10)

def cB():   # Дуга-Коанда: the arc is the ejector - a honeycomb intake on top, the slot along its rear edge blowing the
            # capsules' gas over its curved lower face; the capsules at the tips for the vector
    z = bearing()
    for sd in (1, -1):
        pts = arc_pts(sd, 0.70, 1.56, rise=0.09)
        sweep("arc%d" % sd, pts, lambda t: 0.30 - 0.12 * t, lambda t: 0.07 - 0.02 * t, SHELL, n=60)
        C = spline(pts, 40)
        for i in range(6, 37, 2):                                 # the honeycomb intake along its top
            t = i / 40; ch = 0.30 - 0.12 * t
            for dy in (-0.25, 0.0, 0.25):
                hexplate("in%d_%d_%d" % (sd, i, int(dy * 100)), C[i] + Vector((0, dy * ch * 0.8, (0.07 - 0.02 * t) / 2 + 0.003)), 0.028, CELL)
        slot = [c + Vector((0, (0.30 - 0.12 * (i / 40)) * 0.40, -0.02)) for i, c in enumerate(C) if 4 <= i <= 38]
        sweep("slot%d" % sd, slot[::3], 0.012, 0.012, CELL, n=40, k=8)     # the Coanda slot (dark)
        sweep("rb%d" % sd, [c + Vector((0, -(0.30 - 0.12 * (i / 40)) * 0.42, 0.0)) for i, c in enumerate(C) if 4 <= i <= 38][::3], 0.012, 0.014, RED, n=40, k=8)
        capsule_at(sd, 0.72, 0.0, 1.55)

def cC():   # МГД-соты: two fixed panels of hexagonal channels (no moving parts: the air ionized and driven by the field,
            # a pale glow inside), at her shoulders, in the plane of the centre of mass; the capsules at their outer edges
    z = bearing()
    for sd in (1, -1):
        cx, cy, cz = 0.56, -0.02, 1.55
        sweep("arc%d" % sd, arc_pts(sd, 0.82, 1.585, rise=0.05, y=-0.02), lambda t: 0.10 - 0.03 * t, lambda t: 0.055, SHELL)
        s = 0.15; d = s + 0.006
        cells = [(0, 0)] + [(d * math.cos(math.radians(30 + 60 * k)), d * math.sin(math.radians(30 + 60 * k))) for k in range(6)]
        for j, (dx, dy) in enumerate(cells):
            c = mir((cx + dx, Y(cy + dy), cz), sd)
            hextube("ch%d_%d" % (sd, j), c, s, s * 0.80, 0.09, SHELL, rot=0.0)
            hexplate("gl%d_%d" % (sd, j), c + Vector((0, 0, 0.03)), s * 0.79, GLOW)
            hexplate("gt%d_%d" % (sd, j), c + Vector((0, 0, 0.047)), s * 0.22, CELL)     # the electrode hub on top
        cyl("frr%d" % sd, mir((cx + 0.05, Y(cy), cz + 0.05), sd), 0.012, 0.36, RED, axis=(1, 0, 0), noline=True)
        capsule_at(sd, 0.84, -0.02, 1.565)

CONCEPTS = [("A", cA), ("B", cB), ("C", cC)]

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


LOG2 = open(os.path.join(OUT, "c30_log.txt"), "w", encoding="utf-8")
try:
    for key, fn in CONCEPTS:
        for view in ("rear", "side"):
            fn(); kit(key == "C")
            shoot("c30_%s_%s.png" % (key, view), 0.95, view)
            print("rendered", key, view, file=LOG2); LOG2.flush()
            for o in parts: bpy.data.objects.remove(o, do_unlink=True)
            parts.clear()
    print("DONE", file=LOG2)
except Exception:
    import traceback; traceback.print_exc(file=LOG2); print("FAILED", file=LOG2)
LOG2.close()
