import bpy, os
from mathutils import Vector as V
out=[]
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); out.append("arm mw %s" % [list(r) for r in arm.matrix_world])
for o in bpy.data.objects:
    if o.type=='MESH' and (o.name.startswith("HelmCapGd") or o.name.startswith("HelmCapGr") or "female1605" in o.name):
        dg=bpy.context.evaluated_depsgraph_get(); e=o.evaluated_get(dg); me=e.to_mesh()
        ps=[e.matrix_world @ v.co for v in me.vertices]
        out.append("%s min %s max %s" % (o.name, [round(min(p[i] for p in ps),3) for i in range(3)], [round(max(p[i] for p in ps),3) for i in range(3)]))
        e.to_mesh_clear()
open(os.path.join(os.path.dirname(os.path.abspath(__file__)),"probe.txt"),"w").write("\n".join(out)); bpy.ops.wm.quit_blender()
