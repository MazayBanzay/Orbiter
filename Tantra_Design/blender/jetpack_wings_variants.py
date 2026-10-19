# «Соты»: four wing styles (the user, 2026-10-16: in the game the wings came out bigger than in the mock-up and, folded,
# stood up into her neck; «один из них будет брать пример с крыльев Изиды») - mock-ups on the suited figure, each spread and
# folded. Renders only.   blender --background astronavigator_suit.blend --python jetpack_wings_variants.py
#   -> renders\wings_<A..D>_<open|fold>.png
# A «Классика»  the first mock-up's thin panels; folded: lowered behind her arms like a cape
# B «Изида»     long tapering wings, an arched leading edge, the cells in rows of "feathers" growing towards the tip;
#               folded: lowered and brought forward round her, as the goddess's wings
# C «Веер»      five rigid feather-slats spreading from the root as a fan; folded: closed into one slat behind her
# D «Чайка»     a gull wing: the inner panel raised, the outer lowered and swept; folded: the outer over the inner, lowered
# Blender frame: x her left, -y her front, z up; her back ~y 0.10..0.23; the shoulders ~z 1.40; the neck x |0.09|.
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
for o in bpy.data.objects:
    if o.name.startswith("Jet"): o.hide_render = True; o.hide_viewport = True

mats = {}
def mat(name, col, metal=0.2, rough=0.45):
    if name in mats: return mats[name]
    m = bpy.data.materials.new("JW_" + name); m.use_nodes = True
    bs = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bs.inputs["Base Color"].default_value = (*col, 1); bs.inputs["Metallic"].default_value = metal; bs.inputs["Roughness"].default_value = rough
    mats[name] = m; return m
WHITE = mat("white", (0.88, 0.89, 0.90), 0.1, 0.3); DARK = mat("dark", (0.04, 0.045, 0.05), 0.3, 0.45)
METAL = mat("metal", (0.62, 0.63, 0.65), 0.9, 0.28); ORANGE = mat("orange", (0.92, 0.42, 0.06), 0.1, 0.4)
GOLD = mat("gold", (0.85, 0.62, 0.25), 0.9, 0.3); NOZ = mat("ramp", (0.30, 0.22, 0.18), 0.9, 0.35)

parts = []
def obj(name, bm, m):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); parts.append(o); return o

def prism(name, m, c, r, h, n=6, rot=0.0):
    bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=n, radius1=r, radius2=r, depth=h)
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ Matrix.Rotation(rot, 4, 'Z'), verts=bm.verts); return obj(name, bm, m)

def core():   # the power unit, smaller than in the game: a slim hexagonal prism below the neck
    prism("core", WHITE, (0, 0.30, 1.22), 0.12, 0.40, 6, math.pi / 6)
    prism("core_top", DARK, (0, 0.30, 1.43), 0.085, 0.02, 6, math.pi / 6)
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Translation((0, 0.405, 1.30)) @ Matrix.Diagonal((0.10, 0.004, 0.02, 1)), verts=bm.verts); obj("band", bm, ORANGE)

def panel(name, outline, M, thick, cell_r, cell_rows=None, mat_panel=WHITE, frame=None):
    """a flat wing panel: outline [(u, v)] in the wing frame M (u span, v chord, n normal), extruded to thickness, its top
    and bottom filled with hexagonal cells inside the outline (a margin of one cell radius)"""
    bm = bmesh.new()
    top = [bm.verts.new(M @ Vector((u, v, thick / 2))) for u, v in outline]
    bot = [bm.verts.new(M @ Vector((u, v, -thick / 2))) for u, v in outline]
    bm.faces.new(top); bm.faces.new(bot[::-1])
    k = len(outline)
    for i in range(k): bm.faces.new((top[i], top[(i + 1) % k], bot[(i + 1) % k], bot[i]))
    obj(name, bm, mat_panel)
    # the cells: a honeycomb over the outline's bounding box, kept where the hexagon lies inside
    def inside(u, v):
        c = False
        for i in range(k):
            (u0, v0), (u1, v1) = outline[i], outline[(i + 1) % k]
            if (v0 > v) != (v1 > v) and u < (u1 - u0) * (v - v0) / (v1 - v0) + u0: c = not c
        return c
    us = [p[0] for p in outline]; vs = [p[1] for p in outline]
    dx = cell_r * math.sqrt(3) + 0.004; dz = 1.5 * cell_r + 0.004
    cells = bmesh.new(); rims = bmesh.new(); n = 0
    j = 0; v = min(vs) + cell_r
    while v < max(vs) - cell_r * 0.5:
        u = min(us) + cell_r + (dx / 2 if j % 2 else 0)
        while u < max(us):
            hexp = [(u + cell_r * math.cos(math.pi / 6 + a * math.pi / 3), v + cell_r * math.sin(math.pi / 6 + a * math.pi / 3)) for a in range(6)]
            if all(inside(pu, pv) for pu, pv in hexp):
                n += 1
                for side in (1, -1):
                    hin = [cells.verts.new(M @ Vector((u + 0.84 * (pu - u), v + 0.84 * (pv - v), side * (thick / 2 + 0.001)))) for pu, pv in hexp]
                    cells.faces.new(hin if side > 0 else hin[::-1])
                    hub = [cells.verts.new(M @ Vector((u + 0.18 * (pu - u), v + 0.18 * (pv - v), side * (thick / 2 + 0.003)))) for pu, pv in hexp]
                    hub_r = [rims.verts.new(M @ Vector((u + 0.18 * (pu - u), v + 0.18 * (pv - v), side * (thick / 2 + 0.004)))) for pu, pv in hexp]
                    rims.faces.new(hub_r if side > 0 else hub_r[::-1])
            u += dx
        v += dz; j += 1
    obj(name + "_cells", cells, DARK); obj(name + "_hubs", rims, METAL)
    if frame:   # a metal edge along the outline's leading part (indices)
        e = bmesh.new()
        for i in frame:
            (u0, v0), (u1, v1) = outline[i], outline[(i + 1) % k]
            q = [e.verts.new(M @ Vector(p)) for p in ((u0, v0, thick / 2 + 0.002), (u1, v1, thick / 2 + 0.002), (u1, v1, -thick / 2 - 0.002), (u0, v0, -thick / 2 - 0.002))]
            e.faces.new(q)
        obj(name + "_edge", e, METAL)
    return n

def spike(name, M, u, v0, v1, thick):   # the aerospike at a tip: a wedge pod along the chord, its ramp below
    bm = bmesh.new()
    a = [M @ Vector(p) for p in ((u, v0, thick / 2 + 0.01), (u + 0.05, v0, thick / 2 + 0.01), (u + 0.05, v1, thick / 2 + 0.01), (u, v1, thick / 2 + 0.01),
                                 (u, v0, -thick / 2 - 0.01), (u + 0.05, v0, -thick / 2 - 0.01), (u + 0.05, v1, -thick / 2 - 0.01), (u, v1, -thick / 2 - 0.01))]
    vs = [bm.verts.new(p) for p in a]
    for f in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)): bm.faces.new([vs[i] for i in f])
    obj(name, bm, WHITE)
    r = bmesh.new()
    b = [M @ Vector(p) for p in ((u, v0, -thick / 2 - 0.01), (u + 0.05, v0, -thick / 2 - 0.01), (u + 0.025, v0, -thick / 2 - 0.07),
                                 (u, v1, -thick / 2 - 0.01), (u + 0.05, v1, -thick / 2 - 0.01), (u + 0.025, v1, -thick / 2 - 0.07))]
    w = [r.verts.new(p) for p in b]
    for f in ((0, 1, 2), (5, 4, 3), (0, 2, 5, 3), (1, 4, 5, 2)): r.faces.new([w[i] for i in f])
    obj(name + "_ramp", r, NOZ)

def frame(root, side, yaw=0.0, dihedral=0.0, sweep=0.0, droop=0.0, roll=0.0):
    """the wing frame at the root: u out along the span (her side), v back along the chord, n up; turned by the dihedral
    (about the chord), the sweep (about up), the droop (folding: about the chord, down), the yaw (forward round her)"""
    R = Matrix.Rotation(side * yaw, 4, 'Z') @ Matrix.Rotation(-side * (dihedral - droop), 4, 'Y') @ Matrix.Rotation(side * sweep, 4, 'Z') @ Matrix.Rotation(roll, 4, 'X')
    S = Matrix.Diagonal((side, 1, 1, 1))   # u towards her side: +x for her left (side 1)
    return Matrix.Translation(Vector(root)) @ R @ S

def hinge(side, root):
    prism("hinge%d" % side, DARK, root, 0.035, 0.08, 8)

ROOT = lambda s: (s * 0.13, 0.34, 1.36)        # below the neck, behind the shoulder blades
def A(folded):
    core()
    for s in (1, -1):
        M = frame(ROOT(s), s, dihedral=math.radians(10), droop=math.radians(100) if folded else 0, yaw=math.radians(-6) if folded else 0, roll=math.radians(90) if folded else 0)
        out = [(0.0, -0.18), (0.70, -0.16), (0.76, -0.10), (0.76, 0.16), (0.0, 0.20)]
        panel("A%d" % s, out, M, 0.035, 0.045, frame=[0, 1, 2]); spike("Asp%d" % s, M, 0.76, -0.08, 0.14, 0.035); hinge(s, ROOT(s))
def B(folded):
    core()
    for s in (1, -1):
        M = frame(ROOT(s), s, dihedral=math.radians(14), sweep=math.radians(12), droop=math.radians(96) if folded else 0, yaw=math.radians(-24) if folded else 0, roll=math.radians(90) if folded else 0)
        # the arm of the wing: an arched leading edge; three rows of feathers longer towards the tip, the primaries at the end
        arm = [(0.0, -0.16), (0.30, -0.20), (0.62, -0.17), (0.92, -0.08), (0.98, 0.0), (0.0, 0.0)]
        panel("Barm%d" % s, arm, M, 0.03, 0.035, mat_panel=WHITE, frame=[0, 1, 2, 3])
        for r, (v0, v1, l0, l1) in enumerate(((0.0, 0.11, 0.10, 0.85), (0.11, 0.22, 0.15, 0.70), (0.22, 0.34, 0.25, 0.52))):
            nf = 6 - r
            for i in range(nf):
                u0 = l0 + (l1 - l0) * i / nf; u1 = u0 + (l1 - l0) / nf - 0.012
                tip = 0.06 * (i + 1) / nf
                feather = [(u0, v0), (u1, v0), (u1 + 0.02, v1 + tip), (u0 + 0.02, v1 + tip - 0.03)]
                panel("Bf%d_%d_%d" % (s, r, i), feather, M @ Matrix.Translation((0, 0, -0.004 * r)), 0.02, 0.026, mat_panel=WHITE if r != 1 else WHITE)
            # the gold of the canon: a thin band at each row's root
        e = bmesh.new()
        for v0 in (0.0, 0.11, 0.22):
            q = [e.verts.new(M @ Vector(p)) for p in ((0.08, v0 - 0.006, 0.018), (0.86 - 0.12 * v0 * 4, v0 - 0.006, 0.018), (0.86 - 0.12 * v0 * 4, v0 + 0.006, 0.018), (0.08, v0 + 0.006, 0.018))]
            e.faces.new(q)
        obj("Bgold%d" % s, e, GOLD)
        spike("Bsp%d" % s, M, 0.98, -0.07, 0.0, 0.03); hinge(s, ROOT(s))
def C(folded):
    core()
    for s in (1, -1):
        for i in range(5):
            spread = math.radians(-26 + 13 * i) if not folded else 0.0
            M = frame(ROOT(s), s, dihedral=math.radians(8), sweep=spread, droop=math.radians(100) if folded else 0, yaw=math.radians(-6) if folded else 0, roll=math.radians(90) if folded else 0)
            M = M @ Matrix.Translation((0, 0, 0.005 * i))
            L = 0.80 - 0.05 * abs(i - 2)
            slat = [(0.02, -0.055), (L - 0.06, -0.05), (L, 0.0), (L - 0.06, 0.05), (0.02, 0.055)]
            panel("C%d_%d" % (s, i), slat, M, 0.025, 0.028, frame=[0, 1])
        Mt = frame(ROOT(s), s, dihedral=math.radians(8), droop=math.radians(100) if folded else 0, yaw=math.radians(-6) if folded else 0, roll=math.radians(90) if folded else 0)
        spike("Csp%d" % s, Mt, 0.80, -0.04, 0.04, 0.025); hinge(s, ROOT(s))
def D(folded):
    core()
    for s in (1, -1):
        M1 = frame(ROOT(s), s, dihedral=math.radians(22), droop=math.radians(100) if folded else 0, yaw=math.radians(-6) if folded else 0, roll=math.radians(90) if folded else 0)
        inner = [(0.0, -0.17), (0.38, -0.15), (0.38, 0.19), (0.0, 0.21)]
        panel("Di%d" % s, inner, M1, 0.035, 0.04, frame=[0, 1])
        J = M1 @ Matrix.Translation((0.38, 0, 0))
        # the outer panel in the inner's frame (already mirrored for her side): lowered 36 deg and swept 18 deg spread; folded back
        # over the inner panel's top (-160 deg about the chord)
        M2 = J @ Matrix.Rotation(math.radians(-160) if folded else math.radians(36), 4, 'Y') @ Matrix.Rotation(math.radians(18), 4, 'Z')
        outer = [(0.0, -0.15), (0.42, -0.10), (0.46, -0.05), (0.46, 0.10), (0.0, 0.19)]
        panel("Do%d" % s, outer, M2, 0.03, 0.04, frame=[0, 1, 2]); spike("Dsp%d" % s, M2, 0.46, -0.04, 0.09, 0.03); hinge(s, ROOT(s))
        prism("Dj%d" % s, DARK, J @ Vector((0, 0, 0)), 0.03, 0.05, 8)

VARIANTS = [("A", A), ("B", B), ("C", C), ("D", D)]
sc = bpy.context.scene
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x, sc.render.resolution_y = 800, 800
fl = bpy.data.meshes.new("JW_floor"); fl.from_pydata([(-6, -6, 0), (6, -6, 0), (6, 6, 0), (-6, 6, 0)], [], [(0, 1, 2, 3)])
fo = bpy.data.objects.new("JW_floor", fl); sc.collection.objects.link(fo); fl.materials.append(mat("floor", (0.45, 0.43, 0.40), 0, 0.8))
sun = bpy.data.lights.new("JW_sun", 'SUN'); sun.energy = 3.5; so = bpy.data.objects.new("JW_sun", sun); sc.collection.objects.link(so)
so.rotation_euler = (math.radians(50), 0, math.radians(140))
if sc.world is None: sc.world = bpy.data.worlds.new("JW_world")
sc.world.use_nodes = True
bg = sc.world.node_tree.nodes.get("Background")
if bg: bg.inputs[0].default_value = (0.55, 0.65, 0.78, 1); bg.inputs[1].default_value = 0.8
cd = bpy.data.cameras.new("JW_cam"); cd.lens = 38; cam = bpy.data.objects.new("JW_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
loc, tgt = Vector((1.9, 2.9, 2.2)), Vector((0, 0.15, 1.2))
cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
for tag, fn in VARIANTS:
    for folded in (False, True):
        fn(folded)
        sc.render.filepath = os.path.join(OUT, "wings_%s_%s.png" % (tag, "fold" if folded else "open"))
        bpy.ops.render.render(write_still=True)
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
