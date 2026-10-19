# Pantsir: the game asset in the idle pose with the Motion.cpp suit layer as the game applies it now (class keys
# ArmOutDeg / ForeArmInDeg / LegOutDeg, the heavy knee bend) -> stance_*.png. Env ARM, FORE, LEG (deg), KNEE (rad).
# The blend is only loaded, never saved.
import bpy, os, sys, math, traceback
from mathutils import Vector as V, Matrix
HERE = os.path.dirname(os.path.abspath(__file__)); BL = os.path.abspath(os.path.join(HERE, "..", "..", "blender")); sys.path.insert(0, BL)
LOG = os.path.join(HERE, "check_stance.log"); L = []
A = math.radians(float(os.environ.get("ARM", "21"))); F = math.radians(float(os.environ.get("FORE", "16")))
G = math.radians(float(os.environ.get("LEG", "2"))); K = float(os.environ.get("KNEE", "0.11")); TAG = os.environ.get("TAG", "")
try:
    import render_util
    from pose_util import idle_pose
    def turn(arm, name, a, axis='Y'):
        W = arm.matrix_world; pb = arm.pose.bones[name]; M = W @ pb.matrix; h = M.translation.copy()
        pb.matrix = W.inverted() @ (Matrix.Translation(h) @ Matrix.Rotation(a, 4, axis) @ Matrix.Translation(-h) @ M); bpy.context.view_layer.update()
    def suit_layer(arm):   # Motion.cpp: AX_FWD turns about the model's forward axis (Blender Y), AX_LAT about the side axis (X)
        for n, a in (("LeftArm", -A), ("RightArm", A), ("LeftForeArm", F), ("RightForeArm", -F),
                     ("LeftUpLeg", -G), ("RightUpLeg", G), ("LeftFoot", G), ("RightFoot", -G)): turn(arm, n, a)
        for s in ("Left", "Right"):
            turn(arm, s + "UpLeg", -K, 'X'); turn(arm, s + "Leg", 2 * K, 'X'); turn(arm, s + "Foot", -K, 'X')
    cam = render_util.setup_stage(); sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'CYCLES'
    if sc.render.engine == 'CYCLES': sc.cycles.samples = 8; sc.cycles.use_denoising = True
    w = sc.world.node_tree; bg = next(n for n in w.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.3, 0.31, 0.33, 1); bg.inputs[1].default_value = 0.9
    sun = bpy.data.lights.new("SunK", 'SUN'); sun.energy = 3; so = bpy.data.objects.new("SunK", sun); sc.collection.objects.link(so); so.rotation_euler = (math.radians(50), 0, math.radians(35))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'POSE'
    def shot(name, loc, tgt, lens=60, res=(700, 900)):
        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (V(tgt) - V(loc)).to_track_quat('-Z', 'Y')
        sc.render.resolution_x, sc.render.resolution_y = res; bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(HERE, "stance_" + name + TAG + ".png"); bpy.ops.render.render(write_still=True); L.append("rendered " + name)
    idle_pose(arm); suit_layer(arm)
    shot("back", (0, 4.2, 1.0), (0, 0, 0.85))
    shot("front", (0, -4.2, 1.0), (0, 0, 0.85))
    shot("shoulder", (-1.2, 1.4, 1.75), (-0.25, 0, 1.35), lens=70, res=(700, 700))
except Exception:
    L.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
