# Tantra: retarget a CMU (cgspeed MotionBuilder-friendly) BVH clip onto the astronavigator's cmu_mb rig by AIMING bones.
#
# Why not rotation deltas: copying rotations relative to a T-pose only works if the capture's T-pose and the model's
# T-pose match bone for bone. They never do exactly, and every mismatch shows up as a leaning torso, shrugged
# shoulders and half-bent legs. Here every model bone is pointed where the captured bone points:
#   primary axis   = from the joint to the next joint of its chain (thigh -> knee, shin -> ankle, spine -> neck ...),
#   secondary axis = what fixes the twist: the knee / elbow bend plane, the hip line, the shoulder line.
# The model keeps its own bone lengths; only the hips travel (scaled by the leg-length ratio).
# Output is the same as retarget.py: anim/anim_<clip>.blend, so export_skin.py reads it unchanged.
# Usage: run.ps1 -Script retarget_aim.py -Rest <clip> [<fps>]
import bpy, os, sys, math, traceback
from mathutils import Matrix, Vector

HERE = os.path.dirname(__file__); ROOT = os.path.dirname(HERE)
MOCAP = os.path.join(ROOT, "mocap"); ANIM = os.path.join(HERE, "anim"); os.makedirs(ANIM, exist_ok=True)
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLIP = argv[0] if argv else "08_01"; FPS = int(argv[1]) if len(argv) > 1 else 30
LOG = os.path.join(HERE, "retarget_%s.log" % CLIP); log = ["retarget_aim " + CLIP]

# bone -> joints it aims at (first one far enough from the bone's own joint wins)
AIM = {'Hips': ['LowerBack', 'Spine', 'Spine1'], 'LowerBack': ['Spine', 'Spine1'], 'Spine': ['Spine1', 'Neck'],
       'Spine1': ['Neck', 'Neck1'], 'Neck': ['Neck1', 'Head'], 'Neck1': ['Head'],
       'LHipJoint': ['LeftUpLeg'], 'LeftUpLeg': ['LeftLeg'], 'LeftLeg': ['LeftFoot'], 'LeftFoot': ['LeftToeBase'],
       'RHipJoint': ['RightUpLeg'], 'RightUpLeg': ['RightLeg'], 'RightLeg': ['RightFoot'], 'RightFoot': ['RightToeBase'],
       'LeftShoulder': ['LeftArm'], 'LeftArm': ['LeftForeArm'], 'LeftForeArm': ['LeftHand'],
       'RightShoulder': ['RightArm'], 'RightArm': ['RightForeArm'], 'RightForeArm': ['RightHand']}
TAIL_AIM = {'Head', 'LeftToeBase', 'RightToeBase'}   # end bones: aim along the bone itself (head -> tail)


def hierarchy(arm):
    order = []
    def walk(b):
        order.append(b.name)
        for c in b.children: walk(c)
    for b in arm.data.bones:
        if b.parent is None: walk(b)
    return order


def smooth01(x): x = max(0.0, min(1.0, x)); return x * x * (3 - 2 * x)


def secondaries(P):
    """twist references for one pose, from joint positions P (and tails T for end bones)"""
    hip = (P['LeftUpLeg'] - P['RightUpLeg']).normalized()
    sh = (P['LeftArm'] - P['RightArm']).normalized()
    up = (P['Neck'] - P['Hips']).normalized()
    fwd = up.cross(sh).normalized()
    S = {}
    for n in ('Hips', 'LowerBack', 'LHipJoint', 'RHipJoint'): S[n] = hip
    S['Spine'] = (hip + sh).normalized()
    for n in ('Spine1', 'Neck', 'Neck1', 'Head'): S[n] = sh
    for n in ('LeftShoulder', 'RightShoulder'): S[n] = up
    for side in ('Left', 'Right'):
        # knee: bend-plane normal, falling back to the hip line when the leg is straight
        th = P[side + 'Leg'] - P[side + 'UpLeg']; sn = P[side + 'Foot'] - P[side + 'Leg']
        n = th.cross(sn); w = smooth01(n.length / (th.length * sn.length + 1e-9) / 0.25)
        if n.dot(hip) < 0: n = -n
        k = (n.normalized() * w + hip * (1 - w)).normalized() if n.length > 1e-9 else hip
        for b in ('UpLeg', 'Leg', 'Foot', 'ToeBase'): S[side + b] = k
        # elbow: bend-plane normal, falling back to "as if it bent forward" when the arm is straight
        ua = P[side + 'ForeArm'] - P[side + 'Arm']; fa = P[side + 'Hand'] - P[side + 'ForeArm']
        ref = ua.cross(fwd)
        e = ua.cross(fa); w = smooth01(e.length / (ua.length * fa.length + 1e-9) / 0.25)
        if e.dot(ref) < 0: e = -e
        s = (e.normalized() * w + ref.normalized() * (1 - w)).normalized() if e.length > 1e-9 else ref.normalized()
        for b in ('Arm', 'ForeArm'): S[side + b] = s
    return S


def frame(primary, secondary):
    y = primary.normalized()
    x = secondary - y * secondary.dot(y)
    if x.length < 1e-6: x = y.orthogonal()
    x.normalize(); z = x.cross(y)
    return Matrix((x, y, z)).transposed()   # columns x, y, z


try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    sc = bpy.context.scene
    tgt = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    tgt.animation_data_clear()
    for pb in tgt.pose.bones: pb.rotation_mode = 'QUATERNION'; pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    bpy.context.view_layer.update()
    order = hierarchy(tgt)
    rest = {b.name: b.matrix_local.copy() for b in tgt.data.bones}
    parent = {b.name: (b.parent.name if b.parent else None) for b in tgt.data.bones}
    RP = {n: rest[n].translation.copy() for n in rest}                          # rest joints (armature space)
    RT = {b.name: b.tail_local.copy() for b in tgt.data.bones}
    RS = secondaries(RP)

    def aim_vec(P, T, n):
        if n in TAIL_AIM: return T[n] - P[n]
        for c in AIM.get(n, []):
            v = P[c] - P[n]
            if v.length > 0.02 * scale_of(P): return v
        return None

    def scale_of(P): return (P['Head'] - P['LeftFoot']).length

    RF = {}
    for n in order:
        v = aim_vec(RP, RT, n)
        if v is not None and n in RS: RF[n] = frame(v, RS[n])

    bpy.ops.import_anim.bvh(filepath=os.path.join(MOCAP, CLIP + ".bvh"), global_scale=1.0, rotate_mode='NATIVE',
                            use_fps_scale=False, update_scene_fps=False, update_scene_duration=False, frame_start=1)
    src = bpy.context.object
    act = src.animation_data.action
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    to_tgt = tgt.matrix_world.inverted() @ src.matrix_world
    def src_pose():
        P = {pb.name: to_tgt @ pb.head for pb in src.pose.bones}
        T = {pb.name: to_tgt @ pb.tail for pb in src.pose.bones}
        return P, T
    sc.frame_set(f0); bpy.context.view_layer.update(); P0, _ = src_pose()
    def leglen(P): return sum((P[s + 'UpLeg'] - P[s + 'Foot']).length for s in ('Left', 'Right'))
    ratio = leglen(RP) / leglen(P0)
    missing = [n for n in AIM if n not in P0]
    log.append("clip %s: frames %d..%d, leg ratio %.4f, missing in source: %s" % (CLIP, f0, f1, ratio, missing))

    step = max(1, round(120 / FPS))
    tgt.animation_data_create(); action = bpy.data.actions.new("%s_aim" % CLIP); tgt.animation_data.action = action
    out_frames = list(range(f0 + 1, f1 + 1, step))    # frame 1 is the T-pose
    feet = {'LeftToeBase': [], 'RightToeBase': []}
    for k, f in enumerate(out_frames, start=1):
        sc.frame_set(f)
        P, T = src_pose()
        S = secondaries(P)
        posed = {}
        for n in order:
            pb = tgt.pose.bones[n]; p = parent[n]
            v = aim_vec(P, T, n) if n in RF else None
            if v is not None:
                R = frame(v, S[n]) @ RF[n].transposed()                       # rest frame -> captured frame
                rot = R @ rest[n].to_3x3()
            elif p is not None:
                rot = posed[p].to_3x3() @ (rest[p].to_3x3().inverted() @ rest[n].to_3x3())   # hands, fingers: ride the parent
            else:
                rot = rest[n].to_3x3()
            if p is None:
                head = P['Hips'] * ratio
                want = Matrix.Translation(head) @ rot.to_4x4()
                basis = rest[n].inverted() @ want
                pb.location = basis.translation
            else:
                local_rest = rest[p].inverted() @ rest[n]
                head = (posed[p] @ local_rest).translation
                want = Matrix.Translation(head) @ rot.to_4x4()
                basis = local_rest.inverted() @ posed[p].inverted() @ want
            bq = basis.to_quaternion().normalized(); pb.rotation_quaternion = bq
            if p is None: posed[n] = rest[n] @ Matrix.LocRotScale(basis.translation, bq, None)
            else: posed[n] = posed[p] @ local_rest @ bq.to_matrix().to_4x4()
            pb.keyframe_insert("rotation_quaternion", frame=k)
            if p is None: pb.keyframe_insert("location", frame=k)
        # how well the aim worked: angle between model bone and captured bone, worst over the body
        if k == len(out_frames) // 2:
            errs = []
            for n in RF:
                v = aim_vec(P, T, n)
                if v is None: continue
                c = next((c for c in AIM.get(n, []) if c in posed), None) if n not in TAIL_AIM else None
                if c is None: continue
                mv = posed[c].translation - posed[n].translation
                if mv.length > 1e-6: errs.append((math.degrees(mv.angle(v)), n))
            errs.sort(reverse=True)
            log.append("aim error mid-clip (deg), worst: " + ", ".join("%s %.1f" % (n, e) for e, n in errs[:5]))
        for fb in feet: feet[fb].append((tgt.matrix_world @ posed[fb]).translation.copy())
    sc.frame_start, sc.frame_end = 1, len(out_frames); sc.render.fps = FPS
    bpy.data.objects.remove(src, do_unlink=True)

    for fb in feet:
        Pf = feet[fb]; zmin = min(p.z for p in Pf)
        stance = sorted((Pf[i + 1] - Pf[i]).xy.length * FPS for i in range(len(Pf) - 1) if Pf[i].z < zmin + 0.03)
        if stance:
            log.append("%s: ground z %.3f, stance frames %d, median slide %.3f m/s" % (fb, zmin, len(stance), stance[len(stance) // 2]))
    log.append("frames out %d at %d fps" % (len(out_frames), FPS))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ANIM, "anim_%s.blend" % CLIP))
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
