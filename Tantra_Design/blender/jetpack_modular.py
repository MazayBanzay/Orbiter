# Модульный ранец - the mock-ups of the 10 variants of tools/jetpack_modular_check.py (the user, 2026-10-16: «уйти в
# модульность: атмо-кит - подключаемые блоки, УВТ постоянен; общая кинематика: УВТ - дуга сверху, блоки подключаемы; соты,
# не круглые»). The base: «Наплечный»'s core and its capsule pods, now at the ends of one arc over the shoulders on a pitch
# gimbal (the hub on the core's top; each half twists at its root). The atmo kit: hex honeycomb modules clipped to the arc,
# its power unit and hydrogen tank as a box under the core. The layouts come from tools/jetpack_modular_layout.py.
#   blender --background astronavigator_suit.blend --python jetpack_modular.py -> renders\mod_<key>_<rear|front|fold>.png
#   (env MOD_ONLY=3,8 renders only those)
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
from jetpack_modular_layout import LAYOUTS
LOG = open(os.path.join(OUT, "mod_log.txt"), "w", encoding="utf-8")
KEEP = ("JetShell", "JetVessel", "JetNiche", "JetMark", "JetPorts", "JetCoupling")
for o in bpy.data.objects:
    if o.name.startswith("Jet") and not o.name.startswith(KEEP): o.hide_render = True; o.hide_viewport = True
def bbox(objs):
    ps = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    return Vector((min(p.x for p in ps), min(p.y for p in ps), min(p.z for p in ps))), Vector((max(p.x for p in ps), max(p.y for p in ps), max(p.z for p in ps)))
CORE = [o for o in bpy.data.objects if o.name.startswith(KEEP) and o.type == 'MESH']
C0, C1 = bbox(CORE)
Y0 = (C0.y + C1.y) / 2; ZTOP = C1.z
PODS = {}
for nm in ("JetPodL", "JetPodR"):
    p = bpy.data.objects[nm]; a, b = bbox([p]); PODS[1 if (a.x + b.x) > 0 else -1] = (p, (a + b) / 2)
print("core", tuple(C0), tuple(C1), "pods", {k: tuple(v[1]) for k, v in PODS.items()}, file=LOG); LOG.flush()
mats = {}
def mat(name, col, metal=0.2, rough=0.45):
    if name in mats: return mats[name]
    m = bpy.data.materials.new("P10_" + name); m.use_nodes = True
    bs = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bs.inputs["Base Color"].default_value = (*col, 1); bs.inputs["Metallic"].default_value = metal; bs.inputs["Roughness"].default_value = rough
    mats[name] = m; return m
WHITE = mat("white", (0.88, 0.89, 0.90), 0.1, 0.3); DARK = mat("dark", (0.04, 0.045, 0.05), 0.3, 0.45)
METAL = mat("metal", (0.62, 0.63, 0.65), 0.9, 0.28); ORANGE = mat("orange", (0.92, 0.42, 0.06), 0.1, 0.4)
GOLD = mat("gold", (0.85, 0.62, 0.25), 0.9, 0.3); NOZ = mat("noz", (0.30, 0.22, 0.18), 0.9, 0.35)

parts = []
def obj(name, bm, m):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); parts.append(o); return o

def prism(name, m, c, r, h, n=6, axis=(0, 0, 1), rot=0.0, r2=None, cap=True):
    bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=cap, segments=n, radius1=r, radius2=r if r2 is None else r2, depth=h)
    R = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4() @ Matrix.Rotation(rot, 4, 'Z')
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ R, verts=bm.verts); return obj(name, bm, m)

def duct(name, m, c, ro, ri, h, n=6, axis=(0, 0, 1), rot=math.pi / 6, k=36):
    """an open duct: an n-gon outer skin round a round bore, plain-quad rims"""
    bm = bmesh.new()
    def hexr(a):
        sect = 2 * math.pi / n; aa = (a - rot) % sect - sect / 2
        return ro * math.cos(math.pi / n) / math.cos(aa)
    rings = []
    for z in (-h / 2, h / 2):
        ang = [2 * math.pi * i / k for i in range(k)]
        rings.append(([bm.verts.new((hexr(a) * math.cos(a), hexr(a) * math.sin(a), z)) for a in ang], [bm.verts.new((ri * math.cos(a), ri * math.sin(a), z)) for a in ang]))
    (o0, i0), (o1, i1) = rings
    for q in range(k):
        r = (q + 1) % k
        for f in ((o0[q], o0[r], o1[r], o1[q]), (i0[r], i0[q], i1[q], i1[r]), (o0[r], o0[q], i0[q], i0[r]), (o1[q], o1[r], i1[r], i1[q])): bm.faces.new(f)
    R = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ R, verts=bm.verts); return obj(name, bm, m)

def box(name, m, c, s, R=None):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    M = Matrix.Translation(Vector(c)) @ (R or Matrix.Identity(4)) @ Matrix.Diagonal((*s, 1))
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts); return obj(name, bm, m)

def rod(name, m, a, b, r, n=8):
    a, b = Vector(a), Vector(b); return prism(name, m, (a + b) / 2, r, (b - a).length, n, axis=(b - a))

def rotor(name, c, axis, r0, r1, k=7, pitch=0.5, flip=1):
    axis = Vector(axis).normalized(); R = Vector((0, 0, 1)).rotation_difference(axis).to_matrix()
    bm = bmesh.new()
    for i in range(k):
        a = 2 * math.pi * i / k; u = Vector((math.cos(a), math.sin(a), 0)); t = Vector((-math.sin(a), math.cos(a), 0))
        ch = 0.05; cv = t * math.cos(pitch) * ch + Vector((0, 0, flip * math.sin(pitch) * ch))
        q = [u * r0 - cv, u * r1 - cv, u * r1 + cv, u * r0 + cv]
        bm.faces.new([bm.verts.new(Vector(c) + R @ p) for p in q])
    return obj(name, bm, DARK)

def core(y=0.30, z=1.24, h=0.40, r=0.12):
    prism("core", WHITE, (0, y, z), r, h, 6, rot=math.pi / 6)
    prism("coretop", DARK, (0, y, z + h / 2 + 0.01), r * 0.7, 0.02, 6, rot=math.pi / 6)
    box("band", ORANGE, (0, y + r * 0.87, z + 0.10), (0.10, 0.004, 0.02))

def bell(name, c, axis=(0, 0, -1), s=1.0):   # the boost nozzle, as on «Наплечный»: a short bell
    a = Vector(axis).normalized()
    prism(name + "p", METAL, Vector(c) - a * 0.03 * s, 0.035 * s, 0.06 * s, 12, axis=a)
    prism(name + "b", NOZ, Vector(c) + a * 0.04 * s, 0.025 * s, 0.08 * s, 16, axis=a, r2=0.05 * s, cap=False)

def vec_pod(tag, c, ro, h, axis=(0, 0, 1), coax=True, boost=True):
    """a vectored ducted module: the hexagonal duct on a gimbal yoke, two counter-rotating rotors, the hub, the boost bell"""
    c = Vector(c); ax = Vector(axis).normalized()
    duct(tag + "d", WHITE, c, ro, ro * 0.84, h, axis=ax)
    prism(tag + "h", DARK, c, ro * 0.22, h * 0.8, 16, axis=ax)
    rotor(tag + "r1", c + ax * h * 0.25, ax, ro * 0.22, ro * 0.82, 7, 0.5, 1)
    if coax: rotor(tag + "r2", c - ax * h * 0.05, ax, ro * 0.22, ro * 0.82, 7, 0.5, -1)
    if boost: bell(tag + "bb", c - ax * (h / 2 + 0.03), -ax, ro / 0.27)
    box(tag + "warn", ORANGE, c + ax * h * 0.32 + Vector((0, -ro * 0.86, 0)), (ro * 0.6, 0.004, 0.02))

def honey(name, outline, M, thick, cell_r, frame_idx=None, mp=WHITE):
    """a flat honeycomb panel in frame M (u span, v chord, n normal)"""
    bm = bmesh.new()
    top = [bm.verts.new(M @ Vector((u, v, thick / 2))) for u, v in outline]
    bot = [bm.verts.new(M @ Vector((u, v, -thick / 2))) for u, v in outline]
    bm.faces.new(top); bm.faces.new(bot[::-1]); k = len(outline)
    for i in range(k): bm.faces.new((top[i], top[(i + 1) % k], bot[(i + 1) % k], bot[i]))
    obj(name, bm, mp)
    def inside(u, v):
        c = False
        for i in range(k):
            (u0, v0), (u1, v1) = outline[i], outline[(i + 1) % k]
            if (v0 > v) != (v1 > v) and u < (u1 - u0) * (v - v0) / (v1 - v0) + u0: c = not c
        return c
    us = [p[0] for p in outline]; vs = [p[1] for p in outline]
    dx = cell_r * math.sqrt(3) + 0.004; dz = 1.5 * cell_r + 0.004
    cb = bmesh.new(); hb = bmesh.new(); j = 0; v = min(vs) + cell_r
    while v < max(vs) - cell_r * 0.5:
        u = min(us) + cell_r + (dx / 2 if j % 2 else 0)
        while u < max(us):
            hx = [(u + cell_r * math.cos(math.pi / 6 + a * math.pi / 3), v + cell_r * math.sin(math.pi / 6 + a * math.pi / 3)) for a in range(6)]
            if all(inside(pu, pv) for pu, pv in hx):
                for sd in (1, -1):
                    vv = [cb.verts.new(M @ Vector((u + 0.84 * (pu - u), v + 0.84 * (pv - v), sd * (thick / 2 + 0.001)))) for pu, pv in hx]
                    cb.faces.new(vv if sd > 0 else vv[::-1])
                    hh = [hb.verts.new(M @ Vector((u + 0.2 * (pu - u), v + 0.2 * (pv - v), sd * (thick / 2 + 0.003)))) for pu, pv in hx]
                    hb.faces.new(hh if sd > 0 else hh[::-1])
            u += dx
        v += dz; j += 1
    obj(name + "c", cb, DARK); obj(name + "h", hb, METAL)
    if frame_idx:
        e = bmesh.new()
        for i in frame_idx:
            (u0, v0), (u1, v1) = outline[i], outline[(i + 1) % k]
            e.faces.new([e.verts.new(M @ Vector(p)) for p in ((u0, v0, thick / 2 + 0.002), (u1, v1, thick / 2 + 0.002), (u1, v1, -thick / 2 - 0.002), (u0, v0, -thick / 2 - 0.002))])
        obj(name + "e", e, METAL)

RED = mat("red", (0.75, 0.10, 0.08), 0.2, 0.4)
KITC = mat("kit", (0.80, 0.81, 0.83), 0.15, 0.35)

def pod(sd, at):
    P, Pc = PODS[sd]
    o = P.copy(); o.data = P.data.copy(); bpy.context.scene.collection.objects.link(o); parts.append(o)
    o.hide_render = False; o.hide_viewport = False
    o.matrix_world = Matrix.Translation(Vector(at) - Pc) @ P.matrix_world
    return o

def hexband(name, m, c, s, rot, z, h, grow=0.006):
    bm = bmesh.new(); R = s / math.sqrt(3) + grow
    vs = [(c[0] + R * math.cos(math.radians(rot + 60 * k)), c[1] + R * math.sin(math.radians(rot + 60 * k))) for k in range(6)]
    top = [bm.verts.new((x, y, z + h / 2)) for x, y in vs]; bot = [bm.verts.new((x, y, z - h / 2)) for x, y in vs]
    for i in range(6): bm.faces.new((top[i], top[(i + 1) % 6], bot[(i + 1) % 6], bot[i]))
    return obj(name, bm, m)

def module(name, c, s, rot, z, coax=False, pu=False):
    """a hex module: the honeycomb plate (its rotors), the red band round it, a dark lip under it, the clip on top"""
    R = s / math.sqrt(3); th = 0.05
    outline = [(R * math.cos(math.radians(rot + 60 * k)), R * math.sin(math.radians(rot + 60 * k))) for k in range(6)]
    layers = (z, z - 0.075) if coax else (z,)
    for i, zz in enumerate(layers):
        honey(name + "p%d" % i, outline, Matrix.Translation(Vector((c[0], c[1], zz))), th, 0.024)
        hexband(name + "r%d" % i, RED, c, s, rot, zz + 0.008, 0.010)
        hexband(name + "l%d" % i, DARK, c, s, rot, zz - th / 2 - 0.004, 0.008, grow=-0.004)
    if coax: hexband(name + "w", WHITE, c, s, rot, z - 0.0375, 0.075, grow=-0.002)
    prism(name + "clip", DARK, (c[0], c[1], z + th / 2 + 0.012), 0.03, 0.024, 6)
    if pu: box(name + "pu", ORANGE, (c[0], c[1], z + th / 2 + 0.035), (0.06, 0.06, 0.04))

ARCH = 0.16                                   # the bow's rise over the capsules' level at its middle

def arc_z(x, x_root, z_hub, z_arm, xc=0.6):
    return z_arm + ARCH * (1 - (x / xc) ** 2)

def build(key, fold=False):
    L = LAYOUTS[key]; s, rot, z = L["s"], L["rot"], L["z"]; over = L.get("over_head")
    cap = L["cap"]; capy = Y0 + cap[1]
    z_hub = ZTOP + 0.04; z_arm = z + 0.06
    # the hub: the arc's pitch gimbal on the core's top (its axis across her back)
    prism("hub", DARK, (0, Y0, z_hub), 0.045, 0.16, 16, axis=(1, 0, 0))
    prism("hubr", RED, (0, Y0, z_hub), 0.047, 0.02, 16, axis=(1, 0, 0))
    rod("mast", WHITE, (0, Y0, z_hub), (0, Y0 - (0.02 if over else 0), (z_arm + 0.05) if over else (z_arm + ARCH)), 0.030, 12)
    x_root = 0.16
    N = 14
    for sd in (1, -1):
        mark = len(parts)
        # the arc's half: from the hub out to the capsule (over the head for the nimbus)
        pts = []
        for i in range(N + 1):
            x = cap[0] * i / N
            if over: zz = z_arm + 0.05 - 0.10 * (x / cap[0]) ** 2; yy = Y0 - 0.02 + (cap[1] + 0.02) * (x / cap[0])
            else: zz = arc_z(x, x_root, z_hub, z_arm, cap[0]); yy = Y0 + cap[1] * (x / cap[0])
            pts.append(Vector((sd * x, yy, zz)))
        for i in range(N): rod("arc%d_%d" % (sd, i), WHITE, pts[i], pts[i + 1], 0.024, 12)
        root = pts[3] if not over else pts[1]
        prism("twist%d" % sd, DARK, root, 0.034, 0.05, 16, axis=(1, 0, 0))     # the arm's twist joint (roll/yaw)
        prism("twistr%d" % sd, RED, root, 0.036, 0.012, 16, axis=(1, 0, 0))
        for j, c in enumerate(L["cells"]):
            cc = (sd * c[0], Y0 + c[1])
            module("m%d_%d" % (sd, j), cc, s, rot if sd > 0 else 180 - rot, z, L.get("coax"), L.get("dist"))
            k = min(N, max(0, round(abs(cc[0]) / cap[0] * N)))         # its strut to the arc's nearest point
            rod("st%d_%d" % (sd, j), DARK, (cc[0], cc[1], z + 0.03), tuple(pts[k]), 0.012, 8)
        pod(sd, (sd * cap[0], capy, z_arm - 0.02))
        prism("capj%d" % sd, DARK, (sd * cap[0], capy, z_arm + 0.05), 0.03, 0.04, 12)
        if fold:   # the arm folds down about its twist joint and slides back: the plates stand along her back
            M = Matrix.Translation(Vector((0, 0.16, 0))) @ Matrix.Translation(root) @ Matrix.Rotation(sd * math.radians(92), 4, 'Y') @ Matrix.Translation(-root)
            for o in parts[mark:]: o.matrix_world = M @ o.matrix_world
    # the kit's power unit and hydrogen tank under the core
    w = (C1.x - C0.x) * 0.9; dy = (C1.y - C0.y)
    h = 0.10 if L.get("dist") else (0.16 if L.get("light") else 0.20)
    kz = C0.z - h / 2 - 0.01
    box("kit", KITC, (0, Y0, kz), (w, dy * 0.9, h))
    honey("kitg", [(-w * 0.4, -h * 0.35), (w * 0.4, -h * 0.35), (w * 0.4, h * 0.35), (-w * 0.4, h * 0.35)],
          Matrix.Translation(Vector((0, Y0 + dy * 0.45 + 0.006, kz))) @ Matrix.Rotation(math.pi / 2, 4, 'X'), 0.01, 0.018)
    rod("tank", METAL, (-w * 0.45, Y0 + dy * 0.45 + 0.05, kz - h / 2 + 0.05), (w * 0.45, Y0 + dy * 0.45 + 0.05, kz - h / 2 + 0.05), 0.04, 16)
    box("kitr", RED, (0, Y0, kz + h / 2 - 0.012), (w * 1.01, dy * 0.91, 0.012))

sc = bpy.context.scene
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x, sc.render.resolution_y = 720, 720
fl = bpy.data.meshes.new("MD_floor"); fl.from_pydata([(-8, -8, 0), (8, -8, 0), (8, 8, 0), (-8, 8, 0)], [], [(0, 1, 2, 3)])
fo = bpy.data.objects.new("MD_floor", fl); sc.collection.objects.link(fo); fl.materials.append(mat("floor", (0.45, 0.43, 0.40), 0, 0.8))
for nm, en, rot in (("MD_sun", 3.2, (50, 0, 150)), ("MD_fill", 1.4, (60, 0, -30))):
    sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
    so.rotation_euler = tuple(math.radians(a) for a in rot)
if sc.world is None: sc.world = bpy.data.worlds.new("MD_world")
sc.world.use_nodes = True
bg = sc.world.node_tree.nodes.get("Background")
if bg: bg.inputs[0].default_value = (0.55, 0.65, 0.78, 1); bg.inputs[1].default_value = 0.8
cd = bpy.data.cameras.new("MD_cam"); cd.lens = 38; cam = bpy.data.objects.new("MD_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
VIEWS = {"rear": (Vector((1.45, 2.25, 2.25)), Vector((0, 0.2, 1.35))), "front": (Vector((-1.35, -2.2, 1.85)), Vector((0, 0.1, 1.3))),
         "top": (Vector((0.0, 1.1, 3.9)), Vector((0, 0.05, 1.4)))}
ONLY = os.environ.get("MOD_ONLY", "")
try:
    for key in LAYOUTS:
        if ONLY and key not in ONLY.split(","): continue
        shots = [(False, "rear"), (False, "front"), (False, "top")] + ([(True, "fold")] if LAYOUTS[key].get("fold") else [])
        for fold, vn in shots:
            build(key, fold)
            loc, tgt = VIEWS["rear" if vn == "fold" else vn]
            wide = 1.0 + max(0.0, LAYOUTS[key]["cap"][0] - 0.62) * 1.1
            cam.location = tgt + (loc - tgt) * wide; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - cam.location).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "mod_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
            for o in parts: bpy.data.objects.remove(o, do_unlink=True)
            parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
