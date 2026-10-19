# The astronavigator's helmet anew: ten mock-ups of a round dome with laid-on geometry on the old helmet's sizes (not its mesh), the visor kept.
#   blender --background astronavigator_suit.blend --python helmet_round.py -> renders\helm2_<1..10>_<front|side|rear>.png
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
LOG = open(os.path.join(OUT, "helm2_log.txt"), "w", encoding="utf-8")
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


# ==== the helmet, round two (the user, 2026-10-18, with Interstellar's helmet: «вот же, а не сплющивание»): the dome stays
# near round, at the old helmet's sizes; the geometry is laid on it - the visor's bezel (a raised band round the glass,
# its top sweeping back over the crown), the temple plates with their ports, stepped panels, seams, the neck ring's stack.
# Our own: hexagonal ports, chamfered edges, the family's red line. ====
for n in ("Helmet", "HelmHardware", "HelmLiner", "HelmPods", "HelmRib", "HelmRibRed", "Visor", "SunShade", "ShadeArms",
          "JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR", "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR"):
    if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
if sc.world is None: sc.world = bpy.data.worlds.new("HL_world")
sc.world.use_nodes = True
_bg = sc.world.node_tree.nodes.get("Background")
if _bg: _bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); _bg.inputs[1].default_value = 1.0
GLASS = bpy.data.materials.new("H_glass"); GLASS.use_nodes = True
_g = next(n for n in GLASS.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
_g.inputs["Base Color"].default_value = (0.80, 0.58, 0.20, 1); _g.inputs["Metallic"].default_value = 1.0; _g.inputs["Roughness"].default_value = 0.12
GREY = bpy.data.materials.new("H_grey"); GREY.use_nodes = True
_q = next(n for n in GREY.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
_q.inputs["Base Color"].default_value = (0.30, 0.31, 0.33, 1); _q.inputs["Metallic"].default_value = 0.3; _q.inputs["Roughness"].default_value = 0.45

HC = Vector((0.0, -0.038, 1.637)); RX_, RY_, RZ_ = 0.142, 0.155, 0.170
VZ0, VZ1, VW = 1.523, 1.695, 0.240; VZM = (VZ0 + VZ1) / 2; VH = VZ1 - VZ0
def sp(w, e): return math.copysign(abs(w) ** e, w)
def S_pt(th, ph, k=1.0):
    """the dome: an ellipsoid at the old sizes; th up from the equator, ph round (+x is 0, her front -y is -90 deg)"""
    return HC + Vector((RX_ * math.cos(th) * math.cos(ph), RY_ * math.cos(th) * math.sin(ph), RZ_ * math.sin(th))) * k
def nrm(th, ph): return Vector((math.cos(th) * math.cos(ph) / RX_, math.cos(th) * math.sin(ph) / RY_, math.sin(th) / RZ_)).normalized()
def xz_to_thph(x, z):
    th = math.asin(max(-0.98, min(0.98, (z - HC.z) / RZ_)))
    ph = -math.pi / 2 + math.asin(max(-0.98, min(0.98, x / (RX_ * max(0.25, math.cos(th))))))
    return th, ph

def dome(name):
    nu, nv = 48, 20; th0 = math.asin((1.467 - HC.z) / RZ_)
    bm = bmesh.new(); rings = []
    for i in range(nv + 1):
        th = th0 + (math.pi / 2 - th0) * i / nv
        if i == nv: rings.append([bm.verts.new(S_pt(th, 0))] * nu); continue
        rings.append([bm.verts.new(S_pt(th, 2 * math.pi * j / nu)) for j in range(nu)])
    for a, b in zip(rings, rings[1:]):
        for j in range(nu): quad(bm, a[j], a[(j + 1) % nu], b[(j + 1) % nu], b[j])
    return obj(name, bm, SHELL, smooth=True)

def win_loop(m, n=48, e=0.35, top=0.0):
    """the visor window's outline grown by m (a rounded rectangle, e - its squareness), mapped onto the dome: (th, ph)
    pairs; top lifts the upper edge by that much more (a brow)"""
    out = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x = (VW / 2 + m) * sp(math.cos(t), e); z = VZM + (VH / 2 + m + (top if math.sin(t) > 0 else 0)) * sp(math.sin(t), e)
        out.append(xz_to_thph(x, z))
    return out

def glass(name):
    bm = bmesh.new(); L = win_loop(0.0); rings = []
    for s in (0.0, 0.35, 0.7, 1.0):
        ring = []
        for th, ph in L:
            x = (S_pt(th, ph).x) * s; z = VZM + (S_pt(th, ph).z - VZM) * s
            t2, p2 = xz_to_thph(x, z); ring.append(bm.verts.new(S_pt(t2, p2) + nrm(t2, p2) * 0.007))
        rings.append(ring)
    n = len(L)
    for a, b in zip(rings, rings[1:]):
        for j in range(n): quad(bm, a[j], a[(j + 1) % n], b[(j + 1) % n], b[j])
    return obj(name, bm, GLASS, smooth=True)

def strip(name, loop_in, loop_out, h, mat, closed=True):
    """a raised band on the dome between two lines of (th, ph): its top h off the shell, its walls down to it"""
    bm = bmesh.new()
    ib = [bm.verts.new(S_pt(*p) - nrm(*p) * 0.002) for p in loop_in]; it = [bm.verts.new(S_pt(*p) + nrm(*p) * h) for p in loop_in]
    ob = [bm.verts.new(S_pt(*p) - nrm(*p) * 0.002) for p in loop_out]; ot = [bm.verts.new(S_pt(*p) + nrm(*p) * h) for p in loop_out]
    n = len(loop_in); rng = range(n) if closed else range(n - 1)
    for j in rng:
        k = (j + 1) % n
        quad(bm, it[j], it[k], ot[k], ot[j]); quad(bm, ib[j], ib[k], it[k], it[j]); quad(bm, ot[j], ot[k], ob[k], ob[j])
    if not closed:
        quad(bm, ib[0], it[0], ot[0], ob[0]); quad(bm, ob[-1], ot[-1], it[-1], ib[-1])
    return obj(name, bm, mat, smooth=False)

def bezel(name, m=0.026, h=0.008, e=0.35, top=0.0, mat=None):
    strip(name, win_loop(0.0, e=e), win_loop(m, e=e, top=top), h, mat or GREY)

def band(name, path, w, h, mat):
    """a band of width w along a path of (th, ph) on the dome"""
    L, R = [], []
    for i, (th, ph) in enumerate(path):
        a = S_pt(*path[max(i - 1, 0)]); b = S_pt(*path[min(i + 1, len(path) - 1)])
        t = (b - a).normalized(); n_ = nrm(th, ph); s = n_.cross(t).normalized()
        p = S_pt(th, ph)
        def back(q):   # (th, ph) of a point near the dome
            d = q - HC; return (math.asin(max(-1, min(1, d.z / RZ_ / max(1e-6, (d.x ** 2 / RX_ ** 2 + d.y ** 2 / RY_ ** 2 + d.z ** 2 / RZ_ ** 2) ** 0.5)))), math.atan2(d.y / RY_, d.x / RX_))
        L.append(back(p + s * w / 2)); R.append(back(p - s * w / 2))
    return strip(name, L, R, h, mat, closed=False)

def meridian(ph, th0, th1, n=24): return [(th0 + (th1 - th0) * i / n, ph) for i in range(n + 1)]
def parallel(th, ph0, ph1, n=40): return [(th, ph0 + (ph1 - ph0) * i / n) for i in range(n + 1)]
def over_top(th0, n=30):   # from the bezel's top over the crown and down the back: ph -90 then +90
    pts = [(th0 + (math.pi / 2 - 0.02 - th0) * i / (n // 2), -math.pi / 2) for i in range(n // 2 + 1)]
    pts += [(math.pi / 2 - 0.02 - (math.pi / 2 - 0.02 - 0.15) * i / (n // 2), math.pi / 2) for i in range(1, n // 2 + 1)]
    return pts

def temple(name, sg, th=0.0, ph_off=0.0, r=0.042, nu=6, ring=True, lamp=False, plate=True):
    """the temple module: a plate on the dome's side, a hexagonal port with its red ring (lamp: a lamp instead)"""
    ph = (0.0 if sg > 0 else math.pi) + ph_off; p = S_pt(th, ph); n_ = nrm(th, ph)
    if plate:
        band(name + "pl", [(th + d, ph) for d in (-0.32, -0.16, 0.0, 0.16, 0.32)], 0.085, 0.006, SHELL)
    poly_lathe(name + "port", p + n_ * 0.004, n_, [(0.0, 0.0), (r, 0.0), (r, 0.012), (r * 0.82, 0.016), (0.0, 0.016)], GREY, nu, 0)
    if ring: poly_lathe(name + "ring", p + n_ * 0.016, n_, [(r * 0.62, 0.0), (r * 0.62, 0.004), (r * 0.5, 0.004), (r * 0.5, 0.0)], RED, nu, 0)
    poly_lathe(name + "core", p + n_ * 0.016, n_, [(0.0, 0.0), (r * 0.45, 0.0), (r * 0.45, 0.006), (0.0, 0.006)], DARK if not lamp else GLASS, nu, 0)

def neck():
    lathe("nk1", (0, -0.045, 1.440), (0, 0, 1), [(0.088, 0), (0.108, 0), (0.108, 0.016), (0.088, 0.016)], FRAME, 32)
    lathe("nk2", (0, -0.045, 1.456), (0, 0, 1), [(0.092, 0), (0.114, 0), (0.114, 0.012), (0.092, 0.012)], DARK, 32)
    poly_lathe("nk3", (0, -0.045, 1.466), (0, 0, 1), [(0.100, 0), (0.122, 0), (0.122, 0.010), (0.100, 0.010)], SHELL, 12, 15)

TH_TOP = math.asin((VZ1 + 0.026 - HC.z) / RZ_)
def v1():   # bezel, its top swept back over the crown to the nape; hexagonal temple ports
    bezel("bz"); band("sweep", over_top(TH_TOP), 0.05, 0.008, GREY); band("sweepR", over_top(TH_TOP + 0.02), 0.008, 0.011, RED)
    temple("tR", 1); temple("tL", -1)
def v2():   # an octagonal bezel (chamfered corners), two parallel crown ribs
    bezel("bz", e=0.15, m=0.024)
    for off in (-0.25, 0.25): band("cr%d" % int(off * 100), [(t, -math.pi / 2 + off * (1 - t / 1.6)) for t in [TH_TOP + (math.pi / 2 - 0.05 - TH_TOP) * i / 14 for i in range(15)]], 0.016, 0.008, SHELL)
    temple("tR", 1, ring=False); temple("tL", -1, ring=False)
def v3():   # a crown plate with a stepped edge (a cap laid on the dome), the bezel under it, a rear seam
    bezel("bz"); strip("cap", [(0.95, 2 * math.pi * i / 48) for i in range(48)], [(0.80, 2 * math.pi * i / 48) for i in range(48)], 0.007, SHELL)
    band("seam", parallel(0.25, math.radians(-20), math.radians(200)), 0.006, 0.002, DARK); temple("tR", 1); temple("tL", -1)
def v4():   # the bezel's top carried right round the head as a brow hoop; side modules sit on it
    bezel("bz", top=0.006); band("hoop", parallel(TH_TOP - 0.03, math.radians(-60), math.radians(240)), 0.028, 0.008, GREY)
    band("hoopR", parallel(TH_TOP - 0.03, math.radians(-50), math.radians(230)), 0.006, 0.011, RED); temple("tR", 1, th=0.15); temple("tL", -1, th=0.15)
def v5():   # a spine band from the bezel to the nape, hexagonal temple plates with lamps
    bezel("bz", m=0.022); band("spine", over_top(TH_TOP), 0.028, 0.01, SHELL); band("spineR", over_top(TH_TOP), 0.006, 0.013, RED)
    temple("tR", 1, lamp=True, ring=False); temple("tL", -1, lamp=True, ring=False)
def v6():   # layered: an upper shell a little larger over the lower, a step all round; the bezel
    bezel("bz"); strip("upper", [(0.55, 2 * math.pi * i / 48) for i in range(48)], [(1.45, 2 * math.pi * i / 48) for i in range(48)], 0.006, SHELL)
    band("stepR", parallel(0.56, math.radians(-40), math.radians(220)), 0.005, 0.009, RED); temple("tR", 1, th=0.1); temple("tL", -1, th=0.1)
def v7():   # a thick brow over the glass (the sun shield's housing), swept back at both ends
    bezel("bz", top=0.022, m=0.022)
    band("brow", [(TH_TOP + 0.05, -math.pi / 2 + d) for d in [x * 0.08 for x in range(-11, 12)]], 0.045, 0.014, GREY)
    temple("tR", 1); temple("tL", -1)
def v8():   # two rails from the bezel's upper corners back along the sides, a rear plate between them
    bezel("bz")
    for sg in (1, -1):
        band("rail%d" % sg, [(0.75 - 0.2 * i / 20, -math.pi / 2 + sg * (0.6 + 1.9 * i / 20)) for i in range(21)], 0.02, 0.008, GREY)
    band("rear", meridian(math.pi / 2, -0.2, 0.8), 0.07, 0.006, SHELL); temple("tR", 1, th=-0.05); temple("tL", -1, th=-0.05)
def v9():   # a segmented crown: three panels front to back, the middle one raised, dark seams between
    bezel("bz")
    band("mid", over_top(TH_TOP), 0.07, 0.009, SHELL)
    for off in (-0.045, 0.045): band("sm%d" % int(off * 1000), [(t, ph + off * 2.2 * (1 if ph < 0 else -1)) for t, ph in over_top(TH_TOP)], 0.005, 0.002, DARK)
    temple("tR", 1); temple("tL", -1)
def v10():  # clean: the bezel, one temple port each side, the neck ring's stack - nothing more
    bezel("bz", m=0.03, h=0.009); temple("tR", 1, plate=False); temple("tL", -1, plate=False)

HELMS = [("1", "Окантовка назад", v1), ("2", "Восьмигранная рамка", v2), ("3", "Шапка-ступень", v3), ("4", "Обруч", v4), ("5", "Хребет", v5),
         ("6", "Два слоя", v6), ("7", "Бровь", v7), ("8", "Височные рельсы", v8), ("9", "Сегменты", v9), ("10", "Чисто", v10)]

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
        dome("B_dome"); glass("glass"); neck(); fn()
        for vn, (loc, lens) in VIEWS.items():
            cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "helm2_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
