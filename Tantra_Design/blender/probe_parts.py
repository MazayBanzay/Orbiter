import bpy, bmesh, os
import numpy as np
HERE = os.path.dirname(__file__)
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
cov = bpy.data.objects["Coverall"]; bm = bmesh.new(); bm.from_mesh(cov.data); bm.verts.ensure_lookup_table()
seen = set(); comps = []
for v in bm.verts:
    if v in seen: continue
    stack = [v]; comp = []
    while stack:
        u = stack.pop()
        if u in seen: continue
        seen.add(u); comp.append(u)
        for e in u.link_edges: stack.append(e.other_vert(u))
    comps.append(comp)
L = ["components: %d" % len(comps)]
for c in sorted(comps, key=lambda c: -len(c)):
    P = np.array([cov.matrix_world @ u.co for u in c])
    L.append("  %6d verts  x %.3f..%.3f  y %.3f..%.3f  z %.3f..%.3f" % (len(c), P[:,0].min(), P[:,0].max(), P[:,1].min(), P[:,1].max(), P[:,2].min(), P[:,2].max()))
open(os.path.join(HERE, "probe_parts.log"), "w").write("\n".join(L))
