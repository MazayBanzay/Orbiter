# The approved figure without clothes: the body exactly as the approved coverall build left it (body_extras applied:
# bust cup 0.64, hips, legs, shoulders), the coverall and its skin mask removed. Output: astronavigator_body.blend.
import bpy, os
HERE = os.path.dirname(__file__); L = []
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
for o in [o for o in bpy.data.objects if o.name in ("Coverall",) or o.name.startswith(("Key", "Fill", "Rim", "Floor", "Cam", "Target"))]:
    bpy.data.objects.remove(o, do_unlink=True)
body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
for m in list(body.modifiers):
    if m.type == 'MASK' and m.name != "Hide helpers": body.modifiers.remove(m)
g = body.vertex_groups.get("covered_by_coverall")
if g: body.vertex_groups.remove(g)
for o in bpy.data.objects: L.append("%-45s %s %s" % (o.name, o.type, "hidden" if o.hide_render else ""))
L.append("body verts %d, shape keys %s, modifiers %s" % (len(body.data.vertices), bool(body.data.shape_keys), [m.type + ":" + m.name for m in body.modifiers]))
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
open(os.path.join(HERE, "save_body.log"), "w").write("\n".join(L))
