# Five jet pack concepts for Earth-like air (the user, 2026-10-16: hydrogen, no reactor on the back; the Andromeda era -
# no drones): mock-ups on the suited figure, renders only (nothing for the game).
#   blender --background astronavigator_suit.blend --python jetpack_variants.py  -> renders\jetpack_v<1..5>_<view>.png
# 1 «Вихрь-И»  two ducted impellers in hexagonal fairings, a metallic hydrogen micro-turbine in the core, boost pods
# 2 «Соты»     honeycomb wings: two folding panels of hexagonal cells (electric micro-impellers), hydrogen power cell
# 3 «Струя»    two hydrogen turbofans in hexagonal nacelles beside the torso (air-breathing; the rocket mode in vacuum)
# 4 «Эжектор»  two long hexagonal ejector shrouds round metallic hydrogen jets (no moving parts; poor at a hover)
# 5 «Крыло»    a jet wing (span 1.9 m) with two hydrogen turbojets: flies on its wing, hovers only briefly
# Blender frame: x her left, -y her front, z up; her back is at y ~0.10..0.23 (the life support pack).
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)

for o in bpy.data.objects:
    if o.name.startswith("Jet"): o.hide_render = True; o.hide_viewport = True
mats = {}
def mat(name, col, metal=0.2, rough=0.45, emit=0.0):
    if name in mats: return mats[name]
    m = bpy.data.materials.new("JV_" + name); m.use_nodes = True
    bs = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bs.inputs["Base Color"].default_value = (*col, 1); bs.inputs["Metallic"].default_value = metal; bs.inputs["Roughness"].default_value = rough
    if emit > 0:
        bs.inputs["Emission Color"].default_value = (*col, 1); bs.inputs["Emission Strength"].default_value = emit
    mats[name] = m; return m
WHITE = mat("white", (0.86, 0.87, 0.88), 0.1, 0.35); DARK = mat("dark", (0.05, 0.055, 0.06), 0.3, 0.5)
METAL = mat("metal", (0.55, 0.56, 0.58), 0.9, 0.3); ORANGE = mat("orange", (0.92, 0.42, 0.06), 0.1, 0.4)
NOZ = mat("nozzle", (0.28, 0.20, 0.16), 0.9, 0.35); GLOW = mat("glow", (0.55, 0.65, 1.0), 0.0, 0.3, 3.0)
GLASS = mat("cell", (0.10, 0.12, 0.14), 0.6, 0.25)

parts = []
def obj(name, bm, m):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    for p in me.polygons: p.use_smooth = False
    parts.append(o); return o

def prism(name, m, c, r, h, n=6, axis=(0, 0, 1), rot=0.0, r2=None, cap=True):
    """an n-gon prism (n=6: hexagonal) centred at c along axis, circumradius r (r2 at the far end: a frustum)"""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=cap, segments=n, radius1=r, radius2=r if r2 is None else r2, depth=h)
    R = Vector((0, 0, 1)).rotation_difference(Vector(axis)).to_matrix().to_4x4() @ Matrix.Rotation(rot, 4, 'Z')
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ R, verts=bm.verts)
    return obj(name, bm, m)

def tube(name, m, c, ro, ri, h, n=6, axis=(0, 0, 1), rot=0.0, n_in=None):
    """an open duct: a hexagonal (n) outer skin round a round bore, the end rims between them - one ring of points on the
    hexagon along each bore direction, so the rims are plain quads"""
    bm = bmesh.new()
    k = n_in or 36
    def hexr(a):                                       # the distance from the centre to the n-gon's side along angle a
        sect = 2 * math.pi / n
        aa = (a - rot) % sect - sect / 2
        return ro * math.cos(math.pi / n) / math.cos(aa)
    rings = []
    for z in (-h / 2, h / 2):
        outer = [bm.verts.new((hexr(2 * math.pi * i / k) * math.cos(2 * math.pi * i / k), hexr(2 * math.pi * i / k) * math.sin(2 * math.pi * i / k), z)) for i in range(k)]
        inner = [bm.verts.new((ri * math.cos(2 * math.pi * i / k), ri * math.sin(2 * math.pi * i / k), z)) for i in range(k)]
        rings.append((outer, inner))
    (o0, i0), (o1, i1) = rings
    for q in range(k):
        r = (q + 1) % k
        bm.faces.new((o0[q], o0[r], o1[r], o1[q]))        # the outer skin
        bm.faces.new((i0[r], i0[q], i1[q], i1[r]))        # the bore
        bm.faces.new((o0[r], o0[q], i0[q], i0[r]))        # the exit rim
        bm.faces.new((o1[q], o1[r], i1[r], i1[q]))        # the intake rim
    R = Vector((0, 0, 1)).rotation_difference(Vector(axis)).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ R, verts=bm.verts)
    o = obj(name, bm, m)
    for f in o.data.polygons: f.use_smooth = False
    return o

def box(name, m, c, s, rot=(0, 0, 0)):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    M = Matrix.Translation(Vector(c)) @ Matrix.Rotation(rot[2], 4, 'Z') @ Matrix.Rotation(rot[1], 4, 'Y') @ Matrix.Rotation(rot[0], 4, 'X') @ Matrix.Diagonal((*s, 1))
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts); return obj(name, bm, m)

def rod(name, m, a, b, r, n=8):
    a, b = Vector(a), Vector(b); d = b - a
    return prism(name, m, (a + b) / 2, r, d.length, n, axis=d.normalized())

def blades(name, m, c, r0, r1, z, k=9, pitch=0.5):
    bm = bmesh.new()
    for i in range(k):
        a = 2 * math.pi * i / k
        u = Vector((math.cos(a), math.sin(a), 0)); t = Vector((-math.sin(a), math.cos(a), 0))
        ch = 0.045; cv = t * math.cos(pitch) * ch + Vector((0, 0, math.sin(pitch) * ch))
        p = [Vector(c) + u * r0 - cv, Vector(c) + u * r1 - cv, Vector(c) + u * r1 + cv, Vector(c) + u * r0 + cv]
        vs = [bm.verts.new((q.x, q.y, z + q.z - c[2])) for q in p]; bm.faces.new(vs)
    return obj(name, bm, m)

def clear():
    for o in parts: bpy.data.objects.remove(o, do_unlink=True)
    parts.clear()

def core(y=0.30, z=1.28, h=0.44, r=0.14):           # the hydrogen core behind the life support pack: tank + power
    prism("core", WHITE, (0, y, z), r, h, 6, rot=math.pi / 6)
    prism("core_top", DARK, (0, y, z + h / 2 + 0.01), r * 0.7, 0.03, 6, rot=math.pi / 6)
    box("core_band", ORANGE, (0, y + r * 0.86, z + 0.12), (0.12, 0.004, 0.025))

def boost_pod(tag, c, axis=(0, 0, -1), s=1.0):   # the boost nozzle in the family of «Наплечный»: a short pod with a bell
    a = Vector(axis).normalized()
    prism(tag + "_pod", METAL, Vector(c) - a * 0.05 * s, 0.045 * s, 0.10 * s, 6, axis=a)
    prism(tag + "_bell", NOZ, Vector(c) + a * 0.04 * s, 0.03 * s, 0.08 * s, 16, axis=a, r2=0.055 * s, cap=False)

# ---------------- the five variants ----------------
def v1():   # impellers in hexagonal fairings beside her shoulders
    core()
    for s in (1, -1):
        cx = s * 0.64; c = (cx, 0.06, 1.42)
        rod("boom%d" % s, METAL, (s * 0.12, 0.30, 1.47), (s * 0.30, 0.06, 1.44), 0.03)
        tube("duct%d" % s, WHITE, c, 0.36, 0.285, 0.26, 6, rot=math.pi / 6)
        prism("hub%d" % s, DARK, (cx, 0.06, 1.42), 0.075, 0.20, 16)
        blades("rot%d" % s, DARK, c, 0.075, 0.28, 1.47, 9, 0.45)
        for k in range(3): box("strut%d%d" % (s, k), DARK, (cx, 0.06, 1.34), (0.56, 0.012, 0.03), (0, 0, k * math.pi / 3))
        box("hexband%d" % s, ORANGE, (cx + s * 0.311, 0.06, 1.50), (0.004, 0.36, 0.025))
        boost_pod("b%d" % s, (cx, 0.06, 1.22))

def v2():   # honeycomb wings, folded out and up 12 deg
    core(y=0.30, z=1.30)
    for s in (1, -1):
        ang = s * math.radians(-12)
        root = Vector((s * 0.12, 0.27, 1.40))
        M = Matrix.Translation(root) @ Matrix.Rotation(-ang, 4, 'Y')
        w, d = 0.78, 0.44
        bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
        bmesh.ops.transform(bm, matrix=M @ Matrix.Translation((s * w / 2, 0, 0)) @ Matrix.Diagonal((w, d, 0.07, 1)), verts=bm.verts)
        o = obj("wing%d" % s, bm, WHITE)
        # the cells: hexagonal rings in a honeycomb on the top face, dark discs with blades inside
        rc = 0.052; dx = rc * 1.75; dy = rc * 1.52
        for i in range(-1, 9):
            for j in range(-2, 3):
                x = 0.05 + i * dx + (dy * 0 if j % 2 == 0 else dx / 2); y = j * dy
                if x < 0.03 or x > w - 0.04 or abs(y) > d / 2 - 0.04: continue
                p = M @ Vector((s * x, y, 0.036))
                prism("cell", DARK, p, rc * 0.92, 0.012, 6, axis=(M.to_3x3() @ Vector((0, 0, 1))), rot=0)
                prism("cellrim", METAL, p + (M.to_3x3() @ Vector((0, 0, 0.006))), rc, 0.006, 6, axis=(M.to_3x3() @ Vector((0, 0, 1))))
        boost_pod("b%d" % s, M @ Vector((s * (w + 0.02), 0, -0.02)))
        box("hinge%d" % s, DARK, root, (0.06, 0.10, 0.06))

def v3():   # hydrogen turbofans in hexagonal nacelles beside the torso
    core(h=0.40)
    for s in (1, -1):
        cx = s * 0.40; c = (cx, 0.16, 1.24)
        rod("arm%d" % s, METAL, (s * 0.13, 0.30, 1.36), (s * 0.30, 0.16, 1.30), 0.03)
        tube("nac%d" % s, WHITE, c, 0.15, 0.12, 0.62, 6, rot=math.pi / 6)
        blades("fan%d" % s, DARK, (cx, 0.16, 1.50), 0.04, 0.12, 1.50, 14, 0.6)
        prism("spin%d" % s, METAL, (cx, 0.16, 1.52), 0.04, 0.06, 16, r2=0.005)
        prism("core%d" % s, DARK, (cx, 0.16, 1.18), 0.06, 0.40, 16)
        prism("noz%d" % s, NOZ, (cx, 0.16, 0.90), 0.10, 0.10, 6, r2=0.075, rot=math.pi / 6)
        box("band%d" % s, ORANGE, (cx, 0.16 - 0.13, 1.40), (0.13, 0.004, 0.025))

def v4():   # long hexagonal ejector shrouds round metallic hydrogen jets
    core(h=0.40)
    for s in (1, -1):
        cx = s * 0.36; c = (cx, 0.18, 1.12)
        rod("arm%d" % s, METAL, (s * 0.13, 0.30, 1.40), (s * 0.20, 0.18, 1.42), 0.03)
        tube("shroud%d" % s, WHITE, c, 0.19, 0.16, 0.95, 6, rot=math.pi / 6)
        prism("lip%d" % s, METAL, (cx, 0.18, 1.61), 0.20, 0.03, 6, rot=math.pi / 6, r2=0.23, cap=False)
        prism("jet%d" % s, NOZ, (cx, 0.18, 1.50), 0.035, 0.16, 16, r2=0.02)
        box("band%d" % s, ORANGE, (cx, 0.18 - 0.165, 1.40), (0.16, 0.004, 0.03))

def v5():   # a jet wing on her back, in the plane of her back (prone in flight: the head forward, the wing flat);
            # swept towards her feet, two hydrogen turbojets under it blowing past her legs
    core(z=1.26, h=0.36)
    bm = bmesh.new()
    sweep = math.radians(24); span = 0.95; rootc = 0.62; tipc = 0.24; y0 = 0.36; ztop = 1.55
    for s_ in (1, -1):
        quad = [Vector((0, 0, ztop)), Vector((s_ * span, 0, ztop - span * math.tan(sweep))),
                Vector((s_ * span, 0, ztop - span * math.tan(sweep) - tipc)), Vector((0, 0, ztop - rootc))]
        for dy in (-0.02, 0.02):
            vs = [bm.verts.new((q.x, y0 + dy, q.z)) for q in quad]
            bm.faces.new(vs)
    obj("wing", bm, WHITE)
    for s_ in (1, -1):
        tz = ztop - span * math.tan(sweep) - tipc / 2
        box("winglet%d" % s_, ORANGE, (s_ * span, y0 + 0.08, tz), (0.012, 0.16, tipc))
        c = Vector((s_ * 0.40, y0 + 0.10, 1.05))
        prism("jet%d" % s_, WHITE, c, 0.075, 0.46, 6, rot=math.pi / 6)
        prism("jint%d" % s_, DARK, c + Vector((0, 0, 0.235)), 0.06, 0.01, 16)
        prism("jnoz%d" % s_, NOZ, c + Vector((0, 0, -0.27)), 0.05, 0.08, 16, r2=0.04, cap=False)

VARIANTS = [("1", v1), ("2", v2), ("3", v3), ("4", v4), ("5", v5)]

# ---------------- the stage ----------------
sc = bpy.context.scene
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x, sc.render.resolution_y = 900, 900
fl = bpy.data.meshes.new("JV_floor"); fl.from_pydata([(-6, -6, 0), (6, -6, 0), (6, 6, 0), (-6, 6, 0)], [], [(0, 1, 2, 3)])
fo = bpy.data.objects.new("JV_floor", fl); sc.collection.objects.link(fo); fl.materials.append(mat("floor", (0.45, 0.43, 0.40), 0, 0.8))
sun = bpy.data.lights.new("JV_sun", 'SUN'); sun.energy = 3.5; so = bpy.data.objects.new("JV_sun", sun); sc.collection.objects.link(so)
so.rotation_euler = (math.radians(50), 0, math.radians(140))
if sc.world is None: sc.world = bpy.data.worlds.new("JV_world")
sc.world.use_nodes = True
bg = sc.world.node_tree.nodes.get("Background")
if bg: bg.inputs[0].default_value = (0.55, 0.65, 0.78, 1); bg.inputs[1].default_value = 0.8
cd = bpy.data.cameras.new("JV_cam"); cd.lens = 40; cam = bpy.data.objects.new("JV_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
VIEWS = {"rear": (Vector((2.2, 2.8, 2.6)), Vector((0, 0.15, 1.15))), "front": (Vector((-1.9, -3.2, 1.6)), Vector((0, 0.1, 1.1)))}
for tag, fn in VARIANTS:
    fn()
    for vname, (loc, tgt) in VIEWS.items():
        cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(OUT, "jetpack_v%s_%s.png" % (tag, vname))
        bpy.ops.render.render(write_still=True)
    clear()
