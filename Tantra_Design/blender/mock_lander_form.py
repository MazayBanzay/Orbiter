# Lander of «Тантра» — FORM from entry physics (lifting body), preview renders only, nothing for the game.
# powershell -File run.ps1 mock_lander_form.py [shot,shot]   shots: hover, entry, stowed
# Blender axes: x forward (nose +x), y left, z up; metres; origin = CG.
# Body: lifting body 19 x 8.8 x 3.5 m, flat ceramic belly, blunt nose (iridium crystal cap, faceted), seamless top.
# Four pods (core V1: x +-4.6, lateral 3.55) sit in edge notches flush with the contour for entry/cruise and swing
# 90 deg down about a lateral axis for hover. Wings retract into the body (entry) and slide out for subsonic flight.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector, Matrix
HERE = r"C:\Games\Orbiter-2024\Tantra_Design\blender"; OUT = os.path.join(HERE, "renders")
LOG = os.path.join(HERE, "mock_lander_form.log"); log = []
def L(s):
    log.append(str(s)); open(LOG, "w", encoding="utf-8").write("\n".join(log))

# ---------------- shape parameters ----------------
LEN_A, LEN_F = -9.5, 9.5          # stern, nose tip
W = 4.4                           # max half width
XM = 4.0                          # nose ellipse starts here
NOTCH = [(3.0, 6.2), (-6.6, -3.4)]   # notch x ranges (front, rear)
NOTCH_D = 1.75                    # notch depth from the edge
POD_R, POD_L = 0.78, 3.0
CAP_X = 7.4                       # iridium cap from here to the tip

def half_w(x):
    if x > XM:
        u = (x - XM) / (LEN_F - XM); return W * max(0.0, 1 - u ** 2.4) ** (1 / 2.4)
    if x < -2.0:
        u = (-2.0 - x) / (-2.0 - LEN_A); return W - 0.45 * u ** 1.5
    return W

def z_bot(x):
    zb = -1.55
    if x > 3.0: zb += 0.55 * ((x - 3.0) / (LEN_F - 3.0)) ** 2
    if x < -7.5: zb += 0.25 * ((-7.5 - x) / 2.0) ** 2
    return zb

def z_top(x):
    return 1.95 - 0.012 * (x + 0.5) ** 2

def scale_nose(x):
    if x <= XM: return 1.0
    u = (x - XM) / (LEN_F - XM); return max(0.0, 1 - u ** 2.4) ** (1 / 2.4)

def section(x, n):
    w = half_w(x); zb = z_bot(x); zt = z_top(x); zc = zb + 0.32 * (zt - zb)
    s = scale_nose(x); zc_n = zc
    up = (zt - zc) * s; dn = (zc - zb) * s
    pts = []
    for j in range(n):
        t = 2 * math.pi * j / n; c, sn = math.cos(t), math.sin(t)
        y = w * math.copysign(abs(c) ** 0.55, c)
        z = zc_n + (up * abs(sn) ** 0.85 if sn >= 0 else -dn * abs(sn) ** 0.30)
        pts.append(Vector((x, y, z)))
    return pts

# ---------------- materials ----------------
MATS = {}
def principled(m): return next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
def setin(b, names, v):
    for nm in names:
        if nm in b.inputs: b.inputs[nm].default_value = v; return
def mat(name, col, rough=0.5, metal=0.0, emit=0.0, aniso=0.0, coat=0.0, noise=0.0):
    if name in MATS: return MATS[name]
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; b = principled(m)
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if aniso: setin(b, ("Anisotropic",), aniso)
    if coat: setin(b, ("Coat Weight", "Clearcoat"), coat); setin(b, ("Coat Roughness", "Clearcoat Roughness"), 0.15)
    if emit > 0:
        setin(b, ("Emission Color", "Emission"), (*col, 1)); setin(b, ("Emission Strength",), emit)
    if noise:
        tc = nt.nodes.new("ShaderNodeTexCoord"); nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = 3.0; nz.inputs["Detail"].default_value = 10
        nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
        mr = nt.nodes.new("ShaderNodeMapRange"); mr.inputs[3].default_value = rough - noise; mr.inputs[4].default_value = rough + noise
        nt.links.new(nz.outputs["Fac"], mr.inputs[0]); nt.links.new(mr.outputs[0], b.inputs["Roughness"])
    MATS[name] = m; return m

def glow_mat(name, col, strength, facing=0.35):
    if name in MATS: return MATS[name]
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL': nt.nodes.remove(n)
    out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
    em = nt.nodes.new("ShaderNodeEmission"); em.inputs[0].default_value = (*col, 1); em.inputs[1].default_value = strength
    tr = nt.nodes.new("ShaderNodeBsdfTransparent"); lw = nt.nodes.new("ShaderNodeLayerWeight"); lw.inputs[0].default_value = facing
    mx = nt.nodes.new("ShaderNodeMixShader")
    inv = nt.nodes.new("ShaderNodeMath"); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0
    nt.links.new(lw.outputs["Facing"], inv.inputs[1]); nt.links.new(inv.outputs[0], mx.inputs[0])
    nt.links.new(tr.outputs[0], mx.inputs[1]); nt.links.new(em.outputs[0], mx.inputs[2]); nt.links.new(mx.outputs[0], out.inputs[0])
    for a, v in (("surface_render_method", 'BLENDED'), ("blend_method", 'BLEND')):
        try: setattr(m, a, v)
        except Exception: pass
    MATS[name] = m; return m

def M_TOP(): return mat("top", (0.035, 0.037, 0.045), 0.32, 0.55, aniso=0.4, coat=0.5, noise=0.06)
def M_BELLY(): return mat("belly", (0.022, 0.020, 0.019), 0.85, 0.0, noise=0.08)
def M_IR(): return mat("iridium", (0.30, 0.27, 0.36), 0.18, 1.0, aniso=0.6)
def M_POD(): return mat("pod", (0.05, 0.05, 0.055), 0.38, 0.8, aniso=0.3, noise=0.05)
def M_COIL(): return mat("coil", (0.55, 0.33, 0.16), 0.30, 1.0)
def M_CUP(): return mat("cupin", (0.012, 0.012, 0.014), 0.6, 0.9)
def M_THROAT(): return mat("throat", (0.45, 0.85, 1.0), 0.3, 0.0, emit=25.0)
def M_GEAR(): return mat("gear", (0.10, 0.10, 0.11), 0.45, 0.9)
def M_LINE(): return mat("seam", (0.008, 0.008, 0.009), 0.7, 0.2)
def M_NAV(c): return mat("nav%.2f" % c[0], c, 0.3, 0.0, emit=12.0)

# ---------------- mesh helpers ----------------
def mkobj(name, V, F, m, smooth=True):
    me = bpy.data.meshes.new(name); me.from_pydata([tuple(v) for v in V], [], F)
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = smooth
    me.materials.append(m); o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); return o

def loft(rings, cap0=True, cap1=True):
    n = len(rings[0]); V = [p for r in rings for p in r]; F = []
    for i in range(len(rings) - 1):
        for j in range(n): F.append((i * n + j, i * n + (j + 1) % n, (i + 1) * n + (j + 1) % n, (i + 1) * n + j))
    if cap0: F.append(tuple(range(n))[::-1])
    if cap1: F.append(tuple(range((len(rings) - 1) * n, len(rings) * n)))
    return V, F

def lathe(prof, seg=40):
    """prof: (r, x) along local +x; returns V, F around x axis."""
    V = []; F = []
    for r, x in prof:
        for j in range(seg):
            t = 2 * math.pi * j / seg; V.append(Vector((x, r * math.cos(t), r * math.sin(t))))
    n = len(prof)
    for i in range(n - 1):
        for j in range(seg): F.append((i * seg + j, i * seg + (j + 1) % seg, (i + 1) * seg + (j + 1) % seg, (i + 1) * seg + j))
    return V, F

def boxobj(name, c, d, m):
    hx, hy, hz = d[0] / 2, d[1] / 2, d[2] / 2; c = Vector(c)
    V = [c + Vector((sx * hx, sy * hy, sz * hz)) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return mkobj(name, V, F, m, smooth=False)

def boolean_cut(o, cutters):
    for k, cu in enumerate(cutters):
        md = o.modifiers.new("cut%d" % k, 'BOOLEAN'); md.operation = 'DIFFERENCE'; md.object = cu
        try: md.solver = 'EXACT'
        except Exception: pass
    bpy.context.view_layer.objects.active = o
    for md in list(o.modifiers):
        try: bpy.ops.object.modifier_apply(modifier=md.name)
        except Exception as e: L("apply fail %s" % e)
    for cu in cutters: bpy.data.objects.remove(cu, do_unlink=True)

# ---------------- build ----------------
def body():
    xs_body = [LEN_A + (CAP_X - LEN_A) * (i / 46) ** 1.0 for i in range(47)]
    rings = [section(x, 64) for x in xs_body]
    V, F = loft(rings, cap0=True, cap1=True)
    top = mkobj("Body", V, F, M_TOP())
    top.data.materials.append(M_BELLY())
    for p in top.data.polygons:   # belly material on downward faces
        if p.normal.z < -0.35: p.material_index = 1
    # notches (open top and bottom, flush pockets for the pods)
    cut = []
    for (xa, xb) in NOTCH:
        for sgn in (1, -1):
            yc = sgn * (W + 0.5 - NOTCH_D / 2 - 0.25)
            cut.append(boxobj("notch", ((xa + xb) / 2, yc, 0.2), (xb - xa, NOTCH_D + 1.0, 8.0), M_TOP()))
    boolean_cut(top, cut)
    # iridium cap: faceted crystal, blunt (coarse rings, flat shading)
    xs_cap = [CAP_X + (LEN_F - CAP_X) * (1 - (1 - i / 7) ** 1.6) for i in range(8)]
    rings = [section(x, 16) for x in xs_cap]
    rings[-1] = [Vector((LEN_F, 0, (z_bot(LEN_F) + 0.32 * (z_top(LEN_F) - z_bot(LEN_F)))))] * 16
    V, F = loft(rings, cap0=False, cap1=False)
    cap = mkobj("Cap", V, F, M_IR(), smooth=False)
    return top, cap

def pod(name, xa, xb, side, mode):
    """Pod lies along x in its notch (cruise/entry); hover: swung 90 deg down about a lateral axis near its front."""
    V, F = lathe([(0.0, 0.0), (0.45, -0.12), (0.70, -0.35), POD_R and (POD_R, -0.7), (POD_R, -2.35), (0.84, -2.75), (0.86, -3.0)], 48)
    shell = mkobj(name + "_shell", V, F, M_POD())
    V, F = lathe([(0.80, -2.98), (0.62, -2.85), (0.40, -2.55)], 48); cup = mkobj(name + "_cup", V, F, M_CUP())
    V, F = lathe([(0.39, -2.56), (0.0, -2.56)], 32); thr = mkobj(name + "_throat", V, F, M_THROAT() if mode == "hover" else M_CUP())
    tor = bpy.data.objects.new(name + "_coil", None)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.74, minor_radius=0.07, location=(-2.9, 0, 0), rotation=(0, math.pi / 2, 0))
    coil = bpy.context.active_object; coil.name = name + "_coil"; coil.data.materials.append(M_COIL())
    parts = [shell, cup, thr, coil]
    yc = side * (W + 0.5 - NOTCH_D / 2 - 0.25 - 0.1); zc = 0.0
    piv = Vector((xb - 0.85, yc, zc))
    base = Matrix.Translation(Vector((xb - 0.15, yc, zc)))
    rot = Matrix.Identity(4)
    if mode == "hover":
        rot = Matrix.Translation(piv) @ Matrix.Rotation(math.radians(-90), 4, 'Y') @ Matrix.Translation(-piv)
    for o in parts: o.matrix_world = rot @ base @ o.matrix_world
    if mode == "hover":   # plasma jet
        tip = rot @ base @ Vector((-3.0, 0, 0)); jet(tip, Vector((0, 0, -1)))
    return parts

def jet(origin, d, length=7.0):
    V, F = lathe([(0.36, 0.0), (0.42, -1.0), (0.55, -3.5), (0.75, -length)], 32)
    o = mkobj("jet", V, F, glow_mat("plasma", (0.45, 0.80, 1.0), 9.0, 0.30))
    q = Vector((-1, 0, 0)).rotation_difference(d)
    o.matrix_world = Matrix.Translation(origin) @ q.to_matrix().to_4x4()

def wing(side, mode):
    if mode != "hover": return None   # retracted into the body for entry, cruise-in-space and stowage
    xr0, xr1 = 2.6, -2.9; span = 4.6; sweep = math.radians(18); tip_c = 2.6
    zt, t = -0.55, 0.20
    root = W - 0.6
    pts_t = []; pts_b = []
    for k in range(13):
        u = k / 12; y = side * (root + span * u)
        le = xr0 - span * u * math.tan(sweep); c = (xr0 - xr1) * (1 - u) + tip_c * u
        th = t * (1 - 0.5 * u)
        pts_t.append((le, y, c, th))
    V = []; F = []; ns = 14
    for (le, y, c, th) in pts_t:
        for j in range(ns):
            a = 2 * math.pi * j / ns; xx = le - c * (1 - math.cos(a)) / 2
            zz = zt + th * 0.5 * math.sin(a) * (1.0 if math.sin(a) > 0 else 0.6)
            V.append(Vector((xx, y, zz)))
    for i in range(len(pts_t) - 1):
        for j in range(ns): F.append((i * ns + j, i * ns + (j + 1) % ns, (i + 1) * ns + (j + 1) % ns, (i + 1) * ns + j))
    F.append(tuple(range((len(pts_t) - 1) * ns, len(pts_t) * ns)))
    o = mkobj("Wing", V, F, M_TOP()); return o

def gear(mode, ground_z):
    if mode not in ("hover", "ground"): return
    for (x, y) in ((6.8, 0.0), (-2.0, 2.6), (-2.0, -2.6), (-7.5, 0.0)):
        zb = z_bot(x); bot = ground_z + 0.12
        V, F = lathe([(0.12, 0.0), (0.12, -(zb - bot))], 16); o = mkobj("leg", V, F, M_GEAR())
        o.matrix_world = Matrix.Translation(Vector((x, y, zb))) @ Matrix.Rotation(math.radians(-90), 4, 'Y')
        boxobj("pad", (x, y, bot - 0.04), (0.9, 0.9, 0.12), M_GEAR())

def nav_lights():
    for (x, y, c) in ((0.0, W - 0.05, (0.1, 1.0, 0.25)), (0.0, -(W - 0.05), (1.0, 0.1, 0.08))):
        V, F = lathe([(0.0, 0.06), (0.06, 0.0), (0.0, -0.06)], 12); o = mkobj("nav", V, F, M_NAV(c))
        o.location = (x, y, z_bot(x) + 0.32 * (z_top(x) - z_bot(x)))

def sheath():
    """Entry plasma under the belly (shock layer), seen as a glowing envelope."""
    xs = [LEN_A + (LEN_F + 0.4 - LEN_A) * i / 30 for i in range(31)]
    rings = []
    for x in xs:
        xx = min(x, LEN_F - 0.02); w = half_w(xx) + 0.45; zb = z_bot(xx) - 0.55; zc = zb + 0.9
        ring = []
        for j in range(32):
            t = math.pi + math.pi * j / 31
            ring.append(Vector((x, w * math.cos(t), zc + (zc - zb) * math.sin(t))))
        rings.append(ring)
    V, F = loft(rings, cap0=False, cap1=False)
    F = [f for f in F]
    mkobj("Sheath", V, F, glow_mat("sheath", (1.0, 0.42, 0.12), 6.0, 0.25))

def hangar_outline():
    m = mat("hangar", (0.9, 0.75, 0.2), 0.5, 0.0, emit=2.0)
    hl, hw = 20.2 / 2, 16.0 / 2
    for (c, d) in (((0, hw, -1.6), (20.2, 0.06, 0.06)), ((0, -hw, -1.6), (20.2, 0.06, 0.06)),
                   ((hl, 0, -1.6), (0.06, 16.0, 0.06)), ((-hl, 0, -1.6), (0.06, 16.0, 0.06))):
        boxobj("hangar", c, d, m)

# ---------------- stage / camera ----------------
def clear():
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)

def stage(ground_z=None, space=True):
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1800, 1050
    try: sc.eevee.taa_render_samples = 64
    except Exception: pass
    for a in ("use_gtao", "use_shadows", "use_bloom", "use_raytracing"):
        try: setattr(sc.eevee, a, True)
        except Exception: pass
    try: sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
    except Exception: pass
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True; nt = world.node_tree
    bg = next(n for n in nt.nodes if n.type == "BACKGROUND")
    if space: bg.inputs[0].default_value = (0.004, 0.005, 0.009, 1); bg.inputs[1].default_value = 1.0
    else: bg.inputs[0].default_value = (0.36, 0.42, 0.52, 1); bg.inputs[1].default_value = 0.8
    def sun(name, d, e, ang=0.5, col=(1, 1, 1)):
        l = bpy.data.lights.new(name, 'SUN'); l.energy = e; l.angle = math.radians(ang); l.color = col
        o = bpy.data.objects.new(name, l); sc.collection.objects.link(o); o.rotation_mode = 'QUATERNION'
        o.rotation_quaternion = (-Vector(d).normalized()).to_track_quat('-Z', 'Y')
    sun("Key", (0.35, 0.55, 0.75), 6.0, col=(1, 0.96, 0.9))
    sun("Rim", (-0.8, -0.4, 0.35), 2.5, 2, (0.6, 0.75, 1.0))
    sun("Fill", (0.2, -0.7, -0.4), 0.25, 10, (0.6, 0.7, 1.0))
    if ground_z is not None:
        me = bpy.data.meshes.new("G"); R = 400
        me.from_pydata([(-R, -R, ground_z), (R, -R, ground_z), (R, R, ground_z), (-R, R, ground_z)], [], [(0, 1, 2, 3)])
        g = bpy.data.objects.new("Ground", me); sc.collection.objects.link(g)
        me.materials.append(mat("ground", (0.30, 0.25, 0.21), 0.95, noise=0.05))
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")); sc.collection.objects.link(cam); sc.camera = cam
    cam.data.clip_end = 3000; return cam

def aim(cam, loc, tgt, lens):
    cam.location = Vector(loc); cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')

def render(name):
    sc = bpy.context.scene; sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); L("rendered " + name)

def build(mode, pitch_deg=0.0, ground_z=None):
    objs_before = set(bpy.data.objects)
    body(); nav_lights()
    for (xa, xb) in NOTCH:
        for sd in (1, -1): pod("pod", xa, xb, sd, mode)
    for sd in (1, -1): wing(sd, mode)
    if ground_z is not None: gear("hover", ground_z)
    if mode == "entry": sheath()
    if pitch_deg:
        R = Matrix.Rotation(math.radians(pitch_deg), 4, 'Y')
        for o in bpy.data.objects:
            if o not in objs_before and o.type == 'MESH': o.matrix_world = R @ o.matrix_world

SHOTS = {
    "lander_form_hover": dict(mode="hover", ground=-7.5, space=False, cam=((17, 15, 1.5), (0.5, 0, -1.5), 32)),
    "lander_form_entry": dict(mode="entry", pitch=-38.0, ground=None, space=True, cam=((2, 30, -4), (0, 0, -0.5), 40)),
    "lander_form_stowed": dict(mode="stowed", ground=None, space=True, hangar=True, cam=((3, 1, 40), (0, 0, 0), 45)),
    "lander_form_front": dict(mode="hover", ground=-7.5, space=False, cam=((26, -6, -2.5), (0, 0, -1.5), 45)),
}

try:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    names = argv or list(SHOTS)
    L("start " + ",".join(names))
    for nm in names:
        s = SHOTS[nm]; clear()
        cam = stage(s.get("ground"), s.get("space", True))
        build(s["mode"], s.get("pitch", 0.0), s.get("ground"))
        if s.get("hangar"): hangar_outline()
        aim(cam, *s["cam"]); render(nm)
    L("done")
except Exception:
    L(traceback.format_exc())
