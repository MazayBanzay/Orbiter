# Tantra: retarget a CMU (cgspeed MotionBuilder-friendly) BVH clip onto the astronavigator's cmu_mb rig.
# Method: world-space rotation deltas relative to the T-pose (BVH frame 1 is a T-pose; the target is put
# into MPFB's cmu_mb T-pose), solved top-down into pose-bone basis rotations. Root translation is scaled by
# the leg-length ratio so steps land where the feet are (no skating). Reports foot sliding during stance.
# Usage: run.ps1 -Script retarget.py -Rest <clip> [<fps>]
import bpy, os, sys, json, math, traceback
from mathutils import Matrix, Quaternion, Vector, Euler

HERE = os.path.dirname(__file__); ROOT = os.path.dirname(HERE)
MOCAP = os.path.join(ROOT, "mocap"); OUT = os.path.join(HERE, "renders"); ANIM = os.path.join(HERE, "anim")
os.makedirs(OUT, exist_ok=True); os.makedirs(ANIM, exist_ok=True)
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLIP = argv[0] if argv else "35_01"; FPS = int(argv[1]) if len(argv) > 1 else 30
LOG = os.path.join(HERE, "retarget_%s.log" % CLIP); log = []

def hierarchy(arm):
    order = []
    def walk(b):
        order.append(b.name)
        for c in b.children: walk(c)
    for b in arm.data.bones:
        if b.parent is None: walk(b)
    return order

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    sc = bpy.context.scene
    tgt = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    tgt.animation_data_clear()
    for pb in tgt.pose.bones: pb.rotation_mode = 'QUATERNION'; pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)

    # --- target T-pose (MPFB pose file for cmu_mb) ---
    pose = json.load(open(os.path.join(HERE, "..", "bx", "mpfb", "data", "poses", "cmu_mb_fk", "t-pose.json")))
    for name, eul in pose.get("bone_rotations", {}).items():
        pb = tgt.pose.bones.get(name)
        if pb: pb.rotation_quaternion = Euler(eul, 'XYZ').to_quaternion()
    bpy.context.view_layer.update()
    Tt = {pb.name: (tgt.matrix_world @ pb.matrix).copy() for pb in tgt.pose.bones}

    # --- source BVH ---
    bpy.ops.import_anim.bvh(filepath=os.path.join(MOCAP, CLIP + ".bvh"), global_scale=1.0, rotate_mode='NATIVE',
                            use_fps_scale=False, update_scene_fps=False, update_scene_duration=False, frame_start=1)
    src = bpy.context.object
    act = src.animation_data.action
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    sc.frame_set(f0); bpy.context.view_layer.update()
    S0 = {pb.name: (src.matrix_world @ pb.matrix).copy() for pb in src.pose.bones}
    names = [n for n in hierarchy(tgt) if n in S0]
    log.append("clip %s: frames %d..%d, shared bones %d/%d" % (CLIP, f0, f1, len(names), len(tgt.pose.bones)))

    # leg-length ratio (hip joint to foot) for root translation
    def leglen(M): return (M['LeftUpLeg'].translation - M['LeftFoot'].translation).length + (M['RightUpLeg'].translation - M['RightFoot'].translation).length
    ratio = leglen(Tt) / leglen(S0)
    log.append("leg ratio target/source %.4f" % ratio)

    # --- solve every output frame ---
    step = max(1, round(120 / FPS))   # CMU is 120 fps
    tgt.animation_data_create(); action = bpy.data.actions.new("%s_retarget" % CLIP); tgt.animation_data.action = action
    rest = {b.name: b.matrix_local.copy() for b in tgt.data.bones}
    parent = {b.name: (b.parent.name if b.parent else None) for b in tgt.data.bones}
    out_frames = list(range(f0 + 1, f1 + 1, step))    # skip the T-pose frame
    feet = {'LeftToeBase': [], 'RightToeBase': [], 'LeftFoot': [], 'RightFoot': []}
    for k, f in enumerate(out_frames, start=1):
        sc.frame_set(f)
        S = {pb.name: (src.matrix_world @ pb.matrix) for pb in src.pose.bones}
        posed = {}
        for n in hierarchy(tgt):
            pb = tgt.pose.bones[n]
            if n in S:
                q = (S[n].to_quaternion() @ S0[n].to_quaternion().inverted()) @ Tt[n].to_quaternion()
            else:
                q = Tt[n].to_quaternion()
            p = parent[n]
            if p is None:
                head = Tt[n].translation + (S[n].translation - S0[n].translation) * ratio if n in S else Tt[n].translation
                want = Matrix.LocRotScale(head, q, None)
                basis = rest[n].inverted() @ want
                pb.location = basis.translation
            else:
                local_rest = rest[p].inverted() @ rest[n]
                parent_pose = posed[p]
                head = (parent_pose @ local_rest).translation
                want = Matrix.LocRotScale(head, q, None)
                basis = local_rest.inverted() @ parent_pose.inverted() @ want
            bq = basis.to_quaternion(); pb.rotation_quaternion = bq
            if p is None:
                posed[n] = rest[n] @ Matrix.LocRotScale(basis.translation, bq, None)
            else:
                posed[n] = posed[p] @ local_rest @ bq.to_matrix().to_4x4()
            pb.keyframe_insert("rotation_quaternion", frame=k)
            if p is None: pb.keyframe_insert("location", frame=k)
        for fb in feet:
            if fb in posed: feet[fb].append((tgt.matrix_world @ posed[fb]).translation.copy())
    sc.frame_start, sc.frame_end = 1, len(out_frames); sc.render.fps = FPS
    bpy.data.objects.remove(src, do_unlink=True)

    # --- foot sliding report: horizontal speed of the toe while it is on the ground ---
    for fb in ('LeftToeBase', 'RightToeBase'):
        P = feet[fb]
        if len(P) < 3: continue
        zmin = min(p.z for p in P)
        stance = [(P[i + 1] - P[i]).xy.length * FPS for i in range(len(P) - 1) if P[i].z < zmin + 0.03]
        moving = [(P[i + 1] - P[i]).xy.length * FPS for i in range(len(P) - 1)]
        if stance:
            stance.sort()
            log.append("%s: ground z %.3f, stance frames %d, median slide %.3f m/s, p90 %.3f m/s (overall mean speed %.2f m/s)"
                       % (fb, zmin, len(stance), stance[len(stance) // 2], stance[int(len(stance) * 0.9)], sum(moving) / len(moving)))
    log.append("frames out %d at %d fps" % (len(out_frames), FPS))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ANIM, "anim_%s.blend" % CLIP))
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
