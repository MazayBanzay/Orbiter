# Renders a side-view strip of an animation blend (every N-th frame) for kinematics review, plus an MP4.
# Usage: run.ps1 -Script render_anim.py -Rest <clip> [every] [video 0/1]
import bpy, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLIP = argv[0] if argv else "35_01"; EVERY = int(argv[1]) if len(argv) > 1 else 6; VIDEO = (argv[2] == "1") if len(argv) > 2 else True
OUT = os.path.join(HERE, "renders", "anim_" + CLIP); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "render_anim_%s.log" % CLIP); log = []
try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "anim", "anim_%s.blend" % CLIP))
    sc = bpy.context.scene
    cam = render_util.setup_stage()
    for o in bpy.data.objects:
        if o.name == "Floor": o.scale = (6, 6, 1)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    sc.render.resolution_x, sc.render.resolution_y = 540, 720
    frames = list(range(sc.frame_start, sc.frame_end + 1))
    def hips(f):
        sc.frame_set(f); return (arm.matrix_world @ arm.pose.bones['Hips'].matrix).translation.copy()
    for f in frames[::EVERY]:
        h = hips(f)
        cam.location = (h.x + 4.2, h.y, 0.95); cam.data.lens = 50
        cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector((h.x, h.y, 0.9)) - Vector(cam.location)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(OUT, "f%03d.png" % f); bpy.ops.render.render(write_still=True)
    log.append("stills %d" % len(frames[::EVERY]))
    if VIDEO:
        # camera tracks the hips from the side
        for f in frames:
            h = hips(f); cam.location = (h.x + 4.2, h.y, 0.95); cam.keyframe_insert("location", frame=f)
            cam.rotation_quaternion = (Vector((h.x, h.y, 0.9)) - Vector(cam.location)).to_track_quat('-Z', 'Y'); cam.keyframe_insert("rotation_quaternion", frame=f)
        sc.render.image_settings.file_format = 'FFMPEG'; sc.render.ffmpeg.format = 'MPEG4'; sc.render.ffmpeg.codec = 'H264'
        sc.render.ffmpeg.constant_rate_factor = 'MEDIUM'
        sc.render.filepath = os.path.join(HERE, "renders", "anim_%s.mp4" % CLIP)
        bpy.ops.render.render(animation=True); log.append("video " + sc.render.filepath)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
