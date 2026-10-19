# The astronavigator's helmet anew: ten faceted, ribbed mock-ups on the old helmet's sizes (not its mesh), the visor kept.
#   blender --background astronavigator_suit.blend --python helmet_hex.py -> renders\helm_<1..10>_<front|side|rear>.png
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
LOG = open(os.path.join(OUT, "helm_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
KEEP = {"JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR", "JetPodL", "JetPodR"}
for o in bpy.data.objects:
    if o.name.startswith("Jet") and o.name not in KEEP: o.hide_render = True; o.hide_viewport = True
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


# ==== the helmet anew (the user, 2026-10-18: «круглый 60-х - пережиток прошлого, переднее стекло не уменьшаем, добавим
# геометрию: рёбра жёсткости и скос, не подражание - свой стиль; за основу только размеры, не меш; 10 вариаций - хекс-
# геометрия, не сумасшедшие формы, а не такой округлый, с рёбрами и геометрией»). The old helmet's sizes: the shell
# 0.284 wide, 0.310 deep, 0.340 tall, centre ~(0, -0.038, 1.637); the visor 0.240 x 0.172 (z 1.523..1.695) - kept.
# The neck ring stays where it is. ====
for n in ("Helmet", "HelmHardware", "HelmLiner", "HelmPods", "HelmRib", "HelmRibRed", "Visor", "SunShade", "ShadeArms"):
    bpy.data.objects[n].hide_render = True
for n in ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR", "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR"):
    if n in bpy.data.objects: bpy.data.objects[n].hide_render = True                      # the pack out of the shot
if sc.world is None: sc.world = bpy.data.worlds.new("HL_world")
sc.world.use_nodes = True
_bg = sc.world.node_tree.nodes.get("Background")
if _bg: _bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); _bg.inputs[1].default_value = 1.0   # a light sky for the glass to mirror

def tube(name, pts, r, mat, ns=6):
    """one continuous rib through the points (a ns-sided tube, its ends closed)"""
    bm = bmesh.new(); rings = []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        u = t.cross(Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))).normalized(); w = t.cross(u)
        rings.append([bm.verts.new(p + (u * math.cos(2 * math.pi * k / ns) + w * math.sin(2 * math.pi * k / ns)) * r) for k in range(ns)])
    for a, b in zip(rings, rings[1:]):
        for k in range(ns): quad(bm, a[k], a[(k + 1) % ns], b[(k + 1) % ns], b[k])
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
    return obj(name, bm, mat, smooth=False)
GLASS = bpy.data.materials.new("H_glass"); GLASS.use_nodes = True
_g = next(n for n in GLASS.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
_g.inputs["Base Color"].default_value = (0.80, 0.58, 0.20, 1); _g.inputs["Metallic"].default_value = 1.0; _g.inputs["Roughness"].default_value = 0.12
HC = Vector((0.0, -0.038, 1.637)); HR = (0.142, 0.155, 0.170)
VZ0, VZ1, VW = 1.523, 1.695, 0.240

def sp(w, e): return math.copysign(abs(w) ** e, w)
def shell_pt(th, ph, S):
    """the shell's point: th up from the equator, ph round from her left (+x is 0, the front -y is -90)"""
    rx, ry, rz = S["r"]; e1, e2 = S["e1"], S["e2"]
    p = Vector((rx * sp(math.cos(th), e1) * sp(math.cos(ph), e2), ry * sp(math.cos(th), e1) * sp(math.sin(ph), e2), rz * sp(math.sin(th), e1)))
    if S.get("chin") and p.y < 0 and p.z < 0: p.y *= 1 - S["chin"] * (-p.z / rz)            # the chin swept back
    if S.get("brow") and p.y < 0 and p.z > 0: p.y *= 1 - S["brow"] * (p.z / rz) ** 2          # the brow sloped back (the «скос»)
    if S.get("back") and p.y > 0: p.y *= 1 - S["back"] * max(0.0, -p.z / rz + 0.3)           # the back bevelled under
    return HC + Vector((0, S.get("dy", 0.0), 0)) + p

def shell(name, S, th0=None):
    nu, nv = S["nu"], S["nv"]; th0 = th0 if th0 is not None else math.asin((1.467 - HC.z) / S["r"][2])
    bm = bmesh.new(); rings = []
    for i in range(nv + 1):
        th = th0 + (math.pi / 2 - th0) * i / nv
        if i == nv: rings.append([bm.verts.new(shell_pt(th, 0, S))] * nu); continue
        rings.append([bm.verts.new(shell_pt(th, math.radians(S.get("phase", 0)) + 2 * math.pi * j / nu, S)) for j in range(nu)])
    for a, b in zip(rings, rings[1:]):
        for j in range(nu): quad(bm, a[j], a[(j + 1) % nu], b[(j + 1) % nu], b[j])
    return obj(name, bm, SHELL, smooth=S.get("smooth", False))

def visor(name, S, inset=0.004, frame=True):
    """the glass: a patch of the shell's own surface over the visor's window (0.240 x 0.172), a hair proud, with its frame"""
    def win(u, v):   # u -1..1 across, v 0..1 up
        z = VZ0 + (VZ1 - VZ0) * v; th = math.asin(max(-0.99, min(0.99, (z - HC.z) / S["r"][2])))
        x = u * VW / 2; ph = -math.pi / 2 + math.asin(max(-0.99, min(0.99, x / (S["r"][0] * max(0.3, math.cos(th))))))
        p = shell_pt(th, ph, S); n = (p - HC).normalized(); return p + n * inset
    nu, nv = 12, 6
    bm = bmesh.new(); g = [[bm.verts.new(win(-1 + 2 * i / nu, j / nv)) for i in range(nu + 1)] for j in range(nv + 1)]
    for j in range(nv):
        for i in range(nu): quad(bm, g[j][i], g[j][i + 1], g[j + 1][i + 1], g[j + 1][i])
    obj(name, bm, GLASS, smooth=True)
    if frame:
        edge = [win(-1 + 2 * i / nu, 0) for i in range(nu + 1)] + [win(1, j / nv) for j in range(1, nv + 1)] + [win(1 - 2 * i / nu, 1) for i in range(1, nu + 1)] + [win(-1, 1 - j / nv) for j in range(1, nv)]
        tube(name + "frame", edge + [edge[0]], 0.008, DARK, 8)

def onface(p, S):
    """a point of the true surface brought onto the facets (a faceted shell's flats lie inside its vertices' surface)"""
    k = 1.0 if S.get("smooth") else math.cos(math.pi / S["nu"]) * (0.5 + 0.5 * math.cos(math.pi / (2 * S["nv"])))
    return HC + Vector((0, S.get("dy", 0.0), 0)) + (p - HC - Vector((0, S.get("dy", 0.0), 0))) * k

def rib(name, S, ph, th0=-0.2, th1=math.pi / 2, w=0.012, h=0.008, mat=None, n=14):
    """a rib along a meridian (constant ph), standing h off the shell"""
    pts = [shell_pt(th0 + (th1 - th0) * i / n, ph, S) for i in range(n + 1)]
    pts = [onface(p, S) + (p - HC).normalized() * h / 2 for p in pts]
    tube(name, pts, w / 2, mat or SHELL, 6)

def hoop(name, S, th, ph0=0.0, ph1=2 * math.pi, w=0.010, mat=None, n=40, skip_front=True):
    """a rib round the shell at constant th (a hoop), broken where the visor is"""
    run, runs = [], []
    for k in range(n + 1):
        ph = ph0 + (ph1 - ph0) * k / n
        if skip_front and abs(math.degrees(ph) % 360 - 270) < 52 and VZ0 - 0.01 < shell_pt(th, ph, S).z < VZ1 + 0.01:
            if len(run) > 1: runs.append(run)
            run = []; continue
        p = shell_pt(th, ph, S); run.append(onface(p, S) + (p - HC).normalized() * 0.006)
    if len(run) > 1: runs.append(run)
    for i, r_ in enumerate(runs): tube("%s%d" % (name, i), r_, w / 2, mat or SHELL, 6)

def pod(name, S, sg, z=1.63, r=0.03, nu=6):
    th = math.asin((z - HC.z) / S["r"][2]); p = shell_pt(th, 0 if sg > 0 else math.pi, S)
    poly_lathe(name, p - Vector((sg * 0.004, 0, 0)), (sg, 0, 0), [(0.0, 0.0), (r, 0.0), (r, 0.022), (r * 0.7, 0.03), (0.0, 0.03)], SHELL, nu, 0)
    poly_lathe(name + "l", p + Vector((sg * 0.026, 0, 0)), (sg, 0, 0), [(r * 0.55, 0.0), (r * 0.55, 0.006)], RED, nu, 0)

def ring():
    lathe("neckring", (0, -0.045, 1.452), (0, 0, 1), [(0.088, 0), (0.104, 0), (0.104, 0.02), (0.088, 0.02)], DARK, 24)
    poly_lathe("neckring2", (0, -0.045, 1.472), (0, 0, 1), [(0.095, 0), (0.112, 0), (0.112, 0.012), (0.095, 0.012)], SHELL, 12, 15)

def H(**k):
    S = dict(r=HR, e1=1.0, e2=1.0, nu=12, nv=6); S.update(k); return S

def h1():   # «Октагон»: eight facets round, three tiers to a flat crown; a spine rib
    S = H(nu=8, nv=4, phase=22.5, e1=0.9, e2=0.95); shell("B_h", S); visor("vis", S); rib("spine", S, math.pi / 2, -0.1); rib("spineR", S, math.pi / 2, 0.15, w=0.004, h=0.012, mat=RED)
def h2():   # «Гекса»: six facets round, the crown a hex plate, ear pods hexagonal
    S = H(nu=6, nv=4, phase=0, e1=0.85, e2=0.9); shell("B_h", S); visor("vis", S); pod("podR", S, 1); pod("podL", S, -1)
    hoop("crown", S, math.asin((1.76 - HC.z) / HR[2]), mat=RED, skip_front=False)
def h3():   # «Скос»: the brow sloped back, the chin swept back, the back bevelled under; a rib round the visor's frame and a spine
    S = H(nu=12, nv=6, e1=0.7, e2=0.75, brow=0.18, chin=0.2, back=0.15); shell("B_h", S); visor("vis", S)
    rib("spine", S, math.pi / 2, -0.25); hoop("brow", S, math.asin((VZ1 + 0.02 - HC.z) / HR[2]), ph0=math.radians(200), ph1=math.radians(340), skip_front=False, mat=RED)
def h4():   # «Обручи»: a calmer body with two hoops and the spine - a cage of ribs
    S = H(nu=16, nv=7, e1=0.75, e2=0.8, smooth=True); shell("B_h", S); visor("vis", S)
    hoop("hoop1", S, 0.62); hoop("hoop2", S, 1.05, skip_front=False); rib("spine", S, math.pi / 2, -0.3)
    for ph in (math.radians(20), math.radians(160)): rib("side%d" % int(ph * 100), S, ph, -0.4, 0.6)
def h5():   # «Шеврон»: the facets meet in a prow over the brow; the visor's top edge a chevron under it
    S = H(nu=10, nv=5, phase=-90 + 18, e1=0.8, e2=0.7, brow=0.10); shell("B_h", S); visor("vis", S)
    for ph in (math.radians(-90 - 36), math.radians(-90 + 36)): rib("chev%d" % int(ph * 100), S, ph, math.asin((VZ1 + 0.03 - HC.z) / HR[2]), 1.3, mat=RED)
    rib("spine", S, math.pi / 2, -0.2)
def h6():   # «Ступени»: a stepped crown - the upper tier a smaller faceted cap on the body
    S = H(nu=12, nv=5, phase=15, e1=0.75, e2=0.8); shell("B_h", S, th0=None); visor("vis", S)
    S2 = H(r=(HR[0] * 0.80, HR[1] * 0.82, HR[2] * 1.03), nu=12, nv=3, phase=15, e1=0.6, e2=0.8)
    shell("B_cap", S2, th0=math.asin((1.74 - HC.z) / (HR[2] * 1.03))); hoop("step", S, math.asin((1.735 - HC.z) / HR[2]), mat=RED, skip_front=False)
def h7():   # «Гранёное яйцо»: a low-poly egg, twelve by five facets, no ribs - the geometry is the facets
    S = H(nu=12, nv=5, phase=0, e1=1.0, e2=1.0, r=(HR[0], HR[1], HR[2] * 1.04)); shell("B_h", S); visor("vis", S, frame=False)
    hoop("seam", S, 0.35, w=0.006, mat=DARK)
def h8():   # «Плоский лоб»: a flat forehead plate and flat cheeks, the back still round
    S = H(nu=14, nv=6, e1=0.55, e2=0.6, brow=0.22); shell("B_h", S); visor("vis", S)
    for sg in (1, -1): pod("pod%d" % sg, S, sg, 1.60, 0.034, 8)
    rib("spine", S, math.pi / 2, 0.2)
def h9():   # «Гребень»: a raised crest front to back, the sides flat facets either side of it
    S = H(nu=10, nv=5, phase=0, e1=0.75, e2=0.65); shell("B_h", S); visor("vis", S)
    rib("crest", S, math.pi / 2, -0.35, w=0.03, h=0.02); rib("crestF", S, -math.pi / 2, math.asin((VZ1 + 0.02 - HC.z) / HR[2]), math.pi / 2, w=0.03, h=0.02)
    rib("crestR", S, math.pi / 2, 0.3, w=0.006, h=0.032, mat=RED)
def h10():  # «Козырёк»: a ribbed brow peak over the glass (the sun's shield built in), hex lamp pods
    S = H(nu=12, nv=6, e1=0.7, e2=0.75, brow=0.08); shell("B_h", S); visor("vis", S)
    for k in range(9):
        u0, u1 = -1 + 2 * k / 9, -1 + 2 * (k + 1) / 9
        th = math.asin((VZ1 + 0.012 - HC.z) / HR[2])
        a = shell_pt(th, -math.pi / 2 + u0 * 0.62, S); b = shell_pt(th, -math.pi / 2 + u1 * 0.62, S)
        prism("peak%d" % k, [(0, 0), ((b - a).length, 0), ((b - a).length * 0.9, 0.045), ((b - a).length * 0.1, 0.045)], -0.004, 0.004, SHELL, ch=0.001,
              M=Matrix.Translation(a + (a - HC).normalized() * 0.004) @ Vector((1, 0, 0)).rotation_difference((b - a).normalized()).to_matrix().to_4x4() @ RX(-110))   # tipped out over the glass
    for sg in (1, -1): pod("pod%d" % sg, S, sg, 1.64, 0.028, 6)
    rib("spine", S, math.pi / 2, -0.2)

HELMS = [("1", "Октагон", h1), ("2", "Гекса", h2), ("3", "Скос", h3), ("4", "Обручи", h4), ("5", "Шеврон", h5),
         ("6", "Ступени", h6), ("7", "Гранёное яйцо", h7), ("8", "Плоский лоб", h8), ("9", "Гребень", h9), ("10", "Козырёк", h10)]

try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 560, 560; sc.render.film_transparent = True
    for nm, en, rot in (("HL_key", 3.0, (42, 0, 145)), ("HL_fill", 1.4, (65, 0, -40)), ("HL_front", 1.6, (55, 0, 20)), ("HL_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("HL_cam"); cam = bpy.data.objects.new("HL_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0, -0.03, 1.62))
    VIEWS = {"front": (T + Vector((-0.62, -0.95, 0.14)), 50), "side": (T + Vector((1.15, -0.02, 0.02)), 50), "rear": (T + Vector((0.7, 0.95, 0.25)), 50)}
    ONLY = os.environ.get("HELM_ONLY", "")
    for key, name, fn in HELMS:
        if ONLY and key not in ONLY.split(","): continue
        ring(); fn()
        for vn, (loc, lens) in VIEWS.items():
            cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "helm_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
