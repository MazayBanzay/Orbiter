# «Наплечный» M2 after the user's references (UMMU turbopack, Off the Grid packs): five variants of the M1 parts.
#   blender --background astronavigator_suit.blend --python jetpack_m2ref.py -> renders\m2r_<R1..R5>_<rear|side>.png
import bpy, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "m2r_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
NAMES = ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR", "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR")
J = {n: bpy.data.objects[n] for n in NAMES}
REST = {n: o.matrix_world.copy() for n, o in J.items()}
SHELL = ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling")
FRONT_Y, TOP_Z, BOT_Z, BACK_Y = 0.108, 1.523, 1.048, 0.326
POD_C = Vector((0.597, 0.175, 1.471)); POD_H = 0.236
HINGE_C = Vector((0.204, 0.186, 1.4685))
extra = []

def copy(name, M):
    o = J[name].copy(); o.data = J[name].data; sc.collection.objects.link(o); extra.append(o)
    o.matrix_world = M @ REST[name]; return o

def tank(kd=1.0, kh=1.0):
    S = Matrix.Translation(Vector((0, FRONT_Y, TOP_Z))) @ Matrix.Diagonal((1, kd, kh, 1)) @ Matrix.Translation(Vector((0, -FRONT_Y, -TOP_Z)))
    for n in SHELL: J[n].matrix_world = S @ REST[n]
    return FRONT_Y + (BACK_Y - FRONT_Y) * kd, TOP_Z - (TOP_Z - BOT_Z) * kh   # the new back face, the new base

def engine(at, k, pitch_deg=0.0, yaw_deg=0.0):
    """the main engine: JetPodR scaled by k, its centre at `at`, its exit turned from down by pitch (+ aft) about x"""
    return copy("JetPodR", Matrix.Translation(Vector(at)) @ Matrix.Rotation(math.radians(yaw_deg), 4, 'Z') @ Matrix.Rotation(math.radians(pitch_deg), 4, 'X')
                @ Matrix.Scale(k, 4) @ Matrix.Translation(-POD_C))

def drum(at, k, axis='X'):
    """a swivel drum: JetHinge copy scaled, its axis across her back"""
    R = Matrix.Rotation(math.radians(90), 4, 'Y') if axis == 'X' else Matrix.Identity(4)
    return copy("JetHingeR", Matrix.Translation(Vector(at)) @ R @ Matrix.Scale(k, 4) @ Matrix.Translation(-HINGE_C))

def bay(back, base, h=0.17, kx=0.86):
    """the engine bay under the tank: the shell's own form, low"""
    sx, sz = kx, h / (TOP_Z - BOT_Z)
    M = Matrix.Translation(Vector((0, 0, base - h))) @ Matrix.Diagonal((sx, (back - FRONT_Y) / (BACK_Y - FRONT_Y), sz, 1)) @ Matrix.Translation(Vector((0, 0, -BOT_Z)))
    for n in ("JetShell",): copy(n, M)
    return base - h

def reset():
    for n, o in J.items(): o.matrix_world = REST[n].copy()


# ==== M2 after the user's references (2026-10-17: the UMMU turbopack - its tanks in a cage; Off the Grid packs - a
# swappable module up top, the propulsion in a rounded lumbar block, the jets out of its base). Still the M1 parts:
# JetShell copies make the lumbar block and the hip shells, JetPod copies the main jets, JetVessel copies the tanks ====
import bmesh
SHELL_C = Vector((0.0, 0.217, 1.2855)); VESSEL_C = Vector((0.0, 0.278, 1.200))
BAR = bpy.data.materials.new("M2R_bar"); BAR.use_nodes = True
_b = next(n for n in BAR.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'); _b.inputs["Base Color"].default_value = (0.20, 0.21, 0.23, 1); _b.inputs["Metallic"].default_value = 0.8; _b.inputs["Roughness"].default_value = 0.35

def shellpiece(at, s, yaw=0.0, pitch=0.0):
    return copy("JetShell", Matrix.Translation(Vector(at)) @ Matrix.Rotation(math.radians(yaw), 4, 'Z') @ Matrix.Rotation(math.radians(pitch), 4, 'X')
                @ Matrix.Diagonal((*s, 1)) @ Matrix.Translation(-SHELL_C))

def vessel(at, k_len, k_r, vertical=True):
    R = Matrix.Rotation(math.radians(90), 4, 'Y') if vertical else Matrix.Identity(4)
    return copy("JetVessel", Matrix.Translation(Vector(at)) @ R @ Matrix.Diagonal((k_len, k_r, k_r, 1)) @ Matrix.Translation(-VESSEL_C))

def bar(a, b, r=0.009):
    a, b = Vector(a), Vector(b); bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=r, radius2=r, depth=(b - a).length)
    R = Vector((0, 0, 1)).rotation_difference((b - a).normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation((a + b) / 2) @ R, verts=bm.verts)
    me = bpy.data.meshes.new("bar"); bm.to_mesh(me); bm.free(); me.materials.append(BAR)
    o = bpy.data.objects.new("bar", me); sc.collection.objects.link(o); extra.append(o); return o

def lumbar(back, w=1.05, d=1.25, h=0.40, z=0.93):
    """the lumbar propulsion block: the shell's own form, low and wide, its face against her lower back"""
    shellpiece((0, FRONT_Y + (SHELL_C.y - FRONT_Y) * d + 0.01, z), (w, d, h))
    return z - (TOP_Z - BOT_Z) * h / 2                                  # its base

def r1():   # «Поясной маршевый»: the tank deeper, the lumbar block under it, one big jet out of its base, down-back 20
    back, base = tank(1.2); b = lumbar(back)
    engine((0, 0.30, b + 0.02), 1.5, 20)
def r2():   # «Почки»: two hip shells hugging her sides, a jet in each, down and a little out and back
    back, base = tank(1.25)
    for sg in (1, -1):
        shellpiece((sg * 0.165, 0.235, 0.93), (0.42, 1.05, 0.48), yaw=-sg * 18)
        engine((sg * 0.19, 0.29, 0.82), 1.2, 15, yaw_deg=0)
def r3():   # «Каркас-баки» (UMMU): the tank volume as two upright vessels behind the core in a cage; the lumbar jet below
    back, base = tank(1.0)
    for x in (0.105, -0.105): vessel((x, 0.43, 1.30), 1.45, 1.35)
    xs, ys, zs = (-0.215, 0.215), (0.33, 0.53), (1.05, 1.55)
    for x in xs:
        for y in ys: bar((x, y, zs[0]), (x, y, zs[1]))
        for z in zs: bar((x, ys[0], z), (x, ys[1], z))
    for y in ys:
        for z in zs: bar((xs[0], y, z), (xs[1], y, z))
    b = lumbar(back, w=0.95, d=1.1, h=0.34)
    engine((0, 0.29, b + 0.02), 1.4, 20)
def r4():   # «Тройной»: the lumbar block with a big central jet and two small ones at its corners (one may fail)
    back, base = tank(1.2); b = lumbar(back, w=1.12)
    engine((0, 0.30, b + 0.02), 1.3, 20)
    for sg in (1, -1): engine((sg * 0.17, 0.27, b + 0.03), 0.95, 15)
def r5():   # «Модуль сверху»: the propulsion is the M1 core with the hip shells (as r2); a swappable module on its back
    back, base = tank(1.0)
    shellpiece((0, 0.40, 1.36), (0.80, 0.85, 0.42))                   # the module (cargo / medical / tank), on the core's back
    for sg in (1, -1):
        shellpiece((sg * 0.165, 0.235, 0.93), (0.42, 1.05, 0.48), yaw=-sg * 18)
        engine((sg * 0.19, 0.29, 0.82), 1.2, 15)
VARS = [("R1", r1), ("R2", r2), ("R3", r3), ("R4", r4), ("R5", r5)]

try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 720, 720; sc.render.film_transparent = True
    for nm, en, rot in (("MM_key", 3.0, (42, 0, 145)), ("MM_fill", 1.3, (65, 0, -40)), ("MM_low", 0.8, (120, 0, 160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("MM_cam"); cam = bpy.data.objects.new("MM_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    VIEWS = {"rear": (Vector((1.25, 2.0, 0.85)), Vector((0, 0.25, 1.22)), 'PERSP'), "side": (Vector((3.5, 0.32, 1.22)), Vector((0, 0.32, 1.22)), 'ORTHO')}
    for key, fn in VARS:
        reset(); fn()
        for vn, (loc, tgt, typ) in VIEWS.items():
            cd.type = typ; cd.ortho_scale = 1.25; cd.lens = 42
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "m2r_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in extra: bpy.data.objects.remove(o, do_unlink=True)
        extra.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
