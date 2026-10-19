# The user's helmet with the new visor (b), the lower third and the back's lower half worked (2026-10-18: «нужно поработать с
# нижней третью, а сзади с нижней половиной; перед не менять») - laid-on parts only, the shell's form untouched. Earlier:
# the new visor (2026-10-18: «само стекло вот такое, не увеличивая
# шлем»): a wider window with a level top and a lower edge rising to the sides, a grey rim, a hexagonal temple port.
# The shell is cut where the new glass goes; render only, the .blend is not saved.
#   blender --background astronavigator_suit.blend --python helmet_visor.py -> renders\hv_<a|b|c>_<front|q34|side|rear>.png
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "hk_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
for n in ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR", "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR",
          "Visor", "SunShade", "ShadeArms"):
    if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
CUT = ["Helmet", "HelmLiner", "HelmRib", "HelmRibRed", "HelmHardware"]
C = Vector((0.0, -0.038, 1.637)); R = Vector((0.142, 0.155, 0.170))
def mk(name, col, metal, rough, alpha=None):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
    if alpha is not None:
        b.inputs["Alpha"].default_value = alpha
        try: m.surface_render_method = 'BLENDED'
        except Exception: m.blend_method = 'BLEND'
    return m
GLASS = mk("HV_glass", (0.55, 0.42, 0.25), 0.6, 0.05, 0.30)
GREY = mk("HV_grey", (0.28, 0.29, 0.31), 0.35, 0.45)
RED = mk("HV_red", (0.78, 0.09, 0.07), 0.1, 0.35)
DARK = mk("HV_dark", (0.05, 0.055, 0.06), 0.3, 0.5)
WHITE = bpy.data.materials.get("HelmetShell") or mk("HV_white", (0.88, 0.89, 0.90), 0.05, 0.3)
parts = []
def obj(name, bm, mat, smooth=True):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(mat)
    for p in me.polygons: p.use_smooth = smooth
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); parts.append(o); return o

def S(th, ph): return C + Vector((R.x * math.cos(th) * math.cos(ph), R.y * math.cos(th) * math.sin(ph), R.z * math.sin(th)))
def N(th, ph): return Vector((math.cos(th) * math.cos(ph) / R.x, math.cos(th) * math.sin(ph) / R.y, math.sin(th) / R.z)).normalized()
def th_of(z): return math.asin(max(-0.99, min(0.99, (z - C.z) / R.z)))
FRONT = -math.pi / 2

class Win:
    def __init__(s, half_deg, top, bot, rise): s.h, s.top, s.bot, s.rise = math.radians(half_deg), top, bot, rise
    def z0(s, u): return s.bot + s.rise * u * u
    def inside(s, p, grow=0.0):
        d = p - C; a = math.atan2(d.x / R.x, -d.y / R.y); u = a / (s.h + grow * 3)
        return abs(u) < 1 and s.z0(max(-1, min(1, a / s.h))) - grow < p.z < s.top + grow and d.y < 0.03

def cut(w):
    for n in CUT:
        o = bpy.data.objects[n]; me = o.data; bm = bmesh.new(); bm.from_mesh(me); M = o.matrix_world
        dead = [f for f in bm.faces if w.inside(M @ f.calc_center_median(), 0.004)]
        bmesh.ops.delete(bm, geom=dead, context='FACES'); bm.to_mesh(me); bm.free(); me.update()

def glass(w):
    bm = bmesh.new(); rows = []; n, m = 40, 12
    for j in range(m + 1):
        row = []
        for i in range(n + 1):
            u = -1 + 2 * i / n; ph = FRONT + u * w.h; z = w.z0(u) + (w.top - w.z0(u)) * j / m; th = th_of(z)
            row.append(bm.verts.new(S(th, ph) + N(th, ph) * 0.004))
        rows.append(row)
    for a, b in zip(rows, rows[1:]):
        for i in range(n): bm.faces.new((a[i], a[i + 1], b[i + 1], b[i]))
    return obj("glass", bm, GLASS)

def tube(name, pts, r, mat, ns=8, closed=True):
    bm = bmesh.new(); rings = []
    P = pts + ([pts[0]] if closed else [])
    for i, p in enumerate(P):
        t = (P[min(i + 1, len(P) - 1)] - P[max(i - 1, 0)]).normalized()
        a = t.cross(Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))).normalized(); b = t.cross(a)
        rings.append([bm.verts.new(p + (a * math.cos(2 * math.pi * k / ns) + b * math.sin(2 * math.pi * k / ns)) * r) for k in range(ns)])
    for x, y in zip(rings, rings[1:]):
        for k in range(ns): bm.faces.new((x[k], x[(k + 1) % ns], y[(k + 1) % ns], y[k]))
    return obj(name, bm, mat)

def rim(w, r=0.007):
    pts = []
    n = 40
    for i in range(n + 1): u = -1 + 2 * i / n; pts.append((th_of(w.z0(u)), FRONT + u * w.h))
    for i in range(1, 8): pts.append((th_of(w.z0(1) + (w.top - w.z0(1)) * i / 8), FRONT + w.h))
    for i in range(1, n + 1): u = 1 - 2 * i / n; pts.append((th_of(w.top), FRONT + u * w.h))
    for i in range(1, 8): pts.append((th_of(w.top - (w.top - w.z0(-1)) * i / 8), FRONT - w.h))
    tube("rim", [S(t, p) + N(t, p) * 0.005 for t, p in pts], r, GREY)

def hexport(sg, z=1.63, ph_off=12):
    th = th_of(z); ph = (0.0 if sg > 0 else math.pi) + sg * math.radians(-ph_off)
    p, n_ = S(th, ph), N(th, ph)
    for name, rad, h0, h1, mat in (("hp", 0.030, 0.0, 0.012, WHITE), ("hr", 0.024, 0.012, 0.016, RED), ("hc", 0.017, 0.016, 0.019, DARK)):
        bm = bmesh.new(); t = n_.cross(Vector((0, 0, 1))).normalized(); b = n_.cross(t)
        lo = [bm.verts.new(p + n_ * h0 + (t * math.cos(math.radians(60 * k)) + b * math.sin(math.radians(60 * k))) * rad) for k in range(6)]
        hi = [bm.verts.new(p + n_ * h1 + (t * math.cos(math.radians(60 * k)) + b * math.sin(math.radians(60 * k))) * rad) for k in range(6)]
        bm.faces.new(hi); bm.faces.new(lo[::-1])
        for k in range(6): bm.faces.new((lo[k], lo[(k + 1) % 6], hi[(k + 1) % 6], hi[k]))
        obj("%s%d" % (name, sg), bm, mat, smooth=False)


W = Win(62, 1.712, 1.528, 0.050)
AV = math.radians(62 + 9)                    # the parts stop this far round from her front (the visor's edge and its rim)
def patch(name, ph0, ph1, z0, z1, h, mat, n=48, m=6, inner=0.0015):
    """a thickened patch of the shell between ph0..ph1 and z0..z1 (z0, z1 may be functions of ph), its top h off the shell"""
    Z0 = z0 if callable(z0) else (lambda p, v=z0: v); Z1 = z1 if callable(z1) else (lambda p, v=z1: v)
    bm = bmesh.new(); top, bot = [], []
    for j in range(m + 1):
        rt, rb = [], []
        for i in range(n + 1):
            ph = ph0 + (ph1 - ph0) * i / n; z = Z0(ph) + (Z1(ph) - Z0(ph)) * j / m; th = th_of(z)
            rt.append(bm.verts.new(S(th, ph) + N(th, ph) * h)); rb.append(bm.verts.new(S(th, ph) + N(th, ph) * inner))
        top.append(rt); bot.append(rb)
    for j in range(m):
        for i in range(n):
            bm.faces.new((top[j][i], top[j][i + 1], top[j + 1][i + 1], top[j + 1][i])); bm.faces.new((bot[j][i], bot[j + 1][i], bot[j + 1][i + 1], bot[j][i + 1]))
    for i in range(n):
        bm.faces.new((bot[0][i], bot[0][i + 1], top[0][i + 1], top[0][i])); bm.faces.new((top[m][i], top[m][i + 1], bot[m][i + 1], bot[m][i]))
    for j in range(m):
        bm.faces.new((bot[j][0], top[j][0], top[j + 1][0], bot[j + 1][0])); bm.faces.new((top[j][n], bot[j][n], bot[j + 1][n], top[j + 1][n]))
    return obj(name, bm, mat)
L0, L1 = FRONT + AV, FRONT + 2 * math.pi - AV            # round the back, from one visor edge to the other
BK = math.pi / 2                                           # her back
ZB = 1.47                                                  # the shell's lower edge (on the neck ring)

HELMOBJ = bpy.data.objects["Helmet"]
def onmesh(p):
    """the point and normal of the user's helmet surface nearest to p (the parts lie on the real shell, not an ellipsoid)"""
    M = HELMOBJ.matrix_world; ok, loc, nr, _ = HELMOBJ.closest_point_on_mesh(M.inverted() @ p)
    if not ok: return p, (p - C).normalized()
    n = (M.to_3x3() @ nr).normalized()
    if n.dot(p - C) < 0: n = -n
    return M @ loc, n
def patch2(name, ph0, ph1, z0, z1, h, mat, n=48, m=8, inner=0.0015, smooth=True):
    """a laid-on part between ph0..ph1, z0(ph)..z1(ph); its thickness h(ph, t) (t 0 at z0, 1 at z1), on the real shell"""
    Z0 = z0 if callable(z0) else (lambda p, v=z0: v); Z1 = z1 if callable(z1) else (lambda p, v=z1: v)
    H = h if callable(h) else (lambda p, t, v=h: v)
    bm = bmesh.new(); top, bot = [], []
    for j in range(m + 1):
        rt, rb = [], []
        for i in range(n + 1):
            ph = ph0 + (ph1 - ph0) * i / n; t = j / m; z = Z0(ph) + (Z1(ph) - Z0(ph)) * t; th = th_of(z)
            q, nn = onmesh(S(th, ph))
            rt.append(bm.verts.new(q + nn * max(inner + 0.0005, H(ph, t)))); rb.append(bm.verts.new(q + nn * inner))
        top.append(rt); bot.append(rb)
    for j in range(m):
        for i in range(n):
            bm.faces.new((top[j][i], top[j][i + 1], top[j + 1][i + 1], top[j + 1][i])); bm.faces.new((bot[j][i], bot[j + 1][i], bot[j + 1][i + 1], bot[j][i + 1]))
    for i in range(n):
        bm.faces.new((bot[0][i], bot[0][i + 1], top[0][i + 1], top[0][i])); bm.faces.new((top[m][i], top[m][i + 1], bot[m][i + 1], bot[m][i]))
    for j in range(m):
        bm.faces.new((bot[j][0], top[j][0], top[j + 1][0], bot[j + 1][0])); bm.faces.new((top[j][n], bot[j][n], bot[j + 1][n], top[j + 1][n]))
    return obj(name, bm, mat, smooth)
def ease(x): x = max(0.0, min(1.0, x)); return x * x * (3 - 2 * x)

# ---- the nape shield, finished: symmetric, a smooth arc peaking at the back, ending well behind the temple ports;
# a white rim over it; a framed opening round the connector ----
def shield(lo=ZB, peak=1.635, side=1.545, h=0.006, rim=True):
    zt = lambda p: side + (peak - side) * ease((math.cos(p - BK) - math.cos(1.25)) / (1 - math.cos(1.25)))
    a0, a1 = BK - 1.6, BK + 1.6
    patch2("shield", a0, a1, lo, zt, lambda p, t: h * ease(min(p - a0, a1 - p) / 0.12), GREY, n=80)
    if rim: patch2("shieldrim", a0 + 0.05, a1 - 0.05, zt, lambda p: zt(p) + 0.007, lambda p, t: 0.009 * ease(min(p - a0, a1 - p) / 0.15), WHITE, n=80, m=1)
    patch2("connframe", BK - 0.19, BK + 0.19, 1.540, 1.602, lambda p, t: 0.0085 if (t < 0.15 or t > 0.85 or abs(p - BK) > 0.15) else 0.0, DARK, n=24, m=10, smooth=False)

# ---- the cheekbones: a wedge on each lower side, thickest along its upper edge (the cheekbone line) and gone at the
# neck - from the front the helmet narrows down: a slight triangle ----
CK0, CK1 = math.radians(72), math.radians(128)       # from just behind the visor's lower corner back to below the ear
def cheek_z1(sg, top=1.615, back=1.53):
    return lambda p: top + (back - top) * ease((abs(p - FRONT) if sg < 0 else abs(p - FRONT)) / 1)
def cheeks(hmax=0.014, top=1.615, back=1.525, mat=None, rib=False, two=False, lo=ZB):
    for sg in (1, -1):
        p0, p1 = (FRONT + CK0, FRONT + CK1) if sg > 0 else (FRONT - CK1, FRONT - CK0)
        def z1(p, sg=sg): return top + (back - top) * ease((abs(p - FRONT) - CK0) / (CK1 - CK0))
        def hh(p, t, sg=sg, p0=p0, p1=p1): return hmax * t ** 1.4 * ease(min(p - p0, p1 - p) / 0.10)
        patch2("cheek%d" % sg, p0, p1, lo, z1, hh, mat or WHITE, n=40, m=10)
        if rib: patch2("cheekrib%d" % sg, p0 + 0.06, p1 - 0.08, lambda p, z1=z1: z1(p) - 0.006, z1, lambda p, t: hmax + 0.003, RED, n=40, m=1)
        if two: patch2("cheekfac%d" % sg, p0 + 0.08, p1 - 0.15, lo, lambda p, z1=z1: z1(p) - 0.03, lambda p, t: hmax * 0.6 * t * ease(min(p - p0 - 0.08, p1 - 0.15 - p) / 0.08), GREY, n=30, m=6)

def k1(): shield()
def k2(): cheeks()
def k3(): cheeks(rib=True)
def k4(): cheeks(hmax=0.018, top=1.62, back=1.51, two=True)
def k5(): cheeks(hmax=0.016, mat=GREY); shield(rim=False)
def k6(): cheeks(rib=True); shield()
def k7(): cheeks(hmax=0.020, top=1.625, back=1.50, rib=True)
VARS = [("0", "", None), ("1", "", k1), ("2", "", k2), ("3", "", k3), ("4", "", k4), ("5", "", k5), ("6", "", k6), ("7", "", k7)]
REST = {n: bpy.data.objects[n].data.copy() for n in CUT}
try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 520, 520; sc.render.film_transparent = True
    if sc.world is None: sc.world = bpy.data.worlds.new("HV_world")
    sc.world.use_nodes = True
    bg = sc.world.node_tree.nodes.get("Background")
    if bg: bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("HV_key", 3.0, (42, 0, 145)), ("HV_fill", 1.4, (65, 0, -40)), ("HV_front", 1.6, (55, 0, 20)), ("HV_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("HV_cam"); cam = bpy.data.objects.new("HV_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0, -0.01, 1.58))
    VIEWS = {"front": T + Vector((0.0, -1.1, 0.04)), "q34": T + Vector((-0.62, -0.95, 0.12)), "side": T + Vector((1.10, 0.0, 0.02)), "rear": T + Vector((0.55, 0.95, 0.15)), "back": T + Vector((0.0, 1.1, 0.06))}
    bpy.data.objects["HelmPods"].hide_render = True
    for key, name, fn in VARS:
        for n in CUT: bpy.data.objects[n].data = REST[n].copy()
        cut(W); glass(W); rim(W); hexport(1); hexport(-1)
        if fn: fn()
        for vn, loc in VIEWS.items():
            cd.lens = 50; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "hk_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
