import bpy, os
from mathutils import Vector
HERE = os.path.dirname(__file__); out = []
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_base.blend"))
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); body = next(o for o in bpy.data.objects if o.type == 'MESH' and 'female1605' in o.name)
out.append("arm mw %s" % [list(r) for r in arm.matrix_world]); out.append("body mw %s" % [list(r) for r in body.matrix_world])
out.append("shape keys: %s" % (body.data.shape_keys.key_blocks.keys() if body.data.shape_keys else None))
for b in ('upperarm_l','lowerarm_l','hand_l','index_03_l'):
    out.append("%s head %s" % (b, tuple(round(c,3) for c in arm.matrix_world @ arm.data.bones[b].head_local)))
xs = sorted(body.data.vertices, key=lambda v: -v.co.x)[:3]
out.append("max-x verts local %s" % [tuple(round(c,3) for c in v.co) for v in xs])
out.append("max-x verts world %s" % [tuple(round(c,3) for c in body.matrix_world @ v.co) for v in xs])
open(os.path.join(HERE, "probe_arm.log"), "w").write("\n".join(out))
