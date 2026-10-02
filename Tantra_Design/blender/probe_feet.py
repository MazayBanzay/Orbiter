import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 700
for name, (loc, tgt) in {"fs_cu_feet": ((0.6, -0.8, 0.35), (0.0, 0.0, 0.15)), "fs_cu_feet_back": ((-0.5, 0.8, 0.35), (0.0, 0.0, 0.15))}.items():
    cam.location = loc; cam.data.lens = 60; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", name + ".png"); bpy.ops.render.render(write_still=True)
