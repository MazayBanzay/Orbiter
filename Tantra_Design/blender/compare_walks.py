# Tantra: compare retargeted walk clips side by side - front-view frames over one gait cycle plus sway numbers.
# The front view shows what a side strip hides: hip sway, pelvis roll, arms crossing or flaring.
# Usage: run.ps1 -Script compare_walks.py -Rest <clip>,<clip>,...
import bpy, os, sys, math, traceback
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLIPS = argv or ["35_01"]
OUT = os.path.join(HERE, "renders", "compare"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "compare_walks.log"); log = []
NSHOT = 6
try:
    for clip in CLIPS:
        bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "anim", "anim_%s.blend" % clip))
        sc = bpy.context.scene
        arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
        F = list(range(sc.frame_start, sc.frame_end + 1))
        def M(f, b):
            sc.frame_set(f); return (arm.matrix_world @ arm.pose.bones[b].matrix).copy()
        H = [M(f, "Hips").translation for f in F]
        fwd = (H[-1] - H[0]); fwd.z = 0; fwd.normalize(); side = Vector((-fwd.y, fwd.x, 0))
        # one cycle in the middle: left foot forward peaks
        rel = [(M(f, "LeftFoot").translation - M(f, "Hips").translation).dot(fwd) for f in F]
        mean = sum(rel) / len(rel)
        pk = [i for i in range(2, len(rel) - 2) if rel[i] >= max(rel[i - 2:i + 3]) and rel[i] > mean]
        pk = [p for k, p in enumerate(pk) if k == 0 or p - pk[k - 1] > 6]
        a, b = (pk[len(pk) // 2 - 1], pk[len(pk) // 2]) if len(pk) >= 2 else (0, len(F) - 1)
        cyc = list(range(a, b))
        # sway numbers over the cycle (after removing the straight line of travel)
        line = lambda i: H[a] + (H[b] - H[a]) * ((i - a) / (b - a))
        lat = [(H[i] - line(i)).dot(side) for i in cyc]
        up = [H[i].z for i in cyc]
        def roll(i):   # pelvis roll: tilt of the hip joints line in the frontal plane
            l = M(F[i], "LeftUpLeg").translation; r = M(F[i], "RightUpLeg").translation; d = l - r
            return math.degrees(math.atan2(d.z, d.dot(side)))
        def yaw(i):
            l = M(F[i], "LeftUpLeg").translation; r = M(F[i], "RightUpLeg").translation; d = l - r
            return math.degrees(math.atan2(d.dot(fwd), d.dot(side)))
        def hand_out(i):   # how far the hands swing out from the body centre line (m)
            h = M(F[i], "Hips").translation
            return max(abs((M(F[i], s + "Hand").translation - h).dot(side)) for s in ("Left", "Right"))
        def width(i):   # lateral distance between the ankles; a catwalk puts the feet on one line
            return abs((M(F[i], "LeftFoot").translation - M(F[i], "RightFoot").translation).dot(side))
        Wd = [width(i) for i in cyc]
        R = [roll(i) for i in cyc]; Y = [yaw(i) for i in cyc]; HO = [hand_out(i) for i in cyc]
        spd = (H[b] - H[a]).length / ((b - a) / sc.render.fps)
        log.append("%s: cycle %d..%d  speed %.2f m/s  cadence %.0f steps/min  hip sway %.1f cm  bob %.1f cm  pelvis roll %.1f deg  pelvis yaw %.1f deg  hands out max %.2f m  step width min %.1f mean %.1f cm" % (
            clip, a, b, spd, 120 * sc.render.fps / (b - a), 100 * (max(lat) - min(lat)), 100 * (max(up) - min(up)), max(R) - min(R), max(Y) - min(Y), max(HO), 100 * min(Wd), 100 * sum(Wd) / len(Wd)))

        if os.environ.get("TANTRA_NORENDER"): continue
        cam = render_util.setup_stage()
        for o in bpy.data.objects:
            if o.name == "Floor": o.scale = (8, 8, 1)
        sc.render.resolution_x, sc.render.resolution_y = 300, 480
        allf = bool(os.environ.get("TANTRA_ALLFRAMES"))
        if allf: sc.render.resolution_x, sc.render.resolution_y = 240, 400
        for k in range(len(cyc) if allf else NSHOT):
            f = F[cyc[k]] if allf else F[cyc[int(k * len(cyc) / NSHOT)]]
            h = M(f, "Hips").translation
            cam.location = h + fwd * 4.6; cam.location.z = 1.0; cam.data.lens = 55
            cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector((h.x, h.y, 0.88)) - Vector(cam.location)).to_track_quat('-Z', 'Y')
            sc.frame_set(f)
            sc.render.filepath = os.path.join(OUT, ("all_%s_%02d.png" if allf else "%s_%d.png") % (clip, k)); bpy.ops.render.render(write_still=True)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
