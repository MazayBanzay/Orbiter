import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
for o in bpy.data.objects:
    if o.type == 'MESH' and 'bob' in o.name: o.hide_render = True
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 800; sc.view_settings.exposure = -0.4
for nm, loc, tgt in (("arm_top", (0.25, -0.15, 1.95), (0.25, 0.0, 1.25)), ("arm_front", (0.35, -1.0, 1.35), (0.28, 0, 1.2)), ("arm_back", (0.35, 1.0, 1.35), (0.28, 0, 1.2))):
    cam.location = loc; cam.data.lens = 40; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
for nm, loc, tgt in (("arm_back2", (0.0, 1.1, 1.55), (0, 0, 1.35)),):
    cam.location = loc; cam.data.lens = 40; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
