# Ten atmospheric jet pack concepts (the user, 2026-10-16: a controllable, safe complex for Earth, Titan and the upper
# Venus; a vectored boost as on «Наплечный»; no single fatal failure; 2-3 honeycomb wings and a «Стрекоза» among them).
# Mock-ups on the suited figure, renders only:  blender --background astronavigator_suit.blend --python jetpack_10.py
#   -> renders\p10_<NN>.png
# Blender frame: x her left, -y her front, z up; her back ~y 0.10..0.23; shoulders ~z 1.40; hands ~(+-0.45, -0.18, 1.06).
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
for o in bpy.data.objects:
    if o.name.startswith("Jet"): o.hide_render = True; o.hide_viewport = True

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

# ---------------- the ten ----------------
def p01():   # «Вектор»: two coaxial ducted modules on gimbals at the shoulders, the boost in each hub
    core()
    for s in (1, -1):
        rod("b%d" % s, METAL, (s * 0.11, 0.30, 1.42), (s * 0.36, 0.06, 1.42), 0.03)
        box("y%d" % s, DARK, (s * 0.38, 0.06, 1.42), (0.03, 0.10, 0.10))
        vec_pod("v%d" % s, (s * 0.62, 0.06, 1.42), 0.26, 0.30)
def p02():   # «Стрекоза»: four slim honeycomb wings, the fore and the hind pair, each on its own pivot
    core()
    for s in (1, -1):
        for nm, root, span, ch, sw, dh in (("f", (s * 0.12, 0.30, 1.44), 0.88, 0.15, -6, 8), ("h", (s * 0.11, 0.32, 1.16), 0.74, 0.14, 14, 2)):
            M = wframe(root, s, math.radians(dh), math.radians(sw))
            out = [(0.0, -ch / 2), (span - 0.05, -ch / 2 + 0.01), (span, 0.0), (span - 0.05, ch / 2), (0.0, ch / 2)]
            honey("w%s%d" % (nm, s), out, M, 0.022, 0.026, [0, 1])
            prism("pv%s%d" % (nm, s), DARK, root, 0.03, 0.06, 8, axis=(s, 0, 0))
            bell("bs%s%d" % (nm, s), M @ Vector((span + 0.02, 0, -0.02)), (0, 0, -1), 0.7)
def p03():   # «Изида»: long tapering feathered honeycomb wings
    core()
    for s in (1, -1):
        M = wframe((s * 0.13, 0.34, 1.36), s, math.radians(14), math.radians(12))
        honey("arm%d" % s, [(0.0, -0.16), (0.30, -0.20), (0.62, -0.17), (0.92, -0.08), (0.98, 0.0), (0.0, 0.0)], M, 0.03, 0.035, [0, 1, 2, 3])
        for r, (v0, v1, l0, l1) in enumerate(((0.0, 0.11, 0.10, 0.85), (0.11, 0.22, 0.15, 0.70), (0.22, 0.34, 0.25, 0.52))):
            nf = 6 - r
            for i in range(nf):
                u0 = l0 + (l1 - l0) * i / nf; u1 = u0 + (l1 - l0) / nf - 0.012; tip = 0.06 * (i + 1) / nf
                honey("f%d%d%d" % (s, r, i), [(u0, v0), (u1, v0), (u1 + 0.02, v1 + tip), (u0 + 0.02, v1 + tip - 0.03)], M @ Matrix.Translation((0, 0, -0.004 * r)), 0.02, 0.026)
        bell("bi%d" % s, M @ Vector((1.0, -0.04, -0.02)), (0, 0, -1), 0.8)
def p04():   # «Соты-К»: straight thin honeycomb wings with vectored tip modules
    core()
    for s in (1, -1):
        M = wframe((s * 0.13, 0.34, 1.36), s, math.radians(8))
        honey("k%d" % s, [(0.0, -0.17), (0.66, -0.15), (0.66, 0.15), (0.0, 0.19)], M, 0.03, 0.04, [0, 1])
        vec_pod("t%d" % s, M @ Vector((0.78, 0.0, 0.0)), 0.13, 0.20)
def p05():   # «Соты-блоки»: two hexagonal honeycomb blocks on gimbals at the shoulder blades
    core()
    for s in (1, -1):
        c = Vector((s * 0.46, 0.22, 1.42))
        rod("a%d" % s, METAL, (s * 0.11, 0.30, 1.40), c + Vector((-s * 0.20, 0, 0)), 0.03)
        hexo = [(0.24 * math.cos(math.pi / 6 + a * math.pi / 3), 0.24 * math.sin(math.pi / 6 + a * math.pi / 3)) for a in range(6)]
        honey("blk%d" % s, hexo, Matrix.Translation(c), 0.09, 0.034, list(range(6)))
        bell("bb%d" % s, c + Vector((0, 0, -0.08)), (0, 0, -1), 0.9)
def p06():   # «Нимб»: one large coaxial duct over her head on a mast, the vanes for the vector
    core(h=0.44)
    rod("mast", METAL, (0, 0.34, 1.44), (0, 0.34, 1.95), 0.035)
    vec_pod("n", (0, 0.10, 2.02), 0.44, 0.26, boost=False)
    for s in (1, -1): bell("nb%d" % s, (s * 0.20, 0.10, 1.86), (0, 0, -1), 0.8)
def p07():   # «Наплечный-А»: the family's pods made air-breathing - hydrogen ram-ejector shrouds on gimbals
    core()
    for s in (1, -1):
        rod("b%d" % s, METAL, (s * 0.11, 0.28, 1.46), (s * 0.52, 0.18, 1.46), 0.028)
        duct("sh%d" % s, WHITE, (s * 0.60, 0.18, 1.30), 0.12, 0.10, 0.60, axis=(0, 0, 1))
        prism("lip%d" % s, METAL, (s * 0.60, 0.18, 1.61), 0.12, 0.03, 6, rot=math.pi / 6, r2=0.15, cap=False)
        prism("jet%d" % s, NOZ, (s * 0.60, 0.18, 1.52), 0.03, 0.12, 16, r2=0.015)
        bell("eb%d" % s, (s * 0.60, 0.18, 0.97), (0, 0, -1), 0.8)
def p08():   # «Трилистник»: three vectored ducts - two at the shoulders, one low behind her
    core()
    for s in (1, -1):
        rod("b%d" % s, METAL, (s * 0.11, 0.30, 1.42), (s * 0.34, 0.10, 1.42), 0.03)
        vec_pod("v%d" % s, (s * 0.55, 0.10, 1.42), 0.21, 0.24)
    rod("lb", METAL, (0, 0.32, 1.06), (0, 0.52, 1.02), 0.03)
    vec_pod("vl", (0, 0.72, 1.02), 0.21, 0.24)
def p09():   # «Крыло-вектор»: a wing in the plane of her back with two vectored ducts at its root (hover and cruise)
    core(z=1.26, h=0.36)
    bm = bmesh.new()
    for s in (1, -1):
        quad = [Vector((0, 0, 1.55)), Vector((s * 0.95, 0, 1.55 - 0.42)), Vector((s * 0.95, 0, 1.31)), Vector((0, 0, 0.93))]
        for dy in (-0.015, 0.015): bm.faces.new([bm.verts.new((q.x, 0.36 + dy, q.z)) for q in quad])
    obj("wing", bm, WHITE)
    for s in (1, -1):
        box("wl%d" % s, ORANGE, (s * 0.95, 0.43, 1.21), (0.01, 0.14, 0.22))
        vec_pod("wv%d" % s, (s * 0.36, 0.56, 1.22), 0.16, 0.22)
def p10():   # «Рукава»: a dorsal coaxial duct and two small vectored cell pods on her forearms (steered by her hands)
    core(h=0.36, z=1.16)
    rod("m", METAL, (0, 0.32, 1.30), (0, 0.42, 1.34), 0.03)
    vec_pod("d", (0, 0.52, 1.30), 0.30, 0.26)
    for s in (1, -1):
        c = Vector((s * 0.48, -0.16, 1.02))
        duct("arm%d" % s, WHITE, c, 0.075, 0.062, 0.16, axis=(0, 0, 1))
        rotor("ar%d" % s, c, (0, 0, 1), 0.015, 0.06, 6, 0.5)
        bell("ab%d" % s, c + Vector((0, 0, -0.10)), (0, 0, -1), 0.5)

VARIANTS = [p01, p02, p03, p04, p05, p06, p07, p08, p09, p10]
sc = bpy.context.scene
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x, sc.render.resolution_y = 760, 760
fl = bpy.data.meshes.new("P10_floor"); fl.from_pydata([(-6, -6, 0), (6, -6, 0), (6, 6, 0), (-6, 6, 0)], [], [(0, 1, 2, 3)])
fo = bpy.data.objects.new("P10_floor", fl); sc.collection.objects.link(fo); fl.materials.append(mat("floor", (0.45, 0.43, 0.40), 0, 0.8))
sun = bpy.data.lights.new("P10_sun", 'SUN'); sun.energy = 3.5; so = bpy.data.objects.new("P10_sun", sun); sc.collection.objects.link(so)
so.rotation_euler = (math.radians(50), 0, math.radians(140))
if sc.world is None: sc.world = bpy.data.worlds.new("P10_world")
sc.world.use_nodes = True
bg = sc.world.node_tree.nodes.get("Background")
if bg: bg.inputs[0].default_value = (0.55, 0.65, 0.78, 1); bg.inputs[1].default_value = 0.8
cd = bpy.data.cameras.new("P10_cam"); cd.lens = 34; cam = bpy.data.objects.new("P10_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
loc, tgt = Vector((2.1, 3.1, 2.4)), Vector((0, 0.15, 1.25))
cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
for i, fn in enumerate(VARIANTS, 1):
    fn()
    sc.render.filepath = os.path.join(OUT, "p10_%02d.png" % i)
    bpy.ops.render.render(write_still=True)
    for o in parts: bpy.data.objects.remove(o, do_unlink=True)
    parts.clear()
