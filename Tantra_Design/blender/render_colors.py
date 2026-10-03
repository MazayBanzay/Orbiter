import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
COLS = {"anthracite": (0.033, 0.040, 0.047), "teal": (0.0, 0.085, 0.10), "red": (0.30, 0.012, 0.012)}
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
m = bpy.data.materials["SuitRed"]; b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x, sc.render.resolution_y = 600, 1000; sc.view_settings.exposure = -0.4
for k, c in COLS.items():
    b.inputs["Base Color"].default_value = (*c, 1)
    for nm, loc, tgt in (("f", (1.3, -2.6, 1.15), (0, 0, 1.0)), ("b", (-1.3, 2.6, 1.15), (0, 0, 1.0))):
        cam.location = loc; cam.data.lens = 50; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(HERE, "renders", "col_%s_%s.png" % (k, nm)); bpy.ops.render.render(write_still=True)
