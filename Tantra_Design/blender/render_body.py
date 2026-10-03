import bpy, os, sys
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
cam = render_util.setup_stage()
H = max((body.matrix_world @ v.co).z for v in body.data.vertices)
shots = render_util.standard_shots("body", H)[:3] + [("body_side", (3.9, -0.15, 0.6 * H), 0.54 * H, 70)]
render_util.shoot(cam, os.path.join(HERE, "renders"), shots)
