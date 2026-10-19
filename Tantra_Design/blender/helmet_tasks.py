# The astronavigator's helmet anew: a new helmet from its tasks: three groups of ten.
#   blender --background astronavigator_suit.blend --python helmet_tasks.py -> renders\h4_<A|B|C><1..10>_<str|turn|down>.png
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
LOG = open(os.path.join(OUT, "h4_log.txt"), "w", encoding="utf-8")
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


# ==== a new helmet from its tasks (the user, 2026-10-18: «хватит мучить мой шлем, спроектируем новый по задачам - и то и то,
# три группы по 10, с поворотом головы в кадрах»). The pressure is held by an inner bladder, so the outer shell's form is
# free. Three architectures:
#   A - fixed, roomy: the head turns inside; a rounded-box (not a ball) or hexagonal plan; a wide visor and a chin window;
#   B - turning with the head on a neck bearing: close round the head, compact;
#   C - a clear pressure bubble and a load-carrying frame (a crown, arches) round it: lamps, HUD, the shade in the frame.
# Each is shot straight, with the head turned 50 deg, and looking down 30 deg (the view to her feet for the jet pack). ====
import mathutils
for n in ("Helmet", "HelmHardware", "HelmLiner", "HelmPods", "HelmRib", "HelmRibRed", "Visor", "SunShade", "ShadeArms", "NeckSeal",
          "JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR", "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR"):
    if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
if sc.world is None: sc.world = bpy.data.worlds.new("HL_world")
sc.world.use_nodes = True
_bg = sc.world.node_tree.nodes.get("Background")
if _bg: _bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); _bg.inputs[1].default_value = 1.0
def mk(name, col, metal, rough, alpha=None):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
    if alpha is not None:
        b.inputs["Alpha"].default_value = alpha
        try: m.surface_render_method = 'BLENDED'
        except Exception: m.blend_method = 'BLEND'
    return m
GLASS = mk("H4_glass", (0.55, 0.42, 0.25), 0.6, 0.05, 0.30)
BUBBLE = mk("H4_bubble", (0.75, 0.80, 0.85), 0.2, 0.03, 0.16)
GREYM = mk("H4_grey", (0.28, 0.29, 0.31), 0.35, 0.45)
SILVER = mk("H4_silver", (0.78, 0.79, 0.81), 1.0, 0.25)
COLLAR = mk("H4_collar", (0.10, 0.10, 0.11), 0.0, 0.8)
LAMP = mk("H4_lamp", (1.0, 0.95, 0.8), 0.0, 0.2)
_lb = next(n for n in LAMP.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
try: _lb.inputs["Emission Color"].default_value = (1.0, 0.95, 0.8, 1); _lb.inputs["Emission Strength"].default_value = 4.0
except Exception: pass
ARM = bpy.data.objects["Astronavigator"]

def sp(w, e): return math.copysign(abs(w) ** e, w)
class Shape:
    """a closed shell: centre c, radii r, squareness e1 (up) e2 (round); plan 'hex'/'oct' bends the round into a polygon;
    brow - the forehead sloped back, chin - the chin swept back, back - the back of the head cut under (clears the pack)"""
    def __init__(s, c, r, e1=1.0, e2=1.0, plan=None, brow=0.0, chin=0.0, back=0.0, rot=0.0):
        s.c, s.r, s.e1, s.e2, s.plan, s.brow, s.chin, s.back, s.rot = Vector(c), r, e1, e2, plan, brow, chin, back, rot
    def pt(s, th, ph):
        rx, ry, rz = s.r; k = 1.0
        if s.plan:
            n = 6 if s.plan == "hex" else 8
            seg = 2 * math.pi / n; a = (ph - math.radians(s.rot)) % seg - seg / 2
            k = math.cos(seg / 2) / math.cos(a)
        if s.plan: hx, hy = rx * k * math.cos(ph), ry * k * math.sin(ph)
        else: hx, hy = rx * sp(math.cos(ph), s.e2), ry * sp(math.sin(ph), s.e2)
        c1 = sp(math.cos(th), s.e1)
        p = Vector((hx * c1, hy * c1, rz * sp(math.sin(th), s.e1)))
        if s.brow and p.y < 0 and p.z > 0: p.y *= 1 - s.brow * (p.z / rz) ** 2
        if s.chin and p.y < 0 and p.z < 0: p.y *= 1 - s.chin * (-p.z / rz)
        if s.back and p.y > 0: p.y *= 1 - s.back * max(0.0, -p.z / rz + 0.25)
        return s.c + p
    def n(s, th, ph):
        d = 1e-3; a = s.pt(th + d, ph) - s.pt(th - d, ph); b = s.pt(th, ph + d) - s.pt(th, ph - d)
        v = b.cross(a)
        if v.length < 1e-9: return (s.pt(th, ph) - s.c).normalized()
        v.normalize()
        return v if v.dot(s.pt(th, ph) - s.c) > 0 else -v
    def th_of(s, z): return math.asin(max(-0.99, min(0.99, sp((z - s.c.z) / s.r[2], 1 / s.e1))))

FRONT = -math.pi / 2
def ang(ph): return (ph - FRONT + math.pi) % (2 * math.pi) - math.pi          # the angle from her front

class Win:
    """a window: |ang| < half, between z0(u) and z1(u) (u = ang / half)"""
    def __init__(s, half, z0, z1, ph0=0.0): s.half, s.z0, s.z1, s.ph0 = half, z0, z1, ph0
    def inside(s, S, th, ph):
        a = ang(ph) - s.ph0; u = a / s.half
        if abs(u) >= 1: return False
        z = S.pt(th, ph).z; return s.z0(u) < z < s.z1(u)

def shell(name, S, wins, mat=None, th_lo=None, nu=72, nv=30):
    th0 = th_lo if th_lo is not None else S.th_of(1.47)
    bm = bmesh.new(); rings = []
    for i in range(nv + 1):
        th = th0 + (math.pi / 2 - th0) * i / nv
        if i == nv: rings.append([bm.verts.new(S.pt(th, 0))] * nu); continue
        rings.append([bm.verts.new(S.pt(th, 2 * math.pi * j / nu)) for j in range(nu)])
    for i, (a, b) in enumerate(zip(rings, rings[1:])):
        th = th0 + (math.pi / 2 - th0) * (i + 0.5) / nv
        for j in range(nu):
            ph = 2 * math.pi * (j + 0.5) / nu
            if any(w.inside(S, th, ph) for w in wins): continue
            quad(bm, a[j], a[(j + 1) % nu], b[(j + 1) % nu], b[j])
    return obj(name, bm, mat or SHELL, smooth=True)

def pane(name, S, w, off=0.005, mat=None, n=36, m=12):
    """the glass over a window: the shell's own surface, off proud"""
    bm = bmesh.new(); rows = []
    for j in range(m + 1):
        row = []
        for i in range(n + 1):
            u = -1 + 2 * i / n; ph = FRONT + w.ph0 + u * w.half
            z = w.z0(u) + (w.z1(u) - w.z0(u)) * j / m; th = S.th_of(z)
            row.append(bm.verts.new(S.pt(th, ph) + S.n(th, ph) * off))
        rows.append(row)
    for a, b in zip(rows, rows[1:]):
        for i in range(n): quad(bm, a[i], a[i + 1], b[i + 1], b[i])
    return obj(name, bm, mat or GLASS, smooth=True)

def tube(name, pts, r, mat, ns=8):
    bm = bmesh.new(); rings = []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        u = t.cross(Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))).normalized(); w = t.cross(u)
        rings.append([bm.verts.new(p + (u * math.cos(2 * math.pi * k / ns) + w * math.sin(2 * math.pi * k / ns)) * r) for k in range(ns)])
    for a, b in zip(rings, rings[1:]):
        for k in range(ns): quad(bm, a[k], a[(k + 1) % ns], b[(k + 1) % ns], b[k])
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
    return obj(name, bm, mat, smooth=True)

def frame_line(name, S, path, r, h, mat=None):
    """a rib/frame member along a (th, ph) path, standing h off the surface"""
    return tube(name, [S.pt(th, ph) + S.n(th, ph) * h for th, ph in path], r, mat or SHELL)

def win_rim(name, S, w, r=0.006, mat=None, n=40):
    pts = []
    for i in range(n + 1): u = -1 + 2 * i / n; pts.append((S.th_of(w.z0(u)), FRONT + w.ph0 + u * w.half))
    for i in range(1, 7): pts.append((S.th_of(w.z0(1) + (w.z1(1) - w.z0(1)) * i / 6), FRONT + w.ph0 + w.half))
    for i in range(1, n + 1): u = 1 - 2 * i / n; pts.append((S.th_of(w.z1(u)), FRONT + w.ph0 + u * w.half))
    for i in range(1, 7): pts.append((S.th_of(w.z1(-1) - (w.z1(-1) - w.z0(-1)) * i / 6), FRONT + w.ph0 - w.half))
    return frame_line(name, S, pts, r, 0.004, mat or GREYM)

def block(name, S, th, ph, size=0.05, depth=0.016, nu=6, lamp=False, mat=None):
    p, n_ = S.pt(th, ph), S.n(th, ph)
    poly_lathe(name, p - n_ * 0.003, n_, [(0.0, 0.0), (size, 0.0), (size, depth * 0.7), (size * 0.85, depth), (0.0, depth)], mat or SHELL, nu, 0)
    poly_lathe(name + "r", p + n_ * depth * 0.7, n_, [(size * 1.01, 0.0), (size * 1.01, 0.004), (size * 0.9, 0.004)], RED, nu, 0)
    poly_lathe(name + "c", p + n_ * depth, n_, [(0.0, 0.0), (size * 0.5, 0.0), (size * 0.5, 0.004), (0.0, 0.004)], LAMP if lamp else DARK, nu, 0)

def mer(ph, th0, th1, n=24): return [(th0 + (th1 - th0) * i / n, ph) for i in range(n + 1)]
def par(th, ph0, ph1, n=40): return [(th, ph0 + (ph1 - ph0) * i / n) for i in range(n + 1)]

def neck_fixed():   # the suit's neck ring for A and C: the helmet locks on it
    lathe("nk_collar", (0, -0.04, 1.36), (0, 0, 1), [(0.075, 0.0), (0.108, 0.01), (0.118, 0.045), (0.112, 0.075), (0.098, 0.09)], COLLAR, 40)
    lathe("nk1", (0, -0.035, 1.430), (0, 0, 1), [(0.100, 0), (0.124, 0), (0.124, 0.014), (0.100, 0.014)], SILVER, 48)
    poly_lathe("nk2", (0, -0.035, 1.444), (0, 0, 1), [(0.104, 0), (0.130, 0), (0.130, 0.012), (0.104, 0.012)], SHELL, 12, 15)
def neck_bearing():   # B: a wide bearing at the neck; the helmet turns on it with the head
    lathe("nb_collar", (0, -0.04, 1.36), (0, 0, 1), [(0.075, 0.0), (0.108, 0.01), (0.118, 0.045), (0.112, 0.075), (0.098, 0.09)], COLLAR, 40)
    lathe("nb1", (0, -0.035, 1.428), (0, 0, 1), [(0.098, 0), (0.132, 0), (0.132, 0.016), (0.098, 0.016)], SILVER, 48)
    lathe("nb2", (0, -0.035, 1.444), (0, 0, 1), [(0.100, 0), (0.128, 0), (0.128, 0.008), (0.100, 0.008)], RED, 48)

# ---- the windows ----
def W_wrap(half=70, top=1.715, bot=1.50, rise=0.07): return Win(math.radians(half), lambda u: bot + rise * u * u, lambda u: top)
def W_chin(half=26, z0=1.47, z1=1.505): return Win(math.radians(half), lambda u: z0 + 0.012 * u * u, lambda u: z1 - 0.004 * u * u)
def W_band(half=80, z0=1.585, z1=1.695): return Win(math.radians(half), lambda u: z0, lambda u: z1)

# ---- group A: fixed, roomy (the head turns inside) ----
CA = (0.0, -0.030, 1.640)
def a_common(S, wins, rim=True):
    shell("A_shell", S, wins)
    for i, w in enumerate(wins): pane("A_pane%d" % i, S, w);  rim and win_rim("A_rim%d" % i, S, w)
def A1():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.65, 0.6); w = [W_wrap(), W_chin()]; a_common(S, w)
    frame_line("crest", S, mer(math.pi / 2, -0.2, math.pi / 2 - 0.02) + mer(-math.pi / 2, math.pi / 2 - 0.02, S.th_of(1.75)), 0.008, 0.006, GREYM)
def A2():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.7, 1.0, plan="hex", rot=30); w = [W_wrap(64), W_chin()]; a_common(S, w)
    for sg in (1, -1): block("t%d" % sg, S, S.th_of(1.63), 0 if sg > 0 else math.pi)
def A3():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.7, 1.0, plan="oct", rot=22.5)
    w = [W_band(70, 1.60, 1.70), Win(math.radians(14), lambda u: 1.47, lambda u: 1.60)]; a_common(S, w)   # the T: a band and a slot down to the chin
def A4():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.65, 0.65, brow=0.12)
    w = [W_wrap(45, 1.705, 1.50, 0.03), Win(math.radians(18), lambda u: 1.56, lambda u: 1.69, ph0=math.radians(-62)), Win(math.radians(18), lambda u: 1.56, lambda u: 1.69, ph0=math.radians(62))]
    a_common(S, w); frame_line("brow", S, par(S.th_of(1.73), math.radians(-160), math.radians(-20)), 0.012, 0.008, GREYM)
def A5():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.65, 0.6, back=0.35); w = [W_wrap(), W_chin()]; a_common(S, w)
    for ph in (math.radians(60), math.radians(90), math.radians(120)): frame_line("rib%d" % int(ph * 100), S, mer(ph, -0.3, 1.2), 0.006, 0.005)
def A6():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.6, 0.6); w = [W_wrap(68, 1.715, 1.585, 0.02), Win(math.radians(50), lambda u: 1.47 + 0.03 * u * u, lambda u: 1.565)]; a_common(S, w)
    frame_line("mull", S, par(S.th_of(1.575), FRONT - math.radians(52), FRONT + math.radians(52)), 0.009, 0.006, GREYM)
def A7():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.7, 1.0, plan="hex", rot=0); w = [W_wrap(62), W_chin()]; a_common(S, w)
    frame_line("spine", S, mer(math.pi / 2, -0.3, math.pi / 2 - 0.02), 0.009, 0.006, RED)
    for sg in (1, -1): block("t%d" % sg, S, S.th_of(1.62), (0 if sg > 0 else math.pi) + sg * math.radians(25), lamp=True)
def A8():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.6, 0.6, brow=0.2, chin=0.15); w = [W_wrap(66), W_chin()]; a_common(S, w)
    for z in (1.74, 1.78): frame_line("h%d" % int(z * 100), S, par(S.th_of(z), math.radians(-150), math.radians(150) + 2 * math.pi - 2 * math.pi), 0.005, 0.004)
def A9():
    S = Shape(CA, (0.150, 0.165, 0.185), 0.65, 0.65); w = [W_wrap(), W_chin()]; a_common(S, w)
    S2 = Shape((CA[0], CA[1] + 0.01, CA[2] + 0.01), (0.12, 0.13, 0.185), 0.55, 0.65)
    shell("A_cap", S2, [], th_lo=S2.th_of(1.75)); frame_line("capr", S, par(S.th_of(1.75), 0, 2 * math.pi, 60), 0.005, 0.004, RED)
def A10():
    S = Shape(CA, (0.150, 0.165, 0.180), 0.55, 1.0, plan="hex", rot=30); w = [W_wrap(60, 1.70), W_chin()]; a_common(S, w)
    for ph in [math.radians(30 + 60 * k) for k in range(6)]: frame_line("e%d" % int(ph * 100), S, mer(ph, S.th_of(1.72), math.pi / 2 - 0.03, 10), 0.006, 0.004, GREYM)

# ---- group B: close, turning with the head on a neck bearing ----
CB = (0.0, -0.040, 1.660)
def b_common(S, wins, rim=True):
    shell("B_shell", S, wins, th_lo=S.th_of(1.448))
    for i, w in enumerate(wins): pane("B_pane%d" % i, S, w);  rim and win_rim("B_rim%d" % i, S, w)
def WB(half=72, top=1.725, bot=1.545, rise=0.05): return Win(math.radians(half), lambda u: bot + rise * u * u, lambda u: top)
def B1():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.7, 0.65); b_common(S, [WB()])
def B2():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.75, 1.0, plan="hex", rot=30); b_common(S, [WB(64)])
    for sg in (1, -1): block("t%d" % sg, S, S.th_of(1.65), 0 if sg > 0 else math.pi, 0.04)
def B3():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.7, 0.65); b_common(S, [WB(70, 1.71)])
    frame_line("brow", S, par(S.th_of(1.74), math.radians(-150), math.radians(-30)), 0.012, 0.01, GREYM)
    for sg in (1, -1): block("l%d" % sg, S, S.th_of(1.745), FRONT + sg * math.radians(48), 0.016, 0.01, 8, lamp=True)
def B4():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.7, 0.7); b_common(S, [WB(100, 1.715, 1.56, 0.04)])
    frame_line("spine", S, mer(math.pi / 2, -0.4, math.pi / 2 - 0.02), 0.008, 0.005, RED)
def B5():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.7, 0.65, chin=0.1); b_common(S, [WB(70, 1.72, 1.585, 0.03), Win(math.radians(34), lambda u: 1.47, lambda u: 1.56)])
def B6():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.65, 0.7, brow=0.1); b_common(S, [WB()])
    frame_line("crest", S, mer(math.pi / 2, -0.3, math.pi / 2 - 0.02) + mer(-math.pi / 2, math.pi / 2 - 0.02, S.th_of(1.745)), 0.012, 0.012, SHELL)
    frame_line("crestr", S, mer(math.pi / 2, -0.2, math.pi / 2 - 0.02), 0.004, 0.024, RED)
def B7():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.7, 0.65, back=0.25); b_common(S, [WB()])
    p = S.pt(S.th_of(1.62), math.pi / 2); prism("vent", rect(0.08, 0.06, 0.01), 0, 0.02, GREYM, ch=0.004, M=Matrix.Translation(p) @ RX(-90))
def B8():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.75, 1.0, plan="oct", rot=22.5); b_common(S, [WB(66)])
    for sg in (1, -1): block("t%d" % sg, S, S.th_of(1.64), (0 if sg > 0 else math.pi) + sg * math.radians(15), 0.045, 0.02, 6, lamp=True)
def B9():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.7, 0.65); b_common(S, [WB()])
    S2 = Shape(CB, (0.125, 0.155, 0.161), 0.7, 0.65); shell("B_upper", S2, [WB()], mat=GREYM, th_lo=S2.th_of(1.73))
def B10():
    S = Shape(CB, (0.122, 0.152, 0.158), 0.8, 0.75); b_common(S, [WB(74, 1.73, 1.54, 0.05)], rim=False)

# ---- group C: a clear bubble and a frame round it ----
CC = (0.0, -0.032, 1.645)
def c_common(Sb):
    shell("C_bubble", Sb, [], mat=BUBBLE, th_lo=Sb.th_of(1.455))
def SC(): return Shape(CC, (0.150, 0.162, 0.180), 1.0, 1.0)
def crown(S, z, n=60, r=0.009, mat=None, h=0.012):
    return frame_line("crown%d" % int(z * 100), S, par(S.th_of(z), 0, 2 * math.pi, n), r, h, mat)
def C1():
    S = SC(); c_common(S); crown(S, 1.47)
    for ph in [math.radians(a) for a in (0, 90, 180)]: frame_line("m%d" % int(ph * 100), S, mer(ph, S.th_of(1.47), math.pi / 2 - 0.01), 0.008, 0.012)
    for sg in (1, -1): block("t%d" % sg, S, S.th_of(1.60), 0 if sg > 0 else math.pi, 0.03, 0.02, 6, lamp=True)
def C2():
    S = SC(); c_common(S); crown(S, 1.47); crown(S, 1.77, 6, 0.009)
    for ph in [math.radians(30 + 60 * k) for k in range(6)]:
        if abs(ang(ph)) > math.radians(40): frame_line("m%d" % int(ph * 100), S, mer(ph, S.th_of(1.47), S.th_of(1.77), 12), 0.007, 0.012)
def C3():
    S = SC(); c_common(S); crown(S, 1.47)
    frame_line("arch", S, mer(0, S.th_of(1.47), math.pi / 2 - 0.01) + mer(math.pi, math.pi / 2 - 0.01, S.th_of(1.47)), 0.012, 0.014, SHELL)
    for sg in (1, -1): block("p%d" % sg, S, S.th_of(1.61), 0 if sg > 0 else math.pi, 0.045, 0.024, 6)
def C4():
    S = SC(); c_common(S); crown(S, 1.47)
    for a0 in (40, 140):
        frame_line("x%d" % a0, S, mer(math.radians(a0), S.th_of(1.47), math.pi / 2 - 0.01) + mer(math.radians(a0 + 180), math.pi / 2 - 0.01, S.th_of(1.47)), 0.009, 0.012)
def C5():
    S = SC(); c_common(S); crown(S, 1.47, r=0.014)
    frame_line("brow", S, par(S.th_of(1.73), math.radians(-155), math.radians(-25)), 0.014, 0.014, GREYM)
    for sg in (1, -1): frame_line("post%d" % sg, S, mer(FRONT + sg * math.radians(65), S.th_of(1.47), S.th_of(1.73), 10), 0.009, 0.013)
def C6():
    S = SC(); c_common(S); crown(S, 1.47)
    frame_line("spine", S, mer(math.pi / 2, S.th_of(1.47), math.pi / 2 - 0.01) + mer(-math.pi / 2, math.pi / 2 - 0.01, S.th_of(1.76)), 0.016, 0.016, SHELL)
    for k in range(3): block("l%d" % k, S, S.th_of(1.55 + 0.07 * k), math.pi / 2, 0.016, 0.03, 8, lamp=True)
def C7():
    S = SC(); c_common(S); crown(S, 1.47); crown(S, 1.70, 6)
    for ph in [math.radians(30 + 60 * k) for k in range(6)]: frame_line("c%d" % int(ph * 100), S, mer(ph, S.th_of(1.70), math.pi / 2 - 0.01, 8), 0.007, 0.012)
def C8():
    S = SC(); c_common(S); crown(S, 1.47)
    frame_line("arch", S, mer(math.radians(20), S.th_of(1.47), math.pi / 2 - 0.01) + mer(math.radians(200), math.pi / 2 - 0.01, S.th_of(1.62)), 0.012, 0.014)
    block("hud", S, S.th_of(1.63), math.radians(20), 0.04, 0.03, 6, lamp=True)
def C9():
    S = SC(); c_common(S); crown(S, 1.47); crown(S, 1.70, 60, 0.012, GREYM)
    S2 = Shape(CC, (0.158, 0.170, 0.188), 1.0, 1.0)
    pane("shade", S2, Win(math.radians(60), lambda u: 1.70, lambda u: 1.79), mat=GREYM)
def C10():
    S = SC(); c_common(S); crown(S, 1.47)
    for ph in [math.radians(a) for a in (90, 210, 330)]:
        frame_line("t%d" % int(ph * 100), S, mer(ph, S.th_of(1.47), math.pi / 2 - 0.01), 0.010, 0.012)

GROUPS = {
 "A": ([("1", "Скруглённый короб", A1), ("2", "Шестигранный план", A2), ("3", "Т-стекло", A3), ("4", "Бровь и боковые окна", A4), ("5", "Подрезанный затылок", A5),
        ("6", "Два стекла", A6), ("7", "Шестигранник с фонарями", A7), ("8", "Скос и пояса", A8), ("9", "Ступенчатая макушка", A9), ("10", "Шестигранный купол", A10)], neck_fixed, False),
 "B": ([("1", "Плотный", B1), ("2", "Шестигранный", B2), ("3", "Бровь-фонари", B3), ("4", "Стекло до ушей", B4), ("5", "Подбородочное окно", B5),
        ("6", "Гребень", B6), ("7", "Затылочный блок", B7), ("8", "Восьмигранник с модулями", B8), ("9", "Двухслойный", B9), ("10", "Минимум", B10)], neck_bearing, True),
 "C": ([("1", "Корона и дуги", C1), ("2", "Шестигранная корона", C2), ("3", "Одна арка", C3), ("4", "Крест-накрест", C4), ("5", "Бровь на стойках", C5),
        ("6", "Хребет с огнями", C6), ("7", "Шестигранная шапка", C7), ("8", "Асимметричная арка", C8), ("9", "Ореол и шторка", C9), ("10", "Три дуги", C10)], neck_fixed, False),
}

def pose_head(yaw=0.0, pitch=0.0):
    ARM.data.pose_position = 'POSE'
    for bn, k in (("Neck", 0.25), ("Neck1", 0.35), ("Head", 0.40)):
        pb = ARM.pose.bones[bn]; pb.rotation_mode = 'XYZ'
        pb.rotation_euler = (math.radians(pitch) * k, math.radians(yaw) * k, 0.0)
    bpy.context.view_layer.update()

def head_delta():
    pb = ARM.pose.bones["Head"]
    return (ARM.matrix_world @ pb.matrix) @ (ARM.matrix_world @ pb.bone.matrix_local).inverted()

try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 480, 480; sc.render.film_transparent = True
    for nm, en, rot in (("HL_key", 3.0, (42, 0, 145)), ("HL_fill", 1.4, (65, 0, -40)), ("HL_front", 1.6, (55, 0, 20)), ("HL_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("HL_cam"); cam = bpy.data.objects.new("HL_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0, -0.03, 1.60))
    SHOTS = [("str", 0, 0, T + Vector((-0.62, -0.95, 0.12))), ("turn", 50, 0, T + Vector((0.55, -1.0, 0.10))), ("down", 0, 30, T + Vector((1.15, -0.25, 0.05)))]
    ONLY = os.environ.get("HELM_ONLY", "")
    for g, (items, neckfn, turns) in GROUPS.items():
        for key, name, fn in items:
            tag = g + key
            if ONLY and tag not in ONLY.split(","): continue
            for sname, yaw, pitch, loc in SHOTS:
                pose_head(0, 0); neckfn(); mark = len(parts); fn(); helm = parts[mark:]
                pose_head(yaw, pitch)
                if turns:   # B: the helmet turns with her head
                    D = head_delta()
                    for o in helm: o.matrix_world = D @ o.matrix_world
                cd.lens = 50; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
                sc.render.filepath = os.path.join(OUT, "h4_%s_%s.png" % (tag, sname)); bpy.ops.render.render(write_still=True)
                print("rendered", tag, sname, file=LOG); LOG.flush()
                for o in parts: bpy.data.objects.remove(o, do_unlink=True)
                parts.clear()
    pose_head(0, 0)
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
