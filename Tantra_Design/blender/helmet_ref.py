# The astronavigator's helmet anew: the helmet after the user's Interstellar photos.
#   blender --background astronavigator_suit.blend --python helmet_ref.py -> renders\helm3_<A|B>_<front|q34|side|rear>.png
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
LOG = open(os.path.join(OUT, "helm3_log.txt"), "w", encoding="utf-8")
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


# ==== the helmet as the user showed it (2026-10-18: Interstellar's helmet - «зачем мне круглый, если я показал, какой мне
# нужен»): an egg dome; a big wrap-round visor, its top edge level, its lower edge dropping to the chin; a dark band over
# the visor running back along the dome's side; a large temple plate with its port (ours hexagonal) laid on the side;
# a tall neck stack of metal rings over a soft dark collar. The sizes from the old helmet. ====
for n in ("Helmet", "HelmHardware", "HelmLiner", "HelmPods", "HelmRib", "HelmRibRed", "Visor", "SunShade", "ShadeArms", "NeckSeal",
          "JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR", "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR"):
    if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
if sc.world is None: sc.world = bpy.data.worlds.new("HL_world")
sc.world.use_nodes = True
_bg = sc.world.node_tree.nodes.get("Background")
if _bg: _bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); _bg.inputs[1].default_value = 1.0
def mk(name, col, metal, rough):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
    return m
GLASS = mk("H3_glass", (0.55, 0.42, 0.25), 0.6, 0.05)
try: GLASS.surface_render_method = 'BLENDED'
except Exception: GLASS.blend_method = 'BLEND'
_gb = next(n for n in GLASS.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'); _gb.inputs["Alpha"].default_value = 0.30   # tinted, her face seen through
BANDM = mk("H3_band", (0.28, 0.29, 0.31), 0.35, 0.45)
SILVER = mk("H3_silver", (0.78, 0.79, 0.81), 1.0, 0.25)
COLLAR = mk("H3_collar", (0.10, 0.10, 0.11), 0.0, 0.8)
WHITE = SHELL

HC = Vector((0.0, -0.030, 1.640)); RX_, RY_, RZ_ = 0.145, 0.160, 0.178
def S_pt(th, ph, k=1.0): return HC + Vector((RX_ * math.cos(th) * math.cos(ph), RY_ * math.cos(th) * math.sin(ph), RZ_ * math.sin(th))) * k
def nrm(th, ph): return Vector((math.cos(th) * math.cos(ph) / RX_, math.cos(th) * math.sin(ph) / RY_, math.sin(th) / RZ_)).normalized()
def th_of(z): return math.asin(max(-0.99, min(0.99, (z - HC.z) / RZ_)))
FRONT = -math.pi / 2; HALF = math.radians(66)                     # the visor wraps 66 deg each side of her front
ZT = 1.715                                                        # its top edge, level
def zbot(ph):                                                     # its lower edge: low at the chin, rising to the sides
    u = (ph - FRONT) / HALF; return 1.488 + 0.075 * u * u
TH0 = th_of(1.452)                                                # the shell's lower rim, on the neck stack

def dome():
    nu, nv = 64, 26; bm = bmesh.new(); rings = []
    for i in range(nv + 1):
        th = TH0 + (math.pi / 2 - TH0) * i / nv
        if i == nv: rings.append([bm.verts.new(S_pt(th, 0))] * nu); continue
        rings.append([bm.verts.new(S_pt(th, 2 * math.pi * j / nu)) for j in range(nu)])
    def open_(i, j):   # the face's opening: the visor's window, the shell cut behind the glass
        th = TH0 + (math.pi / 2 - TH0) * (i + 0.5) / nv; ph = 2 * math.pi * (j + 0.5) / nu
        d = (ph - FRONT + math.pi) % (2 * math.pi) - math.pi; z = S_pt(th, ph).z
        return abs(d) < HALF + 0.03 and zbot(FRONT + max(-HALF, min(HALF, d))) - 0.006 < z < ZT + 0.006
    for i, (a, b) in enumerate(zip(rings, rings[1:])):
        for j in range(nu):
            if not open_(i, j): quad(bm, a[j], a[(j + 1) % nu], b[(j + 1) % nu], b[j])
    return obj("B_dome", bm, WHITE, smooth=True)

def window(dph=0.0, dz_top=0.0, dz_bot=0.0, n=40, corner=0.035):
    """the visor's outline as (th, ph): the lower edge left to right, up the right side, the top right to left, down the
    left side; grown by dph round and dz up/down"""
    a, b = FRONT - HALF - dph, FRONT + HALF + dph
    lo = [(th_of(zbot(min(max(p, FRONT - HALF), FRONT + HALF)) - dz_bot), p) for p in [a + (b - a) * i / n for i in range(n + 1)]]
    zr0, zr1 = zbot(FRONT + HALF) - dz_bot, ZT + dz_top
    right = [(th_of(zr0 + (zr1 - zr0) * i / 8), b) for i in range(1, 8)]
    top = [(th_of(ZT + dz_top), p) for p in [b - (b - a) * i / n for i in range(n + 1)]]
    left = [(th_of(zr1 - (zr1 - zr0) * i / 8), a) for i in range(1, 8)]
    return lo + right + top + left

def surf_strip(name, loop_in, loop_out, h, mat, closed=True, h_in=None):
    bm = bmesh.new(); h_in = h if h_in is None else h_in
    ib = [bm.verts.new(S_pt(*p) - nrm(*p) * 0.002) for p in loop_in]; it = [bm.verts.new(S_pt(*p) + nrm(*p) * h_in) for p in loop_in]
    ob = [bm.verts.new(S_pt(*p) - nrm(*p) * 0.002) for p in loop_out]; ot = [bm.verts.new(S_pt(*p) + nrm(*p) * h) for p in loop_out]
    n = len(loop_in); rng = range(n) if closed else range(n - 1)
    for j in rng:
        k = (j + 1) % n
        quad(bm, it[j], it[k], ot[k], ot[j]); quad(bm, ib[j], ib[k], it[k], it[j]); quad(bm, ot[j], ot[k], ob[k], ob[j])
    if not closed: quad(bm, ib[0], it[0], ot[0], ob[0]); quad(bm, ob[-1], ot[-1], it[-1], ib[-1])
    return obj(name, bm, mat, smooth=True)

def glass():
    """the visor: the dome's own surface over the window, 6 mm proud (it sits in front of the shell's opening)"""
    bm = bmesh.new(); rows = []; n = 40
    for j in range(13):
        row = []
        for i in range(n + 1):
            p = FRONT - HALF + 2 * HALF * i / n
            z = zbot(p) + (ZT - zbot(p)) * j / 12; th = th_of(z)
            row.append(bm.verts.new(S_pt(th, p) + nrm(th, p) * 0.006))
        rows.append(row)
    for a, b in zip(rows, rows[1:]):
        for i in range(n): quad(bm, a[i], a[i + 1], b[i + 1], b[i])
    return obj("glass", bm, GLASS, smooth=True)

def band_path(name, pts, w, h, mat):
    """a band of width w along (th, ph) points, offset sideways in the parameters (w split by the local metric)"""
    L, R = [], []
    for i, (th, ph) in enumerate(pts):
        a = S_pt(*pts[max(i - 1, 0)]); b = S_pt(*pts[min(i + 1, len(pts) - 1)]); t = (b - a).normalized()
        n_ = nrm(th, ph); s = n_.cross(t).normalized()
        dth = 1e-3; dph = 1e-3
        Pt = (S_pt(th + dth, ph) - S_pt(th, ph)) / dth; Pp = (S_pt(th, ph + dph) - S_pt(th, ph)) / dph
        # solve [Pt Pp] [a b]^T = s * w/2 in least squares
        A11, A12, A22 = Pt.dot(Pt), Pt.dot(Pp), Pp.dot(Pp); v = s * (w / 2); b1, b2 = Pt.dot(v), Pp.dot(v); det = A11 * A22 - A12 * A12
        da = (b1 * A22 - b2 * A12) / det; db = (A11 * b2 - A12 * b1) / det
        L.append((th + da, ph + db)); R.append((th - da, ph - db))
    return surf_strip(name, L, R, h, mat, closed=False)

def build(port_hex=True, red=True):
    dome(); glass()
    # the visor's rim: a thin grey frame round the glass
    surf_strip("rim", window(), window(0.035, 0.012, 0.010), 0.009, BANDM)
    # the dark band over the visor, running back along the dome's sides to the back, falling a little
    for sg in (1, -1):
        pts = []
        for i in range(31):
            u = i / 30
            ph = FRONT + sg * (HALF * (1 - u) + 0.0) + sg * u * (math.pi * 0.95 - 0.0)   # from the visor's top corner round to the back
            ph = FRONT + sg * (HALF * 0.0 + u * math.radians(170))
            z = ZT + 0.028 - 0.045 * max(0.0, (u - 0.35) / 0.65) ** 1.5
            pts.append((th_of(z), ph))
        band_path("band%d" % sg, pts, 0.042, 0.007, BANDM)
        band_path("bandedge%d" % sg, [(t + 0.13, p) for t, p in pts[:28]], 0.008, 0.009, WHITE)          # the white raised edge over it
        if red: band_path("bandred%d" % sg, [(t - 0.005, p) for t, p in pts[2:30]], 0.005, 0.0095, RED)
    # the temple plate: a large plate laid on the side, its port (hexagonal - ours), behind the visor
    for sg in (1, -1):
        th, ph = th_of(1.615), (0.0 if sg > 0 else math.pi) + sg * math.radians(22)
        p, n_ = S_pt(th, ph), nrm(th, ph)
        poly_lathe("tplate%d" % sg, p - n_ * 0.004, n_, [(0.0, 0.0), (0.058, 0.0), (0.058, 0.010), (0.052, 0.015), (0.0, 0.015)], WHITE, 6 if port_hex else 24, 0)
        poly_lathe("tring%d" % sg, p + n_ * 0.011, n_, [(0.036, 0.0), (0.036, 0.010), (0.028, 0.010), (0.028, 0.0)], BANDM, 6 if port_hex else 24, 0)
        poly_lathe("tport%d" % sg, p + n_ * 0.011, n_, [(0.0, 0.0), (0.027, 0.0), (0.027, 0.007), (0.0, 0.007)], DARK, 6 if port_hex else 24, 0)
        poly_lathe("tred%d" % sg, p + n_ * 0.018, n_, [(0.006, 0.0), (0.006, 0.004), (0.0, 0.004)], RED, 12, 0)
        # the latch on the visor's lower corner (the little box in the photo)
        q_th, q_ph = th_of(zbot(FRONT + sg * HALF) + 0.012), FRONT + sg * (HALF + 0.20)
        q, qn = S_pt(q_th, q_ph), nrm(q_th, q_ph)
        tng = (S_pt(q_th, q_ph + 0.01) - S_pt(q_th, q_ph)).normalized()
        prism("latch%d" % sg, rect(0.055, 0.022, 0.004), 0, 0.016, BANDM, ch=0.003,
              M=Matrix.Translation(q) @ Matrix((tng, qn.cross(tng), qn)).transposed().to_4x4())
    # the back: a raised spine plate with three vents
    surf_strip("rearplate", [(th_of(z), math.pi / 2 - 0.22) for z in [1.50 + 0.2 * i / 10 for i in range(11)]],
               [(th_of(z), math.pi / 2 + 0.22) for z in [1.50 + 0.2 * i / 10 for i in range(11)]], 0.008, WHITE, closed=False)
    for k in range(3):
        z = 1.54 + k * 0.035; th = th_of(z); p, n_ = S_pt(th, math.pi / 2), nrm(th, math.pi / 2)
        prism("vent%d" % k, rect(0.05, 0.008, 0.002), 0, 0.012, DARK, ch=0.001,
              M=Matrix.Translation(p) @ Matrix((Vector((1, 0, 0)), n_.cross(Vector((1, 0, 0))), n_)).transposed().to_4x4())
    # the neck stack: silver, dark, silver rings; under them the soft dark collar
    lathe("nk_collar", (0, -0.04, 1.36), (0, 0, 1), [(0.075, 0.0), (0.108, 0.01), (0.118, 0.045), (0.112, 0.075), (0.098, 0.09)], COLLAR, 40)
    lathe("nk1", (0, -0.035, 1.430), (0, 0, 1), [(0.100, 0), (0.122, 0), (0.122, 0.012), (0.100, 0.012)], SILVER, 48)
    lathe("nk2", (0, -0.035, 1.442), (0, 0, 1), [(0.104, 0), (0.118, 0), (0.118, 0.008), (0.104, 0.008)], DARK, 48)
    lathe("nk3", (0, -0.033, 1.450), (0, 0, 1), [(0.110, 0), (0.132, 0), (0.132, 0.010), (0.110, 0.010)], SILVER, 48)

VARS = [("A", "по фото, порт шестигранный, красная линия", dict(port_hex=True, red=True)),
        ("B", "по фото, порт круглый, без красной линии", dict(port_hex=False, red=False))]
try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 700, 700; sc.render.film_transparent = True
    for nm, en, rot in (("HL_key", 3.0, (42, 0, 145)), ("HL_fill", 1.4, (65, 0, -40)), ("HL_front", 1.6, (55, 0, 20)), ("HL_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("HL_cam"); cam = bpy.data.objects.new("HL_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0, -0.03, 1.60))
    VIEWS = {"front": (T + Vector((0.0, -1.1, 0.06)), 50), "q34": (T + Vector((-0.70, -0.85, 0.14)), 50), "side": (T + Vector((1.15, 0.0, 0.03)), 50), "rear": (T + Vector((0.65, 0.95, 0.25)), 50)}
    for key, name, kw in VARS:
        build(**kw)
        for vn, (loc, lens) in VIEWS.items():
            cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "helm3_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
