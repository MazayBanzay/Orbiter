# The jet pack worn over the suit's back block (the user, 2026-10-18: «форма неадекватна задачам; сопла и механика остаются;
# подключается к основному блоку скафандра и одевается поверх; блок - источник питания; 10 концептов; РСУ водородные»).
# The approved block (suit_back_block.py, SB_BUILD_ONLY), the booms / telescope / twin-chamber capsules / hinge pins from the
# game mesh (jetpack_hexa_build.py), and ten pack bodies: each docks on the block's four pads and connector, holds the
# metallic hydrogen (13 kg = 11.3 L; ~13 L with its walls), carries the hinge pins and four hydrogen RCS quads.
# Measured per concept: the tank volume, the depth behind her back, how much of the lid's radiator it covers.
#   blender -b astronavigator_suit.blend --python jetpack_onblock.py -> renders\pob_<n>_<view>.png, pob_metrics.json
import bpy, bmesh, math, os, json
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "pob_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
try:
    G = {"__name__": "x", "SB_BUILD_ONLY": True, "SB_SUBSURF": 1}
    exec(open(os.path.join(HERE, "suit_back_block.py"), encoding="utf-8").read(), G)
    for o in G["inside"]: bpy.data.objects.remove(o, do_unlink=True)
    outer_y = G["outer_y"]
    print("block built", file=LOG); LOG.flush()
    exec(open(os.path.join(HERE, "jetpack_hexa_build.py"), encoding="utf-8").read(), globals())
except Exception:
    import traceback; traceback.print_exc(file=LOG); LOG.close(); raise

def mk(name, col, metal=0.1, rough=0.35):
    m = bpy.data.materials.new("POB_" + name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
    return m
WHITE = mk("white", (0.86, 0.87, 0.88)); RED = mk("red", (0.78, 0.09, 0.07)); DARK = mk("dark", (0.05, 0.055, 0.06), 0.3, 0.5)
METAL = mk("metal", (0.55, 0.56, 0.58), 0.9, 0.3); GREY = mk("grey", (0.30, 0.31, 0.33), 0.3, 0.4)
parts, tanks = [], []

def obj(name, bm, m, smooth=True, tank=False):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    for p in me.polygons: p.use_smooth = smooth
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); parts.append(o)
    if tank: tanks.append(o)
    return o

GAP = 0.006
def oy(x, z):
    """the block's outer surface behind (x, z): the upper part's, clamped to its edges"""
    return outer_y(max(-0.185, min(0.185, x)), max(1.215, min(1.475, z)), "up")

def conformal(name, x0, x1, z0, z1, t, m, tank=False, nx=24, nz=16, round_=0.35, surf=None, gap=GAP):
    """a shell on the block's lid (or on surf): inner face gap off it, thickness t(x, z) (or a number), its rim rounded"""
    surf = surf or oy
    T = t if callable(t) else (lambda x, z, v=t: v)
    bm = bmesh.new(); rows_i, rows_o = [], []
    for j in range(nz + 1):
        z = z0 + (z1 - z0) * j / nz; ri, ro = [], []
        for i in range(nx + 1):
            x = x0 + (x1 - x0) * i / nx
            s = min(i, nx - i) / (nx / 2); q = min(j, nz - j) / (nz / 2)
            edge = min(1.0, (min(s, q) / max(round_, 1e-3)) ** 0.5)
            y0 = surf(x, z) + gap; ri.append(bm.verts.new((x, y0, z))); ro.append(bm.verts.new((x, y0 + max(0.003, T(x, z) * edge), z)))
        rows_i.append(ri); rows_o.append(ro)
    for j in range(nz):
        for i in range(nx):
            bm.faces.new((rows_o[j][i], rows_o[j][i + 1], rows_o[j + 1][i + 1], rows_o[j + 1][i]))
            bm.faces.new((rows_i[j][i], rows_i[j + 1][i], rows_i[j + 1][i + 1], rows_i[j][i + 1]))
    for i in range(nx):
        bm.faces.new((rows_i[0][i], rows_i[0][i + 1], rows_o[0][i + 1], rows_o[0][i])); bm.faces.new((rows_o[nz][i], rows_o[nz][i + 1], rows_i[nz][i + 1], rows_i[nz][i]))
    for j in range(nz):
        bm.faces.new((rows_i[j][0], rows_o[j][0], rows_o[j + 1][0], rows_i[j + 1][0])); bm.faces.new((rows_o[j][nx], rows_i[j][nx], rows_i[j + 1][nx], rows_o[j + 1][nx]))
    return obj(name, bm, m, True, tank)

def cyl(name, c, axis, r, L, m, tank=False, seg=32, caps=0.5):
    """a tank: a cylinder with domed ends (caps: the dome's depth over r)"""
    bm = bmesh.new(); A = Vector(axis).normalized()
    prof = []
    for k in range(7): a = math.pi / 2 * k / 6; prof.append((r * math.sin(a), -L / 2 - r * caps * math.cos(a)))
    for k in range(7): a = math.pi / 2 * (6 - k) / 6; prof.append((r * math.sin(a), L / 2 + r * caps * math.cos(a)))
    T = A.cross(Vector((0, 0, 1)) if abs(A.z) < 0.9 else Vector((1, 0, 0))).normalized(); S = A.cross(T)
    rings = []
    for rr, zz in prof:
        rings.append([bm.verts.new(Vector(c) + A * zz + (T * math.cos(2 * math.pi * j / seg) + S * math.sin(2 * math.pi * j / seg)) * rr) for j in range(seg)])
    for a_, b_ in zip(rings, rings[1:]):
        for j in range(seg):
            try: bm.faces.new((a_[j], a_[(j + 1) % seg], b_[(j + 1) % seg], b_[j]))
            except ValueError: pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    return obj(name, bm, m, True, tank)

def sphere(name, c, r, m, tank=False):
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=r)
    bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, True, tank)

def box(name, c, s, m, bevel=0.01, tank=False, rot=None):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ (rot or Matrix.Identity(4)) @ Matrix.Diagonal((*s, 1)), verts=bm.verts)
    if bevel: bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=2, affect='EDGES')
    return obj(name, bm, m, bevel > 0, tank)

def hexprism(name, c, R, d, m, tank=False, rot=90.0, back_y=None):
    """a hexagonal prism, its axis along y (from back_y to back_y + d)"""
    bm = bmesh.new(); y0 = back_y if back_y is not None else c[1] - d / 2
    pts = [(c[0] + R * math.cos(math.radians(rot + 60 * k)), c[2] + R * math.sin(math.radians(rot + 60 * k))) for k in range(6)]
    lo = [bm.verts.new((x, y0, z)) for x, z in pts]; hi = [bm.verts.new((x, y0 + d, z)) for x, z in pts]
    bm.faces.new(lo); bm.faces.new(hi[::-1])
    for k in range(6): bm.faces.new((lo[k], lo[(k + 1) % 6], hi[(k + 1) % 6], hi[k]))
    bmesh.ops.bevel(bm, geom=list(bm.edges), offset=0.01, segments=2, affect='EDGES')
    return obj(name, bm, m, False, tank)

def tube(name, pts, r, m, tank=False, ns=20):
    bm = bmesh.new(); rings = []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        u = t.cross(Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))).normalized(); w = t.cross(u)
        rings.append([bm.verts.new(p + (u * math.cos(2 * math.pi * k / ns) + w * math.sin(2 * math.pi * k / ns)) * r) for k in range(ns)])
    for a_, b_ in zip(rings, rings[1:]):
        for k in range(ns): bm.faces.new((a_[k], a_[(k + 1) % ns], b_[(k + 1) % ns], b_[k]))
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
    return obj(name, bm, m, True, tank)

HY, HZ = 0.186, 1.4685                                   # the hinge pins (x +-0.204)
def yoke(width=0.22, y=None, z=1.475, d=0.05, h=0.035):
    """the yoke across the block's top carrying the hinge pins"""
    y = y if y is not None else HY
    box("yoke", (0, y, z), (2 * width, d, h), WHITE, 0.008)
    box("yokered", (0, y + d / 2 + 0.001, z), (2 * width - 0.04, 0.003, 0.008), RED, 0.001)

def rcs(name, x, z, y=None, sx=1):
    """a hydrogen RCS quad: a block with three nozzles (out, back, up/down)"""
    y = y if y is not None else oy(x, z) + 0.03
    box(name, (x, y, z), (0.038, 0.03, 0.032), DARK, 0.004)
    for dv in ((sx, 0, 0), (0, 1, 0), (0, 0, 1 if z > 1.35 else -1)):
        p = Vector((x, y, z)) + Vector(dv) * 0.02
        cyl(name + "n", p, dv, 0.005, 0.008, METAL, seg=10, caps=0.1)

def dock_plate(z0=1.26, z1=1.44, t=0.012):
    conformal("dockplate", -0.13, 0.13, z0, z1, t, GREY, nx=12, nz=8)

# ---- the ten ----
def c1():   # Седло: a thick conformal shell over the lid, the yoke on its top edge
    conformal("tank", -0.185, 0.185, 1.235, 1.47, lambda x, z: 0.15 + 0.07 * (1 - abs(x) / 0.185), WHITE, True)
    yoke(); [rcs("r%d%d" % (a, b), a * 0.20, z, None, a) for a in (1, -1) for b, z in ((0, 1.25), (1, 1.44))]
def c2():   # Ярмо + поперечные баки: two horizontal tanks on the lid under the yoke
    dock_plate()
    for z in (1.29, 1.405): cyl("tank%d" % int(z * 100), (0, oy(0, z) + 0.075, z), (1, 0, 0), 0.068, 0.41, WHITE, True)
    yoke(); [rcs("r%d%d" % (a, b), a * 0.23, z, None, a) for a in (1, -1) for b, z in ((0, 1.29), (1, 1.42))]
def c3():   # Два боковых бака: upright tanks along the block's edges, the lid's middle (radiator) open
    for sx in (1, -1):
        cyl("tank%d" % sx, (sx * 0.175, 0.205, 1.335), (0, 0, 1), 0.065, 0.47, WHITE, True)
        box("clamp%d" % sx, (sx * 0.13, oy(sx * 0.13, 1.34) + 0.02, 1.34), (0.07, 0.03, 0.22), GREY, 0.006)
    yoke(); [rcs("r%d%d" % (a, b), a * 0.24, z, 0.205, a) for a in (1, -1) for b, z in ((0, 1.16), (1, 1.52))]
def c4():   # Воротник: a horseshoe tank over the block's top edge behind the neck, down the sides
    pts = []
    for k in range(25):
        a = math.radians(-95 + 190 * k / 24)
        x = 0.20 * math.sin(a); z = 1.47 + 0.06 * math.cos(a) - 0.10 * (1 - math.cos(a))
        pts.append(Vector((x, max(oy(x, z), 0.15) + 0.07, z)))
    tube("tank", pts, 0.081, WHITE, True)
    dock_plate(1.27, 1.40); yoke(0.21, 0.24, 1.50)
    [rcs("r%d%d" % (a, b), a * 0.22, z, None, a) for a in (1, -1) for b, z in ((0, 1.26), (1, 1.45))]
def c5():   # Шестигранная рама: a frame on the four pads, a hexagonal tank inside it
    for sx in (1, -1):
        for z in (1.28, 1.42): cyl("post%d%d" % (sx, int(z * 100)), (sx * 0.11, oy(sx * 0.11, z) + 0.025, z), (0, 1, 0), 0.014, 0.05, METAL)
    hexprism("tank", (0, 0, 1.345), 0.155, 0.215, WHITE, True, 90, back_y=0.27)
    hexprism("ring", (0, 0, 1.345), 0.162, 0.012, RED, False, 90, back_y=0.42)
    yoke(); [rcs("r%d%d" % (a, b), a * 0.20, z, 0.32, a) for a in (1, -1) for b, z in ((0, 1.25), (1, 1.44))]
def c6():   # Плита: a flat plate tank on the lid, its outer face the radiator now (ribbed)
    conformal("tank", -0.19, 0.19, 1.23, 1.47, 0.165, WHITE, True, round_=0.15)
    for k in range(-5, 6):
        if abs(k) < 2: continue
        x = k * 0.03; pts = [Vector((x, oy(x, z) + GAP + 0.167, z)) for z in (1.26, 1.32, 1.38, 1.44)]
        tube("fin%d" % k, pts, 0.0035, GREY)
    yoke(); [rcs("r%d%d" % (a, b), a * 0.21, z, None, a) for a in (1, -1) for b, z in ((0, 1.24), (1, 1.46))]
def c7():   # Четыре сферы: four spherical tanks over the four pads
    for sx in (1, -1):
        for z in (1.28, 1.42): sphere("tank%d%d" % (sx, int(z * 100)), (sx * 0.11, oy(sx * 0.11, z) + 0.095, z), 0.092, WHITE, True)
    dock_plate(); yoke()
    [rcs("r%d%d" % (a, b), a * 0.225, z, None, a) for a in (1, -1) for b, z in ((0, 1.25), (1, 1.45))]
def c8():   # Хребет: one upright tank on the centre line, the radiator fields either side open
    cyl("tank", (0, oy(0, 1.34) + 0.10, 1.335), (0, 0, 1), 0.097, 0.385, WHITE, True)
    box("spine", (0, oy(0, 1.34) + 0.012, 1.335), (0.06, 0.02, 0.24), GREY, 0.004)
    yoke(); [rcs("r%d%d" % (a, b), a * 0.20, z, None, a) for a in (1, -1) for b, z in ((0, 1.25), (1, 1.44))]
def c9():   # Коромысло: the tank is the yoke itself - a thick beam across the shoulders above the block's top
    cyl("tank", (0, 0.245, 1.49), (1, 0, 0), 0.085, 0.52, WHITE, True, caps=0.6)
    box("saddle", (0, 0.20, 1.44), (0.30, 0.05, 0.05), GREY, 0.008)
    dock_plate(1.27, 1.43)
    [rcs("r%d%d" % (a, b), a * 0.31, z, 0.245, a) for a in (1, -1) for b, z in ((0, 1.44), (1, 1.54))]
def c10():  # V-баки: two tanks in a V over the lid, the dock and the radiator's middle between them
    for sx in (1, -1):
        a = math.radians(sx * 18)
        c = Vector((sx * 0.105, 0, 1.345)); c.y = oy(c.x, c.z) + 0.072
        cyl("tank%d" % sx, c, (math.sin(a), 0, math.cos(a)), 0.07, 0.38, WHITE, True)
    yoke(); [rcs("r%d%d" % (a, b), a * 0.21, z, None, a) for a in (1, -1) for b, z in ((0, 1.24), (1, 1.46))]
CONCEPTS = [("1", "Седло", c1), ("2", "Ярмо + поперечные баки", c2), ("3", "Два боковых бака", c3), ("4", "Воротник", c4), ("5", "Шестигранная рама", c5),
            ("6", "Плита-радиатор", c6), ("7", "Четыре сферы", c7), ("8", "Хребет", c8), ("9", "Коромысло", c9), ("10", "V-баки", c10)]

# ---- round 2 (the user, 2026-10-18: «у нас внизу место и сверху; обойти радиатор ИЛИ общий теплоотвод через коннектор»):
# the pack follows the block's silhouette - a beam over the top behind the neck, rails down the lid's edges (outside the
# radiator fields), a tank in the step behind the lumbar part (its back is 6 cm in from the lid's). The lumbar part bends
# with LowerBack: the low tank rides on it and the rails slide (telescopic sleeves at z 1.215).
def oyl(x, z):
    """the lumbar part's outer surface behind (x, z), clamped to its edges"""
    return outer_y(max(-0.155, min(0.155, x)), max(1.065, min(1.24, z)), "lo")
LOWZ0, LOWZ1 = 1.062, 1.183            # the low tank: over the hip ring's strut, under the sublimator's vapour slot (z 1.19-1.22)
def slab(name, x0, x1, z0, z1, yback, surf, gap, m=WHITE, tank=True, round_=0.35, nx=24, nz=12):
    """a shell whose inner face follows surf (gap off it) and whose back is flat at y = yback"""
    return conformal(name, x0, x1, z0, z1, lambda x, z: yback - surf(x, z) - gap, m, tank, nx, nz, round_, surf=surf, gap=gap)
def rad_channels(name, x0, x1, z0, z1, y, n):
    """radiator channels on a flat back (the connector's coolant loop runs through them)"""
    for k in range(n):
        x = x0 + (x1 - x0) * (k + 0.5) / n
        tube(name, [Vector((x, y + 0.0022, z)) for z in (z0, (z0 + z1) / 2, z1)], 0.0022, GREY)
def hinge_blocks(y=0.205, z=1.472):
    for sx in (1, -1): box("hingeblk%d" % sx, (sx * 0.200, y, z), (0.05, 0.06, 0.06), GREY, 0.008)
def rails(xs, r, ytop, ztop, tank=False, m=GREY):
    """the rails down the lid's edges into the low tank, a telescopic sleeve at the lumbar joint"""
    for sx in (1, -1):
        x = sx * xs; pts = [Vector((x, ytop, ztop))]
        for z in (1.40, 1.34, 1.28, 1.235): pts.append(Vector((x, oy(x, z) + GAP + r + 0.003, z)))
        pts += [Vector((x, oyl(x, 1.19) + 0.012 + r + 0.006, 1.19)), Vector((x, oyl(x, 1.17) + 0.012 + r + 0.006, 1.165))]
        tube("rail%d" % sx, pts, r, m, tank)
        box("sleeve%d" % sx, (x, pts[4].y, 1.215), (2 * r + 0.012, 2 * r + 0.012, 0.035), METAL, 0.004)
def c11():   # Скоба: top beam tank + thin rails + low tank; the lid's radiator open (the bypass)
    top_tank()
    rails(0.165, 0.017, 0.226, 1.43)
    slab("tanklow", -0.17, 0.17, LOWZ0, LOWZ1, 0.29, oyl, 0.012)
    box("lowred", (0, 0.291, 1.17), (0.24, 0.004, 0.01), RED, 0.001)
    [rcs("r%d%d" % (a, b), a * x, z, y, a) for a in (1, -1) for b, x, z, y in ((0, 0.13, 1.555, 0.24), (1, 0.19, 1.09, 0.28))]
def c12():   # Скоба-низ: the fuel low (near her centre of mass), a slim hinge beam; the low tank's back a radiator (connector)
    box("beam", (0, 0.255, 1.445), (0.36, 0.045, 0.045), WHITE, 0.008); box("beamred", (0, 0.278, 1.445), (0.32, 0.003, 0.008), RED, 0.001); hinge_blocks()
    rails(0.163, 0.03, 0.222, 1.46, True, WHITE)
    slab("tanklow", -0.20, 0.20, LOWZ0, LOWZ1, 0.31, oyl, 0.012, round_=0.25)
    rad_channels("chan", -0.17, 0.17, LOWZ0 + 0.025, LOWZ1 - 0.025, 0.31, 14)
    [rcs("r%d%d" % (a, b), a * x, z, y, a) for a in (1, -1) for b, x, z, y in ((0, 0.24, 1.485, 0.212), (1, 0.225, 1.09, 0.30))]
def c13():   # Контур: one outline frame round the lid (window over its radiator fields) + the low tank; the frame's
             # back is a radiator on the connector's loop (the shared heat sink)
    slab("tanktopband", -0.19, 0.19, 1.37, 1.445, 0.30, oy, GAP, round_=0.25)
    for sx in (1, -1): slab("tankside%d" % sx, min(sx * 0.137, sx * 0.19), max(sx * 0.137, sx * 0.19), 1.225, 1.40, 0.30, oy, GAP, round_=0.5, nx=6)
    slab("tanklow", -0.17, 0.17, LOWZ0, LOWZ1, 0.29, oyl, 0.012)
    rad_channels("chan", -0.15, 0.15, 1.385, 1.43, 0.30, 12); rad_channels("chanl", -0.15, 0.15, LOWZ0 + 0.02, LOWZ1 - 0.02, 0.29, 12)
    for sx in (1, -1): box("sleeve%d" % sx, (sx * 0.163, 0.27, 1.205), (0.05, 0.045, 0.04), METAL, 0.004)
    hinge_blocks()
    [rcs("r%d%d" % (a, b), a * x, z, y, a) for a in (1, -1) for b, x, z, y in ((0, 0.2, 1.43, 0.30), (1, 0.19, 1.09, 0.28))]
TOPY, TOPZ = 0.268, 1.447                 # the top tank's axis: in the corner behind the lid's sloping top, under the helmet's sweep
def top_tank(r=0.062, L=0.31):
    cyl("tanktop", (0, TOPY, TOPZ), (1, 0, 0), r, L, WHITE, True); hinge_blocks()
    box("topred", (0, TOPY + r - 0.0005, TOPZ), (L - 0.03, 0.004, 0.012), RED, 0.001)
def low_tank(yback=0.29, x=0.17, z0=LOWZ0):
    slab("tanklow", -x, x, z0, LOWZ1, yback, oyl, 0.012)
    box("vent", (0, yback + 0.001, 1.10), (0.12, 0.004, 0.025), DARK, 0.001)   # the sublimator's vapour duct exits here
def rcs4(top=(0.12, 1.515, 0.27), low=(0.19, 1.09, 0.27)):
    [rcs("r%d%d" % (a, b), a * x, z, y, a) for a in (1, -1) for b, (x, z, y) in ((0, top), (1, low))]
def c14():   # Пояс: the low tank a curved belt round the lumbar part's back and sides; a slim top tank (bypass)
    pts = []
    for k in range(21):
        a = math.radians(-100 + 200 * k / 20); pts.append(Vector((0.215 * math.sin(a), 0.10 + 0.15 * math.cos(a), 1.125)))
    tube("tankbelt", pts, 0.058, WHITE, True); tube("beltred", [p + Vector((0, 0, 0.063)) for p in pts[4:17]], 0.004, RED)
    top_tank(); rcs4(low=(0.235, 1.125, 0.16))
def c15():   # Капли: two drop tanks behind the shoulders' tops, the low tank (bypass)
    for sx in (1, -1):
        cyl("tankdrop%d" % sx, (sx * 0.11, 0.275, 1.44), (sx, 0, 0), 0.07, 0.05, WHITE, True, caps=1.0)
        box("dropred%d" % sx, (sx * 0.11, 0.275, 1.511), (0.05, 0.03, 0.004), RED, 0.001)
    hinge_blocks(); box("bridge", (0, 0.262, 1.445), (0.08, 0.04, 0.03), GREY, 0.006)
    low_tank(0.29); rcs4(top=(0.0, 1.47, 0.29))
def c16():   # Гребень: top tank + low tank with radiator fins on their backs (the connector's loop, fins radiate both sides)
    top_tank(); low_tank(0.29)
    for k in range(-4, 5):
        x = k * 0.036
        box("fintop", (x, TOPY + 0.062 + 0.025, TOPZ), (0.003, 0.05, 0.09), WHITE, 0.0008)
        box("finlow", (x, 0.29 + 0.03, 1.125), (0.003, 0.06, 0.10), WHITE, 0.0008)
    rcs4(low=(0.19, 1.09, 0.30))
def c17():   # Арка: one hoop tank along the block's outline outside the radiator fields, its skin a radiator (connector); low tank
    pts = []
    for z in (1.17, 1.24, 1.30, 1.36, 1.42): x = 0.168; pts.append(Vector((x, oy(x, z) + GAP + 0.048, z)))
    pts += [Vector((0.15, 0.262, 1.445)), Vector((0.08, 0.272, 1.468)), Vector((0, 0.274, 1.473))]
    pts = pts + [Vector((-p.x, p.y, p.z)) for p in reversed(pts[:-1])]
    tube("tankhoop", pts, 0.05, WHITE, True, 24); hinge_blocks()
    rad_channels("chanl", -0.15, 0.15, LOWZ0 + 0.02, LOWZ1 - 0.02, 0.29, 10)
    low_tank(); rcs4(top=(0.12, 1.515, 0.27))
def c18():   # Штанги-радиаторы: top tank + low tank; the heat to the booms' skins through the hinges (connector)
    top_tank(); low_tank(); rcs4()
def c19():   # Люлька: the low tank sits on the hip ring (the load to the hips on Mars / Earth), a top tank (bypass)
    slab("tanklow", -0.17, 0.17, 1.045, LOWZ1, 0.29, oyl, 0.012); box("vent", (0, 0.291, 1.10), (0.12, 0.004, 0.025), DARK, 0.001)
    for sx in (1, -1): tube("leg%d" % sx, [Vector((sx * 0.12, 0.27, 1.06)), Vector((sx * 0.14, 0.23, 1.02)), Vector((sx * 0.16, 0.19, 0.99))], 0.011, METAL)
    top_tank(); rcs4()
def c20():   # Киль: a keel tank on the lid's middle strip (the dock strip, no radiator there) joining the top and low tanks (bypass)
    slab("tankkeel", -0.045, 0.045, 1.24, 1.42, 0.33, oy, GAP, round_=0.4, nx=6, nz=12)
    box("keelred", (0, 0.331, 1.345), (0.004, 0.004, 0.16), RED, 0.001)
    top_tank(); low_tank(0.29); rcs4()
CONCEPTS += [("11", "Скоба", c11), ("12", "Скоба-низ", c12), ("13", "Контур", c13), ("14", "Пояс", c14), ("15", "Капли", c15),
             ("16", "Гребень", c16), ("17", "Арка", c17), ("18", "Штанги-радиаторы", c18), ("19", "Люлька", c19), ("20", "Киль", c20)]
PLUMBED = {"12": ("tanklow",), "13": ("tanktopband", "tanklow"), "16": ("fin",), "17": ("tankhoop", "tanklow"), "18": ("Hexa_Stage",)}   # on the connector's loop

# ---- round 3 (the user, 2026-10-18: «работаем с дугой, в 5 вариантах, облагородим под общий дизайн»): the arch (17) in the
# suit's language - flat faceted profiles (no round tubes), its flat back a radiator panel on the connector's loop, the
# hinge's screw inside a node housing at each shoulder (the bare posts gone), red bands, stencilled marks; seamless, no
# fasteners in sight (the user: «какие болты? 30 век»)
def sweep(name, pts, prof, m, tank=False, scale=None, smooth=False, off=(0.0, 0.0)):
    """a faceted beam along pts: prof is a closed polygon (u in the arch's plane, w towards her back); scale(i) widens it"""
    bm = bmesh.new(); rings = []; n = len(prof)
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        w = Vector((0, 1, 0)); w = (w - t * w.dot(t)).normalized(); u = t.cross(w)
        k = scale(i) if scale else (1.0, 1.0)
        rings.append([bm.verts.new(p + u * ((a + off[0]) * k[0]) + w * ((b + off[1]) * k[1])) for a, b in prof])
    for a_, b_ in zip(rings, rings[1:]):
        for j in range(n): bm.faces.new((a_[j], a_[(j + 1) % n], b_[(j + 1) % n], b_[j]))
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
    return obj(name, bm, m, smooth, tank)
def rrect(a, b, c):
    """a rectangle 2a x 2b with its corners chamfered by c"""
    return [(a, b - c), (a - c, b), (-a + c, b), (-a, b - c), (-a, -b + c), (-a + c, -b), (a - c, -b), (a, -b + c)]
def hexp(r, sq=1.0):
    return [(r * math.cos(math.radians(60 * k)), sq * r * math.sin(math.radians(60 * k))) for k in range(6)]
def arch_path(depth, n_side=6, top=True):
    """the arch's centre line: up the lid's edge (x 0.168, outside the radiator fields), over the top behind the lid's slope"""
    side = []
    for j in range(n_side):
        z = 1.17 + (1.42 - 1.17) * j / (n_side - 1); x = 0.168
        side.append(Vector((x, oy(x, z) + GAP + depth + 0.003, z)))
    side += [Vector((0.15, 0.262, 1.445)), Vector((0.08, 0.272, 1.468)), Vector((0, 0.274, 1.473))]
    return side + [Vector((-p.x, p.y, p.z)) for p in reversed(side[:-1])]
def label(text, loc, size, m=RED, y_face=True):
    """a stencilled mark on a back face (reads from behind her)"""
    cu = bpy.data.curves.new("lbl", 'FONT'); cu.body = text; cu.size = size; cu.align_x = 'CENTER'; cu.align_y = 'CENTER'; cu.extrude = 0.0004
    try: cu.font = bpy.data.fonts.load("C:/Windows/Fonts/arialbd.ttf", check_existing=True)
    except Exception: pass
    co = bpy.data.objects.new("lbl", cu); sc.collection.objects.link(co)
    co.matrix_world = Matrix.Translation(Vector(loc)) @ Matrix.Rotation(math.pi, 4, 'Z') @ Matrix.Rotation(math.pi / 2, 4, 'X')
    dg = bpy.context.evaluated_depsgraph_get(); me = bpy.data.meshes.new_from_object(co.evaluated_get(dg)); M = co.matrix_world.copy()
    bpy.data.objects.remove(co, do_unlink=True); me.transform(M); me.materials.clear(); me.materials.append(m)
    o = bpy.data.objects.new("label", me); sc.collection.objects.link(o); parts.append(o); return o
def node(sx, kind="hex", h=0.13):
    """the hinge node: the screw and its ring motor inside a housing round the old post (x 0.204, y 0.186)"""
    c = Vector((sx * 0.204, 0.186, 1.452 + h / 2 - 0.02))
    if kind == "hex":
        bm = bmesh.new(); R = 0.034
        lo = [bm.verts.new((c.x + R * math.cos(math.radians(30 + 60 * k)), c.y + R * math.sin(math.radians(30 + 60 * k)), c.z - h / 2)) for k in range(6)]
        hi = [bm.verts.new((v.co.x, v.co.y, c.z + h / 2)) for v in lo]
        bm.faces.new(lo[::-1]); bm.faces.new(hi)
        for k in range(6): bm.faces.new((lo[k], lo[(k + 1) % 6], hi[(k + 1) % 6], hi[k]))
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=0.004, segments=1, affect='EDGES'); obj("node%d" % sx, bm, WHITE, False)
    else:
        box("node%d" % sx, tuple(c), (0.06, 0.06, h), WHITE, 0.006)
    box("nodered%d" % sx, (c.x, c.y, c.z + h / 2 - 0.018), (0.072 if kind != "hex" else 0.07, 0.072 if kind != "hex" else 0.062, 0.008), RED, 0.001)
    box("nodeslot%d" % sx, (c.x + sx * 0.02, c.y + 0.005, c.z - 0.005), (0.022, 0.044, h - 0.035), DARK, 0.002)   # the boom's root rides up this slot
    box("nodelink%d" % sx, ((c.x + sx * 0.15) / 2, 0.23, 1.45), (abs(c.x - sx * 0.15) + 0.02, 0.05, 0.03), GREY, 0.004)   # to the arch's shoulder
def low_box(yback=0.29, z0=LOWZ0, x=0.17, stencil=True):
    slab("tanklow", -x, x, z0, LOWZ1, yback, oyl, 0.012, round_=0.12)
    box("vent", (0, yback + 0.001, 1.098), (0.12, 0.004, 0.022), DARK, 0.001)
    for k in range(-5, 6): box("ventbar", (k * 0.0105, yback + 0.003, 1.098), (0.003, 0.002, 0.02), GREY, 0)
    box("lowred", (0, yback + 0.001, 1.165), (2 * x - 0.04, 0.003, 0.009), RED, 0.001)
    if stencil: label("ВОДОРОД", (0, yback + 0.0025, 1.135), 0.022)
def arch_common(pts, depth, prof_w, kind="hex", scale=None):
    """the red band and the radiator channels along the arch's flat back, the nodes, the low tank, the RCS"""
    sweep("archred", pts, rrect(0.006, 0.0016, 0), RED, off=(0, depth + 0.0012))
    for du in (-0.55, 0.55):
        sweep("chan", pts, [(du * prof_w + 0.0025 * math.cos(a), depth + 0.0025 * math.sin(a)) for a in (0, 2.1, 4.2)], GREY, scale=scale)
    for sx in (1, -1): node(sx, kind)
    low_box(); rcs4(top=(0.12, 1.515, 0.27))
def c21():   # Профиль: a chamfered flat beam 90 x 64 mm, hex nodes
    d = 0.032; pts = arch_path(d); sweep("tankarch", pts, rrect(0.045, d, 0.012), WHITE, True); arch_common(pts, d, 0.045)
def c22():   # Гекса: the hexagonal section of the pack's capsules and the old body
    d = 0.036; pts = arch_path(d); sweep("tankarch", pts, hexp(0.052, d / (0.052 * 0.866)), WHITE, True); arch_common(pts, d * 0.98, 0.026)
def c23():   # Лира: slim up the sides, full at the shoulders and the low corners - the fuel where the space is
    d = 0.036; pts = arch_path(d, 8); N = len(pts)
    def sc_(i):
        z = pts[i].z; k = 0.72 + 0.28 * max(min(1, (z - 1.33) / 0.1), min(1, (1.25 - z) / 0.08), 0)
        return (k, 0.85 + 0.15 * k)
    sweep("tankarch", pts, rrect(0.05, d, 0.013), WHITE, True, scale=sc_); arch_common(pts, d, 0.05, "box", scale=sc_)
def c24():   # Кольцо: the arch closes under the upper part - its bottom span is the low tank (one faceted ring round the lid)
    d = 0.034; side = arch_path(d)
    low = [Vector((0.168, oyl(0.15, 1.12) + 0.012 + 0.05, 1.12)), Vector((0.08, oyl(0.08, 1.12) + 0.012 + 0.05, 1.115)), Vector((0, oyl(0, 1.12) + 0.012 + 0.05, 1.112))]
    lowpath = low + [Vector((-p.x, p.y, p.z)) for p in reversed(low[:-1])]
    sweep("tankarch", side, rrect(0.045, d, 0.012), WHITE, True)
    sweep("tankring", [Vector((0.168, side[0].y, 1.17))] + lowpath + [Vector((-0.168, side[0].y, 1.17))], rrect(0.05, 0.05, 0.014), WHITE, True)
    sweep("archred", side, rrect(0.006, 0.0016, 0), RED, off=(0, d + 0.0012))
    for sx in (1, -1):
        node(sx, "hex"); box("sleeve%d" % sx, (sx * 0.168, side[0].y, 1.20), (0.105, 0.075, 0.04), METAL, 0.004)
    label("ВОДОРОД", (0, lowpath[2].y + 0.0525, 1.112), 0.02); box("vent", (0, lowpath[2].y + 0.051, 1.085), (0.1, 0.004, 0.012), DARK, 0.001)
    rcs4(top=(0.12, 1.515, 0.27), low=(0.235, 1.11, 0.24))
def c25():   # Экзо: a structural frame of flat profiles (front and back flanges, webs), the tanks nested in it
    d = 0.036; pts = arch_path(d)
    for nm, w0 in (("flangeF", -d + 0.004), ("flangeB", d - 0.004)):
        sweep(nm, pts, rrect(0.05, 0.004, 0.0015), METAL, off=(0, w0))
    for i in range(1, len(pts) - 1):
        p = pts[i]; t = (pts[i + 1] - pts[i - 1]).normalized(); w = Vector((0, 1, 0)); w = (w - t * w.dot(t)).normalized(); u = t.cross(w)
        rot = Matrix((u, w, t)).transposed().to_4x4()
        for su in (1, -1): box("web", tuple(p + u * su * 0.047), (0.006, 2 * d - 0.006, 0.02), METAL, 0.001, rot=rot)
    sweep("tankarch", pts, rrect(0.038, d - 0.008, 0.012), WHITE, True)
    sweep("archred", pts, rrect(0.006, 0.0016, 0), RED, off=(0, d - 0.0065))
    for sx in (1, -1): node(sx, "box")
    low_box(); rcs4(top=(0.12, 1.515, 0.27))
CONCEPTS += [("21", "Арка-профиль", c21), ("22", "Арка-гекса", c22), ("23", "Арка-лира", c23), ("24", "Арка-кольцо", c24), ("25", "Арка-экзо", c25)]
PLUMBED.update({k: ("tankarch", "tankring", "tanklow") for k in ("21", "22", "23", "24", "25")})

# ---- round 4 (the user: «3 склеенных банана. а нельзя монолитный каркас?»): one cast body - the arch, the shoulder nodes
# and the low tank are a single shell (a heightfield over her back: inner face on the block, outer face its own, windows
# open over the lid's radiator fields), rounded by subdivision. It rides on the upper part's pads; the lumbar part moves
# under its low end with a gap. Its whole skin is the radiator on the connector's loop.
def monolith(name, keep, inner, outer, x0=-0.24, x1=0.24, z0=1.055, z1=1.565, h=0.01, sub=2):
    nx, nz = int(round((x1 - x0) / h)), int(round((z1 - z0) / h))
    X = [x0 + (x1 - x0) * i / nx for i in range(nx + 1)]; Z = [z0 + (z1 - z0) * j / nz for j in range(nz + 1)]
    cell = [[keep(X[i] + h / 2, Z[j] + h / 2) for j in range(nz)] for i in range(nx)]
    bm = bmesh.new(); vi, vo = {}, {}
    def V(i, j, out):
        d = vo if out else vi
        if (i, j) not in d:
            x, z = X[i], Z[j]; yi = inner(x, z); d[(i, j)] = bm.verts.new((x, max(outer(x, z), yi + 0.025) if out else yi, z))
        return d[(i, j)]
    k = lambda i, j: 0 <= i < nx and 0 <= j < nz and cell[i][j]
    for i in range(nx):
        for j in range(nz):
            if not cell[i][j]: continue
            bm.faces.new((V(i, j, 1), V(i + 1, j, 1), V(i + 1, j + 1, 1), V(i, j + 1, 1)))
            bm.faces.new((V(i, j, 0), V(i, j + 1, 0), V(i + 1, j + 1, 0), V(i + 1, j, 0)))
            for (di, dj), (a, b) in (((-1, 0), ((i, j + 1), (i, j))), ((1, 0), ((i + 1, j), (i + 1, j + 1))),
                                     ((0, -1), ((i, j), (i + 1, j))), ((0, 1), ((i + 1, j + 1), (i, j + 1)))):
                if not k(i + di, j + dj): bm.faces.new((V(*a, 1), V(*b, 1), V(*b, 0), V(*a, 0)))
    o = obj(name, bm, WHITE, True, True)
    md = o.modifiers.new("sub", 'SUBSURF'); md.levels = sub; md.render_levels = sub
    return o
def mono_inner(x, z):
    if z >= 1.215:
        y = oy(x, z) + GAP
        if abs(x) < 0.19:                               # the top stands off the lid's slope: the tipped-back helmet's rim passes over it
            t = min(max((z - 1.40) / 0.04, 0), 1); y = max(y, y * (1 - t) + 0.218 * t)
        return y
    return oyl(x, z) + 0.012
def mono_outer_fn(y_up=0.30, y_low=0.285, y_node=0.235, top_dip=0.0):
    def f(x, z):
        s = min(max((z - 1.19) / 0.04, 0), 1); s = s * s * (3 - 2 * s)
        y = y_low + (y_up - y_low) * s
        if z > 1.47: y = y_node                       # the shoulder nodes above the block's top
        if abs(x) > 0.19 and z > 1.40: y = min(y, y_node + 0.02)
        return y - top_dip * max(0.0, (z - 1.42) / 0.05)
    return f
WIN_X, WIN_Z = (0.040, 0.140), (1.230, 1.385)          # over the lid's radiator fields (|x| 0.044-0.132, z 1.245-1.365)
def mono_keep(keel=True):
    def keep(x, z):
        ax = abs(x)
        if z > 1.47: return 0.175 <= ax <= 0.235 and z <= 1.50          # the hinge nodes (x 0.204): low - the fold clears without the screw's lift
        if ax > 0.19: return z >= 1.40 and ax <= 0.235                  # the shoulders reaching out to the nodes
        if z < 1.21 and ax > 0.17: return False                         # the lumbar part is narrower
        if WIN_Z[0] <= z <= WIN_Z[1]:
            if keel and WIN_X[0] <= ax <= WIN_X[1]: return False        # two windows, the keel over the dock strip
            if not keel and ax <= WIN_X[1]: return False                # one window
        return True
    return lambda x, z: keep(x, z) and (z > 1.40 or abs(x) <= 0.19)
def mono_detail(y_up=0.30, y_low=0.285, keel=True):
    for sx in (1, -1):                                  # the red band round the windows, the radiator channels down the sides
        x = sx * 0.165
        tube("monored", [Vector((x, y_up + 0.0015, z)) for z in (1.24, 1.30, 1.36, 1.42)], 0.0028, RED)
        for dx in (-0.012, 0.012):
            tube("monochan", [Vector((x + dx, y_up + 0.0012, z)) for z in (1.25, 1.31, 1.37)], 0.0018, GREY)
    tube("monored", [Vector((x, y_up + 0.0015, 1.43)) for x in (-0.15, -0.05, 0.05, 0.15)], 0.0028, RED)
    for z in (1.40, 1.415, 1.445, 1.46):
        tube("monochan", [Vector((x, y_up + 0.0012, z)) for x in (-0.14, -0.05, 0.05, 0.14)], 0.0018, GREY)
    if keel: tube("monored", [Vector((0, y_up + 0.0015, z)) for z in (1.24, 1.31, 1.38)], 0.0028, RED)
    box("vent", (0, y_low + 0.001, 1.098), (0.12, 0.004, 0.022), DARK, 0.001)
    label("ВОДОРОД", (0, y_low + 0.0025, 1.14), 0.022)
    rcs4(top=(0.205, 1.52, 0.21), low=(0.19, 1.09, 0.255))
def c26():   # Монолит «Ш»: two windows over the fields, the keel over the dock strip carries the connector
    monolith("tankmono", mono_keep(True), mono_inner, mono_outer_fn(), h=globals().get("MONO_GRID", 0.01)); mono_detail()
def c27():   # Монолит «О»: one window over both fields and the dock strip (the connector reached through it)
    monolith("tankmono", mono_keep(False), mono_inner, mono_outer_fn()); mono_detail(keel=False)
CONCEPTS += [("26", "Монолит «Ш»", c26), ("27", "Монолит «О»", c27)]
PLUMBED.update({"26": ("tankmono",), "27": ("tankmono",)})

def faces_world(o):
    """(centre, normal, area) of an object's evaluated faces in the world"""
    dg = bpy.context.evaluated_depsgraph_get(); oe = o.evaluated_get(dg); me = oe.to_mesh()
    bm = bmesh.new(); bm.from_mesh(me); bm.transform(oe.matrix_world)
    out = [(f.calc_center_median(), f.normal.copy(), f.calc_area()) for f in bm.faces]
    bm.free(); oe.to_mesh_clear(); return out
def tree_of(objs):
    dg = bpy.context.evaluated_depsgraph_get(); V, P = [], []
    for o in objs:
        oe = o.evaluated_get(dg); me = oe.to_mesh(); M = oe.matrix_world; b = len(V)
        V += [M @ v.co for v in me.vertices]; P += [[b + i for i in p.vertices] for p in me.polygons]; oe.to_mesh_clear()
    return BVHTree.FromPolygons(V, P)
def view_area(objs, trees, n=16, only_back=False):
    """sum of face area x its view factor to open space (cosine-weighted rays that escape every occluder)"""
    import random; rnd = random.Random(7); A = 0.0
    for o in objs:
        for c, nrm, a in faces_world(o):
            if a < 1e-7 or (only_back and nrm.y < 0.5): continue
            if o.name.startswith("Hexa_"):
                tr0 = trees[-1]; hit_out = tr0.ray_cast(c + nrm * 0.0015, nrm, 0.3)[0] is not None
                if hit_out: nrm = -nrm
            t = nrm.orthogonal().normalized(); s = nrm.cross(t); esc = 0
            for k in range(n):
                u1, u2 = rnd.random(), rnd.random(); r = math.sqrt(u1); ph = 2 * math.pi * u2
                d = t * (r * math.cos(ph)) + s * (r * math.sin(ph)) + nrm * math.sqrt(1 - u1); p = c + nrm * 0.0015
                if all(tr.ray_cast(p, d, 5.0)[0] is None for tr in trees): esc += 1
            A += a * esc / n
    return A
Q_BLOCK, Q_LOOP = 0.9 * 5.67e-8 * 360 ** 4, 0.9 * 5.67e-8 * 350 ** 4   # W/m2 to cold space: the block at 360 K, the pack's loop at 350 K

def volume(objs):
    v = 0.0
    for o in objs:
        dg = bpy.context.evaluated_depsgraph_get(); oe = o.evaluated_get(dg)
        bm = bmesh.new(); bm.from_mesh(oe.to_mesh()); bm.transform(o.matrix_world); oe.to_mesh_clear()
        try: v += abs(bm.calc_volume())
        except Exception: pass
        bm.free()
    return v

def coverage(objs):
    """the share of the lid's radiator fields (|x| 0.05..0.17, z 1.245..1.425) that the pack's parts hide from behind"""
    dg = bpy.context.evaluated_depsgraph_get(); trees = [BVHTree.FromObject(o, dg) for o in objs]
    hit = n = 0
    for i in range(14):
        for j in range(10):
            x = (0.05 + 0.12 * i / 13) * (1 if i % 2 else -1); z = 1.245 + 0.18 * j / 9
            p = Vector((x, oy(x, z) + 0.002, z)); n += 1
            if any(t.ray_cast(p, Vector((0, 1, 0)), 1.0)[0] is not None for t in trees): hit += 1
    return hit / n

# ---- the game mesh's body: POB_EXPORT=<key> builds that concept and writes its parts (Blender frame, triangles with corner
# normals, by material) to Tantra_Design/tools/jetpack_mono.json for build_jetpack_hexa.py; nothing rendered
if os.environ.get("POB_EXPORT"):
    try:
        key = os.environ["POB_EXPORT"]; fn = next(f for k, n, f in CONCEPTS if k == key)
        MONO_GRID = 0.012
        fn()
        dg = bpy.context.evaluated_depsgraph_get(); out = {}
        MATN = {"POB_white": "White", "POB_red": "Red", "POB_dark": "Dark", "POB_metal": "Metal", "POB_grey": "Grey"}
        for o in parts:
            if o.modifiers:
                o.modifiers[0].levels = 1
                d = o.modifiers.new("flat", 'DECIMATE'); d.decimate_type = 'DISSOLVE'; d.angle_limit = math.radians(0.6)
        dg.update()
        for o in parts:
            oe = o.evaluated_get(dg); me = oe.to_mesh(); me.calc_loop_triangles(); M = o.matrix_world; N = M.to_3x3().inverted().transposed()
            mn = MATN.get(me.materials[0].name.split(".")[0], "White") if me.materials else "White"
            g = out.setdefault(mn, {"v": [], "t": []})
            for lt in me.loop_triangles:
                b = len(g["v"])
                for li in lt.loops:
                    co = M @ me.vertices[me.loops[li].vertex_index].co; nn = (N @ me.corner_normals[li].vector).normalized()
                    g["v"].append([round(co.x, 5), round(co.y, 5), round(co.z, 5), round(nn.x, 4), round(nn.y, 4), round(nn.z, 4)])
                g["t"].append([b, b + 1, b + 2])
            oe.to_mesh_clear()
        dst = os.path.abspath(os.path.join(HERE, "..", "tools", "jetpack_mono.json"))
        json.dump({"concept": key, "frame": "Blender (x her left, y back, z up), m", "groups": out}, open(dst, "w", encoding="utf-8"))
        print("EXPORTED", dst, {k: len(v["t"]) for k, v in out.items()}, file=LOG)
    except Exception:
        import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
    LOG.close(); raise SystemExit
try:
    arm = bpy.data.objects["Astronavigator"]
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 600, 600; sc.render.film_transparent = True
    for nm, en, rot in (("PB_key", 3.0, (42, 0, 145)), ("PB_fill", 1.4, (65, 0, -40)), ("PB_low", 0.9, (120, 0, 160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("PB_cam"); cam = bpy.data.objects.new("PB_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    VIEWS = {"q34": ((1.25, 1.9, 1.75), (0, 0.2, 1.33), 'PERSP', 42), "side": ((3.2, 0.25, 1.33), (0, 0.25, 1.33), 'ORTHO', 0), "back": ((0, 3.2, 1.33), (0, 0.25, 1.33), 'ORTHO', 0)}
    ONLY = os.environ.get("POB_ONLY", "")
    MP = os.path.join(OUT, "pob_metrics.json"); metrics = json.load(open(MP, encoding="utf-8")) if os.path.exists(MP) else {}
    HEAT = ONLY and all(int(k) > 10 for k in ONLY.split(","))          # round 2: the heat, the sublimator, the helmet
    if HEAT:
        static = [o for o in sc.objects if o.type == 'MESH' and not o.hide_render and o.visible_get()]
        st_tree = tree_of(static); shells = G["shells"]; sublim = [o for o in G["outside"] if o.name.startswith("sublim")]
        helm = [o for o in static if o.name.startswith("Helm")]
        arm.data.pose_position = 'POSE'
        hb = [b for b in arm.pose.bones if "neck" in b.name.lower() or "head" in b.name.lower()]
        print("helmet", [o.name for o in helm], "bones", [b.name for b in hb], file=LOG)
        def helm_pts():
            dg = bpy.context.evaluated_depsgraph_get(); P = []
            for o in helm:
                oe = o.evaluated_get(dg); M = oe.matrix_world; me = oe.to_mesh(); P += [M @ v.co for v in list(me.vertices)[::5]]; oe.to_mesh_clear()
            return P
        NECK = arm.matrix_world @ arm.pose.bones["Neck"].head; M0 = {o: o.matrix_world.copy() for o in helm}
        RT = Matrix.Translation(NECK) @ Matrix.Rotation(math.radians(-40), 4, 'X') @ Matrix.Translation(-NECK)
        def tilt(ang):
            for o in helm: o.matrix_world = (RT @ M0[o]) if ang else M0[o]
            bpy.context.view_layer.update()
        cen = lambda P: sum(P, Vector()) / len(P); top0 = cen(helm_pts()); tilt(1); top1 = cen(helm_pts()); TILT = 1; tilt(0)
        print("neck", NECK, "helmet centre", top0, "->", top1, file=LOG)
        base = view_area(shells, [st_tree]); metrics["base"] = dict(block_A=base, by={o.name: view_area([o], [st_tree]) for o in shells}, sublim_F=view_area(sublim, [st_tree]) / max(1e-9, sum(a for o in sublim for c, n, a in faces_world(o))))
        print("base", metrics["base"], file=LOG); LOG.flush()
    for key, name, fn in CONCEPTS:
        if ONLY and key not in ONLY.split(","): continue
        DROP = ("Body", "Core", "CoreRed", "Ports", "Brackets") + (("Posts", "PostsRed") if int(key) > 20 else ())   # round 3: the screw in the node
        def hexa_build(folded):
            hx = build_hexa(folded=folded, hide_figure_pack=True)
            for o in list(hx):
                if o.name.split(".")[0].replace("Hexa_", "") in DROP: hx.remove(o); bpy.data.objects.remove(o, do_unlink=True)
            return hx
        hexa = hexa_build(False)
        if HEAT and "base_hexa" not in metrics:
            metrics["base_hexa"] = dict(block_A=view_area(shells, [st_tree, tree_of(hexa)])); print("base_hexa", metrics["base_hexa"], file=LOG)
        fn()
        ys = [(o.matrix_world @ v.co).y for o in parts for v in o.data.vertices]
        metrics[key] = dict(name=name, tank_l=volume(tanks) * 1000, depth=max(ys) - 0.095, cover=coverage(parts))
        if HEAT:
            dy_tree = tree_of(parts + hexa); m_ = metrics[key]; trees = [st_tree, dy_tree]
            m_["by"] = {o.name: view_area([o], trees) for o in shells}; m_["block_A"] = sum(m_["by"].values())
            m_["pack_A"] = view_area([o for o in parts + hexa if o.name.startswith(PLUMBED.get(key, ("-",)))], trees)
            m_["sublim_F"] = view_area(sublim, trees) / max(1e-9, sum(a for o in sublim for c, n, a in faces_world(o)))
            pk = tree_of(parts); m_["helm_clear"] = min(pk.find_nearest(v)[3] for v in helm_pts())
            tilt(TILT); hq = min(((pk.find_nearest(v)[3], tuple(round(c, 3) for c in v)) for v in helm_pts()), key=lambda q: q[0])
            m_["helm_clear_tilt"], m_["helm_where"] = hq
            m_["fuel_kg"] = m_["tank_l"] * 0.87 * 1.15; m_["hover_moon_s"] = 10000 / 1.62 * math.log((122 + m_["fuel_kg"]) / 122)
            zs = [(o.matrix_world @ v.co).z for o in tanks for v in o.data.vertices]; m_["tank_zc"] = sum(zs) / len(zs)
        print(key, metrics[key], file=LOG); LOG.flush()
        VV = dict(VIEWS)
        if HEAT: VV = {"tilt": VIEWS["side"], **VIEWS}
        for vn, (loc, tgt, typ, lens) in ({} if os.environ.get("POB_NORENDER") else VV).items():
            if HEAT and vn == "q34": tilt(0)
            cd.type = typ; cd.lens = lens or 50; cd.ortho_scale = 1.15
            cam.location = Vector(loc); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "pob_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
        if int(key) > 20:          # the fold: the booms swung back over the arch - with the screw's 9 cm lift, and without
            if HEAT: tilt(0)
            for o in hexa: bpy.data.objects.remove(o, do_unlink=True)
            pk = tree_of([o for o in parts if not o.name.startswith("node")])   # the node holds the boom's root by design
            for tag, lv in (("fold_clear", None), ("fold_clear_nolift", 0.0)):
                if lv is None: globals().pop("HEXA_LIFT_OVERRIDE", None)
                else: globals()["HEXA_LIFT_OVERRIDE"] = lv
                hexa = hexa_build(True); dg = bpy.context.evaluated_depsgraph_get(); dmin = 9.0; where = None
                for o in hexa:
                    if o.name.split(".")[0].replace("Hexa_", "")[:4] not in ("Stag", "Pod", "PodB", "Coll", "Glow"): continue
                    me = o.evaluated_get(dg).to_mesh(); M = o.matrix_world
                    for v in me.vertices:
                        w = M @ v.co
                        if abs(abs(w.x) - 0.204) < 0.045 and abs(w.y - 0.186) < 0.05 and w.z > 1.40: continue   # the boom's root in its node
                        q = pk.find_nearest(w)
                        if q[3] < dmin: dmin, where = q[3], (o.name, tuple(round(c, 3) for c in M @ v.co))
                    o.evaluated_get(dg).to_mesh_clear()
                metrics[key][tag] = dmin; print(key, tag, dmin, where, file=LOG); LOG.flush()
                if (lv is None) == (int(key) < 26) and not os.environ.get("POB_NORENDER"):   # the monoliths fold without the lift
                    loc, tgt, typ, lens = VIEWS["q34"]; cd.type = typ; cd.lens = lens; cam.location = Vector(loc)
                    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
                    sc.render.filepath = os.path.join(OUT, "pob_%s_fold.png" % key); bpy.ops.render.render(write_still=True)
                for o in hexa: bpy.data.objects.remove(o, do_unlink=True)
                hexa = []
            globals().pop("HEXA_LIFT_OVERRIDE", None)
        for o in parts + hexa: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear(); tanks.clear()
    json.dump(metrics, open(MP, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
