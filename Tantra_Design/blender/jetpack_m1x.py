# «Наплечный» M1 finished: ten closed bodies and the faceted capsules - see the notes below; the helpers are those of
# jetpack_m2body.py.  blender --background astronavigator_suit.blend --python jetpack_m1x.py -> renders\m1x_<0..10>_<rear|side|back>.png
# «Наплечный» M2 - the body designed anew (the user, 2026-10-17: «доработай форму согласно моим предложениям и скринам,
# с нуля, только УВТ не менять»). Kept exactly as they are: the booms, their hinges and the capsule pods (the thrust
# vector). Built new, in the family's materials (JetShell, JetDark, JetRed, JetSeam, JetPlug):
#   the back plate against her back - the pack's spine, everything hangs on it;
#   the tank section - two upright metallic-hydrogen tanks in a bar cage between armoured cheeks, under a lid that
#     carries the hinges (the UMMU turbopack: the tanks in their frame, on show);
#   the lumbar propulsion block - a rounded armoured block on her lower back with two kidney panels round the hips
#     (the Off the Grid packs: the propulsion low, hugging the body, nothing on arms);
#   the main engine out of the block's base, down and 20 deg aft, in a gimbal ring with trunnions (+-10 deg: the
#     cruise thrust and its vector); the attitude ports in four corner clusters.
#   blender --background astronavigator_suit.blend --python jetpack_m2body.py -> renders\m2b_<rear|side|back|low>.png
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "m1x_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
KEEP = {"JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR"}
for o in bpy.data.objects:
    if o.name.startswith("Jet") and o.name not in KEEP: o.hide_render = True
SHELL, DARK, RED, SEAM, PLUG = (bpy.data.materials[k] for k in ("JetShell", "JetDark", "JetRed", "JetSeam", "JetPlug"))
FRAME = bpy.data.materials.new("M2_frame"); FRAME.use_nodes = True
_b = next(n for n in FRAME.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
_b.inputs["Base Color"].default_value = (0.52, 0.54, 0.57, 1); _b.inputs["Metallic"].default_value = 0.85; _b.inputs["Roughness"].default_value = 0.3
parts = []

def obj(name, bm, mat, smooth=True):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(mat)
    if smooth:
        for p in me.polygons: p.use_smooth = True
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); parts.append(o); return o

def quad(bm, a, b, c, d):
    vs = []
    for v in (a, b, c, d):
        if v not in vs: vs.append(v)
    if len(vs) >= 3:
        try: bm.faces.new(vs)
        except ValueError: pass

def sbox(name, c, r, mat, e1=0.4, e2=0.4, rot=None, nu=36, nv=18):
    """a superellipsoid: a rounded armoured block (e -> 0 boxy, 1 an ellipsoid)"""
    def sp(w, e): return math.copysign(abs(w) ** e, w)
    bm = bmesh.new(); rings = []
    for i in range(nv + 1):
        th = -math.pi / 2 + math.pi * i / nv
        if i == 0 or i == nv: rings.append([bm.verts.new(Vector((0, 0, r[2] * sp(math.sin(th), e1))))] * nu); continue
        rings.append([bm.verts.new(Vector((r[0] * sp(math.cos(th), e1) * sp(math.cos(2 * math.pi * j / nu), e2),
                                            r[1] * sp(math.cos(th), e1) * sp(math.sin(2 * math.pi * j / nu), e2), r[2] * sp(math.sin(th), e1)))) for j in range(nu)])
    for i in range(nv):
        for j in range(nu): quad(bm, rings[i][j], rings[i][(j + 1) % nu], rings[i + 1][(j + 1) % nu], rings[i + 1][j])
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ (rot or Matrix.Identity(4)), verts=bm.verts)
    return obj(name, bm, mat)

def prism(name, outline, t0, t1, mat, ch=0.006, M=None, smooth=False):
    """a chamfered slab: the outline (local x, y) extruded along local z from t0 to t1; M places it"""
    n = len(outline); cx = sum(p[0] for p in outline) / n; cy = sum(p[1] for p in outline) / n
    bm = bmesh.new()
    ext = max(math.hypot(x - cx, y - cy) for x, y in outline); s_in = 1 - ch / ext
    def ring(s, z): return [bm.verts.new(Vector((cx + (x - cx) * s, cy + (y - cy) * s, z))) for x, y in outline]
    L = [ring(s_in, t0), ring(1, t0 + ch), ring(1, t1 - ch), ring(s_in, t1)]
    bm.faces.new(L[0][::-1]); bm.faces.new(L[3])
    for a, b in zip(L, L[1:]):
        for k in range(n): quad(bm, a[k], a[(k + 1) % n], b[(k + 1) % n], b[k])
    if M is not None: bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
    return obj(name, bm, mat, smooth)

def lathe(name, c, axis, prof, mat, nu=36, smooth=True):
    """revolve (r, t) about the axis through c (t along it)"""
    A = Vector(axis).normalized(); T = A.cross(Vector((1, 0, 0)) if abs(A.x) < 0.9 else Vector((0, 0, 1))).normalized(); S = A.cross(T)
    bm = bmesh.new(); rings = []
    for r, t in prof:
        if r < 1e-6: rings.append([bm.verts.new(Vector(c) + A * t)] * nu)
        else: rings.append([bm.verts.new(Vector(c) + A * t + T * (r * math.cos(2 * math.pi * j / nu)) + S * (r * math.sin(2 * math.pi * j / nu))) for j in range(nu)])
    for a, b in zip(rings, rings[1:]):
        for j in range(nu): quad(bm, a[j], a[(j + 1) % nu], b[(j + 1) % nu], b[j])
    return obj(name, bm, mat, smooth)

def bar(name, a, b, r, mat=None, nu=12):
    a, b = Vector(a), Vector(b); L = (b - a).length
    return lathe(name, a, b - a, [(0, 0), (r, 0), (r, L), (0, L)], mat or FRAME, nu)

def belt(name, c, r, mat, h, e=0.4, rot=None, nu=48):
    """a thin band round a superellipsoid's waist (a panel seam, a red line)"""
    def sp(w, e_): return math.copysign(abs(w) ** e_, w)
    bm = bmesh.new(); rings = []
    for z in (-h / 2, h / 2):
        rings.append([bm.verts.new(Vector((r[0] * 1.004 * sp(math.cos(2 * math.pi * j / nu), e), r[1] * 1.004 * sp(math.sin(2 * math.pi * j / nu), e), z))) for j in range(nu)])
    for j in range(nu): quad(bm, rings[0][j], rings[0][(j + 1) % nu], rings[1][(j + 1) % nu], rings[1][j])
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ (rot or Matrix.Identity(4)), verts=bm.verts)
    return obj(name, bm, mat)

def rect(w, h, cut=0.0):
    if cut <= 0: return [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
    return [(-w / 2 + cut, -h / 2), (w / 2 - cut, -h / 2), (w / 2, -h / 2 + cut), (w / 2, h / 2 - cut), (w / 2 - cut, h / 2), (-w / 2 + cut, h / 2), (-w / 2, h / 2 - cut), (-w / 2, -h / 2 + cut)]

RX = lambda deg: Matrix.Rotation(math.radians(deg), 4, 'X')
RY = lambda deg: Matrix.Rotation(math.radians(deg), 4, 'Y')
RZ = lambda deg: Matrix.Rotation(math.radians(deg), 4, 'Z')
BACK = 0.108                                                     # her back plane (where M1's shell face was)


# ==== «Наплечный» M1, the body finished: ten closed bodies (no open tanks), the booms and hinges as they are, the
# capsules kept in silhouette but faceted (the user, 2026-10-17: «элегантно и технично, 30 век; УВТ можно немного
# изменить, убрав округлости и добавив углы/грани/рёбра; 10 вариантов самого ранца: эргономика, вместимость,
# удобство, надёжность»). Each body is measured: its volume -> kg of metallic hydrogen, its depth off her back, its
# width at her elbows, the fuel's centre behind her - written to renders\m1x_metrics.json for the sheet. ====
import json
POD_C = {1: Vector((0.597, 0.175, 1.471)), -1: Vector((-0.597, 0.175, 1.471))}
POD_R, POD_H = 0.047, 0.236
PROF = [(0.0, 0.5), (0.45, 0.47), (0.75, 0.38), (0.92, 0.24), (1.0, 0.08), (1.0, -0.12), (0.97, -0.30), (0.90, -0.42), (0.80, -0.5)]

def poly_lathe(name, c, axis, prof, mat, nu=8, phase=22.5):
    """a faceted body of revolution: flat facets, a vertex phase so a flat faces outboard"""
    A = Vector(axis).normalized(); T = A.cross(Vector((1, 0, 0)) if abs(A.x) < 0.9 else Vector((0, 0, 1))).normalized(); S = A.cross(T)
    bm = bmesh.new(); rings = []
    for r, t in prof:
        if r < 1e-6: rings.append([bm.verts.new(Vector(c) + A * t)] * nu)
        else: rings.append([bm.verts.new(Vector(c) + A * t + T * (r * math.cos(math.radians(phase) + 2 * math.pi * j / nu)) + S * (r * math.sin(math.radians(phase) + 2 * math.pi * j / nu))) for j in range(nu)])
    for a, b in zip(rings, rings[1:]):
        for j in range(nu): quad(bm, a[j], a[(j + 1) % nu], b[(j + 1) % nu], b[j])
    return obj(name, bm, mat, smooth=False)

def fpod(sg):
    """the capsule, faceted: an octagonal egg with M1's silhouette, its red band, the annular exit and the plug"""
    c = POD_C[sg]
    poly_lathe("fpod%d" % sg, c, (0, 0, 1), [(r * POD_R, y * POD_H) for r, y in PROF], SHELL)
    poly_lathe("fpodband%d" % sg, c, (0, 0, 1), [(POD_R * 1.03, 0.012), (POD_R * 1.03, -0.012)], RED)
    for k in range(4):   # four longitudinal ribs on the facets' edges, the 30th-century technic
        a = math.radians(45 * k + 22.5 + 45)
        bar("fpodrib%d_%d" % (sg, k), c + Vector((POD_R * 0.9 * math.cos(a), POD_R * 0.9 * math.sin(a), 0.09)), c + Vector((POD_R * 0.98 * math.cos(a), POD_R * 0.98 * math.sin(a), -0.07)), 0.004, SEAM, 6)
    poly_lathe("fpodexit%d" % sg, c, (0, 0, 1), [(POD_R * 0.8, -0.5 * POD_H - 0.001), (POD_R * 0.38, -0.5 * POD_H - 0.001)], DARK)
    poly_lathe("fpodplug%d" % sg, c, (0, 0, 1), [(POD_R * 0.38, -0.5 * POD_H - 0.002), (0.0, -0.5 * POD_H - 0.05)], PLUG)

def arc_outline(cy, r_in, r_out, half_deg, n=24):
    """the plan outline of a shell wrapping her back: the outer arc, then the inner arc back (centre (0, cy))"""
    angs = [math.radians(-half_deg + 2 * half_deg * i / n) for i in range(n + 1)]
    return ([(r_out * math.sin(a), cy + r_out * math.cos(a)) for a in angs] + [(r_in * math.sin(a), cy + r_in * math.cos(a)) for a in reversed(angs)])[::-1]   # counter-clockwise, as rect()

def arc_shell(name, cy, r_in, r_out, half_deg, z0, z1, mat, n=16):
    """a shell wrapping her back, faceted: n convex segments between the radii, each a chamfered prism"""
    angs = [math.radians(-half_deg + 2 * half_deg * i / n) for i in range(n + 1)]
    for i in range(n):
        a0, a1 = angs[i], angs[i + 1]
        out = [(r_in * math.sin(a0), cy + r_in * math.cos(a0)), (r_in * math.sin(a1), cy + r_in * math.cos(a1)),
               (r_out * math.sin(a1), cy + r_out * math.cos(a1)), (r_out * math.sin(a0), cy + r_out * math.cos(a0))]
        prism("%s%d" % (name, i), out, z0, z1, mat, ch=min(0.004, (z1 - z0) / 3), smooth=False)

BACK = 0.108
def yoke():   # the top beam the hinges sit on - the same on every body (the hinges are not moved)
    prism("B_yoke", rect(0.41, 0.09, 0.015), 0, 0.045, SHELL, M=Matrix.Translation(Vector((0, 0.185, 1.455))))
def ports(pts):   # the attitude ports: dark discs at the body's corners
    for i, (p, d) in enumerate(pts): lathe("port%d" % i, Vector(p) - Vector(d) * 0.002, d, [(0, 0), (0.011, 0), (0.011, 0.004), (0, 0.004)], DARK, 12)
def coupling():   # the suit coupling on the lower back - the hose port, as M1's JetCoupling
    prism("coup", rect(0.05, 0.05), 0, 0.03, DARK, M=Matrix.Translation(Vector((0.14, 0.26, 1.18))) @ RY(90))

def v0():   # M1 as it is: its own shell back on
    for n in ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling"): bpy.data.objects[n].hide_render = False
def v1():   # «Грань»: the slab, big chamfers, three ribs, a flat back
    prism("B_slab", rect(0.41, 0.48, 0.06), 0, 0.232, SHELL, ch=0.028, M=Matrix.Translation(Vector((0, BACK, 1.27))) @ RX(-90))
    for x in (-0.12, 0.0, 0.12): prism("rib%d" % int(x * 100), rect(0.016, 0.36), 0, 0.008, SHELL, M=Matrix.Translation(Vector((x, 0.34, 1.27))) @ RX(-90))
    prism("band", rect(0.414, 0.012, 0.0), 0.02, 0.23, RED, ch=0.001, M=Matrix.Translation(Vector((0, BACK, 1.41))) @ RX(-90))
    ports([((0.17, 0.342, 1.48), (0, 1, 0)), ((-0.17, 0.342, 1.48), (0, 1, 0)), ((0.17, 0.342, 1.06), (0, 1, 0)), ((-0.17, 0.342, 1.06), (0, 1, 0))])
def wedge(name, y_top, y_bot, z0=1.02, z1=1.52):
    out = [(-z0, BACK), (-z0, y_bot), (-z1, y_top), (-z1, BACK)]   # (local x = -z, local y = y)
    prism(name, out, -0.20, 0.20, SHELL, ch=0.02, M=RY(90))
def v2():   # «Клин»: deep at the waist, thin at the shoulders - the mass low, the head free
    wedge("B_wedge", 0.26, 0.40)
    belt_y = 0.40 - (0.40 - 0.26) * (1.12 - 1.02) / 0.5
    prism("band", rect(0.404, 0.012), 0, 0.012, RED, ch=0.001, M=Matrix.Translation(Vector((0, belt_y - 0.004, 1.12))) @ RX(-90) @ RX(-16))
    ports([((0.17, 0.27, 1.48), (0, 1, 0)), ((-0.17, 0.27, 1.48), (0, 1, 0)), ((0.17, 0.39, 1.05), (0, 1, 0)), ((-0.17, 0.39, 1.05), (0, 1, 0))])
def v3():   # «Обратный клин»: deep under the booms (their feed), thin at the waist - the arms swing free
    wedge("B_wedge", 0.40, 0.24)
    prism("band", rect(0.404, 0.012), 0, 0.012, RED, ch=0.001, M=Matrix.Translation(Vector((0, 0.372, 1.44))) @ RX(-90) @ RX(18))
    ports([((0.17, 0.39, 1.49), (0, 1, 0)), ((-0.17, 0.39, 1.49), (0, 1, 0)), ((0.17, 0.25, 1.05), (0, 1, 0)), ((-0.17, 0.25, 1.05), (0, 1, 0))])
def v4():   # «Талия»: the depth pinched at the elbows' height, two lobes of volume
    out = [(-1.02, BACK), (-1.02, 0.36), (-1.15, 0.36), (-1.27, 0.25), (-1.39, 0.36), (-1.52, 0.36), (-1.52, BACK)]
    prism("B_waist", out, -0.20, 0.20, SHELL, ch=0.02, M=RY(90))
    for z in (1.09, 1.45): prism("band%d" % int(z * 100), rect(0.404, 0.012), 0, 0.012, RED, ch=0.001, M=Matrix.Translation(Vector((0, 0.356, z))) @ RX(-90))
    ports([((0.17, 0.36, 1.48), (0, 1, 0)), ((-0.17, 0.36, 1.48), (0, 1, 0)), ((0.17, 0.36, 1.06), (0, 1, 0)), ((-0.17, 0.36, 1.06), (0, 1, 0))])
def v5():   # «Двухлистник»: two independent tanks with the plumbing in a protected spine between them
    for sg in (1, -1):
        sbox("B_lobe%d" % sg, (sg * 0.105, 0.23, 1.265), (0.095, 0.115, 0.235), SHELL, 0.25, 0.3)
        belt("lband%d" % sg, (sg * 0.105, 0.23, 1.40), (0.095, 0.115), RED, 0.012, 0.3)
    prism("spine", rect(0.05, 0.44), 0, 0.18, DARK, ch=0.004, M=Matrix.Translation(Vector((0, BACK + 0.01, 1.27))) @ RX(-90))
    yoke()
    ports([((0.19, 0.30, 1.47), (0, 1, 0)), ((-0.19, 0.30, 1.47), (0, 1, 0)), ((0.19, 0.30, 1.06), (0, 1, 0)), ((-0.19, 0.30, 1.06), (0, 1, 0))])
def v6():   # «Щит»: a shell wrapping her back, horizontal ribs - low and wide, nothing sticks out
    arc_shell("B_shield", -0.25, 0.36, 0.52, 38, 1.02, 1.52, SHELL)
    for z in (1.10, 1.27, 1.44): arc_shell("rib%d" % int(z * 100), -0.25, 0.52, 0.53, 36, z - 0.006, z + 0.006, SEAM)
    arc_shell("band", -0.25, 0.52, 0.532, 36, 1.355, 1.37, RED)
    yoke()
    ports([((0.25, 0.20, 1.48), (0.6, 0.8, 0)), ((-0.25, 0.20, 1.48), (-0.6, 0.8, 0)), ((0.25, 0.20, 1.06), (0.6, 0.8, 0)), ((-0.25, 0.20, 1.06), (-0.6, 0.8, 0))])
def v7():   # «Призма»: a stretched hexagonal prism - six facets, the honeycomb motif
    out = [(-0.11, BACK), (0.11, BACK), (0.22, 0.23), (0.11, 0.35), (-0.11, 0.35), (-0.22, 0.23)]
    prism("B_hex", out, 1.02, 1.52, SHELL, ch=0.02)
    prism("band", [(x * 1.01, BACK + (y - BACK) * 1.03) for x, y in out], 1.395, 1.41, RED, ch=0.001)
    yoke()
    ports([((0.11, 0.352, 1.48), (0, 1, 0)), ((-0.11, 0.352, 1.48), (0, 1, 0)), ((0.11, 0.352, 1.06), (0, 1, 0)), ((-0.11, 0.352, 1.06), (0, 1, 0))])
def v8():   # «Ступень»: a thin upper plate (the hinges, the electronics), the tank a deep block at the lumbar
    prism("B_plate", rect(0.40, 0.30, 0.03), 0, 0.08, SHELL, ch=0.012, M=Matrix.Translation(Vector((0, BACK, 1.37))) @ RX(-90))
    sbox("B_tank", (0, 0.24, 1.10), (0.20, 0.13, 0.12), SHELL, 0.3, 0.3)
    belt("band", (0, 0.24, 1.14), (0.20, 0.13), RED, 0.012, 0.3)
    ports([((0.17, 0.19, 1.49), (0, 1, 0)), ((-0.17, 0.19, 1.49), (0, 1, 0)), ((0.17, 0.372, 1.02), (0, 1, 0)), ((-0.17, 0.372, 1.02), (0, 1, 0))])
def v9():   # «Ребристый»: the box with five fins out of its back - stiffness, protection, heat rejection
    prism("B_box", rect(0.38, 0.46, 0.03), 0, 0.19, SHELL, ch=0.012, M=Matrix.Translation(Vector((0, BACK, 1.27))) @ RX(-90))
    for i, x in enumerate((-0.14, -0.07, 0.0, 0.07, 0.14)):
        prism("fin%d" % i, rect(0.40, 0.07, 0.015), 0, 0.012, SHELL, ch=0.002, M=Matrix.Translation(Vector((x, 0.335, 1.27))) @ RY(90))
    prism("band", rect(0.384, 0.012), 0.02, 0.19, RED, ch=0.001, M=Matrix.Translation(Vector((0, BACK, 1.43))) @ RX(-90))
    ports([((0.17, 0.30, 1.49), (0, 1, 0)), ((-0.17, 0.30, 1.49), (0, 1, 0)), ((0.17, 0.30, 1.05), (0, 1, 0)), ((-0.17, 0.30, 1.05), (0, 1, 0))])
def v10():  # «Карапакс»: three segments, the middle one deeper, dark seams - it follows the back's bend
    for z, dy, h in ((1.10, 0.0, 0.085), (1.27, 0.012, 0.08), (1.44, 0.0, 0.08)):
        sbox("B_seg%d" % int(z * 100), (0, 0.225 + dy, z), (0.20, 0.115 + dy, h), SHELL, 0.25, 0.3)
    belt("band", (0, 0.237, 1.27), (0.20, 0.127), RED, 0.012, 0.3)
    ports([((0.17, 0.335, 1.49), (0, 1, 0)), ((-0.17, 0.335, 1.49), (0, 1, 0)), ((0.17, 0.335, 1.05), (0, 1, 0)), ((-0.17, 0.335, 1.05), (0, 1, 0))])

VARS = [("0", "М1 как есть", v0), ("1", "Грань", v1), ("2", "Клин", v2), ("3", "Обратный клин", v3), ("4", "Талия", v4), ("5", "Двухлистник", v5),
        ("6", "Щит", v6), ("7", "Призма", v7), ("8", "Ступень", v8), ("9", "Ребристый", v9), ("10", "Карапакс", v10)]

def measure(key):
    """the body's volume (its B_ parts), the fuel's centre, its depth off her back, its width at the elbows (z 1.1..1.2)"""
    if key == "0":
        names = ("JetShell", "JetVessel"); objs = [bpy.data.objects[n] for n in names]
    else: objs = [o for o in parts if o.name.startswith("B_")]
    vol, cy, n = 0.0, 0.0, 0; xmax = 0.0; ymax = 0.0; xe = 0.0
    for o in objs:
        bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world)
        try: v = abs(bm.calc_volume())
        except Exception: v = 0.0
        vol += v; cs = [f.calc_center_median() for f in bm.faces]; cy += sum(c.y for c in cs) / max(1, len(cs)) * v; n += 1
        for vtx in bm.verts:
            p = vtx.co; xmax = max(xmax, abs(p.x)); ymax = max(ymax, p.y)
            if 1.10 <= p.z <= 1.20: xe = max(xe, abs(p.x))
        bm.free()
    if key == "0": vol = 0.406 * 0.218 * 0.475 * 0.80   # M1's shell is not a closed mesh: its box, less the niche
    cy = cy / vol if vol else 0.0
    return dict(volume=vol, cy=cy, depth=ymax - BACK, width=2 * xmax, width_elbow=2 * xe)

try:
    for n in ("JetPodL", "JetPodR", "JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling"): bpy.data.objects[n].hide_render = True
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 640, 640; sc.render.film_transparent = True
    for nm, en, rot in (("MX_key", 3.0, (42, 0, 145)), ("MX_fill", 1.3, (65, 0, -40)), ("MX_low", 0.9, (120, 0, 160)), ("MX_front", 1.0, (60, 0, 54.5))):   # MX_front: from her right-front, for the faces that look forward
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("MX_cam"); cam = bpy.data.objects.new("MX_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    VIEWS = {"rear": ((1.35, 2.0, 1.25), (0, 0.25, 1.27), 'PERSP', 48, 0), "side": ((3.5, 0.25, 1.27), (0, 0.25, 1.27), 'ORTHO', 0, 0.95),
             "back": ((0, 3.5, 1.27), (0, 0.25, 1.27), 'ORTHO', 0, 1.05)}
    metrics = {}
    ONLY = os.environ.get("M1X_ONLY", "")
    if os.path.exists(os.path.join(OUT, "m1x_metrics.json")): metrics = json.load(open(os.path.join(OUT, "m1x_metrics.json"), encoding="utf-8"))
    for key, name, fn in VARS:
        if ONLY and key not in ONLY.split(","): continue
        if key == "0":
            for n in ("JetPodL", "JetPodR"): bpy.data.objects[n].hide_render = False
            fn()
        else:
            fn(); coupling(); fpod(1); fpod(-1)
        metrics[key] = dict(name=name, **measure(key))
        for vn, (loc, tgt, typ, lens, osc) in VIEWS.items():
            cd.type = typ; cd.lens = lens or 50; cd.ortho_scale = osc or 1
            cam.location = Vector(loc); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "m1x_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
        if key == "0":
            for n in ("JetPodL", "JetPodR", "JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling"): bpy.data.objects[n].hide_render = True
    json.dump(metrics, open(os.path.join(OUT, "m1x_metrics.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
