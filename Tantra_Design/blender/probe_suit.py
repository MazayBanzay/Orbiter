import bpy, os
HERE = os.path.dirname(__file__); log = []
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_base.blend"))
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
for b in arm.data.bones:
    h = arm.matrix_world @ b.head_local; t = arm.matrix_world @ b.tail_local
    log.append("%-16s head %6.3f %6.3f %6.3f  tail %6.3f %6.3f %6.3f" % (b.name, *h, *t))
for o in bpy.data.objects:
    if o.type == 'MESH':
        vs = [o.matrix_world @ v.co for v in o.data.vertices]
        if vs: log.append("%-40s v%6d hide_render %s  x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f mods %s" % (o.name, len(vs), o.hide_render, min(v.x for v in vs), max(v.x for v in vs), min(v.y for v in vs), max(v.y for v in vs), min(v.z for v in vs), max(v.z for v in vs), [m.type for m in o.modifiers]))
body = next(o for o in bpy.data.objects if o.type == 'MESH' and 'female1605' in o.name)
hv = [body.matrix_world @ v.co for v in body.data.vertices if v.groups and max(v.groups, key=lambda g: g.weight).group in [g.index for g in body.vertex_groups if g.name in ('Head','head')]]
log.append("head verts %d" % len(hv))
import math
for zc in (1.50, 1.55, 1.60, 1.65, 1.70):
    ring = [v for v in (body.matrix_world @ v.co for v in body.data.vertices) if abs(v.z - zc) < 0.01]
    if ring: log.append("z %.2f: x %.3f..%.3f  y %.3f..%.3f" % (zc, min(v.x for v in ring), max(v.x for v in ring), min(v.y for v in ring), max(v.y for v in ring)))
hair = [o for o in bpy.data.objects if 'bob' in o.name]
open(os.path.join(HERE, "probe_suit.log"), "w").write("\n".join(log))
