import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 800
for o in bpy.data.objects:
    if o.type == 'MESH' and 'bob' in o.name: o.hide_render = True
for nm, loc, tgt in (("ep_back", (0.0, 0.75, 1.55), (0, 0.02, 1.38)), ("ep_side", (0.55, 0.05, 1.45), (0.12, 0.0, 1.38)), ("ep_top", (0.12, -0.05, 1.75), (0.12, 0.0, 1.38)), ("ep_topback", (0.0, 0.45, 1.75), (0.0, 0.0, 1.40))):
    cam.location = loc; cam.data.lens = 50; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
