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
LOG = open(os.path.join(OUT, "m1f_log.txt"), "w", encoding="utf-8")
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


# ---- ten NEW bodies, each a different idea (the user: «10 ЗАНОВО ранцев, ребристый оставить») ----
def facet(o):
    for p in o.data.polygons: p.use_smooth = False
    return o
def silhouette(name, pts_xz, d, mat=SHELL, ch=0.02, y0=BACK):
    """a body by its silhouette seen from behind (x, z), extruded d back from her back"""
    return prism(name, pts_xz, -d, 0, mat, ch=ch, M=Matrix.Translation(Vector((0, y0, 0))) @ RX(90))
def red_line_xz(name, pts_xz, d, z0, z1, y0=BACK):
    """the red line: the silhouette's slice between z0 and z1, a hair proud of the body"""
    cut = [(x * 1.012, min(max(z, z0), z1)) for x, z in pts_xz]
    prism(name, cut, -d - 0.002, 0.0, RED, ch=0.0005, M=Matrix.Translation(Vector((0, y0 + 0.001, 0))) @ RX(90))

def n1():   # «Капля»: wide at the shoulders, narrowing to the waist - the elbows pass by its flanks
    s = [(-0.21, 1.52), (0.21, 1.52), (0.20, 1.30), (0.15, 1.12), (0.07, 1.02), (-0.07, 1.02), (-0.15, 1.12), (-0.20, 1.30)]
    silhouette("B_drop", s, 0.20, ch=0.03)
    red_line_xz("band", s, 0.20, 1.395, 1.41); ports4(0.31, 0.17, 1.49, 1.30); coupling(0.25)
def n2():   # «Капюшон»: the upper part flares over the shoulder blades and carries the hinges, the lower part slim
    silhouette("B_body", [(-0.17, 1.40), (0.17, 1.40), (0.15, 1.04), (-0.15, 1.04)], 0.19, ch=0.02)
    silhouette("B_hood", [(-0.25, 1.53), (0.25, 1.53), (0.26, 1.44), (0.18, 1.36), (-0.18, 1.36), (-0.26, 1.44)], 0.15, ch=0.02)
    prism("hoodred", rect(0.50, 0.012), 0, 0.152, RED, ch=0.001, M=Matrix.Translation(Vector((0, BACK, 1.45))) @ RX(-90))
    ports4(0.30, 0.22, 1.49, 1.06); coupling(0.25)
def n3():   # «Три ствола»: three tanks inside a faceted fairing that shows them as three bulges, grooves between
    out = [(-0.205, BACK)]
    for cx in (-0.13, 0.0, 0.13):
        for a in range(180, -1, -30):
            out.append((cx + 0.072 * math.cos(math.radians(a)), 0.235 + 0.072 * math.sin(math.radians(a))))
    out.append((0.205, BACK))
    prism("B_three", out, 1.04, 1.50, SHELL, ch=0.008)
    for cx in (-0.13, 0.0, 0.13): prism("cap%d" % int(cx * 100), [(cx + 0.06 * math.cos(math.radians(a)), 0.235 + 0.06 * math.sin(math.radians(a))) for a in range(0, 360, 45)], 1.50, 1.52, SHELL, ch=0.004)
    prism("band", [(x, BACK + (y - BACK) * 1.01) for x, y in out], 1.40, 1.412, RED, ch=0.0005)
    ports4(0.30, 0.13, 1.49, 1.05); coupling(0.22)
def n4():   # «Кокон»: one faceted low-poly egg - no corners to catch, every panel a flat facet
    o = sbox("B_cocoon", (0, 0.215, 1.27), (0.205, 0.11, 0.25), SHELL, 0.85, 0.85, nu=10, nv=6); facet(o)
    belt("band", (0, 0.215, 1.38), (0.19, 0.10), RED, 0.012, 0.9); ports4(0.322, 0.10, 1.40, 1.12); coupling(0.25)
def n5():   # «Портфель»: a slim case to grab - the carry handle on top, two quick-release latches, grip rails on the sides
    box("B_case", 0.40, 0.46, 0.15)
    lathe("handle", (-0.09, BACK + 0.11, 1.535), (1, 0, 0), [(0, 0), (0.012, 0), (0.012, 0.18), (0, 0.18)], FRAME, 12)
    for x in (-0.09, 0.09): bar("hpost%d" % int(x * 100), (x, BACK + 0.11, 1.50), (x, BACK + 0.11, 1.535), 0.01, FRAME)
    for sg in (1, -1):
        prism("latch%d" % sg, rect(0.04, 0.08, 0.006), 0, 0.02, DARK, ch=0.002, M=Matrix.Translation(Vector((sg * 0.20, BACK + 0.07, 1.20))) @ RY(90 * sg))
        bar("grip%d" % sg, (sg * 0.215, BACK + 0.03, 1.08), (sg * 0.215, BACK + 0.03, 1.46), 0.008)
    band(1.43, 0.40, 0.15); ports4(0.26); coupling(0.22)
def n6():   # «Крестовина»: four sealed tank modules in the quadrants of an X frame - lose one, three keep feeding
    prism("B_hubplate", rect(0.40, 0.48, 0.05), 0, 0.05, SHELL, ch=0.008, M=Matrix.Translation(Vector((0, BACK, 1.27))) @ RX(-90))
    for sx in (1, -1):
        for sz in (1, -1):
            o = sbox("B_mod%d%d" % (sx, sz), (sx * 0.10, 0.225, 1.27 + sz * 0.12), (0.085, 0.07, 0.10), SHELL, 0.3, 0.3); facet(o)
    for a in (35, -35): prism("xbeam%d" % a, rect(0.03, 0.60), 0, 0.17, DARK, ch=0.003, M=Matrix.Translation(Vector((0, BACK + 0.05, 1.27))) @ RX(-90) @ RZ(a))
    lathe("hub", (0, BACK + 0.05, 1.27), (0, 1, 0), [(0, 0), (0.05, 0), (0.05, 0.17), (0, 0.17)], RED, 16)
    ports4(0.30, 0.19, 1.50, 1.04); coupling(0.20)
def n7():   # «Асимметрия»: a deep tank on her left, a shallow electronics block on her right with its service panel to hand
    box("B_tank", 0.20, 0.48, 0.23, x=-0.10)
    box("B_elec", 0.19, 0.42, 0.12, x=0.105, cz=1.30)
    prism("screen", rect(0.12, 0.10, 0.01), 0, 0.004, DARK, ch=0.001, M=Matrix.Translation(Vector((0.105, BACK + 0.12, 1.36))) @ RX(-90))
    for i in range(3): prism("key%d" % i, rect(0.025, 0.018), 0, 0.006, RED if i == 0 else SEAM, ch=0.001, M=Matrix.Translation(Vector((0.07 + i * 0.035, BACK + 0.12, 1.26))) @ RX(-90))
    prism("band", rect(0.204, 0.012), 0.02, 0.232, RED, ch=0.001, M=Matrix.Translation(Vector((-0.10, BACK, 1.43))) @ RX(-90))
    ports4(0.34, 0.17, 1.49, 1.05); coupling(0.25)
def n8():   # «Поясной»: the tanks ride on her hips (a hip belt carries the weight, as a good hiking pack), the upper plate thin
    box("B_plate", 0.36, 0.30, 0.08, cz=1.37)
    for sg in (1, -1):
        o = sbox("B_kidney%d" % sg, (sg * 0.17, 0.16, 1.07), (0.10, 0.085, 0.085), SHELL, 0.3, 0.35, rot=RZ(-sg * 25)); facet(o)
    o = sbox("B_lumbar", (0, 0.20, 1.07), (0.10, 0.08, 0.075), SHELL, 0.3, 0.35); facet(o)
    for sg in (1, -1): bar("belt%d" % sg, (sg * 0.10, 0.21, 1.03), (sg * 0.27, 0.06, 1.03), 0.014, DARK)
    belt("band", (0, 0.20, 1.10), (0.10, 0.08), RED, 0.012, 0.35)
    for sg in (1, -1): bar("strut%d" % sg, (sg * 0.12, 0.17, 1.22), (sg * 0.12, 0.17, 1.14), 0.012, FRAME)
    ports4(0.19, 0.17, 1.49, 1.30); coupling(0.20)
def n9():   # «Гекса»: a hexagonal silhouette with a deeper hexagonal core - the family's cell, the fuel at the centre of mass
    H = lambda r, cz: [(r * math.cos(math.radians(90 + 60 * k)), cz + r * math.sin(math.radians(90 + 60 * k))) for k in range(6)]
    silhouette("B_hex", H(0.26, 1.27), 0.13, ch=0.02)
    silhouette("B_core", H(0.15, 1.27), 0.24, ch=0.02)
    red_line_xz("band", H(0.152, 1.27), 0.24, 1.33, 1.345)
    ports4(0.24, 0.18, 1.40, 1.14); coupling(0.22)
def n10():  # «Ромб»: a diamond silhouette in two steps - the facets lead the eye and the airflow, the fuel at the middle
    D = lambda w, h, cz: [(0, cz + h), (w, cz), (0, cz - h), (-w, cz)]
    silhouette("B_d1", [(-0.21, 1.43), (0, 1.53), (0.21, 1.43), (0.21, 1.11), (0, 1.02), (-0.21, 1.11)], 0.12, ch=0.02)
    silhouette("B_d2", D(0.15, 0.20, 1.27), 0.22, ch=0.02)
    red_line_xz("band", D(0.15, 0.20, 1.27), 0.22, 1.30, 1.315)
    ports4(0.23, 0.18, 1.42, 1.12); coupling(0.22)

NEW = [("N1", "Капля", n1), ("N2", "Капюшон", n2), ("N3", "Три ствола", n3), ("N4", "Кокон", n4), ("N5", "Портфель", n5),
       ("N6", "Крестовина", n6), ("N7", "Асимметрия", n7), ("N8", "Поясной", n8), ("N9", "Гекса", n9), ("N10", "Ромб", n10),
       ("N0", "Ребристый (оставлен)", r1)]

# ---- the finalists (Капля, Гекса) with the boom as the numbers have it (tools/jetpack_m1_capsule.py): one box beam
# 40 x 60 mm, wall 1 mm, the feed lines (two of 4-8 mm, one per chamber) inside - nothing doubled on the outside; and the
# twin-chamber capsule with the air «форсаж» ----
def boom3(sg):
    x0, x1 = sg * 0.215, sg * 0.552; y, z = 0.18, 1.492; L = abs(x1 - x0)
    oct_ = [(-0.020, -0.022), (-0.012, -0.030), (0.012, -0.030), (0.020, -0.022), (0.020, 0.022), (0.012, 0.030), (-0.012, 0.030), (-0.020, 0.022)]   # (y, z): 40 x 60, chamfered
    prism("boom%d" % sg, oct_, 0, L, SHELL, ch=0.002, M=Matrix.Translation(Vector((x0, y, z))) @ RY(90 * sg))
    prism("boomseam%d" % sg, rect(0.002, 0.044), 0.03, L - 0.03, SEAM, ch=0.0005, M=Matrix.Translation(Vector((x0, y + 0.0205, z))) @ RY(90 * sg))   # the access panel's seam
    for k in range(3):   # three stiffener bands
        xk = x0 + sg * (0.07 + k * 0.10)
        prism("boomband%d_%d" % (sg, k), [(a * 1.06, c * 1.06) for a, c in oct_], 0, 0.006, SEAM, ch=0.0005, M=Matrix.Translation(Vector((xk, y, z))) @ RY(90 * sg))
    poly_lathe("bcollar%d" % sg, (x1, y, z), (sg, 0, 0), [(0, -0.01), (0.045, -0.01), (0.045, 0.04), (0.0, 0.04)], SHELL, 8, 22.5)
    poly_lathe("bcollarr%d" % sg, (x1 + sg * 0.012, y, z), (sg, 0, 0), [(0.047, 0), (0.047, 0.01)], RED, 8, 22.5)

FINAL = [("K", "Капля", n1), ("G", "Гекса", n9)]
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
    for key, name, fn in FINAL:
        if ONLY and key not in ONLY.split(","): continue
        fn(); boom3(1); boom3(-1); fpod_twin(1); fpod_twin(-1)
        metrics[key] = dict(name=name, **measure_body())
        for vn, (loc, tgt, typ, lens, osc) in VIEWS.items(): shoot("m1f_%s_%s.png" % (key, vn), loc, tgt, typ, lens, osc)
        if key == "K": shoot("m1f_pod_close.png", (0.95, 0.55, 1.12), (0.60, 0.175, 1.36), 'PERSP', 70, 0); shoot("m1f_pod_under.png", (0.60, 0.05, 1.05), (0.60, 0.175, 1.36), 'PERSP', 60, 0)
        clear()
    json.dump(metrics, open(os.path.join(OUT, "m1f_metrics.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
