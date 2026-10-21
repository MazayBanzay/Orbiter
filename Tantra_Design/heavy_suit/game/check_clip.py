# Pantsir: the game asset posed by an exported clip frame (Config\Tantra\anim_pantsir\<CLIP>.clip, frame FRAME) plus the
# Motion.cpp suit layer for this class (LegOutDeg LEG, the heavy knee bend KNEE) -> clip_*.png. Loaded, never saved.
import bpy, os, sys, math, traceback
from mathutils import Vector as V, Matrix, Quaternion
HERE = os.path.dirname(os.path.abspath(__file__)); BL = os.path.abspath(os.path.join(HERE, "..", "..", "blender")); sys.path.insert(0, BL)
LOG = os.path.join(HERE, "check_clip.log"); L = []
CLIP = os.environ.get("CLIP", "idle"); FRAME = int(os.environ.get("FRAME", "0")); TAG = os.environ.get("TAG", "")
G = math.radians(float(os.environ.get("LEG", "5"))); K = float(os.environ.get("KNEE", "0.11")); ORIGIN_H = 0.93
C3 = Matrix(((-1, 0, 0), (0, 0, 1), (0, -1, 0)))
try:
    import render_util
    txt = open(os.path.join(HERE, "..", "..", "..", "Config", "Tantra", "anim_pantsir", CLIP + ".clip")).read().split("\n")
    nb = int(next(l for l in txt if l.startswith("BONES")).split()[1]); i0 = txt.index("FRAME %d" % FRAME) + 1
    rows = [list(map(float, txt[i0 + k].split())) for k in range(nb)]
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'POSE'
    order = []
    def walk(b):
        order.append(b.name)
        for c in b.children: walk(c)
    for b in arm.data.bones:
        if b.parent is None: walk(b)
    W = arm.matrix_world
    for k, n in enumerate(order):
        q = Quaternion(rows[k][0:4]); t = V(rows[k][4:7])
        Rb = C3.transposed() @ q.to_matrix() @ C3; tb = C3.transposed() @ (t + V((0, ORIGIN_H, 0)))
        M = Matrix.Translation(tb) @ Rb.to_4x4()
        arm.pose.bones[n].matrix = W.inverted() @ M; bpy.context.view_layer.update()
    def turn(name, a, axis='Y'):
        pb = arm.pose.bones[name]; M = W @ pb.matrix; h = M.translation.copy()
        pb.matrix = W.inverted() @ (Matrix.Translation(h) @ Matrix.Rotation(a, 4, axis) @ Matrix.Translation(-h) @ M); bpy.context.view_layer.update()
    for n, a in (("LeftUpLeg", -G), ("RightUpLeg", G), ("LeftFoot", G), ("RightFoot", -G)): turn(n, a)
    for s in ("Left", "Right"): turn(s + "UpLeg", -K, 'X'); turn(s + "Leg", 2 * K, 'X'); turn(s + "Foot", -K, 'X')
    cam = render_util.setup_stage(); sc = bpy.context.scene
    sc.render.engine = os.environ.get('ENGINE', 'BLENDER_EEVEE_NEXT')
    w = sc.world.node_tree; bg = next(n for n in w.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.3, 0.31, 0.33, 1); bg.inputs[1].default_value = 0.9
    sun = bpy.data.lights.new("SunK", 'SUN'); sun.energy = 3; so = bpy.data.objects.new("SunK", sun); sc.collection.objects.link(so); so.rotation_euler = (math.radians(50), 0, math.radians(35))
    def shot(name, loc, tgt, lens=60, res=(700, 900)):
        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (V(tgt) - V(loc)).to_track_quat('-Z', 'Y')
        sc.render.resolution_x, sc.render.resolution_y = res; bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(HERE, "clip_" + CLIP + "_" + name + TAG + ".png"); bpy.ops.render.render(write_still=True); L.append("rendered " + name)
    h = W @ arm.pose.bones["Hips"].head
    shot("back", h + V((0, 4.2, 0.1)), h + V((0, 0, -0.05)))
    shot("front", h + V((0, -4.2, 0.1)), h + V((0, 0, -0.05)))
    shot("shoulder", h + V((-1.2, 1.4, 0.85)), h + V((-0.25, 0, 0.45)), lens=70, res=(700, 700))
    shot("side", h + V((-4.0, -0.3, 0.1)), h + V((0, 0, -0.05)))
    if os.environ.get("HIP") == "1":
        hp = W @ arm.pose.bones["Hips"].head
        shot("hip_back", hp + V((0.25, 1.6, -0.25)), hp + V((0, 0, -0.12)), lens=60, res=(700, 700))
        shot("hip_side", hp + V((-1.7, -0.2, -0.2)), hp + V((0, 0, -0.12)), lens=60, res=(700, 700))
    if os.environ.get("CLOSE") == "1":
        hd = W @ arm.pose.bones["Head"].head
        shot("helm_F", hd + V((0.25, -0.95, 0.12)), hd + V((0, -0.05, 0.08)), lens=70, res=(700, 700))
        shot("helm_L", hd + V((0.75, 0.55, 0.15)), hd + V((0, 0, 0.05)), lens=70, res=(700, 700))
        shot("helm_R", hd + V((-0.75, 0.55, 0.15)), hd + V((0, 0, 0.05)), lens=70, res=(700, 700))
        el = W @ arm.pose.bones["LeftForeArm"].head
        shot("elbow_L", el + V((0.9, 0.9, 0.2)), el, lens=70, res=(700, 700))
except Exception:
    L.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
