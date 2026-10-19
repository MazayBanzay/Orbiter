# «Наплечный» M2 with a main (cruise) engine, six variants (the user, 2026-10-17: «задний - как маршевый, не уступающий
# 2м УВТ, и переработка ранца по форме; 2-я версия, первая - моя»). Only the M1 pack's own parts, scaled and copied:
# the main engine - a JetPod copy (its egg, red ring, ring nozzle with the plug) scaled up; its swivel drum - a JetHinge
# copy; the engine bay - a JetShell copy, low; the tank - the shell itself, deeper or taller.
#   blender --background astronavigator_suit.blend --python jetpack_m2main.py -> renders\m2m_<n>_<rear|side>.png
import bpy, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "m2m_log.txt"), "w", encoding="utf-8")
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

def v1():   # «Корма»: a pure cruise engine, aft, in a bay under a deeper tank; the capsules as they are
    back, base = tank(1.25); b = bay(back, base)
    engine((0, back - 0.06, b + 0.085), 1.45, 90)
def v2():   # «Харриер»: one swivel engine under the pack - down to hover with the capsules, aft to cruise (shown at 45)
    back, base = tank(1.25)
    drum((0, back - 0.03, base - 0.03), 2.2)
    engine((0, back + 0.03, base - 0.15), 1.45, 45)
def v3():   # «Хребет»: the main engine along her spine, its jet down and 30 deg aft - she leans forward to cruise
    back, base = tank(1.2, 1.1)
    engine((0, back + 0.02, base - 0.06), 1.55, 30)
def v4():   # «Пара»: two swivel engines at the base's corners (one can fail), shown at 60
    back, base = tank(1.25)
    for x in (0.13, -0.13):
        drum((x, back - 0.03, base - 0.025), 1.6)
        engine((x, back + 0.03, base - 0.12), 1.15, 60)
def v5():   # «Высокий бак»: the shell taller (down), the engine half sunk in its base, aft
    back, base = tank(1.1, 1.3)
    engine((0, back - 0.02, base + 0.06), 1.45, 90)
def v6():   # «Киль»: a big engine on the back of the shell along it, on a drum at its middle - down to hover, aft to cruise
    back, base = tank(1.15)
    drum((0, back + 0.035, (TOP_Z + base) / 2), 2.2)
    engine((0, back + 0.075, (TOP_Z + base) / 2 - 0.02), 1.65, 0)
VARS = [("1", v1), ("2", v2), ("3", v3), ("4", v4), ("5", v5), ("6", v6)]


# ---- the finalists (the user picks from 2, 4, 6), refined 2026-10-17: the tank x1.35 for all (~24 kg of МВ), the main
# engine turns about its drum's axis - down to hover with the capsules, aft to cruise - shown in both states ----
def on_drum(dr, k, ang, L):
    """the engine hanging L below the drum's axis, swung about it by ang (0 down, 90 its exit aft)"""
    R = Matrix.Rotation(math.radians(ang), 4, 'X')
    return engine(Vector(dr) + (R @ Vector((0, 0, -L, 0))).to_3d(), k, ang)
def f2(ang):
    back, base = tank(1.35); dr = (0, back - 0.02, base - 0.035)
    drum(dr, 2.2); on_drum(dr, 1.45, ang, 0.17)
def f4(ang):
    back, base = tank(1.35)
    for x in (0.13, -0.13):
        dr = (x, back - 0.02, base - 0.03); drum(dr, 1.6); on_drum(dr, 1.15, ang, 0.135)
def f6(ang):
    back, base = tank(1.35); dr = (0, back + 0.075, (TOP_Z + base) / 2)
    drum((0, back + 0.035, dr[2]), 2.2); on_drum(dr, 1.65, ang, 0.0)
FINAL = [("2", f2), ("4", f4), ("6", f6)]
if os.environ.get("M2F"):
    VARS = [("F%s_%s" % (k, st), (lambda f=f, a=a: f(a))) for k, f in FINAL for st, a in (("hover", 0), ("cruise", 90))]

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
            sc.render.filepath = os.path.join(OUT, "m2m_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in extra: bpy.data.objects.remove(o, do_unlink=True)
        extra.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
