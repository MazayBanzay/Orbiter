# orthographic front and side of the crew suit (plain materials, no fabric pattern) for the golden-section sheet
import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x, sc.render.resolution_y = 700, 1100
sc.view_settings.exposure = -0.4
cam.data.type = 'ORTHO'; cam.data.ortho_scale = 2.0          # 2.0 m tall frame (the longer side), centre z 0.95
for nm, loc in (("gs_front", (0, -5, 0.95)), ("gs_side", (5, 0, 0.95)), ("gs_back", (0, 5, 0.95))):
    cam.location = loc; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0, 0, 0.95)) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
