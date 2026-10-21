import bpy, os, math
from mathutils import Vector as V
HERE = os.path.dirname(os.path.abspath(__file__)); out = []
dg = bpy.context.evaluated_depsgraph_get()
EYE = V((0.0, -0.118, 1.638))
for ang in (0, 10, -10, 20, -20):
    for el in (0, 5, -5):
        d = V((math.sin(math.radians(ang)), -math.cos(math.radians(ang)), math.tan(math.radians(el)))).normalized()
        o = EYE.copy(); hits = []
        for k in range(6):
            ok, loc, n, idx, ob, M = bpy.context.scene.ray_cast(dg, o, d)
            if not ok: break
            hits.append("%s@%.3f" % (ob.name, (loc - EYE).length)); o = loc + d * 0.0005
        out.append("az %3d el %2d: %s" % (ang, el, ", ".join(hits)))
open(os.path.join(HERE, "probe_ray.txt"), "w").write("\n".join(out)); bpy.ops.wm.quit_blender()
