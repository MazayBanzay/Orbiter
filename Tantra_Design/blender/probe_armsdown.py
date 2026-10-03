# the garment with the arms down (the game's idle), armpit and side views
import bpy, os, sys, math
from mathutils import Vector, Quaternion
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
def rot(bname, axis, ang):
    pb = arm.pose.bones[bname]; pb.rotation_mode = 'QUATERNION'
    M = arm.matrix_world @ pb.bone.matrix_local
    R = M.to_3x3().inverted() @ Quaternion(axis, ang).to_matrix() @ M.to_3x3(); pb.rotation_quaternion = R.to_quaternion()
rot("LeftArm", (0, 1, 0), math.radians(62)); rot("RightArm", (0, 1, 0), math.radians(-62))
bpy.context.view_layer.update()
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 800
for nm, loc, tgt in (("ad_side", (1.3, 0.1, 1.15), (0, 0, 1.1)), ("ad_back34", (-0.9, 0.9, 1.25), (0, 0, 1.15)), ("ad_front34", (0.9, -0.9, 1.25), (0, -0.05, 1.15))):
    cam.location = loc; cam.data.lens = 55; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
