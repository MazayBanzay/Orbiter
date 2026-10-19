# «Наплечный» M2 sketches (tools/jetpack_m2_check.py): only the M1 pack's own objects, scaled and copied - the capsules
# JetPodL/R by k (moved out so their inner side stays where the booms end, the booms stretched to them), the shell and its
# fittings (JetShell, JetVessel, JetNiche, JetPorts, JetMark, JetCoupling) deeper (aft of her back) and / or taller
# (down), the cruise nozzles - JetPodR copies at 0.6 scale in the base, their exits aft (one shown at its 10 deg vector).
#   blender --background astronavigator_suit.blend --python jetpack_m2.py -> renders\m2_<key>_<rear|side>.png
import bpy, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "m2_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
J = {n: bpy.data.objects[n] for n in ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling", "JetPodL", "JetPodR",
                                      "JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR")}
REST = {n: o.matrix_world.copy() for n, o in J.items()}
SHELL = ("JetShell", "JetVessel", "JetNiche", "JetPorts", "JetMark", "JetCoupling")
FRONT_Y, TOP_Z = 0.108, 1.523                                   # the shell's face to her back, its top
POD_C = {"R": Vector((0.597, 0.175, 1.471)), "L": Vector((-0.597, 0.175, 1.471))}
POD_HALF_W, BOOM_IN, BOOM_OUT = 0.047, 0.213, 0.56
VARS = [("M1", 1.0, 1.0, 1.0, 0, 0.0), ("a", 1.30, 1.35, 1.0, 1, 0.0), ("b", 1.30, 1.20, 1.20, 2, 0.0),
        ("c", 1.45, 1.50, 1.0, 2, 0.06), ("d", 1.30, 1.50, 1.20, 2, 0.0)]
extra = []

def build(kp, kd, kh, n_aux, out):
    for n, o in J.items(): o.matrix_world = REST[n].copy()
    S = Matrix.Translation(Vector((0, FRONT_Y, TOP_Z))) @ Matrix.Diagonal((1, kd, kh, 1)) @ Matrix.Translation(Vector((0, -FRONT_Y, -TOP_Z)))
    for n in SHELL: J[n].matrix_world = S @ REST[n]
    for side, sg in (("R", 1), ("L", -1)):
        c = POD_C[side]; dx = sg * (POD_HALF_W * (kp - 1) + out)
        J["JetPod" + side].matrix_world = Matrix.Translation(c + Vector((dx, 0, 0))) @ Matrix.Scale(kp, 4) @ Matrix.Translation(-c) @ REST["JetPod" + side]
        kb = (BOOM_OUT - BOOM_IN + abs(dx)) / (BOOM_OUT - BOOM_IN)          # the boom stretched out to the bigger capsule
        hb = Vector((sg * BOOM_IN, 0, 0))
        J["JetBoom" + side].matrix_world = Matrix.Translation(hb) @ Matrix.Diagonal((kb, 1, 1, 1)) @ Matrix.Translation(-hb) @ REST["JetBoom" + side]
    # the cruise nozzles: JetPodR at 0.6, turned so its exit (down) points aft (+y), in the base of the shell
    back = FRONT_Y + 0.218 * kd; base = TOP_Z - 0.475 * kh
    xs = [0.0] if n_aux == 1 else [0.12, -0.12] if n_aux == 2 else []
    for i, x in enumerate(xs):
        o = J["JetPodR"].copy(); o.data = J["JetPodR"].data; sc.collection.objects.link(o); extra.append(o)
        yaw = math.radians(10) if i == 0 and n_aux == 2 else 0.0             # one shown at its 10 deg vector
        at = Vector((x, back - 0.02, base + 0.075))
        o.matrix_world = (Matrix.Translation(at) @ Matrix.Rotation(yaw, 4, 'Z') @ Matrix.Rotation(math.radians(90), 4, 'X') @ Matrix.Scale(0.6, 4)
                          @ Matrix.Translation(-POD_C["R"]) @ REST["JetPodR"])

try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 760, 760; sc.render.film_transparent = True
    for nm, en, rot in (("M2_key", 3.0, (42, 0, 145)), ("M2_fill", 1.2, (65, 0, -40))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("M2_cam"); cam = bpy.data.objects.new("M2_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    VIEWS = {"rear": (Vector((1.05, 1.75, 1.05)), Vector((0, 0.22, 1.33)), 'PERSP'), "side": (Vector((3.5, 0.32, 1.30)), Vector((0, 0.32, 1.30)), 'ORTHO')}
    for key, kp, kd, kh, na, out in VARS:
        build(kp, kd, kh, na, out)
        for vn, (loc, tgt, typ) in VIEWS.items():
            cd.type = typ; cd.ortho_scale = 1.05; cd.lens = 45
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "m2_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", key, vn, file=LOG); LOG.flush()
        for o in extra: bpy.data.objects.remove(o, do_unlink=True)
        extra.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
