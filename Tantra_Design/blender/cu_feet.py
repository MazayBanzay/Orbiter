# close-up of the feet of the game blend (rest pose), for the boot check
import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "astronavigator_crewsuit.blend"))
sc = bpy.context.scene; cam = render_util.setup_stage()
sc.render.resolution_x, sc.render.resolution_y = 700, 500
log = ["objs " + str([(o.name, o.hide_render) for o in bpy.data.objects if o.type == 'MESH'])]
shots = {"cu_feet_front": ((0.25, -0.75, 0.35), (0.0, -0.05, 0.06)), "cu_feet_side": ((0.75, -0.2, 0.25), (0.1, -0.05, 0.06)),
         "cu_feet_top": ((0.1, -0.45, 0.75), (0.1, -0.05, 0.05))}
for name, (loc, tgt) in shots.items():
    cam.location = loc; cam.data.lens = 50
    cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", name + ".png"); bpy.ops.render.render(write_still=True)
open(os.path.join(HERE, "cu_feet.log"), "w").write("\n".join(log))
