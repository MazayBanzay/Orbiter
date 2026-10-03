# Tantra / OrbiterCrew: the seat clips of the coverall body from a retargeted CMU capture (default 113_15,
# "sit in chair and get up"; anim/anim_<clip>.blend from retarget.py).
#   Config\Tantra\anim\sit_down.clip  - standing -> seated (non-looping)
#   Config\Tantra\anim\sit.clip       - the seated pose (one frame)
#   Config\Tantra\anim\stand_up.clip  - seated -> standing (non-looping)
# Same skeleton and coordinates as export_skin.py (model space, Orbiter LH Y up, the body faces +Z). Every frame is
# turned so that she faces +Z when seated, and moved so that her seated hips are over the model origin (the seat).
# Usage: run.ps1 -Script export_sit.py -Rest [clip]
import bpy, os, sys, traceback
from mathutils import Matrix, Vector
HERE = os.path.dirname(__file__); ROOT = os.path.dirname(HERE)
ORBITER = os.path.abspath(os.path.join(ROOT, ".."))
CLIP_DIR = os.path.join(ORBITER, "Config", "Tantra", "anim")
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLIP = argv[0] if argv else "113_15"
ORIGIN_H = 0.93
C3 = Matrix(((-1, 0, 0), (0, 0, 1), (0, -1, 0)))
LOG = os.path.join(HERE, "export_sit.log"); log = []

def cp(p):
    q = C3 @ Vector(p); return Vector((q.x, q.y - ORIGIN_H, q.z))
def cm(M4):
    R = C3 @ M4.to_3x3() @ C3.transposed(); return R, cp(M4.translation)
def hierarchy(arm):
    order = []
    def walk(b):
        order.append(b.name)
        for c in b.children: walk(c)
    for b in arm.data.bones:
        if b.parent is None: walk(b)
    return order
def write_clip(name, frames, fps, loop, bones):
    with open(os.path.join(CLIP_DIR, name + ".clip"), "w", newline="\n") as f:
        f.write("TANTRA_CLIP 1\nNAME %s\nFPS %d\nFRAMES %d\nSTRIDE 0.0000\nSPEED 0.0000\nLOOP %d\nBONES %d\n" % (name, fps, len(frames), loop, len(bones)))
        for k, fr in enumerate(frames):
            f.write("FRAME %d\n" % k)
            for n in bones:
                R, t = fr[n]; q = R.to_quaternion()
                f.write("%.6f %.6f %.6f %.6f %.5f %.5f %.5f\n" % (q.w, q.x, q.y, q.z, t.x, t.y, t.z))
    log.append("clip %s: %d frames" % (name, len(frames)))

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "anim", "anim_%s.blend" % CLIP))
    sc = bpy.context.scene; arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); fps = sc.render.fps
    bones = hierarchy(arm)
    F = list(range(sc.frame_start, sc.frame_end + 1)); mats = []
    for fr in F:
        sc.frame_set(fr); mats.append({n: (arm.matrix_world @ arm.pose.bones[n].matrix).copy() for n in bones})
    h = [m["Hips"].translation.z for m in mats]
    stand = sorted(h[:15])[7]; low = min(h); seat_i = h.index(low)
    log.append("clip %s: %d frames at %d fps; hips standing %.3f, seated %.3f (frame %d)" % (CLIP, len(F), fps, stand, low, seat_i))
    near_low = lambda i: h[i] < low + 0.03
    up = lambda i: h[i] > stand - 0.03
    # sit-down: from the last standing frame before the seat to the first seated one
    s1 = next(i for i in range(len(h)) if near_low(i))
    s0 = max(i for i in range(s1) if up(i))
    # seated span, then stand-up: from the last seated frame to the first standing one after it
    e0 = max(i for i in range(s1, len(h)) if near_low(i) and all(h[j] < stand - 0.1 for j in range(s1, i + 1)))
    e1 = next(i for i in range(e0, len(h)) if up(i))
    mid = (s1 + e0) // 2
    log.append("sit_down %d..%d (%.2f s), seated %d..%d, stand_up %d..%d (%.2f s)" % (s0, s1, (s1 - s0) / fps, s1, e0, e0, e1, (e1 - e0) / fps))

    # turn and move: seated facing -Y (Blender; +Z in Orbiter), seated hips over the origin
    M = mats[mid]
    lr = M["LeftUpLeg"].translation - M["RightUpLeg"].translation; lr.z = 0; lr.normalize()
    fwd = lr.cross(Vector((0, 0, 1))); fwd.z = 0; fwd.normalize()
    turn = fwd.rotation_difference(Vector((0, -1, 0))).to_matrix().to_4x4()
    hp = turn @ M["Hips"].translation
    T = Matrix.Translation(Vector((-hp.x, -hp.y, 0))) @ turn
    log.append("seated facing (%.2f %.2f) turned to -Y" % (fwd.x, fwd.y))
    conv = lambda i: {n: cm(T @ mats[i][n]) for n in bones}
    write_clip("sit_down", [conv(i) for i in range(s0, s1 + 1)], fps, 0, bones)
    write_clip("sit", [conv(mid)], fps, 0, bones)
    write_clip("stand_up", [conv(i) for i in range(e0, e1 + 1)], fps, 0, bones)
    hs = conv(mid)["Hips"][1]; ft = conv(mid)["LeftFoot"][1]; f0 = conv(s0)["Hips"][1]; f1 = conv(e1)["Hips"][1]
    log.append("seated: hips (%.3f %.3f %.3f), left foot (%.3f %.3f %.3f); standing at start hips z %.3f, at end z %.3f" %
               (hs.x, hs.y, hs.z, ft.x, ft.y, ft.z, f0.z, f1.z))
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log) + "\n")
