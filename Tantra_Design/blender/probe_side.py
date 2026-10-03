import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE)
sys.argv = [sys.argv[0], "--", "2"]
src = open(os.path.join(HERE, "concept_dx.py"), encoding="utf-8").read()
src = src[:src.index("    sc = bpy.context.scene; cam = render_util.setup_stage()")].replace("\ntry:\n", "\nif True:\n", 1)
exec(compile(src, "concept_dx", "exec"))
import render_util
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.view_settings.exposure = -0.6; sc.render.resolution_x = sc.render.resolution_y = 800
for nm, loc in (("ps_side", (2.0, 0.0, 0.95)), ("ps_back34", (1.4, 1.4, 1.0)), ("ps_front34", (1.4, -1.4, 1.0)), ("ps_cu", (0.75, 0.55, 0.95))):
    cam.location = loc; cam.data.lens = 40; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0.12, 0.0, 0.88) if nm == 'ps_cu' else (0, 0, 0.92)) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
open(os.path.join(HERE, "probe_side.log"), "w").write("\n".join(log))
