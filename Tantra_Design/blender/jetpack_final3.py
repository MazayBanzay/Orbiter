# «Соты-блоки»: the three finalists (6, 9, 5 of tools/jetpack_blocks_check.py) in the style of «Наплечный» (the user,
# 2026-10-16: «колокола мы убрали, посмотри на мой ранец»): its own core (the shell, the tank, the ports), round honeycomb
# blocks with a red ring, and the boost as its capsule pods with the plug nozzle - copies of JetPodL/R - on booms of their own.
#   blender --background astronavigator_suit.blend --python jetpack_final3.py  -> rendersin_<6|9|5>_<rear|front>.png
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
KEEP = ("JetShell", "JetVessel", "JetNiche", "JetMark", "JetPorts", "JetCoupling")   # the pack's core stays as it is
for o in bpy.data.objects:
    if o.name.startswith("Jet") and not o.name.startswith(KEEP): o.hide_render = True; o.hide_viewport = True
POD = bpy.data.objects["JetPodL"]
POD_C = sum((POD.matrix_world @ Vector(c) for c in POD.bound_box), Vector()) / 8
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

def wframe(root, side, dihedral=0.0, sweep=0.0):
    R = Matrix.Rotation(-side * dihedral, 4, 'Y') @ Matrix.Rotation(side * sweep, 4, 'Z')
    return Matrix.Translation(Vector(root)) @ R @ Matrix.Diagonal((side, 1, 1, 1))

RED = mat("red", (0.75, 0.10, 0.08), 0.2, 0.4)

def pod(name, at, scale=1.25, tilt=0.0):
    """a copy of «Наплечный»'s pod (its capsule, red ring, the plug nozzle), scaled, its centre at `at`"""
    o = POD.copy(); o.data = POD.data.copy(); bpy.context.scene.collection.objects.link(o); parts.append(o)
    o.hide_render = False; o.hide_viewport = False
    o.matrix_world = Matrix.Translation(Vector(at)) @ Matrix.Rotation(tilt, 4, 'X') @ Matrix.Scale(scale, 4) @ Matrix.Translation(-POD_C) @ POD.matrix_world
    return o

def boom(name, a, b, r=0.022):
    rod(name, WHITE, a, b, r, 12)
    prism(name + "j", DARK, b, r * 1.6, r * 2.2, 12, axis=(Vector(b) - Vector(a)))

def block(name, c, R, thick=0.08, cell=0.032):
    """a round honeycomb block: a disc with hexagonal cells, the red ring round its side, a dark lip, the gimbal yoke"""
    n = 32
    circ = [(R * math.cos(2 * math.pi * i / n), R * math.sin(2 * math.pi * i / n)) for i in range(n)]
    honey(name, circ, Matrix.Translation(Vector(c)), thick, cell)
    prism(name + "ring", RED, Vector(c) + Vector((0, 0, thick * 0.15)), R + 0.004, 0.012, 32)
    prism(name + "lip", DARK, Vector(c) + Vector((0, 0, -thick / 2 - 0.006)), R * 0.98, 0.012, 32)

def fin6():   # Б-19 + capsule pods on booms of their own (at the belt): blocks at the shoulder blades
    for s in (1, -1):
        c = Vector((s * 0.42, 0.24, 1.42))
        rod("arm%d" % s, WHITE, (s * 0.19, 0.25, 1.42), c - Vector((s * 0.22, 0, 0)), 0.024, 12)
        block("blk%d" % s, c, 0.22)
        boom("pb%d" % s, (s * 0.16, 0.24, 1.08), (s * 0.44, 0.20, 0.98))
        pod("pod%d" % s, (s * 0.50, 0.20, 0.98))

def fin9():   # Б-37 + capsule pods: bigger blocks
    for s in (1, -1):
        c = Vector((s * 0.50, 0.24, 1.42))
        rod("arm%d" % s, WHITE, (s * 0.19, 0.25, 1.42), c - Vector((s * 0.30, 0, 0)), 0.026, 12)
        block("blk%d" % s, c, 0.30, 0.09, 0.034)
        boom("pb%d" % s, (s * 0.16, 0.24, 1.08), (s * 0.48, 0.20, 0.98))
        pod("pod%d" % s, (s * 0.54, 0.20, 0.98), 1.3)

def fin5():   # four blocks: the shoulders and the hips, small capsules on the upper ones
    for s in (1, -1):
        for z, R in ((1.42, 0.16), (1.00, 0.16)):
            c = Vector((s * 0.40, 0.24, z))
            rod("arm%d%d" % (s, int(z * 10)), WHITE, (s * 0.17, 0.25, z), c - Vector((s * 0.16, 0, 0)), 0.022, 12)
            block("blk%d%d" % (s, int(z * 10)), c, R, 0.07, 0.03)
        pod("pod%d" % s, (s * 0.60, 0.24, 1.40), 0.9)
        boom("pb%d" % s, (s * 0.56, 0.24, 1.42), (s * 0.58, 0.24, 1.42))

VARIANTS = [("6", fin6), ("9", fin9), ("5", fin5)]
sc = bpy.context.scene
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x, sc.render.resolution_y = 800, 800
fl = bpy.data.meshes.new("F3_floor"); fl.from_pydata([(-6, -6, 0), (6, -6, 0), (6, 6, 0), (-6, 6, 0)], [], [(0, 1, 2, 3)])
fo = bpy.data.objects.new("F3_floor", fl); sc.collection.objects.link(fo); fl.materials.append(mat("floor", (0.45, 0.43, 0.40), 0, 0.8))
sun = bpy.data.lights.new("F3_sun", 'SUN'); sun.energy = 3.5; so = bpy.data.objects.new("F3_sun", sun); sc.collection.objects.link(so)
so.rotation_euler = (math.radians(50), 0, math.radians(140))
if sc.world is None: sc.world = bpy.data.worlds.new("F3_world")
sc.world.use_nodes = True
bg = sc.world.node_tree.nodes.get("Background")
if bg: bg.inputs[0].default_value = (0.55, 0.65, 0.78, 1); bg.inputs[1].default_value = 0.8
cd = bpy.data.cameras.new("F3_cam"); cd.lens = 40; cam = bpy.data.objects.new("F3_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
VIEWS = {"rear": (Vector((1.9, 2.8, 2.1)), Vector((0, 0.15, 1.2))), "front": (Vector((-1.7, -2.9, 1.7)), Vector((0, 0.1, 1.15)))}
for tag, fn in VARIANTS:
    fn()
    for vn, (loc, tgt) in VIEWS.items():
        cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(OUT, "fin_%s_%s.png" % (tag, vn)); bpy.ops.render.render(write_still=True)
    for o in parts: bpy.data.objects.remove(o, do_unlink=True)
    parts.clear()
