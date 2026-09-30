# Tantra: the capture next to our figure, frame for frame - is the retarget faithful? Left: the captured skeleton
# (as it was recorded, scaled to our leg length), right: our retargeted figure. Front and side views.
# Usage: run.ps1 -Script compare_source.py -Rest <clip>
import bpy, os, sys, traceback
from mathutils import Vector, Matrix
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLIP = argv[0] if argv else "Neutral_FW"
OUT = os.path.join(HERE, "renders", "src_" + CLIP); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "compare_source.log"); log = []
try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "anim", "anim_%s.blend" % CLIP))
    sc = bpy.context.scene; tgt = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    frames = list(sc["src_frames"]); ratio = float(sc["src_ratio"])
    bpy.ops.import_anim.bvh(filepath=sc["src_bvh"], global_scale=ratio, rotate_mode='NATIVE', use_fps_scale=False,
                            update_scene_fps=False, update_scene_duration=False, frame_start=1)
    src = bpy.context.object; src.data.display_type = 'STICK'; src.show_in_front = True
    # make the source armature visible in renders: a thin tube per bone
    bpy.ops.object.select_all(action='DESELECT')
    cam = render_util.setup_stage()
    for o in bpy.data.objects:
        if o.name == "Floor": o.scale = (10, 10, 1)
    sc.render.resolution_x, sc.render.resolution_y = 360, 480
    # bone tubes as curves hooked each frame
    tubes = []
    mat = bpy.data.materials.new("bone"); mat.diffuse_color = (0.9, 0.35, 0.1, 1)
    for pb in src.pose.bones:
        cu = bpy.data.curves.new(pb.name, 'CURVE'); cu.dimensions = '3D'; cu.bevel_depth = 0.018
        sp = cu.splines.new('POLY'); sp.points.add(1)
        ob = bpy.data.objects.new("tube_" + pb.name, cu); ob.data.materials.append(mat); sc.collection.objects.link(ob); tubes.append((ob, pb))
    for k in range(0, len(frames), max(1, len(frames) // 8)):
        f_src = frames[k]; sc.frame_set(k + 1)
        # our figure's hips this frame, and the source hips: place the skeleton 0.9 m to the side of her
        th = (tgt.matrix_world @ tgt.pose.bones['Hips'].head)
        sc.frame_set(f_src); bpy.context.view_layer.update()
        sh = src.matrix_world @ src.pose.bones['Hips'].head
        off = Vector((th.x + 0.9 - sh.x, th.y - sh.y, 0))
        for ob, pb in tubes:
            a = src.matrix_world @ pb.head + off; b = src.matrix_world @ pb.tail + off
            sp = ob.data.splines[0]; sp.points[0].co = (*a, 1); sp.points[1].co = (*b, 1)
        sc.frame_set(k + 1)
        for vn, d in (("front", Vector((0, -4.2, 0))), ("side", Vector((4.2, 0, 0)))):
            c = Vector((th.x + 0.45, th.y, 0.9)); cam.location = c + d + Vector((0, 0, 0.2)); cam.data.lens = 40
            cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (c - cam.location).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "%s_%02d.png" % (vn, k)); bpy.ops.render.render(write_still=True)
    log.append("rendered")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
