# Shared poses for the astronavigator rig (cmu_mb): natural standing idle.
import bpy, os
from mathutils import Vector, Matrix, Quaternion
KNEE = float(os.environ.get('TANTRA_KNEE', '0.16'))

def aim(arm, name, target_dir):
    """Rotate a pose bone (in armature space) so its head->tail points along target_dir."""
    pb = arm.pose.bones[name]; M = pb.matrix.copy(); head = M.translation.copy()
    cur = (M @ Vector((0, pb.bone.length, 0)) - head).normalized()
    q = cur.rotation_difference(target_dir.normalized())
    pb.matrix = Matrix.Translation(head) @ q.to_matrix().to_4x4() @ Matrix.Translation(-head) @ M
    bpy.context.view_layer.update()

def idle_pose(arm):
    """Relaxed standing: arms down along the body with a slight elbow bend, legs straight under the hips."""
    for pb in arm.pose.bones: pb.rotation_mode = 'QUATERNION'; pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    bpy.context.view_layer.update()
    # weight on the right leg (contrapposto): pelvis tilts, left knee relaxes, shoulders counter-tilt
    hips = arm.pose.bones["Hips"]; hips.location = hips.bone.matrix_local.inverted().to_3x3() @ Vector((-0.010, 0, -0.006))
    hips.rotation_quaternion = (Matrix.Rotation(0.035, 4, 'Y') @ Matrix.Rotation(-0.06, 4, 'Z')).to_quaternion(); bpy.context.view_layer.update()
    aim(arm, "RightUpLeg", Vector((-0.02, 0.0, -1)))
    aim(arm, "LeftUpLeg", Vector((0.06, -0.05, -1)))
    # relaxed left knee: hinge about the bone's own X axis only (no twist)
    kb = arm.pose.bones["LeftLeg"]; kb.rotation_quaternion = Quaternion((1, 0, 0), KNEE) @ kb.rotation_quaternion
    bpy.context.view_layer.update()
    # the arm on the weight-bearing (right) side hangs a little wider so the hand clears the hip
    for side, sx, out in (("Left", 1, 0.14), ("Right", -1, 0.24)):
        aim(arm, side + "Arm", Vector((sx * out, 0.04, -1)))
        aim(arm, side + "ForeArm", Vector((sx * (out - 0.06), -0.25 if side == "Left" else -0.18, -1)))
        aim(arm, side + "Hand", Vector((sx * (out - 0.10), -0.14, -1)))
    for n, ax, ang in (("Spine1", 'Y', -0.03), ("Neck1", 'Z', 0.10), ("Head", 'Y', 0.05)):
        pb = arm.pose.bones.get(n)
        if pb: pb.rotation_quaternion = Matrix.Rotation(ang, 4, ax).to_quaternion() @ pb.rotation_quaternion
    bpy.context.view_layer.update()

