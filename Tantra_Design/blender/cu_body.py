import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
out = []
for blend, tag in (("astronavigator_body.blend", "nb"), ("astronavigator_coverall.blend", "nc")):
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, blend))
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 600
    for nm, loc, tgt in (("side", (0.8, -0.1, 1.27), (0, -0.08, 1.25)), ("34", (0.55, -0.65, 1.30), (0, -0.06, 1.24))):
        cam.location = loc; cam.data.lens = 60; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(HERE, "renders", "%s_%s.png" % (tag, nm)); bpy.ops.render.render(write_still=True)
