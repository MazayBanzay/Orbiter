# Pantsir: render the game asset (astronavigator_pantsir.blend, skinned by the rig) in the idle pose and in walk / run
# frames (with the Motion.cpp suit layer), plus the cap open 65 deg -> check_*.png.  The blend is only loaded, never saved.
import bpy, os, sys, math, traceback
from mathutils import Vector as V
HERE = os.path.dirname(os.path.abspath(__file__)); BL = os.path.abspath(os.path.join(HERE, "..", "..", "blender")); sys.path.insert(0, BL)
OUT = os.path.abspath(os.path.join(HERE, "..")); LOG = os.path.join(HERE, "render_game_check.log"); L = []
try:
    import render_util
    from pose_util import idle_pose
    from mathutils import Matrix
    def suit_layer(arm):                                           # = build_pantsir_game.suit_layer (Motion.cpp)
        W = arm.matrix_world
        for name, a in (("LeftArm", -0.46), ("RightArm", 0.46), ("LeftUpLeg", -0.21), ("RightUpLeg", 0.21), ("LeftFoot", 0.21), ("RightFoot", -0.21)):
            pb = arm.pose.bones[name]; M = W @ pb.matrix; h = M.translation.copy()
            pb.matrix = W.inverted() @ (Matrix.Translation(h) @ Matrix.Rotation(a, 4, 'Y') @ Matrix.Translation(-h) @ M); bpy.context.view_layer.update()
    cam = render_util.setup_stage(); sc = bpy.context.scene
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 24; sc.cycles.use_denoising = True
    sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Base Contrast'; sc.view_settings.exposure = -0.6
    w = sc.world.node_tree; bg = next(n for n in w.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.3, 0.31, 0.33, 1); bg.inputs[1].default_value = 0.8
    sun = bpy.data.lights.new("SunK", 'SUN'); sun.energy = 2.5; so = bpy.data.objects.new("SunK", sun); sc.collection.objects.link(so)
    so.rotation_euler = (math.radians(50), 0, math.radians(35))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'POSE'
    def shot(name, loc, tgt, lens=60, res=(800, 1000)):
        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (V(tgt) - V(loc)).to_track_quat('-Z', 'Y')
        sc.render.resolution_x, sc.render.resolution_y = res; bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(HERE, name + ".png"); bpy.ops.render.render(write_still=True); L.append("rendered " + name)
    idle_pose(arm); suit_layer(arm)
    if os.environ.get("ONLY_IDLE_BACK") == "1": shot("check_idle_back34", (-2.6, 3.2, 1.1), (0, 0, 0.75)); raise SystemExit
    shot("check_idle_front", (2.3, -3.6, 1.3), (0, 0, 0.9))
    if os.environ.get("ONLY_IDLE") == "1": raise SystemExit                     # one quick idle front only
    shot("check_idle_back", (-2.3, 3.6, 1.3), (0, 0, 0.9))
    for pb in arm.pose.bones: pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    with bpy.data.libraries.load(os.path.join(BL, "anim", "anim_35_01.blend")) as (a, b): b.actions = list(a.actions)
    arm.animation_data_create(); arm.animation_data.action = b.actions[0]; sc.frame_set(22); bpy.context.view_layer.update(); suit_layer(arm)
    h = arm.matrix_world @ arm.pose.bones["Hips"].head
    shot("check_walk_side", h + V((-3.8, -0.4, 0.1)), h + V((0, 0, -0.05)))
    shot("check_walk_front", h + V((1.6, -3.6, 0.4)), h + V((0, 0, -0.05)))
    # the cap open 65 deg (rest pose: no deformation, so the hinge is the exported one): HelmCapHinge (0, 0.8745, -0.0943)
    arm.animation_data.action = None
    for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    arm.data.pose_position = 'REST'; hb = V((0, 0.0943, 0.8745 + 0.93))
    T = Matrix.Translation(hb) @ Matrix.Rotation(math.radians(-65), 4, 'X') @ Matrix.Translation(-hb)
    for o in bpy.data.objects:
        if o.type == 'MESH' and o.name.startswith("HelmCap"): o.matrix_world = T @ o.matrix_world
    shot("check_cap_open", (0.35, -1.05, 1.75), (0, -0.02, 1.68), lens=60, res=(800, 1000))
except SystemExit:
    pass
except Exception:
    L.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
