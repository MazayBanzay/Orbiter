import bpy, os
HERE = os.path.dirname(__file__); LOG = os.path.join(HERE, "probe_rig.log"); out = []
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_base.blend"))
for o in bpy.data.objects:
    out.append("%s %s parent=%s mods=%s" % (o.name, o.type, o.parent.name if o.parent else None, [m.type for m in getattr(o, 'modifiers', [])]))
    if o.type == 'ARMATURE':
        out.append("  bones: " + ", ".join("%s(%.2f)" % (b.name, (o.matrix_world @ b.head_local).z) for b in o.data.bones))
    if o.type == 'MESH' and 'female1605' in o.name:
        out.append("  vgroups: " + ", ".join(g.name for g in o.vertex_groups))
        out.append("  uv layers: %s, verts %d, materials %s" % ([u.name for u in o.data.uv_layers], len(o.data.vertices), [m.name for m in o.data.materials]))
open(LOG, "w", encoding="utf-8").write("\n".join(out))
