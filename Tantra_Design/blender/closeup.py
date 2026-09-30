import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
sc = bpy.context.scene; cam = render_util.setup_stage()
sc.render.resolution_x, sc.render.resolution_y = 700, 700
shots = {"cu_nape": ((0.0, 0.55, 1.62), (0, 0.02, 1.43)), "cu_neck_side": ((0.45, -0.25, 1.55), (0.0, 0.0, 1.43)),
         "cu_wrist": ((0.75, -0.45, 0.95), (0.40, 0.0, 0.88))}
for name, (loc, tgt) in shots.items():
    cam.location = loc; cam.data.lens = 60
    cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", name + ".png"); bpy.ops.render.render(write_still=True)
