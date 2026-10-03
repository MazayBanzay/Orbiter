import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
log = [o.name + " " + o.type + (" hidden" if o.hide_render else "") for o in bpy.data.objects]
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 500
for o in bpy.data.objects:
    if o.type == 'MESH' and any(k in o.name.lower() for k in ('hair', 'bob')): o.hide_render = True
cam.location = (0.0, 0.55, 1.62); cam.data.lens = 60; cam.rotation_mode = 'QUATERNION'
cam.rotation_quaternion = (Vector((0, 0.02, 1.43)) - cam.location).to_track_quat('-Z', 'Y')
sc.render.filepath = os.path.join(HERE, "renders", "probe_nohair.png"); bpy.ops.render.render(write_still=True)
open(os.path.join(HERE, "probe_collar.log"), "w").write("\n".join(log))
