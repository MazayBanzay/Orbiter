# Pantsir game asset: probe the arm / leg joint positions in the suit clips (read only)
import bpy, os, sys, traceback
HERE = os.path.dirname(os.path.abspath(__file__)); BL = os.path.abspath(os.path.join(HERE, "..", "..", "blender"))
OUT = os.path.join(HERE, "probe_clip.log"); L = []
J = ("LeftArm", "LeftForeArm", "LeftHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "Spine1", "Head")
def dump(tag, arm):
    s = []
    for n in J:
        p = arm.matrix_world @ arm.pose.bones[n].head; s.append("%s=(%.3f %.3f %.3f)" % (n, p.x, p.y, p.z))
    L.append(tag + " " + " ".join(s))
try:
    sys.path.insert(0, BL); from pose_util import idle_pose
    bpy.ops.wm.open_mainfile(filepath=os.path.join(BL, "astronavigator_suit.blend"))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'POSE'; idle_pose(arm); bpy.context.view_layer.update()
    dump("IDLE", arm)
    for clip in ("35_01", "09_01_deltas"):
        bpy.ops.wm.open_mainfile(filepath=os.path.join(BL, "anim", "anim_%s.blend" % clip))
        sc = bpy.context.scene; arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
        F = list(range(sc.frame_start, sc.frame_end + 1)); L.append("CLIP %s frames %d..%d" % (clip, F[0], F[-1]))
        for fr in F[::max(1, len(F) // 12)]:
            sc.frame_set(fr); h = arm.matrix_world @ arm.pose.bones["Hips"].head
            s = []
            for n in J:
                p = arm.matrix_world @ arm.pose.bones[n].head - h; s.append("%s=(%.3f %.3f %.3f)" % (n[:9], p.x, p.y, p.z))
            L.append(" f%d hips_z=%.3f " % (fr, h.z) + " ".join(s))
except Exception:
    L.append(traceback.format_exc())
open(OUT, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
