# The jet pack's capsule, its nozzle worked through (the user, 2026-10-18: «сопла доработать тщательно, пока не похожи на
# реальные - и раскал внутри»). The twin-chamber capsule of tools/jetpack_m1_capsule.py: one annular plug nozzle, its throat
# ring split by two struts into the two chambers' arcs; a thin rhenium cowl lip converging to the throat; a truncated plug of
# HfB2-SiC with a cBN tip ring, its concave (isentropic) contour, a flat base with the bleed holes. The glow: blackbody
# colours by the wall temperatures - the throat ~1800 K, the plug's tip ~1100 K, the lip's inside ~1500 K.
#   blender --background astronavigator_suit.blend --python capsule_nozzle.py -> renders\cn_<state>_<view>.png
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "cn_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
for o in bpy.data.objects: o.hide_render = True
parts = []

def mat(name, col, metal, rough, temp=None, strength=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
    if temp:
        bb = nt.nodes.new("ShaderNodeBlackbody"); bb.inputs[0].default_value = temp
        key = "Emission Color" if "Emission Color" in b.inputs else "Emission"
        nt.links.new(bb.outputs[0], b.inputs[key])
        if "Emission Strength" in b.inputs: b.inputs["Emission Strength"].default_value = strength
    return m

def gmat(name, col, metal, rough, k, tref=1850.0):
    """a glowing material by the per-vertex temperature T (K): the colour by the blackbody, the strength ~ (T/tref)^4"""
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
    at = nt.nodes.new("ShaderNodeAttribute"); at.attribute_name = "T"
    bb = nt.nodes.new("ShaderNodeBlackbody"); nt.links.new(at.outputs["Fac"], bb.inputs[0])
    d = nt.nodes.new("ShaderNodeMath"); d.operation = 'DIVIDE'; d.inputs[1].default_value = tref; nt.links.new(at.outputs["Fac"], d.inputs[0])
    pw = nt.nodes.new("ShaderNodeMath"); pw.operation = 'POWER'; pw.inputs[1].default_value = 4.0; nt.links.new(d.outputs[0], pw.inputs[0])
    mu = nt.nodes.new("ShaderNodeMath"); mu.operation = 'MULTIPLY'; mu.inputs[1].default_value = k; nt.links.new(pw.outputs[0], mu.inputs[0])
    key = "Emission Color" if "Emission Color" in b.inputs else "Emission"
    nt.links.new(bb.outputs[0], b.inputs[key]); nt.links.new(mu.outputs[0], b.inputs["Emission Strength"])
    return m

def set_T(o, temps):
    """the per-vertex temperature, ring by ring (the lathe's order)"""
    me = o.data; n = len(me.vertices) // len(temps)
    a = me.attributes.new("T", 'FLOAT', 'POINT')
    for i in range(len(me.vertices)): a.data[i].value = temps[min(i // n, len(temps) - 1)]

def obj(name, bm, m, smooth=True):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    for p in me.polygons: p.use_smooth = smooth
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); parts.append(o); return o

def lathe(name, prof, m, nu=64, a0=0.0, a1=360.0, smooth=True, nseg=8):
    """revolve (r, z) about the z axis from a0 to a1 deg; nseg > 0 faceted (the capsule's octagon), 0 round"""
    bm = bmesh.new(); full = abs(a1 - a0) >= 359.9; n = nu if full else max(4, int(nu * abs(a1 - a0) / 360))
    rings = []
    for r, z in prof:
        ring = []
        for j in range(n + (0 if full else 1)):
            a = math.radians(a0 + (a1 - a0) * j / n)
            k = 1.0
            if nseg: seg = 2 * math.pi / nseg; k = math.cos(seg / 2) / math.cos((a - seg / 2) % seg - seg / 2)
            ring.append(bm.verts.new((r * k * math.cos(a), r * k * math.sin(a), z)))
        rings.append(ring)
    m_ = len(rings[0])
    for A, B in zip(rings, rings[1:]):
        for j in range(m_ if full else m_ - 1):
            k = (j + 1) % m_
            try: bm.faces.new((A[j], A[k], B[k], B[j]))
            except ValueError: pass
    return obj(name, bm, m, smooth)

def box(name, c, s, m, rz=0.0):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ Matrix.Rotation(rz, 4, 'Z') @ Matrix.Diagonal((*s, 1)), verts=bm.verts)
    return obj(name, bm, m, False)

R, Hh = 0.047, 0.118                                    # the capsule's radius, half height (M1's pod)
PROF = [(0.0, 0.5), (0.45, 0.47), (0.75, 0.38), (0.92, 0.24), (1.0, 0.08), (1.0, -0.12), (0.97, -0.30), (0.90, -0.42), (0.86, -0.47)]
ZE = -Hh                                                 # the exit plane
R_LIP_O, R_LIP_I, R_PLUG = 0.0405, 0.0300, 0.0225      # the cowl's outer and inner lip, the plug at the exit plane
L_PLUG, R_TIP = 0.046, 0.0075                          # the plug's length beyond the exit, its truncated tip

def build(state, cut=False):
    hot = state != "off"
    T = {"nominal": (1850, 1500, 1150, 1.0), "forsazh": (1600, 1350, 1050, 0.6), "off": (0, 0, 0, 0)}[state]
    WHITE = mat("white", (0.88, 0.89, 0.90), 0.05, 0.30)
    RED = mat("red", (0.78, 0.09, 0.07), 0.1, 0.35)
    SEAM = mat("seam", (0.20, 0.21, 0.23), 0.4, 0.4)
    RHEN = mat("rhenium", (0.55, 0.56, 0.58), 1.0, 0.32)
    a0, a1 = (0.0, 270.0) if cut else (0.0, 360.0)
    # the body: the faceted egg down to the cowl
    lathe("body", [(r * R, z * Hh * 2) for r, z in PROF[:-1]] + [(0.86 * R, -0.47 * Hh * 2)], WHITE, 64, a0, a1, True, 8)
    lathe("band", [(R * 1.03, 0.012), (R * 1.03, -0.012)], RED, 64, a0, a1, True, 8)
    # the cowl: from the body into a thin rhenium lip; its inside converging to the throat
    lathe("cowl", [(0.86 * R, -0.47 * Hh * 2), (R_LIP_O, ZE + 0.004), (R_LIP_O - 0.001, ZE)], RHEN, 64, a0, a1, True, 0)
    lip_in = [(R_LIP_O - 0.001, ZE), (R_LIP_I + 0.0015, ZE - 0.0005), (R_LIP_I, ZE + 0.003), (R_LIP_I + 0.002, ZE + 0.012), (0.034, ZE + 0.026)]
    if hot:
        o = lathe("lipin", lip_in, gmat("lipin", (0.40, 0.40, 0.42), 1.0, 0.30, 2.2 * T[3]), 64, a0, a1, True, 0)
        set_T(o, [T[1] - 450, T[1] - 250, T[1], T[1] + 80, T[1] + 150])
    else: lathe("lipin", lip_in, mat("lipin", (0.40, 0.40, 0.42), 1.0, 0.30), 64, a0, a1, True, 0)
    # the plug: hidden root in the chamber ring, the concave contour out to the truncated tip
    pts = []
    for i in range(13):
        t = i / 12; z = ZE + 0.012 - (L_PLUG + 0.012) * t
        r = R_TIP + (R_PLUG + 0.002 - R_TIP) * (1 - t) ** 1.7
        pts.append((r, z))
    if hot:
        o = lathe("plug", pts, gmat("plug", (0.10, 0.10, 0.11), 0.3, 0.45, 2.6 * T[3]), 64, a0, a1, True, 0)
        set_T(o, [T[0] - (T[0] - T[2]) * (i / 12) ** 0.8 for i in range(13)])
    else: lathe("plug", pts, mat("plug", (0.10, 0.10, 0.11), 0.3, 0.45), 64, a0, a1, True, 0)
    tipz = pts[-1][1]
    lathe("tipring", [(R_TIP + 0.0006, tipz + 0.004), (R_TIP + 0.0006, tipz)], mat("cbn", (0.45, 0.38, 0.30), 0.2, 0.25, (T[2] - 100) or None, T[3] * 0.4), 32, a0, a1, True, 0)
    lathe("base", [(R_TIP, tipz), (0.0, tipz)], mat("base", (0.08, 0.08, 0.09), 0.2, 0.6), 32, a0, a1, True, 0)
    for k in range(6):                                  # the base bleed holes
        a = math.radians(30 + 60 * k)
        if cut and not (math.radians(a0) <= a <= math.radians(a1)): continue
        lathe("bleed%d" % k, [(0.0014, tipz - 0.0002), (0.0, tipz - 0.0002)], mat("hole", (0.0, 0.0, 0.0), 0.0, 1.0, (T[2] + 200) or None, T[3] * 0.6), 8, 0, 360, False, 0)
        parts[-1].location = (0.0042 * math.cos(a), 0.0042 * math.sin(a), 0)
    # the throat ring between the lip and the plug: dark, glowing deep inside; two struts split it into the chambers' arcs
    lathe("throat", [(R_LIP_I, ZE + 0.010), (R_PLUG + 0.002, ZE + 0.010)], mat("throat", (0.02, 0.02, 0.02), 0.0, 0.9, (T[0] + 250) or None, T[3] * 4.0), 64, a0, a1, False, 0)
    for k in range(2):
        a = math.pi / 2 + k * math.pi
        if cut and k == 1: pass
        box("strut%d" % k, ((R_LIP_I + R_PLUG) / 2 * math.cos(a), (R_LIP_I + R_PLUG) / 2 * math.sin(a), ZE + 0.004), (R_LIP_I - R_PLUG + 0.002, 0.0022, 0.012), RHEN, a)
    # the plug's cooling: eight fine grooves along the hot zone (the hydrogen bled through them)
    for k in range(8):
        a = math.radians(22.5 + 45 * k)
        if cut and math.degrees(a) > a1: continue
        g = []
        for i in range(0, 9):
            r, z = pts[i]; g.append(Vector(((r + 0.0002) * math.cos(a), (r + 0.0002) * math.sin(a), z)))
        bm = bmesh.new(); vs = []
        for p in g:
            t = Vector((-math.sin(a), math.cos(a), 0)) * 0.0006
            vs.append((bm.verts.new(p - t), bm.verts.new(p + t)))
        for (a_, b_), (c_, d_) in zip(vs, vs[1:]): bm.faces.new((a_, b_, d_, c_))
        obj("groove%d" % k, bm, mat("groove", (0.05, 0.05, 0.05), 0.0, 0.8), False)
    if cut:   # the section's faces: the chambers' walls in the cut plane, simple
        pass

STATES = [("off", "холодное"), ("nominal", "номинал, вакуум"), ("forsazh", "форсаж, воздух")]
try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    try: sc.eevee.use_bloom = True
    except Exception: pass
    sc.view_settings.view_transform = "Standard"; sc.view_settings.look = "None"; sc.view_settings.exposure = 0.0
    sc.render.resolution_x, sc.render.resolution_y = 640, 640; sc.render.film_transparent = False
    if sc.world is None: sc.world = bpy.data.worlds.new("CN_world")
    sc.world.use_nodes = True
    bg = sc.world.node_tree.nodes.get("Background")
    if bg: bg.inputs[0].default_value = (0.05, 0.055, 0.06, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("CN_key", 3.0, (42, 0, 145)), ("CN_fill", 1.0, (65, 0, -40)), ("CN_low", 1.6, (150, 0, 30))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.3; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("CN_cam"); cam = bpy.data.objects.new("CN_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T0 = Vector((0, 0, -0.10))
    VIEWS = {"under": (T0 + Vector((0.10, -0.12, -0.17)), 55), "below": (Vector((0.0, 0.0, -0.40)), 70), "side": (Vector((0.26, -0.06, -0.15)), 60)}
    for st, name in STATES:
        for cut in (False, True):
            if cut and st == "forsazh": continue
            build(st, cut)
            views = {"cut": (Vector((0.16, -0.22, -0.12)), 60)} if cut else VIEWS
            for vn, (loc, lens) in views.items():
                tgt = Vector((0, 0, -0.13))
                if vn == "below": tgt = Vector((0, 0, -0.12))
                cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'
                cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
                sc.render.filepath = os.path.join(OUT, "cn_%s_%s.png" % (st, vn)); bpy.ops.render.render(write_still=True)
                print("rendered", st, vn, file=LOG); LOG.flush()
            for o in parts: bpy.data.objects.remove(o, do_unlink=True)
            parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
