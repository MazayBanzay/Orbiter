# Pantsir game asset: build P3 into the suit blend (not saved) and list the generated objects by name
import bpy, os, sys, traceback
from collections import defaultdict
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
OUT = os.path.join(HERE, "probe_build.log"); L = []
try:
    import pantsir_lib
    ns = pantsir_lib.load()
    ns["build"](); ns["set_open"](False); ns["set_variant"](3)
    dg = bpy.context.evaluated_depsgraph_get()
    stats = defaultdict(lambda: [0, 0, None, None, set()])
    tot = 0
    for o in ns["COLL"][0].objects:
        base = o.name.split(".")[0]
        if o.type == 'MESH':
            me = o.evaluated_get(dg).to_mesh(); nv = len(me.vertices)
            co = [o.matrix_world @ v.co for v in me.vertices]
            mn = [min(c[i] for c in co) for i in range(3)] if co else [0, 0, 0]; mx = [max(c[i] for c in co) for i in range(3)] if co else [0, 0, 0]
            o.evaluated_get(dg).to_mesh_clear()
        else: nv = 0; mn = mx = list(o.matrix_world.translation)
        s = stats[base]; s[0] += 1; s[1] += nv; tot += nv
        s[2] = mn if s[2] is None else [min(a, b) for a, b in zip(s[2], mn)]; s[3] = mx if s[3] is None else [max(a, b) for a, b in zip(s[3], mx)]
        s[4].update(m.name for m in (o.data.materials if o.type == 'MESH' else []) if m)
    L.append("total evaluated verts %d, objects %d" % (tot, len(ns["COLL"][0].objects)))
    for k in sorted(stats):
        n, nv, mn, mx, ms = stats[k]
        L.append("%-34s n=%-3d v=%-6d min(%.3f %.3f %.3f) max(%.3f %.3f %.3f) %s" % (k, n, nv, *mn, *mx, sorted(ms)))
    L.append("OPENABLE: " + ", ".join(sorted(set(o.name.split(".")[0] for o in ns["OPENABLE"]))))
    L.append("PARTS: " + ", ".join("%s=%d" % (k, len(v)) for k, v in ns["PARTS"].items()))
except Exception:
    L.append(traceback.format_exc())
open(OUT, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
