import bpy, os
from mathutils import Vector
HERE = os.path.dirname(__file__); log = []
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_suit.blend"))
for o in bpy.data.objects:
    if o.type != 'MESH' or o.name.startswith("Astronavigator"): continue
    bad = []
    for p in o.data.polygons:
        c = o.matrix_world @ p.center
        if 0.95 < c.z < 1.15 and abs(c.x) < 0.12 and c.y < -0.05:
            n = p.normal
            if n.y > 0.3: bad.append((round(c.x, 3), round(c.y, 3), round(c.z, 3), round(n.y, 2)))
    if bad: log.append("%s: %d inward-facing front faces, e.g. %s" % (o.name, len(bad), bad[:6]))
body = next(o for o in bpy.data.objects if 'female1605' in o.name)
vis = [v for v in body.data.vertices if 0.95 < v.co.z < 1.15 and abs(v.co.x) < 0.1 and v.co.y < -0.05]
gi = body.vertex_groups["covered_by_suit"].index
log.append("body verts in belly zone %d, masked %d" % (len(vis), sum(1 for v in vis if any(g.group == gi for g in v.groups))))
suit = bpy.data.objects["SuitBody"]
front = [v.co for v in suit.data.vertices if 0.95 < v.co.z < 1.15 and abs(v.co.x) < 0.06]
log.append("suit belly min y %.3f, body belly min y %.3f" % (min(c.y for c in front), min(v.co.y for v in vis)))
open(os.path.join(HERE, "probe_blob.log"), "w").write("\n".join(log))
