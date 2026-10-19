# Pantsir game asset: probe the astronavigator suit blend (read only) - objects, armature bones and rest joints
import bpy, os, traceback
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "probe_rig.log"); L = []
try:
    for o in bpy.data.objects:
        info = "%-28s %-9s hide_r=%d" % (o.name, o.type, o.hide_render)
        if o.type == 'MESH':
            info += " v=%d f=%d mats=%s vg=%d mods=%s parent=%s coll=%s" % (len(o.data.vertices), len(o.data.polygons), [m.name for m in o.data.materials][:6], len(o.vertex_groups), [m.type for m in o.modifiers], o.parent.name if o.parent else None, [c.name for c in o.users_collection])
        L.append(info)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    L.append("ARMATURE %s pose_position=%s matrix_world=%s" % (arm.name, arm.data.pose_position, [list(r) for r in arm.matrix_world]))
    for b in arm.data.bones:
        h = arm.matrix_world @ b.head_local; t = arm.matrix_world @ b.tail_local
        L.append("BONE %-18s parent=%-16s head=(%.4f %.4f %.4f) tail=(%.4f %.4f %.4f)" % (b.name, b.parent.name if b.parent else "-", h.x, h.y, h.z, t.x, t.y, t.z))
    for c in bpy.data.collections: L.append("COLL %s objs=%d" % (c.name, len(c.objects)))
except Exception:
    L.append(traceback.format_exc())
open(OUT, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
