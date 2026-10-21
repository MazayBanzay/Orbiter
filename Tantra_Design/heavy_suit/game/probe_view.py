import bpy, os, math
from mathutils import Vector as V
HERE = os.path.dirname(os.path.abspath(__file__)); sc = bpy.context.scene; sc.render.engine = 'BLENDER_EEVEE_NEXT'
if sc.world is None: sc.world = bpy.data.worlds.new('W')
sc.world.use_nodes = True; bg = next(n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.9, 0.5, 0.2, 1)
EYE = V((0.0, -0.118, 1.638))
cam_d = bpy.data.cameras.new("C"); cam_d.clip_start = 0.004; cam_d.angle = math.radians(85); cam = bpy.data.objects.new("C", cam_d); sc.collection.objects.link(cam); sc.camera = cam
cam.location = EYE; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = V((0, -1, 0)).to_track_quat('-Z', 'Y')
sc.render.resolution_x, sc.render.resolution_y = 640, 360
names = sorted(o.name for o in bpy.data.objects if o.type == 'MESH')
open(os.path.join(HERE, "probe_names.txt"), "w").write("\n".join(names))
for tag, hide in (("nohead", lambda n: any(k in n for k in ("female1605", "eyebrow", "eyelash", "low-poly", "toigo"))),
                  ("nohelm", lambda n: n.startswith("Helm") or any(k in n for k in ("female1605", "eyebrow", "eyelash", "low-poly", "toigo")))):
    for o in bpy.data.objects:
        if o.type == 'MESH': o.hide_render = hide(o.name)
    sc.render.filepath = os.path.join(HERE, "probe_" + tag + ".png"); bpy.ops.render.render(write_still=True)
bpy.ops.wm.quit_blender()
