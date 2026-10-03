# the chest in the game's views: arms down, side, three-quarter, front - grey lighting like Orbiter's
import bpy, os, sys, math
from mathutils import Vector, Quaternion
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
def rot(bname, axis, ang):
    pb = arm.pose.bones[bname]; pb.rotation_mode = 'QUATERNION'
    M = arm.matrix_world @ pb.bone.matrix_local
    pb.rotation_quaternion = (M.to_3x3().inverted() @ Quaternion(axis, ang).to_matrix() @ M.to_3x3()).to_quaternion()
rot("LeftArm", (0, 1, 0), math.radians(62)); rot("RightArm", (0, 1, 0), math.radians(-62)); bpy.context.view_layer.update()
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 700
for nm, loc, tgt in (("ch_side", (1.1, -0.15, 1.25), (0, -0.05, 1.22)), ("ch_34", (0.75, -0.85, 1.28), (0, -0.05, 1.20)), ("ch_front", (0.0, -1.1, 1.25), (0, -0.05, 1.18))):
    cam.location = loc; cam.data.lens = 55; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
