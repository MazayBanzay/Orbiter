# The user's own helmet, reshaped (2026-10-18: «даже наш неплох, просто чуть форму надо сменить - затылок и т.д.»).
# The helmet's own mesh, visor and parts; only the shell's form is changed - the back of the head and the sides behind the
# visor. Nothing in front of the visor's edges moves. Render only, the .blend is not saved.
#   blender --background astronavigator_suit.blend --python helmet_reshape.py -> renders\hr_<n>_<front|side|rear>.png
import bpy, math, os
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "hr_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
HELM = ["Helmet", "HelmHardware", "HelmLiner", "HelmPods", "HelmRib", "HelmRibRed", "Visor", "SunShade", "ShadeArms"]
for n in ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR", "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR"):
    if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
C = Vector((0.0, -0.038, 1.637)); RZ = 0.170
REST = {}
for n in HELM:
    o = bpy.data.objects[n]; REST[n] = [o.matrix_world @ v.co for v in o.data.vertices]

def smooth01(x): x = max(0.0, min(1.0, x)); return x * x * (3 - 2 * x)

def deform(p, occ_cut=0.0, occ_long=0.0, flats=0.0, brow=0.0, crown=0.0):
    d = p - C; q = Vector(d)
    ang = math.degrees(math.atan2(d.x, -d.y))                 # 0 at her front, +-180 at the back
    back = smooth01((abs(ang) - 75) / 40)                     # 0 in front of the visor's edge, 1 well behind it
    low = smooth01((-d.z + 0.02) / 0.12)                      # the lower part of the head
    high = smooth01((d.z - 0.02) / 0.12)
    if occ_cut:    # the back of the head cut in under the crown, below her hair (clears the pack's top, a ledge over the neck)
        nape = smooth01((-d.z - 0.06) / 0.07)
        q.y -= occ_cut * back * nape * max(0.0, d.y - 0.06)
    if occ_long:   # the back drawn out back and up (the elongated nape)
        q.y += occ_long * back * high * max(0.0, d.y) * 0.6 + occ_long * back * smooth01((d.z + 0.05) / 0.15) * 0.03
        q.z += occ_long * back * max(0.0, d.y) * 0.25
    if flats:      # the sides behind the visor flattened into planes (a 6-sided plan behind the glass)
        r = math.hypot(q.x, q.y); a = math.atan2(q.y, q.x); seg = math.pi / 3
        k = math.cos(seg / 2) / math.cos((a - math.pi / 6) % seg - seg / 2)
        w = flats * back
        q.x *= (1 - w) + w * k; q.y *= (1 - w) + w * k
    if crown:      # a flatter crown
        q.z -= crown * high * max(0.0, d.z) * 0.5
    return C + q

VARS = [("0", "ваш как есть", {}),
        ("1", "затылок подрезан снизу", dict(occ_cut=0.45)),
        ("2", "затылок вытянут назад-вверх", dict(occ_long=0.5)),
        ("3", "грани по бокам за стеклом", dict(flats=0.9)),
        ("4", "подрезан + грани", dict(occ_cut=0.45, flats=0.9)),
        ("5", "вытянут + подрезан снизу", dict(occ_long=0.45, occ_cut=0.4)),
        ("6", "вытянут + грани + плоская макушка", dict(occ_long=0.45, flats=0.8, crown=0.25))]
try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 520, 520; sc.render.film_transparent = True
    for nm, en, rot in (("HR_key", 3.0, (42, 0, 145)), ("HR_fill", 1.4, (65, 0, -40)), ("HR_front", 1.6, (55, 0, 20)), ("HR_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("HR_cam"); cam = bpy.data.objects.new("HR_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0, -0.02, 1.60))
    VIEWS = {"front": T + Vector((-0.62, -0.95, 0.12)), "side": T + Vector((1.15, 0.0, 0.03)), "rear": T + Vector((0.7, 0.95, 0.25))}
    for key, name, kw in VARS:
        for n in HELM:
            o = bpy.data.objects[n]; inv = o.matrix_world.inverted()
            for v, p in zip(o.data.vertices, REST[n]): v.co = inv @ deform(p, **kw)
            o.data.update()
        for vn, loc in VIEWS.items():
            cd.lens = 50; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "hr_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
