# Pantsir: shell-against-shell interpenetration in the idle pose and the walk / run frames (read only)
import bpy, os, sys, traceback
from mathutils.bvhtree import BVHTree
HERE = os.path.dirname(os.path.abspath(__file__)); BL = os.path.abspath(os.path.join(HERE, "..", "..", "blender")); sys.path.insert(0, BL)
LOG = os.path.join(HERE, "probe_collide.log"); L = []
try:
    from pose_util import idle_pose
    from mathutils import Matrix
    def suit_layer(arm):                                           # = build_pantsir_game.suit_layer (Motion.cpp)
        W = arm.matrix_world
        for name, a in (("LeftArm", -0.46), ("RightArm", 0.46), ("LeftUpLeg", -0.21), ("RightUpLeg", 0.21), ("LeftFoot", 0.21), ("RightFoot", -0.21)):
            pb = arm.pose.bones[name]; M = W @ pb.matrix; h = M.translation.copy()
            pb.matrix = W.inverted() @ (Matrix.Translation(h) @ Matrix.Rotation(a, 4, 'Y') @ Matrix.Translation(-h) @ M); bpy.context.view_layer.update()
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'POSE'
    objs = [o for o in bpy.data.objects if o.type == 'MESH']
    sets = {"legL": ("ThL", "KnL", "ShL", "FtL", "ToL"), "legR": ("ThR", "KnR", "ShR", "FtR", "ToR"), "armL": ("UaL", "FaL", "HdL"), "armR": ("UaR", "FaR", "HdR"), "torso": ("Tor", "Pel", "Wst")}
    def bvh(keys):
        dg = bpy.context.evaluated_depsgraph_get(); vs, fs = [], []
        for o in objs:
            if o.name.startswith(keys):
                me = o.evaluated_get(dg).to_mesh(); b = len(vs); vs += [o.matrix_world @ v.co for v in me.vertices]; fs += [tuple(b + i for i in p.vertices) for p in me.polygons]; o.evaluated_get(dg).to_mesh_clear()
        return BVHTree.FromPolygons(vs, fs), vs
    def run(tag):
        bpy.context.view_layer.update(); T = {k: bvh(v) for k, v in sets.items()}
        res = []
        for a, b in (("legL", "legR"), ("armL", "torso"), ("armR", "torso"), ("legL", "torso"), ("legR", "torso")):
            ov = T[a][0].overlap(T[b][0]); res.append("%s/%s %d" % (a, b, len(ov)))
        L.append(tag + ": " + ", ".join(res))
    idle_pose(arm); suit_layer(arm); run("idle+layer")
    for pb in arm.pose.bones: pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    for clip, frames in (("35_01", (8, 15, 22, 36, 43, 50, 57)), ("09_01_deltas", (4, 7, 10, 22, 31))):
        with bpy.data.libraries.load(os.path.join(BL, "anim", "anim_%s.blend" % clip)) as (a, b): b.actions = list(a.actions)
        arm.animation_data_create(); arm.animation_data.action = b.actions[0]
        for f in frames: bpy.context.scene.frame_set(f); suit_layer(arm); run("%s f%d +layer" % (clip, f))
except Exception:
    L.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
