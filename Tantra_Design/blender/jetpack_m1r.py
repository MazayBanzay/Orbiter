# «Наплечный» M1, round two: the ribbed bodies, the twin-tube booms, the faceted capsules, the three failure cases -
# see the notes below; helpers from jetpack_m2body.py.  blender --background astronavigator_suit.blend --python jetpack_m1r.py
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
LOG = open(os.path.join(OUT, "m1r_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
KEEP = {"JetHingeL", "JetHingeR"}
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


# ==== «Наплечный» M1, round two (the user, 2026-10-17): the bodies of the ribbed family - ten new ones (the earlier ten
# «мимо, кроме Ребристого»), the booms redone as 30th-century twin-tube plumbing with their hinges, the capsules faceted;
# and three ways to live through the loss of one capsule: bank and descend, twin chambers in every capsule, a third
# (emergency) nozzle at the body's base. ====
import json
BACK = 0.108
J2 = {n: bpy.data.objects[n] for n in ("JetBoomL", "JetBoomR")}
for o in J2.values(): o.hide_render = True                       # the booms are redone below

def plate(name, c, w, h, out, mat=SHELL, rotz=0.0, ch=0.004, prof=None):
    """a fin standing out of the back face (+y) at c: w along x, h along z, `out` deep; rotz turns it in the back's plane"""
    outline = prof or rect(w, h, 0.012)
    prism(name, outline, 0, out, mat, ch=ch, M=Matrix.Translation(Vector(c)) @ RY(rotz) @ RX(-90))

def box(name, w, h, d, cz=1.27, ch=0.012, x=0.0):
    prism(name, rect(w, h, 0.03), 0, d, SHELL, ch=ch, M=Matrix.Translation(Vector((x, BACK, cz))) @ RX(-90))

def ports4(y, x=0.17, zt=1.49, zb=1.05):
    for i, (sx, z) in enumerate(((x, zt), (-x, zt), (x, zb), (-x, zb))):
        lathe("port%d" % i, (sx, y - 0.002, z), (0, 1, 0), [(0, 0), (0.011, 0), (0.011, 0.004), (0, 0.004)], DARK, 12)

def coupling(y=0.26):
    prism("coup", rect(0.05, 0.05), 0, 0.03, DARK, M=Matrix.Translation(Vector((0.14, y, 1.18))) @ RY(90))

def band(z, w=0.384, d=0.19, y=BACK):
    prism("band%d" % int(z * 100), rect(w + 0.004, 0.012), 0.02, d + 0.003, RED, ch=0.001, M=Matrix.Translation(Vector((0, y, z))) @ RX(-90))   # a raised red line round the box

def boom2(sg, twin=False):
    """the boom redone: two tubes one above the other with a web between (a box section in bending, the hydrogen line
    inside the upper tube, the return in the lower), a collar at the capsule, the hinge as it is"""
    x0, x1 = sg * 0.215, sg * 0.552; y, z = 0.18, 1.492
    for dz, r in ((0.022, 0.014), (-0.022, 0.012)):
        lathe("btube%d_%d" % (sg, int(dz * 1000)), (x0, y, z + dz), (sg, 0, 0), [(0, 0), (r, 0), (r, abs(x1 - x0)), (0, abs(x1 - x0))], SHELL, 16)
    prism("bweb%d" % sg, rect(abs(x1 - x0) - 0.03, 0.036), -0.004, 0.004, SHELL, ch=0.001, M=Matrix.Translation(Vector(((x0 + x1) / 2, y, z))) @ RX(-90))
    if twin:   # the second line: a conduit along the web's rear, for the second chamber
        lathe("bline%d" % sg, (x0, y + 0.012, z), (sg, 0, 0), [(0, 0), (0.006, 0), (0.006, abs(x1 - x0)), (0, abs(x1 - x0))], SEAM, 10)
    poly_lathe("bcollar%d" % sg, (x1, y, z), (sg, 0, 0), [(0, -0.01), (0.045, -0.01), (0.045, 0.04), (0.0, 0.04)], SHELL, 8, 22.5)
    poly_lathe("bcollarr%d" % sg, (x1 + sg * 0.012, y, z), (sg, 0, 0), [(0.047, 0), (0.047, 0.01)], RED, 8, 22.5)

def fpod_twin(sg):
    """the capsule with two chambers: the exit split by a bridge, two plugs, each chamber its own line"""
    c = POD_C[sg]
    poly_lathe("fpod%d" % sg, c, (0, 0, 1), [(r * POD_R, y * POD_H) for r, y in PROF], SHELL)
    poly_lathe("fpodband%d" % sg, c, (0, 0, 1), [(POD_R * 1.03, 0.012), (POD_R * 1.03, -0.012)], RED)
    poly_lathe("fpodexit%d" % sg, c, (0, 0, 1), [(POD_R * 0.8, -0.5 * POD_H - 0.001), (POD_R * 0.0, -0.5 * POD_H - 0.001)], DARK)
    prism("fpodbridge%d" % sg, rect(0.012, 0.076), 0, 0.006, SHELL, ch=0.001, M=Matrix.Translation(Vector((c.x, c.y, c.z - 0.5 * POD_H - 0.006))))
    for dy in (-0.019, 0.019):
        poly_lathe("fpodplug%d_%d" % (sg, int(dy * 1000)), c + Vector((0, dy, 0)), (0, 0, 1), [(0.014, -0.5 * POD_H - 0.002), (0.0, -0.5 * POD_H - 0.036)], PLUG, 8)

def dead_pod(sg):
    """the failed capsule: the same, dark and cold"""
    c = POD_C[sg]
    poly_lathe("dpod%d" % sg, c, (0, 0, 1), [(r * POD_R, y * POD_H) for r, y in PROF], SEAM)
    poly_lathe("dpodplug%d" % sg, c, (0, 0, 1), [(POD_R * 0.38, -0.5 * POD_H - 0.002), (0.0, -0.5 * POD_H - 0.05)], DARK)

def emergency_nozzle():
    """the third nozzle: a small faceted one in the body's base, on the centre line, pointing down; 300 N"""
    c = Vector((0, 0.21, 1.03)); k = 0.75
    poly_lathe("en", c, (0, 0, -1), [(0.0, -0.03), (0.055 * k, 0.0), (0.06 * k, 0.05 * k), (0.056 * k, 0.09 * k), (0.045 * k, 0.09 * k)], SHELL, 8)
    poly_lathe("enexit", c, (0, 0, -1), [(0.045 * k, 0.09 * k), (0.02 * k, 0.085 * k)], DARK, 8)
    poly_lathe("enplug", c, (0, 0, -1), [(0.02 * k, 0.085 * k), (0.0, 0.12 * k)], PLUG, 8)
    poly_lathe("enring", c, (0, 0, -1), [(0.064 * k, 0.02 * k), (0.064 * k, 0.035 * k)], RED, 8)

# ---- the ten ribbed bodies ----
def r1():   # «Пять рёбер»: the box, five full-height fins, their ends chamfered
    box("B_box", 0.38, 0.46, 0.19)
    for x in (-0.14, -0.07, 0.0, 0.07, 0.14): plate("fin%d" % int(x * 100), (x, BACK + 0.19, 1.27), 0.012, 0.40, 0.055)
    band(1.43); ports4(0.30); coupling()
def r2():   # «Семь тонких»: narrower and deeper, seven thin fins - the radiator look
    box("B_box", 0.34, 0.46, 0.22)
    for i in range(7): plate("fin%d" % i, (-0.135 + i * 0.045, BACK + 0.22, 1.27), 0.008, 0.40, 0.05)
    band(1.43, 0.344, 0.22); ports4(0.33, 0.15); coupling()
def r3():   # «Горизонтальные»: six horizontal fins across the back - heat rejection, protection from above
    box("B_box", 0.38, 0.46, 0.19)
    for i in range(6): prism("hfin%d" % i, rect(0.36, 0.05, 0.01), 0, 0.01, SHELL, ch=0.002, M=Matrix.Translation(Vector((0, BACK + 0.19, 1.08 + i * 0.076))))
    band(1.43); ports4(0.30); coupling()
def r4():   # «Окантовка»: rails on the four edges round a smooth panel - a bumper frame
    box("B_box", 0.38, 0.46, 0.19)
    for x in (-0.175, 0.175): plate("rail%d" % int(x * 100), (x, BACK + 0.19, 1.27), 0.03, 0.46, 0.035)
    for z in (1.05, 1.49): prism("hrail%d" % int(z * 100), rect(0.38, 0.03, 0.006), 0, 0.035, SHELL, ch=0.003, M=Matrix.Translation(Vector((0, BACK + 0.19, z))))
    prism("panel", rect(0.30, 0.38, 0.02), 0, 0.006, SEAM, ch=0.001, M=Matrix.Translation(Vector((0, BACK + 0.19, 1.27))) @ RX(-90))
    band(1.43, 0.384, 0.19); ports4(0.33); coupling()
def r5():   # «Киль»: three fins, the middle one deep - the plumbing spine, a dark channel in its crest
    box("B_box", 0.38, 0.46, 0.19)
    for x in (-0.12, 0.12): plate("fin%d" % int(x * 100), (x, BACK + 0.19, 1.27), 0.012, 0.40, 0.05)
    plate("keel", (0, BACK + 0.19, 1.27), 0.03, 0.44, 0.09)
    prism("keelch", rect(0.014, 0.40), 0.09, 0.094, SEAM, ch=0.001, M=Matrix.Translation(Vector((0, BACK + 0.19, 1.27))) @ RX(-90))
    band(1.43); ports4(0.30); coupling()
def r6():   # «Шеврон»: four chevrons of slanted fins - stiff in torsion
    box("B_box", 0.38, 0.46, 0.19)
    for i in range(4):
        z = 1.10 + i * 0.11
        plate("chl%d" % i, (-0.085, BACK + 0.19, z), 0.19, 0.012, 0.045, rotz=math.radians(28))
        plate("chr%d" % i, (0.085, BACK + 0.19, z), 0.19, 0.012, 0.045, rotz=math.radians(-28))
    band(1.47, 0.384, 0.19); ports4(0.30); coupling()
def r7():   # «Решётка»: a shallow grid of fins
    box("B_box", 0.38, 0.46, 0.19)
    for x in (-0.12, 0.0, 0.12): plate("vf%d" % int(x * 100), (x, BACK + 0.19, 1.27), 0.01, 0.42, 0.035)
    for i in range(4): prism("hf%d" % i, rect(0.36, 0.01, 0.0), 0, 0.035, SHELL, ch=0.002, M=Matrix.Translation(Vector((0, BACK + 0.19, 1.10 + i * 0.115))))
    band(1.47, 0.384, 0.19); ports4(0.30); coupling()
def r8():   # «Радиатор снизу»: fins only on the lower half (the power unit's heat), the upper half smooth
    box("B_box", 0.38, 0.46, 0.19)
    for i in range(7): plate("fin%d" % i, (-0.15 + i * 0.05, BACK + 0.19, 1.14), 0.01, 0.19, 0.05)
    band(1.255); ports4(0.30); coupling()
def r9():   # «Бочка»: fins with a bulged outer edge - the barrel silhouette in side view, the box itself thinner
    box("B_box", 0.38, 0.46, 0.16)
    prof = [(-0.22, 0), (0.22, 0), (0.19, 0.05), (0.08, 0.085), (-0.08, 0.085), (-0.19, 0.05)]   # (height, out): the bulged outer edge
    for x in (-0.14, -0.07, 0.0, 0.07, 0.14):
        prism("bfin%d" % int(x * 100), prof, -0.006, 0.006, SHELL, ch=0.002, M=Matrix.Translation(Vector((x, BACK + 0.16, 1.27))) @ RY(90))
    band(1.43, 0.384, 0.16); ports4(0.27); coupling(0.24)
def r10():  # «Трубчатые»: the ribs are the hydrogen manifolds themselves - four armoured pipes with clamps, the header into the booms
    box("B_box", 0.38, 0.46, 0.17)
    for i, x in enumerate((-0.135, -0.045, 0.045, 0.135)):
        lathe("pipe%d" % i, (x, BACK + 0.17 + 0.02, 1.06), (0, 0, 1), [(0, 0), (0.017, 0), (0.017, 0.42), (0, 0.42)], FRAME, 14)
        for z in (1.12, 1.27, 1.42): prism("clamp%d_%d" % (i, int(z * 100)), rect(0.05, 0.028, 0.006), 0, 0.012, SEAM, ch=0.001, M=Matrix.Translation(Vector((x, BACK + 0.17 + 0.03, z))) @ RX(-90))
    lathe("header", (-0.17, BACK + 0.17 + 0.02, 1.485), (1, 0, 0), [(0, 0), (0.017, 0), (0.017, 0.34), (0, 0.34)], FRAME, 14)
    band(1.235, 0.384, 0.17); ports4(0.28, 0.17, 1.44, 1.05); coupling(0.23)

RIBBED = [("R1", "Пять рёбер", r1), ("R2", "Семь тонких", r2), ("R3", "Горизонтальные", r3), ("R4", "Окантовка", r4), ("R5", "Киль", r5),
          ("R6", "Шеврон", r6), ("R7", "Решётка", r7), ("R8", "Радиатор снизу", r8), ("R9", "Бочка", r9), ("R10", "Трубчатые", r10)]

def measure_body():
    vol, ymax, xmax = 0.0, 0.0, 0.0
    for o in parts:
        bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world)
        if o.name.startswith("B_"):
            try: vol += abs(bm.calc_volume())
            except Exception: pass
        if not o.name.startswith(("fpod", "bcollar", "btube", "bweb", "bline", "dpod")):
            for v in bm.verts: ymax = max(ymax, v.co.y); xmax = max(xmax, abs(v.co.x))
        bm.free()
    return dict(volume=vol, depth=ymax - BACK, width=2 * xmax)

try:
    for n in ("JetPodL", "JetPodR", "JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling"): bpy.data.objects[n].hide_render = True
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 640, 640; sc.render.film_transparent = True
    for nm, en, rot in (("MR_key", 3.0, (42, 0, 145)), ("MR_fill", 1.3, (65, 0, -40)), ("MR_low", 0.9, (120, 0, 160)), ("MR_front", 1.0, (60, 0, 54.5))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("MR_cam"); cam = bpy.data.objects.new("MR_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    def shoot(fname, loc, tgt, typ, lens, osc, roll=0.0):
        cd.type = typ; cd.lens = lens or 50; cd.ortho_scale = osc or 1
        cam.location = Vector(loc); cam.rotation_mode = 'QUATERNION'
        q = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        if roll: q = q @ Matrix.Rotation(math.radians(roll), 3, 'Z').to_quaternion()
        cam.rotation_quaternion = q
        sc.render.filepath = os.path.join(OUT, fname); bpy.ops.render.render(write_still=True); print("rendered", fname, file=LOG); LOG.flush()
    def clear():
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
    VIEWS = {"rear": ((1.35, 2.0, 1.25), (0, 0.25, 1.27), 'PERSP', 48, 0), "side": ((3.5, 0.25, 1.27), (0, 0.25, 1.27), 'ORTHO', 0, 0.95),
             "back": ((0, 3.5, 1.27), (0, 0.25, 1.27), 'ORTHO', 0, 1.05)}
    ONLY = os.environ.get("M1R_ONLY", "")
    metrics = {}
    # the ribbed ten
    for key, name, fn in RIBBED:
        if ONLY and key not in ONLY.split(","): continue
        fn(); boom2(1); boom2(-1); fpod(1); fpod(-1)
        metrics[key] = dict(name=name, **measure_body())
        for vn, (loc, tgt, typ, lens, osc) in VIEWS.items(): shoot("m1r_%s_%s.png" % (key, vn), loc, tgt, typ, lens, osc)
        clear()
    # the failure three, on the «Пять рёбер» body
    if not ONLY or "F1" in ONLY:
        r1(); boom2(1); boom2(-1); fpod(1); dead_pod(-1)
        shoot("m1r_F1_back.png", (0, 3.5, 1.27), (0, 0.25, 1.27), 'ORTHO', 0, 1.6, roll=36)   # the world seen rolled: she banks 36 deg
        shoot("m1r_F1_rear.png", (1.35, 2.0, 1.25), (0, 0.25, 1.27), 'PERSP', 48, 0)
        clear()
    if not ONLY or "F2" in ONLY:
        r1(); boom2(1, twin=True); boom2(-1, twin=True); fpod_twin(1); fpod_twin(-1)
        shoot("m1r_F2_rear.png", (1.35, 2.0, 1.25), (0, 0.25, 1.27), 'PERSP', 48, 0)
        shoot("m1r_F2_close.png", (0.95, 0.55, 1.12), (0.60, 0.175, 1.36), 'PERSP', 70, 0)
        clear()
    if not ONLY or "F3" in ONLY:
        r1(); boom2(1); boom2(-1); fpod(1); dead_pod(-1); emergency_nozzle()
        shoot("m1r_F3_rear.png", (1.35, 2.0, 1.25), (0, 0.25, 1.27), 'PERSP', 48, 0)
        shoot("m1r_F3_low.png", (0.8, 1.6, 0.45), (0, 0.22, 1.1), 'PERSP', 45, 0)
        clear()
    json.dump(metrics, open(os.path.join(OUT, "m1r_metrics.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
