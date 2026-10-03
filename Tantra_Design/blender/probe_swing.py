# coverall chest with the arms swung as in a walk (one back, one forward) - side views
import bpy, os, sys, math
from mathutils import Vector, Euler
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
for name, ang in (("LeftArm", 1), ("RightArm", -1)):
    pb = arm.pose.bones[name]; pb.rotation_mode = 'XYZ'
for a_ in ("LeftArm", "RightArm"):
    pb = arm.pose.bones[a_]
# bring the arms down from the A-pose and swing them: rotate about the world X axis
def swing(bname, down, fwd):
    pb = arm.pose.bones[bname]; pb.rotation_mode = 'QUATERNION'
    M = arm.matrix_world @ pb.bone.matrix_local
    from mathutils import Quaternion
    side = 1 if 'Left' in bname else -1
    qw = Quaternion((0, 1, 0), side * down) @ Quaternion((1, 0, 0), fwd)
    R = M.to_3x3().inverted() @ qw.to_matrix() @ M.to_3x3(); pb.rotation_quaternion = R.to_quaternion()
swing("LeftArm", math.radians(-25), math.radians(35)); swing("RightArm", math.radians(-25), math.radians(-35))
bpy.context.view_layer.update()
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 600
for nm, loc in (("sw_left", (0.9, -0.15, 1.30)), ("sw_right", (-0.9, -0.15, 1.30)), ("sw_front", (0.35, -0.9, 1.32))):
    cam.location = loc; cam.data.lens = 55; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0, -0.05, 1.22)) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
